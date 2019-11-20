// Copyright (c) 2019 The Bitcoin SV developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "util.h"
#include "validation.h"
#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>

/**
 * Testing txn validation utils.
 *
 * 1. A number of Low and High priority threads (default estimations):
 * - GetNumLowPriorityValidationThrs()
 * - GetNumHighPriorityValidationThrs()
 */

BOOST_FIXTURE_TEST_SUITE(test_txnvalidation_utils, BasicTestingSetup)

/**
 * A basic static test case.
 *
 * Checks based on the given set [0, 24] of Hardware Concurrency Threads.
 */
BOOST_AUTO_TEST_CASE(test_number_of_priority_threads_static) {
    // Hardware Concurrency Threads: 0
    BOOST_CHECK_EQUAL(1, GetNumLowPriorityValidationThrs(0));
    BOOST_CHECK_EQUAL(1, GetNumHighPriorityValidationThrs(0));
    // Hardware Concurrency Threads: 1
    BOOST_CHECK_EQUAL(1, GetNumLowPriorityValidationThrs(1));
    BOOST_CHECK_EQUAL(1, GetNumHighPriorityValidationThrs(1));
    // Hardware Concurrency Threads: 2
    BOOST_CHECK_EQUAL(1, GetNumLowPriorityValidationThrs(2));
    BOOST_CHECK_EQUAL(1, GetNumHighPriorityValidationThrs(2));
    // Hardware Concurrency Threads: 3
    BOOST_CHECK_EQUAL(1, GetNumLowPriorityValidationThrs(3));
    BOOST_CHECK_EQUAL(2, GetNumHighPriorityValidationThrs(3));
    // Hardware Concurrency Threads: 4
    BOOST_CHECK_EQUAL(1, GetNumLowPriorityValidationThrs(4));
    BOOST_CHECK_EQUAL(3, GetNumHighPriorityValidationThrs(4));
    // Hardware Concurrency Threads: 5
    BOOST_CHECK_EQUAL(1, GetNumLowPriorityValidationThrs(5));
    BOOST_CHECK_EQUAL(4, GetNumHighPriorityValidationThrs(5));
    // Hardware Concurrency Threads: 6
    BOOST_CHECK_EQUAL(1, GetNumLowPriorityValidationThrs(6));
    BOOST_CHECK_EQUAL(5, GetNumHighPriorityValidationThrs(6));
    // Hardware Concurrency Threads: 7
    BOOST_CHECK_EQUAL(1, GetNumLowPriorityValidationThrs(7));
    BOOST_CHECK_EQUAL(6, GetNumHighPriorityValidationThrs(7));
    // Hardware Concurrency Threads: 8
    BOOST_CHECK_EQUAL(2, GetNumLowPriorityValidationThrs(8));
    BOOST_CHECK_EQUAL(6, GetNumHighPriorityValidationThrs(8));
    // Hardware Concurrency Threads: 9
    BOOST_CHECK_EQUAL(2, GetNumLowPriorityValidationThrs(9));
    BOOST_CHECK_EQUAL(7, GetNumHighPriorityValidationThrs(9));
    // Hardware Concurrency Threads: 10
    BOOST_CHECK_EQUAL(2, GetNumLowPriorityValidationThrs(10));
    BOOST_CHECK_EQUAL(8, GetNumHighPriorityValidationThrs(10));
    // Hardware Concurrency Threads: 11
    BOOST_CHECK_EQUAL(2, GetNumLowPriorityValidationThrs(11));
    BOOST_CHECK_EQUAL(9, GetNumHighPriorityValidationThrs(11));
    // Hardware Concurrency Threads: 12
    BOOST_CHECK_EQUAL(3, GetNumLowPriorityValidationThrs(12));
    BOOST_CHECK_EQUAL(9, GetNumHighPriorityValidationThrs(12));
    // Hardware Concurrency Threads: 13
    BOOST_CHECK_EQUAL(3, GetNumLowPriorityValidationThrs(13));
    BOOST_CHECK_EQUAL(10, GetNumHighPriorityValidationThrs(13));
    // Hardware Concurrency Threads: 14
    BOOST_CHECK_EQUAL(3, GetNumLowPriorityValidationThrs(14));
    BOOST_CHECK_EQUAL(11, GetNumHighPriorityValidationThrs(14));
    // Hardware Concurrency Threads: 15
    BOOST_CHECK_EQUAL(3, GetNumLowPriorityValidationThrs(15));
    BOOST_CHECK_EQUAL(12, GetNumHighPriorityValidationThrs(15));
    // Hardware Concurrency Threads: 16
    BOOST_CHECK_EQUAL(4, GetNumLowPriorityValidationThrs(16));
    BOOST_CHECK_EQUAL(12, GetNumHighPriorityValidationThrs(16));
    // Hardware Concurrency Threads: 17
    BOOST_CHECK_EQUAL(4, GetNumLowPriorityValidationThrs(17));
    BOOST_CHECK_EQUAL(13, GetNumHighPriorityValidationThrs(17));
    // Hardware Concurrency Threads: 18
    BOOST_CHECK_EQUAL(4, GetNumLowPriorityValidationThrs(18));
    BOOST_CHECK_EQUAL(14, GetNumHighPriorityValidationThrs(18));
    // Hardware Concurrency Threads: 19
    BOOST_CHECK_EQUAL(4, GetNumLowPriorityValidationThrs(19));
    BOOST_CHECK_EQUAL(15, GetNumHighPriorityValidationThrs(19));
    // Hardware Concurrency Threads: 20
    BOOST_CHECK_EQUAL(5, GetNumLowPriorityValidationThrs(20));
    BOOST_CHECK_EQUAL(15, GetNumHighPriorityValidationThrs(20));
    // Hardware Concurrency Threads: 21
    BOOST_CHECK_EQUAL(5, GetNumLowPriorityValidationThrs(21));
    BOOST_CHECK_EQUAL(16, GetNumHighPriorityValidationThrs(21));
    // Hardware Concurrency Threads: 22
    BOOST_CHECK_EQUAL(5, GetNumLowPriorityValidationThrs(22));
    BOOST_CHECK_EQUAL(17, GetNumHighPriorityValidationThrs(22));
    // Hardware Concurrency Threads: 23
    BOOST_CHECK_EQUAL(5, GetNumLowPriorityValidationThrs(23));
    BOOST_CHECK_EQUAL(18, GetNumHighPriorityValidationThrs(23));
    // Hardware Concurrency Threads: 24
    BOOST_CHECK_EQUAL(6, GetNumLowPriorityValidationThrs(24));
    BOOST_CHECK_EQUAL(18, GetNumHighPriorityValidationThrs(24));
}

/**
 * A dynamic test case.
 *
 * Checks based on the given set [25, 100K] of Hardware Concurrency Threads.
 */
BOOST_AUTO_TEST_CASE(test_number_of_priority_threads_dynamic) {
    for (size_t n=25; n < 100000; ++n) {
        // A number of Low priority threads
        size_t nEstimateNumLowPriorityThrs = static_cast<size_t>(n * 0.25);
        size_t nLowPriorityThrs = GetNumLowPriorityValidationThrs(n);
        BOOST_CHECK(nLowPriorityThrs > 0);
        BOOST_CHECK(nLowPriorityThrs >= nEstimateNumLowPriorityThrs);
        // A number of High priority threads
        size_t nEstimateNumHighPriorityThrs = n - nEstimateNumLowPriorityThrs;
        size_t nHighPriorityThrs = GetNumHighPriorityValidationThrs(n);
        BOOST_CHECK(nHighPriorityThrs >= nEstimateNumHighPriorityThrs);
        BOOST_CHECK(nHighPriorityThrs > 0);
        BOOST_CHECK(nHighPriorityThrs < n);
        // A number of high priority threads should be greater than a number of low priority threads.
        BOOST_CHECK(nHighPriorityThrs > nLowPriorityThrs);
    }
}

BOOST_AUTO_TEST_SUITE_END()
