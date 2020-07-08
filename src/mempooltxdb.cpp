// Copyright (c) 2020 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#include "mempooltxdb.h"
#include <core_read.cpp>

CMempoolTxDB::CMempoolTxDB(size_t nCacheSize, bool fMemory, bool fWipe)
    : mempoolTxDB(GetDataDir() / "mempoolTxDB", nCacheSize, fMemory, fWipe) {}

bool CMempoolTxDB::AddTransaction(
    const uint256 &txid, 
    const CTransactionRef &tx)
{
    CDBBatch batch(mempoolTxDB);

    batch.Write(std::make_pair(DB_TRANSACTIONS, txid), *tx);

    uint64_t diskUsage;
    if (!GetDiskUsage(diskUsage))
    {
        diskUsage = 0;
    }
    batch.Write(DB_DISK_USAGE, diskUsage + tx->GetTotalSize());

    return mempoolTxDB.WriteBatch(batch, true);

}

bool CMempoolTxDB::AddTransactions(std::vector<CTransactionRef> &txs) {
    CDBBatch batch(mempoolTxDB);
    uint64_t diskUsage;
    if (!GetDiskUsage(diskUsage)) 
    {
        diskUsage = 0;
    }
    for (unsigned int i = 0; i < txs.size(); i++)
    {
        CTransactionRef tx = txs[i];
        batch.Write(std::make_pair(DB_TRANSACTIONS, tx->GetId()), tx);

        diskUsage += tx->GetTotalSize();
    }
    batch.Write(DB_DISK_USAGE, diskUsage);
    return mempoolTxDB.WriteBatch(batch, true);
}

bool CMempoolTxDB::GetTransaction(const uint256 &txid, CTransactionRef &tx) {
    if (!mempoolTxDB.Exists(std::make_pair(DB_TRANSACTIONS, txid)))
    {
        return false;
    }
    CMutableTransaction txm;
    mempoolTxDB.Read(std::make_pair(DB_TRANSACTIONS, txid), txm);
    tx = MakeTransactionRef(std::move(txm));

    return true;
}

bool CMempoolTxDB::RemoveTransactions(
    const std::vector<uint256> &txidsRemoved, uint64_t diskUsageRemoved) {

    CDBBatch batch(mempoolTxDB);

    for (const uint256 &txid :
         txidsRemoved) {
        batch.Erase(std::make_pair(DB_TRANSACTIONS,txid));
    }

    uint64_t diskUsage;
    if (!GetDiskUsage(diskUsage)) {
        diskUsage = diskUsageRemoved;
    }
    batch.Write(DB_DISK_USAGE, diskUsage - diskUsageRemoved);

    return mempoolTxDB.WriteBatch(batch, true);
}

bool CMempoolTxDB::GetDiskUsage(uint64_t& diskUsageOut)
{
    return mempoolTxDB.Read(DB_DISK_USAGE, diskUsageOut);
}
