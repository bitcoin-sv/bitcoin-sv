// Copyright (c) 2024 BSV Blockchain Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.
 
#pragma once

#include "primitives/block.h"
#include "threadpool.h"
#include "uint256.h"

#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>

class CBlockIndex;
class Config;

/**
 * Class to help when reading multiple sequential blocks from disk.
 *
 * Try to cache the expected next block in the sequence in the background
 * so the caller doesn't have to wait so long during IBD or reorgs.
 */
class BlockReadCache
{
  public:

    // Read the given block
    bool ReadBlock(const CBlockIndex* index, const CBlockIndex* indexMostWork,
        CBlock& block, const Config& config);

    // Try to start reading the next block in the chain
    void CacheNextBlock(const CBlockIndex* index, const CBlockIndex* indexMostWork,
        const Config& config);

    // For unit testing
    template<typename T> struct UnitTestAccess;

  private:

    // Return the requested block from the cache if we have it
    bool GetBlockFromCacheNL(const CBlockIndex* index, CBlock& block);

    // Thread entry point for background block read
    void ThreadRead(const CBlockIndex* index, const Config& config);

    // A block plus its hash
    struct BlockWithHash
    {
        BlockWithHash() = default;
        BlockWithHash(const uint256& hashIn) : hash{hashIn} {}

        CBlock block {};
        uint256 hash {};
    };

    // The cached next block, if we have one
    std::optional<BlockWithHash> mNextBlock { std::nullopt };

    // Hash of block we are in the process of caching
    std::optional<uint256> mHashReading { std::nullopt };

    // Inter-thread signalling
    mutable std::mutex mMtx {};
    std::condition_variable mCondVar {};

    // For reading next block in the background.
    // Always keep as last member declared in class.
    CThreadPool<CQueueAdaptor> mThreadPool { false, "BlockReadCache", 1 };

};

// Global block reading cache
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
inline std::unique_ptr<BlockReadCache> g_blockReadCache {nullptr};

