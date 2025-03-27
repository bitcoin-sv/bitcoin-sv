// Copyright (c) 2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "crypto/hmac_sha512.h"
#include "crypto/sha256.h"

#include <cstring>

// NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays)
CHMAC_SHA512::CHMAC_SHA512(const uint8_t *key, size_t keylen)
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
    std::array<uint8_t, 128> rkey;
    if(keylen <= rkey.size())
    {
        memcpy(rkey.data(), key, keylen);
        memset(rkey.data() + keylen, 0, 128 - keylen);
    }
    else
    {
        CSHA512()
            .Write(key, keylen)
            .Finalize(CSHA512::span{rkey.data(), CSHA512::OUTPUT_SIZE});
        memset(rkey.data() + 64, 0, 64);
    }

    for (int n = 0; n < 128; n++)
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
        rkey[n] ^= 0x5c;
    outer.Write(rkey.data(), 128);

    for (int n = 0; n < 128; n++)
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
        rkey[n] ^= 0x5c ^ 0x36;
    inner.Write(rkey.data(), 128);
}

void CHMAC_SHA512::Finalize(const span hash)
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
    std::array<uint8_t, 64> temp;
    inner.Finalize(temp);
    outer.Write(temp.data(), 64).Finalize(hash);
}

