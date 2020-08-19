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

BOOST_AUTO_TEST_CASE(simple_group) 
{
    //          entryNotPaying
    //                 |
    //         entryPayForItself
    //          |            |   
    //   entryPayForGroup   entryNotPaying2
    //
    //
    //
    //  entries in group (entering primary mempool): entryNotPaying, entryPayForItself and entryPayForGroup
    //  entry still in secondary: entryNotPaying2

    auto entryNotPaying = MakeEntry(mempool, CFeeRate(), MakeConfirmedInputs(1, Amount(1000000)), {}, 1);
    
    auto entryPayForItself = MakeEntry(mempool, DefaultFeeRate(), {}, {std::make_tuple(entryNotPaying.GetSharedTx(), 0)}, 2);

    auto entryNotPaying2 = MakeEntry(mempool, CFeeRate(), {}, {std::make_tuple(entryPayForItself.GetSharedTx(), 1)}, 1);
    
    auto sizeSoFar = entryNotPaying.GetSharedTx()->GetTotalSize() + entryPayForItself.GetSharedTx()->GetTotalSize();
    auto feeSoFar = entryNotPaying.GetModifiedFee() + entryPayForItself.GetModifiedFee();
    auto entryPayForGroup = MakeEntry(mempool, DefaultFeeRate(), {}, {std::make_tuple(entryPayForItself.GetSharedTx(), 0)}, 1, sizeSoFar, feeSoFar);

    
    CTxMemPoolTestAccess testAccess(mempool);
    auto journal = testAccess.getJournalBuilder().getCurrentJournal();
    auto& mapTx = testAccess.mapTx();

    auto notPayingIt = AddToMempool(entryNotPaying);
    BOOST_ASSERT(!notPayingIt->IsInPrimaryMempool());

    auto payForItselfIt = AddToMempool(entryPayForItself);
    BOOST_ASSERT(!payForItselfIt->IsInPrimaryMempool());

    auto notPayingIt2 = AddToMempool(entryNotPaying2);
    BOOST_ASSERT(!notPayingIt2->IsInPrimaryMempool());
    
    BOOST_ASSERT(mining::CJournalTester(journal).journalSize() == 0);

    auto payForGroupIt = AddToMempool(entryPayForGroup);
    BOOST_ASSERT(payForGroupIt->IsInPrimaryMempool() && payForGroupIt->IsCPFPGroupMember());
    BOOST_ASSERT(payForItselfIt->IsInPrimaryMempool() && payForItselfIt->IsCPFPGroupMember());
    BOOST_ASSERT(notPayingIt->IsInPrimaryMempool() && notPayingIt->IsCPFPGroupMember());
    BOOST_ASSERT(payForGroupIt->GetCPFPGroup() == payForItselfIt->GetCPFPGroup());
    BOOST_ASSERT(payForGroupIt->GetCPFPGroup() == notPayingIt->GetCPFPGroup());

    BOOST_ASSERT(!notPayingIt2->IsInPrimaryMempool());

    BOOST_ASSERT(mining::CJournalTester(journal).journalSize() != 0);

    BOOST_ASSERT(mining::CJournalTester(journal).checkTxnExists({*notPayingIt}));
    BOOST_ASSERT(mining::CJournalTester(journal).checkTxnExists({*payForItselfIt}));
    BOOST_ASSERT(mining::CJournalTester(journal).checkTxnExists({*payForGroupIt}));

    BOOST_ASSERT(! mining::CJournalTester(journal).checkTxnExists({*notPayingIt2}));

};

BOOST_AUTO_TEST_SUITE_END()
