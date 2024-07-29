// Copyright (c) 2024 BSV Blockchain Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#include "bench.h"

#include "config.h"
#include "script/interpreter.h"
#include "script/opcodes.h"
#include "script/script_error.h"
#include "taskcancellation.h"

#include <cstdint>

static void op_dup(benchmark::State& state)
{
    using namespace std;

    const Config& config = GlobalConfig::GetConfig();
    const int32_t flags{SCRIPT_UTXO_AFTER_GENESIS};
    const vector<uint8_t> script{ OP_1, 2, 0xff, 0x7f, OP_NUM2BIN,
                                  OP_DUP,
                                  OP_2DUP,
                                  OP_3DUP, OP_3DUP, OP_3DUP, OP_3DUP, OP_3DUP,
                                  OP_3DUP, OP_3DUP, OP_3DUP, OP_3DUP, OP_3DUP};
    while(state.KeepRunning())
    {
        auto source = task::CCancellationSource::Make();
        LimitedStack stack{INT64_MAX};
        const auto status = EvalScript(config,
                                       false,
                                       source->GetToken(),
                                       stack,
                                       CScript{script.begin(), script.end()},
                                       flags,
                                       BaseSignatureChecker{});
        assert(status);
        assert(status->index() == 1);
    }
}
BENCHMARK(op_dup);

static void op_2rot(benchmark::State& state)
{
    using namespace std;

    const Config& config = GlobalConfig::GetConfig();
    const int32_t flags{SCRIPT_UTXO_AFTER_GENESIS};
    const vector<uint8_t> script{ OP_1, 2, 0xff, 0x7f, OP_NUM2BIN,
                                  OP_DUP,
                                  OP_DUP,
                                  OP_3DUP,
								  OP_2ROT, OP_2ROT, OP_2ROT, OP_2ROT, OP_2ROT,
                                  OP_2ROT, OP_2ROT, OP_2ROT, OP_2ROT, OP_2ROT};
    while(state.KeepRunning())
    {
        auto source = task::CCancellationSource::Make();
        LimitedStack stack{INT64_MAX};
        const auto status = EvalScript(config,
                                       false,
                                       source->GetToken(),
                                       stack,
                                       CScript{script.begin(), script.end()},
                                       flags,
                                       BaseSignatureChecker{});
        assert(status);
        assert(status->index() == 1);
    }
}
BENCHMARK(op_2rot);

static void op_split(benchmark::State& state)
{
    using namespace std;

    const Config& config = GlobalConfig::GetConfig();
    const int32_t flags{SCRIPT_UTXO_AFTER_GENESIS};
    const vector<uint8_t> script{ OP_1, 3, 0xff, 0xff, 0x7f, OP_NUM2BIN,
                                        3, 0xff, 0xff, 0x3f, OP_SPLIT,
                                        3, 0xff, 0xff, 0x1f, OP_SPLIT,
                                        3, 0xff, 0xff, 0x0f, OP_SPLIT,
                                        3, 0xff, 0xff, 0x07, OP_SPLIT,
                                        3, 0xff, 0xff, 0x03, OP_SPLIT,
                                        3, 0xff, 0xff, 0x01, OP_SPLIT };
    while(state.KeepRunning())
    {
        auto source = task::CCancellationSource::Make();
        LimitedStack stack{INT64_MAX};
        const auto status = EvalScript(config,
                                       false,
                                       source->GetToken(),
                                       stack,
                                       CScript{script.begin(), script.end()},
                                       flags,
                                       BaseSignatureChecker{});
        assert(status);
        assert(status->index() == 1);
    }
}
BENCHMARK(op_split);

