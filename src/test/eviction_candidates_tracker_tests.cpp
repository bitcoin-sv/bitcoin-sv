// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#include "test/test_bitcoin.h"
#include "mempool_test_access.h"

#include "txmempool.h"
#include "txmempoolevictioncandidates.h"

#include <boost/test/unit_test.hpp>
#include <deque>



CTxMemPoolEntry MakeEntry(
    double feerate, 
    const std::vector<std::tuple<TxId, int, Amount>>& inChainInputs, 
    const std::vector<std::tuple<CTransactionRef, int>>& inMempoolInputs,
    size_t nOutputs, size_t opReturnSize=0)
{
    CMutableTransaction tx;
    Amount totalInput;
    for(const auto& [id, ndx, amount]: inChainInputs)
    {
        tx.vin.push_back(CTxIn(id, ndx, CScript()));
        totalInput += amount;
    }

    for(const auto& [txInput, ndx]: inMempoolInputs)
    {
        tx.vin.push_back(CTxIn(txInput->GetId(), ndx, CScript()));
        totalInput += txInput->vout[ndx].nValue;
    }

    for(size_t i = 0; i < nOutputs; i++)
    {
        CScript script;
        script << OP_TRUE;
        tx.vout.push_back(CTxOut(Amount(), script));
    }

    if(opReturnSize > 0)
    {
        CScript script;
        script << OP_RETURN;
        script << std::vector<uint8_t>(opReturnSize);
        tx.vout.push_back(CTxOut(Amount(), script));
    }
    
    auto txSize = ::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION);
    auto totalFee = Amount(int64_t( feerate * txSize) / nOutputs * nOutputs);
    auto perOutput = (totalInput - totalFee) / int64_t(nOutputs);

    for(auto& output: tx.vout)
    {
        output.nValue = perOutput;
    }

    auto txRef = MakeTransactionRef(tx);
    CTxMemPoolEntry entry {txRef, totalFee, int64_t{0}, 0, false, LockPoints{}};
    return entry;
}



class MempoolMockup
{
public:
    std::optional<CEvictionCandidateTracker> tracker;
    CTxMemPoolTestAccess::txlinksMap links;
    CTxMemPoolTestAccess::Indexed_transaction_set mapTx;

    CTxMemPoolTestAccess::txiter AddTx(CTxMemPoolEntry entry)
    {
        auto[iter, sucess] = mapTx.insert(std::move(entry));
        BOOST_ASSERT(sucess);
        
        CTxMemPoolTestAccess::TxLinks txlinks;
        for(const auto& input: iter->GetSharedTx()->vin)
        {
            auto inputid = input.prevout.GetTxId();
            auto it = mapTx.find(inputid);
            if(it == mapTx.end())
            {
                continue;
            }
            txlinks.parents.insert(it);
            links[it].children.insert(iter);
        }
        
        links.insert(std::make_pair(iter,txlinks));

        if(tracker)
        {
            tracker->EntryAdded(iter);
        }   
        return iter;
    }

    void AddGroup(std::vector<CTxMemPoolEntry> entries)
    {
        SecondaryMempoolEntryData groupData;
        std::vector<CTxMemPoolTestAccess::txiter> iters;
        CTxMemPoolTestAccess::txiter lastIt;
        for(auto& entry: entries)
        {
            groupData.fee += entry.GetFee();
            groupData.size += entry.GetTxSize();
            auto it = AddTx(entry);
            iters.push_back(it);   
            lastIt = it;
        }
        
        auto group = std::make_shared<CPFPGroup>(groupData, std::vector<CTxMemPoolTestAccess::txiter>(iters));
        for(auto iter: iters)
        {
            mapTx.modify(iter, [&group](CTxMemPoolEntry& entry) {
                                    CTestTxMemPoolEntry(entry).group() = group;
                                    CTestTxMemPoolEntry(entry).groupingData() = std::nullopt;
                                });
        }
        
        
        if(tracker)
        {
            tracker->EntryModified(lastIt);
        }
    }
        
    void RemoveTx(CTxMemPoolTestAccess::txiter entry)
    {
        auto linksIter = links.find(entry);
        
        assert(linksIter != links.end());
        assert(linksIter->second.children.empty());

        auto parents = linksIter->second.parents;
        auto txId = entry->GetTxId();

        if(entry->IsCPFPGroupMember())
        {
            for(auto it: entry->GetCPFPGroup()->Transactions())
            {
                CTestTxMemPoolEntry(const_cast<CTxMemPoolEntry&>(*it)).group().reset();
            }
        }

        for(const auto& parent: linksIter->second.parents)
        {
            auto parentLinksIter = links.find(parent);
            if(parentLinksIter == links.end())
            {
                continue;
            }
            parentLinksIter->second.children.erase(entry);
        }

        links.erase(linksIter);
        mapTx.erase(entry);

        if(tracker)
        {
            tracker->EntryRemoved(txId, parents);
        }
    }
    
    void InitializeTracker()
    {
        tracker =
            CEvictionCandidateTracker(
                links, 
                [](CTxMemPoolTestAccess::txiter entry)
                {
                    int64_t score = entry->GetFee().GetSatoshis() * 100000 / entry->GetTxSize();
                    if(!entry->IsInPrimaryMempool())
                    {
                        score += std::numeric_limits<int64_t>::min();
                    }
                    return score;
                });
    }

    
    void RemoveMostWorthless()
    {
        auto iter = tracker->GetMostWorthless();
        RemoveTx(iter);
    }
};

BOOST_FIXTURE_TEST_SUITE(eviction_candidates_tracker_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(single_long_chain) {
    MempoolMockup mempool;
    auto confirmedEntry = std::make_tuple<TxId, int, Amount>(TxId(), 0, Amount(10000000));
    auto entry = MakeEntry(1,{confirmedEntry}, {}, 1, 0);
    mempool.AddTx(entry);
    
    
    for(int n = 0; n < 2; n++)
    {
        auto oldEntry = entry;
        std::deque<uint256> addedTransactions;
        for(int i = 0; i < 100; i++)
        {
            auto newEntry = MakeEntry(1, {}, { std::make_tuple<CTransactionRef, int>(oldEntry.GetSharedTx(), 0)}, 1, 0);
            mempool.AddTx(newEntry);
            addedTransactions.push_back(newEntry.GetSharedTx()->GetId());
            oldEntry = newEntry;
            if(mempool.tracker)
            {
                BOOST_ASSERT(mempool.tracker->GetAllCandidates().size() == 1);
            }
        }

        if(!mempool.tracker)
        {
            mempool.InitializeTracker();
        }
        
        std::deque<uint256> removedTransactions;
        for(int i = 0; i < 100; i++)
        {
            auto txToRemove = mempool.tracker->GetMostWorthless();
            auto txId = txToRemove->GetSharedTx()->GetId();
            removedTransactions.push_front(txId);
            mempool.RemoveTx(txToRemove);
            BOOST_ASSERT(mempool.tracker->GetAllCandidates().size() == 1);
        }

        BOOST_ASSERT(addedTransactions == removedTransactions);
    }


}

BOOST_AUTO_TEST_CASE(broad_tree) {
    MempoolMockup mempool;
    auto confirmedEntry = std::make_tuple<TxId, int, Amount>(TxId(), 0, Amount(100000000));
    auto entry = MakeEntry(1,{confirmedEntry}, {}, 100, 0);
    mempool.AddTx(entry);

    for(int n = 0; n < 2; n++)
    {
        for(size_t i = 0; i < 100; i++)
        {
            auto feerate = 100 + ((i % 2 == 0) ? (i * 0.1) : (i * -0.1)); 
            auto newEntry = MakeEntry(feerate, {}, { std::make_tuple<CTransactionRef, int>(entry.GetSharedTx(), std::move(i))}, 1, 0);
            mempool.AddTx(newEntry);
            if(mempool.tracker)
            {
                BOOST_ASSERT(mempool.tracker->GetAllCandidates().size() == (i + 1));
            }
        }

        if(!mempool.tracker)
        {
            mempool.InitializeTracker();
        }

        double lastRemovedFeeRate = 0;
        for(size_t i = 0; i < 100; i++)
        {
            BOOST_ASSERT(mempool.tracker->GetAllCandidates().size() == (100-i));
            auto txToRemove = mempool.tracker->GetMostWorthless();
            double feeRate = double(txToRemove->GetFee().GetSatoshis()) / txToRemove->GetTxSize();
            mempool.RemoveTx(txToRemove);
            BOOST_ASSERT(feeRate >= lastRemovedFeeRate);
            lastRemovedFeeRate = feeRate;
        }
    }
}



BOOST_AUTO_TEST_CASE(secondary_mempool_first) {
    MempoolMockup mempool;
    auto confirmedEntry = std::make_tuple<TxId, int, Amount>(TxId(), 0, Amount(10000000));
    auto entry = MakeEntry(1,{confirmedEntry}, {}, 100, 0);
    mempool.AddTx(entry);

    for(int i = 0; i < 100; i++)
    {    
        auto newEntry = MakeEntry(100 + (i * 0.1), {}, { std::make_tuple<CTransactionRef, int>(entry.GetSharedTx(), std::move(i))}, 1, 0);
        if(i % 2 == 0)
        {
            CTestTxMemPoolEntry(newEntry).groupingData() =
                SecondaryMempoolEntryData{newEntry.GetFee(), Amount(), newEntry.GetTxSize(), 0};
        }
        mempool.AddTx(newEntry);
    }

    mempool.InitializeTracker();

    double lastRemovedFeeRate = 0;
    bool lastFromSecondary = true;
    for(int i = 0; i < 100; i++)
    {
        auto txToRemove = mempool.tracker->GetMostWorthless();
        double feeRate = double(txToRemove->GetFee().GetSatoshis()) / txToRemove->GetTxSize();
        bool fromSecondary = !txToRemove->IsInPrimaryMempool();
        mempool.RemoveMostWorthless();
        if(fromSecondary)
        {
            BOOST_ASSERT(lastFromSecondary);
            BOOST_ASSERT(feeRate >= lastRemovedFeeRate);
        }
        else
        {
            if(lastFromSecondary)
            {
                // txToRemove is from primary but transaction from last round it is from secondary
                // transaction from last round it is evicted first despite of higher fee
                BOOST_ASSERT(feeRate < lastRemovedFeeRate);
            }
            else
            {
                // txToRemove is from primary as well as tx from last round
                BOOST_ASSERT(feeRate >= lastRemovedFeeRate);
            }
        }
        lastFromSecondary = fromSecondary;
        lastRemovedFeeRate = feeRate;
    }
}



BOOST_AUTO_TEST_CASE(group) {
    MempoolMockup mempool;
    std::vector<std::tuple<TxId, int, Amount>> confirmedInputs = {
        std::make_tuple<TxId, int, Amount>(TxId(), 0, Amount(10000000)),
        std::make_tuple<TxId, int, Amount>(TxId(), 1, Amount(10000000)),
        std::make_tuple<TxId, int, Amount>(TxId(), 2, Amount(10000000)),
        std::make_tuple<TxId, int, Amount>(TxId(), 3, Amount(10000000))
    };
    
    std::vector<CTxMemPoolEntry> group;
    std::vector<std::tuple<CTransactionRef, int>> inMempoolInputs;
    for(int i = 0; i < 4; i++)
    {
        group.push_back(MakeEntry(0,{confirmedInputs[i]}, {}, 2, 1000));
        inMempoolInputs.push_back(std::make_tuple(group.back().GetSharedTx(), 0));
    }
    group.push_back(MakeEntry(10,{}, inMempoolInputs, 1, 1000));
    mempool.AddGroup(group);
    mempool.InitializeTracker();
    BOOST_ASSERT(mempool.tracker->GetAllCandidates().size() == 1);
    mempool.AddTx(MakeEntry(1000, {}, {std::make_tuple(group[0].GetSharedTx(), 1)}, 1, 1000));
    BOOST_ASSERT(mempool.tracker->GetAllCandidates().size() == 1);
    mempool.RemoveMostWorthless();
    mempool.RemoveMostWorthless();
    BOOST_ASSERT(mempool.tracker->GetAllCandidates().size() == 4);

}


BOOST_AUTO_TEST_CASE(performance, * boost::unit_test::disabled()) {
    MempoolMockup mempool;
    
    // a tree of 1 million transactions with a single tx at root

    constexpr size_t NUM_OF_TX = 1000000; 
    constexpr size_t INPUTS_PER_TX = 1;
    constexpr size_t OUTPUTS_PER_TX = 2;

    std::deque<std::tuple<CTransactionRef, int>> inputs;
    auto input = std::make_tuple<TxId, int, Amount>(TxId(), 0, Amount(10000000000));
    auto rootTx = MakeEntry(0, {input}, {}, OUTPUTS_PER_TX, 300);
    mempool.AddTx(rootTx);

    for(size_t i = 0; i < OUTPUTS_PER_TX; i++)
    {
        inputs.push_back(std::make_tuple(rootTx.GetSharedTx(), static_cast<int>(i)));
    }
    std::vector<std::tuple<CTransactionRef, int>> inMempoolInputs;
    for(size_t n = 1; n < NUM_OF_TX; n++)
    {
        inMempoolInputs.clear();
        for(size_t inp = 0; inp < INPUTS_PER_TX; inp++)
        {
            inMempoolInputs.emplace_back(inputs.front());
            inputs.pop_front();
        }
        auto tx = MakeEntry(double(insecure_rand()) / double(RAND_MAX) + 0.5, {}, inMempoolInputs, OUTPUTS_PER_TX, 1);
        for(size_t outp = 0; outp < OUTPUTS_PER_TX; outp++)
        {
            inputs.emplace_back(tx.GetSharedTx(), static_cast<int>(outp));
        }
        mempool.AddTx(tx);
    }
    
    //auto t0 = std::chrono::high_resolution_clock::now();
    mempool.InitializeTracker();
    //auto t1 = std::chrono::high_resolution_clock::now();
    for(size_t i = 0; i < NUM_OF_TX; i++)
    {
        mempool.RemoveMostWorthless();
    }
    //auto t2 = std::chrono::high_resolution_clock::now();

    //std::cout << "InitializeTracker   " << std::chrono::duration_cast<std::chrono::milliseconds>(t1-t0).count() << "ms" << std::endl;
    //std::cout << "RemoveMostWorthless " << std::chrono::duration_cast<std::chrono::milliseconds>(t2-t1).count() << "ms" << std::endl;
}


BOOST_AUTO_TEST_SUITE_END()
