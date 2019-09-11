// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2018-2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.
#include "script_num.h"

#include <iostream>
#include <iterator>

#include "int_serialization.h"

using bsv::bint;
using namespace std;

CScriptNum::CScriptNum(const vector<uint8_t>& vch,
                       bool fRequireMinimal,
                       const size_t nMaxNumSize)
{
    if(vch.size() > nMaxNumSize)
    {
        throw scriptnum_overflow_error("script number overflow");
    }
    if(fRequireMinimal && !bsv::IsMinimallyEncoded(vch, nMaxNumSize))
    {
        throw scriptnum_minencode_error("non-minimally encoded script number");
    }
    if(vch.empty())
        m_value = 0;
    else if(vch.size() <= MAXIMUM_ELEMENT_SIZE)
        m_value = bsv::deserialize<int64_t>(begin(vch), end(vch));
    else
        m_value = bsv::deserialize<bint>(begin(vch), end(vch));
}

namespace
{
    template<typename... Ts>
    struct overload : Ts...
    {
        using Ts::operator()...;
    };
    template<typename... Ts>
    overload(Ts...)->overload<Ts...>;
}

CScriptNum& CScriptNum::operator&=(const CScriptNum& other)
{
    static_assert(variant_size_v<CScriptNum::value_type> == 2);

    // clang-format off
    std::visit(overload{[&other](int64_t& n) 
                        {
                            visit(overload{[&n](const int64_t m)
                            {
                                // little int & little int
                                n &= m;
                            },
                            [/*&n*/](const auto& /*m*/)
                            {
                                // little int & big int
                                assert(false); 
                            }},
                            other.m_value);
                        },
                        [&other](auto& n) 
                        {
                            visit(overload{[&n](const int64_t m)
                            {
                                // big int & little int
                                n &= m;
                            },
                            [&n](const bint& m)
                            {
                                // big int & big int
                                n &= m;
                            }},
                            other.m_value);
                        }},
               m_value);
    // clang-format on
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

    // clang-format off
    std::visit(overload{[&other](int64_t& n) 
                        {
                            visit(overload{[&n](const int64_t& m)
                            {
                                // little int + little int
                                assert(
                                    m == 0 ||
                                    (m > 0 &&
                                    n <=
                                        std::numeric_limits<int64_t>::max() - m) ||
                                    (m < 0 &&
                                    n >= std::numeric_limits<int64_t>::min() - m));
                                n += m;
                            },
                            [/*&n*/](const auto& /*m*/)
                            {
                                // little int + big int
                                // assert(false); 
                            }},
                            other.m_value);
                        },
                        [&other](auto& n) 
                        {
                            visit(overload{[&n](const int64_t& m)
                            {
                                // big int + little int
                                n += m;
                            },
                            [&n](const bint& m)
                            {
                                // big int + big int
                                n += m;
                            }},
                            other.m_value);
                        }},
               m_value);
    // clang-format on
    return *this;
}

CScriptNum& CScriptNum::operator-=(const CScriptNum& other)
{
    static_assert(variant_size_v<CScriptNum::value_type> == 2);

    // clang-format off
    std::visit(overload{[&other](int64_t& n) 
                        {
                            visit(overload{[&n](const int64_t& m)
                            {
                                // little int - little int
                                assert(
                                    m == 0 ||
                                    (m > 0 &&
                                    n >= std::numeric_limits<int64_t>::min() + m)
                                    || (m < 0 && n <=
                                    std::numeric_limits<int64_t>::max() + m));
                                n -= m;
                            },
                            [/*&n*/](const auto& /*m*/)
                            {
                                // little int - big int
                                // assert(false); 
                            }},
                            other.m_value);
                        },
                        [&other](auto& n) 
                        {
                            visit(overload{[&n](const int64_t& m)
                            {
                                // big int - little int
                                n -= m;
                            },
                            [&n](const bint& m)
                            {
                                // big int - big int
                                n -= m;
                            }},
                            other.m_value);
                        }},
               m_value);
    // clang-format on
    return *this;
}

CScriptNum& CScriptNum::operator*=(const CScriptNum& other)
{
    static_assert(variant_size_v<CScriptNum::value_type> == 2);

    // clang-format off
    std::visit(overload{[&other](int64_t& n) 
                        {
                            visit(overload{[&n](const int64_t& m)
                            {
                                // little int * little int
                                n *= m;
                            },
                            [/*&n*/](const auto& /*m*/)
                            {
                                // little int * big int
                                // assert(false); 
                            }},
                            other.m_value);
                        },
                        [&other](auto& n) 
                        {
                            visit(overload{[&n](const int64_t& m)
                            {
                                // big int * little int
                                n *= m;
                            },
                            [&n](const bint& m)
                            {
                                // big int * big int
                                n *= m;
                            }},
                            other.m_value);
                        }},
               m_value);
    // clang-format on

    return *this;
}

CScriptNum& CScriptNum::operator/=(const CScriptNum& other)
{
    static_assert(variant_size_v<CScriptNum::value_type> == 2);

    // clang-format off
    std::visit(overload{[&other](int64_t& n) 
                        {
                            visit(overload{[&n](const int64_t& m)
                            {
                                // little int / little int
                                n /= m;
                            },
                            [/*&n*/](const auto& /*m*/)
                            {
                                // little int / big int
                                // assert(false); 
                            }},
                            other.m_value);
                        },
                        [&other](auto& n) 
                        {
                            visit(overload{[&n](const int64_t& m)
                            {
                                // big int / little int
                                n /= m;
                            },
                            [&n](const bint& m)
                            {
                                // big int / big int
                                n /= m;
                            }},
                            other.m_value);
                        }},
               m_value);
    // clang-format on
    return *this;
}

CScriptNum& CScriptNum::operator%=(const CScriptNum& other)
{
    static_assert(variant_size_v<CScriptNum::value_type> == 2);

    // clang-format off
    std::visit(overload{[&other](int64_t& n) 
                        {
                            visit(overload{[&n](const int64_t& m)
                            {
                                // little int % little int
                                n %= m;
                            },
                            [/*&n*/](const auto& /*m*/)
                            {
                                // little int % big int
                                // assert(false); 
                            }},
                            other.m_value);
                        },
                        [&other](auto& n) 
                        {
                            visit(overload{[&n](const int64_t& m)
                            {
                                // big int % little int
                                n %= m;
                            },
                            [&n](const bint& m)
                            {
                                // big int % big int
                                n %= m;
                            }},
                            other.m_value);
                        }},
               m_value);
    // clang-format on
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

int CScriptNum::getint() const
{
    static_assert(variant_size_v<CScriptNum::value_type> == 2);

    // clang-format off
    return visit(overload{[](const int64_t n) -> int 
                 {
                     if(n > std::numeric_limits<int>::max())
                         return std::numeric_limits<int>::max();
                     else if(n < std::numeric_limits<int>::min())
                         return std::numeric_limits<int>::min();
                     else
                         return n;
                 },
                 [](const bsv::bint& n) -> int
                 {
                     assert(false);
                     return 0;
                 }}, 
                 m_value);
    // clang-format on
}

vector<uint8_t> CScriptNum::getvch() const
{
    static_assert(variant_size_v<CScriptNum::value_type> == 2);

    // clang-format off
    return std::visit(overload{[](const bsv::bint& n) 
                      {
                          vector<uint8_t> v;
                          v.reserve(n.size_bytes());
                          bsv::serialize(n, back_inserter(v));
                          return v;
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

