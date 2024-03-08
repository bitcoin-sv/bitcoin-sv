// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2018-2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#pragma once

#include <cassert>
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
    static const size_t MAXIMUM_ELEMENT_SIZE = 4;

    CScriptNum():m_value(0){}
    explicit CScriptNum(const int64_t& n) : m_value(n) {}
    explicit CScriptNum(const bsv::bint& n) : m_value(n) {}
    explicit CScriptNum(std::span<const uint8_t>,
                        bool fRequireMinimal,
                        const size_t nMaxNumSize = MAXIMUM_ELEMENT_SIZE,
                        bool big_int = false);

    CScriptNum& operator=(int64_t rhs)
    {
        m_value = rhs;
        return *this;
    }

    friend bool operator==(const CScriptNum&, const CScriptNum&);

    friend bool operator<(const CScriptNum&, const CScriptNum&);
    friend bool operator<(const CScriptNum&, int64_t);
    friend bool operator<(int64_t, const CScriptNum&);

    CScriptNum& operator+=(const CScriptNum&);
    CScriptNum& operator-=(const CScriptNum&);
    CScriptNum& operator*=(const CScriptNum&);
    CScriptNum& operator/=(const CScriptNum&);
    CScriptNum& operator%=(const CScriptNum&);

    CScriptNum& operator&=(const CScriptNum&);
    CScriptNum& operator&=(int64_t);

    CScriptNum operator-() const;

    friend std::ostream& operator<<(std::ostream&, const CScriptNum&);

    int getint() const;
    std::vector<uint8_t> getvch() const;

    // Precondition: n <= numeric_limit<int32_t>::max() and n>=0
    size_t to_size_t_limited() const;

private:
    bool equal_index(const CScriptNum&) const;

    using value_type = std::variant<int64_t, bsv::bint>;
    value_type m_value;
};

// Equality operators
bool operator==(const CScriptNum&, const CScriptNum&);
inline bool operator!=(const CScriptNum& a, const CScriptNum& b)
{
    return !(a == b);
}

// Relational operators
bool operator<(const CScriptNum&, const CScriptNum&);
inline bool operator<(const CScriptNum& a, int64_t b)
{
    return std::visit([b](const auto& a) { return a < b; }, a.m_value);
}

inline bool operator<(int64_t a, const CScriptNum& b)
{
    return std::visit([a](const auto& b) { return a < b; }, b.m_value);
}

inline bool operator>=(const CScriptNum& a, const CScriptNum& b)
{
    return !(a < b);
}
inline bool operator>(const CScriptNum& a, const CScriptNum& b)
{
    return b < a;
}
inline bool operator<=(const CScriptNum& a, const CScriptNum& b)
{
    return !(b < a);
}

inline bool operator>=(const CScriptNum& a, int64_t b) { return !(a < b); }
inline bool operator>(const CScriptNum& a, int64_t b) { return b < a; }
inline bool operator<=(const CScriptNum& a, int64_t b) { return !(b < a); }

inline bool operator>=(int64_t a, const CScriptNum& b) { return !(a < b); }
inline bool operator>(int64_t a, const CScriptNum& b) { return b < a; }
inline bool operator<=(int64_t a, const CScriptNum& b) { return !(b < a); }

// Arithmetic operators
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

