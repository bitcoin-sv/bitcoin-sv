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

    size_t freezeEachNthTransaction(const std::vector<CMutableTransaction>& txns, size_t step)
    {
        size_t frozenCount = 0;
        auto& db = CFrozenTXODB::Instance();

        for(size_t i=0; i < txns.size(); i += step)
        {
            assert(txns[i].vin.size() > 1);

            auto result = db.FreezeTXOConsensus(txns[i].vin[1].prevout, {{0}}, false);
            BOOST_CHECK(result == CFrozenTXODB::FreezeTXOResult::OK);
            CFrozenTXODB::FrozenTXOData ftd = CFrozenTXODB::FrozenTXOData::Create_Uninitialized();
            BOOST_CHECK(db.GetFrozenTXOData(txns[i].vin[1].prevout, ftd));
            ++frozenCount;
        }

        return frozenCount;
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
}

BOOST_AUTO_TEST_SUITE_END()
