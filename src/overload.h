// Copyright (c) 2025 BSV Blockchain Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#pragma once

template<typename... Ts>
struct overload : Ts... // inherit from variadic template arguments
{
    using Ts::operator()...; // 'use' all base type function call operators
};

// Deduction guide so base types are deduced from passed arguments
template <typename... Ts>
overload(Ts...)->overload<Ts...>;
