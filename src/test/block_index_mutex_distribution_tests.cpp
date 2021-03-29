// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "block_index.h"
#include "block_index_store.h"
#include "config.h"
#include "pow.h"
#include "validation.h"

#include "test/test_bitcoin.h"
#include <boost/test/unit_test.hpp>

namespace{ class Unique; }

template <>
struct CBlockIndex::UnitTestAccess<class Unique>
{
    UnitTestAccess() = delete;

    static std::mutex& GetCBIMutex(CBlockIndex* blockIndex)
    {
        return blockIndex->GetMutex();
    }
};
using TestAccessCBlockIndex = CBlockIndex::UnitTestAccess<class Unique>;

BOOST_FIXTURE_TEST_SUITE(blockindexmutex_tests, TestingSetup)

CBlockIndex* AddBlockIndex(CBlockIndex& prev, BlockIndexStore& mapBlockIndex)
{
    CBlockHeader header;
    header.nTime = GetTime();
    header.hashPrevBlock = prev.GetBlockHash();
    header.nBits = GetNextWorkRequired( &prev, &header, GlobalConfig::GetConfig() );
    CBlockIndex* current = mapBlockIndex.Insert( header );

    return current;
}

BOOST_AUTO_TEST_CASE(BlockIndexMutexDistributionTest)
{
    uint32_t BLOCK_COUNT = 100000;
    uint32_t MUTEX_COUNT = 8;
    uint32_t LOWER_LIMIT = BLOCK_COUNT / MUTEX_COUNT - 0.1 * BLOCK_COUNT;
    uint32_t UPPER_LIMIT = BLOCK_COUNT / MUTEX_COUNT + 0.1 * BLOCK_COUNT;

    LOCK(cs_main);
    std::map<std::mutex*, uint32_t> blockIndexDistribution;

    CBlockIndex* prev = AddBlockIndex(*chainActive.Genesis(), mapBlockIndex);
    for (uint32_t i = 0; i < BLOCK_COUNT; i++)
    {
        prev = AddBlockIndex(*prev, mapBlockIndex);
        ++blockIndexDistribution[&TestAccessCBlockIndex::GetCBIMutex(prev)];
    }

    BOOST_CHECK(blockIndexDistribution.size() == MUTEX_COUNT);
    for (auto it = blockIndexDistribution.begin(); it != blockIndexDistribution.end(); it++)
    {
        BOOST_CHECK(it->second > LOWER_LIMIT && it->second < UPPER_LIMIT);
    }
}

BOOST_AUTO_TEST_SUITE_END()
