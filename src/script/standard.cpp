// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "script/standard.h"
#include "script/script_num.h"
#include "pubkey.h"
#include "script/script.h"
#include "int_serialization.h"
#include "validation.h"

typedef std::vector<uint8_t> valtype;

CScriptID::CScriptID(const CScript &in)
    : uint160(Hash160(in.begin(), in.end())) {}

const char *GetTxnOutputType(txnouttype t) {
    switch (t) {
        case TX_NONSTANDARD:
            return "nonstandard";
        case TX_PUBKEY:
            return "pubkey";
        case TX_PUBKEYHASH:
            return "pubkeyhash";
        case TX_SCRIPTHASH:
            return "scripthash";
        case TX_MULTISIG:
            return "multisig";
        case TX_NULL_DATA:
            return "nulldata";
    }
    return nullptr;
}

/**
 * Return public keys or hashes from scriptPubKey, for 'standard' transaction
 * types.
 */
bool Solver(
    const CScript &scriptPubKey, 
    ProtocolEra era,
    txnouttype &typeRet,
    std::vector<std::vector<uint8_t>> &vSolutionsRet)
{
    // Templates
    static std::multimap<txnouttype, CScript> mTemplates{
            // Standard tx, sender provides pubkey, receiver adds signature
            {TX_PUBKEY, CScript() << OP_PUBKEY << OP_CHECKSIG},

            // Bitcoin address tx, sender provides hash of pubkey, receiver
            // provides signature and pubkey
            {
                TX_PUBKEYHASH,
                CScript() << OP_DUP << OP_HASH160 << OP_PUBKEYHASH
                          << OP_EQUALVERIFY << OP_CHECKSIG
            },

            // Sender provides N pubkeys, receivers provides M signatures
            {
                TX_MULTISIG,
                CScript() << OP_SMALLINTEGER << OP_PUBKEYS
                          << OP_SMALLINTEGER << OP_CHECKMULTISIG
            }
        };

    vSolutionsRet.clear();

    // Shortcut for pay-to-script-hash, which are more constrained than the
    // other types:
    // it is always OP_HASH160 20 [20 byte hash] OP_EQUAL
    if (IsP2SH(scriptPubKey)) {
        if (IsProtocolActive(era, ProtocolName::Genesis)) {
            typeRet = TX_NONSTANDARD;
            return false;
        } else {
            typeRet = TX_SCRIPTHASH;
            std::vector<uint8_t> hashBytes(scriptPubKey.begin() + 2,
                                           scriptPubKey.begin() + 22);
            vSolutionsRet.push_back(hashBytes);
            return true;
        }
    }
    
    bool isOpReturn = false;
    int offset = 0;
    //check if starts with OP_RETURN (only before Genesis upgrade) or OP_FALSE, OP_RETURN (both pre and post Genesis upgrade)
    if (!IsProtocolActive(era, ProtocolName::Genesis) && scriptPubKey.size() > 0 && scriptPubKey[0] == OP_RETURN) {
        isOpReturn = true;
        offset = 1;
    }
    else if (scriptPubKey.size() > 1 && scriptPubKey[0] == OP_FALSE && scriptPubKey[1] == OP_RETURN) {
        isOpReturn = true;
        offset = 2;
    }

    // Provably prunable, data-carrying output
    //
    // So long as script passes the IsUnspendable() test and all but the first
    // byte passes the IsPushOnly() test we don't care what exactly is in the
    // script.
    if (isOpReturn && scriptPubKey.IsPushOnly(scriptPubKey.begin() + offset)) {
        typeRet = TX_NULL_DATA;
        return true;
    }


    // Scan templates
    const CScript &script1 = scriptPubKey;
    for (const auto &[tp_outtype, tp_script]  : mTemplates) {
        const CScript &script2 = tp_script;
        vSolutionsRet.clear();

        opcodetype opcode1, opcode2; //NOLINT(cppcoreguidelines-init-variables)
        std::vector<uint8_t> vch1, vch2;

        // Compare
        CScript::const_iterator pc1 = script1.begin();
        CScript::const_iterator pc2 = script2.begin();
        while (true) {
            if (pc1 == script1.end() && pc2 == script2.end()) {
                // Found a match
                typeRet = tp_outtype;
                if (typeRet == TX_MULTISIG) {
                    // we check minimal encoding before calling CScriptNum to prevent exception throwing in CScriptNum constructor.
                    // This output will then be unspendable because EvalScript will fail execution of such script
                    if (!bsv::IsMinimallyEncoded(vSolutionsRet.front(), CScriptNum::MAXIMUM_ELEMENT_SIZE) ||
                        !bsv::IsMinimallyEncoded(vSolutionsRet.back(), CScriptNum::MAXIMUM_ELEMENT_SIZE)){
                        typeRet = TX_NONSTANDARD;
                        return false;
                    }
                    // Additional checks for TX_MULTISIG:
                    int m = CScriptNum(vSolutionsRet.front(), min_encoding_check::no).getint();
                    int n = CScriptNum(vSolutionsRet.back(), min_encoding_check::no).getint();
                    if (m < 1 || n < 1 || m > n || vSolutionsRet.size() < 2 ||
                        vSolutionsRet.size() - 2 != static_cast<uint64_t>(n)) {
                        return false;
                    }
                }
                return true;
            }
            if (!script1.GetOp(pc1, opcode1, vch1)) {
                break;
            }
            if (!script2.GetOp(pc2, opcode2, vch2)) {
                break;
            }

            // Template matching opcodes:
            if (opcode2 == OP_PUBKEYS) {
                while (vch1.size() >= 33 && vch1.size() <= 65) {
                    vSolutionsRet.push_back(vch1);
                    if (!script1.GetOp(pc1, opcode1, vch1)) {
                        break;
                    }
                }
                if (!script2.GetOp(pc2, opcode2, vch2)) {
                    break;
                }
                // Normal situation is to fall through to other if/else
                // statements
            }

            if (opcode2 == OP_PUBKEY) {
                if (vch1.size() < 33 || vch1.size() > 65) {
                    break;
                }
                vSolutionsRet.push_back(vch1);
            } else if (opcode2 == OP_PUBKEYHASH) {
                if (vch1.size() != sizeof(uint160)) {
                    break;
                }
                vSolutionsRet.push_back(vch1);
            } else if (opcode2 == OP_SMALLINTEGER) {
                // OP_0 may be pushed onto vector as empty element if minimal encoding is used
                if (opcode1 == OP_0 || (IsProtocolActive(era, ProtocolName::Genesis) && !vch1.empty())) {
                    //if number size is greater than currently max allowed (4 bytes) we break the execution and mark the transaction as non-standard
                    if (vch1.size() > CScriptNum::MAXIMUM_ELEMENT_SIZE)
                        break;

                    vSolutionsRet.push_back(vch1);
                }
                else if (opcode1 >= OP_1 && opcode1 <= OP_16) {
                    char n = (char)DecodeOP_N(opcode1);
                    vSolutionsRet.push_back(valtype(1, n));
                } else {
                    break;
                }
            } else if (opcode1 != opcode2 || vch1 != vch2) {
                // Others must match exactly
                break;
            }
        }
    }

    vSolutionsRet.clear();
    typeRet = TX_NONSTANDARD;
    return false;
}

bool ExtractDestination(const CScript &scriptPubKey, ProtocolEra era, CTxDestination &addressRet)
{
    std::vector<valtype> vSolutions;
    txnouttype whichType; //NOLINT(cppcoreguidelines-init-variables)
    if (!Solver(scriptPubKey, era, whichType, vSolutions)) {
        return false;
    }

    if (whichType == TX_PUBKEY) {
        CPubKey pubKey(vSolutions[0]);
        if (!pubKey.IsValid()) {
            return false;
        }

        addressRet = pubKey.GetID();
        return true;
    }
    if (whichType == TX_PUBKEYHASH) {
        addressRet = CKeyID(uint160(vSolutions[0]));
        return true;
    }
    if (whichType == TX_SCRIPTHASH) {
        addressRet = CScriptID(uint160(vSolutions[0]));
        return true;
    }
    // Multisig txns have more than one address and OP_RETURN outputs have no addresses
    return false;
}

bool ExtractDestinations(const CScript &scriptPubKey, ProtocolEra era, txnouttype &typeRet,
                         std::vector<CTxDestination> &addressRet, int &nRequiredRet)
{
    addressRet.clear();
    typeRet = TX_NONSTANDARD;
    std::vector<valtype> vSolutions;
    if (!Solver(scriptPubKey, era, typeRet, vSolutions)) {
        return false;
    }

    if (typeRet == TX_NULL_DATA) {
        // This is data, not addresses
        return false;
    }

    if (typeRet == TX_MULTISIG) {
        nRequiredRet = vSolutions.front()[0];
        for (size_t i = 1; i < vSolutions.size() - 1; i++) {
            CPubKey pubKey(vSolutions[i]);
            if (!pubKey.IsValid()) {
                continue;
            }

            CTxDestination address = pubKey.GetID();
            addressRet.push_back(address);
        }

        if (addressRet.empty()) {
            return false;
        }
    } else {
        nRequiredRet = 1;
        CTxDestination address;
        if (!ExtractDestination(scriptPubKey, era, address)) {
            return false;
        }
        addressRet.push_back(address);
    }

    return true;
}

namespace
{

class CScriptVisitor : public boost::static_visitor<bool>
{
    CScript *script;

public:
    CScriptVisitor(CScript* scriptin):script{scriptin}
    {}

    bool operator()(const CNoDestination& /*dest*/) const {
        script->clear();
        return false;
    }

    bool operator()(const CKeyID &keyID) const {
        script->clear();
        *script << OP_DUP << OP_HASH160 << ToByteVector(keyID) << OP_EQUALVERIFY
                << OP_CHECKSIG;
        return true;
    }

    bool operator()(const CScriptID &scriptID) const {
        script->clear();
        *script << OP_HASH160 << ToByteVector(scriptID) << OP_EQUAL;
        return true;
    }
};
} // namespace

CScript GetScriptForDestination(const CTxDestination &dest) {
    CScript script;

    boost::apply_visitor(CScriptVisitor(&script), dest);
    return script;
}

CScript GetScriptForRawPubKey(const CPubKey &pubKey) {
    return CScript() << std::vector<uint8_t>(pubKey.begin(), pubKey.end())
                     << OP_CHECKSIG;
}

CScript GetScriptForMultisig(int nRequired, const std::vector<CPubKey> &keys) {
    CScript script;

    script << static_cast<int64_t>(nRequired); // we cast to int64_t to use operator<<(int64_t b) which uses push_int64 method that encodes numbers between 0..16 to opcodes OP_0..OP_16
    for (const CPubKey &key : keys) {
        script << ToByteVector(key);
    }
    script << (int64_t)keys.size() << OP_CHECKMULTISIG;
    return script;
}

bool IsValidDestination(const CTxDestination &dest) {
    return dest.which() != 0;
}

uint32_t MandatoryScriptVerifyFlags(ProtocolEra era)
{
    if(IsProtocolActive(era, ProtocolName::Chronicle))
    {
        return POST_CHRONICLE_MANDATORY_SCRIPT_VERIFY_FLAGS;
    }
    return PRE_CHRONICLE_MANDATORY_SCRIPT_VERIFY_FLAGS;
}

/**
 * Check transaction inputs to mitigate two potential denial-of-service attacks:
 *
 * 1. scriptSigs with extra data stuffed into them, not consumed by scriptPubKey
 * (or P2SH script)
 * 2. P2SH scripts with a crazy number of expensive CHECKSIG/CHECKMULTISIG
 * operations
 *
 * Why bother? To avoid denial-of-service attacks; an attacker can submit a
 * standard HASH... OP_EQUAL transaction, which will get accepted into blocks.
 * The redemption script can be anything; an attacker could use a very
 * expensive-to-check-upon-redemption script like:
 *   DUP CHECKSIG DROP ... repeated 100 times... OP_1
 */
bool IsStandardOutput(const ConfigScriptPolicy &scriptPolicy, const CScript &scriptPubKey, int32_t nScriptPubKeyHeight, txnouttype &whichType)
{
    std::vector<std::vector<uint8_t>> vSolutions;
    if (!Solver(scriptPubKey, GetProtocolEra(scriptPolicy, nScriptPubKeyHeight), whichType, vSolutions)) {
        return false;
    }

    if (whichType == TX_MULTISIG) {
        // we don't require minimal encoding here because Solver method is already checking minimal encoding
        int m = CScriptNum(vSolutions.front(), min_encoding_check::no).getint();
        int n = CScriptNum(vSolutions.back(), min_encoding_check::no).getint();
        // Support up to x-of-3 multisig txns as standard
        if (n < 1 || n > 3) return false;
        if (m < 1 || m > n) return false;
    } else if (whichType == TX_NULL_DATA) {
        if (!scriptPolicy.GetDataCarrier()) {
            return false;
        }
    }

    return whichType != TX_NONSTANDARD;
}

std::optional<bool> IsInputStandard(
    const task::CCancellationToken& token,
    const eval_script_params& params,
    //NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    const CScript& scriptSig,
    const CScript& prevScript,
    ProtocolEra utxoEra,
    uint32_t flags)
{
    std::vector<std::vector<uint8_t>> vSolutions;
    txnouttype whichType; //NOLINT(cppcoreguidelines-init-variables)

    if (!Solver(prevScript, utxoEra, whichType, vSolutions)) {
        return false;
    }

    if (whichType == TX_SCRIPTHASH) {
        // Pre-genesis limitations are stricter than post-genesis, so LimitedStack can use UINT32_MAX as max size.
        LimitedStack stack(UINT32_MAX);
        // convert the scriptSig into a stack, so we can inspect the
        // redeemScript
        if(const auto o = EvalScript(params,
                                     token,
                                     stack,
                                     scriptSig,
                                     flags,
                                     BaseSignatureChecker());
            !o.has_value())
        {
            return {};
        }
        else if(o.value() != SCRIPT_ERR_OK)
        {
            return false;
        }

        if(stack.empty())
            return false;

        // Active release is set to PreGenesis, because TX_SCRIPTHASH is not supported after genesis
        CScript subscript(stack.back().begin(), stack.back().end());
        bool sigOpCountError; //NOLINT(cppcoreguidelines-init-variables)
        uint64_t nSigOpCount = subscript.GetSigOpCount(true, ProtocolEra::PreGenesis, sigOpCountError);
        if (sigOpCountError || nSigOpCount > MAX_P2SH_SIGOPS) {
            return false;
        }
    }

    return true;
}

bool IsStandardTx(const ConfigScriptPolicy &scriptPolicy, const CTransaction &tx, int32_t nHeight, std::string &reason)
{
    ProtocolEra era { GetProtocolEra(scriptPolicy, nHeight) };

    if (!IsProtocolActive(era, ProtocolName::Chronicle))
    {
        if (tx.nVersion > CTransaction::PRE_CHRONICLE_MAX_STANDARD_VERSION ||
            tx.nVersion < CTransaction::PRE_CHRONICLE_MIN_STANDARD_VERSION)
        {
            reason = "version";
            return false;
        }
    }

    // Extremely large transactions with lots of inputs can cost the network
    // almost as much to process as they cost the sender in fees, because
    // computing signature hashes is O(ninputs*txsize). Limiting transactions
    // mitigates CPU exhaustion attacks.
    unsigned int sz = tx.GetTotalSize();
    if (sz > scriptPolicy.GetMaxTxSize(era, false)) {
        reason = "tx-size";
        return false;
    }

    for (const CTxIn &txin : tx.vin) {
        // Biggest 'standard' txin is a 15-of-15 P2SH multisig with compressed
        // keys (remember the 520 byte limit on redeemScript size). That works
        // out to a (15*(33+1))+3=513 byte redeemScript, 513+1+15*(73+1)+3=1627
        // bytes of scriptSig, which we round off to 1650 bytes for some minor
        // future-proofing. That's also enough to spend a 20-of-20 CHECKMULTISIG
        // scriptPubKey, though such a scriptPubKey is not considered standard.
        if (!IsProtocolActive(era, ProtocolName::Genesis) && txin.scriptSig.size() > 1650) {
            reason = "scriptsig-size";
            return false;
        }
        if (!IsProtocolActive(era, ProtocolName::Chronicle) && !txin.scriptSig.IsPushOnly()) {
            reason = "scriptsig-not-pushonly";
            return false;
        }
    }

    unsigned int nDataSize = 0;
    txnouttype whichType; //NOLINT(cppcoreguidelines-init-variables)
    bool scriptpubkey = false;
    for (const CTxOut &txout : tx.vout) {
        if (!::IsStandardOutput(scriptPolicy, txout.scriptPubKey, nHeight, whichType)) {
            scriptpubkey = true;
        }

        if (whichType == TX_NULL_DATA) {
            nDataSize += txout.scriptPubKey.size();
        } else if ((whichType == TX_MULTISIG) && (!scriptPolicy.GetPermitBareMultisig())) {
            reason = "bare-multisig";
            return false;
        } else if (txout.IsDust(era)) {
            reason = "dust";
            return false;
        }
    }

    // cumulative size of all OP_RETURN txout should be smaller than -datacarriersize
    if (nDataSize > scriptPolicy.GetDataCarrierSize()) {
        reason = "datacarrier-size-exceeded";
        return false;
    }

    if(scriptpubkey)
    {
        reason = "scriptpubkey";
        return false;
    }

    return true;
}

