// Copyright (c) 2021 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include <mutex>
#include <set>
#include <vector>

class CBlockIndex;

class DirtyBlockIndexStore
{
public:
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

private:
    std::mutex mMutex;
    std::set<const CBlockIndex*> mDirty;
};
