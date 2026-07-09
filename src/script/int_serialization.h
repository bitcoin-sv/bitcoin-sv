// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#pragma once

#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <vector>

namespace bsv
{
    inline auto abs(const int64_t value)
    {
        const bool neg = value < 0;
        uint64_t absvalue = neg ? -(static_cast<uint64_t>(value)) : value;
        return absvalue;
    }

    // O models Concept of OutputIterator
    template <typename T, typename O>
    inline void serialize(const T& value, O o)
    {
        if(value == 0)
            return;

        const bool neg = value < 0;
        auto absvalue = abs(value);
        while(absvalue != 0)
        {
            auto tmp = absvalue & 0xff;
            absvalue >>= 8;
            if(absvalue != 0) // test that this is not the last byte
                *o = tmp;
            else
            {
                // - If the most significant byte is >= 0x80 and the value is
                // positive, push a new zero-byte to make the significant byte <
                // 0x80 again.
                // - If the most significant byte is >= 0x80 and the value is
                // negative, push a new 0x80 byte that will be popped off when
                // converting to an integral.
                // - If the most significant byte is < 0x80 and the value is
                // negative, add 0x80 to it, since it will be subtracted and
                // interpreted as a negative when converting to an integral.
                if((tmp & 0x80) != 0)
                {
                    *o = tmp;
                    ++o;
                    *o = neg ? 0x80 : 0;
                }
                else
                    *o = neg ? tmp | 0x80 : tmp;
            }
            ++o;
        }
    }

    // Deserialize a little-endian, sign-magnitude script number into N.
    //
    // Returns nullopt when the operand cannot denote a value of type N:
    //  - longer than sizeof(N)+1 bytes; or
    //  - exactly sizeof(N)+1 bytes with either a final byte that carries
    //    magnitude bits (not a pure 0x00/0x80 sign byte) or a magnitude out of
    //    range for the sign.
    //
    // numeric_limits<N>::min() is the only value that needs the extra sign
    // byte; it cannot be negated, so it is handled explicitly and every other
    // value uses an ordinary signed negation.
    template<std::signed_integral N>
    inline std::optional<N> deserialize(const std::span<const uint8_t> s)
    {
        if(s.empty())
            return N{};

        constexpr auto n{sizeof(N)};
        const auto d{s.size()};
        if(d > n + 1)
            return {};

        if(d == n + 1 && (s.back() & 0x7f) != 0)
            return {};

        N result{};
        for(size_t i{}; i < d - 1; ++i)
        {
            N tmp{s[i]};
            tmp <<= (8 * i);
            result |= tmp;
        }

        bool negative{};
        N tmp{s[d - 1]};
        if(tmp & 0x80)
        {
            negative = true;
            if(d <= n)
                tmp &= 0x7f;
        }

        if(d <= n)
        {
            tmp <<= (8 * (d - 1));
            result |= tmp;
        }
        else
        {
            if(result == std::numeric_limits<N>::min())
                return negative ? std::optional{result} : std::nullopt;

            if(result < 0)
                return {};
        }

        return negative ? -result : result;
    }

    inline bool IsMinimallyEncoded(std::span<const uint8_t> span,
                                   size_t nMaxNumSize)
    {
        const size_t size = span.size();
        if(size > nMaxNumSize)
        {
            return false;
        }

        if(size > 0)
        {
            // Check that the number is encoded with the minimum possible number
            // of bytes.
            //
            // If the most-significant-byte - excluding the sign bit - is zero
            // then we're not minimal. Note how this test also rejects the
            // negative-zero encoding, 0x80.
            if((span.back() & 0x7f) == 0)
            {
                // One exception: if there's more than one byte and the most
                // significant bit of the second-most-significant-byte is set it
                // would conflict with the sign bit. An example of this case is
                // +-255, which encode to 0xff00 and 0xff80 respectively.
                // (big-endian).
                if(size <= 1 || (span[size - 2] & 0x80) == 0)
                {
                    return false;
                }
            }
        }

        return true;
    }

    inline bool MinimallyEncode(std::vector<uint8_t>& data)
    {
        if(data.size() == 0)
        {
            return false;
        }

        // If the last byte is not 0x00 or 0x80, we are minimally encoded.
        uint8_t last = data.back();
        if(last & 0x7f)
        {
            return false;
        }

        // If the script is one byte long, then we have a zero, which encodes as
        // an empty array.
        if(data.size() == 1)
        {
            data = {};
            return true;
        }

        // If the next byte has it sign bit set, then we are minimaly encoded.
        if(data[data.size() - 2] & 0x80)
        {
            return false;
        }

        // We are not minimally encoded, we need to figure out how much to trim.
        for(size_t i = data.size() - 1; i > 0; i--)
        {
            // We found a non zero byte, time to encode.
            if(data[i - 1] != 0)
            {
                if(data[i - 1] & 0x80)
                {
                    // We found a byte with it sign bit set so we need one more
                    // byte.
                    data[i++] = last;
                }
                else
                {
                    // the sign bit is clear, we can use it.
                    data[i - 1] |= last;
                }

                data.resize(i);
                return true;
            }
        }

        // If we the whole thing is zeros, then we have a zero.
        data = {};
        return true;
    }
}

