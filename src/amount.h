// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#ifndef BITCOIN_AMOUNT_H
#define BITCOIN_AMOUNT_H

#include "serialize.h"

#include <concepts>
#include <cstdlib>
#include <iostream>
#include <string>

struct Amount {
private:
    int64_t amount_{};

public:
    explicit constexpr Amount(std::integral auto amount) noexcept:
        amount_(amount)
    {}

    Amount() = default;

    // Allow access to underlying value for non-monetary operations
    int64_t GetSatoshis() const { return amount_; }

    /**
     * Implement standard operators
     */
    Amount &operator+=(const Amount& a) {
        amount_ += a.amount_;
        return *this;
    }
    Amount &operator-=(const Amount& a) {
        amount_ -= a.amount_;
        return *this;
    }

    friend auto operator<=>(const Amount&, const Amount&) = default;

    /**
     * Unary minus
     */
    constexpr Amount operator-() const { return Amount(-amount_); }

    /**
     * Addition and subtraction.
     */
    friend constexpr Amount operator+(const Amount a, const Amount b) {
        return Amount(a.amount_ + b.amount_);
    }
    friend constexpr Amount operator-(const Amount a, const Amount b) {
        return a + -b;
    }

    /**
     * Multiplication
     */
    friend constexpr Amount operator*(const int64_t a, const Amount b) {
        return Amount(a * b.amount_);
    }
    friend constexpr Amount operator*(const int a, const Amount b) {
        return Amount(a * b.amount_);
    }

    /**
     * Division
     */
    constexpr int64_t operator/(const Amount b) const {
        return amount_ / b.amount_;
    }
    constexpr Amount operator/(const int64_t b) const {
        return Amount(amount_ / b);
    }
    constexpr Amount operator/(const int b) const { return Amount(amount_ / b); }

    /**
     * Modulus
     */
    constexpr int64_t operator%(const Amount b) const {
        return amount_ % b.amount_;
    }
    constexpr Amount operator%(const int64_t b) const {
        return Amount(amount_ % b);
    }
    constexpr Amount operator%(const int b) const { return Amount(amount_ % b); }

    /**
     * Do not implement double ops to get an error with double and ensure
     * casting to integer is explicit.
     */
    friend constexpr Amount operator*(const double a, const Amount b) = delete;
    constexpr Amount operator/(const double b) const = delete;
    constexpr Amount operator%(const double b) const = delete;

    // ostream support
    friend std::ostream &operator<<(std::ostream &stream, const Amount &ca) {
        return stream << ca.amount_;
    }

    std::string ToString() const;

    // serialization support
    ADD_SERIALIZE_METHODS

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        READWRITE(amount_);
    }
};

static const Amount COIN{100'000'000}; 
static const Amount CENT{1'000'000};

extern const std::string CURRENCY_UNIT;

/**
 * No amount larger than this (in satoshi) is valid.
 *
 * Note that this constant is *not* the total money supply, which in Bitcoin SV
 * currently happens to be less than 21,000,000 BSV for various reasons, but
 * rather a sanity check. As this sanity check is used by consensus-critical
 * validation code, the exact value of the MAX_MONEY constant is consensus
 * critical; in unusual circumstances like a(nother) overflow bug that allowed
 * for the creation of coins out of thin air modification could lead to a fork.
 */
static const Amount MAX_MONEY{21'000'000 * COIN}; // NOLINT(cert-err58-cpp)

inline bool MoneyRange(const Amount nValue) {
    return (nValue >= Amount(0) && nValue <= MAX_MONEY);
}

/**
 * Fee rate in satoshis per kilobyte: Amount / kB
 */
class CFeeRate {
private:
    // unit is satoshis-per-1,000-bytes
    Amount nSatoshisPerK {0};

public:
    CFeeRate () = default;
    explicit CFeeRate(const Amount& _nSatoshisPerK)
        : nSatoshisPerK(_nSatoshisPerK) {}
    /**
     * Constructor for a fee rate in satoshis per kB. The size in bytes must not
     * exceed (2^63 - 1)
     */
    CFeeRate(const Amount nFeePaid, size_t nBytes);
    /**
     * Return the fee in satoshis for the given size in bytes.
     */
    Amount GetFee(size_t nBytes) const;
    /**
     * Return the fee in satoshis for a size of 1000 bytes
     */
    Amount GetFeePerK() const { return nSatoshisPerK; }

    friend auto operator<=>(const CFeeRate&, const CFeeRate&) = default;
    
    CFeeRate &operator+=(const CFeeRate &a) {
        nSatoshisPerK += a.nSatoshisPerK;
        return *this;
    }
    std::string ToString() const;

    ADD_SERIALIZE_METHODS

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        READWRITE(nSatoshisPerK);
    }
};

#endif //  BITCOIN_AMOUNT_H
