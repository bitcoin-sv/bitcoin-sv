// Copyright (c) 2022 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#include "miner_info.h"

#include <regex>
#include <variant>

#include "miner_id/miner_info_error.h"
#include "miner_id/miner_info_ref.h"
#include "primitives/block.h"
#include "span.h"

using namespace std;

miner_info::miner_info(const miner_info_doc& mi_doc,
             bsv::span<const uint8_t> sig,
             const uint256& txid):
    mi_doc_{mi_doc},
    sig_{sig.begin(), sig.end()},
    txid_{txid} 
{
}

std::variant<miner_info, miner_info_error> ParseMinerInfo(
    const CBlock& block,
    const miner_info_ref& mi_ref)
{
    // Find the miner_info-tx
    const auto it_mi_tx = find_by_id(block, mi_ref.txid());
    if(it_mi_tx == block.cend())
        return miner_info_error::txid_not_found;

    // Find the miner_info_script
    const CTransaction& mi_tx{**it_mi_tx};
    const auto it_mi_script = find_if(mi_tx.vout.cbegin(),
                                      mi_tx.vout.cend(),
                                      [](const CTxOut& op) {
                                          return IsMinerInfo(op.scriptPubKey);
                                      });
    if(it_mi_script == mi_tx.vout.cend())
        return miner_info_error::doc_output_not_found;

    // Parse the miner_info script and return the document
    const auto var_mi_doc_sig = ParseMinerInfoScript(
        it_mi_script->scriptPubKey);
    if(holds_alternative<miner_info_error>(var_mi_doc_sig))
        return get<miner_info_error>(var_mi_doc_sig);

    assert(holds_alternative<mi_doc_sig>(var_mi_doc_sig));
    const auto doc_sig = get<mi_doc_sig>(var_mi_doc_sig);

    return miner_info{doc_sig.first, doc_sig.second, (*it_mi_tx)->GetId()};
}

std::variant<miner_info, miner_info_error> ParseMinerInfo(const CBlock& block)
{
    if(block.vtx.empty())
        return miner_info_error::miner_info_ref_not_found;

    const CTransaction& tx{*block.vtx[0]};
    const auto it_mi_ref =
        find_if(tx.vout.cbegin(), tx.vout.cend(), [](const CTxOut& op) {
            return IsMinerInfo(op.scriptPubKey);
        });
    if(it_mi_ref == tx.vout.cend())
        return miner_info_error::miner_info_ref_not_found;

    const auto var_mi_ref = ParseMinerInfoRef(it_mi_ref->scriptPubKey);
    if(std::holds_alternative<miner_info_error>(var_mi_ref))
        return std::get<miner_info_error>(var_mi_ref);

    return ParseMinerInfo(block, get<miner_info_ref>(var_mi_ref));
}

bool is_hash_256(const char* s)
{
    static const std::regex rgx{"([0-9a-fA-F]{2}){32}"};
    return regex_match(s, rgx);
}

bool is_hash_256(const std::string& s)
{
    return is_hash_256(s.c_str());
}

bool is_compressed_key(const char* s)
{
    static const std::regex rgx{"0[23]([0-9a-fA-F]{2}){32}"};
    return regex_match(s, rgx);
}

bool is_compressed_key(const std::string& s)
{
    return is_compressed_key(s.c_str());
}

bool is_der_signature(const char* s)
{
    // todo? starts with 0x30, len 0x45-0x47, type code 0x2 
    // r/s values can be less than 32 bytes?!
    static const std::regex rgx{"([0-9a-fA-F]{2}){69,72}"};
    return regex_match(s, rgx);
}

bool is_der_signature(const std::string& s)
{
    return is_der_signature(s.c_str());
}

bool is_der_signature(const bsv::span<const uint8_t> script)
{
    return script.size() >= 69 && script.size() <= 72;
}

