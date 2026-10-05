// Copyright (c) 2026 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "binary_counter.h"

#include <functional>

static_assert(std::is_same_v<int, op_return_t<std::plus<int>>>);
static_assert(std::is_same_v<long, op_return_t<std::minus<long>>>);

constexpr auto add_lambda = [](int a, int b) { return a + b; };
static_assert(std::is_same_v<int,
                             op_return_t<decltype(add_lambda)> >);

using min_int_ptr = int const&(*)(int const&, int const&);
static_assert(std::is_same_v<int,
                             op_return_t<min_int_ptr>>);

static_assert(std::is_same_v<binary_counter<std::plus<int>>,
                             decltype(binary_counter{std::plus<int>{}})>);

static_assert(std::is_same_v<binary_counter<min_int_ptr, int>,
                             decltype(binary_counter{std::min<int>})>);
