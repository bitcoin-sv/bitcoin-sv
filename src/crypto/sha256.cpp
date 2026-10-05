// Copyright (c) 2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "crypto/sha256.h"

#include "crypto/common.h"
#include "crypto/sha256_dispatch.h"

#include <cassert>

/** Initialize the SHA-256 state to the FIPS 180-4 IV. */
static void Initialize(const std::span<uint32_t, 8> s)
{
    s[0] = 0x6a09e667ul;
    s[1] = 0xbb67ae85ul;
    s[2] = 0x3c6ef372ul;
    s[3] = 0xa54ff53aul;
    s[4] = 0x510e527ful;
    s[5] = 0x9b05688cul;
    s[6] = 0x1f83d9abul;
    s[7] = 0x5be0cd19ul;
}

// NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
CSHA256::CSHA256()
{
    Initialize(s);
}

CSHA256& CSHA256::Write(const uint8_t* data, size_t len)
{
    if(len == 0)
        return *this;

    assert(data);
    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    const uint8_t* end = data + len;
    size_t bufsize = bytes % 64;
    if(bufsize && bufsize + len >= 64)
    {
        // Fill the buffer, and process it.
        memcpy(buf.data() + bufsize, data, 64 - bufsize);
        bytes += 64 - bufsize;
        data += 64 - bufsize;
        sha256_dispatch::stream(buf, s);
        bufsize = 0;
    }
    if(end - data >= 64)
    {
        size_t blocks = (end - data) / 64;
        sha256_dispatch::stream(std::span<const std::uint8_t>{data, blocks * 64}, s);
        data += 64 * blocks;
        bytes += 64 * blocks;
    }
    // NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)

    if(end > data)
    {
        // Fill the buffer with what remains.
        //NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        memcpy(buf.data() + bufsize, data, end - data);
        bytes += end - data;
    }
    return *this;
}

void CSHA256::Finalize(const span hash)
{
    static const std::array<uint8_t, 64> pad = {0x80};
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
    std::array<uint8_t, 8> sizedesc;
    WriteBE64(sizedesc.data(), bytes << 3);
    Write(pad.data(), 1 + ((119 - (bytes % 64)) % 64));
    Write(sizedesc.data(), 8);
    WriteBE32(hash.data(), s[0]);
    //NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    WriteBE32(hash.data() + 4, s[1]);
    WriteBE32(hash.data() + 8, s[2]);
    WriteBE32(hash.data() + 12, s[3]);
    WriteBE32(hash.data() + 16, s[4]);
    WriteBE32(hash.data() + 20, s[5]);
    WriteBE32(hash.data() + 24, s[6]);
    WriteBE32(hash.data() + 28, s[7]);
    //NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
}

CSHA256& CSHA256::Reset()
{
    bytes = 0;
    Initialize(s);
    return *this;
}

