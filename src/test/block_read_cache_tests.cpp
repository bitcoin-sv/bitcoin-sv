// Copyright (c) 2024 BSV Blockchain Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "block_read_cache.h"
#include "config.h"
#include "primitives/block.h"
#include "validation.h"

#include "test/test_bitcoin.h"
#include "test/testutil.h"

#include <boost/test/unit_test.hpp>

#include <chrono>
#include <mutex>

namespace
{
    // For testing ID only
    class tests_id;
}

// BlockReadCache class inspection
template<>
struct BlockReadCache::UnitTestAccess<tests_id>
{
    static const std::optional<BlockReadCache::BlockWithHash>& GetNextBlock(const BlockReadCache& cache)
    {
        std::lock_guard lock { cache.mMtx };
        return cache.mNextBlock;
    }

    static const std::optional<uint256>& GetHashReading(const BlockReadCache& cache)
    {
        std::lock_guard lock { cache.mMtx };
        return cache.mHashReading;
    }
};
using UnitTestAccess = BlockReadCache::UnitTestAccess<tests_id>;

BOOST_AUTO_TEST_SUITE(block_read_cache)

BOOST_FIXTURE_TEST_CASE(cache, TestChain100Setup)
{
    using namespace std::literals::chrono_literals;

    const Config& config { GlobalConfig::GetConfig() };

    // Check intial state
    BlockReadCache cache {};
    BOOST_CHECK(!UnitTestAccess::GetNextBlock(cache));
    BOOST_CHECK(!UnitTestAccess::GetHashReading(cache));

    // Lambda for checking we have the required block cached
    auto GotCachedBlock = [&cache](const uint256& block)
    {
        const auto& cached { UnitTestAccess::GetNextBlock(cache) };
        return cached && cached->hash == block;
    };

    // Request for a block gets us that block and triggers a read of the next
    const CBlockIndex* index { chainActive[50] };
    const CBlockIndex* bestIndex { chainActive.Tip() };
    CBlock block {};
    BOOST_CHECK(cache.ReadBlock(index, bestIndex, block, config));
    BOOST_CHECK(wait_for([&](){ return GotCachedBlock(chainActive[51]->GetBlockHash()); }, 500ms));
    BOOST_CHECK(!UnitTestAccess::GetHashReading(cache));
    BOOST_CHECK_EQUAL(index->GetBlockHash(), block.GetHash());

    // And request for cached block returns that and triggers a read of the next
    index = chainActive[51];
    BOOST_CHECK(cache.ReadBlock(index, bestIndex, block, config));
    BOOST_CHECK(wait_for([&](){ return GotCachedBlock(chainActive[52]->GetBlockHash()); }, 500ms));
    BOOST_CHECK(!UnitTestAccess::GetHashReading(cache));
    BOOST_CHECK_EQUAL(index->GetBlockHash(), block.GetHash());

    // Request for block other than cached block returns that and triggers a read of the next
    index = chainActive[60];
    BOOST_CHECK(cache.ReadBlock(index, bestIndex, block, config));
    BOOST_CHECK(wait_for([&](){ return GotCachedBlock(chainActive[61]->GetBlockHash()); }, 500ms));
    BOOST_CHECK(!UnitTestAccess::GetHashReading(cache));
    BOOST_CHECK_EQUAL(index->GetBlockHash(), block.GetHash());

    // Explicit request to cache the next block
    index = chainActive[70];
    cache.CacheNextBlock(index, bestIndex, config);
    BOOST_CHECK(wait_for([&](){ return GotCachedBlock(chainActive[71]->GetBlockHash()); }, 500ms));
    BOOST_CHECK(!UnitTestAccess::GetHashReading(cache));

    // Another request to cache the same block changes nothing
    cache.CacheNextBlock(index, bestIndex, config);
    BOOST_CHECK(GotCachedBlock(chainActive[71]->GetBlockHash()));

    // Can fetch explicitly cached block
    BOOST_CHECK(cache.ReadBlock(index, bestIndex, block, config));
    BOOST_CHECK_EQUAL(index->GetBlockHash(), block.GetHash());
}

BOOST_AUTO_TEST_SUITE_END()

