// Copyright (c) 2022 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include <mutex>
#include "merkleproof.h"
#include "miner_id/dataref_index_detail.h"
#include "miner_id/coinbase_doc.h"
#include <univalue.h>
#include <vector>

class CBlock;

class DataRefTxnDB {

    // Unit test support
    miner::detail::DataDB db_;
    mutable std::mutex mtx_{};
public:

    using DBMinerInfo = miner::detail::DataDB::DBMinerInfo;
    using DBDataref = miner::detail::DataDB::DBDataref;

    explicit DataRefTxnDB(const Config& config);

    class LockingAccess {

        friend class DataRefTxnDB;
        auto & GetDetail() { return data_.db_; }; //for unit testing

        std::lock_guard<std::mutex> lock_;
        DataRefTxnDB & data_;
        explicit LockingAccess (DataRefTxnDB & data) : lock_{data.mtx_}, data_{data} {}

    public:
        template<typename T> struct UnitTestAccess;

        [[nodiscard]] UniValue DumpDataRefTxnsJSON() const;
        [[nodiscard]] UniValue DumpMinerInfoTxnsJSON() const;

        void DeleteDatarefTxn(const uint256& txid) {data_.db_.DeleteEntry<DBDataref>(txid);}
        void DeleteMinerInfoTxn(const uint256& txid) {data_.db_.DeleteEntry<DBMinerInfo>(txid);}

        [[nodiscard]] bool DataRefTxnExists(const uint256 txid) const { return data_.db_.EntryExists<DBDataref>(txid); }
        [[nodiscard]] bool MinerInfoTxnExists(const uint256 txid) const { return data_.db_.EntryExists<DBMinerInfo>(txid); }
        [[nodiscard]] CTransactionRef GetDataRefTxn(const uint256 txid) const {
            auto entry =  data_.db_.LookupEntry<DBDataref>(txid);
            return entry ? entry->txn : nullptr;
        }
        [[nodiscard]] CTransactionRef GetMinerInfoTxn(const uint256 txid) const {
            auto entry =  data_.db_.LookupEntry<DBMinerInfo>(txid);
            return entry ? entry->txn : nullptr;
        }
        [[nodiscard]] std::optional<DBDataref> GetDataRefEntry(const uint256 txid) const { return data_.db_.LookupEntry<DBDataref>(txid); }
        [[nodiscard]] std::optional<DBMinerInfo> GetMinerInfoEntry(const uint256 txid) const { return data_.db_.LookupEntry<DBMinerInfo>(txid); }
    };

    auto CreateLockingAccess () {
        return LockingAccess(*this);
    }

    // Below functions lock and unlock for themselves, Calling these two functions
    // in the scope of a Locking Access will create a dead lock.
    void ExtractMinerInfoTxnFromBlock (CBlock const & block,
                                       TxId const & txid,
                                       std::function<std::optional<MerkleProof>(TxId const &, uint256 const &)> const & getMerkleProof);

    void ExtractDatarefTxnsFromBlock (CBlock const & block,
                                      std::vector<CoinbaseDocument::DataRef> const & datarefs,
                                      std::function<std::optional<MerkleProof>(TxId const &, uint256 const &)> const & getMerkleProof);

};

extern std::unique_ptr<DataRefTxnDB> g_dataRefIndex;
