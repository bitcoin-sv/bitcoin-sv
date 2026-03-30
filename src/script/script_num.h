// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2018-2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#pragma once

#include <cassert>
#include <compare>
#include <iosfwd>
#include <stdexcept>
#include <variant>
#include <vector>

#include "big_int.h"

class scriptnum_overflow_error : public std::overflow_error
{
public:
    explicit scriptnum_overflow_error(const std::string& str)
        : std::overflow_error(str)
    {
    }
};

class scriptnum_minencode_error : public std::runtime_error
{
public:
    explicit scriptnum_minencode_error(const std::string& str)
        : std::runtime_error(str)
    {
    }
};

enum class min_encoding_check : uint8_t
{
    no,
    yes
};

class CScriptNum
{
    /**
     * Numeric opcodes (OP_1ADD, etc) are restricted to operating on 4-byte
     * integers. The semantics are subtle, though: operands must be in the range
     * [-2^31 +1...2^31 -1], but results may overflow (and are valid as long as
     * they are not used in a subsequent numeric operation). CScriptNum enforces
     * those semantics by storing results as an int64 and allowing out-of-range
     * values to be returned as a vector of bytes but throwing an exception if
     * arithmetic is done or the result is interpreted as an integer.
     */
public:
    static constexpr size_t MAXIMUM_ELEMENT_SIZE{4};
    static constexpr size_t INT64_SERIALIZED_SIZE{9};

    CScriptNum();
    explicit CScriptNum(const int64_t& n, size_t max_length=MAXIMUM_ELEMENT_SIZE);
    explicit CScriptNum(const bsv::bint& n, size_t max_length);
    explicit CScriptNum(std::span<const uint8_t>,
                        min_encoding_check,
                        size_t max_length = MAXIMUM_ELEMENT_SIZE,
                        bool big_int = false);

    CScriptNum& operator=(int64_t rhs)
    {
        m_value = rhs;
        return *this;
    }

    friend std::strong_ordering operator<=>(const CScriptNum&, const CScriptNum&);
    friend bool operator==(const CScriptNum&, const CScriptNum&);

    friend auto operator<=>(const CScriptNum&, int64_t);

    CScriptNum& operator+=(const CScriptNum&);
    CScriptNum& operator-=(const CScriptNum&);
    CScriptNum& operator*=(const CScriptNum&);
    CScriptNum& operator/=(const CScriptNum&);
    CScriptNum& operator%=(const CScriptNum&);
	CScriptNum& operator<<=(const CScriptNum&);
	CScriptNum& operator>>=(const CScriptNum&);

    CScriptNum& operator&=(const CScriptNum&);
    CScriptNum& operator&=(int64_t);

    CScriptNum operator-() const;

    friend std::ostream& operator<<(std::ostream&, const CScriptNum&);

    int getint() const;
    int64_t getint64() const;
    std::vector<uint8_t> getvch() const;

    // Precondition: n <= numeric_limit<int32_t>::max() and n>=0
    size_t to_size_t_limited() const;

    constexpr size_t max_length() const { return m_max_length; }
    constexpr auto index() const { return m_value.index(); }

private:
    bool equal_index(const CScriptNum&) const;

    using value_type = std::variant<int64_t, bsv::bint>;
    value_type m_value;
    size_t m_max_length;
};

inline auto operator<=>(const CScriptNum& a, int64_t b)
{
    return std::visit([b](const auto& aa) { return aa <=> b; }, a.m_value);
}

inline CScriptNum operator+(CScriptNum a, const CScriptNum& b)
{
    a += b;
    return a;
}

inline CScriptNum operator-(CScriptNum a, const CScriptNum& b)
{
    a -= b;
    return a;
}

inline CScriptNum operator*(CScriptNum a, const CScriptNum& b)
{
    a *= b;
    return a;
}

inline CScriptNum operator/(CScriptNum a, const CScriptNum& b)
{
    a /= b;
    return a;
}

inline CScriptNum operator%(CScriptNum a, const CScriptNum& b)
{
    a %= b;
    return a;
}

inline CScriptNum operator&(CScriptNum a, const CScriptNum& b)
{
    a &= b;
    return a;
}

inline CScriptNum operator&(CScriptNum a, int64_t b)
{
    a &= b;
    return a;
}

std::ostream& operator<<(std::ostream&, const CScriptNum&);

