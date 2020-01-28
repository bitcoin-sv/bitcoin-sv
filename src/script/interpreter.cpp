// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2018-2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "interpreter.h"
#include "script_flags.h"

#include "crypto/ripemd160.h"
#include "crypto/sha1.h"
#include "crypto/sha256.h"
#include "primitives/transaction.h"
#include "pubkey.h"
#include "script/int_serialization.h"
#include "script/script.h"
#include "script/script_num.h"
#include "taskcancellation.h"
#include "uint256.h"
#include "consensus/consensus.h"
#include "script_config.h"

namespace {

inline bool set_success(ScriptError *ret) {
    if (ret) {
        *ret = SCRIPT_ERR_OK;
    }
    return true;
}

inline bool set_error(ScriptError *ret, const ScriptError serror) {
    if (ret) {
        *ret = serror;
    }
    return false;
}

} // namespace

inline uint8_t make_rshift_mask(size_t n) {
    static uint8_t mask[] = {0xFF, 0xFE, 0xFC, 0xF8, 0xF0, 0xE0, 0xC0, 0x80}; 
    return mask[n]; 
} 

inline uint8_t make_lshift_mask(size_t n) {
    static uint8_t mask[] = {0xFF, 0x7F, 0x3F, 0x1F, 0x0F, 0x07, 0x03, 0x01}; 
    return mask[n]; 
} 

// shift x right by n bits, implements OP_RSHIFT
static valtype RShift(const valtype &x, int n) {
    valtype::size_type bit_shift = n % 8;
    valtype::size_type byte_shift = n / 8;
 
    uint8_t mask = make_rshift_mask(bit_shift); 
    uint8_t overflow_mask = ~mask; 
 
    valtype result(x.size(), 0x00); 
    for (valtype::size_type i = 0; i < x.size(); i++) {
        valtype::size_type k = i + byte_shift;
        if (k < x.size()) {
            uint8_t val = (x[i] & mask); 
            val >>= bit_shift;
            result[k] |= val; 
        } 

        if (k + 1 < x.size()) {
            uint8_t carryval = (x[i] & overflow_mask); 
            carryval <<= 8 - bit_shift; 
            result[k + 1] |= carryval;
        } 
    } 
    return result; 
} 

// shift x left by n bits, implements OP_LSHIFT
static valtype LShift(const valtype &x, int n) {
    valtype::size_type bit_shift = n % 8;
    valtype::size_type byte_shift = n / 8;

    uint8_t mask = make_lshift_mask(bit_shift); 
    uint8_t overflow_mask = ~mask; 

    valtype result(x.size(), 0x00); 
    for (valtype::size_type index = x.size(); index > 0; index--) {
        valtype::size_type i = index - 1;
        // make sure that k is always >= 0
        if (byte_shift <= i)
        {
            valtype::size_type k = i - byte_shift;
            uint8_t val = (x[i] & mask);
            val <<= bit_shift;
            result[k] |= val;

            if (k >= 1) {
                uint8_t carryval = (x[i] & overflow_mask);
                carryval >>= 8 - bit_shift;
                result[k - 1] |= carryval;
            }
        }
    }
    return result; 
} 

bool CastToBool(const valtype &vch) {
    for (size_t i = 0; i < vch.size(); i++) {
        if (vch[i] != 0) {
            // Can be negative zero
            if (i == vch.size() - 1 && vch[i] == 0x80) {
                return false;
            }
            return true;
        }
    }
    return false;
}

static bool IsCompressedOrUncompressedPubKey(const valtype &vchPubKey) {
    if (vchPubKey.size() < 33) {
        //  Non-canonical public key: too short
        return false;
    }
    if (vchPubKey[0] == 0x04) {
        if (vchPubKey.size() != 65) {
            //  Non-canonical public key: invalid length for uncompressed key
            return false;
        }
    } else if (vchPubKey[0] == 0x02 || vchPubKey[0] == 0x03) {
        if (vchPubKey.size() != 33) {
            //  Non-canonical public key: invalid length for compressed key
            return false;
        }
    } else {
        //  Non-canonical public key: neither compressed nor uncompressed
        return false;
    }
    return true;
}

static bool IsCompressedPubKey(const valtype &vchPubKey) {
    if (vchPubKey.size() != 33) {
        //  Non-canonical public key: invalid length for compressed key
        return false;
    }
    if (vchPubKey[0] != 0x02 && vchPubKey[0] != 0x03) {
        //  Non-canonical public key: invalid prefix for compressed key
        return false;
    }
    return true;
}

/**
 * A canonical signature exists of: <30> <total len> <02> <len R> <R> <02> <len
 * S> <S> <hashtype>, where R and S are not negative (their first byte has its
 * highest bit not set), and not excessively padded (do not start with a 0 byte,
 * unless an otherwise negative number follows, in which case a single 0 byte is
 * necessary and even required).
 *
 * See https://bitcointalk.org/index.php?topic=8392.msg127623#msg127623
 *
 * This function is consensus-critical since BIP66.
 */
static bool IsValidSignatureEncoding(const std::vector<uint8_t> &sig) {
    // Format: 0x30 [total-length] 0x02 [R-length] [R] 0x02 [S-length] [S]
    // [sighash]
    // * total-length: 1-byte length descriptor of everything that follows,
    // excluding the sighash byte.
    // * R-length: 1-byte length descriptor of the R value that follows.
    // * R: arbitrary-length big-endian encoded R value. It must use the
    // shortest possible encoding for a positive integers (which means no null
    // bytes at the start, except a single one when the next byte has its
    // highest bit set).
    // * S-length: 1-byte length descriptor of the S value that follows.
    // * S: arbitrary-length big-endian encoded S value. The same rules apply.
    // * sighash: 1-byte value indicating what data is hashed (not part of the
    // DER signature)

    // Minimum and maximum size constraints.
    if (sig.size() < 9) return false;
    if (sig.size() > 73) return false;

    // A signature is of type 0x30 (compound).
    if (sig[0] != 0x30) return false;

    // Make sure the length covers the entire signature.
    if (sig[1] != sig.size() - 3) return false;

    // Extract the length of the R element.
    unsigned int lenR = sig[3];

    // Make sure the length of the S element is still inside the signature.
    if (5 + lenR >= sig.size()) return false;

    // Extract the length of the S element.
    unsigned int lenS = sig[5 + lenR];

    // Verify that the length of the signature matches the sum of the length
    // of the elements.
    if ((size_t)(lenR + lenS + 7) != sig.size()) return false;

    // Check whether the R element is an integer.
    if (sig[2] != 0x02) return false;

    // Zero-length integers are not allowed for R.
    if (lenR == 0) return false;

    // Negative numbers are not allowed for R.
    if (sig[4] & 0x80) return false;

    // Null bytes at the start of R are not allowed, unless R would otherwise be
    // interpreted as a negative number.
    if (lenR > 1 && (sig[4] == 0x00) && !(sig[5] & 0x80)) return false;

    // Check whether the S element is an integer.
    if (sig[lenR + 4] != 0x02) return false;

    // Zero-length integers are not allowed for S.
    if (lenS == 0) return false;

    // Negative numbers are not allowed for S.
    if (sig[lenR + 6] & 0x80) return false;

    // Null bytes at the start of S are not allowed, unless S would otherwise be
    // interpreted as a negative number.
    if (lenS > 1 && (sig[lenR + 6] == 0x00) && !(sig[lenR + 7] & 0x80)) {
        return false;
    }

    return true;
}

static bool IsLowDERSignature(const valtype &vchSig, ScriptError *serror) {
    if (!IsValidSignatureEncoding(vchSig)) {
        return set_error(serror, SCRIPT_ERR_SIG_DER);
    }
    std::vector<uint8_t> vchSigCopy(vchSig.begin(),
                                    vchSig.begin() + vchSig.size() - 1);
    if (!CPubKey::CheckLowS(vchSigCopy)) {
        return set_error(serror, SCRIPT_ERR_SIG_HIGH_S);
    }
    return true;
}

static SigHashType GetHashType(const valtype &vchSig) {
    if (vchSig.size() == 0) {
        return SigHashType(0);
    }

    return SigHashType(vchSig[vchSig.size() - 1]);
}

static void CleanupScriptCode(CScript &scriptCode,
                              const std::vector<uint8_t> &vchSig,
                              uint32_t flags) {
    // Drop the signature in scripts when SIGHASH_FORKID is not used.
    SigHashType sigHashType = GetHashType(vchSig);
    if (!(flags & SCRIPT_ENABLE_SIGHASH_FORKID) || !sigHashType.hasForkId()) {
        scriptCode.FindAndDelete(CScript(vchSig));
    }
}

bool CheckSignatureEncoding(const std::vector<uint8_t> &vchSig, uint32_t flags,
                            ScriptError *serror) {
    // Empty signature. Not strictly DER encoded, but allowed to provide a
    // compact way to provide an invalid signature for use with CHECK(MULTI)SIG
    if (vchSig.size() == 0) {
        return true;
    }
    if ((flags & (SCRIPT_VERIFY_DERSIG | SCRIPT_VERIFY_LOW_S |
                  SCRIPT_VERIFY_STRICTENC)) != 0 &&
        !IsValidSignatureEncoding(vchSig)) {
        return set_error(serror, SCRIPT_ERR_SIG_DER);
    }
    if ((flags & SCRIPT_VERIFY_LOW_S) != 0 &&
        !IsLowDERSignature(vchSig, serror)) {
        // serror is set
        return false;
    }
    if ((flags & SCRIPT_VERIFY_STRICTENC) != 0) {
        if (!GetHashType(vchSig).isDefined()) {
            return set_error(serror, SCRIPT_ERR_SIG_HASHTYPE);
        }
        bool usesForkId = GetHashType(vchSig).hasForkId();
        bool forkIdEnabled = flags & SCRIPT_ENABLE_SIGHASH_FORKID;
        if (!forkIdEnabled && usesForkId) {
            return set_error(serror, SCRIPT_ERR_ILLEGAL_FORKID);
        }
        if (forkIdEnabled && !usesForkId) {
            return set_error(serror, SCRIPT_ERR_MUST_USE_FORKID);
        }
    }
    return true;
}

static bool CheckPubKeyEncoding(const valtype &vchPubKey, uint32_t flags,
                                ScriptError *serror) {
    if ((flags & SCRIPT_VERIFY_STRICTENC) != 0 &&
        !IsCompressedOrUncompressedPubKey(vchPubKey)) {
        return set_error(serror, SCRIPT_ERR_PUBKEYTYPE);
    }
    // Only compressed keys are accepted when
    // SCRIPT_VERIFY_COMPRESSED_PUBKEYTYPE is enabled.
    if (flags & SCRIPT_VERIFY_COMPRESSED_PUBKEYTYPE &&
        !IsCompressedPubKey(vchPubKey)) {
        return set_error(serror, SCRIPT_ERR_NONCOMPRESSED_PUBKEY);
    }
    return true;
}

static bool CheckMinimalPush(const valtype &data, opcodetype opcode) {
    if (data.size() == 0) {
        // Could have used OP_0.
        return opcode == OP_0;
    }
    if (data.size() == 1 && data[0] >= 1 && data[0] <= 16) {
        // Could have used OP_1 .. OP_16.
        return opcode == OP_1 + (data[0] - 1);
    }
    if (data.size() == 1 && data[0] == 0x81) {
        // Could have used OP_1NEGATE.
        return opcode == OP_1NEGATE;
    }
    if (data.size() <= 75) {
        // Could have used a direct push (opcode indicating number of bytes
        // pushed + those bytes).
        return opcode == data.size();
    }
    if (data.size() <= 255) {
        // Could have used OP_PUSHDATA.
        return opcode == OP_PUSHDATA1;
    }
    if (data.size() <= 65535) {
        // Could have used OP_PUSHDATA2.
        return opcode == OP_PUSHDATA2;
    }
    return true;
}

static bool IsOpcodeDisabled(opcodetype opcode) {
    switch (opcode) {
        case OP_2MUL:
        case OP_2DIV:
            // Disabled opcodes.
            return true;

        default:
            break;
    }

    return false;
}

static bool IsInvalidBranchingOpcode(opcodetype opcode) {
    return opcode == OP_VERNOTIF || opcode == OP_VERIF;
}

inline bool IsValidMaxOpsPerScript(uint64_t nOpCount,
                                   const CScriptConfig &config,
                                   bool isGenesisEnabled, bool consensus)
{
    return (nOpCount <= config.GetMaxOpsPerScript(isGenesisEnabled, consensus));
}

std::optional<bool> EvalScript(
    const CScriptConfig& config,
    bool consensus,
    const task::CCancellationToken& token,
    LimitedStack& stack,
    const CScript& script,
    uint32_t flags,
    const BaseSignatureChecker& checker,
    ScriptError* serror)
{
    static const CScriptNum bnZero(0);
    static const CScriptNum bnOne(1);
    static const valtype vchFalse(0);
    static const valtype vchTrue(1, 1);

    CScript::const_iterator pc = script.begin();
    CScript::const_iterator pend = script.end();
    CScript::const_iterator pbegincodehash = script.begin();
    opcodetype opcode;
    valtype vchPushValue;
    std::vector<bool> vfExec;
    std::vector<bool> vfElse;

    // altstack shares memory with stack
    LimitedStack altstack {stack.makeChildStack()};
    set_error(serror, SCRIPT_ERR_UNKNOWN_ERROR);

    const bool utxo_after_genesis{(flags & SCRIPT_UTXO_AFTER_GENESIS) != 0};
    const uint64_t maxScriptNumLength = config.GetMaxScriptNumLength(utxo_after_genesis, consensus);

    if(script.size() > config.GetMaxScriptSize(utxo_after_genesis, consensus))
    {
        return set_error(serror, SCRIPT_ERR_SCRIPT_SIZE);
    }
    uint64_t nOpCount = 0;
    const bool fRequireMinimal = (flags & SCRIPT_VERIFY_MINIMALDATA) != 0;

    // if OP_RETURN is found in executed branches after genesis is activated,
    // we still have to check if the rest of the script is valid
    bool nonTopLevelReturnAfterGenesis = false;
    
    try {
        while (pc < pend) {
            if (token.IsCanceled())
            {
                return {};
            }

            //
            // Read instruction
            //
            if (!script.GetOp(pc, opcode, vchPushValue)) {
                return set_error(serror, SCRIPT_ERR_BAD_OPCODE);
            }

            if (!utxo_after_genesis && (vchPushValue.size() > MAX_SCRIPT_ELEMENT_SIZE_BEFORE_GENESIS))
            {
                return set_error(serror, SCRIPT_ERR_PUSH_SIZE);
            }

            // Do not execute instructions if Genesis OP_RETURN was found in executed branches.
            bool fExec = !count(vfExec.begin(), vfExec.end(), false) && (!nonTopLevelReturnAfterGenesis || opcode == OP_RETURN);

            //
            // Check opcode limits.
            //
            // Push values are not taken into consideration.
            // Note how OP_RESERVED does not count towards the opcode limit.
            if ((opcode > OP_16) && !IsValidMaxOpsPerScript(++nOpCount, config, utxo_after_genesis, consensus)) {
                return set_error(serror, SCRIPT_ERR_OP_COUNT);
            }

            // Some opcodes are disabled.
            if (IsOpcodeDisabled(opcode) && (!utxo_after_genesis || fExec )) {
                return set_error(serror, SCRIPT_ERR_DISABLED_OPCODE);
            }

            if (fExec && 0 <= opcode && opcode <= OP_PUSHDATA4) {
                if (fRequireMinimal &&
                    !CheckMinimalPush(vchPushValue, opcode)) {
                    return set_error(serror, SCRIPT_ERR_MINIMALDATA);
                }
                stack.push_back(vchPushValue);
            } else if (fExec || (OP_IF <= opcode && opcode <= OP_ENDIF)) {
                switch (opcode) {
                    //
                    // Push value
                    //
                    case OP_1NEGATE:
                    case OP_1:
                    case OP_2:
                    case OP_3:
                    case OP_4:
                    case OP_5:
                    case OP_6:
                    case OP_7:
                    case OP_8:
                    case OP_9:
                    case OP_10:
                    case OP_11:
                    case OP_12:
                    case OP_13:
                    case OP_14:
                    case OP_15:
                    case OP_16: {
                        // ( -- value)
                        CScriptNum bn((int)opcode - (int)(OP_1 - 1));
                        stack.push_back(bn.getvch());
                        // The result of these opcodes should always be the
                        // minimal way to push the data they push, so no need
                        // for a CheckMinimalPush here.
                    } break;

                    //
                    // Control
                    //
                    case OP_NOP:
                        break;

                    case OP_CHECKLOCKTIMEVERIFY: {
                        if (!(flags & SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY) || utxo_after_genesis) {
                            // not enabled; treat as a NOP2
                            if (flags &
                                SCRIPT_VERIFY_DISCOURAGE_UPGRADABLE_NOPS) {
                                return set_error(
                                    serror,
                                    SCRIPT_ERR_DISCOURAGE_UPGRADABLE_NOPS);
                            }
                            break;
                        }

                        if (stack.size() < 1) {
                            return set_error(
                                serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                        }

                        // Note that elsewhere numeric opcodes are limited to
                        // operands in the range -2**31+1 to 2**31-1, however it
                        // is legal for opcodes to produce results exceeding
                        // that range. This limitation is implemented by
                        // CScriptNum's default 4-byte limit.
                        //
                        // If we kept to that limit we'd have a year 2038
                        // problem, even though the nLockTime field in
                        // transactions themselves is uint32 which only becomes
                        // meaningless after the year 2106.
                        //
                        // Thus as a special case we tell CScriptNum to accept
                        // up to 5-byte bignums, which are good until 2**39-1,
                        // well beyond the 2**32-1 limit of the nLockTime field
                        // itself.
                        const CScriptNum nLockTime(stack.stacktop(-1).GetElement(),
                                                   fRequireMinimal, 5);

                        // In the rare event that the argument may be < 0 due to
                        // some arithmetic being done first, you can always use
                        // 0 MAX CHECKLOCKTIMEVERIFY.
                        if (nLockTime < 0) {
                            return set_error(serror,
                                             SCRIPT_ERR_NEGATIVE_LOCKTIME);
                        }

                        // Actually compare the specified lock time with the
                        // transaction.
                        if (!checker.CheckLockTime(nLockTime)) {
                            return set_error(serror,
                                             SCRIPT_ERR_UNSATISFIED_LOCKTIME);
                        }

                        break;
                    }

                    case OP_CHECKSEQUENCEVERIFY: {
                        if (!(flags & SCRIPT_VERIFY_CHECKSEQUENCEVERIFY) || utxo_after_genesis) {
                            // not enabled; treat as a NOP3
                            if (flags &
                                SCRIPT_VERIFY_DISCOURAGE_UPGRADABLE_NOPS) {
                                return set_error(
                                    serror,
                                    SCRIPT_ERR_DISCOURAGE_UPGRADABLE_NOPS);
                            }
                            break;
                        }

                        if (stack.size() < 1) {
                            return set_error(
                                serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                        }

                        // nSequence, like nLockTime, is a 32-bit unsigned
                        // integer field. See the comment in CHECKLOCKTIMEVERIFY
                        // regarding 5-byte numeric operands.
                        const CScriptNum nSequence(stack.stacktop(-1).GetElement(),
                                                   fRequireMinimal, 5);

                        // In the rare event that the argument may be < 0 due to
                        // some arithmetic being done first, you can always use
                        // 0 MAX CHECKSEQUENCEVERIFY.
                        if (nSequence < 0) {
                            return set_error(serror,
                                             SCRIPT_ERR_NEGATIVE_LOCKTIME);
                        }

                        // To provide for future soft-fork extensibility, if the
                        // operand has the disabled lock-time flag set,
                        // CHECKSEQUENCEVERIFY behaves as a NOP.
                        if ((nSequence &
                             CTxIn::SEQUENCE_LOCKTIME_DISABLE_FLAG) != bnZero) {
                            break;
                        }

                        // Compare the specified sequence number with the input.
                        if (!checker.CheckSequence(nSequence)) {
                            return set_error(serror,
                                             SCRIPT_ERR_UNSATISFIED_LOCKTIME);
                        }

                        break;
                    }

                    case OP_NOP1:
                    case OP_NOP4:
                    case OP_NOP5:
                    case OP_NOP6:
                    case OP_NOP7:
                    case OP_NOP8:
                    case OP_NOP9:
                    case OP_NOP10: {
                        if (flags & SCRIPT_VERIFY_DISCOURAGE_UPGRADABLE_NOPS) {
                            return set_error(
                                serror, SCRIPT_ERR_DISCOURAGE_UPGRADABLE_NOPS);
                        }
                    } break;

                    case OP_IF:
                    case OP_NOTIF: {
                        // <expression> if [statements] [else [statements]]
                        // endif
                        bool fValue = false;
                        if (fExec) {
                            if (stack.size() < 1) {
                                return set_error(
                                    serror, SCRIPT_ERR_UNBALANCED_CONDITIONAL);
                            }
                            LimitedVector &vch = stack.stacktop(-1);
                            if (flags & SCRIPT_VERIFY_MINIMALIF) {
                                if (vch.size() > 1) {
                                    return set_error(serror,
                                                     SCRIPT_ERR_MINIMALIF);
                                }
                                if (vch.size() == 1 && vch[0] != 1) {
                                    return set_error(serror,
                                                     SCRIPT_ERR_MINIMALIF);
                                }
                            }
                            fValue = CastToBool(vch.GetElement());
                            if (opcode == OP_NOTIF) {
                                fValue = !fValue;
                            }
                            stack.pop_back();
                        }
                        vfExec.push_back(fValue);
                        vfElse.push_back(false);
                    } break;

                    case OP_ELSE: {
                        // Only one ELSE is allowed in IF after genesis.
                        if (vfExec.empty() || (vfElse.back() && utxo_after_genesis)) {
                            return set_error(serror,
                                             SCRIPT_ERR_UNBALANCED_CONDITIONAL);
                        }
                        vfExec.back() = !vfExec.back();
                        vfElse.back() = true;
                    } break;

                    case OP_ENDIF: {
                        if (vfExec.empty()) {
                            return set_error(serror,
                                             SCRIPT_ERR_UNBALANCED_CONDITIONAL);
                        }
                        vfExec.pop_back();
                        vfElse.pop_back();
                    } break;

                    case OP_VERIFY: {
                        // (true -- ) or
                        // (false -- false) and return
                        if (stack.size() < 1) {
                            return set_error(
                                serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                        }
                        bool fValue = CastToBool(stack.stacktop(-1).GetElement());
                        if (fValue) {
                            stack.pop_back();
                        } else {
                            return set_error(serror, SCRIPT_ERR_VERIFY);
                        }
                    } break;

                    case OP_RETURN: {
                        if (utxo_after_genesis) {
                            if (vfExec.empty()) {
                                // Terminate the execution as successful. The remaining of the script does not affect the validity (even in
                                // presence of unbalanced IFs, invalid opcodes etc)
                                return set_success(serror);
                            }

                            // op_return encountered inside if statement after genesis --> check for invalid grammar
                            nonTopLevelReturnAfterGenesis = true;
                        } else {
                            // Pre-Genesis OP_RETURN marks script as invalid
                            return set_error(serror, SCRIPT_ERR_OP_RETURN);
                        }
                    } break;

                    //
                    // Stack ops
                    //
                    case OP_TOALTSTACK: {
                        if (stack.size() < 1) {
                            return set_error(
                                serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                        }
                        altstack.moveTopToStack(stack);
                    } break;

                    case OP_FROMALTSTACK: {
                        if (altstack.size() < 1) {
                            return set_error(
                                serror, SCRIPT_ERR_INVALID_ALTSTACK_OPERATION);
                        }
                        stack.moveTopToStack(altstack);
                    } break;

                    case OP_2DROP: {
                        // (x1 x2 -- )
                        if (stack.size() < 2) {
                            return set_error(
                                serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                        }
                        stack.pop_back();
                        stack.pop_back();
                    } break;

                    case OP_2DUP: {
                        // (x1 x2 -- x1 x2 x1 x2)
                        if (stack.size() < 2) {
                            return set_error(
                                serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                        }
                        LimitedVector vch1 = stack.stacktop(-2);
                        LimitedVector vch2 = stack.stacktop(-1);
                        stack.push_back(vch1);
                        stack.push_back(vch2);
                    } break;

                    case OP_3DUP: {
                        // (x1 x2 x3 -- x1 x2 x3 x1 x2 x3)
                        if (stack.size() < 3) {
                            return set_error(
                                serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                        }
                        LimitedVector vch1 = stack.stacktop(-3);
                        LimitedVector vch2 = stack.stacktop(-2);
                        LimitedVector vch3 = stack.stacktop(-1);
                        stack.push_back(vch1);
                        stack.push_back(vch2);
                        stack.push_back(vch3);
                    } break;

                    case OP_2OVER: {
                        // (x1 x2 x3 x4 -- x1 x2 x3 x4 x1 x2)
                        if (stack.size() < 4) {
                            return set_error(
                                serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                        }
                        LimitedVector vch1 = stack.stacktop(-4);
                        LimitedVector vch2 = stack.stacktop(-3);
                        stack.push_back(vch1);
                        stack.push_back(vch2);
                    } break;

                    case OP_2ROT: {
                        // (x1 x2 x3 x4 x5 x6 -- x3 x4 x5 x6 x1 x2)
                        if (stack.size() < 6) {
                            return set_error(
                                serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                        }
                        LimitedVector vch1 = stack.stacktop(-6);
                        LimitedVector vch2 = stack.stacktop(-5);
                        stack.erase(- 6, - 4);
                        stack.push_back(vch1);
                        stack.push_back(vch2);
                    } break;

                    case OP_2SWAP: {
                        // (x1 x2 x3 x4 -- x3 x4 x1 x2)
                        if (stack.size() < 4) {
                            return set_error(
                                serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                        }
                        stack.swapElements(stack.size() - 4, stack.size() - 2);
                        stack.swapElements(stack.size() - 3, stack.size() - 1);
                    } break;

                    case OP_IFDUP: {
                        // (x - 0 | x x)
                        if (stack.size() < 1) {
                            return set_error(
                                serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                        }
                        LimitedVector vch = stack.stacktop(-1);
                        if (CastToBool(vch.GetElement())) {
                            stack.push_back(vch);
                        }
                    } break;

                    case OP_DEPTH: {
                        // -- stacksize
                        const CScriptNum bn(bsv::bint{stack.size()});
                        stack.push_back(bn.getvch());
                    } break;

                    case OP_DROP: {
                        // (x -- )
                        if (stack.size() < 1) {
                            return set_error(
                                serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                        }
                        stack.pop_back();
                    } break;

                    case OP_DUP: {
                        // (x -- x x)
                        if (stack.size() < 1) {
                            return set_error(
                                serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                        }
                        LimitedVector vch = stack.stacktop(-1);
                        stack.push_back(vch);
                    } break;

                    case OP_NIP: {
                        // (x1 x2 -- x2)
                        if (stack.size() < 2) {
                            return set_error(
                                serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                        }
                        stack.erase(-2);
                    } break;

                    case OP_OVER: {
                        // (x1 x2 -- x1 x2 x1)
                        if (stack.size() < 2) {
                            return set_error(
                                serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                        }
                        LimitedVector vch = stack.stacktop(-2);
                        stack.push_back(vch);
                    } break;

                    case OP_PICK:
                    case OP_ROLL: {
                        // (xn ... x2 x1 x0 n - xn ... x2 x1 x0 xn)
                        // (xn ... x2 x1 x0 n - ... x2 x1 x0 xn)
                        if (stack.size() < 2) {
                            return set_error(
                                serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                        }

                        const auto& top{stack.stacktop(-1).GetElement()};
                        const CScriptNum sn{
                            top, fRequireMinimal,
                            maxScriptNumLength,
                            utxo_after_genesis};
                        stack.pop_back();
                        if(sn < 0 || sn >= stack.size())
                        {
                            return set_error(
                                serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                        }
                        const auto n{sn.to_size_t_limited()};
                        LimitedVector vch = stack.stacktop(-n - 1);

                        if (opcode == OP_ROLL) {
                            stack.erase(- n - 1);
                        }
                        stack.push_back(vch);
                    } break;

                    case OP_ROT: {
                        // (x1 x2 x3 -- x2 x3 x1)
                        //  x2 x1 x3  after first swap
                        //  x2 x3 x1  after second swap
                        if (stack.size() < 3) {
                            return set_error(
                                serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                        }
                        stack.swapElements(stack.size() - 3, stack.size() - 2);
                        stack.swapElements(stack.size() - 2, stack.size() - 1);
                    } break;

                    case OP_SWAP: {
                        // (x1 x2 -- x2 x1)
                        if (stack.size() < 2) {
                            return set_error(
                                serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                        }
                        stack.swapElements(stack.size() - 2, stack.size() - 1);
                    } break;

                    case OP_TUCK: {
                        // (x1 x2 -- x2 x1 x2)
                        if (stack.size() < 2) {
                            return set_error(
                                serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                        }
                        LimitedVector vch = stack.stacktop(-1);
                        stack.insert(-2, vch);
                    } break;

                    case OP_SIZE: {
                        // (in -- in size)
                        if (stack.size() < 1) {
                            return set_error(
                                serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                        }

                        CScriptNum bn(bsv::bint{stack.stacktop(-1).size()});
                        stack.push_back(bn.getvch());
                    } break;

                    //
                    // Bitwise logic
                    //
                    case OP_AND:
                    case OP_OR:
                    case OP_XOR: {
                        // (x1 x2 - out)
                        if (stack.size() < 2) {
                            return set_error(
                                serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                        }
                        LimitedVector &vch1 = stack.stacktop(-2);
                        LimitedVector &vch2 = stack.stacktop(-1);

                        // Inputs must be the same size
                        if (vch1.size() != vch2.size()) {
                            return set_error(serror,
                                             SCRIPT_ERR_INVALID_OPERAND_SIZE);
                        }

                        // To avoid allocating, we modify vch1 in place.
                        switch (opcode) {
                            case OP_AND:
                                for (size_t i = 0; i < vch1.size(); ++i) {
                                    vch1[i] &= vch2[i];
                                }
                                break;
                            case OP_OR:
                                for (size_t i = 0; i < vch1.size(); ++i) {
                                    vch1[i] |= vch2[i];
                                }
                                break;
                            case OP_XOR:
                                for (size_t i = 0; i < vch1.size(); ++i) {
                                    vch1[i] ^= vch2[i];
                                }
                                break;
                            default:
                                break;
                        }

                        // And pop vch2.
                        stack.pop_back();
                    } break;

                    case OP_INVERT: {
                        // (x -- out)
                        if (stack.size() < 1) {
                            return set_error(serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                        }
                        LimitedVector &vch1 = stack.stacktop(-1);
                        // To avoid allocating, we modify vch1 in place
                        for(size_t i=0; i<vch1.size(); i++)
                        {
                            vch1[i] = ~vch1[i];
                        }
                    } break;

                    case OP_LSHIFT:
                    {
                        // (x n -- out)
                        if(stack.size() < 2)
                        {
                            return set_error(
                                serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                        }

                        const LimitedVector vch1 = stack.stacktop(-2);
                        const auto& top{stack.stacktop(-1).GetElement()};
                        CScriptNum n{top, fRequireMinimal, maxScriptNumLength,
                                     utxo_after_genesis};
                        if(n < 0)
                        {
                            return set_error(serror,
                                             SCRIPT_ERR_INVALID_NUMBER_RANGE);
                        }

                        stack.pop_back();
                        stack.pop_back();
                        auto values{vch1.GetElement()};
                        do
                        {
                            values = LShift(values, n.getint());
                            n -= utxo_after_genesis
                                     ? CScriptNum{bsv::bint{INT32_MAX}}
                                     : CScriptNum{INT32_MAX};
                        } while(n > 0);

                        stack.push_back(values);
                    }
                    break;

                    case OP_RSHIFT:
                    {
                        // (x n -- out)
                        if(stack.size() < 2)
                        {
                            return set_error(
                                serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                        }

                        const LimitedVector vch1 = stack.stacktop(-2);
                        const auto& top{stack.stacktop(-1).GetElement()};
                        CScriptNum n{top, fRequireMinimal, maxScriptNumLength,
                                     utxo_after_genesis};
                        if(n < 0)
                        {
                            return set_error(serror,
                                             SCRIPT_ERR_INVALID_NUMBER_RANGE);
                        }

                        stack.pop_back();
                        stack.pop_back();
                        auto values{vch1.GetElement()};
                        do
                        {
                            values = RShift(values, n.getint());
                            n -= utxo_after_genesis
                                     ? CScriptNum{bsv::bint{INT32_MAX}}
                                     : CScriptNum{INT32_MAX};
                        } while(n > 0);

                        stack.push_back(values);
                    }
                    break;

                    case OP_EQUAL:
                    case OP_EQUALVERIFY:
                        // case OP_NOTEQUAL: // use OP_NUMNOTEQUAL
                        {
                            // (x1 x2 - bool)
                            if (stack.size() < 2) {
                                return set_error(
                                    serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                            }
                            LimitedVector &vch1 = stack.stacktop(-2);
                            LimitedVector &vch2 = stack.stacktop(-1);

                            bool fEqual = (vch1.GetElement() == vch2.GetElement());
                            // OP_NOTEQUAL is disabled because it would be too
                            // easy to say something like n != 1 and have some
                            // wiseguy pass in 1 with extra zero bytes after it
                            // (numerically, 0x01 == 0x0001 == 0x000001)
                            // if (opcode == OP_NOTEQUAL)
                            //    fEqual = !fEqual;
                            stack.pop_back();
                            stack.pop_back();
                            stack.push_back(fEqual ? vchTrue : vchFalse);
                            if (opcode == OP_EQUALVERIFY) {
                                if (fEqual) {
                                    stack.pop_back();
                                } else {
                                    return set_error(serror,
                                                     SCRIPT_ERR_EQUALVERIFY);
                                }
                            }
                        }
                        break;

                    //
                    // Numeric
                    //
                    case OP_1ADD:
                    case OP_1SUB:
                    case OP_NEGATE:
                    case OP_ABS:
                    case OP_NOT:
                    case OP_0NOTEQUAL: {
                        // (in -- out)
                        if (stack.size() < 1) {
                            return set_error(
                                serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                        }
                        const auto &top{stack.stacktop(-1).GetElement()};
                        CScriptNum bn{top, fRequireMinimal,
                                      maxScriptNumLength,
                                      utxo_after_genesis};
                        switch (opcode) {
                            case OP_1ADD:
                                bn += utxo_after_genesis
                                          ? CScriptNum{bsv::bint{1}}
                                          : bnOne;
                                break;
                            case OP_1SUB:
                                bn -= utxo_after_genesis
                                          ? CScriptNum{bsv::bint{1}}
                                          : bnOne;
                                // bn -= bnOne;
                                break;
                            case OP_NEGATE:
                                bn = -bn;
                                break;
                            case OP_ABS:
                                if (bn < bnZero) {
                                    bn = -bn;
                                }
                                break;
                            case OP_NOT:
                                bn = (bn == bnZero);
                                break;
                            case OP_0NOTEQUAL:
                                bn = (bn != bnZero);
                                break;
                            default:
                                assert(!"invalid opcode");
                                break;
                        }
                        stack.pop_back();
                        stack.push_back(bn.getvch());
                    } break;

                    case OP_ADD:
                    case OP_SUB:
                    case OP_MUL:
                    case OP_DIV:
                    case OP_MOD:
                    case OP_BOOLAND:
                    case OP_BOOLOR:
                    case OP_NUMEQUAL:
                    case OP_NUMEQUALVERIFY:
                    case OP_NUMNOTEQUAL:
                    case OP_LESSTHAN:
                    case OP_GREATERTHAN:
                    case OP_LESSTHANOREQUAL:
                    case OP_GREATERTHANOREQUAL:
                    case OP_MIN:
                    case OP_MAX: {
                        // (x1 x2 -- out)
                        if (stack.size() < 2) {
                            return set_error(
                                serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                        }

                        const auto& arg_2 = stack.stacktop(-2);                        
                        const auto& arg_1 = stack.stacktop(-1);

                        CScriptNum bn1(arg_2.GetElement(), fRequireMinimal,
                                       maxScriptNumLength,
                                       utxo_after_genesis);
                        CScriptNum bn2(arg_1.GetElement(), fRequireMinimal,
                                       maxScriptNumLength,
                                       utxo_after_genesis);
                        CScriptNum bn;
                        switch (opcode) {
                            case OP_ADD:
                                bn = bn1 + bn2;
                                break;

                            case OP_SUB:
                                bn = bn1 - bn2;
                                break;

                            case OP_MUL:
                                bn = bn1 * bn2;
                                break;

                            case OP_DIV:
                                // denominator must not be 0
                                if (bn2 == bnZero) {
                                    return set_error(serror,
                                                     SCRIPT_ERR_DIV_BY_ZERO);
                                }
                                bn = bn1 / bn2;
                                break;

                            case OP_MOD:
                                // divisor must not be 0
                                if (bn2 == bnZero) {
                                    return set_error(serror,
                                                     SCRIPT_ERR_MOD_BY_ZERO);
                                }
                                bn = bn1 % bn2;
                                break;

                            case OP_BOOLAND:
                                bn = (bn1 != bnZero && bn2 != bnZero);
                                break;
                            case OP_BOOLOR:
                                bn = (bn1 != bnZero || bn2 != bnZero);
                                break;
                            case OP_NUMEQUAL:
                                bn = (bn1 == bn2);
                                break;
                            case OP_NUMEQUALVERIFY:
                                bn = (bn1 == bn2);
                                break;
                            case OP_NUMNOTEQUAL:
                                bn = (bn1 != bn2);
                                break;
                            case OP_LESSTHAN:
                                bn = (bn1 < bn2);
                                break;
                            case OP_GREATERTHAN:
                                bn = (bn1 > bn2);
                                break;
                            case OP_LESSTHANOREQUAL:
                                bn = (bn1 <= bn2);
                                break;
                            case OP_GREATERTHANOREQUAL:
                                bn = (bn1 >= bn2);
                                break;
                            case OP_MIN:
                                bn = (bn1 < bn2 ? bn1 : bn2);
                                break;
                            case OP_MAX:
                                bn = (bn1 > bn2 ? bn1 : bn2);
                                break;
                            default:
                                assert(!"invalid opcode");
                                break;
                        }
                        stack.pop_back();
                        stack.pop_back();
                        stack.push_back(bn.getvch());

                        if (opcode == OP_NUMEQUALVERIFY) {
                            if (CastToBool(stack.stacktop(-1).GetElement())) {
                                stack.pop_back();
                            } else {
                                return set_error(serror,
                                                 SCRIPT_ERR_NUMEQUALVERIFY);
                            }
                        }
                    } break;

                    case OP_WITHIN: {
                        // (x min max -- out)
                        if (stack.size() < 3) {
                            return set_error(
                                serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                        }

                        const auto& top_3{stack.stacktop(-3).GetElement()};
                        const CScriptNum bn1{
                            top_3, fRequireMinimal,
                            maxScriptNumLength,
                            utxo_after_genesis};
                        const auto& top_2{stack.stacktop(-2).GetElement()};
                        const CScriptNum bn2{
                            top_2, fRequireMinimal,
                            maxScriptNumLength,
                            utxo_after_genesis};
                        const auto& top_1{stack.stacktop(-1).GetElement()};
                        const CScriptNum bn3{
                            top_1, fRequireMinimal,
                            maxScriptNumLength,
                            utxo_after_genesis};
                        const bool fValue = (bn2 <= bn1 && bn1 < bn3);
                        stack.pop_back();
                        stack.pop_back();
                        stack.pop_back();

                        stack.push_back(fValue ? vchTrue : vchFalse);
                    } break;

                    //
                    // Crypto
                    //
                    case OP_RIPEMD160:
                    case OP_SHA1:
                    case OP_SHA256:
                    case OP_HASH160:
                    case OP_HASH256: {
                        // (in -- hash)
                        if (stack.size() < 1) {
                            return set_error(
                                serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                        }

                        LimitedVector &vch = stack.stacktop(-1);
                        valtype vchHash((opcode == OP_RIPEMD160 ||
                                         opcode == OP_SHA1 ||
                                         opcode == OP_HASH160)
                                            ? 20
                                            : 32);
                        if (opcode == OP_RIPEMD160) {
                            CRIPEMD160()
                                .Write(vch.GetElement().data(), vch.size())
                                .Finalize(vchHash.data());
                        } else if (opcode == OP_SHA1) {
                            CSHA1()
                                .Write(vch.GetElement().data(), vch.size())
                                .Finalize(vchHash.data());
                        } else if (opcode == OP_SHA256) {
                            CSHA256()
                                .Write(vch.GetElement().data(), vch.size())
                                .Finalize(vchHash.data());
                        } else if (opcode == OP_HASH160) {
                            CHash160()
                                .Write(vch.GetElement().data(), vch.size())
                                .Finalize(vchHash.data());
                        } else if (opcode == OP_HASH256) {
                            CHash256()
                                .Write(vch.GetElement().data(), vch.size())
                                .Finalize(vchHash.data());
                        }
                        stack.pop_back();
                        stack.push_back(vchHash);
                    } break;

                    case OP_CODESEPARATOR: {
                        // Hash starts after the code separator
                        pbegincodehash = pc;
                    } break;

                    case OP_CHECKSIG:
                    case OP_CHECKSIGVERIFY: {
                        // (sig pubkey -- bool)
                        if (stack.size() < 2) {
                            return set_error(
                                serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                        }
                        LimitedVector &vchSig = stack.stacktop(-2);
                        LimitedVector &vchPubKey = stack.stacktop(-1);

                        if (!CheckSignatureEncoding(vchSig.GetElement(), flags, serror) ||
                            !CheckPubKeyEncoding(vchPubKey.GetElement(), flags, serror)) {
                            // serror is set
                            return false;
                        }

                        // Subset of script starting at the most recent
                        // codeseparator
                        CScript scriptCode(pbegincodehash, pend);

                        // Remove signature for pre-fork scripts
                        CleanupScriptCode(scriptCode, vchSig.GetElement(), flags);

                        bool fSuccess = checker.CheckSig(vchSig.GetElement(), vchPubKey.GetElement(),
                                                         scriptCode, flags & SCRIPT_ENABLE_SIGHASH_FORKID);

                        if (!fSuccess && (flags & SCRIPT_VERIFY_NULLFAIL) &&
                            vchSig.size()) {
                            return set_error(serror, SCRIPT_ERR_SIG_NULLFAIL);
                        }

                        stack.pop_back();
                        stack.pop_back();
                        stack.push_back(fSuccess ? vchTrue : vchFalse);
                        if (opcode == OP_CHECKSIGVERIFY) {
                            if (fSuccess) {
                                stack.pop_back();
                            } else {
                                return set_error(serror,
                                                 SCRIPT_ERR_CHECKSIGVERIFY);
                            }
                        }
                    } break;

                    case OP_CHECKMULTISIG:
                    case OP_CHECKMULTISIGVERIFY: {
                        // ([sig ...] num_of_signatures [pubkey ...]
                        // num_of_pubkeys -- bool)

                        uint64_t i = 1;
                        if (stack.size() < i) {
                            return set_error(
                                serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                        }

                        // initialize to max size of CScriptNum::MAXIMUM_ELEMENT_SIZE (4 bytes) 
                        // because only 4 byte integers are supported by  OP_CHECKMULTISIG / OP_CHECKMULTISIGVERIFY
                        int64_t nKeysCountSigned =
                            CScriptNum(stack.stacktop(-i).GetElement(), fRequireMinimal, CScriptNum::MAXIMUM_ELEMENT_SIZE).getint();
                        if (nKeysCountSigned < 0) {
                            return set_error(serror, SCRIPT_ERR_PUBKEY_COUNT);
                        }

                        uint64_t nKeysCount = static_cast<uint64_t>(nKeysCountSigned);
                        if (nKeysCount > config.GetMaxPubKeysPerMultiSig(utxo_after_genesis, consensus)) {
                            return set_error(serror, SCRIPT_ERR_PUBKEY_COUNT);
                        }

                        nOpCount += nKeysCount;
                        if (!IsValidMaxOpsPerScript(nOpCount, config, utxo_after_genesis, consensus)) {
                            return set_error(serror, SCRIPT_ERR_OP_COUNT);
                        }
                        uint64_t ikey = ++i;
                        // ikey2 is the position of last non-signature item in
                        // the stack. Top stack item = 1. With
                        // SCRIPT_VERIFY_NULLFAIL, this is used for cleanup if
                        // operation fails.
                        uint64_t ikey2 = nKeysCount + 2;
                        i += nKeysCount;
                        if (stack.size() < i) {
                            return set_error(
                                serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                        }

                        int64_t nSigsCountSigned =
                            CScriptNum(stack.stacktop(-i).GetElement(), fRequireMinimal, CScriptNum::MAXIMUM_ELEMENT_SIZE).getint();

                        if (nSigsCountSigned < 0) {
                            return set_error(serror, SCRIPT_ERR_SIG_COUNT);
                        }
                        uint64_t nSigsCount = static_cast<uint64_t>(nSigsCountSigned);
                        if (nSigsCount > nKeysCount) {
                            return set_error(serror, SCRIPT_ERR_SIG_COUNT);
                        }

                        uint64_t isig = ++i;
                        i += nSigsCount;
                        if (stack.size() < i) {
                            return set_error(
                                serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                        }

                        // Subset of script starting at the most recent
                        // codeseparator
                        CScript scriptCode(pbegincodehash, pend);

                        // Remove signature for pre-fork scripts
                        for (uint64_t k = 0; k < nSigsCount; k++) {
                            LimitedVector &vchSig = stack.stacktop(-isig - k);
                            CleanupScriptCode(scriptCode, vchSig.GetElement(), flags);
                        }

                        bool fSuccess = true;
                        while (fSuccess && nSigsCount > 0) {
                            if (token.IsCanceled())
                            {
                                return {};
                            }

                            LimitedVector &vchSig = stack.stacktop(-isig);
                            LimitedVector &vchPubKey = stack.stacktop(-ikey);

                            // Note how this makes the exact order of
                            // pubkey/signature evaluation distinguishable by
                            // CHECKMULTISIG NOT if the STRICTENC flag is set.
                            // See the script_(in)valid tests for details.

                            if (!CheckSignatureEncoding(vchSig.GetElement(), flags, serror) ||
                                !CheckPubKeyEncoding(vchPubKey.GetElement(), flags, serror)) {
                                // serror is set
                                return false;
                            }

                            // Check signature
                            bool fOk = checker.CheckSig(vchSig.GetElement(), vchPubKey.GetElement(),
                                                        scriptCode, flags & SCRIPT_ENABLE_SIGHASH_FORKID);

                            if (fOk) {
                                isig++;
                                nSigsCount--;
                            }
                            ikey++;
                            nKeysCount--;

                            // If there are more signatures left than keys left,
                            // then too many signatures have failed. Exit early,
                            // without checking any further signatures.
                            if (nSigsCount > nKeysCount) {
                                fSuccess = false;
                            }
                        }

                        // Clean up stack of actual arguments
                        while (i-- > 1) {
                            // If the operation failed, we require that all
                            // signatures must be empty vector
                            if (!fSuccess && (flags & SCRIPT_VERIFY_NULLFAIL) &&
                                !ikey2 && stack.stacktop(-1).size()) {
                                return set_error(serror,
                                                 SCRIPT_ERR_SIG_NULLFAIL);
                            }
                            if (ikey2 > 0) {
                                ikey2--;
                            }
                            stack.pop_back();
                        }

                        // A bug causes CHECKMULTISIG to consume one extra
                        // argument whose contents were not checked in any way.
                        //
                        // Unfortunately this is a potential source of
                        // mutability, so optionally verify it is exactly equal
                        // to zero prior to removing it from the stack.
                        if (stack.size() < 1) {
                            return set_error(
                                serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                        }
                        if ((flags & SCRIPT_VERIFY_NULLDUMMY) &&
                            stack.stacktop(-1).size()) {
                            return set_error(serror, SCRIPT_ERR_SIG_NULLDUMMY);
                        }
                        stack.pop_back();

                        stack.push_back(fSuccess ? vchTrue : vchFalse);

                        if (opcode == OP_CHECKMULTISIGVERIFY) {
                            if (fSuccess) {
                                stack.pop_back();
                            } else {
                                return set_error(
                                    serror, SCRIPT_ERR_CHECKMULTISIGVERIFY);
                            }
                        }
                    } break;

                    //
                    // Byte string operations
                    //
                    case OP_CAT: {
                        // (x1 x2 -- out)
                        if (stack.size() < 2) {
                            return set_error(
                                serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                        }

                        LimitedVector &vch1 = stack.stacktop(-2);
                        // We make copy of last element on stack (vch2) so we can pop the last
                        // element before appending it to the previous element.
                        // If appending would be first, we could exceed stack size in the process
                        // even though OP_CAT actually reduces total stack size.
                        LimitedVector vch2 = stack.stacktop(-1);

                        if (!utxo_after_genesis &&
                            (vch1.size() + vch2.size() > MAX_SCRIPT_ELEMENT_SIZE_BEFORE_GENESIS))
                        {
                            return set_error(serror, SCRIPT_ERR_PUSH_SIZE);
                        }

                        stack.pop_back();
                        vch1.append(vch2);
                    } break;

                    case OP_SPLIT: {
                        // (in position -- x1 x2)
                        if(stack.size() < 2)
                            return set_error(
                                serror, SCRIPT_ERR_INVALID_STACK_OPERATION);

                        const LimitedVector& data = stack.stacktop(-2);

                        // Make sure the split point is apropriate.
                        const auto& top{stack.stacktop(-1).GetElement()};
                        const CScriptNum n{
                            top, fRequireMinimal,
                            maxScriptNumLength,
                            utxo_after_genesis};
                        if(n < 0 || n > data.size())
                            return set_error(serror,
                                             SCRIPT_ERR_INVALID_SPLIT_RANGE);

                        const auto position{n.to_size_t_limited()};

                        // Prepare the results in their own buffer as `data`
                        // will be invalidated.
                        valtype n1(data.begin(), data.begin() + position);
                        valtype n2(data.begin() + position, data.end());

                        stack.pop_back();
                        stack.pop_back();

                        // Replace existing stack values by the new values.
                        stack.push_back(n1);
                        stack.push_back(n2);
                    } break;

                    //
                    // Conversion operations
                    //
                    case OP_NUM2BIN: {
                        // (in size -- out)
                        if (stack.size() < 2) {
                            return set_error(
                                serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                        }

                        const auto& arg_1 = stack.stacktop(-1).GetElement();
                        const CScriptNum n{
                            arg_1, fRequireMinimal,
                            maxScriptNumLength,
                            utxo_after_genesis};
                        if(n < 0 || n > std::numeric_limits<int32_t>::max())
                            return set_error(serror, SCRIPT_ERR_PUSH_SIZE);

                        const auto size{n.to_size_t_limited()};
                        if(!utxo_after_genesis && (size > MAX_SCRIPT_ELEMENT_SIZE_BEFORE_GENESIS))
                        {
                            return set_error(serror, SCRIPT_ERR_PUSH_SIZE);
                        }

                        stack.pop_back();
                        LimitedVector &rawnum = stack.stacktop(-1);

                        // Try to see if we can fit that number in the number of
                        // byte requested.
                        rawnum.MinimallyEncode();
                        if (rawnum.size() > size) {
                            // We definitively cannot.
                            return set_error(serror,
                                             SCRIPT_ERR_IMPOSSIBLE_ENCODING);
                        }

                        // We already have an element of the right size, we
                        // don't need to do anything.
                        if (rawnum.size() == size) {
                            break;
                        }

                        uint8_t signbit = 0x00;
                        if (rawnum.size() > 0) {
                            signbit = rawnum.GetElement().back() & 0x80;
                            rawnum[rawnum.size() - 1] &= 0x7f;
                        }

                        rawnum.padRight(size, signbit);
                    } break;

                    case OP_BIN2NUM: {
                        // (in -- out)
                        if (stack.size() < 1) {
                            return set_error(
                                serror, SCRIPT_ERR_INVALID_STACK_OPERATION);
                        }

                        LimitedVector &n = stack.stacktop(-1);
                        n.MinimallyEncode();

                        // The resulting number must be a valid number.
                        if (!n.IsMinimallyEncoded(maxScriptNumLength))
                        {
                            return set_error(serror,
                                             SCRIPT_ERR_INVALID_NUMBER_RANGE);
                        }
                    } break;

                    default: {
                        if (IsInvalidBranchingOpcode(opcode) && utxo_after_genesis && !fExec)
                        {
                            break;
                        }

                        return set_error(serror, SCRIPT_ERR_BAD_OPCODE);
                    }
                }
            }

            // Size limits
            if (!utxo_after_genesis &&
               (stack.size() + altstack.size() > MAX_STACK_ELEMENTS_BEFORE_GENESIS))
            {
                return set_error(serror, SCRIPT_ERR_STACK_SIZE);
            }
        }
    }
    catch(scriptnum_overflow_error& err)
    {
        return set_error(serror, SCRIPT_ERR_SCRIPTNUM_OVERFLOW);
    }
    catch(scriptnum_minencode_error& err)
    {
        return set_error(serror, SCRIPT_ERR_SCRIPTNUM_MINENCODE);
    }
    catch(stack_overflow_error& err)
    {
        return set_error(serror, SCRIPT_ERR_STACK_SIZE);
    }
    catch(const bsv::big_int_error&)
    {
        return set_error(serror, SCRIPT_ERR_BIG_INT);
    }
    catch(...)
    {
        return set_error(serror, SCRIPT_ERR_UNKNOWN_ERROR);
    }

    if (!vfExec.empty()) {
        return set_error(serror, SCRIPT_ERR_UNBALANCED_CONDITIONAL);
    }

    return set_success(serror);
}

namespace {

/**
 * Wrapper that serializes like CTransaction, but with the modifications
 *  required for the signature hash done in-place
 */
class CTransactionSignatureSerializer {
private:
    //!< reference to the spending transaction (the one being serialized)
    const CTransaction &txTo;
    //!< output script being consumed
    const CScript &scriptCode;
    //!< input index of txTo being signed
    const unsigned int nIn;
    //!< container for hashtype flags
    const SigHashType sigHashType;

public:
    CTransactionSignatureSerializer(const CTransaction &txToIn,
                                    const CScript &scriptCodeIn,
                                    unsigned int nInIn,
                                    SigHashType sigHashTypeIn)
        : txTo(txToIn), scriptCode(scriptCodeIn), nIn(nInIn),
          sigHashType(sigHashTypeIn) {}

    /** Serialize the passed scriptCode, skipping OP_CODESEPARATORs */
    template <typename S> void SerializeScriptCode(S &s) const {
        CScript::const_iterator it = scriptCode.begin();
        CScript::const_iterator itBegin = it;
        opcodetype opcode;
        unsigned int nCodeSeparators = 0;
        while (scriptCode.GetOp(it, opcode)) {
            if (opcode == OP_CODESEPARATOR) {
                nCodeSeparators++;
            }
        }
        ::WriteCompactSize(s, scriptCode.size() - nCodeSeparators);
        it = itBegin;
        while (scriptCode.GetOp(it, opcode)) {
            if (opcode == OP_CODESEPARATOR) {
                s.write((char *)&itBegin[0], it - itBegin - 1);
                itBegin = it;
            }
        }
        if (itBegin != scriptCode.end()) {
            s.write((char *)&itBegin[0], it - itBegin);
        }
    }

    /** Serialize an input of txTo */
    template <typename S> void SerializeInput(S &s, unsigned int nInput) const {
        // In case of SIGHASH_ANYONECANPAY, only the input being signed is
        // serialized
        if (sigHashType.hasAnyoneCanPay()) {
            nInput = nIn;
        }
        // Serialize the prevout
        ::Serialize(s, txTo.vin[nInput].prevout);
        // Serialize the script
        if (nInput != nIn) {
            // Blank out other inputs' signatures
            ::Serialize(s, CScript());
        } else {
            SerializeScriptCode(s);
        }
        // Serialize the nSequence
        if (nInput != nIn &&
            (sigHashType.getBaseType() == BaseSigHashType::SINGLE ||
             sigHashType.getBaseType() == BaseSigHashType::NONE)) {
            // let the others update at will
            ::Serialize(s, (int)0);
        } else {
            ::Serialize(s, txTo.vin[nInput].nSequence);
        }
    }

    /** Serialize an output of txTo */
    template <typename S>
    void SerializeOutput(S &s, unsigned int nOutput) const {
        if (sigHashType.getBaseType() == BaseSigHashType::SINGLE &&
            nOutput != nIn) {
            // Do not lock-in the txout payee at other indices as txin
            ::Serialize(s, CTxOut());
        } else {
            ::Serialize(s, txTo.vout[nOutput]);
        }
    }

    /** Serialize txTo */
    template <typename S> void Serialize(S &s) const {
        // Serialize nVersion
        ::Serialize(s, txTo.nVersion);
        // Serialize vin
        unsigned int nInputs =
            sigHashType.hasAnyoneCanPay() ? 1 : txTo.vin.size();
        ::WriteCompactSize(s, nInputs);
        for (unsigned int nInput = 0; nInput < nInputs; nInput++) {
            SerializeInput(s, nInput);
        }
        // Serialize vout
        unsigned int nOutputs =
            (sigHashType.getBaseType() == BaseSigHashType::NONE)
                ? 0
                : ((sigHashType.getBaseType() == BaseSigHashType::SINGLE)
                       ? nIn + 1
                       : txTo.vout.size());
        ::WriteCompactSize(s, nOutputs);
        for (unsigned int nOutput = 0; nOutput < nOutputs; nOutput++) {
            SerializeOutput(s, nOutput);
        }
        // Serialize nLockTime
        ::Serialize(s, txTo.nLockTime);
    }
};

uint256 GetPrevoutHash(const CTransaction &txTo) {
    CHashWriter ss(SER_GETHASH, 0);
    for (size_t n = 0; n < txTo.vin.size(); n++) {
        ss << txTo.vin[n].prevout;
    }
    return ss.GetHash();
}

uint256 GetSequenceHash(const CTransaction &txTo) {
    CHashWriter ss(SER_GETHASH, 0);
    for (size_t n = 0; n < txTo.vin.size(); n++) {
        ss << txTo.vin[n].nSequence;
    }
    return ss.GetHash();
}

uint256 GetOutputsHash(const CTransaction &txTo) {
    CHashWriter ss(SER_GETHASH, 0);
    for (size_t n = 0; n < txTo.vout.size(); n++) {
        ss << txTo.vout[n];
    }
    return ss.GetHash();
}

} // namespace

PrecomputedTransactionData::PrecomputedTransactionData(
    const CTransaction &txTo) {
    hashPrevouts = GetPrevoutHash(txTo);
    hashSequence = GetSequenceHash(txTo);
    hashOutputs = GetOutputsHash(txTo);
}

uint256 SignatureHash(const CScript &scriptCode, const CTransaction &txTo,
                      unsigned int nIn, SigHashType sigHashType,
                      const Amount amount,
                      const PrecomputedTransactionData *cache, bool enabledSighashForkid) {
    if (sigHashType.hasForkId() && enabledSighashForkid) {
        uint256 hashPrevouts;
        uint256 hashSequence;
        uint256 hashOutputs;

        if (!sigHashType.hasAnyoneCanPay()) {
            hashPrevouts = cache ? cache->hashPrevouts : GetPrevoutHash(txTo);
        }

        if (!sigHashType.hasAnyoneCanPay() &&
            (sigHashType.getBaseType() != BaseSigHashType::SINGLE) &&
            (sigHashType.getBaseType() != BaseSigHashType::NONE)) {
            hashSequence = cache ? cache->hashSequence : GetSequenceHash(txTo);
        }

        if ((sigHashType.getBaseType() != BaseSigHashType::SINGLE) &&
            (sigHashType.getBaseType() != BaseSigHashType::NONE)) {
            hashOutputs = cache ? cache->hashOutputs : GetOutputsHash(txTo);
        } else if ((sigHashType.getBaseType() == BaseSigHashType::SINGLE) &&
                   (nIn < txTo.vout.size())) {
            CHashWriter ss(SER_GETHASH, 0);
            ss << txTo.vout[nIn];
            hashOutputs = ss.GetHash();
        }

        CHashWriter ss(SER_GETHASH, 0);
        // Version
        ss << txTo.nVersion;
        // Input prevouts/nSequence (none/all, depending on flags)
        ss << hashPrevouts;
        ss << hashSequence;
        // The input being signed (replacing the scriptSig with scriptCode +
        // amount). The prevout may already be contained in hashPrevout, and the
        // nSequence may already be contain in hashSequence.
        ss << txTo.vin[nIn].prevout;
        ss << scriptCode;
        ss << amount.GetSatoshis();
        ss << txTo.vin[nIn].nSequence;
        // Outputs (none/one/all, depending on flags)
        ss << hashOutputs;
        // Locktime
        ss << txTo.nLockTime;
        // Sighash type
        ss << sigHashType;

        return ss.GetHash();
    }

    static const uint256 one(uint256S(
        "0000000000000000000000000000000000000000000000000000000000000001"));
    if (nIn >= txTo.vin.size()) {
        //  nIn out of range
        return one;
    }

    // Check for invalid use of SIGHASH_SINGLE
    if ((sigHashType.getBaseType() == BaseSigHashType::SINGLE) &&
        (nIn >= txTo.vout.size())) {
        //  nOut out of range
        return one;
    }

    // Wrapper to serialize only the necessary parts of the transaction being
    // signed
    CTransactionSignatureSerializer txTmp(txTo, scriptCode, nIn, sigHashType);

    // Serialize and hash
    CHashWriter ss(SER_GETHASH, 0);
    ss << txTmp << sigHashType;
    return ss.GetHash();
}

bool TransactionSignatureChecker::VerifySignature(
    const std::vector<uint8_t> &vchSig, const CPubKey &pubkey,
    const uint256 &sighash) const {
    return pubkey.Verify(sighash, vchSig);
}

bool TransactionSignatureChecker::CheckSig(
    const std::vector<uint8_t> &vchSigIn, const std::vector<uint8_t> &vchPubKey,
    const CScript &scriptCode, bool enabledSighashForkid) const {
    CPubKey pubkey(vchPubKey);
    if (!pubkey.IsValid()) {
        return false;
    }

    // Hash type is one byte tacked on to the end of the signature
    std::vector<uint8_t> vchSig(vchSigIn);
    if (vchSig.empty()) {
        return false;
    }
    SigHashType sigHashType = GetHashType(vchSig);
    vchSig.pop_back();

    uint256 sighash = SignatureHash(scriptCode, *txTo, nIn, sigHashType, amount,
                                    this->txdata, enabledSighashForkid);

    if (!VerifySignature(vchSig, pubkey, sighash)) {
        return false;
    }

    return true;
}

bool TransactionSignatureChecker::CheckLockTime(
    const CScriptNum &nLockTime) const {
    // There are two kinds of nLockTime: lock-by-blockheight and
    // lock-by-blocktime, distinguished by whether nLockTime <
    // LOCKTIME_THRESHOLD.
    //
    // We want to compare apples to apples, so fail the script unless the type
    // of nLockTime being tested is the same as the nLockTime in the
    // transaction.
    if (!((txTo->nLockTime < LOCKTIME_THRESHOLD &&
           nLockTime < LOCKTIME_THRESHOLD) ||
          (txTo->nLockTime >= LOCKTIME_THRESHOLD &&
           nLockTime >= LOCKTIME_THRESHOLD))) {
        return false;
    }

    // Now that we know we're comparing apples-to-apples, the comparison is a
    // simple numeric one.
    if (nLockTime > int64_t(txTo->nLockTime)) {
        return false;
    }

    // Finally the nLockTime feature can be disabled and thus
    // CHECKLOCKTIMEVERIFY bypassed if every txin has been finalized by setting
    // nSequence to maxint. The transaction would be allowed into the
    // blockchain, making the opcode ineffective.
    //
    // Testing if this vin is not final is sufficient to prevent this condition.
    // Alternatively we could test all inputs, but testing just this input
    // minimizes the data required to prove correct CHECKLOCKTIMEVERIFY
    // execution.
    if (CTxIn::SEQUENCE_FINAL == txTo->vin[nIn].nSequence) {
        return false;
    }

    return true;
}

bool TransactionSignatureChecker::CheckSequence(
    const CScriptNum &nSequence) const {
    // Relative lock times are supported by comparing the passed in operand to
    // the sequence number of the input.
    const int64_t txToSequence = int64_t(txTo->vin[nIn].nSequence);

    // Fail if the transaction's version number is not set high enough to
    // trigger BIP 68 rules.
    if (static_cast<uint32_t>(txTo->nVersion) < 2) {
        return false;
    }

    // Sequence numbers with their most significant bit set are not consensus
    // constrained. Testing that the transaction's sequence number do not have
    // this bit set prevents using this property to get around a
    // CHECKSEQUENCEVERIFY check.
    if (txToSequence & CTxIn::SEQUENCE_LOCKTIME_DISABLE_FLAG) {
        return false;
    }

    // Mask off any bits that do not have consensus-enforced meaning before
    // doing the integer comparisons
    const uint32_t nLockTimeMask =
        CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG | CTxIn::SEQUENCE_LOCKTIME_MASK;
    const int64_t txToSequenceMasked = txToSequence & nLockTimeMask;
    const CScriptNum nSequenceMasked = nSequence & nLockTimeMask;

    // There are two kinds of nSequence: lock-by-blockheight and
    // lock-by-blocktime, distinguished by whether nSequenceMasked <
    // CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG.
    //
    // We want to compare apples to apples, so fail the script unless the type
    // of nSequenceMasked being tested is the same as the nSequenceMasked in the
    // transaction.
    if (!((txToSequenceMasked < CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG &&
           nSequenceMasked < CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG) ||
          (txToSequenceMasked >= CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG &&
           nSequenceMasked >= CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG))) {
        return false;
    }

    // Now that we know we're comparing apples-to-apples, the comparison is a
    // simple numeric one.
    if (nSequenceMasked > txToSequenceMasked) {
        return false;
    }

    return true;
}

std::optional<bool> VerifyScript(
    const CScriptConfig& config,
    bool consensus,
    const task::CCancellationToken& token,
    const CScript& scriptSig,
    const CScript& scriptPubKey,
    uint32_t flags,
    const BaseSignatureChecker& checker,
    ScriptError* serror)
{
    set_error(serror, SCRIPT_ERR_UNKNOWN_ERROR);

    // If FORKID is enabled, we also ensure strict encoding.
    if (flags & SCRIPT_ENABLE_SIGHASH_FORKID) {
        flags |= SCRIPT_VERIFY_STRICTENC;
    }

    if ((flags & SCRIPT_VERIFY_SIGPUSHONLY) != 0 && !scriptSig.IsPushOnly()) {
        return set_error(serror, SCRIPT_ERR_SIG_PUSHONLY);
    }

    LimitedStack stack(config.GetMaxStackMemoryUsage(flags & SCRIPT_UTXO_AFTER_GENESIS, consensus));
    LimitedStack stackCopy(config.GetMaxStackMemoryUsage(flags & SCRIPT_UTXO_AFTER_GENESIS, consensus));
    if (auto res = EvalScript(config, consensus, token, stack, scriptSig, flags, checker, serror);
        !res.has_value() || !res.value())
    {
        return res;
    }
    if ((flags & SCRIPT_VERIFY_P2SH)  && !(flags & SCRIPT_UTXO_AFTER_GENESIS)) {
        stackCopy = stack.makeRootStackCopy();
    }
    if (auto res = EvalScript(config, consensus, token, stack, scriptPubKey, flags, checker, serror);
        !res.has_value() || !res.value())
    {
        return res;
    }
    if (stack.empty()) {
        return set_error(serror, SCRIPT_ERR_EVAL_FALSE);
    }

    if (CastToBool(stack.back().GetElement()) == false) {
        return set_error(serror, SCRIPT_ERR_EVAL_FALSE);
    }

    // Additional validation for spend-to-script-hash transactions:
    // But only if if the utxo is before genesis
    if(  (flags & SCRIPT_VERIFY_P2SH) &&
        !(flags & SCRIPT_UTXO_AFTER_GENESIS) &&
        scriptPubKey.IsPayToScriptHash())
    {
        // scriptSig must be literals-only or validation fails
        if (!scriptSig.IsPushOnly()) {
            return set_error(serror, SCRIPT_ERR_SIG_PUSHONLY);
        }

        // Restore stack.
        stack = std::move(stackCopy);

        // stack cannot be empty here, because if it was the P2SH  HASH <> EQUAL
        // scriptPubKey would be evaluated with an empty stack and the
        // EvalScript above would return false.
        assert(!stack.empty());

        const valtype& pubKeySerialized = stack.back().GetElement();
        CScript pubKey2(pubKeySerialized.begin(), pubKeySerialized.end());
        stack.pop_back();

        if (auto res = EvalScript(config, consensus, token, stack, pubKey2, flags, checker, serror);
            !res.has_value() || !res.value())
        {
            return res;
        }
        if (stack.empty()) {
            return set_error(serror, SCRIPT_ERR_EVAL_FALSE);
        }
        if (!CastToBool(stack.back().GetElement())) {
            return set_error(serror, SCRIPT_ERR_EVAL_FALSE);
        }
    }

    // The CLEANSTACK check is only performed after potential P2SH evaluation,
    // as the non-P2SH evaluation of a P2SH script will obviously not result in
    // a clean stack (the P2SH inputs remain). The same holds for witness
    // evaluation.
    if ((flags & SCRIPT_VERIFY_CLEANSTACK) != 0) {
        // Disallow CLEANSTACK without P2SH, as otherwise a switch
        // CLEANSTACK->P2SH+CLEANSTACK would be possible, which is not a
        // softfork (and P2SH should be one).
        assert((flags & SCRIPT_VERIFY_P2SH) != 0);
        if (stack.size() != 1) {
            return set_error(serror, SCRIPT_ERR_CLEANSTACK);
        }
    }

    return set_success(serror);
}
