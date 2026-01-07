// Copyright (c) 2014-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CRYPTO_SHA512_H
#define BITCOIN_CRYPTO_SHA512_H

#include <array>
#include <cstdint>
#include <cstdlib>
#include <span>

/** A hasher class for SHA-512. */
class CSHA512
{
    std::array<uint64_t, 8> s;
    std::array<uint8_t, 128> buf;
    uint64_t bytes{};

public:
    static const size_t OUTPUT_SIZE = 64;
    using span = std::span<uint8_t, OUTPUT_SIZE>;

    CSHA512();

    CSHA512& Write(const uint8_t* data, size_t len);
    void Finalize(span hash);
    CSHA512& Reset();
};

#endif // BITCOIN_CRYPTO_SHA512_H
