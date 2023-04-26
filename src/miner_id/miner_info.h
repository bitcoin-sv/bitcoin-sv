// Copyright (c) 2022 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#pragma once

#include <iosfwd>
#include <span>
#include <variant>

#include "univalue.h"

#include "uint256.h"
#include "miner_id/miner_info_error.h"
#include "miner_id/miner_info_doc.h"

bool is_hash_256(const char*);
bool is_hash_256(const std::string&);

bool is_compressed_key(const char*);
bool is_compressed_key(const std::string&);

bool is_der_signature(const char*);
bool is_der_signature(const std::string&);
bool is_der_signature(const std::span<const uint8_t>);

class miner_info
{
    std::string_view raw_mi_doc_;
    miner_info_doc mi_doc_;
    std::vector<uint8_t> sig_;
    uint256 txid_;
    
public:
    miner_info(std::string_view raw_mi_doc,
               const miner_info_doc&, 
               std::span<const uint8_t> sig,
               const uint256& txid);

    std::string_view raw_mi_doc() const { return raw_mi_doc_; }
    const miner_info_doc& mi_doc() const { return mi_doc_; }
    const std::vector<uint8_t>& sig() const { return sig_; } 
    const uint256& txid() const { return txid_; }
};

class CBlock;
std::variant<miner_info, miner_info_error> ParseMinerInfo(const CBlock&);

class miner_info_ref;
std::variant<miner_info, miner_info_error> ParseMinerInfo(
    const CBlock&,
    const miner_info_ref&);

uint256 modify_merkle_root(const CBlock&);
class block_bind;
std::optional<miner_info_error> verify(const CBlock&,
                                       const block_bind&,
                                       const std::string& key);

std::variant<bool, miner_info_error> VerifyDataScript(const std::span<const uint8_t>);
std::variant<bool, miner_info_error> VerifyDataObject(const std::string_view json);

