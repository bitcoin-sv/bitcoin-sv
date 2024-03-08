// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#include "bench.h"

#include "config.h"
#include "script/interpreter.h"
#include "script/script.h"
#include "taskcancellation.h"

using namespace std;

static void interpreter_lshift_int32_max_minus_1(benchmark::State& state)
{
    std::vector<uint8_t> data(INT32_MAX / 8, 0x0);

    auto source = task::CCancellationSource::Make();
    LimitedStack stack = LimitedStack({data}, INT64_MAX);
    const auto flags{SCRIPT_UTXO_AFTER_GENESIS};
    ScriptError err;
    while(state.KeepRunning())
    {
        EvalScript(GlobalConfig::GetConfig(), true, source->GetToken(), stack,
                   CScript() << INT32_MAX - 1 << OP_LSHIFT, flags,
                   BaseSignatureChecker{}, &err);
    }
}
BENCHMARK(interpreter_lshift_int32_max_minus_1)

static void interpreter_rshift_int32_max_minus_1(benchmark::State& state)
{
    std::vector<uint8_t> data(INT32_MAX / 8, 0x0);

    auto source = task::CCancellationSource::Make();
    LimitedStack stack = LimitedStack({data}, INT64_MAX);
    const auto flags{SCRIPT_UTXO_AFTER_GENESIS};
    ScriptError err;
    while(state.KeepRunning())
    {
        EvalScript(GlobalConfig::GetConfig(), true, source->GetToken(), stack,
                   CScript() << INT32_MAX - 1 << OP_RSHIFT, flags,
                   BaseSignatureChecker{}, &err);
    }
}
BENCHMARK(interpreter_rshift_int32_max_minus_1)

static void interpreter_lshift_6m_minus_1(benchmark::State& state)
{
    constexpr vector<uint8_t>::size_type size{750'000};
    std::vector<uint8_t> data(size, 0x0);

    auto source = task::CCancellationSource::Make();
    LimitedStack stack = LimitedStack({data}, INT64_MAX);
    const auto flags{SCRIPT_UTXO_AFTER_GENESIS};
    ScriptError err;
    while(state.KeepRunning())
    {
        EvalScript(GlobalConfig::GetConfig(), true, source->GetToken(), stack,
                   CScript() << (size*8) - 1 << OP_LSHIFT, flags,
                   BaseSignatureChecker{}, &err);
    }
}
BENCHMARK(interpreter_lshift_6m_minus_1)

static void interpreter_rshift_6m_minus_1(benchmark::State& state)
{
    constexpr vector<uint8_t>::size_type size{750'000};
    std::vector<uint8_t> data(size, 0x0);

    auto source = task::CCancellationSource::Make();
    LimitedStack stack = LimitedStack({data}, INT64_MAX);
    const auto flags{SCRIPT_UTXO_AFTER_GENESIS};
    ScriptError err;
    while(state.KeepRunning())
    {
        EvalScript(GlobalConfig::GetConfig(), true, source->GetToken(), stack,
                   CScript() << (size*8) - 1 << OP_RSHIFT, flags,
                   BaseSignatureChecker{}, &err);
    }
}
BENCHMARK(interpreter_rshift_6m_minus_1)
