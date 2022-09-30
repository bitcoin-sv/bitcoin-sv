// Copyright (c) 2022 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include "consensus/consensus.h"
#include "dbwrapper.h"
#include "merkleproof.h"
#include "miner_id/coinbase_doc.h"
#include "primitives/transaction.h"
#include "serialize.h"

#include <memory>
#include <mutex>

class Config;
class UniValue;

/**
 * Access to the dataRef transaction database.
 */

namespace miner::detail {

class DataDB
{
  public:

    ~DataDB() = default;
    DataDB(const DataDB&) = delete;
    DataDB(DataDB&&) = delete;
    DataDB& operator=(const DataDB&) = delete;
    DataDB& operator=(DataDB&&) = delete;

    explicit DataDB(const Config& config)
            : mConfig{config}
    {
        // Set path and cache size
        const fs::path dbPath { GetDataDir() / "miner_id/dataRefTxDB" };
        uint64_t cacheSize { 1 << 20 };
        mDBWrapper = std::make_unique<CDBWrapper>(dbPath, cacheSize, false, false);

        // Read initial disk usage
        uint64_t storedValue {0};
        if(mDBWrapper->Read(DB_DISK_USAGE, storedValue))
        {
            mDiskUsage = storedValue;
        }
    }

    // Unit test support
    template<typename T> struct UnitTestAccess;

    // Prefix to store map of transaction values with txid as a key
    static constexpr char DB_DATAREF_TXN {'T'};
    // Prefix to store merkle root for block binding
    static constexpr char DB_MINERINFO_TXN {'I'};
    // Prefix to store disk usage
    static constexpr char DB_DISK_USAGE {'D'};
        
    struct Readable
    {
        CMutableTransaction txnm {};
        uint256 blockId {};
        MerkleProof proof {};

        ADD_SERIALIZE_METHODS
        template <typename Stream, typename Operation>
        inline void SerializationOp(Stream& s, Operation ser_action)
        {
            READWRITE(txnm);
            READWRITE(blockId);
            READWRITE(proof);
        }
    };

    template<char STORAGE_TYPE>
    struct DBTxInfo
    {
        // Datatype for reading from the database
        static constexpr char DB_STORAGE_TYPE = STORAGE_TYPE;

        DBTxInfo(CTransactionRef txn, uint256 blockid, MerkleProof proof)
                : txn{std::move(txn)}, blockId{blockid}, proof{std::move(proof)}
        {}

        explicit DBTxInfo(Readable && r)
                : txn { MakeTransactionRef(std::move(r.txnm)) },
                  blockId { r.blockId },
                  proof (std::move(r.proof) )
        {}

        // The datRef transaction
        CTransactionRef txn {nullptr};
        // The block it appeared in and whose coinbase document references it
        uint256 blockId {};
        // The block binding root
        MerkleProof proof {};

        // Return the total size of our data members
        [[nodiscard]] uint64_t GetTotalSize() const
        {
            return txn->GetTotalSize() + sizeof(decltype(blockId)) + (sizeof(uint256) * proof.size());
        }

        ADD_SERIALIZE_METHODS
        template <typename Stream, typename Operation>
        inline void SerializationOp(Stream& s, Operation ser_action)
        {
            READWRITE(txn);
            READWRITE(blockId);
            READWRITE(proof);
        }
    };

    using DBDataref = DBTxInfo<DB_DATAREF_TXN>;
    using DBMinerInfo = DBTxInfo<DB_MINERINFO_TXN>;

    /**
     * Add a single dataRef/minerinfo txn from a block with a minerId coinbase document
     * to the database.
     */
    template <typename Entry, typename Key>
    void AddEntry(const Entry & entry, const Key & key)
    {
        // How large will this txn grow the database?
        uint64_t diskUsageAdded { entry.GetTotalSize() };
        // Build batch of updates to apply
        CDBBatch batch { *mDBWrapper };
        batch.Write(std::make_pair(Entry::DB_STORAGE_TYPE, key), entry);
        batch.Write(DB_DISK_USAGE, mDiskUsage + diskUsageAdded);
        if(mDBWrapper->WriteBatch(batch, true))
        {
            mDiskUsage += diskUsageAdded;
            LogPrint(BCLog::MINERID, "DataRef/Minerid transaction added to DB (New DB size %u)\n", mDiskUsage);
        }
        else
        {
            LogPrintf(strprintf("Failed to add dataRef/Minerid transaction ID %s to DB\n", key.ToString()));
        }
    }

    /**
     * Fetch dataref/minerinfo txn details with the given key
     */
    template <typename Entry, typename Key>
    std::optional<Entry> LookupEntry(const Key& key) const
    {
        const auto& dbkey { std::make_pair(Entry::DB_STORAGE_TYPE, key) };

        if(mDBWrapper->Exists(dbkey))
        {
            Readable dbentryread {};
            if(mDBWrapper->Read(dbkey, dbentryread))
                return { Entry{std::move(dbentryread)} };
        }
        return std::nullopt;
    }

    /**
     * Test if dataref/minerinfo txn exists with the given key
     */
    template<typename Entry>
    [[nodiscard]] bool EntryExists(const uint256& txid) const
    {
        const auto& key { std::make_pair(Entry::DB_STORAGE_TYPE, txid) };
        return mDBWrapper->Exists(key);
    }

    /**
     * Delete dataref/minerinfo txn with the given key
     */
    template<typename Entry>
    void DeleteEntry(const uint256& key)
    {
        // Lookup txn before remove so we can calculate its size
        auto GetEntrySize = [this](const uint256& key) -> size_t
        {
            std::optional<Entry> entry { LookupEntry<Entry>(key) };
            if (!entry) {
                LogPrint(BCLog::MINERID, strprintf("Failed to remove non existant dataRef/minerinfo transaction with ID %s from DB\n", key));
                return 0;
            }

            return entry->GetTotalSize();
        };


        const uint64_t diskUsageRemoved = GetEntrySize(key);

        if (!diskUsageRemoved)
            return;

        // Build batch of updates to apply
        CDBBatch batch { *mDBWrapper };
        batch.Erase(std::make_pair(Entry::DB_STORAGE_TYPE, key));
        batch.Write(DB_DISK_USAGE, mDiskUsage - diskUsageRemoved);
        if(mDBWrapper->WriteBatch(batch, true))
            mDiskUsage -= diskUsageRemoved;
        else
            LogPrintf(strprintf("Failed to remove dataRef/minerinfo transaction with ID %s from DB\n", key));
    }


    /**
     * Fetch dataref/minerinfo txn details for all minerinfo txns
     */
    std::vector<Readable> GetAllEntries(const char storage_type) const
    {
        std::vector<Readable> result {};
        std::unique_ptr<CDBIterator> iter { mDBWrapper->NewIterator() };
        iter->SeekToFirst();

        for(; iter->Valid(); iter->Next())
        {
            // Fetch next key of the correct type
            auto key { std::make_pair(storage_type, uint256{}) };
            if(iter->GetKey(key))
            {
                // Fetch entry for this key
                key.first = storage_type;
                if(mDBWrapper->Exists(key))
                {
                    Readable dbentryread{};
                    if(mDBWrapper->Read(key, dbentryread))
                        result.push_back(std::move(dbentryread));
                }
            }
        }

        return result;
    }

    std::vector<Readable> GetAllMinerInfoEntries()
    {
        return GetAllEntries(miner::detail::DataDB::DB_MINERINFO_TXN);
    }

    std::vector<Readable> GetAllDatarefEntries()
    {
        return GetAllEntries(miner::detail::DataDB::DB_DATAREF_TXN);
    }

    // Keep reference to the config
    const Config& mConfig;

    // Our LevelDB wrapper
    std::unique_ptr<CDBWrapper> mDBWrapper {nullptr};

    // Local copy of how much disk space we're using
    uint64_t mDiskUsage {0};
};

} // namespace miner::detail
