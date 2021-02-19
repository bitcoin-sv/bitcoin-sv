// Copyright (c) 2021 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include <mutex>
#include <set>
#include <vector>

class CBlockIndex;
class BlockIndexStore;
class BlockIndexStoreLoader;

/**
 * DirtyBlockIndexStore's purpose is tracking CBlockIndex objects that were changed and not yet persisted to database.
 * DirtyBlockIndexStore is a storage of CBlockIndex objects which were mutated after initialization.
 * Majority of CBlockIndex members are immutable.
 * Members nStatus, nFile, nDataPos, nUndoPos and mDiskBlockMetaData are mutable and can be changed during object lifetime.
 * If they change, they are inserted into mDirty (DirtyBlockIndexStorage).
 * When changed data is flushed to database, mDirty is cleared.
 * Because CBlockIndex can only be mutated inside of its own class, Insert is called only from CBlockIndex class.
 * Clearing and extracting from mDirty is possible only from BlockIndexStore and BlockIndexStoreLoader.
 * These classes are therefore marked as friend classes.
 */
class DirtyBlockIndexStore
{
private:

    void Clear()
    {
        std::lock_guard lock{ mMutex };

        mDirty.clear();
    }

    void Insert(const CBlockIndex& index)
    {
        std::lock_guard lock{ mMutex };

        mDirty.insert( &index );
    }

    std::vector<const CBlockIndex*> Extract()
    {
        std::lock_guard lock{ mMutex };

        std::vector<const CBlockIndex*> vBlocks( mDirty.begin(), mDirty.end() );
        mDirty.clear();

        return vBlocks;
    }

    std::mutex mMutex;
    std::set<const CBlockIndex*> mDirty;

    friend class CBlockIndex;
    friend class BlockIndexStore;
    friend class BlockIndexStoreLoader;
};
