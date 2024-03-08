// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include <time_locked_mempool.h>
#include <txn_validation_data.h>
#include <validation.h>

#include <test/test_bitcoin.h>

#include <boost/test/unit_test.hpp>

#include <time.h>

namespace MempoolTesting
{
    /**
    * A class to aid testing of the time-locked mempool, so that we don't have to expose lots
    * of testing methods on the mempool itself.
    */
    class CTimeLockedMempoolTester
    {
      public:
        CTimeLockedMempoolTester(CTimeLockedMempool& mempool)
        : mMempool{mempool}
        {}

        // Check if the specified transaction is in the time-locked pool
        bool isInMempool(const CTransactionRef& txn)
        {
            return mMempool.exists(txn->GetId());
        }

        // Check if the specified transaction is in the recently removed list
        bool isRecentlyRemoved(const CTransactionRef& txn)
        {
            return mMempool.recentlyRemoved(txn->GetId());
        }

        // Check if the specified UTXO is tracked in the time-locked pool
        bool isOutpointInMempool(const COutPoint& out)
        {
            std::unique_lock lock { mMempool.mMtx };
            return mMempool.mUTXOMap.find(out) != mMempool.mUTXOMap.end();
        }

        // Get total size of the time-locked pool
        size_t getSize()
        {
            std::unique_lock lock { mMempool.mMtx };
            return mMempool.mTransactionMap.size();
        }

        // Get locked transactions updated by a new one
        std::set<CTransactionRef> getUpdatedTxns(const CTransactionRef& txn)
        {
            std::unique_lock lock { mMempool.mMtx };
            return mMempool.getTransactionsUpdatedByNL(txn);
        }

        // Get memory usage
        size_t getMemUsed()
        {
            std::unique_lock lock { mMempool.mMtx };
            return mMempool.estimateMemoryUsageNL();
        }

      private:
        CTimeLockedMempool& mMempool;
    };
}

namespace
{
    constexpr unsigned NumTxns {3};

    // Create single random transaction for tests
    CMutableTransaction CreateRandomTransaction(unsigned num)
    {
        CMutableTransaction txn {};
        uint32_t now { static_cast<uint32_t>(time(nullptr)) };
        txn.nLockTime = now + num * 60*60;
        txn.vout.resize(1);

        unsigned numInputs { num + 1 };
        txn.vin.resize(numInputs);
        for(unsigned j = 0; j < numInputs; ++j)
        {
            txn.vin[j].nSequence = j;
            txn.vin[j].prevout = { InsecureRand256(), 0 };
        }

        return txn;
    }

    // Create some transactions to use in tests
    std::vector<CMutableTransaction> CreateTransactions()
    {
        std::vector<CMutableTransaction> txns {};

        for(unsigned i = 0; i < NumTxns; ++i)
        {
            txns.emplace_back(CreateRandomTransaction(i));
        }

        return txns;
    }

    // Create us a starting state for a non-final txn
    CValidationState NonFinalState()
    {
        CValidationState state {};
        state.SetNonFinal();
        return state;
    }
}

BOOST_FIXTURE_TEST_SUITE(time_locked_mempool_tests, TestingSetup)

BOOST_AUTO_TEST_CASE(MempoolAddTest)
{
    // Set memory limit
    gArgs.ForceSetArg("-maxmempoolnonfinal", "1");

    // A time-locked mempool to test
    CTimeLockedMempool tlMempool {};
    tlMempool.loadConfig();
    MempoolTesting::CTimeLockedMempoolTester tester { tlMempool };

    // Some time locked transactions
    std::vector<CMutableTransaction> txns { CreateTransactions() };

    // Add transactions and check they are stored correctly
    for(const auto& txn : txns)
    {
        CTransactionRef txnRef { MakeTransactionRef(txn) };
        CValidationState state { NonFinalState() };
        size_t startingMem { tester.getMemUsed() };
        tlMempool.addOrUpdateTransaction({txnRef}, TxInputDataSPtr{}, state);
        BOOST_CHECK(state.IsValid());
        BOOST_CHECK(tester.isInMempool(txnRef));
        BOOST_CHECK(!tester.isRecentlyRemoved(txnRef));
        BOOST_CHECK(tester.getMemUsed() > startingMem);

        for(const auto& input : txnRef->vin)
        {
            BOOST_CHECK(tester.isOutpointInMempool(input.prevout));
        }

        // Also check we can identify updates for transactions we have added
        CMutableTransaction update { txn };
        update.vin[0].nSequence++;
        CTransactionRef updateRef { MakeTransactionRef(update) };
        std::set<CTransactionRef> updated { tester.getUpdatedTxns(updateRef) };
        BOOST_REQUIRE(updated.size() == 1);
        BOOST_CHECK(*(updated.begin()) == txnRef);
    }

    BOOST_CHECK_EQUAL(tester.getSize(), txns.size());
    BOOST_CHECK_EQUAL(tlMempool.getTxnIDs().size(), txns.size());

    // Check max mem limit by attempting to add some large txns
    CValidationState state { NonFinalState() };
    CMutableTransaction large { CreateRandomTransaction(5000) };
    CTransactionRef largeRef { MakeTransactionRef(large) };
    tlMempool.addOrUpdateTransaction({largeRef}, TxInputDataSPtr{}, state);
    BOOST_CHECK(state.IsValid());
    BOOST_CHECK(tester.isInMempool(largeRef));
    BOOST_CHECK(!tester.isRecentlyRemoved(largeRef));
    CTransactionRef oldLargeRef { largeRef };
    large = CreateRandomTransaction(5000);
    largeRef = MakeTransactionRef(large);
    tlMempool.addOrUpdateTransaction({largeRef}, TxInputDataSPtr{}, state);
    BOOST_CHECK(!state.IsValid());
    BOOST_CHECK(state.GetRejectCode() == REJECT_MEMPOOL_FULL);
    BOOST_CHECK(!tester.isInMempool(largeRef));
    BOOST_CHECK(tester.isRecentlyRemoved(largeRef));
    BOOST_CHECK(!tester.isRecentlyRemoved(oldLargeRef));
}

BOOST_AUTO_TEST_CASE(DoubleSpendTest)
{
    // A time-locked mempool to test
    CTimeLockedMempool tlMempool {};
    tlMempool.loadConfig();
    MempoolTesting::CTimeLockedMempoolTester tester { tlMempool };

    // Add some time locked transactions
    std::vector<CMutableTransaction> txns { CreateTransactions() };
    for(const auto& txn : txns)
    {
        CTransactionRef txnRef { MakeTransactionRef(txn) };
        CValidationState state { NonFinalState() };
        tlMempool.addOrUpdateTransaction({txnRef}, TxInputDataSPtr{}, state);
    }

    // Check for double spend of UTXO locked by one of our non-final txns
    CMutableTransaction doubleSpendTxn {};
    doubleSpendTxn.vout.resize(1);    
    doubleSpendTxn.vin.resize(1);
    doubleSpendTxn.vin[0] = txns[1].vin[1];
    CTransactionRef doubleSpendTxnRef { MakeTransactionRef(doubleSpendTxn) };
    BOOST_CHECK(!tlMempool.checkForDoubleSpend(doubleSpendTxnRef).empty());

    // Check for false positive
    CMutableTransaction nonDoubleSpendTxn { CreateRandomTransaction(5) };
    nonDoubleSpendTxn.nLockTime = 0;
    CTransactionRef nonDoubleSpendTxnRef { MakeTransactionRef(nonDoubleSpendTxn) };
    BOOST_CHECK(tlMempool.checkForDoubleSpend(nonDoubleSpendTxnRef).empty());
}

BOOST_AUTO_TEST_CASE(UpdateTest)
{
    // The time locked pool tester
    CTimeLockedMempool tlMempool {};
    tlMempool.loadConfig();
    MempoolTesting::CTimeLockedMempoolTester tester { tlMempool };

    // Build transaction to use in tests
    CMutableTransaction original;
    original.vin.resize(1);
    original.vin[0].nSequence = 1;
    original.vout.resize(1);

    const TxIdTrackerSPtr& pTxIdTracker = std::make_shared<CTxIdTracker>();
    CTransactionRef txnRef { MakeTransactionRef(original) };
    TxInputDataSPtr pTxInputData {
        std::make_shared<CTxInputData>(
            pTxIdTracker,
            txnRef,
            TxSource::unknown,
            TxValidationPriority::high,
            TxStorage::memory,
            GetTime()
        )
    };
    CValidationState state { NonFinalState() };
    tlMempool.addOrUpdateTransaction({txnRef}, pTxInputData, state);

    CTransactionRef lastUpdate {nullptr};

    // Update that decreases nSequence
    {
        CMutableTransaction update { original };
        update.vin[0].nSequence -= 1;
        CTransactionRef originalRef { MakeTransactionRef(original) };
        CTransactionRef updateRef { MakeTransactionRef(update) };
        BOOST_CHECK(!tlMempool.finalisesExistingTransaction(originalRef));
        BOOST_CHECK(!tlMempool.finalisesExistingTransaction(updateRef));
        CValidationState state { NonFinalState() };
        size_t startingMem { tester.getMemUsed() };
        tlMempool.addOrUpdateTransaction({updateRef}, pTxInputData, state);
        BOOST_CHECK_EQUAL(startingMem, tester.getMemUsed());
        BOOST_CHECK(!state.IsValid());
        BOOST_CHECK(!state.IsResubmittedTx());
        BOOST_CHECK(!tester.isInMempool(updateRef));
        BOOST_CHECK(tester.isInMempool(originalRef));
        BOOST_CHECK(!tester.isRecentlyRemoved(updateRef));
        BOOST_CHECK(!tester.isRecentlyRemoved(originalRef));
        lastUpdate = updateRef;
    }

    // Update that doesn't change nSequence
    {
        CMutableTransaction update { original };
        CTransactionRef originalRef { MakeTransactionRef(original) };
        CTransactionRef updateRef { MakeTransactionRef(update) };
        BOOST_CHECK(!tlMempool.finalisesExistingTransaction(originalRef));
        BOOST_CHECK(!tlMempool.finalisesExistingTransaction(updateRef));
        CValidationState state { NonFinalState() };
        size_t startingMem { tester.getMemUsed() };
        tlMempool.addOrUpdateTransaction({updateRef}, pTxInputData, state);
        BOOST_CHECK_EQUAL(startingMem, tester.getMemUsed());
        BOOST_CHECK(!state.IsValid());
        BOOST_CHECK(!state.IsResubmittedTx());
        BOOST_CHECK(tester.isInMempool(originalRef));
        BOOST_CHECK(!tester.isRecentlyRemoved(originalRef));
        BOOST_CHECK(!tester.isRecentlyRemoved(updateRef));
        lastUpdate = updateRef;
    }
    
    // Update that increases nSequence
    {
        CMutableTransaction update { original };
        update.vin[0].nSequence += 1;
        CTransactionRef originalRef { MakeTransactionRef(original) };
        CTransactionRef updateRef { MakeTransactionRef(update) };
        BOOST_CHECK(!tlMempool.finalisesExistingTransaction(originalRef));
        BOOST_CHECK(!tlMempool.finalisesExistingTransaction(updateRef));
        CValidationState state { NonFinalState() };
        size_t startingMem { tester.getMemUsed() };
        tlMempool.addOrUpdateTransaction({updateRef}, pTxInputData, state);
        BOOST_CHECK_EQUAL(startingMem, tester.getMemUsed());
        BOOST_CHECK(state.IsValid());
        BOOST_CHECK(!state.IsResubmittedTx());
        BOOST_CHECK(tester.isInMempool(updateRef));
        BOOST_CHECK(!tester.isInMempool(originalRef));
        BOOST_CHECK(tester.isRecentlyRemoved(originalRef));
        BOOST_CHECK(!tester.isRecentlyRemoved(updateRef));
        lastUpdate = updateRef;
    }

    // Update that finalises nSequence
    {
        CMutableTransaction update { original };
        update.vin[0].nSequence = CTxIn::SEQUENCE_FINAL;
        CTransactionRef originalRef { MakeTransactionRef(original) };
        CTransactionRef updateRef { MakeTransactionRef(update) };
        BOOST_CHECK(!tlMempool.finalisesExistingTransaction(originalRef));
        BOOST_CHECK(tlMempool.finalisesExistingTransaction(updateRef));
        CValidationState state {};
        size_t startingMem { tester.getMemUsed() };
        tlMempool.addOrUpdateTransaction({updateRef}, pTxInputData, state);
        BOOST_CHECK(tester.getMemUsed() < startingMem);
        BOOST_CHECK(state.IsValid());
        BOOST_CHECK(state.IsResubmittedTx());
        BOOST_CHECK(!tester.isInMempool(updateRef));
        BOOST_CHECK(!tester.isInMempool(originalRef));
        BOOST_CHECK(tester.isRecentlyRemoved(originalRef));
        BOOST_CHECK(tester.isRecentlyRemoved(lastUpdate));
        BOOST_CHECK(!tester.isRecentlyRemoved(updateRef));
        lastUpdate = updateRef;
    }
}
 
BOOST_AUTO_TEST_CASE(RateLimitUpdateTest)
{
    // Set update rate limit
    gArgs.ForceSetArg("-mempoolnonfinalmaxreplacementrate", "10");
    gArgs.ForceSetArg("-mempoolnonfinalmaxreplacementrateperiod", "1");

    // The time locked pool tester
    CTimeLockedMempool tlMempool {};
    tlMempool.loadConfig();
    MempoolTesting::CTimeLockedMempoolTester tester { tlMempool };

    // Build transaction to use in tests
    CMutableTransaction original;
    original.vin.resize(1);
    original.vin[0].nSequence = 1;
    original.vout.resize(1);

    const TxIdTrackerSPtr& pTxIdTracker = std::make_shared<CTxIdTracker>();
    CTransactionRef txnRef { MakeTransactionRef(original) };
    TxInputDataSPtr pTxInputData {
        std::make_shared<CTxInputData>(
            pTxIdTracker,
            txnRef,
            TxSource::unknown,
            TxValidationPriority::high,
            TxStorage::memory,
            GetTime()
        )
    };
    CValidationState state { NonFinalState() };
    tlMempool.addOrUpdateTransaction({txnRef}, pTxInputData, state);

    // Check replacement rate tracking while under max rate
    for(int i = 0; i < 10; ++i)
    {
        CMutableTransaction update { original };
        update.vin[0].nSequence += 1;
        CTransactionRef originalRef { MakeTransactionRef(original) };
        CTransactionRef updateRef { MakeTransactionRef(update) };
        state = NonFinalState();
        BOOST_CHECK(tlMempool.checkUpdateWithinRate(updateRef, state));
        BOOST_CHECK(state.IsValid());
        state = NonFinalState();
        tlMempool.addOrUpdateTransaction({updateRef}, pTxInputData, state);
        BOOST_CHECK(state.IsValid());
        BOOST_CHECK(tester.isInMempool(updateRef));
        BOOST_CHECK(!tester.isInMempool(originalRef));
        original = update;
    }

    // Now try exceeding the max replacement rate
    CMutableTransaction update { original };
    update.vin[0].nSequence += 1;
    CTransactionRef originalRef { MakeTransactionRef(original) };
    CTransactionRef updateRef { MakeTransactionRef(update) };
    state = NonFinalState();
    BOOST_CHECK(!tlMempool.checkUpdateWithinRate(updateRef, state));
    BOOST_CHECK(!state.IsValid());
    state = NonFinalState();
    tlMempool.addOrUpdateTransaction({updateRef}, pTxInputData, state);
    BOOST_CHECK(!state.IsValid());
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "non-final-txn-replacement-rate");
    BOOST_CHECK(!tester.isInMempool(updateRef));
    BOOST_CHECK(tester.isInMempool(originalRef));
}

BOOST_AUTO_TEST_SUITE_END()

