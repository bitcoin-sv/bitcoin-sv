// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2018-2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include <cassert>
#include <stdexcept>
#include <vector>

class scriptnum_overflow_error : public std::overflow_error {
public:
    explicit scriptnum_overflow_error(const std::string &str)
        : std::overflow_error(str) {}
};

class scriptnum_minencode_error : public std::runtime_error {
public:
    explicit scriptnum_minencode_error(const std::string &str)
        : std::runtime_error(str) {}
};

class CScriptNum {
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

    explicit CScriptNum(const int64_t &n) : m_value(n) {}
    explicit CScriptNum(const std::vector<uint8_t> &vch, bool fRequireMinimal,
                        const size_t nMaxNumSize = MAXIMUM_ELEMENT_SIZE);
    
    CScriptNum& operator=(int64_t rhs)
    {
        m_value = rhs;
        return *this;
    }

    friend bool operator==(const CScriptNum&, const CScriptNum&);

    friend bool operator<(const CScriptNum&, const CScriptNum&);
    friend bool operator<(const CScriptNum&, int64_t);
    friend bool operator<(int64_t, const CScriptNum&);

    CScriptNum& operator+=(const CScriptNum& other)
    {
        assert(
            other.m_value == 0 ||
            (other.m_value > 0 &&
             m_value <= std::numeric_limits<int64_t>::max() - other.m_value) ||
            (other.m_value < 0 &&
             m_value >= std::numeric_limits<int64_t>::min() - other.m_value));
        m_value += other.m_value;
        return *this;
    }

    CScriptNum& operator-=(const CScriptNum& other)
    {
        assert(
            other.m_value == 0 ||
            (other.m_value > 0 &&
             m_value >= std::numeric_limits<int64_t>::min() + other.m_value) ||
            (other.m_value < 0 &&
             m_value <= std::numeric_limits<int64_t>::max() + other.m_value));
        m_value -= other.m_value;
        return *this;
    }

    CScriptNum& operator*=(const CScriptNum& other) 
    {
        m_value *= other.m_value;
        return *this;
    }
    
    CScriptNum& operator/=(const CScriptNum& other) 
    {
        m_value /= other.m_value;
        return *this;
    }

    CScriptNum& operator%=(const CScriptNum& other) 
    {
        m_value %= other.m_value;
        return *this;
    }

    CScriptNum& operator&=(const CScriptNum& other)
    {
        return operator&=(other.m_value);
    }

    CScriptNum& operator&=(int64_t other)
    {
        m_value &= other;
        return *this;
    }

    CScriptNum operator-() const
    {
        assert(m_value != std::numeric_limits<int64_t>::min());
        return CScriptNum(-m_value);
    }

    int getint() const {
        if (m_value > std::numeric_limits<int>::max())
            return std::numeric_limits<int>::max();
        else if (m_value < std::numeric_limits<int>::min())
            return std::numeric_limits<int>::min();
        return m_value;
    }

    std::vector<uint8_t> getvch() const;

private:
    int64_t m_value;
};

// Equality operators
inline bool operator==(const CScriptNum& a, const CScriptNum& b)
{
    return a.m_value == b.m_value;
}

inline bool operator!=(const CScriptNum& a, const CScriptNum& b)
{
    return !(a == b);
}

// Relational operators
inline bool operator<(const CScriptNum& a, const CScriptNum& b)
{
    return a.m_value < b.m_value;
}
inline bool operator<(const CScriptNum& a, int64_t b) { return a.m_value < b; }
inline bool operator<(int64_t a, const CScriptNum& b) { return a < b.m_value; }

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


