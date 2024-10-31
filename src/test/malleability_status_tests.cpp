// Copyright (c) 2024 BSV Blockchain Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#include "script/malleability_status.h"

#include <boost/test/tools/old/interface.hpp>
#include <boost/test/unit_test.hpp>

#include <sstream>

using namespace std;

constexpr malleability_status ms;
static_assert(0 == ms);
static_assert(0 == ms.value());
static_assert(!is_malleable(ms));
static_assert(!is_unclean_stack(ms));
static_assert(!is_non_minimal_encoding(ms));
static_assert(!is_high_s(ms));
static_assert(!has_non_push_data(ms));
static_assert(!is_disallowed(ms));
    
constexpr malleability_status ms_1{malleability_status::unclean_stack};
static_assert(malleability_status::unclean_stack == ms_1);
static_assert(malleability_status::unclean_stack == ms_1.value());
static_assert(is_malleable(ms_1));
static_assert(is_unclean_stack(ms_1));
static_assert(!is_non_minimal_encoding(ms_1));
static_assert(!is_high_s(ms_1));
static_assert(!has_non_push_data(ms_1));
static_assert(!is_disallowed(ms));

constexpr malleability_status ms_2{malleability_status::non_minimal_encoding};
static_assert(malleability_status::non_minimal_encoding == ms_2);
static_assert(malleability_status::non_minimal_encoding == ms_2.value());
static_assert(is_malleable(ms_2));
static_assert(!is_unclean_stack(ms_2));
static_assert(is_non_minimal_encoding(ms_2));
static_assert(!is_high_s(ms_2));
static_assert(!has_non_push_data(ms_2));
static_assert(!is_disallowed(ms));

constexpr malleability_status ms_3{malleability_status::high_s};
static_assert(malleability_status::high_s == ms_3);
static_assert(malleability_status::high_s == ms_3.value());
static_assert(is_malleable(ms_3));
static_assert(!is_unclean_stack(ms_3));
static_assert(!is_non_minimal_encoding(ms_3));
static_assert(is_high_s(ms_3));
static_assert(!has_non_push_data(ms_3));
static_assert(!is_disallowed(ms));

constexpr malleability_status ms_4{malleability_status::non_push_data};
static_assert(malleability_status::non_push_data == ms_4);
static_assert(malleability_status::non_push_data == ms_4.value());
static_assert(is_malleable(ms_4));
static_assert(!is_unclean_stack(ms_4));
static_assert(!is_non_minimal_encoding(ms_4));
static_assert(!is_high_s(ms_4));
static_assert(has_non_push_data(ms_4));
static_assert(!is_disallowed(ms));

constexpr malleability_status ms_5{malleability_status::disallowed};
static_assert(!is_malleable(ms_5));
static_assert(!is_unclean_stack(ms_5));
static_assert(!is_non_minimal_encoding(ms_5));
static_assert(!is_high_s(ms_5));
static_assert(!has_non_push_data(ms_5));
static_assert(is_disallowed(ms_5));

constexpr malleability_status ms_6{malleability_status::unclean_stack |
                                   malleability_status::non_minimal_encoding | 
                                   malleability_status::high_s |
                                   malleability_status::non_push_data |
                                   malleability_status::disallowed};
static_assert((malleability_status::unclean_stack |
               malleability_status::non_minimal_encoding |
               malleability_status::high_s | 
               malleability_status::non_push_data |
               malleability_status::disallowed) == ms_6);
static_assert(is_malleable(ms_6));
static_assert(is_unclean_stack(ms_6));
static_assert(is_non_minimal_encoding(ms_6));
static_assert(is_high_s(ms_6));
static_assert(has_non_push_data(ms_6));
static_assert(is_disallowed(ms_6));

static_assert(malleability_status{0x84} == []() constexpr
              {
                  malleability_status a{malleability_status::high_s};
                  malleability_status b{malleability_status::disallowed}; 
                  return a |= b;
              }());

static_assert(malleability_status{0x3} == []() constexpr
              {
                  malleability_status a{malleability_status::unclean_stack};
                  malleability_status b{malleability_status::non_minimal_encoding}; 
                  return a | b;
              }());

BOOST_AUTO_TEST_SUITE(malleability_status_tests)

BOOST_AUTO_TEST_CASE(insertion_operator)
{
    constexpr malleability_status ms{malleability_status::unclean_stack |
                                     malleability_status::non_minimal_encoding | 
                                     malleability_status::high_s |
                                     malleability_status::non_push_data |
                                     malleability_status::disallowed};
    std::ostringstream oss;
    oss << ms;
    const string expected{"unclean_stack | non_minimal_encoding "
                          "| high_s | non_push_data | disallowed"};
    BOOST_CHECK_EQUAL(expected, oss.str());
}

BOOST_AUTO_TEST_SUITE_END()

