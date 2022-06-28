// Copyright (c) 2022 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.
#include "miner_info_ref.h"

#include "miner_info.h"

#include <regex>

#include "logging.h"
#include "primitives/block.h"
#include "script/instruction_iterator.h"
#include "span.h"

using namespace std;

block_bind::block_bind(const bsv::span<const uint8_t> mod_merkle_root,
                       const bsv::span<const uint8_t> prev_block_hash,
                       const bsv::span<const uint8_t> sig):
    mod_merkle_root_{mod_merkle_root.begin(), mod_merkle_root.end()},
    prev_block_hash_{prev_block_hash.begin(), prev_block_hash.end()},
    sig_{sig.begin(), sig.end()}
{}

bool operator==(const block_bind& a, const block_bind& b)
{
    return a.mod_merkle_root_ == b.mod_merkle_root_ &&
           a.prev_block_hash_ == b.prev_block_hash_ &&
           std::equal(a.sig_.cbegin(), a.sig_.cend(), b.cbegin_sig());
}

std::ostream& operator<<(std::ostream& os, const block_bind& bb)
{
    os << "\nmodified merkle root: " << bb.mod_merkle_root_
       << "\nprevious block hash: " << bb.prev_block_hash_;

    ostream_iterator<int> it{os};
    os << "\nsignature: ";
    std::copy(bb.cbegin_sig(), bb.cend_sig(), it);

    return os;
}
    
bool verify(const block_bind& bb)
{
    return false;
}


miner_info_ref::miner_info_ref(const bsv::span<const uint8_t> txid,
                               const class block_bind& bb)
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

    os << "txid: " << mir.txid_
       << '\n' << mir.block_bind_;

    return os;
}

std::variant<miner_info_ref, miner_info_error> ParseMinerInfoRef(
    const bsv::span<const uint8_t> script)
{
    assert(IsMinerInfo(script));
    
    // 0 OP_FALSE (1)
    // 1 OP_RETURN (1)
    // 2 pushdata 4 (1)
    // 3 protocol-id (4)
    // 7 pushdata 1 (1)
    // 8 protocol-id-version (1)
    // 9 miner-info-txid (32)
    // 41 modified-merkle-root (32)
    // 73 prev-block-hash (32)
    // 105 sig(modified-merkle-root || prev-block-hash) (69-72)
    // 174-177 end
    // Total 175-178 

    bsv::instruction_iterator it{script.last(script.size() - 7)};
    if(!it.valid())
        return miner_info_error::invalid_instruction;

    const auto operand{it->operand()};
    if(operand.size() != 1)
        return miner_info_error::script_version_unsupported;

    const auto version{operand[0]};
    if(version != 0)
        return miner_info_error::script_version_unsupported;

    ++it;
    if(!it.valid())
        return miner_info_error::invalid_instruction;

    constexpr uint8_t txid_len{32};
    if(it->operand().size() != txid_len)
        return miner_info_error::invalid_txid_len; 
 
    const auto it_txid{it};
    
    ++it;
    if(!it.valid())
        return miner_info_error::invalid_instruction;

    constexpr uint8_t mm_root_len{32};
    const auto mm_root{it->operand()};
    if(mm_root.size() != mm_root_len)
        return miner_info_error::invalid_mm_root_len; 
   
    ++it;
    if(!it.valid())
        return miner_info_error::invalid_instruction;
    
    constexpr uint8_t prev_block_hash_len{32};
    const auto prev_block_hash{it->operand()};
    if(prev_block_hash.size() != prev_block_hash_len)
        return miner_info_error::invalid_prev_block_hash_len; 
    
    ++it;
    if(!it.valid())
        return miner_info_error::invalid_instruction;

    const auto sig{it->operand()};
    if(!is_der_signature(sig))
        return miner_info_error::invalid_sig_len;

    return miner_info_ref{it_txid->operand(),
                          block_bind{mm_root, prev_block_hash, sig}};
}

