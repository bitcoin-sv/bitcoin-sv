// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2018-2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.
#include "script_num.h"

#include <limits>
#include <iostream>
#include <iterator>
#include <limits>

#include "int_serialization.h"

using bsv::bint;
using namespace std;

CScriptNum::CScriptNum(span<const uint8_t> span,
                       bool fRequireMinimal,
                       const size_t nMaxNumSize,
                       const bool big_int)
{
    assert(m_value.index() == 0);
    assert(get<0>(m_value) == 0);

    if(span.size() > nMaxNumSize)
    {
        throw scriptnum_overflow_error("script number overflow");
    }
    if(fRequireMinimal && !bsv::IsMinimallyEncoded(span, nMaxNumSize))
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
    else if(span.size() <= nMaxNumSize)
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
    static_assert(std::variant_size_v<CScriptNum::value_type> == 2);

    // clang-format off
    if(a.equal_index(b))
        return a.m_value == b.m_value;
    else 
    {
        return visit([&b](const auto& a)
        {
            return visit([&a](const auto& b)
            {
                return a == b;
            }, b.m_value);
        }, a.m_value);
    }
    // clang-format on
}

bool operator<(const CScriptNum& a, const CScriptNum& b)
{
    static_assert(variant_size_v<CScriptNum::value_type> == 2);

    // clang-format off
    if(a.equal_index(b))
        return a.m_value < b.m_value;
    else
    {
        return visit([&b](const auto& a)
                    {
                        return visit([&a](const auto& b)
                        {
                            return a < b;
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
        get<1>(m_value) += get<1>(other.m_value);

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
        get<1>(m_value) -= get<1>(other.m_value);

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
        get<1>(m_value) *= get<1>(other.m_value);

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

CScriptNum CScriptNum::operator-() const
{
    return std::visit([](auto& n) -> CScriptNum { return CScriptNum{-n}; },
                      m_value);
}

std::ostream& operator<<(std::ostream& os, const CScriptNum& n)
{
    visit([&os](const auto& n) { os << n; }, n.m_value);
    return os;
}

namespace
{
    // overload is expected to be standardized in C++23
    // see C++17 The Complete Guide, Chapter 14.1, Nico Josuttis
    // or  Functional Programming in C++, Chapter 9.3, Ivan Cukic
    template <typename... Ts>
    struct overload : Ts... // inherit from variadic template arguments
    {
        using Ts::operator()...; // 'use' all base type function call operators
    };
    // Deduction guide so base types are deduced from passed arguments
    template <typename... Ts>
    overload(Ts...)->overload<Ts...>;
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
                                       return bsv::to_long(n);
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

