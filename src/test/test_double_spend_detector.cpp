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
#include <memory>

using namespace std;

namespace
{
    // Create an orphan txn
    auto CreateTxnWithNInputs(size_t nNumInputs)
    {
        CKey key;
        key.MakeNewKey(true);

        CMutableTransaction tx;
        tx.vin.resize(nNumInputs);
        for(size_t i = 0; i < nNumInputs; i++)
        {
            tx.vin[i].prevout = COutPoint(InsecureRand256(), 0);
            tx.vin[i].scriptSig << OP_1;
        }
        tx.vout.resize(1);
        tx.vout[0].nValue = 1 * CENT;
        tx.vout[0].scriptPubKey =
            GetScriptForDestination(key.GetPubKey().GetID());

        return MakeTransactionRef(tx);
    }
}

BOOST_FIXTURE_TEST_SUITE(test_double_spend_detector, TestingSetup)

BOOST_AUTO_TEST_CASE(test_detector_insert_txn_inputs)
{
    CTxnDoubleSpendDetector dsDetector;
    CValidationState state;
    
    // tx1 checks
    const auto ptx1 = CreateTxnWithNInputs(10);
    BOOST_REQUIRE(dsDetector.insertTxnInputs(ptx1, mempool, state, true));
    BOOST_REQUIRE(!dsDetector.insertTxnInputs(ptx1, mempool, state, true));
    
    // tx2 checks
    const auto ptx2 = CreateTxnWithNInputs(10);
    BOOST_REQUIRE(dsDetector.insertTxnInputs(ptx2, mempool, state, true));
    BOOST_REQUIRE(!dsDetector.insertTxnInputs(ptx2, mempool, state, true));
    
    // tx3 checks
    const auto ptx3 = CreateTxnWithNInputs(10);
    BOOST_REQUIRE(dsDetector.insertTxnInputs(ptx3, mempool, state, true));
    BOOST_REQUIRE(!dsDetector.insertTxnInputs(ptx3, mempool, state, true));
 
    const auto nTxnsVinSize = ptx1->vin.size() + ptx2->vin.size() + ptx3->vin.size();
    BOOST_CHECK(dsDetector.getKnownSpendsSize() == nTxnsVinSize);
}

BOOST_AUTO_TEST_CASE(test_detector_conflicts)
{
    CTxnDoubleSpendDetector dsDetector;
    CValidationState state;

    const auto ptx1 = CreateTxnWithNInputs(10);
    const CTransaction& tx1{*ptx1};
    const auto ptx2 = CreateTxnWithNInputs(10);
    const CTransaction& tx2{*ptx2};

    BOOST_REQUIRE(dsDetector.insertTxnInputs(ptx1, mempool, state, true));
    // Assign tx1's input as the first input of tx2 
    const_cast<COutPoint&>(tx2.vin[0].prevout) = const_cast<COutPoint&>(tx1.vin[0].prevout);

    // Try to remove inputs from a copy of tx1 that was never added
    // Should not change anything
    dsDetector.removeTxnInputs(tx2);
    BOOST_CHECK(dsDetector.getKnownSpendsSize() == tx1.vin.size());

    // Try to add conflicted transaction
    // Should not change anything
    BOOST_REQUIRE(!dsDetector.insertTxnInputs(ptx2, mempool, state, true));
    BOOST_CHECK(dsDetector.getKnownSpendsSize() == tx1.vin.size());

    // Try to remove inputs from a copy of tx1 that was never added after a try
    // to add it
    // Should not change anything
    dsDetector.removeTxnInputs(tx2);
    BOOST_CHECK(dsDetector.getKnownSpendsSize() == tx1.vin.size());

    // Check if we are able to add tx2 after conflicting inputs were removed
    dsDetector.removeTxnInputs(tx1);
    BOOST_REQUIRE(dsDetector.insertTxnInputs(ptx2, mempool, state, true));
}

BOOST_AUTO_TEST_CASE(test_detector_remove_txn_inputs)
{
    CTxnDoubleSpendDetector dsDetector;
    CValidationState state;

    const auto ptx1 = CreateTxnWithNInputs(10'000);
    const auto ptx2 = CreateTxnWithNInputs(10);
    
    // Insert tx1
    const CTransaction& tx1{*ptx1};
    BOOST_REQUIRE(dsDetector.insertTxnInputs(ptx1, mempool, state, true));
    BOOST_CHECK(dsDetector.getKnownSpendsSize() == tx1.vin.size());
    
    // Try to remove inputs from a non existing txn
    const CTransaction& tx2{*ptx2};
    dsDetector.removeTxnInputs(tx2);
    BOOST_CHECK(dsDetector.getKnownSpendsSize() == tx1.vin.size());

    // Add inputs from tx2
    BOOST_REQUIRE(dsDetector.insertTxnInputs(ptx2, mempool, state, true));
    BOOST_CHECK(dsDetector.getKnownSpendsSize() == tx1.vin.size() + tx2.vin.size());

    // Remove inputs from tx2
    dsDetector.removeTxnInputs(tx2);
    BOOST_CHECK(dsDetector.getKnownSpendsSize() == tx1.vin.size());
}

BOOST_AUTO_TEST_CASE(test_detector_clear_txn_inputs)
{
    CTxnDoubleSpendDetector dsDetector;

    const auto ptx = CreateTxnWithNInputs(10'000);

    CValidationState state;
    BOOST_REQUIRE(dsDetector.insertTxnInputs(ptx, mempool, state, true));
    
    BOOST_CHECK(dsDetector.getKnownSpendsSize() == ptx->vin.size());
    dsDetector.clear();
    BOOST_CHECK(dsDetector.getKnownSpendsSize() == 0);
}

BOOST_AUTO_TEST_SUITE_END()
