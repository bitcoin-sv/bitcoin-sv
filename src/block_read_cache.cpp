// Copyright (c) 2024 BSV Blockchain Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "block_read_cache.h"
#include "block_index.h"
#include "config.h"
#include "task_helpers.h"

// Read the given block
bool BlockReadCache::ReadBlock(const CBlockIndex* index, const CBlockIndex* indexMostWork,
    CBlock& block, const Config& config)
{
    bool res {false};

    {
        std::unique_lock lock { mMtx };

        // Do we already have the block cached?
        if(res = GetBlockFromCacheNL(index, block); !res)
        {
            // Are we in the process of caching the block?
            if(mHashReading && mHashReading.value() == index->GetBlockHash())
            {
                // Wait for read to complete
                mCondVar.wait(lock);

                // Try again to fetch block from cache
                res = GetBlockFromCacheNL(index, block);
            }
        }
    }

    if(!res)
    {
        // Fallback to just reading from disk
        res = index->ReadBlockFromDisk(block, config);
    }

    // Try to start read of next block
    CacheNextBlock(index, indexMostWork, config);

    return res;
}

// Return the requested block from the cache if we have it
bool BlockReadCache::GetBlockFromCacheNL(const CBlockIndex* index, CBlock& block)
{
    // Do we have the right block cached?
    if(mNextBlock && mNextBlock->hash == index->GetBlockHash())
    {
        // Move block out of our cache
        block = std::move(mNextBlock->block);
        mNextBlock = std::nullopt;
        return true;
    }

    return false;
}

// Try to start reading the next block in the chain
void BlockReadCache::CacheNextBlock(const CBlockIndex* index, const CBlockIndex* indexMostWork,
    const Config& config)
{
    if(!indexMostWork)
    {
        return;
    }

    // What do we think the next block will be?
    const CBlockIndex* nextBlockIndex { indexMostWork->GetAncestor(index->GetHeight() + 1) };
    if(nextBlockIndex)
    {
        std::lock_guard lock { mMtx };

        // Ensure we're only ever caching 1 block at a time
        if(!mHashReading)
        {
            // Check we don't already have the required block cached
            if(!mNextBlock || mNextBlock->hash != nextBlockIndex->GetBlockHash())
            {
                mHashReading = nextBlockIndex->GetBlockHash();
                make_task(mThreadPool,
                    [this, nextBlockIndex, &config]()
                    {
                        this->ThreadRead(nextBlockIndex, config);
                    }
                );
            }
        }
    }
}

// Thread entry point for background block read
void BlockReadCache::ThreadRead(const CBlockIndex* index, const Config& config)
{
    // Read from disk
    BlockWithHash blockWithHash { index->GetBlockHash() };
    bool res { index->ReadBlockFromDisk(blockWithHash.block, config) };

    std::lock_guard lock { mMtx };
    mHashReading = std::nullopt;
    if(res)
    {
        mNextBlock = std::move(blockWithHash);
    }
    else
    {
        mNextBlock = std::nullopt;
    }

    // Notify any waiters for this block
    mCondVar.notify_all();
}

