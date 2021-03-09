// Copyright (c) 2021 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "block_index_store_loader.h"

#include "dbwrapper.h"
#include "disk_block_index.h"
#include "pow.h"
#include "util.h"

#include <utility>
#include <boost/thread/thread.hpp>

bool BlockIndexStoreLoader::ForceLoad(
    const Config& config,
    std::unique_ptr<CDBIterator> cursor )
{
    std::lock_guard lock{ mBlockIndexStore.mMutex };

    assert( mBlockIndexStore.mStore.empty() );

    constexpr char DB_BLOCK_INDEX = 'b';
    cursor->Seek(std::make_pair(DB_BLOCK_INDEX, uint256()));

    // Load mapBlockIndex
    for (; cursor->Valid(); cursor->Next())
    {
        boost::this_thread::interruption_point();
        std::pair<char, uint256> key;
        if (!cursor->GetKey(key) || key.first != DB_BLOCK_INDEX) {
            break;
        }

        // Create uninitialized block index object in array or return one that was created previously
        auto& indexNew = mBlockIndexStore.GetOrInsertNL( key.second );
        assert(indexNew.GetVersion()==0 && indexNew.GetPrev()==nullptr); // We must always get an uninitialized block index object.

        // Initialize object by reading it from database
        CDiskBlockIndex diskindex{ indexNew };
        if (!cursor->GetValue( diskindex ))
        {
            return error("LoadBlockIndex() : failed to read value");
        }

        if(!diskindex.IsGenesis())
        {
            // Set parent of this object. This is a second part part of logical object construction.
            // If parent does not already exist in an array, a new uninitialized object is created.
            indexNew.CBlockIndex_SetPrev( &mBlockIndexStore.GetOrInsertNL(diskindex.GetHashPrev()), CBlockIndex::PrivateTag{} );
        }

        if (!CheckProofOfWork(indexNew.GetBlockHash(), indexNew.GetBits(),
                              config)) {
            return error("LoadBlockIndex(): CheckProofOfWork failed: %s",
                         indexNew.ToString());
        }
    }

    return true;
}
