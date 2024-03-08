// Copyright (c) 2021 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include <functional>
#include <mutex>
#include <unordered_map>

#include "primitives/block.h"
#include "block_hasher.h"
#include "chain.h"
#include "dirty_block_index_store.h"
#include "uint256.h"
#include "utiltime.h"

/**
 * BlockIndexStore tracks all currently existing CBlockIndex objects (except TemporaryBlockIndex objects).
 * In the outside world it can be accessed via mapBlockIndex global variable.
 * CBlockIndex objects are stored in mStore member variable.
 * mStore is locked internally with mMutex member variable on every read/write operation.
 * The header that is valid and has the highest chain work is stored in mBestHeader member.
 * Details about choosing the best header are in CBlockIndexWorkComparator implementation.
 * BlockIndexStore also keeps tracks of objects that were changed during the lifetime and not yet persisted to the database: mDirtyBlockIndex.
 * When changes are persisted to database, mDirtyBlockIndex is cleared via ExtractDirtyBlockIndices method.
 */
class BlockIndexStore
{
public:
    BlockIndexStore() = default;

    BlockIndexStore(BlockIndexStore&&) = delete;
    BlockIndexStore& operator=(BlockIndexStore&&) = delete;
    BlockIndexStore(const BlockIndexStore&) = delete;
    BlockIndexStore& operator=(const BlockIndexStore&) = delete;

    /**
     * Insert new or return existing block index instance.
     *
     * Parent block index must already exist in the provided container.
     * Trying to construct a CBlockIndex instance with hash that already exists
     * in the container is considered no-op and we just return the existing
     * entry.
     */
    CBlockIndex* Insert( const CBlockHeader& block )
    {
        std::lock_guard lock{ mMutex };

        auto prev = mStore.find(block.hashPrevBlock);

        // Only genesis blocks may have missing previous block!
        assert(
            (block.hashPrevBlock.IsNull() && prev == mStore.end() ) ||
            prev != mStore.end());

        auto mi =
            mStore.try_emplace(
                block.GetHash(),
                block,
                (prev != mStore.end() ? &prev->second : nullptr),
                mDirtyBlockIndex,
                CBlockIndex::PrivateTag{})
            .first;

        auto& indexNew = mi->second;
        indexNew.CBlockIndex_SetBlockHash( &mi->first, CBlockIndex::PrivateTag{} );

        if (mBestHeader == nullptr ||
            CBlockIndexWorkComparator()(mBestHeader, &indexNew))
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

    std::vector<const CBlockIndex*> ExtractDirtyBlockIndices()
    {
        return mDirtyBlockIndex.Extract();
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

    const CBlockIndex& GetBestHeader() const
    {
        std::shared_lock lock{ mMutex };

        assert(mBestHeader);

        return *mBestHeader;
    }


    /**
     * Allows BlockIndexStore object to be directly passed to methods that modify CBlockIndex object
     * so that any changes automatically mark the object as dirty within the store.
     */
    operator DirtyBlockIndexStore&()
    {
        return mDirtyBlockIndex;
    }

private:
    CBlockIndex& GetOrInsertNL( const uint256& blockHash )
    {
        // Return existing
        if (auto index = getNL( blockHash ); index)
        {
            return *index;
        }

        auto [mi, inserted] =
            mStore.try_emplace(
                blockHash,
                CBlockIndex::PrivateTag{} );
        assert( inserted );
        auto& indexNew = mi->second;
        indexNew.CBlockIndex_SetBlockHash( &mi->first, CBlockIndex::PrivateTag{} );

        return indexNew;
    }

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
     *
     * NOTE: This is always set to not null after initialization in init.cpp
     *       is complete and before p2p connections are established.
     */
    const CBlockIndex* mBestHeader{ nullptr };

    DirtyBlockIndexStore mDirtyBlockIndex;

    friend class BlockIndexStoreLoader;
};

/**
 * Maintain a map of CBlockIndex for all known headers.
 */
extern BlockIndexStore mapBlockIndex;
