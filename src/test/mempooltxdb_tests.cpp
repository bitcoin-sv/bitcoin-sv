#include "mining/journal_change_set.h"
#include "mempooltxdb.h"

#include "mempool_test_access.h"

#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>

namespace {
mining::CJournalChangeSetPtr nullChangeSet{nullptr};
}

BOOST_FIXTURE_TEST_SUITE(mempooltxdb_tests, TestingSetup)
BOOST_AUTO_TEST_CASE(SaveOnFullMempool)
{
    TestMemPoolEntryHelper entry;
    // Parent transaction with three children, and three grand-children:
    CMutableTransaction txParent;
    txParent.vin.resize(1);
    txParent.vin[0].scriptSig = CScript() << OP_11;
    txParent.vout.resize(3);
    for (int i = 0; i < 3; i++) {
        txParent.vout[i].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
        txParent.vout[i].nValue = Amount(33000LL);
    }
    CMutableTransaction txChild[3];
    for (int i = 0; i < 3; i++) {
        txChild[i].vin.resize(1);
        txChild[i].vin[0].scriptSig = CScript() << OP_11;
        txChild[i].vin[0].prevout = COutPoint(txParent.GetId(), i);
        txChild[i].vout.resize(1);
        txChild[i].vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
        txChild[i].vout[0].nValue = Amount(11000LL);
    }
    CMutableTransaction txGrandChild[3];
    for (int i = 0; i < 3; i++) {
        txGrandChild[i].vin.resize(1);
        txGrandChild[i].vin[0].scriptSig = CScript() << OP_11;
        txGrandChild[i].vin[0].prevout = COutPoint(txChild[i].GetId(), 0);
        txGrandChild[i].vout.resize(1);
        txGrandChild[i].vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
        txGrandChild[i].vout[0].nValue = Amount(11000LL);
    }

    CTxMemPool testPool;
    CTxMemPoolTestAccess testPoolAccess(testPool);

    // Nothing in pool, remove should do nothing:
    BOOST_CHECK_EQUAL(testPool.Size(), 0);
    testPool.SaveTxsToDisk(10000);
    BOOST_CHECK_EQUAL(testPool.GetDiskUsage(), 0);
    BOOST_CHECK_EQUAL(testPool.Size(), 0);

    // Add transactions:
    testPool.AddUnchecked(txParent.GetId(), entry.FromTx(txParent), nullChangeSet);
    for (int i = 0; i < 3; i++) {
        testPool.AddUnchecked(txChild[i].GetId(), entry.FromTx(txChild[i]), nullChangeSet);
        testPool.AddUnchecked(txGrandChild[i].GetId(), entry.FromTx(txGrandChild[i]), nullChangeSet);
    }

    // Saving transactions to disk doesn't change the mempool size:
    const auto poolSize = testPool.Size();
    testPool.SaveTxsToDisk(10000);
    BOOST_CHECK_EQUAL(testPool.Size(), poolSize);

    // But it does store something to disk:
    const auto diskUsage = testPool.GetDiskUsage();
    BOOST_CHECK_GT(diskUsage, 0);

    // Check that all transactions have been saved to disk:
    uint64_t sizeTXsAdded = 0;
    for (const auto& entry : testPoolAccess.mapTx().get<entry_time>())
    {
        BOOST_CHECK(!entry.IsInMemory());
        sizeTXsAdded += entry.GetTxSize();
    }
    BOOST_CHECK_EQUAL(diskUsage, sizeTXsAdded);
}

BOOST_AUTO_TEST_CASE(RemoveFromDiskOnMempoolTrim)
{
    TestMemPoolEntryHelper entry;
    // A bunch of standalone transactions:
    static constexpr int txCount = 6;
    CMutableTransaction txs[txCount];
    for (int i = 0; i < txCount; i++) {
        txs[i].vin.resize(1);
        txs[i].vin[0].scriptSig = CScript() << OP_11;
        txs[i].vout.resize(1);
        txs[i].vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
        txs[i].vout[0].nValue = Amount(33000LL + i);
    }

    CTxMemPool testPool;

    // Add transactions:
    for (int i = 0; i < txCount; i++) {
        testPool.AddUnchecked(txs[i].GetId(), entry.FromTx(txs[i]), nullChangeSet);
    }

    // Saving transactions to disk doesn't change the mempool size:
    const auto poolSize = testPool.Size();
    BOOST_CHECK_EQUAL(poolSize, txCount);
    testPool.SaveTxsToDisk(10000);
    BOOST_CHECK_EQUAL(testPool.Size(), poolSize);

    // But it does store something to disk:
    BOOST_CHECK_GT(testPool.GetDiskUsage(), 0);

    // Trimming the mempool size should also remove transactions from disk:
    testPool.TrimToSize(0, nullChangeSet);
    BOOST_CHECK_EQUAL(testPool.Size(), 0);
    BOOST_CHECK_EQUAL(testPool.GetDiskUsage(), 0);
}
BOOST_AUTO_TEST_SUITE_END()
