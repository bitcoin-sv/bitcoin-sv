// Copyright (c) 2020 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#include <iostream>

#include "bench.h"

#include "blockencodings.h"
#include "core_io.h"
#include "iterator"
#include "netmessagemaker.h"
#include "primitives/transaction.h"
#include "rpc/jsonwriter.h"
#include "rpc/text_writer.h"
#include "serialize.h"
#include "streams.h"
#include "utilstrencodings.h"
#include "version.h"

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

    CScript op_script;
    op_script << OP_0 << OP_RETURN;
    const size_t count{3'000'000};
    op_script.insert(op_script.end(), count, 42);
    CTxOut output;
    output.scriptPubKey = op_script;
    mtx.vout.push_back(output);

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

static auto make_btxs = []
{
    constexpr auto n{100'000};
    BlockTransactions btxs;
    btxs.txn.reserve(n);

    for(size_t i{}; i < n; ++i)
    {
        CMutableTransaction mtx;
        mtx.nVersion = 1;

        mtx.vin.resize(1);

        constexpr auto p2pkh_ip_size{73};
        const std::vector<uint8_t> v_ip(p2pkh_ip_size, 42);
        CScript ip_script{v_ip.cbegin(), v_ip.cend()};
        mtx.vin[0].scriptSig = ip_script;

        mtx.vout.resize(2);
        constexpr auto p2pkh_op_size{25};
        const std::vector<uint8_t> v_op(p2pkh_op_size, 42);
        CScript op_script{v_op.cbegin(), v_op.cend()};
        mtx.vout[0].scriptPubKey = op_script;
        mtx.vout[0].nValue = 100 * COIN;
        mtx.vout[1].scriptPubKey = op_script;
        mtx.vout[1].nValue = 100 * COIN;

        mtx.nLockTime = 2;
        btxs.txn.push_back(std::make_shared<CTransaction>(mtx));
    }
    return btxs;
};

static BlockTransactions btxs = make_btxs();

static void ser_btxs_noreserve(benchmark::State& state)
{
    using namespace std;

    while(state.KeepRunning())
    {
        std::vector<uint8_t> data;
        size_t pos{0};
        CVectorWriter(SER_NETWORK, INIT_PROTO_VERSION, data, pos, btxs);
    }
}
BENCHMARK(ser_btxs_noreserve);

static void ser_btxs_reserve(benchmark::State& state)
{
    using namespace std;

    while(state.KeepRunning())
    {
        std::vector<uint8_t> data;
        data.reserve(ser_size(btxs));
        size_t pos{0};
        CVectorWriter(SER_NETWORK, INIT_PROTO_VERSION, data, pos, btxs);
    }
}
BENCHMARK(ser_btxs_reserve);

static void ser_msgmaker(benchmark::State& state)
{
    using namespace std;

    while(state.KeepRunning())
    {
        const auto version{42};
        CNetMsgMaker mm{version};
        mm.Make(NetMsgType::BLOCKTXN, btxs);
    }
}
BENCHMARK(ser_msgmaker);

static void ser_btxs_getsersize_test(benchmark::State& state)
{
    using namespace std;
        
    while(state.KeepRunning())
    {
        auto x = GetSerializeSize(btxs, SER_NETWORK, PROTOCOL_VERSION);
        asm volatile(""::"g"(&x):"memory");
    }
}
BENCHMARK(ser_btxs_getsersize_test);

static void ser_btxs_size_test(benchmark::State& state)
{
    using namespace std;

    while(state.KeepRunning())
    {
        auto x = ser_size(btxs);
        asm volatile(""::"g"(&x):"memory");
    }
}
BENCHMARK(ser_btxs_size_test);
