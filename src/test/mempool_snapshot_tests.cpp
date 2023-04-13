// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "mining/journal_change_set.h"
#include "txmempool.h"

#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>
#include <array>
#include <set>

namespace
{
    // This test fixture constructs a mempool with a standard set and structure
    // of transactions that will be used by all the snapshot tests.
    struct MemPoolSnapshotTestingSetup : public TestingSetup
    {
        CTxMemPool testPool;

        // Structure of the entries in the test mempool:
        //
        // Tx1   Tx2   Tx3         Tx4
        //  |     |     |           |
        //  +-----+-----+     +-----+-----+
        //        |           |           |
        //       Tx5         Tx6         Tx7
        //        |           |           |
        //        |           +-----+-----+
        //        |                 |
        //       Tx8               Tx9

        CMutableTransaction Tx1;
        CMutableTransaction Tx2;
        CMutableTransaction Tx3;
        CMutableTransaction Tx4;
        CMutableTransaction Tx5;
        CMutableTransaction Tx6;
        CMutableTransaction Tx7;
        CMutableTransaction Tx8;
        CMutableTransaction Tx9;
        CMutableTransaction TxN; // Not part of the mempool.

        template<std::array<CMutableTransaction*, 1>::size_type N>
        using TxArray = std::array<CMutableTransaction*, N>;

        const TxArray<9> allTxs {&Tx1, &Tx2, &Tx3, &Tx4, &Tx5, &Tx6, &Tx7, &Tx8, &Tx9};
        const TxArray<5> topTxs {&Tx1, &Tx2, &Tx3, &Tx4, &TxN};
        const TxArray<2> txs67 {&Tx6, &Tx7};

        static constexpr auto SINGLE = CTxMemPool::TxSnapshotKind::SINGLE;
        static constexpr auto TX_WITH_ANCESTORS = CTxMemPool::TxSnapshotKind::TX_WITH_ANCESTORS;
        static constexpr auto ONLY_ANCESTORS = CTxMemPool::TxSnapshotKind::ONLY_ANCESTORS;
        static constexpr auto TX_WITH_DESCENDANTS = CTxMemPool::TxSnapshotKind::TX_WITH_DESCENDANTS;
        static constexpr auto ONLY_DESCENDANTS = CTxMemPool::TxSnapshotKind::ONLY_DESCENDANTS;

        MemPoolSnapshotTestingSetup(const std::string &chainName = CBaseChainParams::MAIN)
            : TestingSetup(chainName)
        {
            // Inputs and outputs for the top-level transactions.
            for (auto tx : topTxs) {
                tx->vin.resize(1);
                tx->vin[0].scriptSig = CScript() << OP_11;
                tx->vout.resize(1);
                tx->vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
                tx->vout[0].nValue = GetAmount();
            }
            // Add the second output to Tx4.
            Tx4.vout.resize(2);
            Tx4.vout[1].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
            Tx4.vout[1].nValue = GetAmount();

            // Three inputs, one output for Tx5.
            Tx5.vin.resize(3);
            Tx5.vin[0].scriptSig = CScript() << OP_11;
            Tx5.vin[0].prevout = COutPoint(Tx1.GetId(), 0);
            Tx5.vin[1].scriptSig = CScript() << OP_11;
            Tx5.vin[1].prevout = COutPoint(Tx2.GetId(), 0);
            Tx5.vin[2].scriptSig = CScript() << OP_11;
            Tx5.vin[2].prevout = COutPoint(Tx3.GetId(), 0);
            Tx5.vout.resize(1);
            Tx5.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
            Tx5.vout[0].nValue = GetAmount();

            // Tx6 and Tx7 both refer to Tx4 and have one output each.
            int tx4output = 0;
            for (auto tx : txs67) {
                tx->vin.resize(1);
                tx->vin[0].scriptSig = CScript() << OP_11;
                tx->vin[0].prevout = COutPoint(Tx4.GetId(), tx4output++);
                tx->vout.resize(1);
                tx->vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
                tx->vout[0].nValue = GetAmount();
            }

            // One input, one output for Tx8.
            Tx8.vin.resize(1);
            Tx8.vin[0].scriptSig = CScript() << OP_11;
            Tx8.vin[0].prevout = COutPoint(Tx5.GetId(), 0);
            Tx8.vout.resize(1);
            Tx8.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
            Tx8.vout[0].nValue = GetAmount();

            // Two inputs, one output for Tx9.
            Tx9.vin.resize(2);
            Tx9.vin[0].scriptSig = CScript() << OP_11;
            Tx9.vin[0].prevout = COutPoint(Tx6.GetId(), 0);
            Tx9.vin[1].scriptSig = CScript() << OP_11;
            Tx9.vin[1].prevout = COutPoint(Tx7.GetId(), 0);
            Tx9.vout.resize(1);
            Tx9.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
            Tx9.vout[0].nValue = GetAmount();

            // Now insert everyting into the mempool.
            TestMemPoolEntryHelper entry(DEFAULT_TEST_TX_FEE);
            for (auto tx : allTxs) {
                testPool.AddUnchecked(tx->GetId(), entry.FromTx(*tx), TxStorage::memory,
                                      mining::CJournalChangeSetPtr{nullptr});
            }
        }

    private:
        const Amount baseAmount {11000LL};
        Amount nextAmount {0LL};
        Amount GetAmount()
        {
            nextAmount += baseAmount;
            return nextAmount;
        }
    };
}

BOOST_FIXTURE_TEST_SUITE(mempool_snapshot_tests, MemPoolSnapshotTestingSetup)

BOOST_AUTO_TEST_CASE(ValidateTestPool)
{
    BOOST_CHECK_EQUAL(testPool.Size(), allTxs.size());
}

BOOST_AUTO_TEST_CASE(PoolSnapshotTest)
{
    const auto slice = testPool.GetSnapshot();
    BOOST_CHECK(!!slice);
    BOOST_CHECK(!slice.empty());
    BOOST_CHECK_EQUAL(slice.size(), testPool.Size());

    // Slice iterator sanity check
    BOOST_CHECK(slice.begin() == slice.cbegin());
    BOOST_CHECK(slice.end() == slice.cend());
    BOOST_CHECK(slice.begin() != slice.cend());
    BOOST_CHECK(slice.end() != slice.cbegin());
    BOOST_CHECK(slice.begin() != slice.end());
    BOOST_CHECK(slice.cend() != slice.cbegin());

    // Check that all transaction IDs exist in the pool's lookup table.
    std::set<uint256> hashes;
    BOOST_CHECK_EQUAL(hashes.size(), 0U);
    for (const auto tx : allTxs) {
        hashes.emplace(tx->GetId());
    }
    BOOST_CHECK_EQUAL(hashes.size(), slice.size());
    for (const auto& hash : hashes) {
        BOOST_CHECK(slice.TxIdExists(hash));
    }

    // Check that all hashes are unique.
    for (const auto& entry : slice) {
        const auto key = entry.GetTxId();
        BOOST_CHECK_EQUAL(1U, hashes.count(key));
        BOOST_CHECK_NO_THROW(hashes.erase(key));
    }
    BOOST_CHECK_EQUAL(hashes.size(), 0U);
}

BOOST_AUTO_TEST_CASE(InvalidTxIdTest)
{
    {
        const auto slice = testPool.GetTxSnapshot(TxN.GetId(), SINGLE);
        BOOST_CHECK(!slice);
        BOOST_CHECK(slice.empty());
        BOOST_CHECK_EQUAL(slice.size(), 0U);
        BOOST_CHECK(slice.cbegin() == slice.cend());
    }{
        const auto slice = testPool.GetTxSnapshot(TxN.GetId(), TX_WITH_ANCESTORS);
        BOOST_CHECK(!slice);
        BOOST_CHECK(slice.empty());
        BOOST_CHECK_EQUAL(slice.size(), 0U);
        BOOST_CHECK(slice.cbegin() == slice.cend());
    }{
        const auto slice = testPool.GetTxSnapshot(TxN.GetId(), ONLY_ANCESTORS);
        BOOST_CHECK(!slice);
        BOOST_CHECK(slice.empty());
        BOOST_CHECK_EQUAL(slice.size(), 0U);
        BOOST_CHECK(slice.cbegin() == slice.cend());
    }{
        const auto slice = testPool.GetTxSnapshot(TxN.GetId(), TX_WITH_DESCENDANTS);
        BOOST_CHECK(!slice);
        BOOST_CHECK(slice.empty());
        BOOST_CHECK_EQUAL(slice.size(), 0U);
        BOOST_CHECK(slice.cbegin() == slice.cend());
    }{
        const auto slice = testPool.GetTxSnapshot(TxN.GetId(), ONLY_DESCENDANTS);
        BOOST_CHECK(!slice);
        BOOST_CHECK(slice.empty());
        BOOST_CHECK_EQUAL(slice.size(), 0U);
        BOOST_CHECK(slice.cbegin() == slice.cend());
    }
}

BOOST_AUTO_TEST_CASE(SingleTest)
{
    {
        const auto slice = testPool.GetTxSnapshot(Tx1.GetId(), SINGLE);
        BOOST_CHECK(slice.IsValid());
        BOOST_CHECK(!slice.empty());
        BOOST_CHECK_EQUAL(slice.size(), 1U);
        BOOST_CHECK( slice.TxIdExists(Tx1.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx2.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx3.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx4.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx5.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx6.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx7.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx8.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx9.GetId()));
        BOOST_CHECK(!slice.TxIdExists(TxN.GetId()));

        BOOST_CHECK(slice.find(Tx1.GetId()) != slice.cend());
        BOOST_CHECK(slice.find(Tx2.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx3.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx4.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx5.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx6.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx7.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx8.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx9.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(TxN.GetId()) == slice.cend());
    }{
        const auto slice = testPool.GetTxSnapshot(Tx5.GetId(), SINGLE);
        BOOST_CHECK(slice.IsValid());
        BOOST_CHECK(!slice.empty());
        BOOST_CHECK_EQUAL(slice.size(), 1U);
        BOOST_CHECK( slice.TxIdExists(Tx1.GetId()));
        BOOST_CHECK( slice.TxIdExists(Tx2.GetId()));
        BOOST_CHECK( slice.TxIdExists(Tx3.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx4.GetId()));
        BOOST_CHECK( slice.TxIdExists(Tx5.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx6.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx7.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx8.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx9.GetId()));
        BOOST_CHECK(!slice.TxIdExists(TxN.GetId()));

        BOOST_CHECK(slice.find(Tx1.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx2.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx3.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx4.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx5.GetId()) != slice.cend());
        BOOST_CHECK(slice.find(Tx6.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx7.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx8.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx9.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(TxN.GetId()) == slice.cend());
    }{
        const auto slice = testPool.GetTxSnapshot(Tx8.GetId(), SINGLE);
        BOOST_CHECK(slice.IsValid());
        BOOST_CHECK(!slice.empty());
        BOOST_CHECK_EQUAL(slice.size(), 1U);
        BOOST_CHECK(!slice.TxIdExists(Tx1.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx2.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx3.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx4.GetId()));
        BOOST_CHECK( slice.TxIdExists(Tx5.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx6.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx7.GetId()));
        BOOST_CHECK( slice.TxIdExists(Tx8.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx9.GetId()));
        BOOST_CHECK(!slice.TxIdExists(TxN.GetId()));

        BOOST_CHECK(slice.find(Tx1.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx2.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx3.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx4.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx5.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx6.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx7.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx8.GetId()) != slice.cend());
        BOOST_CHECK(slice.find(Tx9.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(TxN.GetId()) == slice.cend());
    }
}

BOOST_AUTO_TEST_CASE(TxWithAncestorsTest)
{
    {
        const auto slice = testPool.GetTxSnapshot(Tx2.GetId(), TX_WITH_ANCESTORS);
        BOOST_CHECK(slice.IsValid());
        BOOST_CHECK(!slice.empty());
        BOOST_CHECK_EQUAL(slice.size(), 1U);
        BOOST_CHECK(!slice.TxIdExists(Tx1.GetId()));
        BOOST_CHECK( slice.TxIdExists(Tx2.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx3.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx4.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx5.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx6.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx7.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx8.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx9.GetId()));
        BOOST_CHECK(!slice.TxIdExists(TxN.GetId()));

        BOOST_CHECK(slice.find(Tx1.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx2.GetId()) != slice.cend());
        BOOST_CHECK(slice.find(Tx3.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx4.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx5.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx6.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx7.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx8.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx9.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(TxN.GetId()) == slice.cend());
    }{
        const auto slice = testPool.GetTxSnapshot(Tx6.GetId(), TX_WITH_ANCESTORS);
        BOOST_CHECK(slice.IsValid());
        BOOST_CHECK(!slice.empty());
        BOOST_CHECK_EQUAL(slice.size(), 2U);
        BOOST_CHECK(!slice.TxIdExists(Tx1.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx2.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx3.GetId()));
        BOOST_CHECK( slice.TxIdExists(Tx4.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx5.GetId()));
        BOOST_CHECK( slice.TxIdExists(Tx6.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx7.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx8.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx9.GetId()));
        BOOST_CHECK(!slice.TxIdExists(TxN.GetId()));

        BOOST_CHECK(slice.find(Tx1.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx2.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx3.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx4.GetId()) != slice.cend());
        BOOST_CHECK(slice.find(Tx5.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx6.GetId()) != slice.cend());
        BOOST_CHECK(slice.find(Tx7.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx8.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx9.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(TxN.GetId()) == slice.cend());
    }{
        const auto slice = testPool.GetTxSnapshot(Tx9.GetId(), TX_WITH_ANCESTORS);
        BOOST_CHECK(slice.IsValid());
        BOOST_CHECK(!slice.empty());
        BOOST_CHECK_EQUAL(slice.size(), 4U);
        BOOST_CHECK(!slice.TxIdExists(Tx1.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx2.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx3.GetId()));
        BOOST_CHECK( slice.TxIdExists(Tx4.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx5.GetId()));
        BOOST_CHECK( slice.TxIdExists(Tx6.GetId()));
        BOOST_CHECK( slice.TxIdExists(Tx7.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx8.GetId()));
        BOOST_CHECK( slice.TxIdExists(Tx9.GetId()));
        BOOST_CHECK(!slice.TxIdExists(TxN.GetId()));

        BOOST_CHECK(slice.find(Tx1.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx2.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx3.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx4.GetId()) != slice.cend());
        BOOST_CHECK(slice.find(Tx5.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx6.GetId()) != slice.cend());
        BOOST_CHECK(slice.find(Tx7.GetId()) != slice.cend());
        BOOST_CHECK(slice.find(Tx8.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx9.GetId()) != slice.cend());
        BOOST_CHECK(slice.find(TxN.GetId()) == slice.cend());
    }
}

BOOST_AUTO_TEST_CASE(OnlyAncestorsTest)
{
    {
        const auto slice = testPool.GetTxSnapshot(Tx2.GetId(), ONLY_ANCESTORS);
        BOOST_CHECK(slice.IsValid());
        BOOST_CHECK(slice.empty());
        BOOST_CHECK_EQUAL(slice.size(), 0U);
        BOOST_CHECK(!slice.TxIdExists(Tx1.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx2.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx3.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx4.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx5.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx6.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx7.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx8.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx9.GetId()));
        BOOST_CHECK(!slice.TxIdExists(TxN.GetId()));

        BOOST_CHECK(slice.find(Tx1.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx2.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx3.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx4.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx5.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx6.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx7.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx8.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx9.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(TxN.GetId()) == slice.cend());
    }{
        const auto slice = testPool.GetTxSnapshot(Tx6.GetId(), ONLY_ANCESTORS);
        BOOST_CHECK(slice.IsValid());
        BOOST_CHECK(!slice.empty());
        BOOST_CHECK_EQUAL(slice.size(), 1U);
        BOOST_CHECK(!slice.TxIdExists(Tx1.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx2.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx3.GetId()));
        BOOST_CHECK( slice.TxIdExists(Tx4.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx5.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx6.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx7.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx8.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx9.GetId()));
        BOOST_CHECK(!slice.TxIdExists(TxN.GetId()));

        BOOST_CHECK(slice.find(Tx1.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx2.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx3.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx4.GetId()) != slice.cend());
        BOOST_CHECK(slice.find(Tx5.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx6.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx7.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx8.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx9.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(TxN.GetId()) == slice.cend());
    }{
        const auto slice = testPool.GetTxSnapshot(Tx9.GetId(), ONLY_ANCESTORS);
        BOOST_CHECK(slice.IsValid());
        BOOST_CHECK(!slice.empty());
        BOOST_CHECK_EQUAL(slice.size(), 3U);
        BOOST_CHECK(!slice.TxIdExists(Tx1.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx2.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx3.GetId()));
        BOOST_CHECK( slice.TxIdExists(Tx4.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx5.GetId()));
        BOOST_CHECK( slice.TxIdExists(Tx6.GetId()));
        BOOST_CHECK( slice.TxIdExists(Tx7.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx8.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx9.GetId()));
        BOOST_CHECK(!slice.TxIdExists(TxN.GetId()));

        BOOST_CHECK(slice.find(Tx1.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx2.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx3.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx4.GetId()) != slice.cend());
        BOOST_CHECK(slice.find(Tx5.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx6.GetId()) != slice.cend());
        BOOST_CHECK(slice.find(Tx7.GetId()) != slice.cend());
        BOOST_CHECK(slice.find(Tx8.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx9.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(TxN.GetId()) == slice.cend());
    }
}

BOOST_AUTO_TEST_CASE(TxWithDescendantsTest)
{
    {
        const auto slice = testPool.GetTxSnapshot(Tx3.GetId(), TX_WITH_DESCENDANTS);
        BOOST_CHECK(slice.IsValid());
        BOOST_CHECK(!slice.empty());
        BOOST_CHECK_EQUAL(slice.size(), 3U);
        BOOST_CHECK( slice.TxIdExists(Tx1.GetId()));
        BOOST_CHECK( slice.TxIdExists(Tx2.GetId()));
        BOOST_CHECK( slice.TxIdExists(Tx3.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx4.GetId()));
        BOOST_CHECK( slice.TxIdExists(Tx5.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx6.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx7.GetId()));
        BOOST_CHECK( slice.TxIdExists(Tx8.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx9.GetId()));
        BOOST_CHECK(!slice.TxIdExists(TxN.GetId()));

        BOOST_CHECK(slice.find(Tx1.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx2.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx3.GetId()) != slice.cend());
        BOOST_CHECK(slice.find(Tx4.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx5.GetId()) != slice.cend());
        BOOST_CHECK(slice.find(Tx6.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx7.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx8.GetId()) != slice.cend());
        BOOST_CHECK(slice.find(Tx9.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(TxN.GetId()) == slice.cend());
    }{
        const auto slice = testPool.GetTxSnapshot(Tx7.GetId(), TX_WITH_DESCENDANTS);
        BOOST_CHECK(slice.IsValid());
        BOOST_CHECK(!slice.empty());
        BOOST_CHECK_EQUAL(slice.size(), 2U);
        BOOST_CHECK(!slice.TxIdExists(Tx1.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx2.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx3.GetId()));
        BOOST_CHECK( slice.TxIdExists(Tx4.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx5.GetId()));
        BOOST_CHECK( slice.TxIdExists(Tx6.GetId()));
        BOOST_CHECK( slice.TxIdExists(Tx7.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx8.GetId()));
        BOOST_CHECK( slice.TxIdExists(Tx9.GetId()));
        BOOST_CHECK(!slice.TxIdExists(TxN.GetId()));

        BOOST_CHECK(slice.find(Tx1.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx2.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx3.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx4.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx5.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx6.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx7.GetId()) != slice.cend());
        BOOST_CHECK(slice.find(Tx8.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx9.GetId()) != slice.cend());
        BOOST_CHECK(slice.find(TxN.GetId()) == slice.cend());
    }{
        const auto slice = testPool.GetTxSnapshot(Tx9.GetId(), TX_WITH_DESCENDANTS);
        BOOST_CHECK(slice.IsValid());
        BOOST_CHECK(!slice.empty());
        BOOST_CHECK_EQUAL(slice.size(), 1U);
        BOOST_CHECK(!slice.TxIdExists(Tx1.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx2.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx3.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx4.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx5.GetId()));
        BOOST_CHECK( slice.TxIdExists(Tx6.GetId()));
        BOOST_CHECK( slice.TxIdExists(Tx7.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx8.GetId()));
        BOOST_CHECK( slice.TxIdExists(Tx9.GetId()));
        BOOST_CHECK(!slice.TxIdExists(TxN.GetId()));

        BOOST_CHECK(slice.find(Tx1.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx2.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx3.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx4.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx5.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx6.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx7.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx8.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx9.GetId()) != slice.cend());
        BOOST_CHECK(slice.find(TxN.GetId()) == slice.cend());
    }
}

BOOST_AUTO_TEST_CASE(OnlyDescendantsTest)
{
    {
        const auto slice = testPool.GetTxSnapshot(Tx3.GetId(), ONLY_DESCENDANTS);
        BOOST_CHECK(slice.IsValid());
        BOOST_CHECK(!slice.empty());
        BOOST_CHECK_EQUAL(slice.size(), 2U);
        BOOST_CHECK( slice.TxIdExists(Tx1.GetId()));
        BOOST_CHECK( slice.TxIdExists(Tx2.GetId()));
        BOOST_CHECK( slice.TxIdExists(Tx3.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx4.GetId()));
        BOOST_CHECK( slice.TxIdExists(Tx5.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx6.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx7.GetId()));
        BOOST_CHECK( slice.TxIdExists(Tx8.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx9.GetId()));
        BOOST_CHECK(!slice.TxIdExists(TxN.GetId()));

        BOOST_CHECK(slice.find(Tx1.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx2.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx3.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx4.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx5.GetId()) != slice.cend());
        BOOST_CHECK(slice.find(Tx6.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx7.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx8.GetId()) != slice.cend());
        BOOST_CHECK(slice.find(Tx9.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(TxN.GetId()) == slice.cend());
    }{
        const auto slice = testPool.GetTxSnapshot(Tx7.GetId(), ONLY_DESCENDANTS);
        BOOST_CHECK(slice.IsValid());
        BOOST_CHECK(!slice.empty());
        BOOST_CHECK_EQUAL(slice.size(), 1U);
        BOOST_CHECK(!slice.TxIdExists(Tx1.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx2.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx3.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx4.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx5.GetId()));
        BOOST_CHECK( slice.TxIdExists(Tx6.GetId()));
        BOOST_CHECK( slice.TxIdExists(Tx7.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx8.GetId()));
        BOOST_CHECK( slice.TxIdExists(Tx9.GetId()));
        BOOST_CHECK(!slice.TxIdExists(TxN.GetId()));

        BOOST_CHECK(slice.find(Tx1.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx2.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx3.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx4.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx5.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx6.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx7.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx8.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx9.GetId()) != slice.cend());
        BOOST_CHECK(slice.find(TxN.GetId()) == slice.cend());
    }{
        const auto slice = testPool.GetTxSnapshot(Tx9.GetId(), ONLY_DESCENDANTS);
        BOOST_CHECK(slice.IsValid());
        BOOST_CHECK(slice.empty());
        BOOST_CHECK_EQUAL(slice.size(), 0U);
        BOOST_CHECK(!slice.TxIdExists(Tx1.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx2.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx3.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx4.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx5.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx6.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx7.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx8.GetId()));
        BOOST_CHECK(!slice.TxIdExists(Tx9.GetId()));
        BOOST_CHECK(!slice.TxIdExists(TxN.GetId()));

        BOOST_CHECK(slice.find(Tx1.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx2.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx3.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx4.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx5.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx6.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx7.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx8.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(Tx9.GetId()) == slice.cend());
        BOOST_CHECK(slice.find(TxN.GetId()) == slice.cend());
    }
}

BOOST_AUTO_TEST_SUITE_END()
