// Copyright (c) 2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// Vendored from bitcoin/bitcoin commit
// ddb94fd3e1061c465e425629d6c7d4b292301da5 (src/crypto/sha256.cpp,
// function sha256::TransformD64). Universal scalar fallback used by
// SHA256D64_2way when no SIMD D64 implementation is selected at runtime.
//
// The implementation body of TransformD64 is bit-identical to upstream.
// Local additions are header-level only: the wrapper function
// TransformD64Scalar with span-typed signature for type-level expression
// of the size precondition, and a clang-tidy lint suppression block
// for the vendored pointer-arithmetic style.

#include "crypto/sha256d64_scalar.h"

#include "crypto/common.h"

#include <cstdint>

namespace {

inline uint32_t Ch(uint32_t x, uint32_t y, uint32_t z) {
    return z ^ (x & (y ^ z));
}
inline uint32_t Maj(uint32_t x, uint32_t y, uint32_t z) {
    return (x & y) | (z & (x | y));
}
inline uint32_t Sigma0(uint32_t x) {
    return (x >> 2 | x << 30) ^ (x >> 13 | x << 19) ^ (x >> 22 | x << 10);
}
inline uint32_t Sigma1(uint32_t x) {
    return (x >> 6 | x << 26) ^ (x >> 11 | x << 21) ^ (x >> 25 | x << 7);
}
inline uint32_t sigma0(uint32_t x) {
    return (x >> 7 | x << 25) ^ (x >> 18 | x << 14) ^ (x >> 3);
}
inline uint32_t sigma1(uint32_t x) {
    return (x >> 17 | x << 15) ^ (x >> 19 | x << 13) ^ (x >> 10);
}

/** One round of SHA-256, upstream-compatible 9-arg form where the
 *  last argument is the pre-summed (k + w). */
inline void Round(uint32_t a, uint32_t b, uint32_t c, uint32_t& d,
                  uint32_t e, uint32_t f, uint32_t g, uint32_t& h,
                  uint32_t k_plus_w) {
    uint32_t t1 = h + Sigma1(e) + Ch(e, f, g) + k_plus_w;
    uint32_t t2 = Sigma0(a) + Maj(a, b, c);
    d += t1;
    h = t1 + t2;
}

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
// NOLINTBEGIN(cppcoreguidelines-init-variables)

static void TransformBody(unsigned char* out, const unsigned char* in)
{
    // Transform 1
    uint32_t a = 0x6a09e667ul;
    uint32_t b = 0xbb67ae85ul;
    uint32_t c = 0x3c6ef372ul;
    uint32_t d = 0xa54ff53aul;
    uint32_t e = 0x510e527ful;
    uint32_t f = 0x9b05688cul;
    uint32_t g = 0x1f83d9abul;
    uint32_t h = 0x5be0cd19ul;

    uint32_t w0, w1, w2, w3, w4, w5, w6, w7, w8, w9, w10, w11, w12, w13, w14, w15;

    Round(a, b, c, d, e, f, g, h, 0x428a2f98ul + (w0 = ReadBE32(in + 0)));
    Round(h, a, b, c, d, e, f, g, 0x71374491ul + (w1 = ReadBE32(in + 4)));
    Round(g, h, a, b, c, d, e, f, 0xb5c0fbcful + (w2 = ReadBE32(in + 8)));
    Round(f, g, h, a, b, c, d, e, 0xe9b5dba5ul + (w3 = ReadBE32(in + 12)));
    Round(e, f, g, h, a, b, c, d, 0x3956c25bul + (w4 = ReadBE32(in + 16)));
    Round(d, e, f, g, h, a, b, c, 0x59f111f1ul + (w5 = ReadBE32(in + 20)));
    Round(c, d, e, f, g, h, a, b, 0x923f82a4ul + (w6 = ReadBE32(in + 24)));
    Round(b, c, d, e, f, g, h, a, 0xab1c5ed5ul + (w7 = ReadBE32(in + 28)));
    Round(a, b, c, d, e, f, g, h, 0xd807aa98ul + (w8 = ReadBE32(in + 32)));
    Round(h, a, b, c, d, e, f, g, 0x12835b01ul + (w9 = ReadBE32(in + 36)));
    Round(g, h, a, b, c, d, e, f, 0x243185beul + (w10 = ReadBE32(in + 40)));
    Round(f, g, h, a, b, c, d, e, 0x550c7dc3ul + (w11 = ReadBE32(in + 44)));
    Round(e, f, g, h, a, b, c, d, 0x72be5d74ul + (w12 = ReadBE32(in + 48)));
    Round(d, e, f, g, h, a, b, c, 0x80deb1feul + (w13 = ReadBE32(in + 52)));
    Round(c, d, e, f, g, h, a, b, 0x9bdc06a7ul + (w14 = ReadBE32(in + 56)));
    Round(b, c, d, e, f, g, h, a, 0xc19bf174ul + (w15 = ReadBE32(in + 60)));
    Round(a, b, c, d, e, f, g, h, 0xe49b69c1ul + (w0 += sigma1(w14) + w9 + sigma0(w1)));
    Round(h, a, b, c, d, e, f, g, 0xefbe4786ul + (w1 += sigma1(w15) + w10 + sigma0(w2)));
    Round(g, h, a, b, c, d, e, f, 0x0fc19dc6ul + (w2 += sigma1(w0) + w11 + sigma0(w3)));
    Round(f, g, h, a, b, c, d, e, 0x240ca1ccul + (w3 += sigma1(w1) + w12 + sigma0(w4)));
    Round(e, f, g, h, a, b, c, d, 0x2de92c6ful + (w4 += sigma1(w2) + w13 + sigma0(w5)));
    Round(d, e, f, g, h, a, b, c, 0x4a7484aaul + (w5 += sigma1(w3) + w14 + sigma0(w6)));
    Round(c, d, e, f, g, h, a, b, 0x5cb0a9dcul + (w6 += sigma1(w4) + w15 + sigma0(w7)));
    Round(b, c, d, e, f, g, h, a, 0x76f988daul + (w7 += sigma1(w5) + w0 + sigma0(w8)));
    Round(a, b, c, d, e, f, g, h, 0x983e5152ul + (w8 += sigma1(w6) + w1 + sigma0(w9)));
    Round(h, a, b, c, d, e, f, g, 0xa831c66dul + (w9 += sigma1(w7) + w2 + sigma0(w10)));
    Round(g, h, a, b, c, d, e, f, 0xb00327c8ul + (w10 += sigma1(w8) + w3 + sigma0(w11)));
    Round(f, g, h, a, b, c, d, e, 0xbf597fc7ul + (w11 += sigma1(w9) + w4 + sigma0(w12)));
    Round(e, f, g, h, a, b, c, d, 0xc6e00bf3ul + (w12 += sigma1(w10) + w5 + sigma0(w13)));
    Round(d, e, f, g, h, a, b, c, 0xd5a79147ul + (w13 += sigma1(w11) + w6 + sigma0(w14)));
    Round(c, d, e, f, g, h, a, b, 0x06ca6351ul + (w14 += sigma1(w12) + w7 + sigma0(w15)));
    Round(b, c, d, e, f, g, h, a, 0x14292967ul + (w15 += sigma1(w13) + w8 + sigma0(w0)));
    Round(a, b, c, d, e, f, g, h, 0x27b70a85ul + (w0 += sigma1(w14) + w9 + sigma0(w1)));
    Round(h, a, b, c, d, e, f, g, 0x2e1b2138ul + (w1 += sigma1(w15) + w10 + sigma0(w2)));
    Round(g, h, a, b, c, d, e, f, 0x4d2c6dfcul + (w2 += sigma1(w0) + w11 + sigma0(w3)));
    Round(f, g, h, a, b, c, d, e, 0x53380d13ul + (w3 += sigma1(w1) + w12 + sigma0(w4)));
    Round(e, f, g, h, a, b, c, d, 0x650a7354ul + (w4 += sigma1(w2) + w13 + sigma0(w5)));
    Round(d, e, f, g, h, a, b, c, 0x766a0abbul + (w5 += sigma1(w3) + w14 + sigma0(w6)));
    Round(c, d, e, f, g, h, a, b, 0x81c2c92eul + (w6 += sigma1(w4) + w15 + sigma0(w7)));
    Round(b, c, d, e, f, g, h, a, 0x92722c85ul + (w7 += sigma1(w5) + w0 + sigma0(w8)));
    Round(a, b, c, d, e, f, g, h, 0xa2bfe8a1ul + (w8 += sigma1(w6) + w1 + sigma0(w9)));
    Round(h, a, b, c, d, e, f, g, 0xa81a664bul + (w9 += sigma1(w7) + w2 + sigma0(w10)));
    Round(g, h, a, b, c, d, e, f, 0xc24b8b70ul + (w10 += sigma1(w8) + w3 + sigma0(w11)));
    Round(f, g, h, a, b, c, d, e, 0xc76c51a3ul + (w11 += sigma1(w9) + w4 + sigma0(w12)));
    Round(e, f, g, h, a, b, c, d, 0xd192e819ul + (w12 += sigma1(w10) + w5 + sigma0(w13)));
    Round(d, e, f, g, h, a, b, c, 0xd6990624ul + (w13 += sigma1(w11) + w6 + sigma0(w14)));
    Round(c, d, e, f, g, h, a, b, 0xf40e3585ul + (w14 += sigma1(w12) + w7 + sigma0(w15)));
    Round(b, c, d, e, f, g, h, a, 0x106aa070ul + (w15 += sigma1(w13) + w8 + sigma0(w0)));
    Round(a, b, c, d, e, f, g, h, 0x19a4c116ul + (w0 += sigma1(w14) + w9 + sigma0(w1)));
    Round(h, a, b, c, d, e, f, g, 0x1e376c08ul + (w1 += sigma1(w15) + w10 + sigma0(w2)));
    Round(g, h, a, b, c, d, e, f, 0x2748774cul + (w2 += sigma1(w0) + w11 + sigma0(w3)));
    Round(f, g, h, a, b, c, d, e, 0x34b0bcb5ul + (w3 += sigma1(w1) + w12 + sigma0(w4)));
    Round(e, f, g, h, a, b, c, d, 0x391c0cb3ul + (w4 += sigma1(w2) + w13 + sigma0(w5)));
    Round(d, e, f, g, h, a, b, c, 0x4ed8aa4aul + (w5 += sigma1(w3) + w14 + sigma0(w6)));
    Round(c, d, e, f, g, h, a, b, 0x5b9cca4ful + (w6 += sigma1(w4) + w15 + sigma0(w7)));
    Round(b, c, d, e, f, g, h, a, 0x682e6ff3ul + (w7 += sigma1(w5) + w0 + sigma0(w8)));
    Round(a, b, c, d, e, f, g, h, 0x748f82eeul + (w8 += sigma1(w6) + w1 + sigma0(w9)));
    Round(h, a, b, c, d, e, f, g, 0x78a5636ful + (w9 += sigma1(w7) + w2 + sigma0(w10)));
    Round(g, h, a, b, c, d, e, f, 0x84c87814ul + (w10 += sigma1(w8) + w3 + sigma0(w11)));
    Round(f, g, h, a, b, c, d, e, 0x8cc70208ul + (w11 += sigma1(w9) + w4 + sigma0(w12)));
    Round(e, f, g, h, a, b, c, d, 0x90befffaul + (w12 += sigma1(w10) + w5 + sigma0(w13)));
    Round(d, e, f, g, h, a, b, c, 0xa4506cebul + (w13 += sigma1(w11) + w6 + sigma0(w14)));
    Round(c, d, e, f, g, h, a, b, 0xbef9a3f7ul + (w14 + sigma1(w12) + w7 + sigma0(w15)));
    Round(b, c, d, e, f, g, h, a, 0xc67178f2ul + (w15 + sigma1(w13) + w8 + sigma0(w0)));

    a += 0x6a09e667ul;
    b += 0xbb67ae85ul;
    c += 0x3c6ef372ul;
    d += 0xa54ff53aul;
    e += 0x510e527ful;
    f += 0x9b05688cul;
    g += 0x1f83d9abul;
    h += 0x5be0cd19ul;

    uint32_t t0 = a, t1 = b, t2 = c, t3 = d, t4 = e, t5 = f, t6 = g, t7 = h;

    // Transform 2
    Round(a, b, c, d, e, f, g, h, 0xc28a2f98ul);
    Round(h, a, b, c, d, e, f, g, 0x71374491ul);
    Round(g, h, a, b, c, d, e, f, 0xb5c0fbcful);
    Round(f, g, h, a, b, c, d, e, 0xe9b5dba5ul);
    Round(e, f, g, h, a, b, c, d, 0x3956c25bul);
    Round(d, e, f, g, h, a, b, c, 0x59f111f1ul);
    Round(c, d, e, f, g, h, a, b, 0x923f82a4ul);
    Round(b, c, d, e, f, g, h, a, 0xab1c5ed5ul);
    Round(a, b, c, d, e, f, g, h, 0xd807aa98ul);
    Round(h, a, b, c, d, e, f, g, 0x12835b01ul);
    Round(g, h, a, b, c, d, e, f, 0x243185beul);
    Round(f, g, h, a, b, c, d, e, 0x550c7dc3ul);
    Round(e, f, g, h, a, b, c, d, 0x72be5d74ul);
    Round(d, e, f, g, h, a, b, c, 0x80deb1feul);
    Round(c, d, e, f, g, h, a, b, 0x9bdc06a7ul);
    Round(b, c, d, e, f, g, h, a, 0xc19bf374ul);
    Round(a, b, c, d, e, f, g, h, 0x649b69c1ul);
    Round(h, a, b, c, d, e, f, g, 0xf0fe4786ul);
    Round(g, h, a, b, c, d, e, f, 0x0fe1edc6ul);
    Round(f, g, h, a, b, c, d, e, 0x240cf254ul);
    Round(e, f, g, h, a, b, c, d, 0x4fe9346ful);
    Round(d, e, f, g, h, a, b, c, 0x6cc984beul);
    Round(c, d, e, f, g, h, a, b, 0x61b9411eul);
    Round(b, c, d, e, f, g, h, a, 0x16f988faul);
    Round(a, b, c, d, e, f, g, h, 0xf2c65152ul);
    Round(h, a, b, c, d, e, f, g, 0xa88e5a6dul);
    Round(g, h, a, b, c, d, e, f, 0xb019fc65ul);
    Round(f, g, h, a, b, c, d, e, 0xb9d99ec7ul);
    Round(e, f, g, h, a, b, c, d, 0x9a1231c3ul);
    Round(d, e, f, g, h, a, b, c, 0xe70eeaa0ul);
    Round(c, d, e, f, g, h, a, b, 0xfdb1232bul);
    Round(b, c, d, e, f, g, h, a, 0xc7353eb0ul);
    Round(a, b, c, d, e, f, g, h, 0x3069bad5ul);
    Round(h, a, b, c, d, e, f, g, 0xcb976d5ful);
    Round(g, h, a, b, c, d, e, f, 0x5a0f118ful);
    Round(f, g, h, a, b, c, d, e, 0xdc1eeefdul);
    Round(e, f, g, h, a, b, c, d, 0x0a35b689ul);
    Round(d, e, f, g, h, a, b, c, 0xde0b7a04ul);
    Round(c, d, e, f, g, h, a, b, 0x58f4ca9dul);
    Round(b, c, d, e, f, g, h, a, 0xe15d5b16ul);
    Round(a, b, c, d, e, f, g, h, 0x007f3e86ul);
    Round(h, a, b, c, d, e, f, g, 0x37088980ul);
    Round(g, h, a, b, c, d, e, f, 0xa507ea32ul);
    Round(f, g, h, a, b, c, d, e, 0x6fab9537ul);
    Round(e, f, g, h, a, b, c, d, 0x17406110ul);
    Round(d, e, f, g, h, a, b, c, 0x0d8cd6f1ul);
    Round(c, d, e, f, g, h, a, b, 0xcdaa3b6dul);
    Round(b, c, d, e, f, g, h, a, 0xc0bbbe37ul);
    Round(a, b, c, d, e, f, g, h, 0x83613bdaul);
    Round(h, a, b, c, d, e, f, g, 0xdb48a363ul);
    Round(g, h, a, b, c, d, e, f, 0x0b02e931ul);
    Round(f, g, h, a, b, c, d, e, 0x6fd15ca7ul);
    Round(e, f, g, h, a, b, c, d, 0x521afacaul);
    Round(d, e, f, g, h, a, b, c, 0x31338431ul);
    Round(c, d, e, f, g, h, a, b, 0x6ed41a95ul);
    Round(b, c, d, e, f, g, h, a, 0x6d437890ul);
    Round(a, b, c, d, e, f, g, h, 0xc39c91f2ul);
    Round(h, a, b, c, d, e, f, g, 0x9eccabbdul);
    Round(g, h, a, b, c, d, e, f, 0xb5c9a0e6ul);
    Round(f, g, h, a, b, c, d, e, 0x532fb63cul);
    Round(e, f, g, h, a, b, c, d, 0xd2c741c6ul);
    Round(d, e, f, g, h, a, b, c, 0x07237ea3ul);
    Round(c, d, e, f, g, h, a, b, 0xa4954b68ul);
    Round(b, c, d, e, f, g, h, a, 0x4c191d76ul);

    w0 = t0 + a;
    w1 = t1 + b;
    w2 = t2 + c;
    w3 = t3 + d;
    w4 = t4 + e;
    w5 = t5 + f;
    w6 = t6 + g;
    w7 = t7 + h;

    // Transform 3
    a = 0x6a09e667ul;
    b = 0xbb67ae85ul;
    c = 0x3c6ef372ul;
    d = 0xa54ff53aul;
    e = 0x510e527ful;
    f = 0x9b05688cul;
    g = 0x1f83d9abul;
    h = 0x5be0cd19ul;

    Round(a, b, c, d, e, f, g, h, 0x428a2f98ul + w0);
    Round(h, a, b, c, d, e, f, g, 0x71374491ul + w1);
    Round(g, h, a, b, c, d, e, f, 0xb5c0fbcful + w2);
    Round(f, g, h, a, b, c, d, e, 0xe9b5dba5ul + w3);
    Round(e, f, g, h, a, b, c, d, 0x3956c25bul + w4);
    Round(d, e, f, g, h, a, b, c, 0x59f111f1ul + w5);
    Round(c, d, e, f, g, h, a, b, 0x923f82a4ul + w6);
    Round(b, c, d, e, f, g, h, a, 0xab1c5ed5ul + w7);
    Round(a, b, c, d, e, f, g, h, 0x5807aa98ul);
    Round(h, a, b, c, d, e, f, g, 0x12835b01ul);
    Round(g, h, a, b, c, d, e, f, 0x243185beul);
    Round(f, g, h, a, b, c, d, e, 0x550c7dc3ul);
    Round(e, f, g, h, a, b, c, d, 0x72be5d74ul);
    Round(d, e, f, g, h, a, b, c, 0x80deb1feul);
    Round(c, d, e, f, g, h, a, b, 0x9bdc06a7ul);
    Round(b, c, d, e, f, g, h, a, 0xc19bf274ul);
    Round(a, b, c, d, e, f, g, h, 0xe49b69c1ul + (w0 += sigma0(w1)));
    Round(h, a, b, c, d, e, f, g, 0xefbe4786ul + (w1 += 0xa00000ul + sigma0(w2)));
    Round(g, h, a, b, c, d, e, f, 0x0fc19dc6ul + (w2 += sigma1(w0) + sigma0(w3)));
    Round(f, g, h, a, b, c, d, e, 0x240ca1ccul + (w3 += sigma1(w1) + sigma0(w4)));
    Round(e, f, g, h, a, b, c, d, 0x2de92c6ful + (w4 += sigma1(w2) + sigma0(w5)));
    Round(d, e, f, g, h, a, b, c, 0x4a7484aaul + (w5 += sigma1(w3) + sigma0(w6)));
    Round(c, d, e, f, g, h, a, b, 0x5cb0a9dcul + (w6 += sigma1(w4) + 0x100ul + sigma0(w7)));
    Round(b, c, d, e, f, g, h, a, 0x76f988daul + (w7 += sigma1(w5) + w0 + 0x11002000ul));
    Round(a, b, c, d, e, f, g, h, 0x983e5152ul + (w8 = 0x80000000ul + sigma1(w6) + w1));
    Round(h, a, b, c, d, e, f, g, 0xa831c66dul + (w9 = sigma1(w7) + w2));
    Round(g, h, a, b, c, d, e, f, 0xb00327c8ul + (w10 = sigma1(w8) + w3));
    Round(f, g, h, a, b, c, d, e, 0xbf597fc7ul + (w11 = sigma1(w9) + w4));
    Round(e, f, g, h, a, b, c, d, 0xc6e00bf3ul + (w12 = sigma1(w10) + w5));
    Round(d, e, f, g, h, a, b, c, 0xd5a79147ul + (w13 = sigma1(w11) + w6));
    Round(c, d, e, f, g, h, a, b, 0x06ca6351ul + (w14 = sigma1(w12) + w7 + 0x400022ul));
    Round(b, c, d, e, f, g, h, a, 0x14292967ul + (w15 = 0x100ul + sigma1(w13) + w8 + sigma0(w0)));
    Round(a, b, c, d, e, f, g, h, 0x27b70a85ul + (w0 += sigma1(w14) + w9 + sigma0(w1)));
    Round(h, a, b, c, d, e, f, g, 0x2e1b2138ul + (w1 += sigma1(w15) + w10 + sigma0(w2)));
    Round(g, h, a, b, c, d, e, f, 0x4d2c6dfcul + (w2 += sigma1(w0) + w11 + sigma0(w3)));
    Round(f, g, h, a, b, c, d, e, 0x53380d13ul + (w3 += sigma1(w1) + w12 + sigma0(w4)));
    Round(e, f, g, h, a, b, c, d, 0x650a7354ul + (w4 += sigma1(w2) + w13 + sigma0(w5)));
    Round(d, e, f, g, h, a, b, c, 0x766a0abbul + (w5 += sigma1(w3) + w14 + sigma0(w6)));
    Round(c, d, e, f, g, h, a, b, 0x81c2c92eul + (w6 += sigma1(w4) + w15 + sigma0(w7)));
    Round(b, c, d, e, f, g, h, a, 0x92722c85ul + (w7 += sigma1(w5) + w0 + sigma0(w8)));
    Round(a, b, c, d, e, f, g, h, 0xa2bfe8a1ul + (w8 += sigma1(w6) + w1 + sigma0(w9)));
    Round(h, a, b, c, d, e, f, g, 0xa81a664bul + (w9 += sigma1(w7) + w2 + sigma0(w10)));
    Round(g, h, a, b, c, d, e, f, 0xc24b8b70ul + (w10 += sigma1(w8) + w3 + sigma0(w11)));
    Round(f, g, h, a, b, c, d, e, 0xc76c51a3ul + (w11 += sigma1(w9) + w4 + sigma0(w12)));
    Round(e, f, g, h, a, b, c, d, 0xd192e819ul + (w12 += sigma1(w10) + w5 + sigma0(w13)));
    Round(d, e, f, g, h, a, b, c, 0xd6990624ul + (w13 += sigma1(w11) + w6 + sigma0(w14)));
    Round(c, d, e, f, g, h, a, b, 0xf40e3585ul + (w14 += sigma1(w12) + w7 + sigma0(w15)));
    Round(b, c, d, e, f, g, h, a, 0x106aa070ul + (w15 += sigma1(w13) + w8 + sigma0(w0)));
    Round(a, b, c, d, e, f, g, h, 0x19a4c116ul + (w0 += sigma1(w14) + w9 + sigma0(w1)));
    Round(h, a, b, c, d, e, f, g, 0x1e376c08ul + (w1 += sigma1(w15) + w10 + sigma0(w2)));
    Round(g, h, a, b, c, d, e, f, 0x2748774cul + (w2 += sigma1(w0) + w11 + sigma0(w3)));
    Round(f, g, h, a, b, c, d, e, 0x34b0bcb5ul + (w3 += sigma1(w1) + w12 + sigma0(w4)));
    Round(e, f, g, h, a, b, c, d, 0x391c0cb3ul + (w4 += sigma1(w2) + w13 + sigma0(w5)));
    Round(d, e, f, g, h, a, b, c, 0x4ed8aa4aul + (w5 += sigma1(w3) + w14 + sigma0(w6)));
    Round(c, d, e, f, g, h, a, b, 0x5b9cca4ful + (w6 += sigma1(w4) + w15 + sigma0(w7)));
    Round(b, c, d, e, f, g, h, a, 0x682e6ff3ul + (w7 += sigma1(w5) + w0 + sigma0(w8)));
    Round(a, b, c, d, e, f, g, h, 0x748f82eeul + (w8 += sigma1(w6) + w1 + sigma0(w9)));
    Round(h, a, b, c, d, e, f, g, 0x78a5636ful + (w9 += sigma1(w7) + w2 + sigma0(w10)));
    Round(g, h, a, b, c, d, e, f, 0x84c87814ul + (w10 += sigma1(w8) + w3 + sigma0(w11)));
    Round(f, g, h, a, b, c, d, e, 0x8cc70208ul + (w11 += sigma1(w9) + w4 + sigma0(w12)));
    Round(e, f, g, h, a, b, c, d, 0x90befffaul + (w12 += sigma1(w10) + w5 + sigma0(w13)));
    Round(d, e, f, g, h, a, b, c, 0xa4506cebul + (w13 += sigma1(w11) + w6 + sigma0(w14)));
    Round(c, d, e, f, g, h, a, b, 0xbef9a3f7ul + (w14 + sigma1(w12) + w7 + sigma0(w15)));
    Round(b, c, d, e, f, g, h, a, 0xc67178f2ul + (w15 + sigma1(w13) + w8 + sigma0(w0)));

    // Output
    WriteBE32(out + 0, a + 0x6a09e667ul);
    WriteBE32(out + 4, b + 0xbb67ae85ul);
    WriteBE32(out + 8, c + 0x3c6ef372ul);
    WriteBE32(out + 12, d + 0xa54ff53aul);
    WriteBE32(out + 16, e + 0x510e527ful);
    WriteBE32(out + 20, f + 0x9b05688cul);
    WriteBE32(out + 24, g + 0x1f83d9abul);
    WriteBE32(out + 28, h + 0x5be0cd19ul);
}

// NOLINTEND(cppcoreguidelines-init-variables)
// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)

}  // anonymous namespace

void sha256d64_scalar::Transform(const std::span<const std::uint8_t, 64> ip,
                                 std::span<std::uint8_t, 32> op)
{
    TransformBody(op.data(), ip.data());
}
