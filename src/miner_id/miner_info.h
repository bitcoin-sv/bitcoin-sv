// Copyright (c) 2022 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#pragma once

#include <iosfwd>

#include "univalue.h"

#include "uint256.h"
#include "span.h"
#include "miner_id/miner_info_error.h"
#include "miner_id/miner_info_doc.h"

bool is_hash_256(const char*);
bool is_hash_256(const std::string&);

bool is_compressed_key(const char*);
bool is_compressed_key(const std::string&);

bool is_der_signature(const char*);
bool is_der_signature(const std::string&);
bool is_der_signature(const bsv::span<const uint8_t>);

class miner_info
{
    miner_info_doc mi_doc_;
    std::vector<uint8_t> sig_;
    uint256 txid_;
    
public:
    miner_info(const miner_info_doc&, 
               bsv::span<const uint8_t> sig,
               const uint256& txid);

    const miner_info_doc& mi_doc() const { return mi_doc_; }
    const std::vector<uint8_t>& sig() const { return sig_; } 
    const uint256& txid() const { return txid_; }
};

class CBlock;
std::variant<miner_info, miner_info_error> ParseMinerInfo(const CBlock&);
