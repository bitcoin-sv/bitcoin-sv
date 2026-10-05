// Copyright (c) 2026 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "binary_counter.h"

#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <functional>

BOOST_AUTO_TEST_SUITE(binary_counter_tests)

BOOST_AUTO_TEST_CASE(plus_1)
{
    binary_counter bc{std::plus<int>()};
    bc(1);
    BOOST_CHECK_EQUAL(1, bc[0]);
    bc(1);
    BOOST_CHECK_EQUAL(0, bc[0]);
    BOOST_CHECK_EQUAL(2, bc[1]);
    bc(1);
    BOOST_CHECK_EQUAL(1, bc[0]);
    BOOST_CHECK_EQUAL(2, bc[1]);
    bc(1);
    BOOST_CHECK_EQUAL(0, bc[0]);
    BOOST_CHECK_EQUAL(0, bc[1]);
    BOOST_CHECK_EQUAL(4, bc[2]);
}

BOOST_AUTO_TEST_CASE(min)
{
    const auto ip{std::to_array({ 8, 6, 4, 1, 3, 2, 5, 7})};
    binary_counter bc{std::min<int>};
    bc(ip.begin(), ip.end());

    const auto expected{std::to_array({0, 0, 0, 1})};
    BOOST_CHECK_EQUAL_COLLECTIONS(expected.begin(), expected.end(),
                                  bc.begin(), bc.end());
}

BOOST_AUTO_TEST_CASE(string_concat)
{
    binary_counter bc{std::plus<std::string>()};
    bc("1");

    BOOST_CHECK_EQUAL("1", bc[0]);
    bc("2");
    BOOST_CHECK_EQUAL("", bc[0]);
    BOOST_CHECK_EQUAL("12", bc[1]);
    bc("3");
    BOOST_CHECK_EQUAL("3", bc[0]);
    BOOST_CHECK_EQUAL("12", bc[1]);
    bc("4");
    BOOST_CHECK_EQUAL("", bc[0]);
    BOOST_CHECK_EQUAL("", bc[1]);
    BOOST_CHECK_EQUAL("1234", bc[2]);
}

BOOST_AUTO_TEST_CASE(lambda_mult)
{
    binary_counter bc{[](const int a, const int b) { return a*b; }};
    bc(1);
    BOOST_CHECK_EQUAL(1, bc[0]);
    bc(2);
    BOOST_CHECK_EQUAL(0, bc[0]);
    BOOST_CHECK_EQUAL(2, bc[1]);
    bc(3);
    BOOST_CHECK_EQUAL(3, bc[0]);
    BOOST_CHECK_EQUAL(2, bc[1]);
    bc(4);
    BOOST_CHECK_EQUAL(0, bc[0]);
    BOOST_CHECK_EQUAL(0, bc[1]);
    BOOST_CHECK_EQUAL(24, bc[2]);
}

BOOST_AUTO_TEST_CASE(array_max)
{
    using namespace std;

    binary_counter bc{[](const array<int, 2>& a, const array<int,2>& b)
                      {
                          return a > b ? a : b;
                      }};

    const auto ip1{to_array({1, 2})};
    bc(ip1);
    BOOST_CHECK_EQUAL_COLLECTIONS(ip1.begin(), ip1.end(),
                                  bc[0].begin(), bc[0].end());

    const auto ip2{to_array({3, 4})};
    bc(ip2);
    const auto empty{to_array({0, 0})};
    BOOST_CHECK_EQUAL_COLLECTIONS(empty.begin(), empty.end(),
                                  bc[0].begin(), bc[0].end());
    BOOST_CHECK_EQUAL_COLLECTIONS(ip2.begin(), ip2.end(),
                                  bc[1].begin(), bc[1].end());

    const auto ip3{to_array({5, 6})};
    bc(ip3);
    BOOST_CHECK_EQUAL_COLLECTIONS(ip3.begin(), ip3.end(),
                                  bc[0].begin(), bc[0].end());
    BOOST_CHECK_EQUAL_COLLECTIONS(ip2.begin(), ip2.end(),
                                  bc[1].begin(), bc[1].end());

    const auto ip4{to_array({7, 8})};
    bc(ip4);
    BOOST_CHECK_EQUAL_COLLECTIONS(empty.begin(), empty.end(),
                                  bc[0].begin(), bc[0].end());
    BOOST_CHECK_EQUAL_COLLECTIONS(empty.begin(), empty.end(),
                                  bc[1].begin(), bc[1].end());
    BOOST_CHECK_EQUAL_COLLECTIONS(ip4.begin(), ip4.end(),
                                  bc[2].begin(), bc[2].end());
}

BOOST_AUTO_TEST_CASE(reduce_balanced_min)
{
    using namespace std;

    const auto ip{to_array({ 8, 6, 4, 9, 3, 2, 5, 7, 1})};
    const auto actual = reduce_balanced(ip.begin(), ip.end(),
                                        [](const int a, const int b) { return std::min(a, b); },
                                        0);
    BOOST_CHECK_EQUAL(1, actual); 
}

BOOST_AUTO_TEST_CASE(reduce_balanced_string_concat)
{
    using namespace std;
    using namespace string_literals;

    const auto ip{to_array({ "1"s, "2"s, "3"s, "4"s, "5"s, "6"s, "7"s, "8"s, "9"s})};
    const auto actual = reduce_balanced(ip.begin(), ip.end(),
                                        [](const string& a, const string& b) { return a + b; },
                                        ""s);
    BOOST_CHECK_EQUAL("123456789"s, actual);
}

BOOST_AUTO_TEST_SUITE_END()

