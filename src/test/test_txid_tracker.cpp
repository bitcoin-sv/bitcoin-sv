// Copyright (c) 2020 The Bitcoin SV developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "random.h"
#include "txn_util.h"
#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(test_txid_tracker, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(test_api) {
    const TxId& txid = TxId(GetRandHash());
    CTxIdTracker tracker {};
    // Check size
    BOOST_CHECK_EQUAL(tracker.Size(), 0);
    // Check insert
    tracker.Insert(txid);
    BOOST_CHECK_EQUAL(tracker.Size(), 1);
    // ... try to insert txid again
    tracker.Insert(txid);
    BOOST_CHECK_EQUAL(tracker.Size(), 1);
    // ... insert a new TxId as a rvalue
    tracker.Insert(TxId(GetRandHash()));
    BOOST_CHECK_EQUAL(tracker.Size(), 2);
    // Check contains
    BOOST_REQUIRE(tracker.Contains(txid));
    // Check erase
    tracker.Erase(txid);
    BOOST_CHECK_EQUAL(tracker.Size(), 1);
    BOOST_REQUIRE(!tracker.Contains(txid));
    // ... try to erase txid again
    tracker.Erase(txid);
    BOOST_CHECK_EQUAL(tracker.Size(), 1);
    // Check clear
    tracker.Clear();
    BOOST_CHECK_EQUAL(tracker.Size(), 0);
}

BOOST_AUTO_TEST_SUITE_END()
