// Copyright (c) 2024 BSV Blockchain Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#include "script/malleability_status.h"

using namespace std;

constexpr malleability::status ms{malleability::non_malleable};
static_assert(malleability::non_malleable == ms);
static_assert(!is_malleable(ms));
static_assert(!is_unclean_stack(ms));
static_assert(!is_non_minimal_encoding(ms));
static_assert(!is_high_s(ms));
static_assert(!has_non_push_data(ms));
static_assert(!is_disallowed(ms));
    
constexpr malleability::status ms_1{malleability::unclean_stack};
static_assert(malleability::unclean_stack == ms_1);
static_assert(is_malleable(ms_1));
static_assert(is_unclean_stack(ms_1));
static_assert(!is_non_minimal_encoding(ms_1));
static_assert(!is_high_s(ms_1));
static_assert(!has_non_push_data(ms_1));
static_assert(!is_disallowed(ms));

constexpr malleability::status ms_2{malleability::non_minimal_encoding};
static_assert(malleability::non_minimal_encoding == ms_2);
static_assert(is_malleable(ms_2));
static_assert(!is_unclean_stack(ms_2));
static_assert(is_non_minimal_encoding(ms_2));
static_assert(!is_high_s(ms_2));
static_assert(!has_non_push_data(ms_2));
static_assert(!is_disallowed(ms));

constexpr malleability::status ms_3{malleability::high_s};
static_assert(malleability::high_s == ms_3);
static_assert(is_malleable(ms_3));
static_assert(!is_unclean_stack(ms_3));
static_assert(!is_non_minimal_encoding(ms_3));
static_assert(is_high_s(ms_3));
static_assert(!has_non_push_data(ms_3));
static_assert(!is_disallowed(ms));

constexpr malleability::status ms_4{malleability::non_push_data};
static_assert(malleability::non_push_data == ms_4);
static_assert(is_malleable(ms_4));
static_assert(!is_unclean_stack(ms_4));
static_assert(!is_non_minimal_encoding(ms_4));
static_assert(!is_high_s(ms_4));
static_assert(has_non_push_data(ms_4));
static_assert(!is_disallowed(ms));

constexpr malleability::status ms_5{malleability::disallowed};
static_assert(!is_malleable(ms_5));
static_assert(!is_unclean_stack(ms_5));
static_assert(!is_non_minimal_encoding(ms_5));
static_assert(!is_high_s(ms_5));
static_assert(!has_non_push_data(ms_5));
static_assert(is_disallowed(ms_5));

constexpr malleability::status ms_6{malleability::unclean_stack |
                                   malleability::non_minimal_encoding | 
                                   malleability::high_s |
                                   malleability::non_push_data |
                                   malleability::disallowed};
static_assert((malleability::unclean_stack |
               malleability::non_minimal_encoding |
               malleability::high_s | 
               malleability::non_push_data |
               malleability::disallowed) == ms_6);
static_assert(is_malleable(ms_6));
static_assert(is_unclean_stack(ms_6));
static_assert(is_non_minimal_encoding(ms_6));
static_assert(is_high_s(ms_6));
static_assert(has_non_push_data(ms_6));
static_assert(is_disallowed(ms_6));

static_assert(malleability::status{0x84} == []() constexpr
              {
                  malleability::status a{malleability::high_s};
                  malleability::status b{malleability::disallowed}; 
                  return a |= b;
              }());
static_assert(malleability::status{0x3} == []() constexpr
              {
                  malleability::status a{malleability::unclean_stack};
                  malleability::status b{malleability::non_minimal_encoding}; 
                  return a | b;
              }());
