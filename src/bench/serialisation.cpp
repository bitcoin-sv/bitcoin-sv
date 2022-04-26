// Copyright (c) 2020 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#include <iostream>

#include "bench.h"

#include "rpc/text_writer.h"
#include "utilstrencodings.h"
#include "rpc/jsonwriter.h"
#include "primitives/transaction.h"
#include "core_io.h"
#include "iterator"

void encode_hex_tx(benchmark::State& state)
{
    using namespace std;

    constexpr int nInputs{6'000};
    CMutableTransaction mtx;

    const COutPoint outpoint;
    CScript script;
    constexpr int unlocking_script_len{107};
    script.insert(script.begin(), unlocking_script_len, OP_NOP);
    CTxIn input{outpoint, script};

    generate_n(back_inserter(mtx.vin), nInputs, [&input](){ return input;});

    const CTransaction tx(mtx);
    while (state.KeepRunning())
    {
        CStringWriter sw;
        const auto tx_size{tx.GetTotalSize()};
        sw.ReserveAdditional(tx_size*2);
        CJSONWriter jw{sw, true};
        EncodeHexTx(tx, jw.getWriter());
    }
}
BENCHMARK(encode_hex_tx);

