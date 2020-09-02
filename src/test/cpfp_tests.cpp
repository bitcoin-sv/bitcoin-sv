// Copyright (c) 2020 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include <boost/test/unit_test.hpp>

#include "validation.h"
#include "mining/journal.h"
#include "mining/journal_builder.h"
#include "mining/journal_change_set.h"

#include "test/test_bitcoin.h"
#include "test/mempool_test_access.h"



CTxMemPoolEntry MakeEntry(
    CTxMemPoolBase &pool,
    CFeeRate feerate, 
    std::vector<std::tuple<TxId, int, Amount>> inChainInputs, 
    std::vector<std::tuple<CTransactionRef, int>> inMempoolInputs,
    size_t nOutputs, size_t additionalSize=0, Amount feeAlreadyPaid=Amount(0))
{
    CMutableTransaction tx;
    Amount totalInput;
    Amount totalInChainInput;
    for(const auto& input: inChainInputs)
    {
        auto[id, ndx, amount] = input;
        tx.vin.push_back(CTxIn(id, ndx, CScript()));
        totalInput += amount;
        totalInChainInput += amount;
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
        tx.vout.push_back(CTxOut(Amount(), script));
    }
    
    auto txSize = ::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION) + additionalSize;
    auto totalFee = feerate.GetFee(txSize);
    auto perOputput = (totalInput - totalFee) / int64_t(nOutputs);

    for(auto& output: tx.vout)
    {
        output.nValue = perOputput;
    }

    auto txRef = MakeTransactionRef(tx);
    CTxMemPoolEntry entry(txRef, totalFee, int64_t(0), false, 0, totalInChainInput, false, LockPoints(), pool);
    return entry;
}

TxId MakeId(uint16_t n)
{
    TxId id;
    *id.begin() = n >> 1;
    *(id.begin() + 1) = n | 0x00ff;
    return id;
}

std::vector<std::tuple<TxId, int, Amount>> MakeConfirmedInputs(size_t count, Amount value)
{
    static uint16_t nextTxid = 1;
    std::vector<std::tuple<TxId, int, Amount>> inputs;
    for( int i = 0; i < static_cast<int>(count); i++)
    {
        inputs.push_back(std::make_tuple(MakeId(nextTxid++), i, value));
    }
    return inputs;
}

CFeeRate DefaultFeeRate()
{
    return CFeeRate(DEFAULT_BLOCK_MIN_TX_FEE);
}

CTxMemPoolTestAccess::txiter AddToMempool(CTxMemPoolEntry& entry)
{
    mempool.AddUnchecked(entry.GetSharedTx()->GetId(), entry, {});
    CTxMemPoolTestAccess testAccess(mempool);
    auto it = testAccess.mapTx().find(entry.GetSharedTx()->GetId());
    BOOST_ASSERT(it != testAccess.mapTx().end());
    return it;
}

BOOST_FIXTURE_TEST_SUITE(cpfp_tests, TestingSetup)

BOOST_AUTO_TEST_CASE(group_forming_and_disbanding) 
{
    //          |                    |
    //          |              entryNotPaying
    //          |                    |
    // entryNotPaying3   +---- entryPayForItself ----+
    //          |        |           |               |
    //     entryNotPaying4     entryPayForGroup   entryNotPaying2
    //          |                    
    //     entryPayingFor3And4
    //
    //  
    //
    //
    //  entries in group1 (entering primary mempool): entryNotPaying, entryPayForItself and entryPayForGroup
    //  entries in group2 (entering primary mempool): entryNotPaying3, entryNotPaying4 and entryPayingFor3And4
    //  entry still in secondary: entryNotPaying2

    auto entryNotPaying = MakeEntry(mempool, CFeeRate(), MakeConfirmedInputs(1, Amount(1000000)), {}, 1);
    
    auto entryPayForItself = MakeEntry(mempool, DefaultFeeRate(), {}, {std::make_tuple(entryNotPaying.GetSharedTx(), 0)}, 3);

    auto entryNotPaying2 = MakeEntry(mempool, CFeeRate(), {}, {std::make_tuple(entryPayForItself.GetSharedTx(), 1)}, 1);
    
    auto entryNotPaying3 = MakeEntry(mempool, CFeeRate(), MakeConfirmedInputs(1, Amount(1000000)), {}, 1);

    auto entryNotPaying4 = MakeEntry(mempool, CFeeRate(), {}, {std::make_tuple(entryPayForItself.GetSharedTx(), 2), std::make_tuple(entryNotPaying3.GetSharedTx(), 0)}, 1);
    
    auto sizeOfNotPaying3and4 = entryNotPaying3.GetSharedTx()->GetTotalSize() + entryNotPaying4.GetSharedTx()->GetTotalSize();
    auto feeOfNotPaying3and4 = entryNotPaying3.GetModifiedFee() + entryNotPaying4.GetModifiedFee();
    auto entryPayingFor3And4 = MakeEntry(mempool, DefaultFeeRate(), {}, {std::make_tuple(entryNotPaying4.GetSharedTx(), 0)}, 1, sizeOfNotPaying3and4, feeOfNotPaying3and4);

    auto sizeSoFar = entryNotPaying.GetSharedTx()->GetTotalSize() + entryPayForItself.GetSharedTx()->GetTotalSize();
    auto feeSoFar = entryNotPaying.GetModifiedFee() + entryPayForItself.GetModifiedFee();
    auto entryPayForGroup = MakeEntry(mempool, DefaultFeeRate(), {}, {std::make_tuple(entryPayForItself.GetSharedTx(), 0)}, 1, sizeSoFar, feeSoFar);

    
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
    BOOST_ASSERT(mining::CJournalTester(journal).journalSize() == 0);

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
    BOOST_ASSERT(mining::CJournalTester(journal).journalSize() != 0);

    // check content of the journal
    BOOST_ASSERT(mining::CJournalTester(journal).checkTxnExists({*notPayingIt}));
    BOOST_ASSERT(mining::CJournalTester(journal).checkTxnExists({*payForItselfIt}));
    BOOST_ASSERT(mining::CJournalTester(journal).checkTxnExists({*payForGroupIt}));

    BOOST_ASSERT(mining::CJournalTester(journal).checkTxnExists({*payFor3And4It}));
    BOOST_ASSERT(mining::CJournalTester(journal).checkTxnExists({*notPaying4It}));
    BOOST_ASSERT(mining::CJournalTester(journal).checkTxnExists({*notPaying3It}));

    BOOST_ASSERT(! mining::CJournalTester(journal).checkTxnExists({*notPaying2It}));

    
    // remove payFor3And4It, notPaying4It from mempool
    CTxMemPoolTestAccess::setEntries entriesToRemove = {payFor3And4It, notPaying4It};
    auto changeSet = testAccess.getJournalBuilder().getNewChangeSet(mining::JournalUpdateReason::UNKNOWN);
    testAccess.removeStagedNL(entriesToRemove, *changeSet, MemPoolRemovalReason::UNKNOWN);
    
    changeSet->apply();
    changeSet->clear();

    // entries which we have removed, they should removed from mempool and also from the journal
    for(auto entry: {entryNotPaying4, entryPayingFor3And4})
    {
        BOOST_ASSERT(testAccess.mapTx().find(entry.GetTxId()) == testAccess.mapTx().end());
        BOOST_ASSERT(!mining::CJournalTester(journal).checkTxnExists({entry}));
    }

    // unaffected entries, they should stay in the mempool and journal
    for(auto entry: {entryNotPaying, entryPayForItself, entryPayForGroup})
    {
        BOOST_ASSERT(testAccess.mapTx().find(entry.GetTxId()) != testAccess.mapTx().end());
        BOOST_ASSERT(mining::CJournalTester(journal).checkTxnExists({entry}));
    }

    // notPaying3It is still in the mempool
    BOOST_ASSERT(testAccess.mapTx().find(entryNotPaying3.GetTxId()) != testAccess.mapTx().end());
    // but not in the journal
    BOOST_ASSERT(!mining::CJournalTester(journal).checkTxnExists({entryNotPaying3}));

    
    // return removed transactions back to mempool
    
    notPaying4It = AddToMempool(entryNotPaying4);
    payFor3And4It = AddToMempool(entryPayingFor3And4); 
    BOOST_ASSERT(notPaying4It->IsInPrimaryMempool());
    BOOST_ASSERT(payFor3And4It->IsInPrimaryMempool());

    // things should be as before removal
    for(auto entry: {entryNotPaying, entryPayForItself, entryPayForGroup, entryNotPaying3, entryNotPaying4, entryPayingFor3And4})
    {
        BOOST_ASSERT(testAccess.mapTx().find(entry.GetTxId()) != testAccess.mapTx().end());
        BOOST_ASSERT(mining::CJournalTester(journal).checkTxnExists({entry}));
    }

    // now remove entryPayForGroup
    entriesToRemove.clear();
    entriesToRemove.insert(payForGroupIt);
    testAccess.removeStagedNL(entriesToRemove, *changeSet, MemPoolRemovalReason::UNKNOWN);

    // everything should be removed from journal
    BOOST_ASSERT(mining::CJournalTester(journal).journalSize() == 0);

    // and nothing should stay in the primary mempool
    for(const auto& entryIt: testAccess.mapTx())
    {
        BOOST_ASSERT(!entryIt.IsInPrimaryMempool());
    }
};

BOOST_AUTO_TEST_SUITE_END()
