// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#ifndef BITCOIN_MEMPOOLTXDB_H
#define BITCOIN_MEMPOOLTXDB_H

#include "dbwrapper.h"
#include "primitives/transaction.h"

/** Access to the mempool transaction database (mempoolTxs/) */
class CMempoolTxDB {
private:
    // Prefix to store map of Transaction values with txid as a
    // key
    static constexpr char DB_TRANSACTIONS = 'T';
    // Prefix to store disk usage
    static constexpr char DB_DISK_USAGE = 'D';

    CDBWrapper mempoolTxDB;


public:
    /**
     * Initializes mempool transaction database. nCacheSize is leveldb cache size
     * for this database. fMemory is false by default. If set to true, leveldb's
     * memory environment will be used. fWipe is false by default. If set to
     * true it will remove all existing data in this database.
     */
    CMempoolTxDB(size_t nCacheSize, bool fMemory = false, bool fWipe = false);
    /*
     * Used to add new transaction into the database to sync it with
     * written data.
     */
    bool AddTransaction(const uint256 &txid, const CTransactionRef &tx);

    /*
     * Used to add a batch of new transactions into the database
     */
    bool AddTransactions(std::vector<CTransactionRef> &txs);

    /*
     * Used to retrieve transaction from the database.
     */
    bool GetTransaction(const uint256 &txid, CTransactionRef &tx);

    /*
     * Used to remove all transactions from the database.
     */
    bool RemoveTransactions(const std::vector<uint256> &transactionsToRemove, uint64_t diskUsageRemoved);

    /**
     * Read disk usage value stored in the database to diskUsageOut.
     * Returns false in case record could not be read from the database.
     */
    bool GetDiskUsage(uint64_t &diskUsageOut);
};

#endif // BITCOIN_MEMPOOLDB_H
