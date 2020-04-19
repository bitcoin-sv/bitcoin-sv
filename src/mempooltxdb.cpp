// Copyright (c) 2020 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#include "mempooltxdb.h"
#include "txmempool.h"
#include <core_read.cpp>

CMempoolTxDB::CMempoolTxDB(size_t nCacheSize, bool fMemory, bool fWipe)
    : mempoolTxDB(GetDataDir() / "mempoolTxDB", nCacheSize, fMemory, fWipe) {}

bool CMempoolTxDB::AddTransaction(
    const uint256 &txid, 
    const CTransactionRef &tx)
{
    CDBBatch batch(mempoolTxDB);

    batch.Write(std::make_pair(DB_TRANSACTIONS, txid), *tx);

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
    const std::vector<uint256> &txidsRemoved) {

    CDBBatch batch(mempoolTxDB);

    for (const uint256 &txid :
         txidsRemoved) {
        batch.Erase(std::make_pair(DB_TRANSACTIONS,txid));
    }

    return mempoolTxDB.WriteBatch(batch, true);
}
