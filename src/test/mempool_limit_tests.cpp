// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#include "mining/journal_change_set.h"
#include "validation.h"
#include "mempooltxdb.h"

#include "mempool_test_access.h"

#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>

#include <vector>

namespace {
    mining::CJournalChangeSetPtr nullChangeSet{nullptr};

    std::vector<CTxMemPoolEntry> GetABunchOfEntries(int howMany, const Amount& amount)
    {
        TestMemPoolEntryHelper entry(amount);
        std::vector<CTxMemPoolEntry> entries;
        for (int i = 0; i < howMany; i++) {
            CMutableTransaction mtx;
            mtx.vin.resize(1);
            mtx.vin[0].scriptSig = CScript() << OP_11;
            mtx.vout.resize(1);
            mtx.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
            mtx.vout[0].nValue = Amount(33000LL + i);
            entries.emplace_back(entry.Time(GetTime()).FromTx(mtx));
        }
        return entries;
    }

    std::vector<CTxMemPoolEntry> StuffMempool(CTxMemPool& pool, int howMany, const Amount &amount)
    {
        auto entries = GetABunchOfEntries(howMany, amount);
        // Add transactions:
        for (auto& entry : entries) {
            pool.AddUnchecked(entry.GetTxId(), entry, TxStorage::memory, nullChangeSet);
        }
        return entries;
    }

    constexpr int N_PRIMARY = 10;
    constexpr Amount A_PRIMARY = Amount(600LL);
}

BOOST_FIXTURE_TEST_SUITE(mempool_limit_tests, TestingSetup)

BOOST_AUTO_TEST_CASE(PrimaryBelowLimitAllInRam)
{
    CTxMemPool testPool;
    CTxMemPoolTestAccess testPoolAccess(testPool);

    const auto entries = StuffMempool(testPool, N_PRIMARY, A_PRIMARY);

    const auto poolSize = testPool.Size();
    const auto poolTotal = testPool.DynamicMemoryUsage();
    const auto limitTotal = poolTotal + 10000;
    const auto limitMemory = poolTotal;
    const auto limitDisk = limitTotal - limitMemory;
    BOOST_CHECK_EQUAL(poolSize, entries.size());

    LimitMempoolSize(testPool, nullChangeSet, MempoolSizeLimits(limitMemory, limitDisk, limitMemory/10, 1000000));
    testPoolAccess.SyncWithMempoolTxDB();
    BOOST_CHECK_EQUAL(testPool.Size(), entries.size());
    BOOST_CHECK_EQUAL(testPool.DynamicMemoryUsage(), poolTotal);
    BOOST_CHECK(testPool.GetDiskUsage() == 0);
}

BOOST_AUTO_TEST_CASE(PrimaryBelowLimitAllOnDisk)
{
    CTxMemPool testPool;
    CTxMemPoolTestAccess testPoolAccess(testPool);

    const auto entries = StuffMempool(testPool, N_PRIMARY, A_PRIMARY);

    const auto poolSize = testPool.Size();
    const auto poolTotal = testPool.DynamicMemoryUsage();
    const auto limitTotal = poolTotal + 10000;
    const auto limitMemory = 100;
    const auto limitDisk = limitTotal - limitMemory;
    BOOST_CHECK_EQUAL(poolSize, entries.size());

    LimitMempoolSize(testPool, nullChangeSet, MempoolSizeLimits(limitMemory, limitDisk, limitMemory/10, 1000000));
    testPoolAccess.SyncWithMempoolTxDB();
    BOOST_CHECK_EQUAL(testPool.Size(), entries.size());
    BOOST_CHECK_EQUAL(testPool.DynamicMemoryUsage(), poolTotal);
    BOOST_CHECK(testPool.GetDiskUsage() > 0);
}

BOOST_AUTO_TEST_CASE(PrimaryBelowLimitThirdOnDisk)
{
    CTxMemPool testPool;
    CTxMemPoolTestAccess testPoolAccess(testPool);

    const auto entries = StuffMempool(testPool, N_PRIMARY, A_PRIMARY);

    const auto poolSize = testPool.Size();
    const auto poolTotal = testPool.DynamicMemoryUsage();
    const auto limitTotal = poolTotal + 10000;
    const auto limitMemory = (poolTotal * 2) / 3;
    const auto limitDisk = limitTotal - limitMemory;
    BOOST_CHECK_EQUAL(poolSize, entries.size());

    LimitMempoolSize(testPool, nullChangeSet, MempoolSizeLimits(limitMemory, limitDisk, limitMemory/10, 1000000));
    testPoolAccess.SyncWithMempoolTxDB();
    BOOST_CHECK_EQUAL(testPool.Size(), entries.size());
    BOOST_CHECK_EQUAL(testPool.DynamicMemoryUsage(), poolTotal);
    BOOST_CHECK(testPool.GetDiskUsage() > 0);
}

BOOST_AUTO_TEST_CASE(PrimaryAtLimitAllInRam)
{
    CTxMemPool testPool;
    CTxMemPoolTestAccess testPoolAccess(testPool);

    const auto entries = StuffMempool(testPool, N_PRIMARY, A_PRIMARY);

    const auto poolSize = testPool.Size();
    const auto poolTotal = testPool.DynamicMemoryUsage();
    const auto limitTotal = poolTotal;
    const auto limitMemory = limitTotal;
    const auto limitDisk = limitTotal - limitMemory;
    BOOST_CHECK_EQUAL(poolSize, entries.size());

    LimitMempoolSize(testPool, nullChangeSet, MempoolSizeLimits(limitMemory, limitDisk, limitMemory/10, 1000000));
    testPoolAccess.SyncWithMempoolTxDB();
    BOOST_CHECK_EQUAL(testPool.Size(), entries.size());
    BOOST_CHECK_EQUAL(testPool.DynamicMemoryUsage(), limitTotal);
    BOOST_CHECK(testPool.GetDiskUsage() == 0);
}

BOOST_AUTO_TEST_CASE(PrimaryAtLimitAllOnDisk)
{
    CTxMemPool testPool;
    CTxMemPoolTestAccess testPoolAccess(testPool);

    const auto entries = StuffMempool(testPool, N_PRIMARY, A_PRIMARY);

    const auto poolSize = testPool.Size();
    const auto poolTotal = testPool.DynamicMemoryUsage();
    const auto limitTotal = poolTotal;
    const auto limitMemory = 0;
    const auto limitDisk = limitTotal - limitMemory;
    BOOST_CHECK_EQUAL(poolSize, entries.size());

    LimitMempoolSize(testPool, nullChangeSet, MempoolSizeLimits(limitMemory, limitDisk, limitMemory/10, 1000000));
    testPoolAccess.SyncWithMempoolTxDB();
    BOOST_CHECK_EQUAL(testPool.Size(), entries.size());
    BOOST_CHECK_EQUAL(testPool.DynamicMemoryUsage(), limitTotal);
    BOOST_CHECK(testPool.GetDiskUsage() > 0);
}

BOOST_AUTO_TEST_CASE(PrimaryAtLimitThirdOnDisk)
{
    CTxMemPool testPool;
    CTxMemPoolTestAccess testPoolAccess(testPool);

    const auto entries = StuffMempool(testPool, N_PRIMARY, A_PRIMARY);

    const auto poolSize = testPool.Size();
    const auto poolTotal = testPool.DynamicMemoryUsage();
    const auto limitTotal = poolTotal;
    const auto limitMemory = (limitTotal * 2) / 3;
    const auto limitDisk = limitTotal - limitMemory;
    BOOST_CHECK_EQUAL(poolSize, entries.size());

    LimitMempoolSize(testPool, nullChangeSet, MempoolSizeLimits(limitMemory, limitDisk, limitMemory/10, 1000000));
    testPoolAccess.SyncWithMempoolTxDB();
    BOOST_CHECK_EQUAL(testPool.Size(), entries.size());
    BOOST_CHECK_EQUAL(testPool.DynamicMemoryUsage(), limitTotal);
    BOOST_CHECK(testPool.GetDiskUsage() > 0);
}

BOOST_AUTO_TEST_CASE(PrimaryAboveLimitInRam)
{
    CTxMemPool testPool;
    CTxMemPoolTestAccess testPoolAccess(testPool);

    const auto entries = StuffMempool(testPool, N_PRIMARY, A_PRIMARY);

    const auto poolSize = testPool.Size();
    const auto poolTotal = testPool.DynamicMemoryUsage();
    const auto limitTotal = (poolTotal * 2) / 3;
    const auto limitMemory = limitTotal;
    const auto limitDisk = limitTotal - limitMemory;
    BOOST_CHECK_EQUAL(poolSize, entries.size());

    LimitMempoolSize(testPool, nullChangeSet, MempoolSizeLimits(limitMemory, limitDisk, limitMemory/10, 1000000));
    testPoolAccess.SyncWithMempoolTxDB();
    BOOST_CHECK(testPool.Size() < entries.size());
    BOOST_CHECK(testPool.DynamicMemoryUsage() < limitTotal);
    BOOST_CHECK(testPool.GetDiskUsage() == 0);
}

BOOST_AUTO_TEST_CASE(PrimaryAboveLimitAllOnDisk)
{
    CTxMemPool testPool;
    CTxMemPoolTestAccess testPoolAccess(testPool);

    const auto entries = StuffMempool(testPool, N_PRIMARY, A_PRIMARY);

    const auto poolSize = testPool.Size();
    const auto poolTotal = testPool.DynamicMemoryUsage();
    const auto limitTotal = (poolTotal * 2) / 3;
    const auto limitMemory = 0;
    const auto limitDisk = limitTotal - limitMemory;
    BOOST_CHECK_EQUAL(poolSize, entries.size());

    LimitMempoolSize(testPool, nullChangeSet, MempoolSizeLimits(limitMemory, limitDisk, limitMemory/10, 1000000));
    testPoolAccess.SyncWithMempoolTxDB();
    BOOST_CHECK(testPool.Size() < entries.size());
    BOOST_CHECK(testPool.DynamicMemoryUsage() < limitTotal);
    BOOST_CHECK(testPool.GetDiskUsage() > 0);
}

BOOST_AUTO_TEST_CASE(PrimaryAboveLimitThirdOnDisk)
{
    CTxMemPool testPool;
    CTxMemPoolTestAccess testPoolAccess(testPool);

    const auto entries = StuffMempool(testPool, N_PRIMARY, A_PRIMARY);

    const auto poolSize = testPool.Size();
    const auto poolTotal = testPool.DynamicMemoryUsage();
    const auto limitTotal = (poolTotal * 2) / 3;
    const auto limitMemory = (limitTotal * 2) / 3;
    const auto limitDisk = limitTotal - limitMemory;
    BOOST_CHECK_EQUAL(poolSize, entries.size());

    LimitMempoolSize(testPool, nullChangeSet, MempoolSizeLimits(limitMemory, limitDisk, limitMemory/10, 1000000));
    testPoolAccess.SyncWithMempoolTxDB();
    BOOST_CHECK(testPool.Size() < entries.size());
    BOOST_CHECK(testPool.DynamicMemoryUsage() < limitTotal);
    BOOST_CHECK(testPool.GetDiskUsage() > 0);
}

BOOST_AUTO_TEST_SUITE_END()
