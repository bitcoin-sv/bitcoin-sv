// Copyright (c) 2022 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "txn_grouper.h"

#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>

namespace
{
    // Random TxId
    TxId GetRandTxId()
    {
        return TxId {InsecureRand256()};
    }
    
    // Make random transaction
    CTransactionRef CreateRandomTransaction(std::vector<COutPoint> spends)
    {
        static uint32_t sequence {0};
        CMutableTransaction txn {};
        txn.vout.resize(1);

        // Spend at least 1 input
        if(spends.empty())
        {
            spends.push_back( { GetRandTxId(), 0 } );
        }

        txn.vin.resize(spends.size());
        for(unsigned i = 0; i < spends.size(); ++i)
        {
            txn.vin[i].nSequence = sequence++;
            txn.vin[i].prevout = spends[i];
        }

        return MakeTransactionRef(txn);
    }

    size_t CountTxnsInGroups(const std::vector<TxnGrouper::UPtrTxnGroup>& groups)
    {
        size_t resNumTxns {0};
        for(const auto& group : groups)
        {
            resNumTxns += group->size();
        }

        return resNumTxns;
    }

    bool CheckTxnOrdering(const std::vector<TxnGrouper::UPtrTxnGroup>& groups)
    {
        for(const auto& group : groups)
        {
            ssize_t lastIndex {-1};
            for(const auto& txn : *group)
            {
                if(static_cast<ssize_t>(txn.mIndex) <= lastIndex)
                {
                    return false;
                }
                lastIndex = txn.mIndex;
            }
        }

        return true;
    }

    struct RandomContextFixture {
        RandomContextFixture() {
            ResetGlobalRandomContext();
        }
    };
}

BOOST_FIXTURE_TEST_SUITE(txn_grouper, RandomContextFixture)

// Base case - no transactions at all
BOOST_AUTO_TEST_CASE(empty)
{
    std::vector<CTransactionRef> vtx {};

    {
        TxnGrouper grouper {};
        BOOST_CHECK(grouper.GetGroups(vtx).empty());

        BOOST_CHECK(grouper.GetNumGroups(vtx, 0, 0).empty());
        BOOST_CHECK(grouper.GetNumGroups(vtx, 1, 0).empty());
        BOOST_CHECK(grouper.GetNumGroups(vtx, 0, 1).empty());
        BOOST_CHECK(grouper.GetNumGroups(vtx, 1, 1).empty());
    }
}

// Simple case where no transactions share any dependencies
BOOST_AUTO_TEST_CASE(independent_groups)
{
    std::vector<CTransactionRef> vtx {};
    constexpr size_t NumTxns {10};
    for(size_t i = 0; i < NumTxns; ++i)
    {
        vtx.push_back(CreateRandomTransaction({}));
    }

    {
        TxnGrouper grouper {};
        const auto& groups { grouper.GetGroups(vtx) };

        // Number of unique groups should be the same as number of transactions
        BOOST_CHECK_EQUAL(groups.size(), NumTxns);

        // Total number of txns across all constructed groups should match the number passed in
        BOOST_CHECK_EQUAL(CountTxnsInGroups(groups), vtx.size());

        // Check txn ordering
        BOOST_CHECK(CheckTxnOrdering(groups));

        // Check numbered groups handling
        const auto& numGroups_1_1 { grouper.GetNumGroups(vtx, 1, 1) };
        BOOST_CHECK_EQUAL(numGroups_1_1.size(), 1U);
        BOOST_CHECK_EQUAL(CountTxnsInGroups(numGroups_1_1), vtx.size());

        const auto& numGroups_2_1 { grouper.GetNumGroups(vtx, 2, 1) };
        BOOST_CHECK_EQUAL(numGroups_2_1.size(), 2U);
        BOOST_CHECK_EQUAL(CountTxnsInGroups(numGroups_2_1), vtx.size());

        const auto& numGroups_num_1 { grouper.GetNumGroups(vtx, NumTxns, 1) };
        BOOST_CHECK_EQUAL(numGroups_num_1.size(), NumTxns);
        BOOST_CHECK_EQUAL(CountTxnsInGroups(numGroups_num_1), vtx.size());

        const auto& numGroups_num_2 { grouper.GetNumGroups(vtx, NumTxns, 2) };
        BOOST_CHECK_EQUAL(numGroups_num_2.size(), NumTxns / 2);
        BOOST_CHECK_EQUAL(CountTxnsInGroups(numGroups_num_2), vtx.size());

        const auto& numGroups_num_num { grouper.GetNumGroups(vtx, NumTxns, NumTxns) };
        BOOST_CHECK_EQUAL(numGroups_num_num.size(), 1U);
        BOOST_CHECK_EQUAL(CountTxnsInGroups(numGroups_num_num), vtx.size());
    }
}

// Some transactions share a single dependency
BOOST_AUTO_TEST_CASE(single_dependency)
{
    std::vector<CTransactionRef> vtx {};
    constexpr size_t NumBaseTxns {10};
    for(size_t i = 0; i < NumBaseTxns; ++i)
    {
        vtx.push_back(CreateRandomTransaction({}));
    }

    // Txn that spends a previous output
    vtx.push_back(CreateRandomTransaction({vtx[0]->vin[0].prevout}));
    // Txn that spends multiple outputs from the same previous txn
    vtx.push_back(CreateRandomTransaction({ {vtx[1]->vin[0].prevout.GetTxId(), 0}, {vtx[1]->vin[0].prevout.GetTxId(), 1} }));
    // Txn that spends a previous output plus a new output we don't currently know about
    vtx.push_back(CreateRandomTransaction({ {vtx[2]->vin[0].prevout}, {GetRandTxId(), 0} }));

    {
        TxnGrouper grouper {};
        const auto& groups { grouper.GetGroups(vtx) };

        // Number of unique groups should be the same as number of base transactions
        BOOST_CHECK_EQUAL(groups.size(), NumBaseTxns);

        // Total number of txns across all constructed groups should match the number passed in
        BOOST_CHECK_EQUAL(CountTxnsInGroups(groups), vtx.size());

        // Check txn ordering
        BOOST_CHECK(CheckTxnOrdering(groups));
    }
}

// Some transactions have multiple dependencies requiring groups to be combined
BOOST_AUTO_TEST_CASE(multi_dependency)
{
    std::vector<CTransactionRef> vtx {};
    constexpr size_t NumBaseTxns {10};
    for(size_t i = 0; i < NumBaseTxns; ++i)
    {
        vtx.push_back(CreateRandomTransaction({}));
    }

    size_t oldGroupCount;

    {
        // Txn that spends 2 previous txns -> All 3 txns in group A
        vtx.push_back(CreateRandomTransaction({ {vtx[0]->vin[0].prevout.GetTxId(), 0}, {vtx[1]->vin[0].prevout.GetTxId(), 0} }));

        {
            TxnGrouper grouper {};
            const auto& groups { grouper.GetGroups(vtx) };

            // 2 txns removed from their own group, one new larger group created
            BOOST_CHECK_EQUAL(groups.size(), NumBaseTxns - 2 + 1);

            // Total number of txns across all constructed groups should match the number passed in
            BOOST_CHECK_EQUAL(CountTxnsInGroups(groups), vtx.size());

            // Check txn ordering
            BOOST_CHECK(CheckTxnOrdering(groups));
        }

        oldGroupCount = NumBaseTxns - 2 + 1;
    }

    {
        // Txn that spends 2 different previous txns -> All 3 txns in group B
        vtx.push_back(CreateRandomTransaction({ {vtx[2]->vin[0].prevout.GetTxId(), 0}, {vtx[3]->vin[0].prevout.GetTxId(), 0} }));

        {
            TxnGrouper grouper {};
            const auto& groups { grouper.GetGroups(vtx) };

            // 2 txns removed from their own group, one new larger group created
            BOOST_CHECK_EQUAL(groups.size(), oldGroupCount - 2 + 1);

            // Total number of txns across all constructed groups should match the number passed in
            BOOST_CHECK_EQUAL(CountTxnsInGroups(groups), vtx.size());

            // Check txn ordering
            BOOST_CHECK(CheckTxnOrdering(groups));
        }

        oldGroupCount = oldGroupCount - 2 + 1;
    }

    {
        // Another txn that spends one of the previous txns now in group A -> Txn goes in group A
        vtx.push_back(CreateRandomTransaction({ {vtx[0]->GetId(), 0} }));

        {
            TxnGrouper grouper {};
            const auto& groups { grouper.GetGroups(vtx) };

            // No change to number of groups
            BOOST_CHECK_EQUAL(groups.size(), oldGroupCount);

            // Total number of txns across all constructed groups should match the number passed in
            BOOST_CHECK_EQUAL(CountTxnsInGroups(groups), vtx.size());

            // Check txn ordering
            BOOST_CHECK(CheckTxnOrdering(groups));
        }
    }

    {
        // Txn that spends a txn in group A, and a dependencey of a txn in group B, and a new unknown input -> All txns in a new group C
        vtx.push_back(CreateRandomTransaction({ {vtx[1]->GetId(), 0}, {vtx[2]->vin[0].prevout.GetTxId(), 1}, {GetRandTxId(), 0} }));

        {
            TxnGrouper grouper {};
            const auto& groups { grouper.GetGroups(vtx) };

            // Groups A & B removed, one new larger group created
            BOOST_CHECK_EQUAL(groups.size(), oldGroupCount - 2 + 1);

            // Total number of txns across all constructed groups should match the number passed in
            BOOST_CHECK_EQUAL(CountTxnsInGroups(groups), vtx.size());

            // Check txn ordering
            BOOST_CHECK(CheckTxnOrdering(groups));
        }

        oldGroupCount = oldGroupCount - 2 + 1;
    }
}

// Some corner cases
BOOST_AUTO_TEST_CASE(corner_cases)
{
    std::vector<CTransactionRef> vtx {};
    vtx.push_back(CreateRandomTransaction({}));

    size_t oldGroupCount;

    {
        // Block contains duplicate transaction
        auto dupTxn { CreateRandomTransaction({}) };
        vtx.push_back(dupTxn);
        vtx.push_back(dupTxn);

        TxnGrouper grouper {};
        const auto& groups { grouper.GetGroups(vtx) };

        // 2 groups; a single txn and the 2 duplicates together
        BOOST_CHECK_EQUAL(groups.size(), 2U);

        // Total number of txns across all constructed groups should match the number passed in
        BOOST_CHECK_EQUAL(CountTxnsInGroups(groups), vtx.size());

        // Check txn ordering
        BOOST_CHECK(CheckTxnOrdering(groups));

        oldGroupCount = 2;
    }

    {
        // Block contains out of order transactions
        auto txn { CreateRandomTransaction({}) };
        vtx.push_back(CreateRandomTransaction({ {txn->GetId(), 0} }));
        vtx.push_back(txn);

        TxnGrouper grouper {};
        const auto& groups { grouper.GetGroups(vtx) };

        // 3 groups; the previous groups, plus a new group containing the out of order txns
        BOOST_CHECK_EQUAL(groups.size(), oldGroupCount + 1);

        // Total number of txns across all constructed groups should match the number passed in
        BOOST_CHECK_EQUAL(CountTxnsInGroups(groups), vtx.size());

        // Check txn ordering
        BOOST_CHECK(CheckTxnOrdering(groups));

        oldGroupCount = oldGroupCount + 1;
    }
}

BOOST_AUTO_TEST_SUITE_END()

