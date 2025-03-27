// Copyright (c) 2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "crypto/hmac_sha256.h"
#include "crypto/sha256.h"

#include <array>
#include <cstring>

CHMAC_SHA256::CHMAC_SHA256(const uint8_t* key, const size_t keylen)
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
    std::array<uint8_t, 64> rkey;
    if(keylen <= rkey.size())
    {
        memcpy(rkey.data(), key, keylen);
        memset(rkey.data() + keylen, 0, rkey.size() - keylen);
    }
    else
    {
        CSHA256()
            .Write(key, keylen)
            .Finalize(CSHA256::span{rkey.data(), CSHA256::OUTPUT_SIZE});
        memset(rkey.data() + 32, 0, 32);
    }

    for(size_t n = 0; n < rkey.size(); n++)
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
        rkey[n] ^= 0x5c;
    outer.Write(rkey.data(), rkey.size());

    for(size_t n = 0; n < rkey.size(); n++)
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
        rkey[n] ^= 0x5c ^ 0x36;
    inner.Write(rkey.data(), rkey.size());
}

void CHMAC_SHA256::Finalize(const span hash)
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
    std::array<uint8_t, 32> temp;
    inner.Finalize(temp);
    outer.Write(temp.data(), temp.size())
         .Finalize(hash);
}
