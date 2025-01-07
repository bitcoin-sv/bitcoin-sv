// Copyright (c) 2024 BSV Blockchain Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#include "bench.h"

#include "safe_mode.h"

#include "block_index_store.h"
#include "config.h"

#include "test/chain_guard.h"

static void check_safe_mode_parameters(benchmark::State& state)
{
    chain_guard guard;
    
    LOCK(cs_main);

    CBlockHeader hdr;
    uint256 prev_hash;
    hdr.hashPrevBlock = prev_hash;
    BlockIndexStore blockIndexStore;
    CBlockIndex* tip{blockIndexStore.Insert(hdr)};
    prev_hash = tip->GetBlockHash();
    
    CBlockIndex* tip_1{};
    uint256 prev_hash_1{prev_hash};

    CBlockIndex* tip_2{};
    uint256 prev_hash_2{prev_hash};

    const auto& config{GlobalConfig::GetConfig()};
    const auto safemode_max_fork_dist = config.GetSafeModeMaxForkDistance();
    uint32_t timestamp{};
    for(int64_t i{}; i < safemode_max_fork_dist; ++i)
    {
        CBlockHeader hdr_1;
        hdr_1.hashPrevBlock = prev_hash_1;
        hdr_1.nTime = ++timestamp;
        tip_1 = blockIndexStore.Insert(hdr_1);
        prev_hash_1 = tip_1->GetBlockHash();
        
        CBlockHeader hdr_2;
        hdr_2.hashPrevBlock = prev_hash_2;
        hdr_2.nTime = ++timestamp;
        tip_2 = blockIndexStore.Insert(hdr_2);
        prev_hash_2 = tip_2->GetBlockHash();
    }
    chainActive.SetTip(tip_1);
    
    SafeMode sm;
    sm.CheckSafeModeParameters(config, tip_2);
    const auto forks = sm.forks();

    auto mid_tip = tip_2->GetPrev();
    for(int32_t i{}; i < safemode_max_fork_dist/2; ++i)
        mid_tip = mid_tip->GetPrev();

    while(state.KeepRunning())
    {
        sm.CheckSafeModeParameters(config, mid_tip);
    }
}

BENCHMARK(check_safe_mode_parameters); // NOLINT(cert-err58-cpp)

