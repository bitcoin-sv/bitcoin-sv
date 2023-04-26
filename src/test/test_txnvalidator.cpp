// Copyright (c) 2019 The Bitcoin SV developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "config.h"
#include "key.h"
#include "pubkey.h"
#include "test/test_bitcoin.h"
#include "txmempool.h"
#include "txn_validator.h"
#include "test/mempool_test_access.h"

#include <algorithm>
#include <boost/test/unit_test.hpp>

namespace {
    std::vector<TxSource> vTxSources {
        TxSource::wallet,
        TxSource::rpc,
        TxSource::file,
        TxSource::p2p,
        TxSource::reorg,
        TxSource::unknown,
        TxSource::finalised
    };
    // An exception thrown by GetValueOut .
    bool GetValueOutException(const std::runtime_error &e) {
        static std::string expectedException("GetValueOut: value out of range");
        return expectedException == e.what();
    }
    // Support for P2P node.
    CService ip(uint32_t i) {
        struct in_addr s;
        s.s_addr = i;
        return CService(CNetAddr(s), Params().GetDefaultPort());
    }
    // Create scriptPubKey from a given key
    CScript GetScriptPubKey(CKey& key) {
        CScript scriptPubKey = CScript() << ToByteVector(key.GetPubKey())
                                         << OP_CHECKSIG;
        return scriptPubKey;
    }
    // Create a transaction that spends first nInputs of the funding transaction
    // and creates nOutputs (and pays a fixed fee of 1 cent)
    CMutableTransaction CreateManyToManyTx(const int nInputs, const int nOutputs,
                                           const CTransaction& fundTxn,
                                           CKey& key,
                                           CScript& scriptPubKey) {
        static uint32_t dummyLockTime = 0;
        CMutableTransaction spend_txn;
        spend_txn.nVersion = 1;
        spend_txn.nLockTime = ++dummyLockTime;
        BOOST_REQUIRE_LE(static_cast<size_t>(nInputs), fundTxn.vout.size());
        spend_txn.vin.resize(nInputs);
        auto funds = Amount { 0 };
        for (int input = 0; input < nInputs; ++input) {
            spend_txn.vin[input].prevout = COutPoint(fundTxn.GetId(), input);
            funds += fundTxn.vout[input].nValue;
        }
        funds -= CENT;
        spend_txn.vout.resize(nOutputs);
        for (int output = 0; output < nOutputs; ++output) {
            spend_txn.vout[output].nValue = funds / nOutputs;
            spend_txn.vout[output].scriptPubKey = scriptPubKey;
        }
        // Sign:
        for (int input = 0; input < nInputs; ++input) {
            std::vector<uint8_t> vchSig {};
            uint256 hash = SignatureHash(scriptPubKey, CTransaction(spend_txn), input,
                                         SigHashType().withForkId(),
                                         fundTxn.vout[input].nValue);
            BOOST_CHECK(key.Sign(hash, vchSig));
            vchSig.push_back(uint8_t(SIGHASH_ALL | SIGHASH_FORKID));
            spend_txn.vin[input].scriptSig << vchSig;
        }
        return spend_txn;
    }
    // Create a double spend txn
    CMutableTransaction CreateDoubleSpendTxn(const CTransaction& fundTxn,
                                       CKey& key,
                                       CScript& scriptPubKey) {
        return CreateManyToManyTx(1, 1, fundTxn, key, scriptPubKey);
    }
    // Make N unique large (but rubbish) transactions
    std::vector<CMutableTransaction> MakeNLargeTxns(size_t nNumTxns,
                                                    CTransaction& fundTxn,
                                                    CScript& scriptPubKey) {
        std::vector<CMutableTransaction> res {};
        for (size_t i=0; i<nNumTxns; i++) {
            CMutableTransaction txn;
            txn.nVersion = 1;
            txn.vin.resize(1);
            txn.vin[0].prevout = COutPoint(fundTxn.GetId(), i);
            txn.vout.resize(1000);
            for(size_t j=0; j<1000; ++j) {
                txn.vout[j].nValue = 11 * CENT;
                txn.vout[j].scriptPubKey = scriptPubKey;
            }
            res.emplace_back(txn);
        }
        return res;
    }
    // Create N double spend txns from the given fund txn
    std::vector<CMutableTransaction> CreateNDoubleSpendTxns(size_t nSpendTxns,
                                                      CTransaction& fundTxn,
                                                      CKey& key,
                                                      CScript& scriptPubKey) {
        // Create txns spending the same coinbase txn.
        std::vector<CMutableTransaction> spends {};
        for (size_t i=0; i<nSpendTxns; i++) {
            spends.emplace_back(CreateDoubleSpendTxn(fundTxn, key, scriptPubKey));
        };
        return spends;
    }

    // Create txn input data for a given txn and source
    TxInputDataSPtr TxInputData(TxSource source,
                                CMutableTransaction& spend,
                                std::shared_ptr<CNode> pNode = nullptr,
                                TxValidationPriority priority = TxValidationPriority::normal) {
        // Return txn's input data
        return std::make_shared<CTxInputData>(
                   g_connman->GetTxIdTracker(), // a pointer to the TxIdTracker
                   MakeTransactionRef(spend),// a pointer to the tx
                   source,   // tx source
                   priority, // tx validation priority
                   TxStorage::memory, // tx storage
                   GetTime(),// nAcceptTime
                   Amount(0), // nAbsurdFee
                   pNode);   // pNode
    }
    // Create a vector with input data for a given txn and source
    std::vector<TxInputDataSPtr> TxInputDataVec(TxSource source,
                                                const std::vector<CMutableTransaction>& spends,
                                                std::shared_ptr<CNode> pNode = nullptr,
                                                TxValidationPriority priority = TxValidationPriority::normal) {
        std::vector<TxInputDataSPtr> vTxInputData {};
        // Get a pointer to the TxIdTracker.
        const TxIdTrackerSPtr& pTxIdTracker = g_connman->GetTxIdTracker();
        for (auto& elem : spends) {
            vTxInputData.
                emplace_back(
                    std::make_shared<CTxInputData>(
                        pTxIdTracker, // a pointer to the TxIdTracker
                        MakeTransactionRef(elem),  // a pointer to the tx
                        source,   // tx source
                        priority, // tx validation priority
                        TxStorage::memory, // tx storage
                        GetTime(),// nAcceptTime
                        Amount(0), // nAbsurdFee
                        pNode));   // pNode
        }
        return vTxInputData;
    }
    // Validate txn using asynchronous validation interface
    void ProcessTxnsAsynchApi(
        const Config& config,
        CTxMemPool& pool,
        std::vector<CMutableTransaction>& spends,
        TxSource source,
        std::shared_ptr<CNode> pNode = nullptr) {

        // Create txn validator
        std::shared_ptr<CTxnValidator> txnValidator {
            std::make_shared<CTxnValidator>(
                    config,
                    pool,
                    std::make_shared<CTxnDoubleSpendDetector>(),
                    g_connman->GetTxIdTracker())
        };
        // Clear mempool before validation
        pool.Clear();
        // Schedule txns for processing.
        txnValidator->newTransaction(TxInputDataVec(source, spends, pNode));
        // Wait for the Validator to process all queued txns.
        txnValidator->waitForEmptyQueue();
    }
    // Validate a single txn using synchronous validation interface
    CValidationState ProcessTxnSynchApi(
        const Config& config,
        CTxMemPool& pool,
        CMutableTransaction& spend,
        TxSource source,
        std::shared_ptr<CNode> pNode = nullptr) {

        // Create txn validator
        std::shared_ptr<CTxnValidator> txnValidator {
            std::make_shared<CTxnValidator>(
                    config,
                    pool,
                    std::make_shared<CTxnDoubleSpendDetector>(),
                    g_connman->GetTxIdTracker())
        };
        // Clear mempool before validation
        pool.Clear();
        // Mempool Journal ChangeSet
        mining::CJournalChangeSetPtr changeSet {nullptr};
        return txnValidator->processValidation(TxInputData(source, spend, pNode), changeSet);
    }
    // Validate txn using synchronous validation interface
    void ProcessTxnsSynchApi(
        const Config& config,
        CTxMemPool& pool,
        std::vector<CMutableTransaction>& spends,
        TxSource source,
        std::shared_ptr<CNode> pNode = nullptr) {

        // Create txn validator
        std::shared_ptr<CTxnValidator> txnValidator {
            std::make_shared<CTxnValidator>(
                    config,
                    pool,
                    std::make_shared<CTxnDoubleSpendDetector>(),
                    g_connman->GetTxIdTracker())
        };
        // Clear mempool before validation
        pool.Clear();
        // Mempool Journal ChangeSet
        mining::CJournalChangeSetPtr changeSet {nullptr};
        // Validate the first txn
        CValidationState result {
            txnValidator->processValidation(TxInputData(source, spends[0], pNode), changeSet)
        };
        BOOST_CHECK(result.IsValid());
        // Validate the second txn
        // spends1 should be rejected if spend0 is in the mempool
        result = txnValidator->processValidation(TxInputData(source, spends[1], pNode), changeSet);
        BOOST_CHECK(!result.IsValid());
    }
    // Validate txns using synchronous batch validation interface
    CTxnValidator::RejectedTxns ProcessTxnsSynchBatchApi(
        const Config& config,
        CTxMemPool& pool,
        std::vector<CMutableTransaction>& spends,
        TxSource source,
        std::shared_ptr<CNode> pNode = nullptr) {

        // Create txn validator
        std::shared_ptr<CTxnValidator> txnValidator {
            std::make_shared<CTxnValidator>(
                    config,
                    pool,
                    std::make_shared<CTxnDoubleSpendDetector>(),
                    g_connman->GetTxIdTracker())
        };
        // Clear mempool before validation
        pool.Clear();
        // Mempool Journal ChangeSet
        mining::CJournalChangeSetPtr changeSet {nullptr};
        // Validate the first txn
        return txnValidator->processValidation(TxInputDataVec(source, spends, pNode), changeSet);
    }

    CNodePtr DummyNode(ConfigInit& testConfig)
    {
        // Create a dummy address
        CAddress dummy_addr(ip(0xa0b0c001), NODE_NONE);
        CConnman::CAsyncTaskPool asyncTaskPool{testConfig};
        return CNode::Make(
                0,
                NODE_NETWORK,
                0,
                INVALID_SOCKET,
                dummy_addr,
                0u,
                0u,
                asyncTaskPool,
                "",
                true);
    }
    struct TestChain100Setup2 : TestChain100Setup {
        CScript scriptPubKey {
            GetScriptPubKey(coinbaseKey)
        };
        // twoDoubleSpend2Txns contains two txns spending the same coinbase txn
        std::vector<CMutableTransaction> doubleSpend2Txns {
            CreateDoubleSpendTxn(coinbaseTxns[0], coinbaseKey, scriptPubKey),
            CreateDoubleSpendTxn(coinbaseTxns[0], coinbaseKey, scriptPubKey)
        };
        // doubleSpend10Txns contains 10 double spend txns spending the same coinbase txn
        std::vector<CMutableTransaction> doubleSpend10Txns {
            CreateNDoubleSpendTxns(10, coinbaseTxns[0], coinbaseKey, scriptPubKey)
        };    
    };
}

BOOST_FIXTURE_TEST_SUITE(test_txnvalidator, TestChain100Setup2)

BOOST_AUTO_TEST_CASE(txn_validator_creation) {
    CTxMemPool pool;
    // Create txn validator
    std::shared_ptr<CTxnValidator> txnValidator {
        std::make_shared<CTxnValidator>(
                testConfig,
                pool,
                std::make_shared<CTxnDoubleSpendDetector>(),
                g_connman->GetTxIdTracker())
    };
    // Check if the Validator was created
    BOOST_REQUIRE(txnValidator);
    // Check if orphan txns buffer was created
    BOOST_REQUIRE(txnValidator->getOrphanTxnsPtr());
    // Check if txn recent rejects buffer was created
    BOOST_REQUIRE(txnValidator->getTxnRecentRejectsPtr());
}

BOOST_AUTO_TEST_CASE(txn_validator_set_get_frequency) {
    CTxMemPool pool;
    // Create txn validator
    std::shared_ptr<CTxnValidator> txnValidator {
        std::make_shared<CTxnValidator>(
                testConfig,
                pool,
                std::make_shared<CTxnDoubleSpendDetector>(),
                g_connman->GetTxIdTracker())
    };
    auto defaultfreq = std::chrono::milliseconds(CTxnValidator::DEFAULT_ASYNCH_RUN_FREQUENCY_MILLIS);
    BOOST_CHECK(defaultfreq == txnValidator->getRunFrequency());
    txnValidator->setRunFrequency(++defaultfreq);
    BOOST_CHECK(defaultfreq == txnValidator->getRunFrequency());
}

BOOST_AUTO_TEST_CASE(txn_validator_istxnknown) {
    CTxMemPool pool;
    // Create txn validator
    std::shared_ptr<CTxnValidator> txnValidator {
        std::make_shared<CTxnValidator>(
                testConfig,
                pool,
                std::make_shared<CTxnDoubleSpendDetector>(),
                g_connman->GetTxIdTracker())
    };
    // Schedule txns for processing.
    txnValidator->newTransaction(TxInputDataVec(TxSource::p2p, doubleSpend10Txns));
    BOOST_CHECK(txnValidator->isTxnKnown(doubleSpend10Txns[0].GetId()));
    // Wait for the Validator to process all queued txns.
    txnValidator->waitForEmptyQueue();
    BOOST_CHECK(!txnValidator->isTxnKnown(doubleSpend10Txns[0].GetId()));
}

BOOST_AUTO_TEST_CASE(double_spend_detector)
{
    CTxMemPool pool;
    CTxnDoubleSpendDetector detector;
    std::vector<CMutableTransaction> txns{ MakeNLargeTxns(5, coinbaseTxns[0], scriptPubKey) };
    std::size_t parentOfDoubleSpendIdx = 1;
    // replace the transaction that spends txns[parentOfDoubleSpendIdx] so that we can crate a double spend later
    txns[2] = CreateDoubleSpendTxn(CTransaction{txns[parentOfDoubleSpendIdx]}, coinbaseKey, scriptPubKey);
    auto txnsData = TxInputDataVec(TxSource::p2p, txns);

    for(const auto& data : txnsData)
    {
        CValidationState state;
        BOOST_CHECK(detector.insertTxnInputs(data->GetTxnPtr(), pool, state, true) == true);

        BOOST_CHECK(state.IsDoubleSpendDetected() == false);
        BOOST_CHECK(state.IsMempoolConflictDetected() == false);
        BOOST_CHECK(state.GetCollidedWithTx().empty());
    }

    std::size_t doubleSpendIdx = 2;
    auto doubleSpendTx =
        std::make_shared<CTransaction>(
            CreateDoubleSpendTxn(CTransaction{txns[parentOfDoubleSpendIdx]}, coinbaseKey, scriptPubKey) );
    auto doubleSpendData =
        std::make_shared<CTxInputData>(
            g_connman->GetTxIdTracker(),
            doubleSpendTx,
            TxSource::p2p,
            TxValidationPriority::normal,
            TxStorage::memory,
            GetTime(),// nAcceptTime
            Amount(0), // nAbsurdFee
            std::weak_ptr<CNode>{});
    auto& primaryTx = *txnsData[doubleSpendIdx]->GetTxnPtr();

    // Double spend should be detected
    {
        CValidationState state;
        BOOST_CHECK(
            detector.insertTxnInputs(txnsData[doubleSpendIdx], pool, state, true)
            == false);
        BOOST_CHECK(state.IsDoubleSpendDetected() == true);
        BOOST_CHECK(state.IsMempoolConflictDetected() == false);
        BOOST_CHECK_EQUAL(state.GetCollidedWithTx().size(), 1U);
        BOOST_CHECK_EQUAL((*state.GetCollidedWithTx().begin())->GetId().ToString(), primaryTx.GetId().ToString());
    }

    // Trying to remove the tx with different address doesn't change anything
    {
        detector.removeTxnInputs( CTransaction{txns[doubleSpendIdx]} );
        CValidationState state;
        BOOST_CHECK(
            detector.insertTxnInputs(doubleSpendData, pool, state, true)
            == false);
        BOOST_CHECK(state.IsDoubleSpendDetected() == true);
        BOOST_CHECK(state.IsMempoolConflictDetected() == false);
        BOOST_CHECK_EQUAL(state.GetCollidedWithTx().size(), 1U);
        BOOST_CHECK_EQUAL((*state.GetCollidedWithTx().begin())->GetId().ToString(), primaryTx.GetId().ToString());
    }

    // Trying to remove the double spend tx doesn't change anything
    {
        detector.removeTxnInputs(*doubleSpendTx);
        CValidationState state;
        BOOST_CHECK(
            detector.insertTxnInputs(doubleSpendData, pool, state, true)
            == false);
        BOOST_CHECK(state.IsDoubleSpendDetected() == true);
        BOOST_CHECK(state.IsMempoolConflictDetected() == false);
        BOOST_CHECK_EQUAL(state.GetCollidedWithTx().size(), 1U);
        BOOST_CHECK_EQUAL((*state.GetCollidedWithTx().begin())->GetId().ToString(), primaryTx.GetId().ToString());
    }

    // Remove the first tx that caused double spend spend transaction so that we
    // can add double spend tx without error
    {
        detector.removeTxnInputs(*txnsData[doubleSpendIdx]->GetTxnPtr());
        CValidationState state;
        BOOST_CHECK(detector.insertTxnInputs(doubleSpendData, pool, state, true) == true);

        BOOST_CHECK(state.IsDoubleSpendDetected() == false);
        BOOST_CHECK(state.IsMempoolConflictDetected() == false);
        BOOST_CHECK(state.GetCollidedWithTx().empty());
    }

    // Remove the double spend transaction, add initial tx to mempool and make
    // sure that a mempool collision is detected when adding the double spend tx
    // to the detector
    {
        detector.removeTxnInputs(*doubleSpendTx);

        Amount fee{ 3 };
        int64_t time = 0;
        int32_t height = 1;
        bool spendsCoinbase = false;
        LockPoints lp;
        mining::CJournalChangeSetPtr nullChangeSet{nullptr};
        auto& tx = *txnsData[doubleSpendIdx]->GetTxnPtr();
        pool.AddUnchecked(
            tx.GetId(),
            CTxMemPoolEntry{
                txnsData[doubleSpendIdx]->GetTxnPtr(),
                fee,
                time,
                height,
                spendsCoinbase,
                lp},
            TxStorage::memory,
            nullChangeSet);

        CValidationState state;
        BOOST_CHECK(
            detector.insertTxnInputs(doubleSpendData, pool, state, true)
            == false);
        BOOST_CHECK(state.IsDoubleSpendDetected() == false);
        BOOST_CHECK(state.IsMempoolConflictDetected() == true);
        BOOST_CHECK_EQUAL(state.GetCollidedWithTx().size(), 1U);
        BOOST_CHECK_EQUAL((*state.GetCollidedWithTx().begin())->GetId().ToString(), primaryTx.GetId().ToString());
    }
}

BOOST_AUTO_TEST_CASE(validation_state_collided_with_tx)
{
    std::vector<CMutableTransaction> txns{ MakeNLargeTxns(7, coinbaseTxns[0], scriptPubKey) };
    CValidationState state;

    auto all_present =
        [](
            const std::set<CTransactionRef>& transactions,
            const std::vector<CTransactionRef>& expected)
        {
            BOOST_CHECK_EQUAL(transactions.size(), expected.size());
            BOOST_CHECK(
                std::all_of(
                    expected.begin(),
                    expected.end(),
                    [&transactions](const CTransactionRef& item)
                    {
                        return transactions.find(item) != transactions.end();
                    }));
        };

    BOOST_CHECK(state.IsDoubleSpendDetected() == false);
    BOOST_CHECK(state.IsMempoolConflictDetected() == false);
    BOOST_CHECK_EQUAL(state.GetCollidedWithTx().size(), 0U);

    std::vector<CTransactionRef> added;

    // add two transactions as double spends
    added.push_back(MakeTransactionRef(txns[0]));
    added.push_back(MakeTransactionRef(txns[1]));
    state.SetDoubleSpendDetected({added[0], added[1]});
    BOOST_CHECK(state.IsDoubleSpendDetected() == true);
    BOOST_CHECK(state.IsMempoolConflictDetected() == false);
    all_present( state.GetCollidedWithTx(), added );

    // we can call SetDoubleSpendDetected() multiple times but duplicates won't be added
    added.push_back(MakeTransactionRef(txns[2]));
    state.SetDoubleSpendDetected({added[1], added[2]});
    BOOST_CHECK(state.IsDoubleSpendDetected() == true);
    BOOST_CHECK(state.IsMempoolConflictDetected() == false);
    all_present( state.GetCollidedWithTx(), added );

    // add two transactions as mempool conflicts
    added.push_back(MakeTransactionRef(txns[3]));
    added.push_back(MakeTransactionRef(txns[4]));
    state.SetMempoolConflictDetected({added[3], added[4]});
    BOOST_CHECK(state.IsDoubleSpendDetected() == true);
    BOOST_CHECK(state.IsMempoolConflictDetected() == true);
    all_present( state.GetCollidedWithTx(), added );

    // we can call SetMempoolConflictDetected() multiple times but duplicates won't be added
    added.push_back(MakeTransactionRef(txns[5]));
    state.SetMempoolConflictDetected({added[4], added[5]});
    BOOST_CHECK(state.IsDoubleSpendDetected() == true);
    BOOST_CHECK(state.IsMempoolConflictDetected() == true);
    all_present( state.GetCollidedWithTx(), added );

    // clear the collided with container
    state.ClearCollidedWithTx();
    BOOST_CHECK(state.IsDoubleSpendDetected() == true);
    BOOST_CHECK(state.IsMempoolConflictDetected() == true);
    BOOST_CHECK_EQUAL(state.GetCollidedWithTx().size(), 0U);
}

/**
 * TxnValidator: Test synch interface.
 */
BOOST_AUTO_TEST_CASE(txnvalidator_doublespend_synch_api) {
    CTxMemPool pool;
    // Test all sources.
    for (const auto& txsource: vTxSources) {
        ProcessTxnsSynchApi(testConfig, pool, doubleSpend2Txns, txsource);
        BOOST_CHECK_EQUAL(pool.Size(), 1U);
    }
    // Test: Txns from p2p with a pointer to a dummy node.
    {
        // Create a dummy address
        CAddress dummy_addr(ip(0xa0b0c001), NODE_NONE);
        CConnman::CAsyncTaskPool asyncTaskPool{testConfig};
        CNodePtr pDummyNode =
            CNode::Make(
                0,
                NODE_NETWORK,
                0,
                INVALID_SOCKET,
                dummy_addr,
                0u,
                0u,
                asyncTaskPool,
                "",
                true);
        ProcessTxnsSynchApi(testConfig, pool, doubleSpend2Txns, TxSource::p2p, pDummyNode);
        BOOST_CHECK_EQUAL(pool.Size(), 1U);
    }
}

BOOST_AUTO_TEST_CASE(txnvalidator_doublespend_synch_batch_api) {
    CTxMemPool pool;
    CTxnValidator::RejectedTxns mRejectedTxns {};
    // Test all sources.
    for (const auto& txsource: vTxSources) {
        mRejectedTxns = ProcessTxnsSynchBatchApi(testConfig, pool, doubleSpend10Txns, txsource);
        BOOST_CHECK_EQUAL(pool.Size(), 1U);
        // There should be no insufficient fee txns returned.
        BOOST_REQUIRE(!mRejectedTxns.second.size());
        // Check an expected number of invalid txns returned.
        const CTxnValidator::InvalidTxnStateUMap& mInvalidTxns = mRejectedTxns.first;
        BOOST_REQUIRE(mInvalidTxns.size() == doubleSpend10Txns.size()-1);
        for (const auto& elem : mInvalidTxns) {
            BOOST_REQUIRE(!elem.second.IsValid());
            // Due to runtime-conditions it might be detected as:
            // - a mempool conflict
            // - a double spend
            BOOST_REQUIRE(elem.second.IsMempoolConflictDetected() ||
                elem.second.IsDoubleSpendDetected());
        }
    }
    // Test: Txns from p2p with a pointer to a dummy node.
    {
        // Create a dummy address
        CAddress dummy_addr(ip(0xa0b0c001), NODE_NONE);
        CConnman::CAsyncTaskPool asyncTaskPool{testConfig};
        CNodePtr pDummyNode =
            CNode::Make(
                0,
                NODE_NETWORK,
                0,
                INVALID_SOCKET,
                dummy_addr,
                0u,
                0u,
                asyncTaskPool,
                "",
                true);
        mRejectedTxns = ProcessTxnsSynchBatchApi(testConfig, pool, doubleSpend10Txns, TxSource::p2p, pDummyNode);
        BOOST_CHECK_EQUAL(pool.Size(), 1U);
        // There should be no insufficient fee txns returned.
        BOOST_REQUIRE(!mRejectedTxns.second.size());
        // Check an expected number of invalid txns returned.
        const CTxnValidator::InvalidTxnStateUMap& mInvalidTxns = mRejectedTxns.first;
        BOOST_REQUIRE(mInvalidTxns.size() == doubleSpend10Txns.size()-1);
        for (const auto& elem : mInvalidTxns) {
            BOOST_REQUIRE(!elem.second.IsValid());
            // Due to runtime-conditions it might be detected as:
            // - a mempool conflict
            // - a double spend
            BOOST_REQUIRE(elem.second.IsMempoolConflictDetected() ||
                elem.second.IsDoubleSpendDetected());
        }
    }
}

/**
 * TxnValidator: Test asynch interface.
 */
BOOST_AUTO_TEST_CASE(txnvalidator_doublespend_asynch_api) {
    CTxMemPool pool;
    // Update config params to prevent the failure of the test case
    // - this could happen - due to runtime conditions - on an inefficient environment.
    gArgs.ForceSetArg("-txnvalidationasynchrunfreq", "0");
    testConfig.SetMaxStdTxnValidationDuration(1000);
    testConfig.SetMaxNonStdTxnValidationDuration(5000);
    // Test all sources.
    for (const auto& txsource: vTxSources) {
        ProcessTxnsAsynchApi(testConfig, pool, doubleSpend10Txns, txsource);
        BOOST_CHECK_EQUAL(pool.Size(), 1U);
    }
    // Test: Txns from p2p with a pointer to a dummy node.
    {
        auto pDummyNode = DummyNode(testConfig);
        ProcessTxnsAsynchApi(testConfig, pool, doubleSpend10Txns, TxSource::p2p, pDummyNode);
        BOOST_CHECK_EQUAL(pool.Size(), 1U);
    }
}

BOOST_AUTO_TEST_CASE(txnvalidator_limit_memory_usage)
{
    CTxMemPool pool;
    // Make sure validation thread won't run during this test
    gArgs.ForceSetArg("-txnvalidationasynchrunfreq", "10000");
    gArgs.ForceSetArg("-txnvalidationqueuesmaxmemory", "1");

    // Create a larger number of txns than will fit in a 1Mb queue
    std::vector<CMutableTransaction> txns { MakeNLargeTxns(25, coinbaseTxns[0], scriptPubKey) };
    auto txnsInputs { TxInputDataVec(TxSource::p2p, txns) };

    // Create txn validator
    std::shared_ptr<CTxnValidator> txnValidator {
        std::make_shared<CTxnValidator>(
                testConfig,
                pool,
                std::make_shared<CTxnDoubleSpendDetector>(),
                g_connman->GetTxIdTracker())
    };

    // Attempt to enqueue all txns and verify that we stopped when we hit the max size limit
    txnValidator->newTransaction(txnsInputs);
    BOOST_CHECK(txnValidator->GetTransactionsInQueueCount() < txns.size());
    BOOST_CHECK(txnValidator->GetStdQueueMemUsage() <= 1*ONE_MEBIBYTE);
    BOOST_CHECK_EQUAL(txnValidator->GetNonStdQueueMemUsage(), 0U);
}

BOOST_AUTO_TEST_CASE(txnvalidator_nvalueoutofrange_sync_api) {
    CTxMemPool pool;
    // spendtx_nValue_OutOfRange (a copy of doubleSpend2Txns[0]) with unsupported nValue amount.
    // Set nValue = MAX_MONEY + 1 for the txn to trigger exception when GetValueOut is called.
    auto spendtx_nValue_OutOfRange = doubleSpend2Txns[0];
    spendtx_nValue_OutOfRange.vout[0].nValue = MAX_MONEY + Amount(1);
    BOOST_CHECK_EXCEPTION(
        MoneyRange(CTransaction(spendtx_nValue_OutOfRange).GetValueOut()),
        std::runtime_error,
        GetValueOutException);
    CValidationState result {};
    // Test all sources.
    for (const auto& txsource: vTxSources) {
        result = ProcessTxnSynchApi(testConfig, pool, spendtx_nValue_OutOfRange, txsource);
        BOOST_CHECK(!result.IsValid());
        BOOST_CHECK_EQUAL(pool.Size(), 0U);
    }
}

BOOST_AUTO_TEST_CASE(txnvalidator_nvalueoutofrange_async_api) {
    CTxMemPool pool;
    // Update config params to prevent the failure of the test case
    // - this could happen - due to runtime conditions - on an inefficient environment.
    gArgs.ForceSetArg("-txnvalidationasynchrunfreq", "0");
    testConfig.SetMaxStdTxnValidationDuration(1000);
    testConfig.SetMaxNonStdTxnValidationDuration(5000);
    // Create txn validator
    std::shared_ptr<CTxnValidator> txnValidator {
        std::make_shared<CTxnValidator>(
                testConfig,
                pool,
                std::make_shared<CTxnDoubleSpendDetector>(),
                g_connman->GetTxIdTracker())
    };
    // Case1:
    // doubleSpends10Txns_nValue_OutOfRange (a copy of doubleSpend10Txns) with unsupported nValue amount.
    {
        // Set nValue = MAX_MONEY + 1 for each txn to trigger exception when GetValueOut is called.
        auto doubleSpends10Txns_nValue_OutOfRange = doubleSpend10Txns;
        for (auto& spend: doubleSpends10Txns_nValue_OutOfRange) {
            spend.vout[0].nValue = MAX_MONEY + Amount(1);
            BOOST_CHECK_EXCEPTION(MoneyRange(CTransaction(spend).GetValueOut()), std::runtime_error, GetValueOutException);
        }
        // Schedule txns for processing.
        txnValidator->newTransaction(TxInputDataVec(TxSource::p2p, doubleSpends10Txns_nValue_OutOfRange));
        // Wait for the Validator to process all queued txns.
        txnValidator->waitForEmptyQueue();
        // Non transaction should be accepted due to nValue (value out of range).
        BOOST_CHECK_EQUAL(pool.Size(), 0U);
    }
    // Case2:
    // Send the same txns again (with valid nValue).
    // Check if only one txn (from doubleSpend10Txns) is accepted by the mempool.
    {
        txnValidator->newTransaction(TxInputDataVec(TxSource::p2p, doubleSpend10Txns));
        txnValidator->waitForEmptyQueue();
        BOOST_CHECK_EQUAL(pool.Size(), 1U);
    }
}

BOOST_AUTO_TEST_CASE(txnvalidator_low_priority_chain_async_api) {
    // Test that the part of the chain containing a slow transaction will get processed by a low priority thread.
    CTxMemPool pool;
    CTxMemPoolTestAccess{pool}.InitInMemoryMempoolTxDB();
    // Update config params to prevent the failure of the test case
    // - this could happen - due to runtime conditions - on an inefficient environment.
    gArgs.ForceSetArg("-txnvalidationasynchrunfreq", "1");
    gArgs.ForceSetArg("-maxstdtxnsperthreadratio", "100");
    // Disable processing of slow transactions
    gArgs.ForceSetArg("-maxnonstdtxnsperthreadratio", "0");
    testConfig.SetMaxStdTxnValidationDuration(10);
    testConfig.SetMaxNonStdTxnValidationDuration(5000);
    testConfig.SetMaxTxnChainValidationBudget(0);
    // Create txn validator
    std::shared_ptr<CTxnValidator> txnValidator {
        std::make_shared<CTxnValidator>(
                testConfig,
                pool,
                std::make_shared<CTxnDoubleSpendDetector>(),
                g_connman->GetTxIdTracker())
    };
    auto dummyNode = DummyNode(testConfig);
    auto fundTx = std::make_unique<CTransaction>(coinbaseTxns[0]);

    int ridiculousWidth = 100000;
    // autoscale transaction difficulty
    for (int nWidth = 20; nWidth < ridiculousWidth; ) {
        std::vector<CMutableTransaction> spends {};
        // fast transaction
        spends.push_back(CreateManyToManyTx(1, nWidth, *fundTx, coinbaseKey, scriptPubKey));
        // slow transaction
        spends.push_back(CreateManyToManyTx(nWidth, 1, CTransaction{spends.back()}, coinbaseKey, scriptPubKey));
        // fast transaction
        spends.push_back(CreateManyToManyTx(1, 1, CTransaction{spends.back()}, coinbaseKey, scriptPubKey));

        auto oldPoolSize = pool.Size();

        // only high priority transactions get downgraded to low priority transactions.
        txnValidator->newTransaction(TxInputDataVec(TxSource::p2p, spends, dummyNode, TxValidationPriority::high));

        // wait until we have only non standard transactions to validate
        auto validationDone = [](const auto& counts) -> bool {
            return counts.GetStdQueueCount() + counts.GetProcessingQueueCount() == 0;
        };
        txnValidator->waitUntil( validationDone, false);

        if (pool.Size() > 1 + oldPoolSize) {
            // machine is too fast. try a more difficult transaction
            nWidth *= 2;
            fundTx = std::make_unique<CTransaction>(spends.back());
            BOOST_REQUIRE_LT(nWidth, ridiculousWidth);
            BOOST_TEST_MESSAGE("Machine too fast, trying width " << nWidth << ", remaining funds " << fundTx->vout[0].nValue.GetSatoshis());
            continue;
        }

        auto counts = txnValidator->GetTransactionsInQueueCounts();

        if (pool.Size() == oldPoolSize && txnValidator->getOrphanTxnsPtr()->getTxnsNumber() == 2) {
            BOOST_WARN_MESSAGE(false,
                               "This machine is slow: testConfig.SetMaxStdTxnValidationDuration(" <<
                               testConfig.GetMaxStdTxnValidationDuration().count() <<
                               ") is too small. Skipping the test");
            return;
        }

        BOOST_CHECK_EQUAL(pool.Size(), 1 + oldPoolSize);
        BOOST_CHECK_EQUAL(counts.GetStdQueueCount(), 0U);
        BOOST_CHECK_EQUAL(counts.GetProcessingQueueCount(), 0U);
        BOOST_CHECK_EQUAL(counts.GetNonStdQueueCount(), 1U);
        BOOST_CHECK_EQUAL(txnValidator->getOrphanTxnsPtr()->getTxnsNumber(), 1U);
        break;
    }
}

BOOST_AUTO_TEST_SUITE_END()
