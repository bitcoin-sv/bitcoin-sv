// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#ifndef BITCOIN_TEST_MEMPOOL_TEST_ACCESS_H
#define BITCOIN_TEST_MEMPOOL_TEST_ACCESS_H

#include "txmempool.h"

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

    using setEntries = CTxMemPool::setEntries;

    template<typename... Args>
    bool CalculateMemPoolAncestorsNL(Args... args) { return mempool.CalculateMemPoolAncestorsNL(args...); };

    void RemoveRecursive(
        const CTransaction &tx,
        const mining::CJournalChangeSetPtr& changeSet,
        MemPoolRemovalReason reason = MemPoolRemovalReason::UNKNOWN)
    {
        mempool.RemoveRecursive(tx, changeSet, reason);
    }
};

using CTxMemPoolTestAccess = CTxMemPool::UnitTestAccess<CTxMemPoolUnitTestAccessHack>;

#endif // BITCOIN_TEST_MEMPOOL_TEST_ACCESS_H
