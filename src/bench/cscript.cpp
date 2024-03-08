// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#include "bench.h"
#include "test/script_macros.h"

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

static void cscript_GetSigOpCount_p2sh_multisig_locking_20(benchmark::State& state)
{
    std::vector<uint8_t> p2sh{P2SH_LOCKING};
    const CScript p2sh_script(begin(p2sh), end(p2sh));
    bool error{false};
    std::vector<uint8_t> ip{OP_PUSHDATA2, 0xac, 0x2, MULTISIG_LOCKING_20}; 
    const CScript redeem_script{begin(ip), end(ip)};
    constexpr bool genesis_enabled{false};
    while(state.KeepRunning())
    {
        p2sh_script.GetSigOpCount(redeem_script, genesis_enabled, error);
    }
}

BENCHMARK(cscript_GetSigOpCount_p2sh_multisig_locking_20);
