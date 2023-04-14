// Copyright (c) 2022 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.
#include "miner_info_ref.h"

#include "miner_info.h"

#include <regex>

#include "logging.h"
#include "primitives/block.h"
#include "script/instruction_iterator.h"

using namespace std;

block_bind::block_bind(const std::span<const uint8_t> mmr_pbh_hash,
                       const std::span<const uint8_t> sig):
    mmr_pbh_hash_{mmr_pbh_hash.begin(), mmr_pbh_hash.end()},
    sig_{sig.begin(), sig.end()}
{}

bool operator==(const block_bind& a, const block_bind& b)
{
    return a.mmr_pbh_hash_ == b.mmr_pbh_hash_ &&
           std::equal(a.sig_.cbegin(), a.sig_.cend(), b.cbegin_sig());
}

std::ostream& operator<<(std::ostream& os, const block_bind& bb)
{
    os << "mmr_pbh_hash_: " << bb.mmr_pbh_hash_;

    ostream_iterator<int> it{os};
    os << "\nsignature: ";
    std::copy(bb.cbegin_sig(), bb.cend_sig(), it);

    return os;
}
    
miner_info_ref::miner_info_ref(const std::span<const uint8_t> txid,
                               const block_bind& bb)
    : txid_{txid.begin(), txid.end()},
      block_bind_(bb)
{}

bool operator==(const miner_info_ref& a, const miner_info_ref& b)
{
    return a.txid_ == b.txid_ &&
           a.block_bind_ == b.block_bind_;
}

std::ostream& operator<<(std::ostream& os, const miner_info_ref& mir)
{

    os << "txid: " << std::hex << mir.txid_
       << '\n' << mir.block_bind_;

    return os;
}

std::variant<miner_info_ref, miner_info_error> ParseMinerInfoRef(
    const std::span<const uint8_t> script)
{
    assert(IsMinerInfo(script));
    
    // 0 OP_FALSE (1)
    // 1 OP_RETURN (1)
    // 2 pushdata 4 (1)
    // 3 protocol-id (4)
    // 7 pushdata 1 (1)
    // 8 protocol-id-version (1)
    // 9 miner-info-txid (32)
    // 41 hash(modified-merkle-root || prev-block-hash) (32)
    // 73 sig(modified-merkle-root || prev-block-hash) (69-72)
    // 142-145 end
    // Total 143-146 

    bsv::instruction_iterator it{script.last(script.size() - 7)};
    if(!it)
        return miner_info_error::invalid_instruction;

    const auto operand{it->operand()};
    if(operand.size() != 1)
        return miner_info_error::script_version_unsupported;

    const auto version{operand[0]};
    if(version != 0)
        return miner_info_error::script_version_unsupported;

    if(!++it)
        return miner_info_error::invalid_instruction;

    constexpr uint8_t txid_len{32};
    if(it->operand().size() != txid_len)
        return miner_info_error::invalid_txid_len; 
 
    const auto it_txid{it};
    
    if(!++it)
        return miner_info_error::invalid_instruction;

    constexpr uint8_t mmr_pbh_hash_len{32};
    const auto mmr_pbh_hash{it->operand()};
    if(mmr_pbh_hash.size() != mmr_pbh_hash_len)
        return miner_info_error::invalid_mmr_pbh_hash_len; 
   
    if(!++it)
        return miner_info_error::invalid_instruction;
    
    const auto sig{it->operand()};
    if(!is_der_signature(sig))
        return miner_info_error::invalid_sig_len;

    return miner_info_ref{it_txid->operand(), block_bind{mmr_pbh_hash, sig}};
}

