// Copyright (c) 2022 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#pragma once

#include <iosfwd>
#include <variant>

#include "univalue.h"

#include "miner_id/miner_info_error.h"
#include "span.h"
#include "uint256.h"

class key_set
{
public:
    key_set(const std::string& key,
            const std::string& prev_key,
            const std::string& prev_key_sig);

    friend bool operator==(const key_set&, const key_set&);
    friend std::ostream& operator<<(std::ostream&, const key_set&);
    
    const std::string& key() const { return key_; } 
    const std::string& prev_key() const { return prev_key_; }
    const std::string& prev_key_sig() const { return prev_key_sig_; } 

private:
    std::string key_; 
    std::string prev_key_; 
    std::string prev_key_sig_; 
};

inline bool operator!=(const key_set& a, const key_set& b) { return !(a == b); }

class revocation_msg
{
    std::string compromised_miner_id_;
    std::string sig_1_;
    std::string sig_2_;

public:
    revocation_msg(const std::string& compromised_miner_id,
                   const std::string& sig_1, 
                   const std::string& sig_2);

    const std::string& compromised_miner_id() const { return compromised_miner_id_; }
    const std::string& sig_1() const { return sig_1_; }
    const std::string& sig_2() const { return sig_2_; }

    friend bool operator==(const revocation_msg&, const revocation_msg&);
    friend std::ostream& operator<<(std::ostream&, const revocation_msg&);
};

inline bool operator!=(const revocation_msg& a, const revocation_msg& b)
{
    return !(a == b);
}

class miner_info_doc
{
public:
    enum supported_version
    {
        v0_3
    };
    friend std::ostream& operator<<(std::ostream&, supported_version);

    miner_info_doc(supported_version version,
                   int32_t height,
                   const key_set& miner_id,
                   const key_set& revocation,
                   std::optional<revocation_msg> = std::nullopt);

    supported_version version() const { return version_; }
    int32_t GetHeight() const { return height_; }

    const key_set& miner_id() const { return miner_id_keys_; }
    const key_set& revocation_keys() const { return revocation_keys_; }

    const std::optional<revocation_msg>& revocation_message() const { return rev_msg_; }

    friend bool operator==(const miner_info_doc&, const miner_info_doc&);
    friend std::ostream& operator<<(std::ostream&, const miner_info_doc&);

private:
    supported_version version_;
    int32_t height_;
    key_set miner_id_keys_;
    key_set revocation_keys_;
    std::optional<revocation_msg> rev_msg_{std::nullopt};
};

inline bool operator!=(const miner_info_doc& a, const miner_info_doc& b)
{
    return !(a == b);
}

std::string to_json(const miner_info_doc&);

using mi_doc_sig = std::tuple<std::string_view, miner_info_doc, bsv::span<const uint8_t>>;
std::variant<mi_doc_sig, miner_info_error> ParseMinerInfoScript(
    bsv::span<const uint8_t> script);

std::variant<miner_info_doc, miner_info_error> ParseMinerInfoDoc(
    std::string_view miner_info_doc);
