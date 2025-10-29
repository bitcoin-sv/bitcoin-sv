// Copyright (c) 2025 BSV Blockchain Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "shiftnum.h"

#include "taskcancellation.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iterator>


static constexpr bool is_negative(const std::span<const uint8_t> s)
{
    return !s.empty() && (s.back() & 0x80);
}
static_assert(!is_negative({}));
static_assert(!is_negative(std::array<uint8_t, 1>{0x0}));
static_assert(!is_negative(std::array<uint8_t, 2>{0x0, 0x0}));
static_assert(!is_negative(std::array<uint8_t, 1>{0x7f}));
static_assert(!is_negative(std::array<uint8_t, 2>{0xff, 0x7f}));
static_assert(is_negative(std::array<uint8_t, 1>{0x80}));
static_assert(is_negative(std::array<uint8_t, 2>{0, 0x80}));

// Parks the leftmost n bits of curr into the rightmost n bits of prev
// Precondition: rightmost n bits of prev must be 0
static constexpr uint8_t park_bits_left(const uint8_t prev, const uint8_t curr, const uint8_t n)
{
    assert(n < 8);

    if(n == 0)
        return prev;

    return prev | (curr >> (8 - n));
}

static_assert(park_bits_left(0b0000'0000, 0b1111'1111, 0) == 0b0000'0000);
static_assert(park_bits_left(0b0000'0000, 0b1111'1111, 1) == 0b0000'0001);
static_assert(park_bits_left(0b0000'0000, 0b1111'1111, 2) == 0b0000'0011);
static_assert(park_bits_left(0b0000'0000, 0b1111'1111, 3) == 0b0000'0111);
static_assert(park_bits_left(0b0000'0000, 0b1111'1111, 4) == 0b0000'1111);
static_assert(park_bits_left(0b0000'0000, 0b1111'1111, 5) == 0b0001'1111);
static_assert(park_bits_left(0b0000'0000, 0b1111'1111, 6) == 0b0011'1111);
static_assert(park_bits_left(0b0000'0000, 0b1111'1111, 7) == 0b0111'1111);

static_assert(park_bits_left(0b1111'0000, 0b0000'0000, 0) == 0b1111'0000);
static_assert(park_bits_left(0b1111'1110, 0b0000'0000, 1) == 0b1111'1110);
static_assert(park_bits_left(0b1111'1100, 0b0000'0000, 2) == 0b1111'1100);
static_assert(park_bits_left(0b1111'1000, 0b0000'0000, 3) == 0b1111'1000);
static_assert(park_bits_left(0b1111'0000, 0b0000'0000, 4) == 0b1111'0000);
static_assert(park_bits_left(0b1110'0000, 0b0000'0000, 5) == 0b1110'0000);
static_assert(park_bits_left(0b1100'0000, 0b0000'0000, 6) == 0b1100'0000);
static_assert(park_bits_left(0b1000'0000, 0b0000'0000, 7) == 0b1000'0000);

static_assert(park_bits_left(0b0000'0000, 0b1000'0000, 0) == 0b0000'0000);
static_assert(park_bits_left(0b0000'0000, 0b1000'0000, 1) == 0b0000'0001);
static_assert(park_bits_left(0b0000'0000, 0b1000'0000, 2) == 0b0000'0010);
static_assert(park_bits_left(0b0000'0000, 0b1000'0000, 3) == 0b0000'0100);
static_assert(park_bits_left(0b0000'0000, 0b1000'0000, 4) == 0b0000'1000);
static_assert(park_bits_left(0b0000'0000, 0b1000'0000, 5) == 0b0001'0000);
static_assert(park_bits_left(0b0000'0000, 0b1000'0000, 6) == 0b0010'0000);
static_assert(park_bits_left(0b0000'0000, 0b1000'0000, 7) == 0b0100'0000);

static_assert(park_byte_left(0b1111'1111, 0b0000'0000, 0) == 0b1111'1111);
static_assert(park_byte_left(0b1111'1111, 0b0000'0000, 1) == 0b1111'1110);
static_assert(park_byte_left(0b1111'1111, 0b0000'0000, 2) == 0b1111'1100);
static_assert(park_byte_left(0b1111'1111, 0b0000'0000, 3) == 0b1111'1000);
static_assert(park_byte_left(0b1111'1111, 0b0000'0000, 4) == 0b1111'0000);
static_assert(park_byte_left(0b1111'1111, 0b0000'0000, 5) == 0b1110'0000);
static_assert(park_byte_left(0b1111'1111, 0b0000'0000, 6) == 0b1100'0000);
static_assert(park_byte_left(0b1111'1111, 0b0000'0000, 7) == 0b1000'0000);

static_assert(park_byte_left(0b0000'0000, 0b1111'1111, 0) == 0b0000'0000);
static_assert(park_byte_left(0b0000'0000, 0b1111'1111, 1) == 0b0000'0001);
static_assert(park_byte_left(0b0000'0000, 0b1111'1111, 2) == 0b0000'0011);
static_assert(park_byte_left(0b0000'0000, 0b1111'1111, 3) == 0b0000'0111);
static_assert(park_byte_left(0b0000'0000, 0b1111'1111, 4) == 0b0000'1111);
static_assert(park_byte_left(0b0000'0000, 0b1111'1111, 5) == 0b0001'1111);
static_assert(park_byte_left(0b0000'0000, 0b1111'1111, 6) == 0b0011'1111);
static_assert(park_byte_left(0b0000'0000, 0b1111'1111, 7) == 0b0111'1111);

static_assert(park_byte_left(0b0000'0001, 0b1000'0000, 0) == 0b0000'0001);
static_assert(park_byte_left(0b0000'0001, 0b1000'0000, 1) == 0b0000'0011);
static_assert(park_byte_left(0b0000'0001, 0b1000'0000, 2) == 0b0000'0110);
static_assert(park_byte_left(0b0000'0001, 0b1000'0000, 3) == 0b0000'1100);
static_assert(park_byte_left(0b0000'0001, 0b1000'0000, 4) == 0b0001'1000);
static_assert(park_byte_left(0b0000'0001, 0b1000'0000, 5) == 0b0011'0000);
static_assert(park_byte_left(0b0000'0001, 0b1000'0000, 6) == 0b0110'0000);
static_assert(park_byte_left(0b0000'0001, 0b1000'0000, 7) == 0b1100'0000);

static_assert(park_byte_left(0b0101'0101, 0b1010'1010, 1) == 0b1010'1011);
static_assert(park_byte_left(0b0101'0101, 0b1010'1010, 2) == 0b0101'0110);
static_assert(park_byte_left(0b0101'0101, 0b1010'1010, 3) == 0b1010'1101);
static_assert(park_byte_left(0b0101'0101, 0b1010'1010, 4) == 0b0101'1010);

static_assert(park_byte_left(0b0111'1111, 0b1111'1111, 1) == 0b1111'1111);
static_assert(park_byte_left(0b1000'0000, 0b0000'0001, 1) == 0b0000'0000);
static_assert(park_byte_left(0b0100'0000, 0b1000'0000, 2) == 0b0000'0010);

static_assert(park_byte_left(0b1111'1111, 0b1111'1111, 1) == 0b1111'1111);
static_assert(park_byte_left(0b1111'1111, 0b1111'1111, 4) == 0b1111'1111);
static_assert(park_byte_left(0b1111'1111, 0b1111'1111, 7) == 0b1111'1111);

// Return true if cancelled
bool lshiftnum(const task::CCancellationToken& token,
               const CScriptNum& shift_bits,
               const std::span<const uint8_t> ip,
               const std::span<uint8_t> op,
               const int32_t max_chunk_size_bytes)
{
    assert(shift_bits >= 0);
    assert(ip.size() == op.size());

    if(ip.empty() || shift_bits == CScriptNum{0})
        return false;

    const bool ip_negative{is_negative(ip)};
    const CScriptNum sn_data_size_bytes{bsv::bint{ip.size()}};
    const CScriptNum sn_bits_per_byte{bsv::bint{bits_per_byte}};
    const CScriptNum sn_data_size_bits{sn_data_size_bytes * sn_bits_per_byte};
    // When shift_bits > (ip.size() * bits_per_byte) we can simply fill the data
    // with 0s as all bits will be shifted out. ip.size() is limited to
    // INT64_MAX; so any value of shift_bits >= (INT64_MAX * bits_per_byte)
    // will be handled here.
    if(shift_bits >= sn_data_size_bits)
    {
        std::fill(op.begin(), op.end(), 0);
        if(ip_negative)
            op.back() |= 0x80;
        return false;
    }

    // shift_bits fits in int64_t without saturation as we handled >= data_size_bits case above
    const CScriptNum sn_byte_shift{shift_bits / sn_bits_per_byte};
    const auto byte_shift{sn_byte_shift.getint64()};
    const auto sn_bit_shift{shift_bits % CScriptNum{bsv::bint{bits_per_byte}}};
    const uint8_t bit_shift{static_cast<uint8_t>(sn_bit_shift.getint64())};

    // Chunk data and check for cancellation between chunks.
    // Start after the bytes that will be shifted out
    auto f{ip.begin() + byte_shift};
    auto o{op.begin()};
    while(f != ip.end())
    {
        const auto remaining_bytes{std::distance(f, ip.end())};
        const auto chunk_size_bytes{std::min(remaining_bytes,
                                            static_cast<ptrdiff_t>(max_chunk_size_bytes))};
        o = copy_lshift(f, f + chunk_size_bytes,
                        bit_shift,
                        o);
        f += chunk_size_bytes;

        // Park carry bits from next chunk into last byte of current chunk
        if(bit_shift > 0 && f != ip.end())
        {
            *o = park_bits_left(*o, *f, bit_shift);
            ++o; // Advance to next output position for next chunk
        }

        if(token.IsCanceled())
            return true;
    }

    // Sign bit preservation for negative numbers (3-step algorithm):
    // Step 1: copy_lshift performs standard left shift with carry propagation.
    //         For negative numbers, the sign bit (bit 7 of last byte) carries
    //         into the previous byte via park_byte_left's normal carry logic.
    // Step 2: Clear the carried sign bit (this statement).
    //         The sign bit should not shift - it must stay in bit 7 of the last byte.
    //         When shifting left by N bits, the sign bit carries to bit (N-1) of the
    //         second-to-last byte, so we clear it here.
    if(ip_negative && bit_shift > 0 && sn_data_size_bytes > CScriptNum{bsv::bint{1}})
    {
        // assert(data_size_bytes >= 2);  // Proven by guard: data_size_bytes > 1
        // assert(bit_shift >= 1 && bit_shift <= 7);  // Proven by: (shift_bits % 8) and bit_shift > 0
        op[ip.size() - 2] &= ~(1 << (bit_shift - 1));
    }

    // Shift zeros into the remaining bytes
    std::fill(bit_shift ? std::next(o) : o,
              op.end(),
              0);

    // Step 3: Restore sign bit to correct position (preserve sign across shift)
    if(is_negative(op))
    {
        if(!ip_negative)
            op.back() &= 0x7f; // clear sign bit
    }
    else
    {
        if(ip_negative)
            op.back() |= 0x80; // set sign bit
    }

    return false;
}

