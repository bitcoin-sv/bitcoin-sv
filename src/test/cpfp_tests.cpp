// Copyright (c) 2020 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include <boost/test/unit_test.hpp>

#include "validation.h"
#include "config.h"
#include "script/instruction.h"
#include "script/instruction_iterator.h"
#include "mining/journal.h"
#include "mining/journal_builder.h"
#include "mining/journal_change_set.h"
#include "mining/assembler.h"
#include "validationinterface.h"

#include "test/test_bitcoin.h"
#include "test/mempool_test_access.h"

bool operator==(const CTransactionConflictData& a, const CTransactionConflictData& b)
{
    return *a.conflictedWith == *b.conflictedWith &&
           *a.blockhash == *b.blockhash;
}

std::ostream& operator<<(std::ostream& os, const CTransactionConflictData& conflict)
{
    os << (conflict.conflictedWith ? conflict.conflictedWith->ToString() : "nullptr");
    os << ' ' << (conflict.blockhash ? conflict.blockhash->ToString() : "nullptr");
    return os;
}

namespace std
{
    std::ostream& operator<<(
        std::ostream& os,
        const std::tuple<uint256,
                         MemPoolRemovalReason,
                         std::optional<CTransactionConflictData>>& x)
    {
        os << std::get<0>(x).ToString() << ' ' << std::get<1>(x) << ' '
           << std::get<2>(x).value();
        return os;
    }
}

namespace{

CTxMemPoolEntry MakeEntry(
    CFeeRate feerate, 
    std::vector<std::tuple<TxId, size_t, Amount>> inChainInputs, 
    std::vector<std::tuple<CTransactionRef, int>> inMempoolInputs,
    size_t nOutputs, size_t additionalSize=0, Amount feeAlreadyPaid=Amount{1},
    size_t opReturnSize=0)
{
    CMutableTransaction tx;
    Amount totalInput;
    for(const auto& input: inChainInputs)
    {
        auto[id, ndx, amount] = input;
        tx.vin.push_back(CTxIn(id, ndx, CScript()));
        totalInput += amount;
    }

    for(const auto& input: inMempoolInputs)
    {
        auto[txInput, ndx] = input;
        tx.vin.push_back(CTxIn(txInput->GetId(), ndx, CScript()));
        totalInput += txInput->vout[ndx].nValue;
    }

    for(size_t i = 0; i < nOutputs; i++)
    {
        CScript script;
        script << OP_TRUE;
        tx.vout.push_back(CTxOut(Amount{1}, script));
    }
    
    if(opReturnSize != 0)
    {
        CScript script;
        script << OP_FALSE << OP_RETURN;
        script << std::vector<uint8_t>(opReturnSize);
        tx.vout.push_back(CTxOut(Amount(), script));
    }

    auto txSize = ::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION) + additionalSize;
    auto totalFee = feerate.GetFee(txSize);
    auto perOputput = (totalInput - totalFee) / int64_t(nOutputs) + Amount(1);

    for(auto& output: tx.vout)
    {
        if(output.scriptPubKey.begin_instructions()->opcode() != OP_FALSE)
        {
            output.nValue = perOputput;
        }
    }

    auto txRef = MakeTransactionRef(tx);
    CTxMemPoolEntry entry(txRef, totalFee, int64_t(0), false, false, LockPoints());
    return entry;
}

TxId MakeId(uint16_t n)
{
    TxId id;
    *id.begin() = n >> 1;
    *(id.begin() + 1) = n | 0x00ff;
    return id;
}

std::vector<std::tuple<TxId, size_t, Amount>> MakeConfirmedInputs(size_t count, Amount value)
{
    static uint16_t nextTxid = 1;
    std::vector<std::tuple<TxId, size_t, Amount>> inputs;
    for (size_t i = 0; i < count; i++)
    {
        inputs.push_back(std::make_tuple(MakeId(nextTxid++), i, value));
    }
    return inputs;
}

bool checkGroupContinuity(const mining::CJournalChangeSetPtr& changeSet)
{
    std::unordered_set<mining::GroupID> seenGroups;
    const auto& changes = changeSet->getChangeSet();
    const auto disjoint = std::adjacent_find(changes.cbegin(), changes.cend(),
                                             [&seenGroups](const auto& aChange, const auto&bChange) {
        // adjacent_find returns iterator where condition is true
        auto a = aChange.second.getGroupId();
        auto b = bChange.second.getGroupId();
        return (a != b                                 // we are at a boundary
                && a                                   // ending sequence was a group
                && !seenGroups.insert(a).second);      // that we have already seen
    });
    return disjoint == changes.cend();
}

CFeeRate DefaultFeeRate()
{
    return CFeeRate(DEFAULT_BLOCK_MIN_TX_FEE);
}

CTxMemPoolTestAccess::txiter AddToMempool(CTxMemPoolEntry& entry)
{
    mempool.AddUnchecked(entry.GetSharedTx()->GetId(), entry, TxStorage::memory, {});
    CTxMemPoolTestAccess testAccess(mempool);
    auto it = testAccess.mapTx().find(entry.GetSharedTx()->GetId());
    BOOST_ASSERT(it != testAccess.mapTx().end());
    return it;
}

std::unique_ptr<mining::CBlockTemplate> CreateBlock()
{
    CBlockIndex* pindexPrev {nullptr};
    std::unique_ptr<mining::CBlockTemplate> pblocktemplate;
    CScript scriptPubKey =
    CScript() << ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909"
                            "a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112"
                            "de5c384df7ba0b8d578a4c702b6bf11d5f")
                << OP_CHECKSIG;
    BOOST_CHECK(pblocktemplate = mining::g_miningFactory->GetAssembler()->CreateNewBlock(scriptPubKey, pindexPrev));
    return pblocktemplate;
}

using JournalEntry = mining::CJournalEntry;
using JournalTester = mining::CJournalTester;
}

BOOST_FIXTURE_TEST_SUITE(cpfp_tests, TestingSetup)

mining::CJournalPtr CheckMempoolRebuild(CTxMemPoolTestAccess& testAccess)
{
    auto oldJournal = testAccess.getJournalBuilder().getCurrentJournal();
    auto contentsBefore = JournalTester(oldJournal).getContents();
    auto oldMapTx = testAccess.mapTx();

    auto changeSet = mempool.RebuildMempool();
    
    BOOST_CHECK(checkGroupContinuity(changeSet));
    BOOST_CHECK(changeSet->CheckTopoSort());
    changeSet->apply();

    auto newJournal = testAccess.getJournalBuilder().getCurrentJournal();
    auto contentsAfter = JournalTester(newJournal).getContents();
    BOOST_CHECK(contentsBefore == contentsAfter);

    auto newMapTx = testAccess.mapTx();

    BOOST_TEST(oldMapTx.size() == newMapTx.size());
    const auto& oldAccess = oldMapTx.get<insertion_order>();
    const auto& newAccess = newMapTx.get<insertion_order>();
    for(auto it1 = oldAccess.begin(),it2 =  newAccess.begin(); it1 != oldAccess.end(); it1++, it2++)
    {
        BOOST_CHECK(it1->GetTxId() == it2->GetTxId());
        BOOST_CHECK(it1->IsCPFPGroupMember() == it2->IsCPFPGroupMember());
        BOOST_CHECK(it1->IsInPrimaryMempool() == it2->IsInPrimaryMempool());
    }

    return newJournal;
}

BOOST_AUTO_TEST_CASE(group_forming_and_disbanding) 
{
    //           |                  |
    //           |            entryNotPaying
    //           |                  |
    //   entryNotPaying3    entryPayForItself
    //           |            |     |    |
    //           +------------+     |    +-------------+
    //           |                  |                  |
    //    entryNotPaying4    entryPayForGroup   entryNotPaying2
    //           |
    //  entryPayingFor3And4
    //
    //  entries in group1 (entering primary mempool): entryNotPaying, entryPayForItself and entryPayForGroup
    //  entries in group2 (entering primary mempool): entryNotPaying3, entryNotPaying4 and entryPayingFor3And4
    //  entry still in secondary: entryNotPaying2

    mempool.SetSanityCheck(0);

    auto entryNotPaying = MakeEntry(CFeeRate{}, MakeConfirmedInputs(1, Amount{1000000}), {}, 1);

    auto entryPayForItself = MakeEntry(DefaultFeeRate(), {}, {std::make_tuple(entryNotPaying.GetSharedTx(), 0)}, 3);

    auto entryNotPaying2 = MakeEntry(CFeeRate{}, {}, {std::make_tuple(entryPayForItself.GetSharedTx(), 1)}, 1);
    
    auto entryNotPaying3 = MakeEntry(CFeeRate{}, MakeConfirmedInputs(1, Amount{1000000}), {}, 1);

    auto entryNotPaying4 = MakeEntry(CFeeRate{}, {}, {std::make_tuple(entryPayForItself.GetSharedTx(), 2), std::make_tuple(entryNotPaying3.GetSharedTx(), 0)}, 1);
    
    auto sizeOfNotPaying3and4 = entryNotPaying3.GetSharedTx()->GetTotalSize() + entryNotPaying4.GetSharedTx()->GetTotalSize();
    auto feeOfNotPaying3and4 = entryNotPaying3.GetModifiedFee() + entryNotPaying4.GetModifiedFee();
    auto entryPayingFor3And4 = MakeEntry(DefaultFeeRate(), {}, {std::make_tuple(entryNotPaying4.GetSharedTx(), 0)}, 1, sizeOfNotPaying3and4, feeOfNotPaying3and4);

    auto sizeSoFar = entryNotPaying.GetSharedTx()->GetTotalSize() + entryPayForItself.GetSharedTx()->GetTotalSize();
    auto feeSoFar = entryNotPaying.GetModifiedFee() + entryPayForItself.GetModifiedFee();
    auto entryPayForGroup = MakeEntry(DefaultFeeRate(), {}, {std::make_tuple(entryPayForItself.GetSharedTx(), 0)}, 1, sizeSoFar, feeSoFar);

    CTxMemPoolTestAccess testAccess(mempool);
    auto journal = testAccess.getJournalBuilder().getCurrentJournal();

    auto notPayingIt = AddToMempool(entryNotPaying);
    BOOST_ASSERT(!notPayingIt->IsInPrimaryMempool());

    auto payForItselfIt = AddToMempool(entryPayForItself);
    BOOST_ASSERT(!payForItselfIt->IsInPrimaryMempool());

    auto notPaying2It = AddToMempool(entryNotPaying2);
    BOOST_ASSERT(!notPaying2It->IsInPrimaryMempool());
    
    auto notPaying3It = AddToMempool(entryNotPaying3);
    BOOST_ASSERT(!notPaying3It->IsInPrimaryMempool());

    auto notPaying4It = AddToMempool(entryNotPaying4);
    BOOST_ASSERT(!notPaying4It->IsInPrimaryMempool());

    // entryPayingFor3And4 pays for entryNotPaying4 and entryNotPaying3 
    // but not enough for entryPayForItself and entryNotPaying
    // so it will not be able to form a group yet
    auto payFor3And4It = AddToMempool(entryPayingFor3And4); 
    BOOST_ASSERT(!payFor3And4It->IsInPrimaryMempool());

    // still nothing is accepted to primary mempool
    BOOST_ASSERT(JournalTester(journal).journalSize() == 0);

    // now we will add payForGroupIt which pays enough for entryPayForItself and entryNotPaying
    // this will cause forming a group
    auto payForGroupIt = AddToMempool(entryPayForGroup);
    BOOST_ASSERT(payForGroupIt->IsInPrimaryMempool() && payForGroupIt->IsCPFPGroupMember());
    BOOST_ASSERT(payForItselfIt->IsInPrimaryMempool() && payForItselfIt->IsCPFPGroupMember());
    BOOST_ASSERT(notPayingIt->IsInPrimaryMempool() && notPayingIt->IsCPFPGroupMember());
    
    BOOST_ASSERT(payForGroupIt->GetCPFPGroup() == payForItselfIt->GetCPFPGroup());
    BOOST_ASSERT(payForGroupIt->GetCPFPGroup() == notPayingIt->GetCPFPGroup());

    // as the entryNotPaying4 (and consequently entryPayingFor3And4) is no longer obliged to pay
    // for entryPayForItself and entryNotPaying, a new group can be formed
    // (entryPayingFor3And4 pays for entryNotPaying4 and entryNotPaying3)
    BOOST_ASSERT(payFor3And4It->IsInPrimaryMempool() && payFor3And4It->IsCPFPGroupMember());
    BOOST_ASSERT(notPaying4It->IsInPrimaryMempool() && notPaying4It->IsCPFPGroupMember());
    BOOST_ASSERT(notPaying3It->IsInPrimaryMempool() && notPaying3It->IsCPFPGroupMember());

    BOOST_ASSERT(payFor3And4It->GetCPFPGroup() == notPaying4It->GetCPFPGroup());
    BOOST_ASSERT(payFor3And4It->GetCPFPGroup() == notPaying3It->GetCPFPGroup());

    // check that they are not part of the same group
    BOOST_ASSERT(payForGroupIt->GetCPFPGroup() != payFor3And4It->GetCPFPGroup());

    // nobody payed for notPaying2It, still in secondary mempool
    BOOST_ASSERT(!notPaying2It->IsInPrimaryMempool());

    // journal is no longer empty
    BOOST_ASSERT(JournalTester(journal).journalSize() != 0);

    // check content of the journal
    BOOST_ASSERT(JournalTester(journal).checkTxnExists(JournalEntry{*notPayingIt}));
    BOOST_ASSERT(JournalTester(journal).checkTxnExists(JournalEntry{*payForItselfIt}));
    BOOST_ASSERT(JournalTester(journal).checkTxnExists(JournalEntry{*payForGroupIt}));

    BOOST_ASSERT(JournalTester(journal).checkTxnExists(JournalEntry{*payFor3And4It}));
    BOOST_ASSERT(JournalTester(journal).checkTxnExists(JournalEntry{*notPaying4It}));
    BOOST_ASSERT(JournalTester(journal).checkTxnExists(JournalEntry{*notPaying3It}));

    BOOST_ASSERT(! JournalTester(journal).checkTxnExists(JournalEntry{*notPaying2It}));

    
    // remove payFor3And4It, notPaying4It from mempool
    CTxMemPoolTestAccess::setEntries entriesToRemove = {payFor3And4It, notPaying4It};
    auto changeSet = testAccess.getJournalBuilder().getNewChangeSet(mining::JournalUpdateReason::UNKNOWN);
    testAccess.removeStagedNL(entriesToRemove, *changeSet, CTransactionConflict{}, MemPoolRemovalReason::UNKNOWN);
    
    changeSet->apply();
    changeSet->clear();

    // entries which we have removed, they should removed from mempool and also from the journal
    for(auto entry: {entryNotPaying4, entryPayingFor3And4})
    {
        BOOST_ASSERT(testAccess.mapTx().find(entry.GetTxId()) == testAccess.mapTx().end());
        BOOST_ASSERT(!JournalTester(journal).checkTxnExists(JournalEntry{entry}));
    }

    // unaffected entries, they should stay in the mempool and journal
    for(auto entry: {entryNotPaying, entryPayForItself, entryPayForGroup})
    {
        BOOST_ASSERT(testAccess.mapTx().find(entry.GetTxId()) != testAccess.mapTx().end());
        BOOST_ASSERT(JournalTester(journal).checkTxnExists(JournalEntry{entry}));
    }

    // notPaying3It is still in the mempool
    BOOST_ASSERT(testAccess.mapTx().find(entryNotPaying3.GetTxId()) != testAccess.mapTx().end());
    // but not in the journal
    BOOST_ASSERT(!JournalTester(journal).checkTxnExists(JournalEntry{entryNotPaying3}));

    
    // return removed transactions back to mempool
    
    notPaying4It = AddToMempool(entryNotPaying4);
    payFor3And4It = AddToMempool(entryPayingFor3And4); 
    BOOST_ASSERT(notPaying4It->IsInPrimaryMempool());
    BOOST_ASSERT(payFor3And4It->IsInPrimaryMempool());

    // things should be as before removal
    for(auto entry: {entryNotPaying, entryPayForItself, entryPayForGroup, entryNotPaying3, entryNotPaying4, entryPayingFor3And4})
    {
        BOOST_ASSERT(testAccess.mapTx().find(entry.GetTxId()) != testAccess.mapTx().end());
        BOOST_ASSERT(JournalTester(journal).checkTxnExists(JournalEntry{entry}));
    }

    // now remove entryPayForGroup
    entriesToRemove.clear();
    entriesToRemove.insert(payForGroupIt);
    testAccess.removeStagedNL(entriesToRemove, *changeSet, CTransactionConflict{}, MemPoolRemovalReason::UNKNOWN);
    changeSet->apply();

    // everything should be removed from journal
    BOOST_ASSERT(JournalTester(journal).journalSize() == 0);

    // and nothing should stay in the primary mempool
    for(const auto& entryIt: testAccess.mapTx())
    {
        BOOST_ASSERT(!entryIt.IsInPrimaryMempool());
    }

    // now raise modified fee for the entryPayingFor3And4 so that it can pay for all ancestors (entryNotPaying4, entryNotPaying3, entryPaysForItself, entryNotPaying)
    mempool.PrioritiseTransaction(entryPayingFor3And4.GetTxId(), entryPayingFor3And4.GetTxId().GetHex(), Amount{10000});
    for(const auto& entryIt: {notPayingIt, notPaying3It, notPaying4It, payForItselfIt, payFor3And4It})
    {
        BOOST_ASSERT(entryIt->IsInPrimaryMempool());
        BOOST_ASSERT(JournalTester(journal).checkTxnExists(JournalEntry{*entryIt}));
    }

    CheckMempoolRebuild(testAccess);
};

BOOST_AUTO_TEST_CASE(group_recalculation_when_removing_for_block)
{
    //  
    //  entryNotPaying1            entryNotPaying3            RemoveForBlock
    //        |                           |
    // ----------------------------------------------------------
    //        |                           |
    //  entryNotPaying2            entryPaysForItself
    //        |                           |
    //   entryPaysFor2               entryPaysFor3
    // 


    // before: 1. entryPaysFor2 can not form a group
    //         2. entryPaysFor3 forms a group
    //
    // after: 1. entryPaysFor2 forms a group (got rid of the entryNotPaying1 debt)
    //        2  entryPaysFor3 group is disbanded, and entryPaysForItself and entryPaysFor3 are accepted as standalone

    mempool.SetSanityCheck(0);

    auto entryNotPaying1 = MakeEntry(CFeeRate{}, MakeConfirmedInputs(1, Amount{1000000}), {}, 1);
    auto entryNotPaying2 = MakeEntry(CFeeRate{}, {}, {std::make_tuple(entryNotPaying1.GetSharedTx(), 0)}, 1);
    auto entryPaysFor2 = MakeEntry(DefaultFeeRate(), {}, {std::make_tuple(entryNotPaying2.GetSharedTx(), 0)}, 1, entryNotPaying2.GetSharedTx()->GetTotalSize());
    auto entryNotPaying3 = MakeEntry(CFeeRate{}, MakeConfirmedInputs(1, Amount{1000000}), {}, 1);
    auto entryPayForItself = MakeEntry(DefaultFeeRate(), {}, {std::make_tuple(entryNotPaying3.GetSharedTx(), 0)}, 3);
    auto entryPaysFor3 = MakeEntry(CFeeRate{Amount{10000}}, {}, {std::make_tuple(entryPayForItself.GetSharedTx(), 0)}, 3);

    auto notPaying1   = AddToMempool(entryNotPaying1);
    auto notPaying2   = AddToMempool(entryNotPaying2);
    auto paysFor2     = AddToMempool(entryPaysFor2);
    auto notPaying3   = AddToMempool(entryNotPaying3);
    auto payForItself = AddToMempool(entryPayForItself);
    auto paysFor3     = AddToMempool(entryPaysFor3);

    CTxMemPoolTestAccess testAccess(mempool);
    auto journal = testAccess.getJournalBuilder().getCurrentJournal();

    for(auto entryIt: {notPaying1, notPaying2, paysFor2})
    {
        BOOST_ASSERT(!entryIt->IsInPrimaryMempool());
        BOOST_ASSERT(!JournalTester(journal).checkTxnExists(JournalEntry{*entryIt}));
    }

    for(auto entryIt: {notPaying3, payForItself, paysFor3})
    {
        BOOST_ASSERT(entryIt->IsCPFPGroupMember());
        BOOST_ASSERT(JournalTester(journal).checkTxnExists(JournalEntry{*entryIt}));
    }

    std::vector<CTransactionRef> vtx;
    mempool.RemoveForBlock({ entryNotPaying1.GetSharedTx(), entryNotPaying3.GetSharedTx() }, mining::CJournalChangeSetPtr{}, uint256{}, vtx, testConfig);

    for(auto entryIt: {notPaying2, paysFor2})
    {
        BOOST_ASSERT(entryIt->IsInPrimaryMempool());
        BOOST_ASSERT(JournalTester(journal).checkTxnExists(JournalEntry{*entryIt}));
    }

    for(auto entryIt: {payForItself, paysFor3})
    {
        BOOST_ASSERT(!entryIt->IsCPFPGroupMember());
        BOOST_ASSERT(entryIt->IsInPrimaryMempool());
        BOOST_ASSERT(JournalTester(journal).checkTxnExists(JournalEntry{*entryIt}));
    }
    
    CheckMempoolRebuild(testAccess);
};

BOOST_AUTO_TEST_CASE(mempool_rebuild)
{
    // 
    //  entry1    + -------------- entryGroup1Tx1
    //       |    |                       |
    //  entryGroup2Tx1 ----- + --- entryGroup1Tx2
    //       |               |            |
    //  entryGroup2Tx2    entry2   entryNonPaying1
    //                                    |
    //                             entryNonPaying2

    mempool.SetSanityCheck(0);

    auto entry1 = MakeEntry(DefaultFeeRate(), MakeConfirmedInputs(1, Amount{1000000}), {}, 1);
    auto entryGroup1Tx1 = MakeEntry(CFeeRate{}, MakeConfirmedInputs(1, Amount{1000000}), {}, 2);
    auto entryGroup1Tx2 = MakeEntry(DefaultFeeRate(), {}, {std::make_tuple(entryGroup1Tx1.GetSharedTx(), 1)}, 2, entryGroup1Tx1.GetSharedTx()->GetTotalSize());
    auto entryGroup2Tx1 = MakeEntry(CFeeRate{}, {}, {std::make_tuple(entry1.GetSharedTx(), 0), std::make_tuple(entryGroup1Tx1.GetSharedTx(), 0)}, 2);
    auto entryGroup2Tx2 = MakeEntry(DefaultFeeRate(), {}, {std::make_tuple(entryGroup2Tx1.GetSharedTx(), 0)}, 5, entryGroup2Tx1.GetSharedTx()->GetTotalSize());
    auto entryNonPaying1 = MakeEntry(CFeeRate{}, {}, {std::make_tuple(entryGroup1Tx2.GetSharedTx(), 1)}, 1);
    auto entryNonPaying2 = MakeEntry(CFeeRate{}, {}, {std::make_tuple(entryNonPaying1.GetSharedTx(), 0)}, 1);
    auto entry2 = MakeEntry(DefaultFeeRate(), {}, {std::make_tuple(entryGroup2Tx1.GetSharedTx(), 1), std::make_tuple(entryGroup1Tx2.GetSharedTx(), 0)}, 3);
    
    auto tx1 = AddToMempool(entry1);
    auto tx2 = AddToMempool(entryGroup1Tx1);
    auto tx3 = AddToMempool(entryGroup1Tx2);
    auto tx4 = AddToMempool(entryGroup2Tx1);
    auto tx5 = AddToMempool(entryGroup2Tx2);
    auto tx6 = AddToMempool(entryNonPaying1);
    auto tx7 = AddToMempool(entryNonPaying2);
    auto tx8 = AddToMempool(entry2);

    CTxMemPoolTestAccess testAccess(mempool);
    mempool.SetSanityCheck(0);// CheckMempool checks coins also. we do not have coins in this tests
    auto journal = testAccess.getJournalBuilder().getCurrentJournal();

    for(auto entryIt: {tx1, tx2, tx3, tx4, tx5, tx8})
    {
        BOOST_ASSERT(entryIt->IsInPrimaryMempool());
        BOOST_ASSERT(JournalTester(journal).checkTxnExists(JournalEntry{*entryIt}));
    }

    for(auto entryIt: {tx6, tx7})
    {
        BOOST_ASSERT(!entryIt->IsInPrimaryMempool());
        BOOST_ASSERT(!JournalTester(journal).checkTxnExists(JournalEntry{*entryIt}));
    }

    for(auto entryIt: {tx2, tx3, tx4, tx5})
    {
        BOOST_ASSERT(entryIt->IsCPFPGroupMember());
    }

    CheckMempoolRebuild(testAccess);

};

BOOST_AUTO_TEST_CASE(journal_groups)
{
    // 
    //  entry1     entryGroup1Tx1
    //                   |
    //             entryGroup1Tx1
    //
    //
    
    CTxMemPoolTestAccess testAccess(mempool);

    mempool.SetSanityCheck(0);
    testConfig.SetMaxGeneratedBlockSize(250000);
    CheckMempoolRebuild(testAccess);

    auto journal = testAccess.getJournalBuilder().getCurrentJournal();

    auto entry1 = MakeEntry(DefaultFeeRate(), MakeConfirmedInputs(1, Amount(1000000)), {}, 1, 0, Amount{0}, 100000);
    auto entryGroup1Tx1 = MakeEntry(CFeeRate(), MakeConfirmedInputs(1, Amount(1000000)), {}, 2, 0, Amount{0}, 100000);
    auto entryGroup1Tx2 = MakeEntry(DefaultFeeRate(), {}, {std::make_tuple(entryGroup1Tx1.GetSharedTx(), 1)}, 2, entryGroup1Tx1.GetSharedTx()->GetTotalSize(), Amount{0}, 100000);
    
    auto tx1 = AddToMempool(entry1);
    auto tx2 = AddToMempool(entryGroup1Tx1);
    auto tx3 = AddToMempool(entryGroup1Tx2);

    BOOST_CHECK(tx2->GetCPFPGroupId() == tx3->GetCPFPGroupId());

    std::this_thread::sleep_for(std::chrono::seconds(1));

    for(auto entryIt: {tx1, tx2, tx3})
    {
        BOOST_CHECK(entryIt->IsInPrimaryMempool());
        BOOST_CHECK(JournalTester(journal).checkTxnExists(JournalEntry{*entryIt}));
    }

    for(auto entryIt: {tx2, tx3})
    {
        BOOST_CHECK(entryIt->IsCPFPGroupMember());
    }

    auto blockTemplatePtr = CreateBlock();
    auto vtx = blockTemplatePtr->GetBlockRef()->vtx;
    BOOST_CHECK(vtx.size() == 2);
    BOOST_CHECK(entry1.GetTxId() == vtx[1]->GetId());

    testAccess.RemoveRecursive(*entry1.GetSharedTx(), {nullptr});

    blockTemplatePtr = CreateBlock();
    vtx = blockTemplatePtr->GetBlockRef()->vtx;
    BOOST_CHECK(vtx.size() == 3);
    BOOST_CHECK(entryGroup1Tx1.GetTxId() == vtx[1]->GetId());
    BOOST_CHECK(entryGroup1Tx2.GetTxId() == vtx[2]->GetId());

};

BOOST_AUTO_TEST_CASE(conflicts)
{
    mempool.SetSanityCheck(0);
    CTxMemPoolTestAccess testAccess(mempool);
    auto journal = testAccess.getJournalBuilder().getCurrentJournal();
    
    //               |                            |
    //     entryDoubleSpendMempool         entryToBeMined          
    //               |                       |        | 
    //     ------------------------------------------------------ CONTENT OF THE BLOCK(entryDoubleSpendBlock, entryToBeMined)
    //               |                       |        | 
    //               +-------+        +------+        +---+
    //                       |        |                   |
    //                   entryDoubleSpendChild        entryStayInMempool
    //
    // entryDoubleSpendMempool is in conflict with entryDoubleSpendBlock causing that entryDoubleSpendChild will be removed from the mempool
    //
    //

    auto inputForDoubleSpend = MakeConfirmedInputs(1, Amount{1000000});

    auto entryDoubleSpendMempool = MakeEntry(CFeeRate(CFeeRate()), inputForDoubleSpend, {}, 1);
    auto entryDoubleSpendBlock = MakeEntry(CFeeRate(CFeeRate()), inputForDoubleSpend, {}, 2);
    auto entryToBeMined = MakeEntry(CFeeRate(), MakeConfirmedInputs(1, Amount{1000000}), {}, 2);
    auto entryDoubleSpendChild = MakeEntry(CFeeRate(Amount(20000)), {}, {std::make_tuple(entryDoubleSpendMempool.GetSharedTx(), 0), std::make_tuple(entryToBeMined.GetSharedTx(), 0)}, 1);
    auto entryStayInMempool = MakeEntry(CFeeRate(Amount(20000)), {}, {std::make_tuple(entryToBeMined.GetSharedTx(), 1)}, 1);

    auto tx1 = AddToMempool(entryDoubleSpendMempool);
    auto tx2 = AddToMempool(entryToBeMined);
    auto tx3 = AddToMempool(entryDoubleSpendChild);
    auto tx4 = AddToMempool(entryStayInMempool);

    for(auto entryIt: {tx1, tx2, tx3, tx4})
    {
        BOOST_CHECK(entryIt->IsInPrimaryMempool());
        BOOST_CHECK(JournalTester(journal).checkTxnExists(JournalEntry{*entryIt}));
    }

    std::vector<CTransactionRef> vtx;
    mempool.RemoveForBlock({ entryDoubleSpendBlock.GetSharedTx(), entryToBeMined.GetSharedTx() }, mining::CJournalChangeSetPtr{}, uint256{}, vtx, testConfig);

    BOOST_CHECK(tx4->IsInPrimaryMempool());
    BOOST_CHECK(JournalTester(journal).checkTxnExists(JournalEntry{*tx4}));
    BOOST_CHECK(mempool.GetTransactions().size() == 1);
}

namespace
{
    void generate_outputs(CMutableTransaction& mtx, const size_t nOutputs)
    {
        CScript script;
        script << OP_TRUE;
        mtx.vout.reserve(mtx.vout.size() + nOutputs);
        std::generate_n(back_inserter(mtx.vout), nOutputs, [&script]()
        {
            return CTxOut{Amount{1}, script};
        });
    }

    CMutableTransaction MakeMutableTx(const std::vector<COutPoint>& inputs,
                                      const size_t nOutputs=0)
    {
        CMutableTransaction mtx;
        std::transform(inputs.begin(),
                       inputs.end(),
                       std::back_inserter(mtx.vin),
                       [](const auto& ip) { return CTxIn{ip}; });
        generate_outputs(mtx, nOutputs);
        return mtx;
    }

    std::unique_ptr<const CTransaction> MakeTx(const std::vector<COutPoint>& inputs,
                                         const size_t nOutputs=0)
    {
        const CMutableTransaction mtx{MakeMutableTx(inputs)};
        return std::make_unique<const CTransaction>(mtx);
    }
    
    CTxMemPoolEntry MakeMemPoolEntry(const std::vector<COutPoint>& inputs,
                                     const size_t nOutputs=0)
    {
        const CMutableTransaction mtx{MakeMutableTx(inputs, nOutputs)};
        return CTxMemPoolEntry{std::make_shared<const CTransaction>(mtx),
                               Amount{},
                               int64_t(0),
                               false,
                               false,
                               LockPoints()};
    }
    
    struct test_validator : CValidationInterface
    {
        test_validator()
        {
            RegisterValidationInterface();
        }
        ~test_validator()
        {
            UnregisterValidationInterface();
        }

        void RegisterValidationInterface() override
        {
            using namespace boost::placeholders;
            slotConnection = GetMainSignals().TransactionRemovedFromMempool.connect(boost::bind(&test_validator::TransactionRemovedFromMempool, this, _1, _2, _3));
        }
        void UnregisterValidationInterface() override
        {
            slotConnection.disconnect();
        }

        void TransactionRemovedFromMempool(
            const uint256& txid,
            MemPoolRemovalReason reason,
            const std::optional<CTransactionConflictData>& conflictedWith) override
        {
            notifications_.emplace_back(txid, reason, conflictedWith);
        }

        using value_type  = std::tuple<uint256,
                                       MemPoolRemovalReason,
                                       std::optional<CTransactionConflictData>>;
        using notifications_type = std::vector<value_type>;
        notifications_type notifications_;
        boost::signals2::scoped_connection slotConnection {};
    };
}

BOOST_AUTO_TEST_CASE(double_spend_notifications)
{
    using namespace std;

    test_validator validator;
    CTxMemPool mempool;

    // generate multiple outpoints to spend from the same txid
    const auto txid = MakeId(1);
    uint32_t i{};
    vector<COutPoint> double_spent_ops;
    constexpr size_t n_double_spent_ops{3};
    generate_n(back_inserter(double_spent_ops),
               n_double_spent_ops,
               [&i, &txid]() {
                   return COutPoint{txid, ++i};
               });

    // make a tx for the incoming block
    const shared_ptr<const CTransaction> block_tx = MakeTx(double_spent_ops); 
   
    // make a parent tx for the mempool 
    const auto mempool_entry_parent = MakeMemPoolEntry(double_spent_ops, 1);
    const auto parent_tx = mempool_entry_parent.GetSharedTx().get();
    const auto parent_txid = parent_tx->GetId();
    mempool.AddUnchecked(parent_txid, 
                         mempool_entry_parent,
                         TxStorage::memory,
                         {});

    // make a child tx for the mempool
    const std::vector<COutPoint> outpoints{ {parent_tx->GetId(), 0} };
    const auto mempool_entry_child = MakeMemPoolEntry(outpoints);
    const auto child_tx = mempool_entry_child.GetSharedTx().get();
    const auto child_txid = child_tx->GetId();
    mempool.AddUnchecked(child_txid, 
                         mempool_entry_child,
                         TxStorage::memory,
                         {});

    const uint256 block_hash;
    vector<shared_ptr<const CTransaction>> vtx;
    mempool.RemoveForBlock({block_tx},
                           mining::CJournalChangeSetPtr{},
                           block_hash,
                           vtx,
                           GlobalConfig::GetConfig());
    BOOST_CHECK_EQUAL(0U, mempool.Size());

    test_validator::notifications_type
        expected{{child_txid,
                  MemPoolRemovalReason::CONFLICT,
                  CTransactionConflictData{block_tx.get(), &block_hash}},
                 {parent_txid,
                  MemPoolRemovalReason::CONFLICT,
                  CTransactionConflictData{block_tx.get(), &block_hash}}};
    BOOST_CHECK_EQUAL_COLLECTIONS(expected.begin(),
                                  expected.end(),
                                  validator.notifications_.begin(),
                                  validator.notifications_.end());
}

BOOST_AUTO_TEST_SUITE_END()
