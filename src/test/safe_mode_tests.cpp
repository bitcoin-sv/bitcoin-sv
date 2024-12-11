// Copyright (c) 2024 BSV Blockchain Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#include <boost/test/unit_test.hpp>

#include "block_index_store.h"
#include "safe_mode.h"
#include "config.h"
#include "sync.h"
#include "validation.h"

struct chain_guard
{
    chain_guard()
    {
        chainActive.SetTip(nullptr);
        mapBlockIndex.clear();
    }

    ~chain_guard()
    {
        chainActive.SetTip(nullptr);
        mapBlockIndex.clear();
    }

    chain_guard(const chain_guard&) = default;
    chain_guard(chain_guard&&) = default;
    chain_guard& operator=(const chain_guard&) = default;
    chain_guard& operator=(chain_guard&&) = default;
};

BOOST_AUTO_TEST_SUITE(safe_mode_tests)

BOOST_FIXTURE_TEST_CASE(get_min_relevant_block_height, chain_guard)
{
    SelectParams(CBaseChainParams::REGTEST);
    const auto& config{GlobalConfig::GetConfig()};

    const std::vector<std::pair<int64_t, int64_t>> test_data{{-1, 0}, {0, 0}, {1, 1}};
    for(const auto& [n, exp_height] : test_data)
    {
        BlockIndexStore blockIndexStore;
        CBlockIndex* tip{};
        uint256 prevHash;
        const auto max_safemode_fork_dist{config.GetSafeModeMaxForkDistance()};
        for(int64_t i{}; i <= max_safemode_fork_dist + n; ++i)
        {
            CBlockHeader hdr;
            hdr.hashPrevBlock = prevHash;
            tip = blockIndexStore.Insert(hdr);
            chainActive.SetTip(tip);
            prevHash = tip->GetBlockHash();
        }
        LOCK(cs_main);
        const auto h = GetMinimumRelevantBlockHeight(config);
        BOOST_CHECK_EQUAL(exp_height, h);
    }
}

BOOST_AUTO_TEST_SUITE_END()
