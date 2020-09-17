// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "merkletreedb.h"

CMerkleTreeIndexDB::CMerkleTreeIndexDB(const fs::path& databasePath, size_t leveldbCacheSize, bool fMemory, bool fWipe)
    : merkleTreeIndexDB(databasePath, leveldbCacheSize, fMemory, fWipe)
{
    // Write initial records if they do not yet exist
    bool isIndexOutOfSync = true;
    if (!merkleTreeIndexDB.Read(DB_MERKLE_TREES_INDEX_OUT_OF_SYNC, isIndexOutOfSync))
    {
        merkleTreeIndexDB.Write(DB_MERKLE_TREES_INDEX_OUT_OF_SYNC, isIndexOutOfSync);
    }
    MerkleTreeDiskPosition nextDiskPosition;
    if (!merkleTreeIndexDB.Read(DB_NEXT_MERKLE_TREE_DISK_POSITION, nextDiskPosition))
    {
        nextDiskPosition.fileOffset = 0;
        nextDiskPosition.fileSuffix = 0;
        merkleTreeIndexDB.Write(DB_NEXT_MERKLE_TREE_DISK_POSITION, nextDiskPosition);
    }
    uint64_t diskUsage = 0;
    if (!merkleTreeIndexDB.Read(DB_MERKLE_TREES_DISK_USAGE, diskUsage))
    {
        diskUsage = 0;
        merkleTreeIndexDB.Write(DB_MERKLE_TREES_DISK_USAGE, diskUsage);
    }
}

CMerkleTreeIndexDBIterator<uint256> CMerkleTreeIndexDB::GetDiskPositionsIterator()
{
    return CMerkleTreeIndexDBIterator<uint256>(merkleTreeIndexDB, std::make_pair(DB_MERKLE_TREE_DISK_POSITIONS, uint256()));
}

bool CMerkleTreeIndexDB::GetNextDiskPosition(MerkleTreeDiskPosition& nextDiskPositionOut)
{
    return merkleTreeIndexDB.Read(DB_NEXT_MERKLE_TREE_DISK_POSITION, nextDiskPositionOut);
}

CMerkleTreeIndexDBIterator<int> CMerkleTreeIndexDB::GetFileInfosIterator()
{
    return CMerkleTreeIndexDBIterator<int>(merkleTreeIndexDB, std::make_pair(DB_MERKLE_TREE_FILE_INFOS, int()));
}

bool CMerkleTreeIndexDB::GetDiskUsage(uint64_t& diskUsageOut)
{
    return merkleTreeIndexDB.Read(DB_MERKLE_TREES_DISK_USAGE, diskUsageOut);
}

bool CMerkleTreeIndexDB::AddMerkleTreeData(const uint256& newBlockHash, const MerkleTreeDiskPosition& newDiskPosition,
                                      const MerkleTreeDiskPosition &updatedNextDiskPosition,
                                      const MerkleTreeFileInfo& updatedFileInfo, const uint64_t updatedDiskUsage)
{
    CDBBatch batch(merkleTreeIndexDB);

    batch.Write(std::make_pair(DB_MERKLE_TREE_DISK_POSITIONS, newBlockHash), newDiskPosition);
    batch.Write(DB_NEXT_MERKLE_TREE_DISK_POSITION, updatedNextDiskPosition);
    batch.Write(std::make_pair(DB_MERKLE_TREE_FILE_INFOS, newDiskPosition.fileSuffix), updatedFileInfo);
    batch.Write(DB_MERKLE_TREES_DISK_USAGE, updatedDiskUsage);

    return merkleTreeIndexDB.WriteBatch(batch, true);
}

bool CMerkleTreeIndexDB::RemoveMerkleTreeData(const std::vector<int>& suffixesOfDataFilesRemoved, const std::vector<uint256>& blockHashesOfMerkleTreesRemoved,
                                         const MerkleTreeDiskPosition& updatedNextDiskPosition, const uint64_t updatedDiskUsage)
{
    if (!suffixesOfDataFilesRemoved.size())
    {
        // Nothing to remove
        return true;
    }

    CDBBatch batch(merkleTreeIndexDB);

    for (const int suffixOfDataFileRemoved : suffixesOfDataFilesRemoved)
    {
        batch.Erase(std::make_pair(DB_MERKLE_TREE_FILE_INFOS, suffixOfDataFileRemoved));
    }
    for (const uint256& blockHashOfMerkleTreeRemoved : blockHashesOfMerkleTreesRemoved)
    {
        batch.Erase(std::make_pair(DB_MERKLE_TREE_DISK_POSITIONS, blockHashOfMerkleTreeRemoved));
    }
    batch.Write(DB_NEXT_MERKLE_TREE_DISK_POSITION, updatedNextDiskPosition);
    batch.Write(DB_MERKLE_TREES_DISK_USAGE, updatedDiskUsage);

    return merkleTreeIndexDB.WriteBatch(batch, true);
}

bool CMerkleTreeIndexDB::SetIndexOutOfSync(bool isIndexOutOfSyncIn)
{
    return merkleTreeIndexDB.Write(DB_MERKLE_TREES_INDEX_OUT_OF_SYNC, isIndexOutOfSyncIn);
}

bool CMerkleTreeIndexDB::GetIndexOutOfSync(bool& isIndexOutOfSyncOut)
{
    return merkleTreeIndexDB.Read(DB_MERKLE_TREES_INDEX_OUT_OF_SYNC, isIndexOutOfSyncOut);
}
