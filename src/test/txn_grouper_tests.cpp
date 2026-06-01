// Copyright (c) 2022 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "txn_grouper.h"

#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <initializer_list>
#include <ranges>

namespace
{
    // Random TxId
    TxId GetRandTxId()
    {
        return TxId {InsecureRand256()};
    }
    
    // Make random transaction
    CTransactionRef CreateSharedRandomTransaction(std::vector<COutPoint> spends)
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
        return std::ranges::all_of(groups,
            [](const auto& group) { return std::ranges::is_sorted(*group); });
    }

    // Find the index of the group that contains the given txn index,
    // or groups.size() if it is not present in any group.
    size_t FindGroupOf(const std::vector<TxnGrouper::UPtrTxnGroup>& groups, size_t txnIndex)
    {
        for(size_t g = 0; g < groups.size(); ++g)
        {
            if(std::ranges::find(*groups[g], txnIndex) != groups[g]->end())
            {
                return g;
            }
        }
        return groups.size();
    }

    // Are all the given txn indexes present together in a single group?
    bool InSameGroup(const std::vector<TxnGrouper::UPtrTxnGroup>& groups,
                     std::initializer_list<size_t> txnIndexes)
    {
        size_t target { groups.size() };
        for(size_t idx : txnIndexes)
        {
            const size_t g { FindGroupOf(groups, idx) };
            if(g == groups.size())
            {
                return false; // not found in any group
            }
            if(target == groups.size())
            {
                target = g;
            }
            else if(g != target)
            {
                return false; // found, but in a different group
            }
        }
        return true;
    }

    // Position of txnIndex within a single group's vector, or group.size() if absent.
    size_t PositionInGroup(const TxnGrouper::TxnGroup& group, size_t txnIndex)
    {
        const auto it { std::ranges::find(group, txnIndex) };
        return static_cast<size_t>(std::distance(group.begin(), it));
    }
}

struct RandomContextFixture {
    RandomContextFixture() {
        ResetGlobalRandomContext();
    }
};

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
    constexpr size_t NumTxns {10};
    std::vector<CTransactionRef> vtx{};
    vtx.reserve(NumTxns);
    for(size_t i = 0; i < NumTxns; ++i)
    {
        vtx.push_back(CreateSharedRandomTransaction({}));
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
    constexpr size_t NumBaseTxns {10};
    std::vector<CTransactionRef> vtx{};
    vtx.reserve(NumBaseTxns);
    for(size_t i = 0; i < NumBaseTxns; ++i)
    {
        vtx.push_back(CreateSharedRandomTransaction({}));
    }

    // Txn that spends a previous output
    vtx.push_back(CreateSharedRandomTransaction({vtx[0]->vin[0].prevout}));
    // Txn that spends multiple outputs from the same previous txn
    vtx.push_back(CreateSharedRandomTransaction({ {vtx[1]->vin[0].prevout.GetTxId(), 0}, {vtx[1]->vin[0].prevout.GetTxId(), 1} }));
    // Txn that spends a previous output plus a new output we don't currently know about
    vtx.push_back(CreateSharedRandomTransaction({ {vtx[2]->vin[0].prevout}, {GetRandTxId(), 0} }));

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
    constexpr size_t NumBaseTxns {10};
    std::vector<CTransactionRef> vtx{};
    vtx.reserve(NumBaseTxns);
    for(size_t i = 0; i < NumBaseTxns; ++i)
    {
        vtx.push_back(CreateSharedRandomTransaction({}));
    }

    size_t oldGroupCount; // NOLINT(cppcoreguidelines-init-variables)

    {
        // Txn that spends 2 previous txns -> All 3 txns in group A
        vtx.push_back(CreateSharedRandomTransaction({ {vtx[0]->vin[0].prevout.GetTxId(), 0}, {vtx[1]->vin[0].prevout.GetTxId(), 0} }));

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
        vtx.push_back(CreateSharedRandomTransaction({ {vtx[2]->vin[0].prevout.GetTxId(), 0}, {vtx[3]->vin[0].prevout.GetTxId(), 0} }));

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
        vtx.push_back(CreateSharedRandomTransaction({ {vtx[0]->GetId(), 0} }));

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
        vtx.push_back(CreateSharedRandomTransaction({ {vtx[1]->GetId(), 0}, {vtx[2]->vin[0].prevout.GetTxId(), 1}, {GetRandTxId(), 0} }));

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

        // NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores)
        oldGroupCount = oldGroupCount - 2 + 1;
    }
}

// Some corner cases
BOOST_AUTO_TEST_CASE(corner_cases)
{
    std::vector<CTransactionRef> vtx {};
    vtx.push_back(CreateSharedRandomTransaction({}));

    size_t oldGroupCount; // NOLINT(cppcoreguidelines-init-variables)

    {
        // Block contains duplicate transaction
        auto dupTxn { CreateSharedRandomTransaction({}) };
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
        auto txn { CreateSharedRandomTransaction({}) };
        vtx.push_back(CreateSharedRandomTransaction({ {txn->GetId(), 0} }));
        vtx.push_back(txn);

        TxnGrouper grouper {};
        const auto& groups { grouper.GetGroups(vtx) };

        // 3 groups; the previous groups, plus a new group containing the out of order txns
        BOOST_CHECK_EQUAL(groups.size(), oldGroupCount + 1);

        // Total number of txns across all constructed groups should match the number passed in
        BOOST_CHECK_EQUAL(CountTxnsInGroups(groups), vtx.size());

        // Check txn ordering
        BOOST_CHECK(CheckTxnOrdering(groups));

        // NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores)
        oldGroupCount = oldGroupCount + 1;
    }
}

// Two txns in the block spend the exact same output of another txn in the block (a
// double-spend within the block). They must all end up in a single group so that the
// conflict is seen by one validation thread rather than split across parallel threads.
BOOST_AUTO_TEST_CASE(double_spend_same_inblock_output)
{
    std::vector<CTransactionRef> vtx {};
    vtx.push_back(CreateSharedRandomTransaction({}));                       // 0: parent P
    vtx.push_back(CreateSharedRandomTransaction({ {vtx[0]->GetId(), 0} })); // 1: A spends P:0
    vtx.push_back(CreateSharedRandomTransaction({ {vtx[0]->GetId(), 0} })); // 2: B spends P:0 (same outpoint)

    TxnGrouper grouper {};
    const auto& groups { grouper.GetGroups(vtx) };

    BOOST_CHECK_EQUAL(groups.size(), 1U);
    BOOST_CHECK_EQUAL(CountTxnsInGroups(groups), vtx.size());
    BOOST_CHECK(CheckTxnOrdering(groups));
    BOOST_CHECK(InSameGroup(groups, {0, 1, 2}));
}

// Two (then three) txns in the block spend the same output that is NOT in the block. They
// must still be grouped together. This exercises the placeholder-node path in
// ScanDependencies, taken when `range.first == mNodes.end()` (txn_grouper.cpp:140); the
// unordered associative container requirements guarantee equal_range() returns
// {end(), end()} for a missing key, so that branch reliably creates the shared placeholder
// that links the spenders. This test documents/guards that grouping behaviour.
BOOST_AUTO_TEST_CASE(double_spend_external_output)
{
    const COutPoint external { GetRandTxId(), 0 };

    std::vector<CTransactionRef> vtx {};
    vtx.push_back(CreateSharedRandomTransaction({ external })); // 0: A spends external output
    vtx.push_back(CreateSharedRandomTransaction({ external })); // 1: B spends external output

    {
        TxnGrouper grouper {};
        const auto& groups { grouper.GetGroups(vtx) };

        BOOST_CHECK_EQUAL(groups.size(), 1U);
        BOOST_CHECK_EQUAL(CountTxnsInGroups(groups), vtx.size());
        BOOST_CHECK(CheckTxnOrdering(groups));
        BOOST_CHECK(InSameGroup(groups, {0, 1}));
    }

    // A third spender of the same external output
    vtx.push_back(CreateSharedRandomTransaction({ external })); // 2
    {
        TxnGrouper grouper {};
        const auto& groups { grouper.GetGroups(vtx) };

        BOOST_CHECK_EQUAL(groups.size(), 1U);
        BOOST_CHECK_EQUAL(CountTxnsInGroups(groups), vtx.size());
        BOOST_CHECK(InSameGroup(groups, {0, 1, 2}));
    }
}

// A shared output links two txns that each already belong to their own established group;
// the two groups must merge into one.
BOOST_AUTO_TEST_CASE(double_spend_merges_groups)
{
    const COutPoint shared { GetRandTxId(), 0 };

    std::vector<CTransactionRef> vtx {};
    vtx.push_back(CreateSharedRandomTransaction({}));                              // 0: A0
    vtx.push_back(CreateSharedRandomTransaction({ {vtx[0]->GetId(), 0}, shared })); // 1: A1 spends A0:0 + shared
    vtx.push_back(CreateSharedRandomTransaction({}));                              // 2: B0
    vtx.push_back(CreateSharedRandomTransaction({ {vtx[2]->GetId(), 0}, shared })); // 3: B1 spends B0:0 + shared

    TxnGrouper grouper {};
    const auto& groups { grouper.GetGroups(vtx) };

    // Without the shared output there would be two groups {A0,A1} and {B0,B1};
    // the shared output merges them into one.
    BOOST_CHECK_EQUAL(groups.size(), 1U);
    BOOST_CHECK_EQUAL(CountTxnsInGroups(groups), vtx.size());
    BOOST_CHECK(CheckTxnOrdering(groups));
    BOOST_CHECK(InSameGroup(groups, {0, 1, 2, 3}));
}

// A duplicated transaction that a later txn spends from: the spender must group with both
// copies of the duplicate.
BOOST_AUTO_TEST_CASE(duplicate_txn_with_dependents)
{
    auto dup { CreateSharedRandomTransaction({}) };

    std::vector<CTransactionRef> vtx {};
    vtx.push_back(dup);                                                  // 0: dup
    vtx.push_back(dup);                                                  // 1: dup (identical)
    vtx.push_back(CreateSharedRandomTransaction({ {dup->GetId(), 0} })); // 2: spends dup:0

    TxnGrouper grouper {};
    const auto& groups { grouper.GetGroups(vtx) };

    BOOST_CHECK_EQUAL(groups.size(), 1U);
    BOOST_CHECK_EQUAL(CountTxnsInGroups(groups), vtx.size());
    BOOST_CHECK(CheckTxnOrdering(groups));
    BOOST_CHECK(InSameGroup(groups, {0, 1, 2}));
}

// A duplicated transaction that itself spends a known in-block parent.
BOOST_AUTO_TEST_CASE(duplicate_txn_with_parent)
{
    std::vector<CTransactionRef> vtx {};
    vtx.push_back(CreateSharedRandomTransaction({}));                     // 0: parent P
    auto dup { CreateSharedRandomTransaction({ {vtx[0]->GetId(), 0} }) }; // spends P:0
    vtx.push_back(dup);                                                  // 1
    vtx.push_back(dup);                                                  // 2 (identical)

    TxnGrouper grouper {};
    const auto& groups { grouper.GetGroups(vtx) };

    BOOST_CHECK_EQUAL(groups.size(), 1U);
    BOOST_CHECK_EQUAL(CountTxnsInGroups(groups), vtx.size());
    BOOST_CHECK(CheckTxnOrdering(groups));
    BOOST_CHECK(InSameGroup(groups, {0, 1, 2}));
}

// Three identical copies of the same transaction must all share a single group.
BOOST_AUTO_TEST_CASE(triple_duplicate)
{
    auto dup { CreateSharedRandomTransaction({}) };

    std::vector<CTransactionRef> vtx { dup, dup, dup };

    TxnGrouper grouper {};
    const auto& groups { grouper.GetGroups(vtx) };

    BOOST_CHECK_EQUAL(groups.size(), 1U);
    BOOST_CHECK_EQUAL(CountTxnsInGroups(groups), vtx.size());
    BOOST_CHECK(CheckTxnOrdering(groups));
    BOOST_CHECK(InSameGroup(groups, {0, 1, 2}));
}

// A dependency chain A <- B <- C listed in reverse order in the block (C, B, A). Exercises
// the placeholder -> SetTransaction tie-in path (txn_grouper.cpp:119-126).
BOOST_AUTO_TEST_CASE(out_of_order_chain)
{
    auto a { CreateSharedRandomTransaction({}) };
    auto b { CreateSharedRandomTransaction({ {a->GetId(), 0} }) };
    auto c { CreateSharedRandomTransaction({ {b->GetId(), 0} }) };

    std::vector<CTransactionRef> vtx { c, b, a }; // reverse / out of order

    TxnGrouper grouper {};
    const auto& groups { grouper.GetGroups(vtx) };

    BOOST_CHECK_EQUAL(groups.size(), 1U);
    BOOST_CHECK_EQUAL(CountTxnsInGroups(groups), vtx.size());
    BOOST_CHECK(CheckTxnOrdering(groups));
    BOOST_CHECK(InSameGroup(groups, {0, 1, 2}));
}

// Multiple txns depend on X before the real X appears later in the block; the placeholder
// for X already has several dependents when it is finally tied to a real transaction.
BOOST_AUTO_TEST_CASE(out_of_order_with_multiple_dependents)
{
    auto x { CreateSharedRandomTransaction({}) };

    std::vector<CTransactionRef> vtx {};
    vtx.push_back(CreateSharedRandomTransaction({ {x->GetId(), 0} })); // 0: spends X:0
    vtx.push_back(CreateSharedRandomTransaction({ {x->GetId(), 1} })); // 1: spends X:1
    vtx.push_back(x);                                                  // 2: X itself, out of order

    TxnGrouper grouper {};
    const auto& groups { grouper.GetGroups(vtx) };

    BOOST_CHECK_EQUAL(groups.size(), 1U);
    BOOST_CHECK_EQUAL(CountTxnsInGroups(groups), vtx.size());
    BOOST_CHECK(CheckTxnOrdering(groups));
    BOOST_CHECK(InSameGroup(groups, {0, 1, 2}));
}

// Diamond / DAG fan-in: B and C both spend A; D spends both B and C. All four must be in one
// group, with each index present exactly once (guards against duplicate indices when the
// same node is reachable by more than one path).
BOOST_AUTO_TEST_CASE(diamond_dependency)
{
    std::vector<CTransactionRef> vtx {};
    vtx.push_back(CreateSharedRandomTransaction({}));                       // 0: A
    vtx.push_back(CreateSharedRandomTransaction({ {vtx[0]->GetId(), 0} })); // 1: B spends A:0
    vtx.push_back(CreateSharedRandomTransaction({ {vtx[0]->GetId(), 1} })); // 2: C spends A:1
    vtx.push_back(CreateSharedRandomTransaction({ {vtx[1]->GetId(), 0},
                                                  {vtx[2]->GetId(), 0} }));  // 3: D spends B:0 + C:0

    TxnGrouper grouper {};
    const auto& groups { grouper.GetGroups(vtx) };

    BOOST_CHECK_EQUAL(groups.size(), 1U);
    // Exactly four entries -> no index duplicated during the DAG walk
    BOOST_CHECK_EQUAL(groups[0]->size(), 4U);
    BOOST_CHECK_EQUAL(CountTxnsInGroups(groups), vtx.size());
    BOOST_CHECK(CheckTxnOrdering(groups));
    BOOST_CHECK(InSameGroup(groups, {0, 1, 2, 3}));
}

// A single (invalid) transaction that lists the same outpoint twice. True self-referencing
// or cyclic inputs cannot be constructed, because a txid is a hash of its own content, so
// this repeated-input case stands in as the "must terminate / must not double-count" check
// for the erase-as-visited design of BuildGroupNonRecursive.
BOOST_AUTO_TEST_CASE(repeated_input_in_single_txn)
{
    const COutPoint dup { GetRandTxId(), 0 };

    std::vector<CTransactionRef> vtx {};
    vtx.push_back(CreateSharedRandomTransaction({ dup, dup })); // same outpoint twice

    TxnGrouper grouper {};
    const auto& groups { grouper.GetGroups(vtx) };

    BOOST_CHECK_EQUAL(groups.size(), 1U);
    BOOST_CHECK_EQUAL(groups[0]->size(), 1U);
    BOOST_CHECK_EQUAL(CountTxnsInGroups(groups), vtx.size());
}

// One long dependency chain (t0 <- t1 <- ... <- tN-1). This is the only test that exercises
// the deliberately non-recursive BuildGroupNonRecursive on a deep graph; a recursive
// implementation would risk stack overflow here.
BOOST_AUTO_TEST_CASE(long_dependency_chain)
{
    constexpr size_t ChainLength {10000};
    std::vector<CTransactionRef> vtx {};
    vtx.reserve(ChainLength);
    vtx.push_back(CreateSharedRandomTransaction({}));
    for(size_t i = 1; i < ChainLength; ++i)
    {
        vtx.push_back(CreateSharedRandomTransaction({ {vtx[i - 1]->GetId(), 0} }));
    }

    TxnGrouper grouper {};
    const auto& groups { grouper.GetGroups(vtx) };

    BOOST_CHECK_EQUAL(groups.size(), 1U);
    BOOST_CHECK_EQUAL(groups[0]->size(), ChainLength);
    BOOST_CHECK_EQUAL(CountTxnsInGroups(groups), vtx.size());
    BOOST_CHECK(CheckTxnOrdering(groups));
}

// GetNumGroups with numGroups == 0 returns nothing, even for a non-empty block.
BOOST_AUTO_TEST_CASE(numgroups_zero_nonempty)
{
    std::vector<CTransactionRef> vtx {};
    constexpr size_t NumTxns{5};
    vtx.reserve(NumTxns);
    for(size_t i = 0; i < NumTxns; ++i)
    {
        vtx.push_back(CreateSharedRandomTransaction({}));
    }

    TxnGrouper grouper {};
    BOOST_CHECK(grouper.GetNumGroups(vtx, 0, 0).empty());
    BOOST_CHECK(grouper.GetNumGroups(vtx, 0, 5).empty());
}

// Requesting more groups than there are independent groups. With minSize >= 1 the surplus
// empty buckets are collapsed; with minSize == 0 the empty buckets are returned as-is.
BOOST_AUTO_TEST_CASE(numgroups_more_than_available)
{
    constexpr size_t NumTxns {5};
    std::vector<CTransactionRef> vtx {};
    vtx.reserve(NumTxns);
    for(size_t i = 0; i < NumTxns; ++i)
    {
        vtx.push_back(CreateSharedRandomTransaction({}));
    }

    TxnGrouper grouper {};

    // minSize 1: empty buckets are merged away, leaving at most one group per txn
    const auto& collapsed { grouper.GetNumGroups(vtx, 10, 1) };
    BOOST_CHECK_EQUAL(collapsed.size(), NumTxns);
    BOOST_CHECK_EQUAL(CountTxnsInGroups(collapsed), vtx.size());

    // minSize 0: empty buckets are kept, so we get exactly numGroups buckets, some empty
    const auto& withEmpties { grouper.GetNumGroups(vtx, 10, 0) };
    BOOST_CHECK_EQUAL(withEmpties.size(), 10U);
    BOOST_CHECK_EQUAL(CountTxnsInGroups(withEmpties), vtx.size());
    const size_t emptyCount {
        static_cast<size_t>(std::ranges::count_if(withEmpties,
            [](const auto& g) { return g->empty(); })) };
    BOOST_CHECK_EQUAL(emptyCount, 10U - NumTxns);
}

// A fully-connected block (everything in one dependency group). Asking for several groups
// still yields a single group containing every txn.
BOOST_AUTO_TEST_CASE(numgroups_single_big_group)
{
    constexpr size_t ChainLength {5};
    std::vector<CTransactionRef> vtx {};
    vtx.push_back(CreateSharedRandomTransaction({}));
    for(size_t i = 1; i < ChainLength; ++i)
    {
        vtx.push_back(CreateSharedRandomTransaction({ {vtx[i - 1]->GetId(), 0} }));
    }

    TxnGrouper grouper {};
    const auto& groups { grouper.GetNumGroups(vtx, 3, 1) };

    BOOST_CHECK_EQUAL(groups.size(), 1U);
    BOOST_CHECK_EQUAL(CountTxnsInGroups(groups), vtx.size());
}

// A minimum group size larger than the total number of txns collapses everything into one group.
BOOST_AUTO_TEST_CASE(numgroups_minsize_exceeds_total)
{
    constexpr size_t NumTxns {5};
    std::vector<CTransactionRef> vtx {};
    vtx.reserve(NumTxns);
    for(size_t i = 0; i < NumTxns; ++i)
    {
        vtx.push_back(CreateSharedRandomTransaction({}));
    }

    TxnGrouper grouper {};
    const auto& groups { grouper.GetNumGroups(vtx, 5, 100) };

    BOOST_CHECK_EQUAL(groups.size(), 1U);
    BOOST_CHECK_EQUAL(CountTxnsInGroups(groups), vtx.size());
}

// Independent groups of clearly unequal sizes packed into a fixed number of buckets. The
// total must be preserved and dependent txns must never be split across buckets.
BOOST_AUTO_TEST_CASE(numgroups_unequal_sizes)
{
    std::vector<CTransactionRef> vtx {};
    // Group of 3: indices 0,1,2
    vtx.push_back(CreateSharedRandomTransaction({}));                       // 0
    vtx.push_back(CreateSharedRandomTransaction({ {vtx[0]->GetId(), 0} })); // 1
    vtx.push_back(CreateSharedRandomTransaction({ {vtx[1]->GetId(), 0} })); // 2
    // Group of 1: index 3
    vtx.push_back(CreateSharedRandomTransaction({}));                       // 3
    // Group of 1: index 4
    vtx.push_back(CreateSharedRandomTransaction({}));                       // 4
    // Group of 2: indices 5,6
    vtx.push_back(CreateSharedRandomTransaction({}));                       // 5
    vtx.push_back(CreateSharedRandomTransaction({ {vtx[5]->GetId(), 0} })); // 6

    TxnGrouper grouper {};
    const auto& groups { grouper.GetNumGroups(vtx, 2, 1) };

    BOOST_CHECK_EQUAL(groups.size(), 2U);
    BOOST_CHECK_EQUAL(CountTxnsInGroups(groups), vtx.size());
    // Dependent txns are never split across buckets
    BOOST_CHECK(InSameGroup(groups, {0, 1, 2}));
    BOOST_CHECK(InSameGroup(groups, {5, 6}));
}

// GetNumGroups concatenates already-sorted sub-groups without re-sorting, so a combined
// bucket is not necessarily globally sorted. What must hold is that within a bucket the
// txns of any one dependency chain stay in ascending (block) order.
BOOST_AUTO_TEST_CASE(numgroups_ordering)
{
    std::vector<CTransactionRef> vtx {};
    // Chain a: 0 < 1 < 2
    vtx.push_back(CreateSharedRandomTransaction({}));                       // 0
    vtx.push_back(CreateSharedRandomTransaction({ {vtx[0]->GetId(), 0} })); // 1
    vtx.push_back(CreateSharedRandomTransaction({ {vtx[1]->GetId(), 0} })); // 2
    // Chain b: 3 < 4
    vtx.push_back(CreateSharedRandomTransaction({}));                       // 3
    vtx.push_back(CreateSharedRandomTransaction({ {vtx[3]->GetId(), 0} })); // 4

    TxnGrouper grouper {};
    // Pack both independent chains into a single bucket
    const auto& groups { grouper.GetNumGroups(vtx, 1, 1) };

    BOOST_REQUIRE_EQUAL(groups.size(), 1U);
    BOOST_CHECK_EQUAL(CountTxnsInGroups(groups), vtx.size());

    const auto& group { *groups[0] };
    // Within the bucket each chain keeps ascending order
    BOOST_CHECK_LT(PositionInGroup(group, 0), PositionInGroup(group, 1));
    BOOST_CHECK_LT(PositionInGroup(group, 1), PositionInGroup(group, 2));
    BOOST_CHECK_LT(PositionInGroup(group, 3), PositionInGroup(group, 4));
}

BOOST_AUTO_TEST_SUITE_END()

