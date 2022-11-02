// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "mining/journal_change_set.h"
#include "txmempool.h"
#include "frozentxo_db.h"

#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>
#include <vector>

BOOST_FIXTURE_TEST_SUITE(frozentxo, TestingSetup)

namespace
{
    std::vector<CMutableTransaction> generateNRandomTransactions(size_t number)
    {
        std::vector<CMutableTransaction> txns(number);

        for(size_t i = 0; i < number; ++i)
        {
            txns[i].vin.resize(2);
            for (size_t j = 0; j < 2; ++j)
            {
                txns[i].vin[j].scriptSig = CScript() << OP_11;
                txns[i].vin[j].prevout = COutPoint(InsecureRand256(), 1);
            }

            txns[i].vout.reserve(3);
            for (size_t j = 0; j < 3; ++j)
            {
                txns[i].vout.emplace_back(Amount(33000LL), (CScript() << OP_11 << OP_EQUAL));
            }
        }

        return txns;
    }

    void makeConfiscationTransactions(std::vector<CMutableTransaction>& txns)
    {
        for(auto& tx: txns)
        {
            CTxOut out0(
                Amount(0LL),
                // The following script in first output makes this a confiscation transaction with valid contents.
                CScript() << OP_FALSE << OP_RETURN << std::vector<std::uint8_t>{'c', 'f', 't', 'x'} << std::vector<std::uint8_t>{1, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}
            );
            tx.vout.insert(tx.vout.begin(), out0);
            for(auto& txin: tx.vin)
            {
                txin.scriptSig = CScript();
            }
        }
    }

    void writeTransactionsToMemoryPool(
        const std::vector<CMutableTransaction>& txns,
        mining::CJournalChangeSetPtr& journal,
        CTxMemPool& pool)
    {
        TestMemPoolEntryHelper entry;

        for(const auto& tx : txns)
        {
            pool.AddUnchecked(tx.GetId(), entry.FromTx(tx), TxStorage::memory, journal);
        }
    }

    size_t freezeEachNthTransaction(const std::vector<CMutableTransaction>& txns, size_t step, std::size_t input_index=1)
    {
        size_t frozenCount = 0;
        auto& db = CFrozenTXODB::Instance();

        for(size_t i=0; i < txns.size(); i += step)
        {
            assert(txns[i].vin.size() > input_index);

            auto result = db.FreezeTXOConsensus(txns[i].vin[input_index].prevout, {{0}}, false);
            BOOST_CHECK(result == CFrozenTXODB::FreezeTXOResult::OK);
            CFrozenTXODB::FrozenTXOData ftd = CFrozenTXODB::FrozenTXOData::Create_Uninitialized();
            BOOST_CHECK(db.GetFrozenTXOData(txns[i].vin[input_index].prevout, ftd));
            ++frozenCount;
        }

        return frozenCount;
    }

    size_t whitelistEachNthTransaction(const std::vector<CMutableTransaction>& txns, size_t start, size_t step, int eah)
    {
        size_t whitelistedCount = 0;
        auto& db = CFrozenTXODB::Instance();

        for(size_t i=start; i < txns.size(); i += step)
        {
            auto result = db.WhitelistTx(eah, CTransaction(txns[i]));
            BOOST_CHECK(result == CFrozenTXODB::WhitelistTxResult::OK);
            auto whitelistedTxData = CFrozenTXODB::WhitelistedTxData::Create_Uninitialized();
            BOOST_CHECK(db.IsTxWhitelisted(txns[i].GetId(), whitelistedTxData) && whitelistedTxData.enforceAtHeight==eah);
            ++whitelistedCount;
        }

        return whitelistedCount;
    }
}

BOOST_AUTO_TEST_CASE(mempool_RemoveFrozen)
{
    CTxMemPool testPool;
    mining::CJournalChangeSetPtr nullChangeSet{nullptr};
    auto txns = generateNRandomTransactions(100);

    writeTransactionsToMemoryPool(txns, nullChangeSet, testPool);
    BOOST_CHECK_EQUAL(testPool.Size(), txns.size());

    size_t frozenCount = freezeEachNthTransaction(txns, 3);

    testPool.RemoveFrozen(nullChangeSet);

    BOOST_CHECK_EQUAL(testPool.Size(), txns.size() - frozenCount);


    // Check that confiscation transactions are removed from mempool if they are not whitelisted.
    auto ctxns = generateNRandomTransactions(100);
    makeConfiscationTransactions(ctxns);

    auto initialMempoolSize = testPool.Size();
    writeTransactionsToMemoryPool(ctxns, nullChangeSet, testPool);
    BOOST_CHECK_EQUAL(testPool.Size(), initialMempoolSize + ctxns.size());

    freezeEachNthTransaction(ctxns, 3, 0);
    freezeEachNthTransaction(ctxns, 3, 1); // Both inputs are consensus frozen in every 3rd confiscation transaction so that they can be confiscated.
    whitelistEachNthTransaction(ctxns, 0,6, 2); // Every 6th confiscation transaction (starting from 0) is whitelisted at height 2 (which is higher than mempool's height).
    auto num_valid_ctxs=whitelistEachNthTransaction(ctxns, 3,6, 1); // Every 6th confiscation transaction (starting from 3) is whitelisted at height 1 (which is mempool's height).

    testPool.RemoveInvalidCTXs(nullChangeSet);

    // Only every 6th confiscation transaction staring from 3 is whitelisted at mempool's height,
    // which makes it valid and should therefore stay in mempool.
    BOOST_CHECK_EQUAL(testPool.Size(), initialMempoolSize + num_valid_ctxs);
    for(size_t i=3; i<ctxns.size(); i+=6)
    {
        BOOST_CHECK( testPool.Exists(ctxns[i].GetId()) );
    }
}

BOOST_AUTO_TEST_SUITE_END()
