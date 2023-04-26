// Copyright (c) 2022 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#pragma once

#include <iosfwd>
#include <span>
#include <variant>

#include "uint256.h"
#include "miner_id/miner_info_error.h"

class block_bind
{
    uint256 mmr_pbh_hash_; // hash(mod. Merkle root || previous block hash)
    std::vector<uint8_t> sig_;

public:
    block_bind(const std::span<const uint8_t> mmr_pbh_hash_,
               const std::span<const uint8_t> sig);

    const uint256& mmr_pbh_hash() const { return mmr_pbh_hash_; }
    
    auto cbegin_sig() const { return sig_.cbegin(); }
    auto cend_sig() const { return sig_.cend(); } 
    auto data() const { return sig_.data(); }
    auto size() const { return sig_.size(); }

    friend bool operator==(const block_bind&, const block_bind&);
    friend std::ostream& operator<<(std::ostream&, const block_bind&);
};

inline bool operator!=(const block_bind& a, const block_bind& b)
{
    return !(a == b);
}

class miner_info_ref
{
    uint256 txid_{};
    block_bind block_bind_;

  public:
    miner_info_ref(std::span<const uint8_t> txid, const block_bind&);

    const uint256& txid() const { return txid_; }
    const block_bind& blockbind() const { return block_bind_; }

    friend bool operator==(const miner_info_ref&, const miner_info_ref&);
    friend std::ostream& operator<<(std::ostream&, const miner_info_ref&); 
};

inline bool operator!=(const miner_info_ref& a, const miner_info_ref& b)
{
    return !(a == b);
}

std::variant<miner_info_ref, miner_info_error> ParseMinerInfoRef(
    const std::span<const uint8_t> script);


