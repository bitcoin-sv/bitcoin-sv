// Copyright (c) 2019 The Bitcoin SV developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "config.h"
#include "consensus/validation.h"
#include "txmempool.h"
#include "txn_double_spend_detector.h"
#include "txn_validation_data.h"
#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>

namespace {
    // Create an orphan txn
	TxInputDataSPtr CreateTxnWithNInputs(
        TxSource source,
        size_t nNumInputs) {

        CKey key;
        key.MakeNewKey(true);

        CMutableTransaction tx;
        tx.vin.resize(nNumInputs);
        for (size_t i=0; i<nNumInputs; i++) {
            tx.vin[i].prevout = COutPoint(InsecureRand256(), 0);
            tx.vin[i].scriptSig << OP_1;
        }
        tx.vout.resize(1);
        tx.vout[0].nValue = 1 * CENT;
        tx.vout[0].scriptPubKey =
            GetScriptForDestination(key.GetPubKey().GetID());
        // Return a shared object with txn's input data
        return std::make_shared<CTxInputData>(
                                    source,                   // tx source
                                    TxValidationPriority::normal,          // tx validation priority
                                    MakeTransactionRef(tx));  // a pointer to the tx
    }
}

BOOST_FIXTURE_TEST_SUITE(test_double_spend_detector, TestingSetup)

BOOST_AUTO_TEST_CASE(test_detector_creation) {
    // Create detector object.
    std::shared_ptr<CTxnDoubleSpendDetector> dsDetector {
        std::make_shared<CTxnDoubleSpendDetector>()
    };
    BOOST_REQUIRE(dsDetector);
}

BOOST_AUTO_TEST_CASE(test_detector_insert_txn_inputs) {
    // Create detector object.
    std::shared_ptr<CTxnDoubleSpendDetector> dsDetector {
        std::make_shared<CTxnDoubleSpendDetector>()
    };

    auto txnInputData1 = CreateTxnWithNInputs(TxSource::p2p, 10);
    const CTransaction &tx1 = *txnInputData1->mpTx;
    auto txnInputData2 = CreateTxnWithNInputs(TxSource::p2p, 10);
    const CTransaction &tx2 = *txnInputData2->mpTx;
    auto txnInputData3 = CreateTxnWithNInputs(TxSource::p2p, 10);
    const CTransaction &tx3 = *txnInputData3->mpTx;

    CValidationState state;
    // tx1 checks
    BOOST_REQUIRE(dsDetector->insertTxnInputs(txnInputData1, mempool, state, true));
    BOOST_REQUIRE(!dsDetector->insertTxnInputs(txnInputData1, mempool, state, true));
    // tx2 checks
    BOOST_REQUIRE(dsDetector->insertTxnInputs(txnInputData2, mempool, state, true));
    BOOST_REQUIRE(!dsDetector->insertTxnInputs(txnInputData2, mempool, state, true));
    // tx3 checks
    BOOST_REQUIRE(dsDetector->insertTxnInputs(txnInputData3, mempool, state, true));
    BOOST_REQUIRE(!dsDetector->insertTxnInputs(txnInputData3, mempool, state, true));
 
    auto nTxnsVinSize = tx1.vin.size() + tx2.vin.size() + tx3.vin.size();
    BOOST_CHECK(dsDetector->getKnownSpendsSize() == nTxnsVinSize);
}

BOOST_AUTO_TEST_CASE(test_detector_conflicts) {
    // Create detector object.
    std::shared_ptr<CTxnDoubleSpendDetector> dsDetector {
        std::make_shared<CTxnDoubleSpendDetector>()
    };

    auto txnInputData1 = CreateTxnWithNInputs(TxSource::p2p, 10);
    const CTransaction &tx1 = *txnInputData1->mpTx;
    auto txnInputData2 = CreateTxnWithNInputs(TxSource::p2p, 10);
    const CTransaction &tx2 = *txnInputData2->mpTx;

    CValidationState state;
    BOOST_REQUIRE(dsDetector->insertTxnInputs(txnInputData1, mempool, state, true));
    // Assign tx1's input as the first input of tx2 
    const_cast<COutPoint&>(tx2.vin[0].prevout) = const_cast<COutPoint&>(tx1.vin[0].prevout);

    // Try to remove inputs from a copy of tx1 that was never added
    // Should not change anything
    dsDetector->removeTxnInputs(tx2);
    BOOST_CHECK(dsDetector->getKnownSpendsSize() == tx1.vin.size());

    // Try to add conflicted transaction
    // Should not change anything
    BOOST_REQUIRE(!dsDetector->insertTxnInputs(txnInputData2, mempool, state, true));
    BOOST_CHECK(dsDetector->getKnownSpendsSize() == tx1.vin.size());

    // Try to remove inputs from a copy of tx1 that was never added after a try
    // to add it
    // Should not change anything
    dsDetector->removeTxnInputs(tx2);
    BOOST_CHECK(dsDetector->getKnownSpendsSize() == tx1.vin.size());

    // Check if we are able to add tx2 after conflicting inputs were removed
    dsDetector->removeTxnInputs(tx1);
    BOOST_REQUIRE(dsDetector->insertTxnInputs(txnInputData2, mempool, state, true));
}

BOOST_AUTO_TEST_CASE(test_detector_remove_txn_inputs) {
    // Create detector object.
    std::shared_ptr<CTxnDoubleSpendDetector> dsDetector {
        std::make_shared<CTxnDoubleSpendDetector>()
    };

    auto txnInputData1 = CreateTxnWithNInputs(TxSource::p2p, 10000);
    const CTransaction &tx1 = *txnInputData1->mpTx;
    auto txnInputData2 = CreateTxnWithNInputs(TxSource::p2p, 10);
    const CTransaction &tx2 = *txnInputData2->mpTx;

    CValidationState state;
    // Insert tx1
    BOOST_REQUIRE(dsDetector->insertTxnInputs(txnInputData1, mempool, state, true));
    BOOST_CHECK(dsDetector->getKnownSpendsSize() == tx1.vin.size());
    // Try to remove inputs from a non existing txn
    dsDetector->removeTxnInputs(tx2);
    BOOST_CHECK(dsDetector->getKnownSpendsSize() == tx1.vin.size());
    // Add inputs from tx2
    BOOST_REQUIRE(dsDetector->insertTxnInputs(txnInputData2, mempool, state, true));
    BOOST_CHECK(dsDetector->getKnownSpendsSize() == tx1.vin.size() + tx2.vin.size());
    // Remove inputs from tx2
    dsDetector->removeTxnInputs(tx2);
    BOOST_CHECK(dsDetector->getKnownSpendsSize() == tx1.vin.size());
}

BOOST_AUTO_TEST_CASE(test_detector_clear_txn_inputs) {
    // Create detector object.
    std::shared_ptr<CTxnDoubleSpendDetector> dsDetector {
        std::make_shared<CTxnDoubleSpendDetector>()
    };

    auto txnInputData1 = CreateTxnWithNInputs(TxSource::p2p, 10000);
    const CTransaction &tx1 = *txnInputData1->mpTx;

    CValidationState state;
    // Insert tx1
    BOOST_REQUIRE(dsDetector->insertTxnInputs(txnInputData1, mempool, state, true));
    BOOST_CHECK(dsDetector->getKnownSpendsSize() == tx1.vin.size());
    dsDetector->clear();
    BOOST_CHECK(dsDetector->getKnownSpendsSize() == 0);
}

BOOST_AUTO_TEST_SUITE_END()
