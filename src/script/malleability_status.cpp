// Copyright (c) 2024 BSV Blockchain Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#include "script/malleability_status.h"

using namespace std;

constexpr malleability::status ms{malleability::non_malleable};
static_assert(malleability::non_malleable == ms);
static_assert(!is_malleable(ms));
static_assert(!is_unclean_stack(ms));
static_assert(!is_non_minimal_push(ms));
static_assert(!is_non_minimal_scriptnum(ms));
static_assert(!is_high_s(ms));
static_assert(!has_non_push_data(ms));
static_assert(!is_null_fail(ms));
static_assert(!is_disallowed(ms));
    
constexpr malleability::status ms_unclean_stack{malleability::unclean_stack};
static_assert(malleability::unclean_stack == ms_unclean_stack);
static_assert(is_malleable(ms_unclean_stack));
static_assert(is_unclean_stack(ms_unclean_stack));
static_assert(!is_non_minimal_push(ms_unclean_stack));
static_assert(!is_non_minimal_scriptnum(ms_unclean_stack));
static_assert(!is_high_s(ms_unclean_stack));
static_assert(!has_non_push_data(ms_unclean_stack));
static_assert(!is_null_fail(ms_unclean_stack));
static_assert(!is_disallowed(ms_unclean_stack));

constexpr malleability::status ms_min_push{malleability::non_minimal_push};
static_assert(malleability::non_minimal_push == ms_min_push);
static_assert(is_malleable(ms_min_push));
static_assert(!is_unclean_stack(ms_min_push));
static_assert(is_non_minimal_push(ms_min_push));
static_assert(!is_non_minimal_scriptnum(ms_min_push));
static_assert(!is_high_s(ms_min_push));
static_assert(!has_non_push_data(ms_min_push));
static_assert(!is_null_fail(ms_min_push));
static_assert(!is_disallowed(ms_min_push));

constexpr malleability::status ms_min_scriptnum{malleability::non_minimal_scriptnum};
static_assert(malleability::non_minimal_scriptnum == ms_min_scriptnum);
static_assert(is_malleable(ms_min_scriptnum));
static_assert(!is_unclean_stack(ms_min_scriptnum));
static_assert(!is_non_minimal_push(ms_min_scriptnum));
static_assert(is_non_minimal_scriptnum(ms_min_scriptnum));
static_assert(!is_high_s(ms_min_scriptnum));
static_assert(!has_non_push_data(ms_min_scriptnum));
static_assert(!is_null_fail(ms_min_scriptnum));
static_assert(!is_disallowed(ms_min_scriptnum));

constexpr malleability::status ms_high_s{malleability::high_s};
static_assert(malleability::high_s == ms_high_s);
static_assert(is_malleable(ms_high_s));
static_assert(!is_unclean_stack(ms_high_s));
static_assert(!is_non_minimal_push(ms_high_s));
static_assert(!is_non_minimal_scriptnum(ms_high_s));
static_assert(is_high_s(ms_high_s));
static_assert(!has_non_push_data(ms_high_s));
static_assert(!is_null_fail(ms_high_s));
static_assert(!is_disallowed(ms_high_s));

constexpr malleability::status ms_non_push_data{malleability::non_push_data};
static_assert(malleability::non_push_data == ms_non_push_data);
static_assert(is_malleable(ms_non_push_data));
static_assert(!is_unclean_stack(ms_non_push_data));
static_assert(!is_non_minimal_push(ms_non_push_data));
static_assert(!is_non_minimal_scriptnum(ms_non_push_data));
static_assert(!is_high_s(ms_non_push_data));
static_assert(has_non_push_data(ms_non_push_data));
static_assert(!is_null_fail(ms_non_push_data));
static_assert(!is_disallowed(ms_non_push_data));

constexpr malleability::status ms_null_fail{malleability::null_fail};
static_assert(malleability::null_fail == ms_null_fail);
static_assert(is_malleable(ms_null_fail));
static_assert(!is_unclean_stack(ms_null_fail));
static_assert(!is_non_minimal_push(ms_null_fail));
static_assert(!is_non_minimal_scriptnum(ms_null_fail));
static_assert(!is_high_s(ms_null_fail));
static_assert(!has_non_push_data(ms_null_fail));
static_assert(is_null_fail(ms_null_fail));
static_assert(!is_disallowed(ms_null_fail));

constexpr malleability::status ms_disallowed{malleability::disallowed};
static_assert(malleability::disallowed == ms_disallowed);
static_assert(!is_malleable(ms_disallowed));
static_assert(!is_unclean_stack(ms_disallowed));
static_assert(!is_non_minimal_push(ms_disallowed));
static_assert(!is_non_minimal_scriptnum(ms_disallowed));
static_assert(!is_high_s(ms_disallowed));
static_assert(!has_non_push_data(ms_disallowed));
static_assert(!is_null_fail(ms_disallowed));
static_assert(is_disallowed(ms_disallowed));

constexpr malleability::status ms_all{malleability::unclean_stack |
                                      malleability::non_minimal_push | 
                                      malleability::non_minimal_scriptnum | 
                                      malleability::high_s |
                                      malleability::non_push_data |
                                      malleability::null_fail |
                                      malleability::disallowed};
static_assert((malleability::unclean_stack |
               malleability::non_minimal_push |
               malleability::non_minimal_scriptnum |
               malleability::high_s | 
               malleability::non_push_data |
               malleability::null_fail |
               malleability::disallowed) == ms_all);
static_assert(is_malleable(ms_all));
static_assert(is_unclean_stack(ms_all));
static_assert(is_non_minimal_push(ms_all));
static_assert(is_non_minimal_scriptnum(ms_all));
static_assert(is_high_s(ms_all));
static_assert(has_non_push_data(ms_all));
static_assert(is_null_fail(ms_all));
static_assert(is_disallowed(ms_all));

static_assert(malleability::status{0x88} == []() constexpr
              {
                  malleability::status a{malleability::high_s};
                  malleability::status b{malleability::disallowed}; 
                  return a |= b;
              }());
static_assert(malleability::status{0x3} == []() constexpr
              {
                  malleability::status a{malleability::unclean_stack};
                  malleability::status b{malleability::non_minimal_push}; 
                  return a | b;
              }());
