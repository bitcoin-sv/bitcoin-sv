// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#ifndef BITCOIN_TEST_MEMPOOL_TEST_ACCESS_H
#define BITCOIN_TEST_MEMPOOL_TEST_ACCESS_H

#include "txmempool.h"
#include "mempooltxdb.h"

namespace
{
    struct CTxMemPoolUnitTestAccessHack {};
}

// Note that the template specialization itself cannot be defined in an
// anonymous namespace, but the template parameter will be in a different
// namespace in each unit test compilation unit, so we're not violating the one
// definition rule.
template<>
struct CTxMemPool::UnitTestAccess<CTxMemPoolUnitTestAccessHack>
{
public:
    CTxMemPool& mempool;

    UnitTestAccess(CTxMemPool& _mempool)
        :mempool(_mempool)
    {}

    // Expose private members of CTxMemPool.
    static const int ROLLING_FEE_HALFLIFE = CTxMemPool::ROLLING_FEE_HALFLIFE;

    auto& mapTx() { return mempool.mapTx; }
    auto& mapNextTx() { return mempool.mapNextTx; }
    auto& mapDeltas() { return mempool.mapDeltas; }

    using txiter = CTxMemPool::txiter;
    using TxLinks = CTxMemPool::TxLinks;
    using txlinksMap = CTxMemPool::txlinksMap;
    using setEntries = CTxMemPool::setEntries;


    void SetBlockMinTxFee(const CFeeRate& feeRate)
    {
        mempool.SetBlockMinTxFee(feeRate);
    }

    void RemoveRecursive(
        const CTransaction &tx,
        const mining::CJournalChangeSetPtr& changeSet,
        MemPoolRemovalReason reason = MemPoolRemovalReason::UNKNOWN)
    {
        mempool.RemoveRecursive(tx, changeSet, reason);
    }
  
    mining::CJournalBuilder& getJournalBuilder()
    {
        return mempool.getJournalBuilder();
    }

    unsigned long PrimaryMempoolSizeNL() const 
    {
        return mempool.PrimaryMempoolSizeNL();
    }
    
    void removeStagedNL(setEntries& stage, mining::CJournalChangeSet& changeSet, MemPoolRemovalReason reason)
    {
        return mempool.removeStagedNL(stage, changeSet, reason);
    }

    void SyncWithMempoolTxDB()
    {
        mempool.mempoolTxDB->Sync();
    }
};

using CTxMemPoolTestAccess = CTxMemPool::UnitTestAccess<CTxMemPoolUnitTestAccessHack>;

namespace {
    struct UnitTestAccessTag;
}

template<> struct CTxMemPoolEntry::UnitTestAccess<UnitTestAccessTag>
{
    CTxMemPoolEntry& entry;
    UnitTestAccess(CTxMemPoolEntry& _entry) : entry(_entry) {}

    auto& nFee() {return entry.nFee;};
    auto& feeDelta() {return entry.feeDelta;};
    auto& nTxSize() {return entry.nTxSize;};
    auto& group() {return entry.group;};
    auto& groupingData() {return entry.groupingData;};

    CTransactionWrapperRef Wrapper() { return entry.tx; }
};

using CTestTxMemPoolEntry = CTxMemPoolEntry::UnitTestAccess<UnitTestAccessTag>;

#endif // BITCOIN_TEST_MEMPOOL_TEST_ACCESS_H
