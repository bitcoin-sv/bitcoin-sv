// Copyright (c) 2014-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CRYPTO_SHA256_H
#define BITCOIN_CRYPTO_SHA256_H

#include <array>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <string>

/** A hasher class for SHA-256. */
class CSHA256
{
    std::array<uint32_t, 8> s;
    std::array<uint8_t, 64> buf;
    uint64_t bytes{};

public:
    static const size_t OUTPUT_SIZE = 32;
    //using span = std::span<uint8_t, OUTPUT_SIZE>;
    using span = std::span<uint8_t>;

    CSHA256();
    CSHA256& Write(const uint8_t* data, size_t len);
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays)
    void Finalize(uint8_t hash[OUTPUT_SIZE]);
    void Finalize(span hash);
    CSHA256& Reset();
};

/**
 * Autodetect the best available SHA256 implementation.
 * Returns the name of the implementation.
 */
std::string SHA256AutoDetect();

#endif // BITCOIN_CRYPTO_SHA256_H
