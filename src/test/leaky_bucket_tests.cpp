// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include <leaky_bucket.h>

#include <chrono>
#include <thread>

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(LeakyBucketTests)

BOOST_AUTO_TEST_CASE(FillDrain)
{
    // Bucket that drains 1 every ms
    using namespace std::chrono_literals;
    LeakyBucket<std::chrono::milliseconds> bucket1 { 1000, 1ms };

    // Bucket that drains 2 every ms, expressed in terms of seconds
    LeakyBucket<std::chrono::milliseconds> bucket2 { 1000, 1s, 2000 };

    // Check we're not overflowing at start
    BOOST_CHECK(!bucket1.Overflowing());
    BOOST_CHECK_EQUAL(bucket1.GetFillLevel(), 0);
    BOOST_CHECK(!bucket2.Overflowing());
    BOOST_CHECK_EQUAL(bucket2.GetFillLevel(), 0);

    // Part fill
    BOOST_CHECK(!(bucket1 += 500));
    BOOST_CHECK(bucket1.GetFillLevel() > 0);
    BOOST_CHECK(!(bucket2 += 500));
    BOOST_CHECK(bucket2.GetFillLevel() > 0);

    // Check we drain at something like the correct rate
    double startLevel1 { bucket1.GetFillLevel() };
    double startLevel2 { bucket2.GetFillLevel() };
    std::this_thread::sleep_for(5ms);
    BOOST_CHECK(bucket1.GetFillLevel() < startLevel1);
    BOOST_CHECK(bucket1.GetFillLevel() > 0);
    BOOST_CHECK(bucket2.GetFillLevel() < startLevel2);
    BOOST_CHECK(bucket2.GetFillLevel() > 0);
    BOOST_CHECK(bucket2.GetFillLevel() < bucket1.GetFillLevel());
    std::this_thread::sleep_for(500ms);
    BOOST_CHECK_EQUAL(bucket1.GetFillLevel(), 0);
    BOOST_CHECK_EQUAL(bucket2.GetFillLevel(), 0);

    // Check overflow
    BOOST_CHECK(!(bucket1 += 1000));
    BOOST_CHECK(bucket1 += 1000);
    BOOST_CHECK(bucket1.Overflowing());

    // Test creating partially filled
    LeakyBucket<std::chrono::milliseconds> bucket3 { 1000, 500, 1ms };
    BOOST_CHECK(bucket3.GetFillLevel() > 0);
    BOOST_CHECK(!bucket3.Overflowing());
}

BOOST_AUTO_TEST_SUITE_END()

