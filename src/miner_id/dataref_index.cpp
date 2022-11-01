// Copyright (c) 2022 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "miner_id/dataref_index.h"
#include "miner_id.h"
#include "config.h"

#include <functional>

std::unique_ptr<DataRefTxnDB> g_dataRefIndex {nullptr};

DataRefTxnDB::DataRefTxnDB(const Config& config)
    : db_{config}
{
}

void DataRefTxnDB::ExtractMinerInfoTxnFromBlock (
        CBlock const & block,
        TxId const & txid,
        std::function<std::optional<MerkleProof>(TxId const &, uint256 const &)> const & getMerkleProof)
{
    uint256 blockhash = block.GetHash();
    for (auto const & tx: block.vtx) {
        if (tx->GetId() == txid) {
            auto proof = getMerkleProof(tx->GetId(), blockhash);
            if (proof) {
                DBMinerInfo entry {tx, blockhash, std::move(*proof)};
                std::lock_guard lock{mtx_};
                db_.AddEntry(entry, tx->GetId());
                break;
            }
        }
    }
}

void DataRefTxnDB::ExtractDatarefTxnsFromBlock (
        CBlock const & block,
        std::vector<CoinbaseDocument::DataRef> const & datarefs,
        std::function<std::optional<MerkleProof>(TxId const &, uint256 const &)> const & getMerkleProof)
{
    // bisection search function, requires sorted vector
    auto vtx_in_sorted_vector = [](TxId const & txid, std::vector<TxId> const & sortedVec) -> bool {
        auto found = std::lower_bound (sortedVec.begin(), sortedVec.end(), txid);
        return (found != sortedVec.end() && *found == txid);
    };

    std::vector<TxId> datarefids; // list of datarefs including minerid
    datarefids.reserve(datarefids.size());
    for (auto const & dataref: datarefs)
        datarefids.push_back(dataref.txid);

    uint256 const & blockhash = block.GetHash();
    std::sort(datarefids.begin(), datarefids.end()); // sort for searching via lower bound
    for (auto const & tx: block.vtx) {
        if (vtx_in_sorted_vector (tx->GetId(), datarefids)) {
            auto proof = getMerkleProof(tx->GetId(), blockhash);
            if(proof) {
                DBDataref entry {tx, blockhash, std::move(*proof)};
                std::lock_guard lock{mtx_};
                db_.AddEntry(entry, tx->GetId());
            }
        }
    }
}

UniValue DataRefTxnDB::LockingAccess::DumpDataRefTxnsJSON() const
{
    UniValue result { UniValue::VARR };

    // Dump transaction details
    for(const auto& txn : data_.db_.GetAllDatarefEntries())
    {
        UniValue txnJson { UniValue::VOBJ };
        txnJson.pushKV("txid", txn.txnm.GetId().ToString());
        txnJson.pushKV("blockid", txn.blockId.ToString());

        UniValue nodearray(UniValue::VARR);
        for (auto const & node: txn.proof)
            nodearray.push_back(node.mValue.GetHex());

        txnJson.pushKV("nodes", nodearray);

        result.push_back(txnJson);
    }

    return result;
}


UniValue DataRefTxnDB::LockingAccess::DumpMinerInfoTxnsJSON() const
{
    UniValue result { UniValue::VARR };

    // Dump transaction details
    for(const auto& txn : data_.db_.GetAllMinerInfoEntries())
    {
        UniValue txnJson { UniValue::VOBJ };
        txnJson.pushKV("txid", txn.txnm.GetId().ToString());
        txnJson.pushKV("blockid", txn.blockId.ToString());

        UniValue nodearray(UniValue::VARR);
        for (auto const & node: txn.proof)
            nodearray.push_back(node.mValue.GetHex());

        txnJson.pushKV("nodes", nodearray);

        result.push_back(txnJson);
    }

    return result;
}

