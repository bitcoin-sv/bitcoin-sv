// Copyright (c) 2014-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CRYPTO_SHA1_H
#define BITCOIN_CRYPTO_SHA1_H

#include <array>
#include <cstdint>
#include <cstdlib>

/** A hasher class for SHA1. */
class CSHA1 {
private:
    std::array<uint32_t, 5> s;
    std::array<uint8_t, 64> buf;
    uint64_t bytes{};

public:
    static const size_t OUTPUT_SIZE = 20;

    CSHA1();
    CSHA1 &Write(const uint8_t *data, size_t len);
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays)
    void Finalize(uint8_t hash[OUTPUT_SIZE]);
    CSHA1 &Reset();
};

#endif // BITCOIN_CRYPTO_SHA1_H
