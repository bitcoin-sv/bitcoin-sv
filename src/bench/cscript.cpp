// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#include "bench.h"

#include "script/script.h"

namespace 
{
    const std::vector<uint8_t> v
    {
        OP_DUP, 
        OP_HASH160, 
        20, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
        OP_EQUALVERIFY,
        OP_CHECKSIG
    };
}

static void cscript_GetSigOpCount(benchmark::State& state)
{
    const CScript script(begin(v), end(v));
    bool b{};
    while(state.KeepRunning())
    {
        script.GetSigOpCount(true, true, b);
    }
}

BENCHMARK(cscript_GetSigOpCount);

