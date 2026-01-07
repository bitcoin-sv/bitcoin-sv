// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#include "bench.h"

#include "config.h"
#include "script/interpreter.h"
#include "script/script.h"
#include "taskcancellation.h"

#include <climits>
#include <consensus/consensus.h>
#include <cstdint>

using namespace std;

static void interpreter_int32_max_1_lshift(benchmark::State& state)
{
    auto source = task::CCancellationSource::Make();

    const std::vector<uint8_t> data(INT32_MAX / 8, 0x0);
    const auto flags{SCRIPT_UTXO_AFTER_GENESIS};
    const auto params{make_eval_script_params(GlobalConfig::GetConfig().GetConfigScriptPolicy(), flags, false)};
    while(state.KeepRunning())
    {
        LimitedStack stack = LimitedStack({data}, INT64_MAX);
        EvalScript(params,
                   source->GetToken(),
                   stack,
                   CScript() << OP_1 << OP_LSHIFT,
                   flags,
                   BaseSignatureChecker{});
    }
}
BENCHMARK(interpreter_int32_max_1_lshift)

static void interpreter_int32_max_minus_1_lshift(benchmark::State& state)
{
    auto source = task::CCancellationSource::Make();

    const std::vector<uint8_t> data(INT32_MAX / 8, 0x0);
    const auto flags{SCRIPT_UTXO_AFTER_GENESIS};
    const auto params{make_eval_script_params(GlobalConfig::GetConfig().GetConfigScriptPolicy(), flags, false)};
    while(state.KeepRunning())
    {
        LimitedStack stack = LimitedStack({data}, INT64_MAX);
        EvalScript(params,
                   source->GetToken(),
                   stack,
                   CScript() << INT32_MAX - 1 << OP_LSHIFT,
                   flags,
                   BaseSignatureChecker{});
    }
}
BENCHMARK(interpreter_int32_max_minus_1_lshift)

static void interpreter_int32_max_minus_1_rshift(benchmark::State& state)
{
    auto source = task::CCancellationSource::Make();

    const std::vector<uint8_t> data(INT32_MAX / 8, 0x0);
    const auto flags{SCRIPT_UTXO_AFTER_GENESIS};
    const auto params{make_eval_script_params(GlobalConfig::GetConfig().GetConfigScriptPolicy(), flags, false)};
    while(state.KeepRunning())
    {
        LimitedStack stack = LimitedStack({data}, INT64_MAX);
        EvalScript(params,
                   source->GetToken(),
                   stack,
                   CScript() << INT32_MAX - 1 << OP_RSHIFT,
                   flags,
                   BaseSignatureChecker{});
    }
}
BENCHMARK(interpreter_int32_max_minus_1_rshift)

static void interpreter_6m_1_lshift(benchmark::State& state)
{
    constexpr vector<uint8_t>::size_type size{750'000};
    const std::vector<uint8_t> data(size, 0x0);

    auto source = task::CCancellationSource::Make();
    const auto flags{SCRIPT_UTXO_AFTER_GENESIS};
    const auto params{make_eval_script_params(GlobalConfig::GetConfig().GetConfigScriptPolicy(), flags, false)};
    while(state.KeepRunning())
    {
        LimitedStack stack = LimitedStack({data}, INT64_MAX);
        EvalScript(params,
                   source->GetToken(),
                   stack,
                   CScript() << OP_1 << OP_LSHIFT,
                   flags,
                   BaseSignatureChecker{});
    }
}
BENCHMARK(interpreter_6m_1_lshift)

static void interpreter_max_1_lshiftnum(benchmark::State& state)
{
    constexpr vector<uint8_t>::size_type size{MAX_SCRIPT_NUM_LENGTH_AFTER_CHRONICLE};
    const std::vector<uint8_t> data(size, 0x0);

    auto source = task::CCancellationSource::Make();
    const auto flags{SCRIPT_UTXO_AFTER_GENESIS | SCRIPT_UTXO_AFTER_CHRONICLE};
    const auto params{make_eval_script_params(GlobalConfig::GetConfig().GetConfigScriptPolicy(),
                                              flags,
                                              false)};
    while(state.KeepRunning())
    {
        LimitedStack stack = LimitedStack({data}, INT64_MAX);
        EvalScript(params,
                   source->GetToken(),
                   stack,
                   CScript() << OP_1 << OP_LSHIFTNUM,
                   flags,
                   BaseSignatureChecker{});
    }
}
BENCHMARK(interpreter_max_1_lshiftnum)

static void interpreter_6m_minus_1_lshift(benchmark::State& state)
{
    constexpr vector<uint8_t>::size_type size{750'000};
    const std::vector<uint8_t> data(size, 0x0);

    auto source = task::CCancellationSource::Make();
    const auto flags{SCRIPT_UTXO_AFTER_GENESIS};
    const auto params{make_eval_script_params(GlobalConfig::GetConfig().GetConfigScriptPolicy(), flags, false)};
    while(state.KeepRunning())
    {
        LimitedStack stack = LimitedStack({data}, INT64_MAX);
        EvalScript(params,
                   source->GetToken(),
                   stack,
                   CScript() << (size * 8) - 1 << OP_LSHIFT,
                   flags,
                   BaseSignatureChecker{});
    }
}
BENCHMARK(interpreter_6m_minus_1_lshift)

static void interpreter_max_minus_1_lshiftnum(benchmark::State& state)
{
    auto source = task::CCancellationSource::Make();

    constexpr vector<uint8_t>::size_type size{MAX_SCRIPT_NUM_LENGTH_AFTER_CHRONICLE};
    const std::vector<uint8_t> data(size, 0x0);
    const auto flags{SCRIPT_UTXO_AFTER_GENESIS | SCRIPT_UTXO_AFTER_CHRONICLE};
    const auto params{make_eval_script_params(GlobalConfig::GetConfig().GetConfigScriptPolicy(),
                                              flags,
                                              false)};
    while(state.KeepRunning())
    {
        LimitedStack stack = LimitedStack({data}, INT64_MAX);
        EvalScript(params,
                   source->GetToken(),
                   stack,
                   CScript() << (size * CHAR_BIT) - 1 << OP_LSHIFTNUM,
                   flags,
                   BaseSignatureChecker{});
    }
}
BENCHMARK(interpreter_max_minus_1_lshiftnum)

static void interpreter_6m_minus_1_rshift(benchmark::State& state)
{
    auto source = task::CCancellationSource::Make();

    constexpr vector<uint8_t>::size_type size{750'000};
    const std::vector<uint8_t> data(size, 0x0);
    const auto flags{SCRIPT_UTXO_AFTER_GENESIS};
    const auto params{make_eval_script_params(GlobalConfig::GetConfig().GetConfigScriptPolicy(), flags, false)};
    while(state.KeepRunning())
    {
        LimitedStack stack = LimitedStack({data}, INT64_MAX);
        EvalScript(params,
                   source->GetToken(),
                   stack,
                   CScript() << (size * 8) - 1 << OP_RSHIFT,
                   flags,
                   BaseSignatureChecker{});
    }
}
BENCHMARK(interpreter_6m_minus_1_rshift)

static void interpreter_int32_max_1_rshift(benchmark::State& state)
{
    auto source = task::CCancellationSource::Make();

    const std::vector<uint8_t> data(INT32_MAX / 8, 0x0);
    const auto flags{SCRIPT_UTXO_AFTER_GENESIS | SCRIPT_UTXO_AFTER_CHRONICLE};
    const auto params{make_eval_script_params(GlobalConfig::GetConfig().GetConfigScriptPolicy(), flags, false)};
    while(state.KeepRunning())
    {
        LimitedStack stack = LimitedStack({data}, INT64_MAX);
        EvalScript(params,
                   source->GetToken(),
                   stack,
                   CScript() << OP_1 << OP_RSHIFT,
                   flags,
                   BaseSignatureChecker{});
    }
}
BENCHMARK(interpreter_int32_max_1_rshift)

static void interpreter_6m_1_rshift(benchmark::State& state)
{
    constexpr vector<uint8_t>::size_type size{750'000};
    const std::vector<uint8_t> data(size, 0x0);

    auto source = task::CCancellationSource::Make();
    const auto flags{SCRIPT_UTXO_AFTER_GENESIS | SCRIPT_UTXO_AFTER_CHRONICLE};
    const auto params{make_eval_script_params(GlobalConfig::GetConfig().GetConfigScriptPolicy(), flags, false)};
    while(state.KeepRunning())
    {
        LimitedStack stack = LimitedStack({data}, INT64_MAX);
        EvalScript(params,
                   source->GetToken(),
                   stack,
                   CScript() << OP_1 << OP_RSHIFT,
                   flags,
                   BaseSignatureChecker{});
    }
}
BENCHMARK(interpreter_6m_1_rshift)

static void interpreter_max_1_rshiftnum(benchmark::State& state)
{
    constexpr vector<uint8_t>::size_type size{MAX_SCRIPT_NUM_LENGTH_AFTER_CHRONICLE};
    const std::vector<uint8_t> data(size, 0x0);

    auto source = task::CCancellationSource::Make();
    const auto flags{SCRIPT_UTXO_AFTER_GENESIS | SCRIPT_UTXO_AFTER_CHRONICLE};
    const auto params{make_eval_script_params(GlobalConfig::GetConfig().GetConfigScriptPolicy(),
                                              flags,
                                              false)};
    while(state.KeepRunning())
    {
        LimitedStack stack = LimitedStack({data}, INT64_MAX);
        EvalScript(params,
                   source->GetToken(),
                   stack,
                   CScript() << OP_1 << OP_RSHIFTNUM,
                   flags,
                   BaseSignatureChecker{});
    }
}
BENCHMARK(interpreter_max_1_rshiftnum)

static void interpreter_max_minus_1_rshiftnum(benchmark::State& state)
{
    auto source = task::CCancellationSource::Make();

    constexpr vector<uint8_t>::size_type size{MAX_SCRIPT_NUM_LENGTH_AFTER_CHRONICLE};
    const std::vector<uint8_t> data(size, 0x0);
    const auto flags{SCRIPT_UTXO_AFTER_GENESIS | SCRIPT_UTXO_AFTER_CHRONICLE};
    const auto params{make_eval_script_params(GlobalConfig::GetConfig().GetConfigScriptPolicy(),
                                              flags,
                                              false)};
    while(state.KeepRunning())
    {
        LimitedStack stack = LimitedStack({data}, INT64_MAX);
        EvalScript(params,
                   source->GetToken(),
                   stack,
                   CScript() << (size * CHAR_BIT) - 1 << OP_RSHIFTNUM,
                   flags,
                   BaseSignatureChecker{});
    }
}
BENCHMARK(interpreter_max_minus_1_rshiftnum)
