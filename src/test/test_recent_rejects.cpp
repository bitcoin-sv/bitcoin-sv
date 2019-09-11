// Copyright (c) 2019 The Bitcoin SV developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "config.h"
#include "txn_recent_rejects.h"
#include "txn_validation_data.h"
#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(test_recent_rejects, TestingSetup)

BOOST_AUTO_TEST_CASE(test_creation) {
    // Create recent rejects object.
    TxnRecentRejectsSPtr txnRecentRejects {
        std::make_shared<CTxnRecentRejects>()
    };
    BOOST_REQUIRE(txnRecentRejects);
}

BOOST_AUTO_TEST_CASE(test_insert_isrejected) {
    // Create recent rejects object.
    TxnRecentRejectsSPtr txnRecentRejects {
        std::make_shared<CTxnRecentRejects>()
    };
    std::vector<uint256> vTxnHashes {1000, InsecureRand256()};
    for (const auto& txHash : vTxnHashes) {
        // insert txn hash
        txnRecentRejects->insert(txHash);
        // check if txn was added
        BOOST_REQUIRE(txnRecentRejects->isRejected(txHash));
    }
}

BOOST_AUTO_TEST_CASE(test_reset) {
    // Create recent rejects object.
    TxnRecentRejectsSPtr txnRecentRejects {
        std::make_shared<CTxnRecentRejects>()
    };
    auto txHash1 = InsecureRand256();
    auto txHash2 = InsecureRand256();
    BOOST_REQUIRE(!txnRecentRejects->isRejected(txHash1));
    BOOST_REQUIRE(!txnRecentRejects->isRejected(txHash2));
    txnRecentRejects->insert(txHash1);
    txnRecentRejects->insert(txHash2);
    BOOST_REQUIRE(txnRecentRejects->isRejected(txHash1));
    BOOST_REQUIRE(txnRecentRejects->isRejected(txHash2));
    txnRecentRejects->reset();
    BOOST_REQUIRE(!txnRecentRejects->isRejected(txHash1));
    BOOST_REQUIRE(!txnRecentRejects->isRejected(txHash2));
}

BOOST_AUTO_TEST_SUITE_END()
