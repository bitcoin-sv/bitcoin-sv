// Copyright (c) 2016 The Bitcoin Core developers
// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "bench.h"
#include "crypto/ripemd160.h"
#include "crypto/sha1.h"
#include "crypto/sha256.h"
#include "crypto/sha512.h"
#include "hash.h"
#include "random.h"
#include "uint256.h"

/* Number of bytes to hash per iteration */
static const uint64_t BUFFER_SIZE = 1000 * 1000;

static void RIPEMD160(benchmark::State& state)
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
    std::array<uint8_t, CRIPEMD160::OUTPUT_SIZE> hash;
    std::vector<uint8_t> in(BUFFER_SIZE, 0);
    while (state.KeepRunning())
        CRIPEMD160().Write(in.data(), in.size()).Finalize(hash);
}

static void SHA1(benchmark::State& state)
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
    std::array<uint8_t, CSHA1::OUTPUT_SIZE> hash;
    std::vector<uint8_t> in(BUFFER_SIZE, 0);
    while (state.KeepRunning())
        CSHA1().Write(in.data(), in.size()).Finalize(hash);
}

static void SHA256(benchmark::State& state)
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
    std::array<uint8_t, CSHA256::OUTPUT_SIZE> hash;
    std::vector<uint8_t> in(BUFFER_SIZE, 0);
    while (state.KeepRunning())
        CSHA256().Write(in.data(), in.size()).Finalize(hash);
}

static void SHA256_32b(benchmark::State& state)
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
    std::array<uint8_t, CSHA256::OUTPUT_SIZE> in;
    while (state.KeepRunning()) {
        for (int i = 0; i < 1'000'000; i++) {
            CSHA256().Write(in.data(), in.size()).Finalize(in);
        }
    }
}

static void SHA512(benchmark::State& state)
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
    std::array<uint8_t, CSHA512::OUTPUT_SIZE> hash;
    std::vector<uint8_t> in(BUFFER_SIZE, 0);
    while (state.KeepRunning())
        CSHA512().Write(in.data(), in.size()).Finalize(hash);
}

static void SipHash_32b(benchmark::State &state) {
    uint256 x;
    while (state.KeepRunning()) {
        for (int i = 0; i < 1000000; i++) {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
            *reinterpret_cast<uint64_t *>(x.begin()) = SipHashUint256(0, i, x);
        }
    }
}

static void FastRandom_32bit(benchmark::State &state) {
    FastRandomContext rng(true);
    uint32_t x = 0;
    while (state.KeepRunning()) {
        for (int i = 0; i < 1000000; i++) {
            x += rng.rand32();
        }
    }
    (void) x;
}

static void FastRandom_1bit(benchmark::State &state) {
    FastRandomContext rng(true);
    uint32_t x = 0;
    while (state.KeepRunning()) {
        for (int i = 0; i < 1000000; i++) {
            x += rng.randbool();
        }
    }
    (void) x;
}

BENCHMARK(RIPEMD160)
BENCHMARK(SHA1)
BENCHMARK(SHA256)
BENCHMARK(SHA512)

BENCHMARK(SHA256_32b)
BENCHMARK(SipHash_32b)
BENCHMARK(FastRandom_32bit)
BENCHMARK(FastRandom_1bit)
