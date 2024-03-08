// Copyright (c) 2022 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "mining/group_builder.h"
#include "config.h"

#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>

using namespace mining;

namespace
{
    // Only used as unique identifier
    class group_builder_tests_uid;
}

// For private member access to CJournalEntry
template<>
struct CJournalEntry::UnitTestAccess<group_builder_tests_uid>
{
    template<typename... Args>
    static CJournalEntry Make(Args&&... args)
    {
        return { std::forward<Args>(args)... };
    }
};
using JournalEntryAccess = CJournalEntry::UnitTestAccess<group_builder_tests_uid>;

// For private member access to  TxnGroupBuilder
template<>
struct TxnGroupBuilder::UnitTestAccess<group_builder_tests_uid>
{
    static auto& GetTxns(TxnGroupBuilder& builder)
    {
        return builder.mTxnMap;
    }
    static auto& GetGroups(TxnGroupBuilder& builder)
    {
        return builder.mGroupMap;
    }
    static auto NewGroupID(TxnGroupBuilder& builder)
    {
        return builder.NewGroupID();
    }
    static auto& GetNextGroupID(TxnGroupBuilder& builder)
    {
        return builder.mNextGroupID;
    }
};
using BuilderAccess = TxnGroupBuilder::UnitTestAccess<group_builder_tests_uid>;

namespace
{
    // Generate a new random transaction
    CJournalEntry NewTxn()
    {
        static uint32_t lockTime {0};
        CMutableTransaction txn {};
        txn.nLockTime = lockTime++;
        const auto tx { MakeTransactionRef(std::move(txn)) };
        return JournalEntryAccess::Make(
            std::make_shared<CTransactionWrapper>(tx, nullptr),
            tx->GetTotalSize(), Amount{0}, GetTime(), std::nullopt, false);
    }

    // Generate a new random transaction that depends on another
    CJournalEntry NewTxn(std::initializer_list<CTransactionWrapperRef> other)
    {
        static uint32_t lockTime {0}; // separate counter is OK as we have an input
        CMutableTransaction txn {};
        for(const auto& prev : other)
        {
            txn.vin.emplace_back(CTxIn { COutPoint { prev->GetId(), 0 }, CScript() });
        }
        txn.nLockTime = lockTime++;
        const auto tx { MakeTransactionRef(std::move(txn)) };
        return JournalEntryAccess::Make(
             std::make_shared<CTransactionWrapper>(tx, nullptr),
             tx->GetTotalSize(), Amount{0}, GetTime(), std::nullopt, false);
    }

    // Cross check txn map and group map
    void CrossCheckTxnsAndGroups(TxnGroupBuilder& builder)
    {
        // Check txns are indeed in the group we think they're in
        for(const auto& txn : BuilderAccess::GetTxns(builder))
        {
            bool found {false};
            const auto groupID { txn.second };
            for(const auto& groupMember : builder.GetGroup(groupID))
            {
                const TxId& id { groupMember.getTxn()->GetId() };
                if(id == txn.first)
                {
                    found = true;
                    break;
                }
            }
            if(!found)
            {
                throw std::runtime_error("CrossCheckTxnsAndGroups failed for txn " + txn.first.ToString());
            }
        }
    }

    // Verify the given txn is in the expected group
    bool CheckTxnInGroup(TxnGroupBuilder& builder, const CJournalEntry& journalEntry, TxnGroupID groupID)
    {
        // Firstly check builder txn and group consistency
        CrossCheckTxnsAndGroups(builder);

        // Get txn ID
        CTransactionRef txn { journalEntry.getTxn()->GetTx() };
        BOOST_REQUIRE(txn);
        TxId txid { txn->GetId() };

        // Ensure txid is in builders map
        const auto& txnMap { BuilderAccess::GetTxns(builder) };
        BOOST_CHECK_EQUAL(txnMap.count(txid), 1U);
        // Ensure group is in builders map
        const auto& groupMap { BuilderAccess::GetGroups(builder) };
        BOOST_CHECK_EQUAL(groupMap.count(groupID), 1U);

        // Check txn is in (just this) group
        unsigned count {0};
        const TxnGroup& group { builder.GetGroup(groupID) };
        for(const auto& groupEntry : group)
        {
            CTransactionRef groupTxn { groupEntry.getTxn()->GetTx() };
            BOOST_REQUIRE(groupTxn);
            if(groupTxn->GetId() == txid)
            {
                ++count;
            }
        }

        return (count == 1);
    }
}

BOOST_AUTO_TEST_SUITE(mining_group_builder)

BOOST_AUTO_TEST_CASE(TestNewGroupID)
{
    // Group IDs start at 0
    TxnGroupBuilder builder {};
    BOOST_CHECK_EQUAL(BuilderAccess::GetNextGroupID(builder), 0U);
    BOOST_CHECK_EQUAL(BuilderAccess::NewGroupID(builder), 0U);
    BOOST_CHECK_EQUAL(BuilderAccess::GetNextGroupID(builder), 1U);
    BOOST_CHECK_EQUAL(BuilderAccess::NewGroupID(builder), 1U);

    // Check rolling over limit of uint64_t works (unlikely to ever happen)
    BuilderAccess::GetGroups(builder).insert({0, TxnGroup{0, NewTxn()}});
    BuilderAccess::GetNextGroupID(builder) = std::numeric_limits<TxnGroupID>::max();
    BOOST_CHECK_EQUAL(BuilderAccess::NewGroupID(builder), std::numeric_limits<TxnGroupID>::max());
    BOOST_CHECK_EQUAL(BuilderAccess::GetNextGroupID(builder), 1U);
}

BOOST_AUTO_TEST_CASE(TestStandaloneTxn)
{
    // Builder starts out empty
    TxnGroupBuilder builder {};
    BOOST_CHECK(BuilderAccess::GetTxns(builder).empty());
    BOOST_CHECK(BuilderAccess::GetGroups(builder).empty());

    // Add a single standalone txn
    CJournalEntry txnJournalEntry1 { NewTxn() };
    TxnGroupID groupID1 { builder.AddTxn(txnJournalEntry1) };
    BOOST_CHECK_EQUAL(BuilderAccess::GetTxns(builder).size(), 1U);
    BOOST_CHECK_EQUAL(BuilderAccess::GetGroups(builder).size(), 1U);
    BOOST_CHECK(CheckTxnInGroup(builder, txnJournalEntry1, groupID1));

    // Add another single standalone txn
    CJournalEntry txnJournalEntry2 { NewTxn() };
    TxnGroupID groupID2 { builder.AddTxn(txnJournalEntry2) };
    BOOST_CHECK_EQUAL(BuilderAccess::GetTxns(builder).size(), 2U);
    BOOST_CHECK_EQUAL(BuilderAccess::GetGroups(builder).size(), 2U);
    BOOST_CHECK(CheckTxnInGroup(builder, txnJournalEntry2, groupID2));
    BOOST_CHECK(! CheckTxnInGroup(builder, txnJournalEntry2, groupID1));
    BOOST_CHECK(! CheckTxnInGroup(builder, txnJournalEntry1, groupID2));

    // Add a single txn that spends an existing txn
    CJournalEntry txnJournalEntry3 { NewTxn({txnJournalEntry1.getTxn()}) };
    TxnGroupID groupID3 { builder.AddTxn(txnJournalEntry3) };
    BOOST_CHECK_EQUAL(BuilderAccess::GetTxns(builder).size(), 3U);
    BOOST_CHECK_EQUAL(BuilderAccess::GetGroups(builder).size(), 2U);
    BOOST_CHECK_EQUAL(groupID1, groupID3);
    BOOST_CHECK(CheckTxnInGroup(builder, txnJournalEntry3, groupID3));
    BOOST_CHECK(! CheckTxnInGroup(builder, txnJournalEntry3, groupID2));
    BOOST_CHECK(! CheckTxnInGroup(builder, txnJournalEntry2, groupID3));

    // Add a single txn that spends multiple existing txns
    CJournalEntry txnJournalEntry4 { NewTxn({txnJournalEntry2.getTxn(), txnJournalEntry3.getTxn()}) };
    TxnGroupID groupID4 { builder.AddTxn(txnJournalEntry4) };
    BOOST_CHECK_EQUAL(BuilderAccess::GetTxns(builder).size(), 4U);
    // Txn4 depends on all other txns, so everything should now be in a single group
    BOOST_CHECK_EQUAL(BuilderAccess::GetGroups(builder).size(), 1U);
    BOOST_CHECK(CheckTxnInGroup(builder, txnJournalEntry4, groupID4));

    // Clear and reset
    builder.Clear();
    BOOST_CHECK_EQUAL(BuilderAccess::GetTxns(builder).size(), 0U);
    BOOST_CHECK_EQUAL(BuilderAccess::GetGroups(builder).size(), 0U);
    BOOST_CHECK_EQUAL(BuilderAccess::GetNextGroupID(builder), 0U);
}

BOOST_AUTO_TEST_CASE(TestGroupTxn)
{
    // Builder starts out empty
    TxnGroupBuilder builder {};
    BOOST_CHECK(BuilderAccess::GetTxns(builder).empty());
    BOOST_CHECK(BuilderAccess::GetGroups(builder).empty());

    // Add a few standalone txns
    CJournalEntry txnJournalEntry1 { NewTxn() };
    CJournalEntry txnJournalEntry2 { NewTxn() };
    CJournalEntry txnJournalEntry3 { NewTxn() };
    TxnGroupID groupID1 { builder.AddTxn(txnJournalEntry1) };
    TxnGroupID groupID2 { builder.AddTxn(txnJournalEntry2) };
    TxnGroupID groupID3 { builder.AddTxn(txnJournalEntry3) };
    BOOST_CHECK_EQUAL(BuilderAccess::GetTxns(builder).size(), 3U);
    BOOST_CHECK_EQUAL(BuilderAccess::GetGroups(builder).size(), 3U);
    BOOST_CHECK(CheckTxnInGroup(builder, txnJournalEntry1, groupID1));
    BOOST_CHECK(CheckTxnInGroup(builder, txnJournalEntry2, groupID2));
    BOOST_CHECK(CheckTxnInGroup(builder, txnJournalEntry3, groupID3));

    // Add more txns explicitly to group1
    CJournalEntry txnGroup1Add1 { NewTxn() };
    CJournalEntry txnGroup1Add2 { NewTxn() };
    groupID1 = builder.AddTxn(txnGroup1Add1, groupID1);
    groupID1 = builder.AddTxn(txnGroup1Add2, groupID1);
    BOOST_CHECK_EQUAL(BuilderAccess::GetTxns(builder).size(), 5U);
    BOOST_CHECK_EQUAL(BuilderAccess::GetGroups(builder).size(), 3U);
    BOOST_CHECK(CheckTxnInGroup(builder, txnGroup1Add1, groupID1));
    BOOST_CHECK(CheckTxnInGroup(builder, txnGroup1Add2, groupID1));
    BOOST_CHECK(CheckTxnInGroup(builder, txnJournalEntry1, groupID1));

    // Add more txns explicitly to group2 that would go there anyway,
    // including 1 that spends a txn from group1
    CJournalEntry txnGroup2Add1 { NewTxn({txnJournalEntry2.getTxn()}) };
    CJournalEntry txnGroup2Add2 { NewTxn({txnJournalEntry2.getTxn(), txnGroup1Add2.getTxn()}) };
    CJournalEntry txnGroup2Add3 { NewTxn({txnGroup2Add1.getTxn()}) };
    groupID2 = builder.AddTxn(txnGroup2Add1, groupID2);
    groupID2 = builder.AddTxn(txnGroup2Add2, groupID2);
    groupID2 = builder.AddTxn(txnGroup2Add3, groupID2);
    BOOST_CHECK_EQUAL(BuilderAccess::GetTxns(builder).size(), 8U);
    BOOST_CHECK_EQUAL(BuilderAccess::GetGroups(builder).size(), 2U);
    BOOST_CHECK(CheckTxnInGroup(builder, txnGroup2Add1, groupID2));
    BOOST_CHECK(CheckTxnInGroup(builder, txnGroup2Add2, groupID2));
    BOOST_CHECK(CheckTxnInGroup(builder, txnGroup2Add3, groupID2));
    BOOST_CHECK(CheckTxnInGroup(builder, txnGroup1Add1, groupID2));
    BOOST_CHECK(CheckTxnInGroup(builder, txnGroup1Add2, groupID2));
    BOOST_CHECK(CheckTxnInGroup(builder, txnJournalEntry1, groupID2));

    // Test group removal
    BOOST_CHECK_NO_THROW(builder.RemoveGroup(groupID2));
    BOOST_CHECK_EQUAL(BuilderAccess::GetTxns(builder).size(), 1U);
    BOOST_CHECK_EQUAL(BuilderAccess::GetGroups(builder).size(), 1U);
    BOOST_CHECK_THROW(auto group = builder.GetGroup(groupID2), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(TestSelfishDetection)
{
    const Config& config { GlobalConfig::GetConfig() };
    TxnGroupBuilder builder {};

    // Selfish txn cutoff time
    int64_t selfishTime { GetSystemTimeInSeconds() - config.GetMinBlockMempoolTimeDifferenceSelfish() };

    // Add some txns before selfish cutoff time
    SetMockTime(selfishTime - 1);
    TxnGroupID groupID1 { builder.AddTxn(NewTxn()) };
    TxnGroupID groupID2 { builder.AddTxn(NewTxn()) };
    groupID2 = builder.AddTxn(NewTxn(), groupID2);
    BOOST_CHECK_EQUAL(builder.GetGroup(groupID1).size(), 1U);
    BOOST_CHECK_EQUAL(builder.GetGroup(groupID2).size(), 2U);

    // Add a txn on the selfish cutoff
    SetMockTime(selfishTime);
    TxnGroupID groupID3 { builder.AddTxn(NewTxn()) };
    BOOST_CHECK_EQUAL(builder.GetGroup(groupID1).size(), 1U);

    // Add some txns after the selfish cutoff
    SetMockTime(selfishTime + 1);
    TxnGroupID groupID4 { builder.AddTxn(NewTxn()) };
    TxnGroupID groupID5 { builder.AddTxn(NewTxn()) };
    groupID5 = builder.AddTxn(NewTxn(), groupID5);
    BOOST_CHECK_EQUAL(builder.GetGroup(groupID4).size(), 1U);
    BOOST_CHECK_EQUAL(builder.GetGroup(groupID5).size(), 2U);

    // Put time to current time and check selfish detection
    SetMockTime(GetSystemTimeInSeconds());
    BOOST_CHECK(builder.GetGroup(groupID1).IsSelfish(config));
    BOOST_CHECK(builder.GetGroup(groupID2).IsSelfish(config));
    BOOST_CHECK(! builder.GetGroup(groupID3).IsSelfish(config));
    BOOST_CHECK(! builder.GetGroup(groupID4).IsSelfish(config));
    BOOST_CHECK(! builder.GetGroup(groupID5).IsSelfish(config));

    // Add a non-selfish txn to a selfish group and check that makes the whole group non-selfish
    groupID2 = builder.AddTxn(NewTxn(), groupID2);
    BOOST_CHECK_EQUAL(builder.GetGroup(groupID2).size(), 3U);
    BOOST_CHECK(! builder.GetGroup(groupID2).IsSelfish(config));
}

BOOST_AUTO_TEST_SUITE_END()

