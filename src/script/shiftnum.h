// Copyright (c) 2025 BSV Blockchain Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.
//
// OP_LSHIFTNUM: Left shift with sign preservation
//
// Unlike standard bitwise left shift, OP_LSHIFTNUM preserves the sign bit.
// Negative numbers remain negative after shifting, positive remain positive.
// The sign bit (bit 7 of the last byte) does not participate in the shift.

#pragma once

#include <algorithm>
#include <concepts>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <span>
#include <vector>

#include "script_num.h"

namespace task
{
    class CCancellationToken;
}
    
constexpr uint8_t bits_per_byte{8};

// Shift left n bits. Fill with 0s.
// Return false if successful, true if cancelled.
[[nodiscard]] bool lshiftnum(const task::CCancellationToken&,
                             const CScriptNum&,
                             std::span<const uint8_t> ip,
                             std::span<uint8_t> op,
                             // Default 256MB chunks for cancellation checking.
                             // Operations smaller than this complete in a single chunk with no overhead.
                             int32_t max_chunk_size_bytes = INT32_MAX / bits_per_byte);

// Park a byte into the previous byte by shifting left n bits.
[[nodiscard]] inline constexpr uint8_t park_byte_left(const uint8_t prev,
                                                      const uint8_t curr,
                                                      const uint8_t n)
{
    assert(n < 8);
    return n == 0 ? prev
                  : (prev << n) | (curr >> (bits_per_byte - n));
}

// Shift left n bits in range [f, l).
// Preconditions:
//   - 0 <= bit_shift < 8
// Returns: iterator to one past last modified byte if bit_shift == 0,
//          else iterator to last modified byte (so bits can be parked in it).
template<typename I, typename O>
    requires std::input_iterator<I> &&
             std::same_as<std::iter_value_t<I>, uint8_t> &&
             std::output_iterator<O, uint8_t>
inline O copy_lshift(I f,
                     I l,
                     const uint8_t bit_shift,
                     O of)
{
    assert(bit_shift < 8);

    if(f == l)
        return of;

    if(std::next(f) == l)
    {
        // Input range is a single byte
        if(bit_shift)
            *of = *f << bit_shift;
        else
        {
            if(&*f == &*of)
            {
                // 0 byte shift, 0 bit shift - nothing to do
                return of;
            }
            else
            {
                // copy single byte
                *of = *f;
            }
        }
        return bit_shift ? of : ++of;
    }
    
    // Shift/copy by whole bytes
    const auto count{static_cast<size_t>(std::distance(f, l))};
    std::memmove(&*of, &*f, count);
    auto ol{of + count};

    // Bit shift into final position between bytes if req'd
    if(bit_shift > 0)
    {
        uint8_t prev{*of};
        auto dst_it{of};
        for(auto src_it{std::next(of)}; src_it != ol; ++src_it, ++dst_it)
        {
            const uint8_t curr{*src_it};
            *dst_it = park_byte_left(prev, curr, bit_shift);
            prev = curr;
        }

        // Shift zeroes into the last byte
        *dst_it = park_byte_left(prev, 0, bit_shift);
        ol = dst_it;
    }

    return ol;
}

