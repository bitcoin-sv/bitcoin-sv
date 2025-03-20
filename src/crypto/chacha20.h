// Copyright (c) 2017 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CRYPTO_CHACHA20_H
#define BITCOIN_CRYPTO_CHACHA20_H

#include <array>
#include <cstdint>
#include <cstdlib>
#include <span>

/** A PRNG class for ChaCha20. */
class ChaCha20
{
    std::array<uint32_t, 16> input;

public:
    ChaCha20();
    ChaCha20(std::span<const uint8_t> key);

    void SetKey(std::span<const uint8_t> key);
    void SetIV(uint64_t iv);
    void Seek(uint64_t pos);
    void Output(uint8_t* output, size_t bytes);
};

#endif // BITCOIN_CRYPTO_CHACHA20_H
