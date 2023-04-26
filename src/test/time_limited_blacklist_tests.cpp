// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include <double_spend/time_limited_blacklist.h>

#include <boost/test/unit_test.hpp>

#include <chrono>
#include <thread>
#include <string>

BOOST_AUTO_TEST_SUITE(blacklist)

BOOST_AUTO_TEST_CASE(add_remove)
{
    // Create a blacklist of 3 strings
    TimeLimitedBlacklist<std::string> blacklist { 3 };
    BOOST_CHECK_EQUAL(blacklist.GetMaxSize(), 3U);
    const std::string item1 { "Item1" };
    const std::string item2 { "Item2" };
    const std::string item3 { "Item3" };
    const std::string item4 { "Item4" };
    BOOST_CHECK(! blacklist.Contains(item1));
    BOOST_CHECK(! blacklist.Contains(item2));
    BOOST_CHECK(! blacklist.Contains(item3));
    BOOST_CHECK(! blacklist.Contains(item4));
    BOOST_CHECK(! blacklist.IsBlacklisted(item1));
    BOOST_CHECK(! blacklist.IsBlacklisted(item2));
    BOOST_CHECK(! blacklist.IsBlacklisted(item3));
    BOOST_CHECK(! blacklist.IsBlacklisted(item4));

    // Add a couple of items blacklisted for 2 seconds
    auto now { std::chrono::system_clock::now() };
    auto twoSeconds { std::chrono::seconds {2} };
    auto twoSecondsLater { now + twoSeconds };
    blacklist.Add(item1, twoSecondsLater);
    BOOST_CHECK(blacklist.Contains(item1));
    BOOST_CHECK(blacklist.IsBlacklisted(item1));
    blacklist.Add(item2, twoSeconds);
    BOOST_CHECK(blacklist.Contains(item2));
    BOOST_CHECK(blacklist.IsBlacklisted(item2));

    // Sleep until blacklist time expires
    using namespace std::chrono_literals;
    std::this_thread::sleep_for(2s);
    BOOST_CHECK(! blacklist.IsBlacklisted(item1));
    BOOST_CHECK(! blacklist.IsBlacklisted(item2));

    // Check the limit on the number of items in the blacklist is honored
    blacklist.Add(item1, 2s);
    BOOST_CHECK(blacklist.Contains(item1));
    BOOST_CHECK(blacklist.IsBlacklisted(item1));
    std::this_thread::sleep_for(1ms);
    blacklist.Add(item2, 2s);
    BOOST_CHECK(blacklist.Contains(item2));
    BOOST_CHECK(blacklist.IsBlacklisted(item2));
    std::this_thread::sleep_for(1ms);
    blacklist.Add(item3, 2s);
    BOOST_CHECK(blacklist.Contains(item3));
    BOOST_CHECK(blacklist.IsBlacklisted(item3));

    std::this_thread::sleep_for(1ms);
    blacklist.Add(item4, 2s);
    BOOST_CHECK(blacklist.Contains(item4));
    BOOST_CHECK(blacklist.IsBlacklisted(item4));

    // Item4 has replaced the oldest previous Item1
    BOOST_CHECK(! blacklist.Contains(item1));
    BOOST_CHECK(blacklist.Contains(item2));
    BOOST_CHECK(blacklist.IsBlacklisted(item2));
    BOOST_CHECK(blacklist.Contains(item3));
    BOOST_CHECK(blacklist.IsBlacklisted(item3));

    // Check updating an existing item works as expected
    BOOST_CHECK(! blacklist.Contains(item1));
    blacklist.Add(item1, 2s);
    BOOST_CHECK(blacklist.Contains(item1));
    BOOST_CHECK(blacklist.IsBlacklisted(item1));
    BOOST_CHECK_THROW(blacklist.Add(item1, std::chrono::system_clock::now() - 1s, false), std::runtime_error);
    BOOST_CHECK(blacklist.Contains(item1));
    BOOST_CHECK(blacklist.IsBlacklisted(item1));
    BOOST_CHECK_NO_THROW(blacklist.Add(item1, std::chrono::system_clock::now() - 1s, true));
    BOOST_CHECK(blacklist.Contains(item1));
    BOOST_CHECK(! blacklist.IsBlacklisted(item1));
}

BOOST_AUTO_TEST_SUITE_END()

