// Copyright (c) 2026 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "amount.h"
#include "bench.h"
#include "consensus/merkle.h"
#include "primitives/block.h"
#include "primitives/transaction.h"
#include "script/opcodes.h"
#include "script/script.h"

#include <cstddef>
#include <cstdint>
#include <random>
#include <utility>

// Build a synthetic block of tx_count distinct dummy transactions.
// The transactions are not consensus-valid; they exist only to give
// BlockMerkleRoot() a population of unique txids to hash. Each input
// pushes a deterministic random value so that every txid differs.
static CBlock make_block(const size_t tx_count)
{
    CBlock block;
    block.vtx.reserve(tx_count);
    // Fixed seed so bench results are reproducible across runs.
    // NOLINTNEXTLINE(cert-msc32-c, cert-msc51-cpp)
    std::mt19937 gen{0xC0FFEE};
    std::uniform_int_distribution<int64_t> dist{0, INT64_MAX};
    for(size_t i = 0; i < tx_count; ++i)
    {
        CMutableTransaction tx;
        tx.vin.resize(1);
        tx.vin[0].scriptSig = CScript() << OP_0 << dist(gen);
        tx.vout.resize(1);
        tx.vout[0].nValue = Amount{};
        block.vtx.push_back(std::make_shared<const CTransaction>(std::move(tx)));
    }
    return block;
}

// Small block: ~10-level Merkle tree, bottom level has 512 pairs.
static void MerkleRoot_1024(benchmark::State& state)
{
    CBlock block = make_block(1'024);
    while(state.KeepRunning())
    {
        bool mutated{};
        BlockMerkleRoot(block, &mutated);
    }
}
BENCHMARK(MerkleRoot_1024)

// Medium block: catches any pathological intermediate behaviour
// between the small and large extremes.
static void MerkleRoot_16384(benchmark::State& state)
{
    CBlock block = make_block(16'384);
    while(state.KeepRunning())
    {
        bool mutated{};
        BlockMerkleRoot(block, &mutated);
    }
}
BENCHMARK(MerkleRoot_16384)

// Large block: ~16-level Merkle tree, bottom level has 32768 pairs.
// Exercises the batched path in steady state.
static void MerkleRoot_65536(benchmark::State& state)
{
    CBlock block = make_block(65'536);
    while(state.KeepRunning())
    {
        bool mutated{};
        BlockMerkleRoot(block, &mutated);
    }
}
BENCHMARK(MerkleRoot_65536)
