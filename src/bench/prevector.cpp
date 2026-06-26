// Copyright (c) 2026 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "bench.h"
#include "prevector.h"

#include <cstdint>

static void prevector_copy_construct(benchmark::State& state, const size_t len)
{
    prevector<28, uint8_t> src(len, uint8_t{0x5a});
    while(state.KeepRunning())
    {
        //NOLINTNEXTLINE(performance-unnecessary-copy-initialization)
        const prevector<28, uint8_t> dst{src};
    }
}

static void prevector_copy_construct28(benchmark::State& state)
{
    prevector_copy_construct(state, 28);
}
BENCHMARK(prevector_copy_construct28)

static void prevector_copy_construct29(benchmark::State& state)
{
    prevector_copy_construct(state, 29);
}
BENCHMARK(prevector_copy_construct29)

static void prevector_copy_construct1024(benchmark::State& state)
{
    prevector_copy_construct(state, 1024);
}
BENCHMARK(prevector_copy_construct1024)

static void prevector_copy_assign(benchmark::State& state, const size_t len)
{
    const prevector<28, uint8_t> src(len, uint8_t{0x5a});
    prevector<28, uint8_t> dst;
    while(state.KeepRunning())
    {
        dst = src;
    }
}

static void prevector_copy_assign28(benchmark::State& state)
{
    prevector_copy_assign(state, 28);
}
BENCHMARK(prevector_copy_assign28)

static void prevector_copy_assign29(benchmark::State& state)
{
    prevector_copy_assign(state, 29);
}
BENCHMARK(prevector_copy_assign29)

static void prevector_copy_assign1024(benchmark::State& state)
{
    prevector_copy_assign(state, 1024);
}
BENCHMARK(prevector_copy_assign1024)
