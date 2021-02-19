// Copyright (c) 2021 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include <functional>
#include <mutex>
#include <unordered_map>

#include "primitives/block.h"
#include "block_hasher.h"
#include "chain.h"
#include "uint256.h"
#include "utiltime.h"

class BlockIndexStore
{
public:
    BlockIndexStore() = default;

    BlockIndexStore(BlockIndexStore&&) = delete;
    BlockIndexStore& operator=(BlockIndexStore&&) = delete;
    BlockIndexStore(const BlockIndexStore&) = delete;
    BlockIndexStore& operator=(const BlockIndexStore&) = delete;

    // may only be used in contexts where we are certain that nobody is using
    // CBlockIndex instances that are owned by this class
    void ForceClear()
    {
        std::lock_guard lock{ mMutex };

        mStore.clear();
        mBestHeader = nullptr;
    }

    CBlockIndex* GetOrInsert( const uint256& blockHash )
    {
        std::lock_guard lock{ mMutex };

        // Return existing
        if (auto index = getNL( blockHash ); index)
        {
            return index;
        }

        return &CBlockIndex::UnsafeMakePartial( blockHash, mStore );
    }

    CBlockIndex* Insert( const CBlockHeader& block )
    {
        std::lock_guard lock{ mMutex };

        auto& indexNew = CBlockIndex::Make( block, mStore );

        if (mBestHeader == nullptr ||
            mBestHeader->GetChainWork() < indexNew.GetChainWork())
        {
            mBestHeader = &indexNew;
        }

        return &indexNew;
    }

    CBlockIndex* Get( const uint256& blockHash )
    {
        std::shared_lock lock{ mMutex };

        return getNL( blockHash );
    }

    std::size_t Count() const
    {
        std::shared_lock lock{ mMutex };

        return mStore.size();
    }

    template<class Func>
    void ForEach(Func callback) const
    {
        std::shared_lock lock{ mMutex };

        for (auto& item : mStore)
        {
            callback( item.second );
        }
    }

    template<class Func>
    void ForEachMutable(Func callback)
    {
        std::lock_guard lock{ mMutex };

        for (auto& item : mStore)
        {
            callback( item.second );
        }
    }

    void SetBestHeader(const CBlockIndex& bestHeaderCandidate)
    {
        std::lock_guard lock{ mMutex };

        if (bestHeaderCandidate.IsValid(BlockValidity::TREE) &&
            (mBestHeader == nullptr ||
             CBlockIndexWorkComparator()(mBestHeader, &bestHeaderCandidate)))
        {
            mBestHeader = &bestHeaderCandidate;
        }
    }

    const CBlockIndex& SetBestHeaderIfNotSet(
        const CBlockIndex& bestHeaderCandidate)
    {
        std::lock_guard lock{ mMutex };

        if (mBestHeader == nullptr)
        {
            mBestHeader = &bestHeaderCandidate;
        }

        return *mBestHeader;
    }

    const CBlockIndex& GetBestHeaderRef() const
    {
        std::shared_lock lock{ mMutex };

        assert(mBestHeader);

        return *mBestHeader;
    }

    const CBlockIndex* GetBestHeader() const
    {
        std::shared_lock lock{ mMutex };

        return mBestHeader;
    }

private:
    CBlockIndex* getNL( const uint256& blockHash )
    {
        if (auto it = mStore.find( blockHash ); it != mStore.end())
        {
            return &it->second;
        }

        return nullptr;
    }

    mutable std::shared_mutex mMutex;
    std::unordered_map<uint256, CBlockIndex, BlockHasher> mStore;

    /**
     * Best header we've seen so far (used for getheaders queries' starting
     * points).
     */
    const CBlockIndex* mBestHeader{ nullptr };
};

/**
 * Maintain a map of CBlockIndex for all known headers.
 */
extern BlockIndexStore mapBlockIndex;
