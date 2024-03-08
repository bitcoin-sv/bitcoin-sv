// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2020 Bitcoin Association
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "config.h"
#include "mining/journal_change_set.h"
#include "policy/policy.h"
#include "txmempool.h"
#include "util.h"
#include "validation.h"

#include "mempool_test_access.h"

#include "test/test_bitcoin.h"
#include "mempool_test_access.h"

#include <boost/test/unit_test.hpp>
#include <list>
#include <vector>

namespace
{
    mining::CJournalChangeSetPtr nullChangeSet {nullptr};

    std::vector<CTxMemPoolEntry> GetABunchOfEntries(int howMany, int baseValue)
    {
        TestMemPoolEntryHelper entry;
        std::vector<CTxMemPoolEntry> result;
        for (int i = 0; i < howMany; i++) {
            CMutableTransaction mtx;
            mtx.vin.resize(1);
            mtx.vin[0].scriptSig = CScript() << OP_11;
            mtx.vout.resize(1);
            mtx.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
            mtx.vout[0].nValue = Amount(baseValue + i);
            result.emplace_back(entry.FromTx(mtx));
        }
        return result;
    }
}

BOOST_FIXTURE_TEST_SUITE(mempool_tests, TestingSetup)

BOOST_AUTO_TEST_CASE(MempoolRemoveTest) {
    // Test CTxMemPool::remove functionality

    TestMemPoolEntryHelper entry(DEFAULT_TEST_TX_FEE);
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
    CTxMemPoolTestAccess testPoolAccess{testPool};

    // Nothing in pool, remove should do nothing:
    unsigned int poolSize = testPool.Size();
    testPoolAccess.RemoveRecursive(CTransaction(txParent), nullChangeSet);
    BOOST_CHECK_EQUAL(testPool.Size(), poolSize);

    // Just the parent:
    testPool.AddUnchecked(txParent.GetId(), entry.FromTx(txParent), TxStorage::memory, nullChangeSet);
    poolSize = testPool.Size();
    testPoolAccess.RemoveRecursive(CTransaction(txParent), nullChangeSet);
    BOOST_CHECK_EQUAL(testPool.Size(), poolSize - 1);

    // Parent, children, grandchildren:
    testPool.AddUnchecked(txParent.GetId(), entry.FromTx(txParent), TxStorage::memory, nullChangeSet);
    for (int i = 0; i < 3; i++) {
        testPool.AddUnchecked(txChild[i].GetId(), entry.FromTx(txChild[i]), TxStorage::memory, nullChangeSet);
        testPool.AddUnchecked(txGrandChild[i].GetId(),
                              entry.FromTx(txGrandChild[i]), TxStorage::memory, nullChangeSet);
    }
    // Remove Child[0], GrandChild[0] should be removed:
    poolSize = testPool.Size();
    testPoolAccess.RemoveRecursive(CTransaction(txChild[0]), nullChangeSet);
    BOOST_CHECK_EQUAL(testPool.Size(), poolSize - 2);
    // ... make sure grandchild and child are gone:
    poolSize = testPool.Size();
    testPoolAccess.RemoveRecursive(CTransaction(txGrandChild[0]), nullChangeSet);
    BOOST_CHECK_EQUAL(testPool.Size(), poolSize);
    poolSize = testPool.Size();
    testPoolAccess.RemoveRecursive(CTransaction(txChild[0]), nullChangeSet);
    BOOST_CHECK_EQUAL(testPool.Size(), poolSize);
    // Remove parent, all children/grandchildren should go:
    poolSize = testPool.Size();
    testPoolAccess.RemoveRecursive(CTransaction(txParent), nullChangeSet);
    BOOST_CHECK_EQUAL(testPool.Size(), poolSize - 5);
    BOOST_CHECK_EQUAL(testPool.Size(), 0UL);

    // Add children and grandchildren, but NOT the parent (simulate the parent
    // being in a block)
    for (int i = 0; i < 3; i++) {
        testPool.AddUnchecked(txChild[i].GetId(), entry.FromTx(txChild[i]), TxStorage::memory, nullChangeSet);
        testPool.AddUnchecked(txGrandChild[i].GetId(),
                              entry.FromTx(txGrandChild[i]), TxStorage::memory, nullChangeSet);
    }

    // Now remove the parent, as might happen if a block-re-org occurs but the
    // parent cannot be put into the mempool (maybe because it is non-standard):
    poolSize = testPool.Size();
    testPoolAccess.RemoveRecursive(CTransaction(txParent), nullChangeSet);
    BOOST_CHECK_EQUAL(testPool.Size(), poolSize - 6);
    BOOST_CHECK_EQUAL(testPool.Size(), 0UL);
}

BOOST_AUTO_TEST_CASE(MempoolClearTest) {
    // Test CTxMemPool::clear functionality

    TestMemPoolEntryHelper entry(DEFAULT_TEST_TX_FEE);
    // Create a transaction
    CMutableTransaction txParent;
    txParent.vin.resize(1);
    txParent.vin[0].scriptSig = CScript() << OP_11;
    txParent.vout.resize(3);
    for (int i = 0; i < 3; i++) {
        txParent.vout[i].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
        txParent.vout[i].nValue = Amount(33000LL);
    }

    CTxMemPool testPool;
    CTxMemPoolTestAccess testPoolAccess{testPool};

    // Nothing in pool, clear should do nothing:
    testPool.Clear();
    BOOST_CHECK_EQUAL(testPool.Size(), 0UL);

    // Add the transaction
    testPool.AddUnchecked(txParent.GetId(), entry.FromTx(txParent), TxStorage::memory, nullChangeSet);
    BOOST_CHECK_EQUAL(testPool.Size(), 1UL);
    BOOST_CHECK_EQUAL(testPoolAccess.mapTx().size(), 1UL);
    BOOST_CHECK_EQUAL(testPoolAccess.mapNextTx().size(), 1UL);

    // CTxMemPool's members should be empty after a clear
    testPool.Clear();
    BOOST_CHECK_EQUAL(testPool.Size(), 0UL);
    BOOST_CHECK_EQUAL(testPoolAccess.mapTx().size(), 0UL);
    BOOST_CHECK_EQUAL(testPoolAccess.mapNextTx().size(), 0UL);
}

template <typename name>
void CheckSort(CTxMemPool &pool, std::vector<std::string> &sortedOrder) {
    BOOST_CHECK_EQUAL(pool.Size(), sortedOrder.size());
    CTxMemPoolTestAccess testPoolAccess{ pool };
    int count = 0;
    for ( auto& item : testPoolAccess.mapTx().get<name>() )
    {
        BOOST_CHECK_EQUAL( item.GetTxId().ToString(), sortedOrder[count] );
        ++count;
    }
}

BOOST_AUTO_TEST_CASE(MempoolAncestorSetTest) {
    CTxMemPool pool;
    CTxMemPoolTestAccess testPoolAccess{pool};
    TestMemPoolEntryHelper entry;

    /* 3rd highest fee */
    CMutableTransaction tx1 = CMutableTransaction();
    tx1.vout.resize(1);
    tx1.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx1.vout[0].nValue = 10 * COIN;
    pool.AddUnchecked(tx1.GetId(),
                      entry.Fee(Amount(10000LL)).FromTx(tx1), TxStorage::memory, nullChangeSet);

    /* highest fee */
    CMutableTransaction tx2 = CMutableTransaction();
    tx2.vout.resize(1);
    tx2.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx2.vout[0].nValue = 2 * COIN;
    pool.AddUnchecked(tx2.GetId(),
                      entry.Fee(Amount(20000LL)).FromTx(tx2), TxStorage::memory, nullChangeSet);

    /* lowest fee */
    CMutableTransaction tx3 = CMutableTransaction();
    tx3.vout.resize(1);
    tx3.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx3.vout[0].nValue = 5 * COIN;
    pool.AddUnchecked(tx3.GetId(),
                      entry.Fee(Amount(1000LL)).FromTx(tx3), TxStorage::memory, nullChangeSet);

    /* 2nd highest fee */
    CMutableTransaction tx4 = CMutableTransaction();
    tx4.vout.resize(1);
    tx4.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx4.vout[0].nValue = 6 * COIN;
    pool.AddUnchecked(tx4.GetId(),
                      entry.Fee(Amount(15000LL)).FromTx(tx4), TxStorage::memory, nullChangeSet);

    /* equal fee rate to tx1, but newer */
    CMutableTransaction tx5 = CMutableTransaction();
    tx5.vout.resize(1);
    tx5.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx5.vout[0].nValue = 11 * COIN;
    entry.nTime = 1;
    pool.AddUnchecked(tx5.GetId(), entry.Fee(Amount(10000LL)).FromTx(tx5), TxStorage::memory, nullChangeSet);
    BOOST_CHECK_EQUAL(testPoolAccess.PrimaryMempoolSizeNL(), 5UL);
    BOOST_CHECK_EQUAL(pool.Size(), 5UL);

    /* low fee but with high fee child, will go into secondary mempool */
    /* tx6 -> tx7 -> tx8, tx9 -> tx10 */
    CMutableTransaction tx6 = CMutableTransaction();
    tx6.vout.resize(1);
    tx6.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx6.vout[0].nValue = 20 * COIN;
    pool.AddUnchecked(tx6.GetId(), entry.Fee(Amount(0LL)).FromTx(tx6), TxStorage::memory, nullChangeSet);
    /* primary mempool size did not change */
    BOOST_CHECK_EQUAL(testPoolAccess.PrimaryMempoolSizeNL(), 5UL);
    BOOST_CHECK_EQUAL(pool.Size(), 6UL);

    CTxMemPoolTestAccess::setEntries setAncestors;
    setAncestors.insert(testPoolAccess.mapTx().find(tx6.GetId()));
    CMutableTransaction tx7 = CMutableTransaction();
    tx7.vin.resize(1);
    tx7.vin[0].prevout = COutPoint(tx6.GetId(), 0);
    tx7.vin[0].scriptSig = CScript() << OP_11;
    tx7.vout.resize(2);
    tx7.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx7.vout[0].nValue = 10 * COIN;
    tx7.vout[1].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx7.vout[1].nValue = 1 * COIN;
    
    {
        std::string error;
        
        BOOST_CHECK_EQUAL(
            pool.CheckAncestorLimits( entry.FromTx(tx7), 2, 2, error),
            true);
        BOOST_CHECK_EQUAL(error, "");

        BOOST_CHECK_EQUAL(
            pool.CheckAncestorLimits( entry.FromTx(tx7), 1, 2, error),
            false);
        BOOST_CHECK_EQUAL(error, "too many unconfirmed parents, 1 [limit: 1]");

        BOOST_CHECK_EQUAL(
            pool.CheckAncestorLimits( entry.FromTx(tx7), 2, 1, error),
            false);
        BOOST_CHECK_EQUAL(error, "too many unconfirmed parents which we are not willing to mine, 1 [limit: 1]");
    }

    /* will pull tx6 into the primary pool with tx7, whose fee was set above */
    pool.AddUnchecked(tx7.GetId(), entry.Fee(Amount(2000000LL)).FromTx(tx7), TxStorage::memory, nullChangeSet);
    BOOST_CHECK_EQUAL(testPoolAccess.PrimaryMempoolSizeNL(), 7UL);
    BOOST_CHECK_EQUAL(pool.Size(), 7UL);
}

BOOST_AUTO_TEST_CASE(MempoolSizeLimitTest) {
    CTxMemPool pool;
    TestMemPoolEntryHelper entry;
    Amount feeIncrement = MEMPOOL_FULL_FEE_INCREMENT.GetFeePerK();

    CMutableTransaction tx1 = CMutableTransaction();
    tx1.vin.resize(1);
    tx1.vin[0].scriptSig = CScript() << OP_1;
    tx1.vout.resize(1);
    tx1.vout[0].scriptPubKey = CScript() << OP_1 << OP_EQUAL;
    tx1.vout[0].nValue = 10 * COIN;
    pool.AddUnchecked(tx1.GetId(),
                      entry.Fee(Amount(10000LL)).FromTx(tx1, &pool), TxStorage::memory, nullChangeSet);

    CMutableTransaction tx2 = CMutableTransaction();
    tx2.vin.resize(1);
    tx2.vin[0].scriptSig = CScript() << OP_2;
    tx2.vout.resize(1);
    tx2.vout[0].scriptPubKey = CScript() << OP_2 << OP_EQUAL;
    tx2.vout[0].nValue = 10 * COIN;
    pool.AddUnchecked(tx2.GetId(),
                      entry.Fee(Amount(5000LL)).FromTx(tx2, &pool), TxStorage::memory, nullChangeSet);

    // should do nothing
    pool.TrimToSize(pool.DynamicMemoryUsage(), nullChangeSet);
    BOOST_CHECK(pool.Exists(tx1.GetId()));
    BOOST_CHECK(pool.Exists(tx2.GetId()));

    // should remove the lower-feerate transaction
    pool.TrimToSize(pool.DynamicMemoryUsage() * 3 / 4, nullChangeSet);
    BOOST_CHECK(pool.Exists(tx1.GetId()));
    BOOST_CHECK(!pool.Exists(tx2.GetId()));

    pool.AddUnchecked(tx2.GetId(), entry.FromTx(tx2, &pool), TxStorage::memory, nullChangeSet);
    CMutableTransaction tx3 = CMutableTransaction();
    tx3.vin.resize(1);
    tx3.vin[0].prevout = COutPoint(tx2.GetId(), 0);
    tx3.vin[0].scriptSig = CScript() << OP_2;
    tx3.vout.resize(1);
    tx3.vout[0].scriptPubKey = CScript() << OP_3 << OP_EQUAL;
    tx3.vout[0].nValue = 10 * COIN;
    pool.AddUnchecked(tx3.GetId(),
                      entry.Fee(Amount(20000LL)).FromTx(tx3, &pool), TxStorage::memory, nullChangeSet);

    // tx3 should pay for tx2 (CPFP)
    pool.TrimToSize(pool.DynamicMemoryUsage() * 3 / 4, nullChangeSet);
    BOOST_CHECK(!pool.Exists(tx1.GetId()));
    BOOST_CHECK(pool.Exists(tx2.GetId()));
    BOOST_CHECK(pool.Exists(tx3.GetId()));

    // mempool is limited to tx1's size in memory usage, so nothing fits
    pool.TrimToSize(CTransaction(tx1).GetTotalSize(), nullChangeSet);
    BOOST_CHECK(!pool.Exists(tx1.GetId()));
    BOOST_CHECK(!pool.Exists(tx2.GetId()));
    BOOST_CHECK(!pool.Exists(tx3.GetId()));

    CFeeRate maxFeeRateRemoved(Amount(20000),
                               CTransaction(tx3).GetTotalSize());
    BOOST_CHECK_EQUAL(pool.GetMinFee(1).GetFeePerK(),
                      maxFeeRateRemoved.GetFeePerK() + feeIncrement);

    CMutableTransaction tx4 = CMutableTransaction();
    tx4.vin.resize(2);
    tx4.vin[0].prevout = COutPoint();
    tx4.vin[0].scriptSig = CScript() << OP_4;
    tx4.vin[1].prevout = COutPoint();
    tx4.vin[1].scriptSig = CScript() << OP_4;
    tx4.vout.resize(2);
    tx4.vout[0].scriptPubKey = CScript() << OP_4 << OP_EQUAL;
    tx4.vout[0].nValue = 10 * COIN;
    tx4.vout[1].scriptPubKey = CScript() << OP_4 << OP_EQUAL;
    tx4.vout[1].nValue = 10 * COIN;

    CMutableTransaction tx5 = CMutableTransaction();
    tx5.vin.resize(2);
    tx5.vin[0].prevout = COutPoint(tx4.GetId(), 0);
    tx5.vin[0].scriptSig = CScript() << OP_4;
    tx5.vin[1].prevout = COutPoint();
    tx5.vin[1].scriptSig = CScript() << OP_5;
    tx5.vout.resize(2);
    tx5.vout[0].scriptPubKey = CScript() << OP_5 << OP_EQUAL;
    tx5.vout[0].nValue = 10 * COIN;
    tx5.vout[1].scriptPubKey = CScript() << OP_5 << OP_EQUAL;
    tx5.vout[1].nValue = 10 * COIN;

    CMutableTransaction tx6 = CMutableTransaction();
    tx6.vin.resize(2);
    tx6.vin[0].prevout = COutPoint(tx4.GetId(), 1);
    tx6.vin[0].scriptSig = CScript() << OP_4;
    tx6.vin[1].prevout = COutPoint();
    tx6.vin[1].scriptSig = CScript() << OP_6;
    tx6.vout.resize(2);
    tx6.vout[0].scriptPubKey = CScript() << OP_6 << OP_EQUAL;
    tx6.vout[0].nValue = 10 * COIN;
    tx6.vout[1].scriptPubKey = CScript() << OP_6 << OP_EQUAL;
    tx6.vout[1].nValue = 10 * COIN;

    CMutableTransaction tx7 = CMutableTransaction();
    tx7.vin.resize(2);
    tx7.vin[0].prevout = COutPoint(tx5.GetId(), 0);
    tx7.vin[0].scriptSig = CScript() << OP_5;
    tx7.vin[1].prevout = COutPoint(tx6.GetId(), 0);
    tx7.vin[1].scriptSig = CScript() << OP_6;
    tx7.vout.resize(2);
    tx7.vout[0].scriptPubKey = CScript() << OP_7 << OP_EQUAL;
    tx7.vout[0].nValue = 10 * COIN;
    tx7.vout[1].scriptPubKey = CScript() << OP_7 << OP_EQUAL;
    tx7.vout[1].nValue = 10 * COIN;

    pool.AddUnchecked(tx4.GetId(),
                      entry.Fee(Amount(7000LL)).FromTx(tx4, &pool), TxStorage::memory, nullChangeSet);
    pool.AddUnchecked(tx5.GetId(),
                      entry.Fee(Amount(1000LL)).FromTx(tx5, &pool), TxStorage::memory, nullChangeSet);
    pool.AddUnchecked(tx6.GetId(),
                      entry.Fee(Amount(1100LL)).FromTx(tx6, &pool), TxStorage::memory, nullChangeSet);
    pool.AddUnchecked(tx7.GetId(),
                      entry.Fee(Amount(9000LL)).FromTx(tx7, &pool), TxStorage::memory, nullChangeSet);

    // we only require this remove, at max, 2 txn, because its not clear what
    // we're really optimizing for aside from that
    pool.TrimToSize(pool.DynamicMemoryUsage() - 1, nullChangeSet);
    BOOST_CHECK(pool.Exists(tx4.GetId()));
    BOOST_CHECK(pool.Exists(tx6.GetId()));
    BOOST_CHECK(!pool.Exists(tx7.GetId()));

    if (!pool.Exists(tx5.GetId()))
        pool.AddUnchecked(tx5.GetId(),
                          entry.Fee(Amount(1000LL)).FromTx(tx5, &pool), TxStorage::memory, nullChangeSet);
    pool.AddUnchecked(tx7.GetId(),
                      entry.Fee(Amount(9000LL)).FromTx(tx7, &pool), TxStorage::memory, nullChangeSet);

    // should maximize mempool size by only removing 5/7
    pool.TrimToSize(pool.DynamicMemoryUsage() / 2, nullChangeSet);
    BOOST_CHECK(pool.Exists(tx4.GetId()));
    BOOST_CHECK(!pool.Exists(tx5.GetId()));
    BOOST_CHECK(pool.Exists(tx6.GetId()));
    BOOST_CHECK(!pool.Exists(tx7.GetId()));

    pool.AddUnchecked(tx5.GetId(),
                      entry.Fee(Amount(1000LL)).FromTx(tx5, &pool), TxStorage::memory, nullChangeSet);
    pool.AddUnchecked(tx7.GetId(),
                      entry.Fee(Amount(9000LL)).FromTx(tx7, &pool), TxStorage::memory, nullChangeSet);

    std::vector<CTransactionRef> vtx;
    SetMockTime(42);
    SetMockTime(42 + CTxMemPoolTestAccess::ROLLING_FEE_HALFLIFE);
    BOOST_CHECK_EQUAL(pool.GetMinFee(1).GetFeePerK(),
                      maxFeeRateRemoved.GetFeePerK() + feeIncrement);
    // ... we should keep the same min fee until we get a block

    auto dummyBlockHash = uint256{};
    pool.RemoveForBlock(vtx, nullChangeSet, dummyBlockHash, vtx, testConfig);
    SetMockTime(42 + 2 * CTxMemPoolTestAccess::ROLLING_FEE_HALFLIFE);
    BOOST_CHECK_EQUAL(pool.GetMinFee(1).GetFeePerK(),
                      (maxFeeRateRemoved.GetFeePerK() + feeIncrement) / 2);
    // ... then feerate should drop 1/2 each halflife

    SetMockTime(42 + 2 * CTxMemPoolTestAccess::ROLLING_FEE_HALFLIFE +
                CTxMemPoolTestAccess::ROLLING_FEE_HALFLIFE / 2);
    BOOST_CHECK_EQUAL(
        pool.GetMinFee(pool.DynamicMemoryUsage() * 5 / 2).GetFeePerK(),
        (maxFeeRateRemoved.GetFeePerK() + feeIncrement) / 4);
    // ... with a 1/2 halflife when mempool is < 1/2 its target size

    SetMockTime(42 + 2 * CTxMemPoolTestAccess::ROLLING_FEE_HALFLIFE +
                CTxMemPoolTestAccess::ROLLING_FEE_HALFLIFE / 2 +
                CTxMemPoolTestAccess::ROLLING_FEE_HALFLIFE / 4);
    BOOST_CHECK_EQUAL(
        pool.GetMinFee(pool.DynamicMemoryUsage() * 9 / 2).GetFeePerK(),
        (maxFeeRateRemoved.GetFeePerK() + feeIncrement) / 8);
    // ... with a 1/4 halflife when mempool is < 1/4 its target size

    SetMockTime(0);
}

BOOST_AUTO_TEST_CASE(CTxPrioritizerTest) {
    // Create a transaction
    CMutableTransaction txParent;
    txParent.vin.resize(1);
    txParent.vin[0].scriptSig = CScript() << OP_11;
    txParent.vout.resize(3);
    for (int i = 0; i < 3; i++) {
        txParent.vout[i].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
        txParent.vout[i].nValue = Amount(33000LL);
    }

    CTxMemPool testPool;
    CTxMemPoolTestAccess testPoolAccess{testPool};
    const TxId& txid = txParent.GetId();
    // A lambda-helper to add a txn to the empty testPool and to do basic checks.
    const auto add_txn_to_testpool = [&testPool, &testPoolAccess](
        const CMutableTransaction& txParent,
        const TxId& txid) {
        BOOST_CHECK_EQUAL(testPool.Size(), 0UL);
        testPool.AddUnchecked(txid, TestMemPoolEntryHelper{DEFAULT_TEST_TX_FEE}.FromTx(txParent),
                              TxStorage::memory, nullChangeSet);
        BOOST_CHECK_EQUAL(testPool.Size(), 1UL);
        BOOST_CHECK(!testPoolAccess.mapDeltas().count(txid));
    };
    // A lambda-helper to check if an entry was added to the mapDeltas.
    const auto check_entry_added_to_mapdeltas = [&testPoolAccess](const TxId& txid)
    {
        BOOST_CHECK(testPoolAccess.mapDeltas().count(txid));
        BOOST_CHECK_EQUAL(testPoolAccess.mapDeltas()[txid], MAX_MONEY);
    };
    // Case 1.
    // Instantiate txPrioritizer to prioritise a single txn.
    {
        // Add txn to the testPool
        add_txn_to_testpool(txParent, txid);
        // Instantiate txPrioritizer with a single tx.
        CTxPrioritizer txPrioritizer(testPool, txid);
        // This should add a new entry into mapDeltas.
        check_entry_added_to_mapdeltas(txid);
        // Remove txid from the mapTx.
        testPoolAccess.mapTx().erase(txid);
    }
    // During txPrioritizer's destruction txid should be removed from mapDeltas.
    BOOST_CHECK(!testPoolAccess.mapDeltas().count(txid));
    testPool.Clear();
    // Case 2.
    // Instantiate txPrioritizer to prioritise a vector of txns.
    {
        // Add txn to the testPool
        add_txn_to_testpool(txParent, txid);
        // Instantiate txPrioritizer with a vector.
        CTxPrioritizer txPrioritizer(testPool, std::vector<TxId>{txid});
        // This should add a new entry into mapDeltas.
        check_entry_added_to_mapdeltas(txid);
        // Remove txid from the mapTx.
        testPoolAccess.mapTx().erase(txid);
    }
    // During txPrioritizer's destruction txid should be removed from mapDeltas.
    BOOST_CHECK(!testPoolAccess.mapDeltas().count(txid));
    testPool.Clear();
    // Case 3.
    // Instantiate a no-op txPrioritizer with a null TxId.
    {
        // Add txn to the testPool
        add_txn_to_testpool(txParent, txid);
        // Instantiate txPrioritizer with a null TxId.
        CTxPrioritizer txPrioritizer(testPool, TxId());
        // There should be no operations on the mapDeltas.
        BOOST_CHECK(testPoolAccess.mapDeltas().empty());
        // Remove txid from the mapTx.
        testPoolAccess.mapTx().erase(txid);
    }
    // Check if mapDeltas remains empty.
    BOOST_CHECK(testPoolAccess.mapDeltas().empty());
    testPool.Clear();
    // Case 4.
    // Instantiate a no-op txPrioritizer with an empty vector.
    {
        // Add txn to the testPool
        add_txn_to_testpool(txParent, txid);
        // Instantiate txPrioritizer with an empty vector.
        CTxPrioritizer txPrioritizer(testPool, std::vector<TxId>{});
        // There should be no operations on the mapDeltas.
        BOOST_CHECK(testPoolAccess.mapDeltas().empty());
        // Remove txid from the mapTx.
        testPoolAccess.mapTx().erase(txid);
    }
    // Check if mapDeltas remains empty.
    BOOST_CHECK(testPoolAccess.mapDeltas().empty());
}

BOOST_AUTO_TEST_CASE(SecondaryMempoolDecisionTest) {
    CTxMemPool pool;
    CTxMemPoolTestAccess testPoolAccess{pool};
    TestMemPoolEntryHelper entry;

    testPoolAccess.SetBlockMinTxFee({Amount(100), 1});

    /* Fee highe enough to enter the primary mempool. */
    CMutableTransaction tx1 = CMutableTransaction();
    tx1.vout.resize(1);
    tx1.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx1.vout[0].nValue = 10 * COIN;
    pool.AddUnchecked(tx1.GetId(), entry.Fee(Amount(10000LL)).FromTx(tx1), TxStorage::memory, nullChangeSet);
    const auto tx1it = testPoolAccess.mapTx().find(tx1.GetId());
    BOOST_CHECK(tx1it != testPoolAccess.mapTx().end());

    /* Fee too low to enter the primary mempool. */
    CMutableTransaction tx2 = CMutableTransaction();
    tx2.vout.resize(1);
    tx2.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx2.vout[0].nValue = 2 * COIN;
    pool.AddUnchecked(tx2.GetId(), entry.Fee(Amount(1LL)).FromTx(tx2), TxStorage::memory, nullChangeSet);
    const auto tx2it = testPoolAccess.mapTx().find(tx2.GetId());
    BOOST_CHECK(tx2it != testPoolAccess.mapTx().end());

    BOOST_CHECK_EQUAL(testPoolAccess.PrimaryMempoolSizeNL(), 1UL);
    BOOST_CHECK(tx1it->IsInPrimaryMempool());
    BOOST_CHECK(!tx2it->IsInPrimaryMempool());
}

BOOST_AUTO_TEST_CASE(SecondaryMempoolStatsTest) {
    CTxMemPool pool;
    CTxMemPoolTestAccess testPoolAccess{pool};
    TestMemPoolEntryHelper entry;

    testPoolAccess.SetBlockMinTxFee({Amount(100), 1});

    CMutableTransaction tx1 = CMutableTransaction();
    tx1.vout.resize(1);
    tx1.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx1.vout[0].nValue = 5 * COIN;
    pool.AddUnchecked(tx1.GetId(), entry.Fee(Amount(2LL)).FromTx(tx1), TxStorage::memory, nullChangeSet);
    const auto tx1it = testPoolAccess.mapTx().find(tx1.GetId());
    BOOST_CHECK(tx1it != testPoolAccess.mapTx().end());

    CMutableTransaction tx2 = CMutableTransaction();
    tx2.vout.resize(1);
    tx2.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx2.vout[0].nValue = 10 * COIN;
    pool.AddUnchecked(tx2.GetId(), entry.Fee(Amount(1LL)).FromTx(tx2), TxStorage::memory, nullChangeSet);
    const auto tx2it = testPoolAccess.mapTx().find(tx2.GetId());
    BOOST_CHECK(tx2it != testPoolAccess.mapTx().end());

    CMutableTransaction tx3 = CMutableTransaction();
    tx3.vin.resize(2);
    tx3.vin[0].prevout = COutPoint(tx1.GetId(), 0);
    tx3.vin[0].scriptSig = CScript() << OP_5;
    tx3.vin[1].prevout = COutPoint(tx2.GetId(), 0);
    tx3.vin[1].scriptSig = CScript() << OP_5;
    tx3.vout.resize(1);
    tx3.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx3.vout[0].nValue = 15 * COIN;
    pool.AddUnchecked(tx3.GetId(), entry.Fee(Amount(3LL)).FromTx(tx3), TxStorage::memory, nullChangeSet);
    const auto tx3it = testPoolAccess.mapTx().find(tx3.GetId());
    BOOST_CHECK(tx3it != testPoolAccess.mapTx().end());

    BOOST_CHECK_EQUAL(testPoolAccess.PrimaryMempoolSizeNL(), 0UL);

    CTestTxMemPoolEntry testTx1(const_cast<CTxMemPoolEntry&>(*tx1it));
    BOOST_CHECK(!tx1it->IsInPrimaryMempool());
    BOOST_CHECK_EQUAL(testTx1.groupingData()->fee, tx1it->GetFee());
    BOOST_CHECK_EQUAL(testTx1.groupingData()->feeDelta, tx1it->GetFeeDelta());
    BOOST_CHECK_EQUAL(testTx1.groupingData()->size, tx1it->GetTxSize());
    BOOST_CHECK_EQUAL(testTx1.groupingData()->ancestorsCount, 0U);

    CTestTxMemPoolEntry testTx2(const_cast<CTxMemPoolEntry&>(*tx2it));
    BOOST_CHECK(!tx2it->IsInPrimaryMempool());
    BOOST_CHECK_EQUAL(testTx2.groupingData()->fee, tx2it->GetFee());
    BOOST_CHECK_EQUAL(testTx2.groupingData()->feeDelta, tx2it->GetFeeDelta());
    BOOST_CHECK_EQUAL(testTx2.groupingData()->size, tx2it->GetTxSize());
    BOOST_CHECK_EQUAL(testTx2.groupingData()->ancestorsCount, 0U);

    CTestTxMemPoolEntry testTx3(const_cast<CTxMemPoolEntry&>(*tx3it));
    BOOST_CHECK(!tx3it->IsInPrimaryMempool());
    BOOST_CHECK_EQUAL(testTx3.groupingData()->fee, tx1it->GetFee() + tx2it->GetFee() + tx3it->GetFee());
    BOOST_CHECK_EQUAL(testTx3.groupingData()->feeDelta, tx1it->GetFeeDelta() + tx2it->GetFeeDelta() + tx3it->GetFeeDelta());
    BOOST_CHECK_EQUAL(testTx3.groupingData()->size, tx1it->GetTxSize() + tx2it->GetTxSize() + tx3it->GetTxSize());
    BOOST_CHECK_EQUAL(testTx3.groupingData()->ancestorsCount, 2U);
}

BOOST_AUTO_TEST_CASE(SecondaryMempoolComplexChainTest) {
    //               tx1
    //                |
    //          +-----+-----+
    //          |     |     |
    //         tx2   tx3    |
    //          |     |     |
    //          +-----+-----+
    //                |
    //               tx4
    //                |
    //               tx5    <-- paying transaction

    CTxMemPool pool;
    CTxMemPoolTestAccess testPoolAccess{pool};
    TestMemPoolEntryHelper entry;

    CMutableTransaction tx1 = CMutableTransaction();
    tx1.vout.resize(3);
    for (int i = 0; i < 3; ++i) {
        tx1.vout[i].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
        tx1.vout[i].nValue = (5 + i) * COIN;
    }
    pool.AddUnchecked(tx1.GetId(), entry.FromTx(tx1), TxStorage::memory, nullChangeSet);
    const auto tx1it = testPoolAccess.mapTx().find(tx1.GetId());
    BOOST_CHECK(tx1it != testPoolAccess.mapTx().end());
    BOOST_CHECK(!tx1it->IsInPrimaryMempool());
    CTestTxMemPoolEntry entry1access(const_cast<CTxMemPoolEntry&>(*tx1it));
    const auto& group1data = entry1access.groupingData();
    BOOST_REQUIRE(group1data.has_value());
    BOOST_CHECK_EQUAL(group1data->ancestorsCount, 0U); // exact

    CMutableTransaction tx2 = CMutableTransaction();
    tx2.vin.resize(1);
    tx2.vin[0].prevout = COutPoint(tx1.GetId(), 0);
    tx2.vin[0].scriptSig = CScript() << OP_5;
    tx2.vout.resize(1);
    tx2.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx2.vout[0].nValue = 1 * COIN;
    pool.AddUnchecked(tx2.GetId(), entry.FromTx(tx2), TxStorage::memory, nullChangeSet);
    const auto tx2it = testPoolAccess.mapTx().find(tx2.GetId());
    BOOST_CHECK(tx2it != testPoolAccess.mapTx().end());
    BOOST_CHECK(!tx2it->IsInPrimaryMempool());
    CTestTxMemPoolEntry entry2access(const_cast<CTxMemPoolEntry&>(*tx2it));
    const auto& group2data = entry2access.groupingData();
    BOOST_REQUIRE(group2data.has_value());
    BOOST_CHECK_EQUAL(group2data->ancestorsCount, 1U); // exact

    CMutableTransaction tx3 = CMutableTransaction();
    tx3.vin.resize(1);
    tx3.vin[0].prevout = COutPoint(tx1.GetId(), 0);
    tx3.vin[0].scriptSig = CScript() << OP_5;
    tx3.vout.resize(1);
    tx3.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx3.vout[0].nValue = 2 * COIN;
    pool.AddUnchecked(tx3.GetId(), entry.FromTx(tx3), TxStorage::memory, nullChangeSet);
    const auto tx3it = testPoolAccess.mapTx().find(tx3.GetId());
    BOOST_CHECK(tx3it != testPoolAccess.mapTx().end());
    BOOST_CHECK(!tx3it->IsInPrimaryMempool());
    CTestTxMemPoolEntry entry3access(const_cast<CTxMemPoolEntry&>(*tx3it));
    const auto& group3data = entry3access.groupingData();
    BOOST_REQUIRE(group3data.has_value());
    BOOST_CHECK_EQUAL(group3data->ancestorsCount, 1U); // exact

    CMutableTransaction tx4 = CMutableTransaction();
    tx4.vin.resize(3);
    tx4.vin[0].prevout = COutPoint(tx2.GetId(), 0);
    tx4.vin[0].scriptSig = CScript() << OP_5;
    tx4.vin[1].prevout = COutPoint(tx3.GetId(), 0);
    tx4.vin[1].scriptSig = CScript() << OP_5;
    tx4.vin[2].prevout = COutPoint(tx1.GetId(), 0);
    tx4.vin[2].scriptSig = CScript() << OP_5;
    tx4.vout.resize(1);
    tx4.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx4.vout[0].nValue = 3 * COIN;
    pool.AddUnchecked(tx4.GetId(), entry.FromTx(tx4), TxStorage::memory, nullChangeSet);
    const auto tx4it = testPoolAccess.mapTx().find(tx4.GetId());
    BOOST_CHECK(tx4it != testPoolAccess.mapTx().end());
    BOOST_CHECK(!tx4it->IsInPrimaryMempool());

    BOOST_CHECK_EQUAL(testPoolAccess.PrimaryMempoolSizeNL(), 0UL);
    BOOST_CHECK_EQUAL(pool.Size(), 4UL);

    CTestTxMemPoolEntry entry4access(const_cast<CTxMemPoolEntry&>(*tx4it));
    const auto& group4data = entry4access.groupingData();
    BOOST_REQUIRE(group4data.has_value());
    BOOST_CHECK_EQUAL(group4data->ancestorsCount, 5U); // not exact

    // Pull everything into the primary mempool as a group.
    CMutableTransaction tx5 = CMutableTransaction();
    tx5.vin.resize(1);
    tx5.vin[0].prevout = COutPoint(tx4.GetId(), 0);
    tx5.vin[0].scriptSig = CScript() << OP_5;
    tx5.vout.resize(1);
    tx5.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx5.vout[0].nValue = 4 * COIN;
    pool.AddUnchecked(tx5.GetId(), entry.Fee(Amount(100000)).FromTx(tx5), TxStorage::memory, nullChangeSet);

    BOOST_CHECK_EQUAL(testPoolAccess.PrimaryMempoolSizeNL(), 5UL);
    BOOST_CHECK_EQUAL(pool.Size(), 5UL);
    BOOST_CHECK(tx1it->IsInPrimaryMempool());
    BOOST_CHECK(tx2it->IsInPrimaryMempool());
    BOOST_CHECK(tx3it->IsInPrimaryMempool());
    BOOST_CHECK(tx4it->IsInPrimaryMempool());
    BOOST_CHECK(!group4data.has_value());
}

BOOST_AUTO_TEST_CASE(ReorgWithTransactionsOnDisk)
{
    CTxMemPool testPool;
    CTxMemPoolTestAccess testPoolAccess{testPool};

    const auto beforeCount = 31U;
    uint64_t beforeSize = 0;
    const auto afterCount = 29U;

    const auto before = GetABunchOfEntries(beforeCount, 33000);
    const auto after = GetABunchOfEntries(afterCount, 34000);

    // Fill the mempool
    for (auto& e : before)
    {
        testPool.AddUnchecked(e.GetTxId(), e, TxStorage::memory, nullChangeSet);
        beforeSize += e.GetTxSize();
    }
    for (auto& e : after)
    {
        testPool.AddUnchecked(e.GetTxId(), e, TxStorage::memory, nullChangeSet);
    }

    testPoolAccess.SyncWithMempoolTxDB();
    BOOST_CHECK_EQUAL(testPool.Size(), beforeCount + afterCount);
    BOOST_CHECK_EQUAL(testPool.GetDiskUsage(), 0U);
    BOOST_CHECK_EQUAL(testPool.GetDiskTxCount(), 0U);
    BOOST_CHECK(testPoolAccess.CheckMempoolTxDB());

    // Write half of the pool to disk
    testPool.SaveTxsToDisk(beforeSize);
    testPoolAccess.SyncWithMempoolTxDB();
    BOOST_CHECK_EQUAL(testPool.Size(), beforeCount + afterCount);
    BOOST_CHECK_EQUAL(testPool.GetDiskUsage(), beforeSize);
    BOOST_CHECK_EQUAL(testPool.GetDiskTxCount(), beforeCount);
    BOOST_CHECK(testPoolAccess.CheckMempoolTxDB());

    // Fake, no-op reorg. The shape of the mempool shouldn't change.
    {
        DisconnectedBlockTransactions disconnectPool;
        auto changeSet = testPool.getJournalBuilder().getNewChangeSet(mining::JournalUpdateReason::REORG);
        LOCK(cs_main);
        testPool.AddToMempoolForReorg(testConfig, disconnectPool, changeSet);
    }
    testPoolAccess.SyncWithMempoolTxDB();
    BOOST_CHECK_EQUAL(testPool.Size(), beforeCount + afterCount);
    BOOST_CHECK_EQUAL(testPool.GetDiskUsage(), beforeSize);
    BOOST_CHECK_EQUAL(testPool.GetDiskTxCount(), beforeCount);
    BOOST_CHECK(testPoolAccess.CheckMempoolTxDB());
}

BOOST_AUTO_TEST_CASE(rolling_min_tests) 
{
    using namespace std;

    CTxMemPool pool;
    BOOST_CHECK_EQUAL(CTxMemPool::MAX_ROLLING_FEE_HALFLIFE, pool.GetRollingMinFee());

    constexpr auto too_low{CTxMemPool::MIN_ROLLING_FEE_HALFLIFE - 1};
    BOOST_CHECK(!pool.SetRollingMinFee(too_low));
    BOOST_CHECK_EQUAL(CTxMemPool::MAX_ROLLING_FEE_HALFLIFE, pool.GetRollingMinFee());
    
    constexpr auto too_high{CTxMemPool::MAX_ROLLING_FEE_HALFLIFE + 1};
    BOOST_CHECK(!pool.SetRollingMinFee(too_high));
    BOOST_CHECK_EQUAL(CTxMemPool::MAX_ROLLING_FEE_HALFLIFE, pool.GetRollingMinFee());

    BOOST_CHECK(pool.SetRollingMinFee(CTxMemPool::MIN_ROLLING_FEE_HALFLIFE));
    BOOST_CHECK_EQUAL(CTxMemPool::MIN_ROLLING_FEE_HALFLIFE, pool.GetRollingMinFee());
    
    BOOST_CHECK(pool.SetRollingMinFee(CTxMemPool::MAX_ROLLING_FEE_HALFLIFE));
    BOOST_CHECK_EQUAL(CTxMemPool::MAX_ROLLING_FEE_HALFLIFE, pool.GetRollingMinFee());
}

BOOST_AUTO_TEST_SUITE_END()
