// Copyright (c) 2012-2015 The Bitcoin Core developers
// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "config.h"
#include "test/test_bitcoin.h"
#include <boost/test/unit_test.hpp>
#include <validation.h>
#include "test/mempool_test_access.h"

namespace
{
    std::vector<CMutableTransaction> GetMutableTransactions(int howMany, int baseValue)
    {
        TestMemPoolEntryHelper entry;
        std::vector<CMutableTransaction> result;
        for (int i = 0; i < howMany; i++) {
            CMutableTransaction mtx;
            mtx.vin.resize(1);
            mtx.vin[0].scriptSig = CScript() << OP_11;
            mtx.vout.resize(1);
            mtx.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
            mtx.vout[0].nValue = Amount(baseValue + i);
            result.emplace_back(mtx);
        }
        return result;
    }
}

BOOST_FIXTURE_TEST_SUITE(check_selfish_mining_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(CheckSelfishMining)
{
    TestMemPoolEntryHelper entryHelper;
    CTxMemPool testPool;
    testConfig.SetSelfishTxThreshold(10);
    CTxMemPoolTestAccess testAccess(testPool);
    CTxMemPoolTestAccess::setEntries toRemove;
    
    // Put the same 3 txs in 'block' and mempool. 
    auto mutTxs = GetMutableTransactions(3, 11000);
    int64_t lastBlockTxTime = 0;
    for (auto& tx : mutTxs)
    {
        // Set mempool entry time in the past.
        lastBlockTxTime = GetTime() - DEFAULT_MIN_BLOCK_MEMPOOL_TIME_DIFFERENCE_SELFISH;
        entryHelper.Time(lastBlockTxTime);
        // Set default block tx fee.
        entryHelper.Fee(testPool.GetBlockMinTxFee().GetFeePerK());
        CTxMemPoolEntry entry = entryHelper.FromTx(tx);
        testPool.AddUnchecked(entry.GetTxId(), entry, TxStorage::memory, mining::CJournalChangeSetPtr{ nullptr });
        toRemove.insert(testAccess.mapTx().find(entry.GetTxId()));
    }
    BOOST_CHECK(!testPool.CheckSelfishNL(toRemove, lastBlockTxTime, testPool.GetBlockMinTxFee(), testConfig));

    
    // Add another 2 txs only in the mempool (mempool size=5). Mempool entry time is above 
    // DEFAULT_MIN_BLOCK_MEMPOOL_TIME_DIFFERENCE_SELFISH threshold but txs doesn't have not enough block fee.
    mutTxs = GetMutableTransactions(2, 12000);
    for (auto& tx : mutTxs)
    {
        entryHelper.Time(GetTime());
        // Set tx fee under BlockMinTxFee threshold
        entryHelper.Fee(Amount{0});
        CTxMemPoolEntry entry = entryHelper.FromTx(tx);
        testPool.AddUnchecked(entry.GetTxId(), entry, TxStorage::memory, mining::CJournalChangeSetPtr{ nullptr });
    }
    BOOST_CHECK(!testPool.CheckSelfishNL(toRemove, lastBlockTxTime, testPool.GetBlockMinTxFee(), testConfig));
    
    
    // Add another 2 txs only in mempool (mempool size=7). This 2 txs have enough block fee
    // but mempool selfish percentage threshold 50% is not exceeded (2 of 7 is less than 50%).
    testConfig.SetSelfishTxThreshold(50);
    mutTxs = GetMutableTransactions(2, 13000);
    for (auto& tx : mutTxs)
    {
        entryHelper.Time(GetTime());
        entryHelper.Fee(testPool.GetBlockMinTxFee().GetFeePerK());
        CTxMemPoolEntry entry = entryHelper.FromTx(tx);
        testPool.AddUnchecked(entry.GetTxId(), entry, TxStorage::memory, mining::CJournalChangeSetPtr{ nullptr });
    }
    BOOST_CHECK(!testPool.CheckSelfishNL(toRemove, lastBlockTxTime, testPool.GetBlockMinTxFee(), testConfig));

    
    // Lower the selfish percentage threshold to 10% and now 2 of 7 txs is more than 10%.
    testConfig.SetSelfishTxThreshold(10);
    BOOST_CHECK(testPool.CheckSelfishNL(toRemove, lastBlockTxTime, testPool.GetBlockMinTxFee(), testConfig));
    
    
    // Empty block. This is considered selfish mining.
    toRemove.clear();
    lastBlockTxTime = 0;
    BOOST_CHECK(testPool.CheckSelfishNL(toRemove, lastBlockTxTime, testPool.GetBlockMinTxFee(), testConfig));


    // Empty block. Clear mempool and add 2 txs only in mempool (mempool size=2). 
    // Txs doesn't have not enough block fee.
    testPool.Clear();
    mutTxs = GetMutableTransactions(2, 15000);
    for (auto& tx : mutTxs)
    {
        entryHelper.Time(GetTime());
        // Set tx block fee under threshold
        entryHelper.Fee(Amount{0});
        CTxMemPoolEntry entry = entryHelper.FromTx(tx);
        testPool.AddUnchecked(entry.GetTxId(), entry, TxStorage::memory, mining::CJournalChangeSetPtr{ nullptr });
    }
    BOOST_CHECK(!testPool.CheckSelfishNL(toRemove, lastBlockTxTime, testPool.GetBlockMinTxFee(), testConfig));

}

BOOST_AUTO_TEST_SUITE_END()