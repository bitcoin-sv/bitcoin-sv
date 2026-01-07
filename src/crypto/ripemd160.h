// Copyright (c) 2014-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CRYPTO_RIPEMD160_H
#define BITCOIN_CRYPTO_RIPEMD160_H

#include <array>
#include <cstdint>
#include <cstdlib>
#include <span>

/** A hasher class for RIPEMD-160. */
class CRIPEMD160
{
    std::array<uint32_t, 5> s;
    std::array<uint8_t, 64> buf;
    uint64_t bytes{};

public:
    static const size_t OUTPUT_SIZE = 20;
    using span=std::span<uint8_t, OUTPUT_SIZE>;

    CRIPEMD160();

    CRIPEMD160& Write(const uint8_t* data, size_t len);
    void Finalize(span hash);
    CRIPEMD160& Reset();
};

#endif // BITCOIN_CRYPTO_RIPEMD160_H
