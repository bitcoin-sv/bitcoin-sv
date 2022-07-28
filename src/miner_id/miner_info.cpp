// Copyright (c) 2022 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#include "miner_info.h"

#include <regex>
#include <variant>

#include "consensus/merkle.h"
#include "hash.h"
#include "miner_id/miner_info_error.h"
#include "miner_id/miner_info_ref.h"
#include "primitives/block.h"
#include "pubkey.h"
#include "span.h"

using namespace std;

miner_info::miner_info(string_view raw_mi_doc,
                       const miner_info_doc& mi_doc,
                       bsv::span<const uint8_t> sig,
                       const uint256& txid)
    : raw_mi_doc_{raw_mi_doc},
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
    const auto it_mi_tx = find_tx_by_id(block, mi_ref.txid());
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
    const auto [raw_mi_doc, mi_doc, sig] = get<mi_doc_sig>(var_mi_doc_sig);

    const auto mi_err = verify(block, mi_ref.blockbind(), mi_doc.miner_id().key());
    if(mi_err)
        return mi_err.value();

    return miner_info{raw_mi_doc, mi_doc, sig, (*it_mi_tx)->GetId()};
}

uint256 modify_merkle_root(const CBlock& block)
{
    assert(!block.vtx.empty());
    assert(!block.vtx[0]->vin.empty());
    assert(block.vtx[0]->vout.size() >= 2);

    CMutableTransaction coinbase_tx{*(block.vtx[0])};

    coinbase_tx.nVersion = 0x01000000;
    
    coinbase_tx.vin[0].scriptSig = CScript()
                                << std::vector<uint8_t>{0, 0, 0, 0, 0, 0, 0, 0};

    COutPoint op;
    coinbase_tx.vin[0].prevout = op;

    const auto it = find_if(coinbase_tx.vout.begin(),
                            coinbase_tx.vout.end(),
                            [](const CTxOut& op) { return IsMinerInfo(op.scriptPubKey); });
    if(it != coinbase_tx.vout.cend())
    {
        constexpr size_t truncate_len{42};
        it->scriptPubKey.resize(truncate_len);
    }

    // Calculate merkle root for block with modified coinbase txn
    // TODO this won't scale use spv technique
    std::vector<uint256> leaves{ coinbase_tx.GetId() };
    leaves.reserve(block.vtx.size());
    transform(next(block.vtx.begin()), block.vtx.cend(), back_inserter(leaves),
              [](const auto& vtx)
              {
                  return vtx->GetId();
              });

    return ComputeMerkleRoot(leaves);
}

std::optional<miner_info_error> verify(const CBlock& block,
                                       const block_bind& bb,
                                       const string& key)
{
    const auto mm_root = modify_merkle_root(block);

    vector<uint8_t> buffer{mm_root.begin(), mm_root.end()};
    buffer.reserve(mm_root.size() + block.hashPrevBlock.size());
    buffer.insert(buffer.end(), block.hashPrevBlock.begin(), block.hashPrevBlock.end());
           
    uint256 expected_mmr_pbh_hash; 
    CSHA256().Write(buffer.data(), buffer.size())
             .Finalize(expected_mmr_pbh_hash.begin());

    const auto& mmr_pbh_hash = bb.mmr_pbh_hash();
    if(mmr_pbh_hash != expected_mmr_pbh_hash)
        return miner_info_error::block_bind_hash_mismatch; 

    const bsv::span<const uint8_t> sig{bb.data(), bb.size()};
    
    const CPubKey pubKey{ParseHex(key.c_str())};
    if(!pubKey.Verify(uint256{mmr_pbh_hash}, sig))
        return miner_info_error::block_bind_sig_verification_failed; 

    return nullopt; 
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

