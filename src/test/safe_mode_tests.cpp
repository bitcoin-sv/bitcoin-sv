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

BOOST_FIXTURE_TEST_CASE(exclude_ignored_blocks_nullptr, chain_guard)
{
    LOCK(cs_main);

    const auto& config{GlobalConfig::GetConfig()};
    const auto [new_tip, ignored_blocks] = ExcludeIgnoredBlocks(config, nullptr);
    BOOST_CHECK_EQUAL(new_tip, nullptr);
    BOOST_CHECK_EQUAL(0, ignored_blocks.size());
}

BOOST_FIXTURE_TEST_CASE(exclude_ignored_blocks, chain_guard)
{
    SelectParams(CBaseChainParams::REGTEST);

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

    const auto max_safemode_fork_dist{3};
    uint32_t timestamp{};
    for(int64_t i{}; i < max_safemode_fork_dist; ++i)
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

    LOCK(cs_main);
    tip_2->SetIgnoredForSafeMode(true);
    const auto& config{GlobalConfig::GetConfig()};
    const auto [new_tip, ignored_blocks] = ExcludeIgnoredBlocks(config, tip_2);
    BOOST_CHECK_EQUAL(new_tip, tip_2->GetPrev());
    BOOST_CHECK_EQUAL(1, ignored_blocks.size());
    BOOST_CHECK_EQUAL(tip_2, ignored_blocks[0]);
}

BOOST_FIXTURE_TEST_CASE(get_fork_tips_0, chain_guard)
{
    LOCK(cs_main);
    const auto tips = GetForkTips();
    BOOST_CHECK(tips.empty());
}

BOOST_FIXTURE_TEST_CASE(get_fork_tips_1, chain_guard)
{
    LOCK(cs_main);

    CBlockHeader hdr; 
    auto* bi = mapBlockIndex.Insert(hdr);
    chainActive.SetTip(bi);

    const auto tips = GetForkTips();
    BOOST_CHECK(tips.empty());
}

BOOST_FIXTURE_TEST_CASE(get_fork_tips_2, chain_guard)
{
    LOCK(cs_main);

    CBlockHeader hdr; 
    auto* bi = mapBlockIndex.Insert(hdr);

    const auto tips = GetForkTips();
    BOOST_CHECK_EQUAL(1, tips.size());
    BOOST_CHECK_EQUAL(1, tips.count(bi));
}

BOOST_FIXTURE_TEST_CASE(get_fork_tips_3, chain_guard)
{
    LOCK(cs_main);

    uint32_t timestamp{};
    CBlockHeader hdr_1; 
    hdr_1.nTime = ++timestamp;
    auto* bi_1 = mapBlockIndex.Insert(hdr_1);
    CBlockHeader hdr_2; 
    hdr_2.nTime = ++timestamp;
    auto* bi_2 = mapBlockIndex.Insert(hdr_2);

    const auto tips = GetForkTips();
    BOOST_CHECK_EQUAL(2, tips.size());
    BOOST_CHECK_EQUAL(1, tips.count(bi_1));
    BOOST_CHECK_EQUAL(1, tips.count(bi_2));
}

BOOST_AUTO_TEST_CASE(check_safe_mode_parameters_nullptr)
{
    LOCK(cs_main);

    const auto& config{GlobalConfig::GetConfig()};
    SafeMode sm;
    sm.CheckSafeModeParameters(config, nullptr);
    const auto forks = sm.forks();
    BOOST_CHECK(forks.empty());
}

BOOST_AUTO_TEST_CASE(check_safe_mode_parameters_safe_mode_max_fork_dist)
{
    using test_args = std::tuple<int,   // fork distance offset
                                 int>;  // expected number of forks
    const std::vector<test_args> test_data{ {-1, 1},   // lt safemode_max_fork_dist
                                            { 0, 1},   // eq safemode_max_fork_dist
                                            { 1, 0} }; // gt safemode_max_fork_dist
    for(const auto& [offset, exp_forks_size] : test_data)
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
        for(int64_t i{}; i < safemode_max_fork_dist + offset; ++i)
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
        BOOST_CHECK_EQUAL(exp_forks_size, forks.size());
        if(!forks.empty())
        {
            const auto fork = forks.find(tip_2);
            BOOST_CHECK_EQUAL(safemode_max_fork_dist + offset, fork->second->size());
        }
    
        const auto status = sm.GetStatus();
        BOOST_CHECK(status.starts_with(forks.empty()
                                           ? R"({"safemodeenabled": false)"
                                           : R"({"safemodeenabled": true)"));
    }
}

BOOST_AUTO_TEST_SUITE_END()
