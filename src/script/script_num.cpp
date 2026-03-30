// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2018-2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.
#include "script_num.h"

#include <climits>
#include <compare>
#include <cstdint>
#include <limits>
#include <iostream>
#include <iterator>
#include <limits>
#include <variant>

#include "int_serialization.h"
#include "overload.h"

using bsv::bint;
using namespace std;
    
CScriptNum::CScriptNum():
    m_value{0},
    m_max_length{MAXIMUM_ELEMENT_SIZE}
{}

CScriptNum::CScriptNum(const int64_t& n, const size_t max_length):
    m_value{n},
    m_max_length{max_length}
{
    // Validate that max_length is not greater than INT64_SERIALIZED_SIZE
    if(m_max_length > INT64_SERIALIZED_SIZE)
    {
        throw scriptnum_overflow_error("script number overflow");
    }

    // Validate that the value fits within the max_length
    if(getvch().size() > m_max_length)
    {
        throw scriptnum_overflow_error("script number overflow");
    }
}

CScriptNum::CScriptNum(const bsv::bint& n, const size_t max_length):
    m_value{n},
    m_max_length{max_length}
{
    if(n.serialized_size() > max_length)
        throw scriptnum_overflow_error("script number overflow");
}

CScriptNum::CScriptNum(const span<const uint8_t> span,
                       const min_encoding_check min_encode_check,
                       const size_t max_length,
                       const bool big_int):
    m_max_length{max_length}
{
    assert(m_value.index() == 0);
    assert(get<0>(m_value) == 0);

    if(span.size() > max_length)
    {
        throw scriptnum_overflow_error("script number overflow");
    }

    if(min_encode_check == min_encoding_check::yes && !bsv::IsMinimallyEncoded(span, max_length))
    {
        throw scriptnum_minencode_error("non-minimally encoded script number");
    }

    if(span.empty())
    {
        if(big_int)
        {
            m_value = bint{0};
            assert(m_value.index() == 1);
        }
    }
    else if(span.size() <= max_length)
    {
        if(big_int)
            m_value = bsv::bint::deserialize(span);
        else
            m_value = bsv::deserialize<int64_t>(span.begin(), span.end());
    }

    assert(big_int ? m_value.index() == 1 : m_value.index() == 0);
}

CScriptNum& CScriptNum::operator&=(const CScriptNum& other)
{
    static_assert(variant_size_v<CScriptNum::value_type> == 2);
    assert(equal_index(other));

    if(m_value.index() == 0)
        get<0>(m_value) &= get<0>(other.m_value);
    else
        get<1>(m_value) &= get<1>(other.m_value);

    assert(equal_index(other));
    return *this;
}

CScriptNum& CScriptNum::operator&=(int64_t other)
{
    std::visit([&other](auto& n) { n &= other; }, m_value);
    return *this;
}

bool CScriptNum::equal_index(const CScriptNum& other) const
{
    return m_value.index() == other.m_value.index();
}

bool operator==(const CScriptNum& a, const CScriptNum& b)
{
    return (a <=> b) == std::strong_ordering::equal;
}

std::strong_ordering operator<=>(const CScriptNum& a, const CScriptNum& b)
{
    static_assert(variant_size_v<CScriptNum::value_type> == 2);

    // clang-format off
    if(a.equal_index(b))
        return a.m_value <=> b.m_value;
    else
    {
        return visit([&b](const auto& aa)
                    {
                        return visit([&aa](const auto& bb)
                        {
                            return aa <=> bb;
                        }, b.m_value);
                    }, 
                    a.m_value);
    }
    // clang-format on
}

CScriptNum& CScriptNum::operator+=(const CScriptNum& other)
{
    static_assert(variant_size_v<CScriptNum::value_type> == 2);
    assert(equal_index(other));

    if(m_value.index() == 0)
    {
        // little int - little int
        assert(get<0>(other.m_value) == 0 ||
               (get<0>(other.m_value) > 0 &&
                get<0>(m_value) <= std::numeric_limits<int64_t>::max() -
                                       get<0>(other.m_value)) ||
               (get<0>(other.m_value) < 0 &&
                get<0>(m_value) >= std::numeric_limits<int64_t>::min() -
                                       get<0>(other.m_value)));
        get<0>(m_value) += get<0>(other.m_value);
    }
    else
    {
        auto& value{get<1>(m_value)};
        value += get<1>(other.m_value);

        if(value.serialized_size() > m_max_length)
            throw scriptnum_overflow_error("script number overflow");
    }

    assert(equal_index(other));
    return *this;
}

CScriptNum& CScriptNum::operator-=(const CScriptNum& other)
{
    static_assert(variant_size_v<CScriptNum::value_type> == 2);
    assert(equal_index(other));

    if(m_value.index() == 0)
    {
        // little int - little int
        assert(get<0>(other.m_value) == 0 ||
               (get<0>(other.m_value) > 0 &&
                get<0>(m_value) >= std::numeric_limits<int64_t>::min() +
                                       get<0>(other.m_value)) ||
               (get<0>(other.m_value) < 0 &&
                get<0>(m_value) <= std::numeric_limits<int64_t>::max() +
                                       get<0>(other.m_value)));
        get<0>(m_value) -= get<0>(other.m_value);
    }
    else
    {
        auto& value{get<1>(m_value)};
        value -= get<1>(other.m_value);

        if(value.serialized_size() > m_max_length)
            throw scriptnum_overflow_error("script number overflow");
    }

    assert(equal_index(other));
    return *this;
}

CScriptNum& CScriptNum::operator*=(const CScriptNum& other)
{
    static_assert(variant_size_v<CScriptNum::value_type> == 2);
    assert(equal_index(other));

    if(m_value.index() == 0)
        get<0>(m_value) *= get<0>(other.m_value);
    else
    {
        auto& value{get<1>(m_value)};
        value *= get<1>(other.m_value);

        if(value.serialized_size() > m_max_length)
            throw scriptnum_overflow_error("script number overflow");
    }

    assert(equal_index(other));
    return *this;
}

CScriptNum& CScriptNum::operator/=(const CScriptNum& other)
{
    static_assert(variant_size_v<CScriptNum::value_type> == 2);
    assert(equal_index(other));
    
    if(m_value.index() == 0)
        get<0>(m_value) /= get<0>(other.m_value);
    else
        get<1>(m_value) /= get<1>(other.m_value);

    assert(equal_index(other));
    return *this;
}

CScriptNum& CScriptNum::operator%=(const CScriptNum& other)
{
    static_assert(variant_size_v<CScriptNum::value_type> == 2);
    assert(equal_index(other));
    
    if(m_value.index() == 0)
        get<0>(m_value) %= get<0>(other.m_value);
    else
        get<1>(m_value) %= get<1>(other.m_value);

    assert(equal_index(other));
    return *this;
}

CScriptNum& CScriptNum::operator<<=(const CScriptNum& sn_bit_shift)
{
    static_assert(variant_size_v<CScriptNum::value_type> == 2);
    assert(equal_index(sn_bit_shift));

    if(m_value.index() == 0)
    {
        const auto value{get<0>(m_value)};
        const auto bit_shift{get<0>(sn_bit_shift.m_value)};

        constexpr auto bit_shift_max{sizeof(std::variant_alternative_t<0, value_type>) * CHAR_BIT};
        if(bit_shift >= static_cast<int64_t>(bit_shift_max))
        {
            if(value != 0)
                throw scriptnum_overflow_error("script number overflow");
        }
        else if(value < 0)
        {
            // Overflow check for negative value left shift using well-defined division.
            // We check if value * 2^bit_shift < INT64_MIN
            // Equivalently: value < INT64_MIN / 2^bit_shift
            //
            // Note: Right-shift of negative values is implementation-defined, so we
            // use division instead. For bit_shift == 63, we need special handling
            // since 1LL << 63 would be undefined behavior.

            if(bit_shift == bit_shift_max -1)
            {
                // INT64_MIN / 2^63 = -2^63 / 2^63 = -1
                if(value < -1)
                    throw scriptnum_overflow_error("script number overflow");
            }
            else
            {
                // For bit_shift < 63, 1LL << bit_shift is well-defined
                if(value < std::numeric_limits<int64_t>::min() / (1LL << bit_shift))
                    throw scriptnum_overflow_error("script number overflow");
            }

            // C++20 makes left-shift well-defined only when result is representable.
            // The overflow check above ensures this is safe.
            get<0>(m_value) <<= bit_shift;
        }
        else
        {
            if(value > (std::numeric_limits<int64_t>::max() >> bit_shift))
                throw scriptnum_overflow_error("script number overflow");

            get<0>(m_value) <<= bit_shift;
        }

        if(getvch().size() > m_max_length)
            throw scriptnum_overflow_error("script number overflow");
    }
    else [[likely]]
    {
        auto& value{get<1>(m_value)};
        const auto& bit_shift{get<1>(sn_bit_shift.m_value)};

        const auto current_size{value.serialized_size()};
        const bsv::bint shift_bytes{bit_shift / CHAR_BIT};
        if(bsv::bint{current_size} + shift_bytes > bsv::bint{m_max_length})
            throw scriptnum_overflow_error("script number overflow");

        value <<= bit_shift;
        if(value.serialized_size() > m_max_length)
            throw scriptnum_overflow_error("script number overflow");
    }

    assert(equal_index(sn_bit_shift));
    return *this;
}

CScriptNum& CScriptNum::operator>>=(const CScriptNum& sn_bit_shift)
{
    static_assert(variant_size_v<CScriptNum::value_type> == 2);
    assert(equal_index(sn_bit_shift));

    if(m_value.index() == 0)
    {
        const auto value{get<0>(m_value)};
        const auto bit_shift{get<0>(sn_bit_shift.m_value)};

        constexpr auto bit_shift_max{sizeof(std::variant_alternative_t<0, value_type>) * CHAR_BIT};
        if(bit_shift >= static_cast<int64_t>(bit_shift_max))
        {
            // Shifting by 64 or more bits: mathematical division by 2^64 or more
            // gives 0 for any value that fits in int64_t
            get<0>(m_value) = 0;
        }
        else if(value < 0)
        {
            // Mathematical division by 2^bit_shift, rounding toward zero
            // C++ arithmetic right shift rounds toward negative infinity,
            // but we want division semantics (round toward zero)
            // For negative values: n / 2^k = -((-n) >> k)
            //
            // Special case: -INT64_MIN overflows int64_t (undefined behavior).
            // For INT64_MIN = -2^63, arithmetic right shift gives the correct
            // division result because -2^63 is exactly divisible by all powers of 2.
            if(value == std::numeric_limits<int64_t>::min())
            {
                get<0>(m_value) = value >> bit_shift;
            }
            else
            {
                get<0>(m_value) = -((-value) >> bit_shift);
            }
        }
        else
        {
            // Simple right shift for positive values
            get<0>(m_value) >>= bit_shift;
        }
    }
    else [[likely]]
    {
        auto& value{get<1>(m_value)};
        const auto& bit_shift{get<1>(sn_bit_shift.m_value)};
        value >>= bit_shift;
    }

    assert(equal_index(sn_bit_shift));
    return *this;
}

CScriptNum CScriptNum::operator-() const
{
    if(m_value.index() == 0)
    {
        return CScriptNum{-std::get<0>(m_value), m_max_length};
    }
    else
    {
        return CScriptNum{-std::get<1>(m_value), m_max_length};
    }
}

std::ostream& operator<<(std::ostream& os, const CScriptNum& n)
{
    visit([&os](const auto& nn) { os << nn; }, n.m_value);
    return os;
}

int CScriptNum::getint() const
{
    static_assert(variant_size_v<CScriptNum::value_type> == 2);

    return std::visit(overload{[](const bsv::bint& n) -> int {
                                   static const bint bn_int_min{
                                       std::numeric_limits<int>::min()};
                                   static const bint bn_int_max{
                                       std::numeric_limits<int>::max()};

                                   if(n > bn_int_max)
                                       return std::numeric_limits<int>::max();
                                   else if(n < bn_int_min)
                                       return std::numeric_limits<int>::min();
                                   else
                                       return bsv::to_long(n); //NOLINT(*-narrowing-conversions)
                               },
                               [](const int64_t n) {
                                   if(n > std::numeric_limits<int>::max())
                                       return std::numeric_limits<int>::max();
                                   else if(n < std::numeric_limits<int>::min())
                                       return std::numeric_limits<int>::min();
                                   else
                                       return static_cast<int>(n);
                               }},
                      m_value);
}

// Extract int64_t with saturation: values exceeding int64_t range are clamped to INT64_MIN/MAX
int64_t CScriptNum::getint64() const
{
    static_assert(variant_size_v<CScriptNum::value_type> == 2);

    return std::visit(overload{[](const bsv::bint& n) -> int64_t {
                                   static const bint bn_int64_min{
                                       std::numeric_limits<int64_t>::min()};
                                   static const bint bn_int64_max{
                                       std::numeric_limits<int64_t>::max()};

                                   if(n > bn_int64_max)
                                       return std::numeric_limits<int64_t>::max();
                                   else if(n < bn_int64_min)
                                       return std::numeric_limits<int64_t>::min();
                                   else
                                       return static_cast<int64_t>(bsv::to_long(n));
                               },
                               [](const int64_t n) { return n; }},
                      m_value);
}

size_t CScriptNum::to_size_t_limited() const
{
    static_assert(variant_size_v<CScriptNum::value_type> == 2);

    return std::visit(overload{[](const bsv::bint& n) {
                                   //we are using int32_t because this is minimum supported size in Windows and Linux based compiler
                                   assert(n >= 0 && n <= std::numeric_limits<int32_t>::max());
                                   return bsv::to_size_t_limited(n);
                               },
                               [](const int64_t n) {
                                   //we are using int32_t because this is minimum supported size in Windows and Linux based compiler
                                   assert(n >= 0 && n <= std::numeric_limits<int32_t>::max());
                                   // n <= numeric_limits<size_t>::max());
                                   return size_t(n);
                               }},
                      m_value);
}


vector<uint8_t> CScriptNum::getvch() const
{
    static_assert(variant_size_v<CScriptNum::value_type> == 2);

    // clang-format off
    return std::visit(overload{[](const bsv::bint& n) 
                      {
                          return n.serialize();
                      },
                      [](const auto& n) 
                      {
                          vector<uint8_t> v;
                          v.reserve(sizeof(n));
                          bsv::serialize(n, back_inserter(v));
                          return v;
                      }},
                      m_value);
    // clang-format on
}

