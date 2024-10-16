// Copyright (c) 2024 BSV Blockchain Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#include "script/malleability_status.h"

using namespace std;

constexpr malleability_status ms;
static_assert(0 == ms.value());
static_assert(!is_malleable(ms));
static_assert(!is_unclean_stack(ms));
static_assert(!is_non_minimal_encoding(ms));
static_assert(!is_high_s(ms));
static_assert(!has_non_push_data(ms));
    
constexpr malleability_status ms_1{malleability_status::unclean_stack};
static_assert(malleability_status::unclean_stack == ms_1.value());
static_assert(is_malleable(ms_1));
static_assert(is_unclean_stack(ms_1));
static_assert(!is_non_minimal_encoding(ms_1));
static_assert(!is_high_s(ms_1));
static_assert(!has_non_push_data(ms_1));

constexpr malleability_status ms_2{malleability_status::non_minimal_encoding};
static_assert(malleability_status::non_minimal_encoding == ms_2.value());
static_assert(is_malleable(ms_2));
static_assert(!is_unclean_stack(ms_2));
static_assert(is_non_minimal_encoding(ms_2));
static_assert(!is_high_s(ms_2));
static_assert(!has_non_push_data(ms_2));

constexpr malleability_status ms_3{malleability_status::high_s};
static_assert(malleability_status::high_s == ms_3.value());
static_assert(is_malleable(ms_3));
static_assert(!is_unclean_stack(ms_3));
static_assert(!is_non_minimal_encoding(ms_3));
static_assert(is_high_s(ms_3));
static_assert(!has_non_push_data(ms_3));

constexpr malleability_status ms_4{malleability_status::non_push_data};
static_assert(malleability_status::non_push_data == ms_4.value());
static_assert(is_malleable(ms_4));
static_assert(!is_unclean_stack(ms_4));
static_assert(!is_non_minimal_encoding(ms_4));
static_assert(!is_high_s(ms_4));
static_assert(has_non_push_data(ms_4));

constexpr malleability_status ms_5{malleability_status::unclean_stack |
                                   malleability_status::non_minimal_encoding | 
                                   malleability_status::high_s |
                                   malleability_status::non_push_data};
static_assert((malleability_status::unclean_stack |
               malleability_status::non_minimal_encoding |
               malleability_status::high_s | 
               malleability_status::non_push_data) == ms_5.value());
static_assert(is_malleable(ms_5));
static_assert(is_unclean_stack(ms_5));
static_assert(is_non_minimal_encoding(ms_5));
static_assert(is_high_s(ms_5));
static_assert(has_non_push_data(ms_5));

