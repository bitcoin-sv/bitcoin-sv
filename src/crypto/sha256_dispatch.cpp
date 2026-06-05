// Copyright (c) 2014 The Bitcoin Core developers
// Copyright (c) 2026 Bitcoin Association
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Under autotools USE_ASM is only defined in bitcoin-config.h, so this
// include must precede the USE_ASM checks below; without it the SSE4
// path silently compiles out.
#if defined(HAVE_CONFIG_H)
#include "config/bitcoin-config.h"
#endif

#include "crypto/sha256_dispatch.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>

#if defined(__x86_64__) || defined(__amd64__)
#include <cpuid.h>

#if defined(USE_ASM)
#include "crypto/sha256_sse4.h"
#endif

#include "crypto/sha256d64_shani.h"

#endif

static bool SelfTest(sha256_dispatch::StreamType tr)
{
    static const std::array<unsigned char, 65> in1 = {0, 0x80};
    static const std::array<unsigned char, 129> in2 = {
        0,  32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32,   32, 32,
        32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32,   32, 32,
        32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32,   32, 32,
        32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 0x80, 0,  0,
        0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,    0,  0,
        0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,    0,  0,
        0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,    0,  0,
        0,  0,  0,  0,  0,  0,  0,  0,  2,  0};
    static const std::array<uint32_t, 8> init = {
        0x6a09e667ul, 0xbb67ae85ul, 0x3c6ef372ul, 0xa54ff53aul,
        0x510e527ful, 0x9b05688cul, 0x1f83d9abul, 0x5be0cd19ul};
    static const std::array<uint32_t, 8> out1 = {
        0xe3b0c442ul, 0x98fc1c14ul, 0x9afbf4c8ul, 0x996fb924ul,
        0x27ae41e4ul, 0x649b934cul, 0xa495991bul, 0x7852b855ul};
    static const std::array<uint32_t, 8> out2 = {
        0xce4153b0ul, 0x147c2a86ul, 0x3ed4298eul, 0xe0676bc8ul,
        0x79fc77a1ul, 0x2abe1f49ul, 0xb2b055dful, 0x1069523eul};
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
    std::array<uint32_t, 8> buf;
    memcpy(buf.data(), init.data(), sizeof(buf));
    // Process nothing, and check we remain in the initial state.
    tr(std::span<const std::uint8_t>{}, buf);
    if(memcmp(buf.data(), init.data(), sizeof(buf)) != 0)
        return false;

    // Process the padded empty string. in1 has a 1-byte misalignment prefix
    // followed by 64 bytes of test data; subspan<1>() skips the prefix so the
    // transform sees a deliberately unaligned 64-byte block.
    tr(std::span{in1}.subspan<1>(), buf);
    if(memcmp(buf.data(), out1.data(), sizeof(buf)) != 0)
        return false;

    // Process 64 spaces. in2 is similarly prefixed; subspan<1>() yields
    // 128 unaligned bytes (two blocks).
    memcpy(buf.data(), init.data(), sizeof(buf));
    tr(std::span{in2}.subspan<1>(), buf);
    if(memcmp(buf.data(), out2.data(), sizeof(buf)) != 0)
        return false;

    return true;
}

// KAT vectors from bitcoin/bitcoin commit ddb94fd3..., SelfTest() body.
// The byte-1 alignment offset on `data` is deliberate (the lead `-`
// gives the transform an unaligned input).
namespace {
constexpr std::array<const std::uint8_t, 130> kSelfTestD64Data{
    "-"
    "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do "
    "eiusmod tempor incididunt ut labore et dolore magna aliqua. Et m"};

constexpr std::array<unsigned char, 64> kSelfTestD64Expected = {
    0x09, 0x3a, 0xc4, 0xd0, 0x0f, 0xf7, 0x57, 0xe1, 0x72, 0x85, 0x79, 0x42, 0xfe, 0xe7, 0xe0, 0xa0,
    0xfc, 0x52, 0xd7, 0xdb, 0x07, 0x63, 0x45, 0xfb, 0x53, 0x14, 0x7d, 0x17, 0x22, 0x86, 0xf0, 0x52,
    0x48, 0xb6, 0x11, 0x9e, 0x6e, 0x48, 0x81, 0x6d, 0xcc, 0x57, 0x1f, 0xb2, 0x97, 0xa8, 0xd5, 0x25,
    0x9b, 0x82, 0xaa, 0x89, 0xe2, 0xfd, 0x2d, 0x56, 0xe8, 0x28, 0x83, 0x0b, 0xe2, 0xfa, 0x53, 0xb7,
};
}

/** Minimal startup self-test for a TransformD64 candidate.
 *  Catches runtime-environment failures (CPUID lies, broken XSAVE on the
 *  current host) before the dispatcher commits to a faster implementation.
 *  Comprehensive correctness coverage lives in src/test/merkle_tests.cpp. */
static bool SelfTestD64(sha256_dispatch::DBlock2wayType tr)
{
    std::array<std::uint8_t, 64> out{};
    tr(std::span{kSelfTestD64Data}.subspan<1, 128>(), std::span{out});
    return std::equal(out.begin(), out.end(), kSelfTestD64Expected.begin());
}

#if defined(__x86_64__) || defined(__amd64__)

[[maybe_unused]] static bool has_sse41()
{
    uint32_t eax{}, ebx{}, ecx{}, edx{};
    if(__get_cpuid(1, &eax, &ebx, &ecx, &edx) == 0)
        return false;
    return ((ecx >> 19) & 1u) != 0;
}

static bool has_shani()
{
    uint32_t eax{}, ebx{}, ecx{}, edx{};
    if(__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx) == 0)
        return false;
    return (ebx & (1u << 29)) != 0;
}

#endif

std::string sha256_dispatch::AutoDetect()
{
    static const std::string result = [] {
        std::string ret = "standard";

    #if defined(__x86_64__) || defined(__amd64__)

    #if defined(USE_ASM)
        if(has_sse41())
        {
            if(SelfTest(sha256_sse4::Transform))
            {
                stream = sha256_sse4::Transform;
                ret = "sse4";
            }
            else
            {
                ret += ",sse4(selftest-failed)";
            }
        }
    #endif
        // Install SHA-NI 2-way when present; callers fall back to the
        // scalar 1-way path otherwise.
        if(has_sse41() && has_shani())
        {
            if(SelfTestD64(sha256d64_shani::Transform_2way))
            {
                d_block_2way = sha256d64_shani::Transform_2way;
                ret += ",shani(2way)";
            }
            else
            {
                ret += ",shani(selftest-failed)";
            }
        }
    #endif

        assert(SelfTest(stream));
        return ret;
    }();
    return result;
}

