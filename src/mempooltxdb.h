// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#ifndef BITCOIN_MEMPOOLTXDB_H
#define BITCOIN_MEMPOOLTXDB_H

#include "dbwrapper.h"
#include "txhasher.h"
#include "tx_mempool_info.h"

#include <boost/uuid/uuid.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <thread>
#include <vector>

/**
 * Read-only access to transactions in the database.
 *
 * The methods exposed by this class are safe to use from multiple threads
 * without synchronization.
 */
class CMempoolTxDBReader {
public:
    virtual ~CMempoolTxDBReader() = default;
    virtual bool GetTransaction(const uint256 &txid, CTransactionRef &tx) = 0;
    virtual bool TransactionExists(const uint256 &txid) = 0;
};

/**
 * Access to the mempool transaction database (mempoolTxDB).
 *
 * Objects of this class should be used in a single-threaded context, otherwise
 * internal accounting will not be consistent. The exceptions to this rule are:
 * <ul>
 *  <li>Methods from the base @ref CMempoolTxDBReader that are implemented here;</li>
 *  <li>GetDiskUsage(), GetTxCount() and GetWriteCount().</li>
 * </ul>
 */
class CMempoolTxDB : public CMempoolTxDBReader {
private:
    // Prefix to store map of Transaction values with txid as a key
    static constexpr char DB_TRANSACTIONS = 'T';
    // Prefix to store disk usage
    static constexpr char DB_DISK_USAGE = 'D';
    // Prefix to store transaaction count
    static constexpr char DB_TX_COUNT = 'C';
    // Prefix to store the mempool.dat cross-reference key
    static constexpr char DB_MEMPOOL_XREF = 'X';

    // Saved database parameters
    const fs::path dbPath;
    const size_t nCacheSize;
    const bool fMemory;

    std::unique_ptr<CDBWrapper> wrapper;

    std::atomic_uint64_t diskUsage {0};
    std::atomic_uint64_t txCount {0};
    std::atomic_uint64_t dbWriteCount {0};

public:
    /**
     * Initializes mempool transaction database. nCacheSize is leveldb cache size
     * for this database. fMemory is false by default. If set to true, leveldb's
     * memory environment will be used. fWipe is false by default. If set to
     * true it will remove all existing data in this database.
     */
    CMempoolTxDB(const fs::path& dbPath, size_t nCacheSize,
                 bool fMemory = false, bool fWipe = false);

    /*
     * Clear the contents of the database by recreating an empty one in place,
     * using the same parameters as when the database object was constructed.
     */
    void ClearDatabase();

    /*
     * Used to add a batch of new transactions into the database
     */
    bool AddTransactions(const std::vector<CTransactionRef>& txs);

    /*
     * Used to retrieve transaction from the database.
     */
    virtual bool GetTransaction(const uint256 &txid, CTransactionRef &tx) override;

    /*
     * Checks if the transaction key is in the database.
     */
    virtual bool TransactionExists(const uint256 &txid) override;

    struct TxData
    {
        TxId txid;
        uint64_t size;

        TxData(TxId&& txid_, uint64_t size_) : txid{std::move(txid_)}, size{size_} {}
        TxData(const TxId& txid_, uint64_t size_) : txid{txid_}, size{size_} {}
    };
    /*
     * Used to remove a batch of transactions from the database.
     */
    bool RemoveTransactions(const std::vector<TxData>& txs);

    /**
     * Return the total size of transactions moved to disk.
     */
    uint64_t GetDiskUsage();

    /*
     * Return the number of transactions moved to disk.
     */
    uint64_t GetTxCount();

    using TxIdSet = std::unordered_set<uint256, SaltedTxidHasher>;
    /*
     * Get the set of transaction keys from the database.
     */
    TxIdSet GetKeys();

    using XrefKey = boost::uuids::uuid;
    /*
     * Set the mempool.dat cross-reference key. Any later change to the
     * database (i.e., calls to ClearDatabase(), AddTransactions() or
     * RemoveTransactions() will remove this key.
     */
    bool SetXrefKey(const XrefKey& xrefKey);

    /*
     * Get the mempool.dat cross-reference key.
     */
    bool GetXrefKey(XrefKey& xrefKey);

    /*
     * Remove the mempool.dat cross-reference key.
     */
    bool RemoveXrefKey();

    // Interface for coalescing batch add/remove operations.
    struct Batch
    {
        using Updater = std::function<void(const TxId&)>;

        struct AddOp
        {
            CTransactionRef tx;
            Updater update;
        };
        std::unordered_map<TxId, AddOp, SaltedTxidHasher> adds;

        struct RmOp
        {
            uint64_t size;
            Updater update;
        };
        std::unordered_map<TxId, RmOp, SaltedTxidHasher> removes;

        void Add(const CTransactionRef& tx, const Updater& update = {nullptr});
        void Remove(const TxId& txid, uint64_t size, const Updater& update = {nullptr});
        void Clear() { adds.clear(); removes.clear(); }
    };
    bool Commit(const Batch& batch);

    // Get the number of batch writes performed on the database.
    uint64_t GetWriteCount();
};


/** Wrapper for CMempoolTxDB for asynchronous writes and deletes. */
class CAsyncMempoolTxDB
{
public:
    CAsyncMempoolTxDB(const fs::path& dbPath, size_t cacheSize, bool inMemory);
    ~CAsyncMempoolTxDB();

    // Syncronize with the background thread after finishing pending tasks.
    void Sync();

    // Synchronously clear the database contents, skip all pending tasks.
    // NOTE: Call this only from contexts where no reads or writes
    //       to the database are possible.
    void Clear();

    // Asynchronously add a transaction to the database.
    void Add(CTransactionWrapperRef&& transactionToAdd);

    // Asynchronously remove a transaction from the database.
    void Remove(CMempoolTxDB::TxData&& transactionToRemove);

    // Get the size of the data in the database.
    uint64_t GetDiskUsage()
    {
        return txdb->GetDiskUsage();
    }

    // Get the number of transactions in the database.
    uint64_t GetTxCount()
    {
        return txdb->GetTxCount();
    }

    // Get the number of batch writes performed on the database.
    uint64_t GetWriteCount()
    {
        return txdb->GetWriteCount();
    }

    // Return a read-only database reference
    std::shared_ptr<CMempoolTxDBReader> GetDatabase();


    // Return the keys that are currently in the database. Keys will not be read
    // in the background thread, so for best results, no background changes
    // should be happening at the same time (e.g., use Sync() first to clear the
    // task queue and make sure no new transactions arrive to the mempool in the
    // meantime).
    CMempoolTxDB::TxIdSet GetTxKeys();


    // The following methods are synchronous wrappers of the equivalent
    // CMempoolTxDB methods.
    bool SetXrefKey(const CMempoolTxDB::XrefKey& xrefKey);
    bool GetXrefKey(CMempoolTxDB::XrefKey& xrefKey);
    bool RemoveXrefKey();

private:
    // Task queue for the worker thread.
    class TaskQueue;
    std::unique_ptr<TaskQueue> queue;

    // Initialize the database and worker thread after the queue.
    std::shared_ptr<CMempoolTxDB> txdb;
    std::thread worker;
    void Work();
};

#endif // BITCOIN_MEMPOOLTXDB_H
