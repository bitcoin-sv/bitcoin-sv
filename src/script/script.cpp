// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2018-2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#include "script.h"
#include "consensus/consensus.h"
#include "instruction_iterator.h"
#include "int_serialization.h"
#include "script_num.h"
#include "utilstrencodings.h"

#include <algorithm>
#include <sstream>

uint64_t CScript::GetSigOpCount(bool fAccurate, bool isGenesisEnabled, bool& sigOpCountError) const
{
    sigOpCountError = false;
    uint64_t n = 0;
    bsv::instruction last_instruction;
    const auto it_end{end_instructions()};

    int64_t scopeLevel {0};

    for(auto it{begin_instructions()}; it != it_end; ++it)
    {
        opcodetype lastOpcode{last_instruction.opcode()};

        opcodetype opcode{it->opcode()};
        if(it->opcode() == OP_INVALIDOPCODE)
            break;

        if(fAccurate || isGenesisEnabled)
        {
            if(opcode == OP_RETURN && scopeLevel == 0)
            {
                // Everything after OP_RETURN at top level scope is unexecutable
                break;
            }
            else if(opcode == OP_IF || opcode == OP_NOTIF)
            {
                // Entering a new scope at a new level
                ++scopeLevel;
            }
            else if(opcode == OP_ENDIF)
            {
                // Leaving scope at this level
                if(--scopeLevel < 0)
                {
                    // Invalid script with unbalanced IF/ENDIF
                    sigOpCountError = true;
                    return 0;
                }
            }
        }

        if(opcode == OP_CHECKSIG || opcode == OP_CHECKSIGVERIFY)
        {
            n++;
        }
        else if(opcode == OP_CHECKMULTISIG || 
            opcode == OP_CHECKMULTISIGVERIFY)
        {
            if ((fAccurate || isGenesisEnabled) && lastOpcode >= OP_1 && lastOpcode <= OP_16)
            {
                n += DecodeOP_N(lastOpcode);
            }
            // post Genesis we always count accurate ops because it's not significantly costlier
            else if (isGenesisEnabled)
            {
                if (lastOpcode == OP_0) 
                {
                    // Checking multisig with 0 keys, so nothing to add to n
                }
                else if(last_instruction.operand().size() > CScriptNum::MAXIMUM_ELEMENT_SIZE)
                {
                    // When trying to spend such output EvalScript does not allow numbers bigger than 4 bytes
                    // and the execution of such script would fail and make the coin unspendable
                    sigOpCountError = true;
                    return 0;
                }
                else
                {
                    //  When trying to spend such output EvalScript requires minimal encoding
                    //  and would fail the script if number is not minimally encoded
                    //  We check minimal encoding before calling CScriptNum to avoid
                    //  exception in CScriptNum constructor.
                    if(!bsv::IsMinimallyEncoded(
                           last_instruction.operand(),
                           CScriptNum::MAXIMUM_ELEMENT_SIZE))
                    {
                        sigOpCountError = true;
                        return 0;
                    }

                    int numSigs =
                        CScriptNum(last_instruction.operand(), true).getint();
                    if(numSigs < 0)
                    {
                        sigOpCountError = true;
                        return 0;
                    }
                    n += numSigs;
                }
            }
            else
            {
                n += MAX_PUBKEYS_PER_MULTISIG_BEFORE_GENESIS;
            }
        }
        last_instruction = *it;
    }

    return n;
}

uint64_t CScript::GetSigOpCount(const CScript &scriptSig, bool isGenesisEnabled, bool& sigOpCountError) const 
{
    sigOpCountError = false;
    if(!IsP2SH(*this))
    {
        return GetSigOpCount(true, isGenesisEnabled, sigOpCountError);
    }

    // This is a pay-to-script-hash scriptPubKey;
    // get the last item that the scriptSig
    // pushes onto the stack:
    span<const uint8_t> data;
    const bool valid_script = std::all_of(scriptSig.begin_instructions(), scriptSig.end_instructions(),
            [&data](const auto& inst)
            {
                if((inst.opcode() > OP_16) || (inst.opcode() == OP_INVALIDOPCODE))  
                    return false;

                data = inst.operand(); 
                return true;
            });
    if(!valid_script)
        return 0;

    if (isGenesisEnabled)
    {
        // After Genesis P2SH is not supported and redeem script is not executed, so we return 0
        return 0;
    }
    else
    {
        // ... and return its opcount:
        CScript subscript(data.data(), data.data() + data.size());
        return subscript.GetSigOpCount(true, isGenesisEnabled, sigOpCountError);
    }
}

bool IsP2SH(const span<const uint8_t> script) {
    // Extra-fast test for pay-to-script-hash CScripts:
    return script.size() == 23 && script[0] == OP_HASH160 &&
           script[1] == 0x14 && script[22] == OP_EQUAL;
}

// Quick test for double-spend enabled OP_RETURN script
bool IsDSNotification(span<const uint8_t> script) {
    // OP_FALSE OP_RETURN OP_PUSHDATA 0x64736e74 OP_PUSHDATA Callback Msg
    return script.size() >= 11 && script[0] == OP_FALSE && script[1] == OP_RETURN && script[2] == 0x04 &&
       script[3] == 0x64 && script[4] == 0x73 && script[5] == 0x6e && script[6] == 0x74;
}

bool IsDustReturnScript(const span<const uint8_t> script)
{
    // OP_FALSE, OP_RETURN, OP_PUSHDATA, 'dust'
    static constexpr std::array<uint8_t, 7> dust_return = {0x00,0x6a,0x04,0x64,0x75,0x73,0x74};
    if (script.size() != dust_return.size())
        return false;

    return std::equal(script.begin(), script.end(), dust_return.begin());
}

/*
 * The beginning of the script should look like this: OP_FALSE OP_RETURN
 * OP_PUSHDATA protocol_id OP_PUSHDATA data Method only works for 4-byte
 * protocol ids. It does not check data after OP_PUSHDATA (i.e. is length of
 * data consistent with the chosen PUSHDATA). This should be done on the
 * call-site.
 */
bool IsMinerId(const span<const uint8_t> script)
{
    constexpr std::array<uint8_t, 4> protocol_id{0xac, 0x1e, 0xed, 0x88};
    return script.size() >= 8 && 
           script[0] == OP_FALSE &&
           script[1] == OP_RETURN && 
           script[2] == protocol_id.size() &&
           std::equal(protocol_id.begin(), protocol_id.end(), script.begin() + 3) &&
           script[7] <= OP_PUSHDATA4;
}

bool CScript::IsPushOnly(const_iterator pc) const {
    while (pc < end()) {
        opcodetype opcode;
        if (!GetOp(pc, opcode)) return false;
        // Note that IsPushOnly() *does* consider OP_RESERVED to be a push-type
        // opcode, however execution of OP_RESERVED fails, so it's not relevant
        // to P2SH/BIP62 as the scriptSig would fail prior to the P2SH special
        // validation code being executed.
        if (opcode > OP_16) return false;
    }
    return true;
}

bool CScript::IsPushOnly() const {
    return this->IsPushOnly(begin());
}

CScript &CScript::push_int64(int64_t n) {
    if (n == -1 || (n >= 1 && n <= 16)) {
        push_back(n + (OP_1 - 1));
    } else if (n == 0) {
        push_back(OP_0);
    } else {
        std::vector<uint8_t> v;
        v.reserve(sizeof(n));
        bsv::serialize(n, back_inserter(v));
        *this << v;
    }
    return *this;
}

CScript &CScript::operator<<(const CScriptNum &b) {
    *this << b.getvch();
    return *this;
}

bsv::instruction_iterator CScript::begin_instructions() const
{
    return bsv::instruction_iterator{span<const uint8_t>{data(), size()}};
}

bsv::instruction_iterator CScript::end_instructions() const
{
    return bsv::instruction_iterator{
        span<const uint8_t>{data() + size(), 0}};
}

std::ostream& operator<<(std::ostream& os, const CScript& script)
{
    for(auto it = script.begin_instructions(); it != script.end_instructions();
        ++it)
    {
        os << *it << '\n';
    }

    return os;
}

// used for debugging and pretty-printing in gdb
std::string to_string(const CScript& s)
{
    std::ostringstream oss;
    oss << s;
    return oss.str();
}

size_t CountOp(const span<const uint8_t> s, const opcodetype opcode)
{
    using namespace bsv;
    instruction_iterator first{s};
    instruction_iterator last{s.last(0)};
    return std::count_if(first, last, [opcode](const instruction& inst) {
        return inst.opcode() == opcode;
    });
}
