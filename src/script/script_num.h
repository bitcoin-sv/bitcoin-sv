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

    explicit CScriptNum(const int64_t &n) { m_value = n; }
    explicit CScriptNum(const std::vector<uint8_t> &vch, bool fRequireMinimal,
                        const size_t nMaxNumSize = MAXIMUM_ELEMENT_SIZE);

    inline bool operator==(const int64_t &rhs) const { return m_value == rhs; }
    inline bool operator!=(const int64_t &rhs) const { return m_value != rhs; }
    inline bool operator<=(const int64_t &rhs) const { return m_value <= rhs; }
    inline bool operator<(const int64_t &rhs) const { return m_value < rhs; }
    inline bool operator>=(const int64_t &rhs) const { return m_value >= rhs; }
    inline bool operator>(const int64_t &rhs) const { return m_value > rhs; }

    inline bool operator==(const CScriptNum &rhs) const {
        return operator==(rhs.m_value);
    }
    inline bool operator!=(const CScriptNum &rhs) const {
        return operator!=(rhs.m_value);
    }
    inline bool operator<=(const CScriptNum &rhs) const {
        return operator<=(rhs.m_value);
    }
    inline bool operator<(const CScriptNum &rhs) const {
        return operator<(rhs.m_value);
    }
    inline bool operator>=(const CScriptNum &rhs) const {
        return operator>=(rhs.m_value);
    }
    inline bool operator>(const CScriptNum &rhs) const {
        return operator>(rhs.m_value);
    }

    inline CScriptNum operator+(const int64_t &rhs) const {
        return CScriptNum(m_value + rhs);
    }
    inline CScriptNum operator-(const int64_t &rhs) const {
        return CScriptNum(m_value - rhs);
    }
    inline CScriptNum operator+(const CScriptNum &rhs) const {
        return operator+(rhs.m_value);
    }
    inline CScriptNum operator-(const CScriptNum &rhs) const {
        return operator-(rhs.m_value);
    }

    inline CScriptNum operator/(const int64_t &rhs) const {
        return CScriptNum(m_value / rhs);
    }
    inline CScriptNum operator/(const CScriptNum &rhs) const {
        return operator/(rhs.m_value);
    }

    inline CScriptNum operator*(const int64_t &rhs) const {
        return CScriptNum(m_value * rhs);
    }
    inline CScriptNum operator*(const CScriptNum &rhs) const {
        return operator*(rhs.m_value);
    }

    inline CScriptNum operator%(const int64_t &rhs) const {
        return CScriptNum(m_value % rhs);
    }
    inline CScriptNum operator%(const CScriptNum &rhs) const {
        return operator%(rhs.m_value);
    }

    inline CScriptNum &operator+=(const CScriptNum &rhs) {
        return operator+=(rhs.m_value);
    }
    inline CScriptNum &operator-=(const CScriptNum &rhs) {
        return operator-=(rhs.m_value);
    }

    inline CScriptNum operator&(const int64_t &rhs) const {
        return CScriptNum(m_value & rhs);
    }
    inline CScriptNum operator&(const CScriptNum &rhs) const {
        return operator&(rhs.m_value);
    }

    inline CScriptNum &operator&=(const CScriptNum &rhs) {
        return operator&=(rhs.m_value);
    }

    inline CScriptNum operator-() const {
        assert(m_value != std::numeric_limits<int64_t>::min());
        return CScriptNum(-m_value);
    }

    inline CScriptNum &operator=(const int64_t &rhs) {
        m_value = rhs;
        return *this;
    }

    inline CScriptNum &operator+=(const int64_t &rhs) {
        assert(
            rhs == 0 ||
            (rhs > 0 && m_value <= std::numeric_limits<int64_t>::max() - rhs) ||
            (rhs < 0 && m_value >= std::numeric_limits<int64_t>::min() - rhs));
        m_value += rhs;
        return *this;
    }

    inline CScriptNum &operator-=(const int64_t &rhs) {
        assert(
            rhs == 0 ||
            (rhs > 0 && m_value >= std::numeric_limits<int64_t>::min() + rhs) ||
            (rhs < 0 && m_value <= std::numeric_limits<int64_t>::max() + rhs));
        m_value -= rhs;
        return *this;
    }

    inline CScriptNum &operator&=(const int64_t &rhs) {
        m_value &= rhs;
        return *this;
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
