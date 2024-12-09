// Copyright (c) 2024 BSV Blockchain Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#pragma once

#include <cstdint>

namespace malleability
{
    using status = std::uint8_t;

    constexpr static status non_malleable        {0x00};
    constexpr static status unclean_stack        {0x01};
    constexpr static status non_minimal_push     {0x02};
    constexpr static status non_minimal_scriptnum{0x04};
    constexpr static status high_s               {0x08};
    constexpr static status non_push_data        {0x10};
    constexpr static status disallowed           {0x80};
}

constexpr bool is_malleable(const malleability::status s)
{
    return s & (malleability::unclean_stack
                | malleability::non_minimal_push
                | malleability::non_minimal_scriptnum
                | malleability::high_s
                | malleability::non_push_data);
}

constexpr bool is_unclean_stack(const malleability::status s)
{
    return s & malleability::unclean_stack;
}

constexpr bool is_non_minimal_push(const malleability::status s)
{
    return s & malleability::non_minimal_push;
}

constexpr bool is_non_minimal_scriptnum(const malleability::status s)
{
    return s & malleability::non_minimal_scriptnum;
}

constexpr bool is_high_s(const malleability::status s)
{
    return s & malleability::high_s;
}

constexpr bool has_non_push_data(const malleability::status s)
{
    return s & malleability::non_push_data;
}

constexpr bool is_disallowed(const malleability::status s)
{
    return s & malleability::disallowed;
}
