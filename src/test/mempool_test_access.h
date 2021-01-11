// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#ifndef BITCOIN_TEST_MEMPOOL_TEST_ACCESS_H
#define BITCOIN_TEST_MEMPOOL_TEST_ACCESS_H

#include "txmempool.h"
#include "mempooltxdb.h"

namespace {
    struct UnitTestAccessTag;
}

// Note that the template specialization itself cannot be defined in an
// anonymous namespace, but the template parameter will be in a different
// namespace in each unit test compilation unit, so we're not violating the one
// definition rule.
template<>
struct CTxMemPool::UnitTestAccess<UnitTestAccessTag>
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
    auto& mempoolTxDB() { return mempool.mempoolTxDB; }

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
    
    void removeStagedNL(setEntries& stage, mining::CJournalChangeSet& changeSet, const CTransactionConflict& conflictedWith, MemPoolRemovalReason reason)
    {
        return mempool.removeStagedNL(stage, changeSet, conflictedWith, reason);
    }

    void OpenMempoolTxDB()
    {
        mempool.OpenMempoolTxDB();
    }

    void InitUniqueMempoolTxDB()
    {
        mempool.InitUniqueMempoolTxDB();
    }

    int GetMempoolTxDBUniqueSuffix()
    {
        return mempool.mempoolTxDB_uniqueSuffix;
    }

    void SetMempoolTxDBUniqueSuffix(int uniqueSuffix)
    {
        mempool.mempoolTxDB_uniqueSuffix = uniqueSuffix;
    }

    void InitInMemoryMempoolTxDB()
    {
        mempool.InitInMemoryMempoolTxDB();
    }

    bool CheckMempoolTxDB()
    {
        std::shared_lock lock(mempool.smtx);
        return mempool.CheckMempoolTxDBNL(false);
    }

    void SyncWithMempoolTxDB()
    {
        mempool.mempoolTxDB->Sync();
    }

    void DumpMempool(uint64_t version)
    {
        mempool.DumpMempool(version);
    }

    bool LoadMempool(const Config &config,
                     const task::CCancellationToken& shutdownToken,
                     const std::function<CValidationState(
                         const TxInputDataSPtr& txInputData,
                         const mining::CJournalChangeSetPtr& changeSet,
                         bool limitMempoolSize)>& processValidation)
    {
        return mempool.LoadMempool(config, shutdownToken, processValidation);
    }
};

using CTxMemPoolTestAccess = CTxMemPool::UnitTestAccess<UnitTestAccessTag>;

template<> struct CTxMemPoolEntry::UnitTestAccess<UnitTestAccessTag>
{
    CTxMemPoolEntry& entry;
    UnitTestAccess(CTxMemPoolEntry& _entry) : entry(_entry) {}

    auto& nFee() {return entry.nFee;};
    auto& feeDelta() {return entry.feeDelta;};
    auto& nTxSize() {return entry.nTxSize;};
    auto& group() {return entry.group;};
    auto& groupingData() {return entry.groupingData;};

    static CTransactionWrapperRef GetTxWrapper(const CTxMemPoolEntry& entry)
    {
        return entry.tx;
    }
};

using CTestTxMemPoolEntry = CTxMemPoolEntry::UnitTestAccess<UnitTestAccessTag>;

#endif // BITCOIN_TEST_MEMPOOL_TEST_ACCESS_H
