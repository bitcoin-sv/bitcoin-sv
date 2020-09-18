// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "merkletreestore.h"
#include "util.h"
#include "config.h"
#include "clientversion.h"

/* Global state of Merkle Tree factory.
 * Merkle Trees are stored in memory cache and on disk when requested (RPC).
 */
std::unique_ptr<CMerkleTreeFactory> pMerkleTreeFactory = nullptr;

CMerkleTreeStore::CMerkleTreeStore(const fs::path& storePath, size_t leveldbCacheSize) : diskUsage(0), merkleStorePath(storePath)
{
    merkleTreeIndexDB = std::make_unique<CMerkleTreeIndexDB>(merkleStorePath / "index", leveldbCacheSize);
}

fs::path CMerkleTreeStore::GetDataFilename(int merkleTreeFileSuffix) const
{
    return (merkleStorePath / strprintf("%s%08u.dat", "mrk", merkleTreeFileSuffix));
}

FILE* CMerkleTreeStore::OpenMerkleTreeFile(const MerkleTreeDiskPosition& merkleTreeDiskPosition, bool fReadOnly) const
{
    fs::path path = GetDataFilename(merkleTreeDiskPosition.fileSuffix);
    fs::create_directories(path.parent_path());
    FILE* file = fsbridge::fopen(path, "rb+");
    if (!file && !fReadOnly)
    {
        file = fsbridge::fopen(path, "wb+");
    }
    if (!file)
    {
        LogPrintf("Unable to open file %s\n", path.string());
        return nullptr;
    }
    if (merkleTreeDiskPosition.fileOffset)
    {
        if (fseek(file, merkleTreeDiskPosition.fileOffset, SEEK_SET))
        {
            LogPrintf("Unable to seek to position %u of %s\n", merkleTreeDiskPosition.fileOffset, path.string());
            fclose(file);
            return nullptr;
        }
    }
    return file;
}

void CMerkleTreeStore::RemoveOldDataNL(const int suffixOfDataFileToRemove, std::vector<uint256>& blockHashesOfMerkleTreesRemovedOut)
{
    AssertLockHeld(cs_merkleTreeStore);
    // Remove file info and decrease datafile usage
    auto fileInfoToRemove = fileInfoMap.find(suffixOfDataFileToRemove);
    if (fileInfoToRemove != fileInfoMap.cend())
    {
        diskUsage -= fileInfoToRemove->second.fileSize;
        fileInfoMap.erase(fileInfoToRemove);
    }

    // Remove all related positions
    auto diskPositionToRemove = diskPositionMap.cbegin();
    while (diskPositionToRemove != diskPositionMap.cend())
    {
        if (diskPositionToRemove->second.fileSuffix == suffixOfDataFileToRemove)
        {
            blockHashesOfMerkleTreesRemovedOut.push_back(diskPositionToRemove->first);
            diskPositionToRemove = diskPositionMap.erase(diskPositionToRemove);
        }
        else
        {
            ++diskPositionToRemove;
        }
    }

    //If next disk position is part of a removed file, reset its offset
    if (nextDiskPosition.fileSuffix == suffixOfDataFileToRemove)
    {
        nextDiskPosition.fileOffset = 0;
    }
}

void CMerkleTreeStore::AddNewDataNL(const uint256& newBlockHash, const int32_t newBlockHeight, const MerkleTreeDiskPosition& newDiskPosition, uint64_t writtenDataInBytes)
{
    AssertLockHeld(cs_merkleTreeStore);
    //Add disk position
    diskPositionMap[newBlockHash] = newDiskPosition;

    //Move next disk position to the end of written data
    nextDiskPosition = newDiskPosition;
    nextDiskPosition.fileOffset += writtenDataInBytes;

    //Add or update file info
    if (!fileInfoMap.count(newDiskPosition.fileSuffix))
    {
        fileInfoMap[newDiskPosition.fileSuffix] = {newBlockHeight, nextDiskPosition.fileOffset};
    }
    else
    {
        if (fileInfoMap[newDiskPosition.fileSuffix].greatestBlockHeight < newBlockHeight)
        {
            fileInfoMap[newDiskPosition.fileSuffix].greatestBlockHeight = newBlockHeight;
        }
        fileInfoMap[newDiskPosition.fileSuffix].fileSize = nextDiskPosition.fileOffset;
    }

    //Increase complete disk usage taken by Merkle tree data files
    diskUsage += writtenDataInBytes;
}

bool CMerkleTreeStore::PruneDataFilesNL(const uint64_t maxDiskSpace, uint64_t newDataSizeInBytesToAdd, const int32_t chainHeight)
{
    AssertLockHeld(cs_merkleTreeStore);
    if (!newDataSizeInBytesToAdd || (diskUsage + newDataSizeInBytesToAdd) <= maxDiskSpace)
    {
        //No need to prune if no data is being added or disk space limit is kept
        return true;
    }

    if (newDataSizeInBytesToAdd > maxDiskSpace)
    {
        //Do not prune if Merkle Tree size is bigger than the hard disk size limit
        return false;
    }

    /* Prune until usage is below the limit and there are still candidates to prune
     * For database synchronization, store block hashes of Merkle Trees removed and
     * suffixes of data files removed
     */
    std::vector<uint256> blockHashesOfMerkleTreesRemoved;
    std::vector<int> suffixesOfDataFilesRemoved;
    int32_t numberOfLatestBlocksToKeep = static_cast<int32_t>(MIN_BLOCKS_TO_KEEP);
    auto pruningCandidate = fileInfoMap.cbegin();
    while ((diskUsage + newDataSizeInBytesToAdd) > maxDiskSpace && pruningCandidate != fileInfoMap.cend())
    {
        auto nextCandidate = pruningCandidate;
        ++nextCandidate;
        // We don't want to prune data files that contain merkle trees from latest MIN_BLOCKS_TO_KEEP blocks
        if ((chainHeight - pruningCandidate->second.greatestBlockHeight) > numberOfLatestBlocksToKeep)
        {
            boost::system::error_code errorCode;
            fs::remove(GetDataFilename(pruningCandidate->first), errorCode);

            if (errorCode)
            {
                LogPrintf("PruneDataFilesNL: cannot delete mrk file at the moment (%08u): error code %d - %s.\n", pruningCandidate->first, errorCode.value(), errorCode.message());
            }
            else
            {
                LogPrintf("PruneDataFilesNL: deleted mrk file (%08u)\n", pruningCandidate->first);
                RemoveOldDataNL(pruningCandidate->first, blockHashesOfMerkleTreesRemoved);
                suffixesOfDataFilesRemoved.push_back(pruningCandidate->first);
            }
        }
        pruningCandidate = nextCandidate;
    }

    // Sync with the database
    merkleTreeIndexDB->RemoveMerkleTreeData(suffixesOfDataFilesRemoved, blockHashesOfMerkleTreesRemoved, nextDiskPosition, diskUsage);

    if ((diskUsage + newDataSizeInBytesToAdd) > maxDiskSpace)
    {
        //Even after prune, writing newDataSizeInBytesToAdd will cause to go over the disk space limit
        return false;
    }
    return true;
}

bool CMerkleTreeStore::StoreMerkleTree(const Config& config, const uint256& blockHash, const int32_t blockHeight, const CMerkleTree& merkleTreeIn, const int32_t chainHeight)
{
    LOCK(cs_merkleTreeStore);
    // Continue only if it was not yet written
    if (diskPositionMap.count(blockHash))
    {
        return false;
    }

    uint64_t merkleTreeSizeBytes = ::GetSerializeSize(merkleTreeIn, SER_DISK, CLIENT_VERSION);
    
    // Prune data files if needed, to stay below the disk usage limit
    if (!PruneDataFilesNL(config.GetMaxMerkleTreeDiskSpace(), merkleTreeSizeBytes, chainHeight))
    {
        return error("StoreMerkleTree: Merkle Tree of size %u will not be written to keep disk size hard limit", merkleTreeSizeBytes);
    }

    MerkleTreeDiskPosition writeAtPosition = nextDiskPosition;

    // Check if Merkle Tree needs to be written to a new file
    if (writeAtPosition.fileOffset && (writeAtPosition.fileOffset + merkleTreeSizeBytes) > config.GetPreferredMerkleTreeFileSize())
    {
        ++writeAtPosition.fileSuffix;
        writeAtPosition.fileOffset = 0;
    }

    // Open file to append MerkleTree data
    CAutoFile writeToFile{OpenMerkleTreeFile(writeAtPosition), SER_DISK, CLIENT_VERSION};

    if (writeToFile.IsNull())
    {
        return error("StoreMerkleTree: OpenMerkleTreeFile failed");
    }

    try
    {
        writeToFile << merkleTreeIn;
    }
    catch (const std::runtime_error& e)
    {
        return error("StoreMerkleTree: cannot store to data file: %s", e.what());
    }

    AddNewDataNL(blockHash, blockHeight, writeAtPosition, merkleTreeSizeBytes);

    //Sync with the database
    MerkleTreeFileInfo updatedFileInfo = fileInfoMap[writeAtPosition.fileSuffix];
    merkleTreeIndexDB->AddMerkleTreeData(blockHash, writeAtPosition, nextDiskPosition, updatedFileInfo, diskUsage);

    return true;
}

std::unique_ptr<CMerkleTree> CMerkleTreeStore::GetMerkleTree(const uint256& blockHash)
{
    LOCK(cs_merkleTreeStore);
    if (!diskPositionMap.count(blockHash))
    {
        return nullptr;
    }

    CAutoFile readFromFile{OpenMerkleTreeFile(diskPositionMap.find(blockHash)->second, true), SER_DISK, CLIENT_VERSION};

    if (readFromFile.IsNull())
    {
        LogPrintf("GetMerkleTree: OpenMerkleTreeFile failed\n");
        return nullptr;
    }

    try
    {
        auto readMerkleTree = std::make_unique<CMerkleTree>();
        readFromFile >> *readMerkleTree;
        return readMerkleTree;
    }
    catch (const std::runtime_error& e)
    {
        LogPrintf("GetMerkleTree: cannot read from data file: %s\n", e.what());
    }

    return nullptr;
}

bool CMerkleTreeStore::LoadMerkleTreeIndexDB()
{
    LOCK(cs_merkleTreeStore);
    // Clear current data
    ResetStateNL();

    // Measure duration of loading Merkle tree index database
    auto loadMerkleTreeIndexDBStartedAt = std::chrono::high_resolution_clock::now();

    // Load Merkle Tree disk positions
    CMerkleTreeIndexDBIterator<uint256> diskPositionsIterator = merkleTreeIndexDB->GetDiskPositionsIterator();
    uint256 blockHash;
    while (diskPositionsIterator.Valid(blockHash))
    {
        MerkleTreeDiskPosition diskPosition;
        if (!diskPositionsIterator.GetValue(diskPosition))
        {
            ResetStateNL();
            return error("LoadMerkleTreeIndexDB() : failed to read disk position value");
        }
        diskPositionMap[blockHash] = diskPosition;
        diskPositionsIterator.Next();
    }

    // Load Merkle Tree disk position that marks position of next write
    if (!merkleTreeIndexDB->GetNextDiskPosition(nextDiskPosition))
    {
        ResetStateNL();
        return error("LoadMerkleTreeIndexDB() : failed to read next disk position value");
    }

    // Load Merkle Tree file infos
    CMerkleTreeIndexDBIterator<int> fileInfosIterator = merkleTreeIndexDB->GetFileInfosIterator();
    int fileSuffix = 0;
    while (fileInfosIterator.Valid(fileSuffix))
    {
        MerkleTreeFileInfo fileInfo;
        if (!fileInfosIterator.GetValue(fileInfo))
        {
            ResetStateNL();
            return error("LoadMerkleTreeIndexDB() : failed to read file info value");
        }
        fileInfoMap[fileSuffix] = fileInfo;
        fileInfosIterator.Next();
    }

    // Load Merkle Trees disk usage
    if (!merkleTreeIndexDB->GetDiskUsage(diskUsage))
    {
        ResetStateNL();
        return error("LoadMerkleTreeIndexDB() : failed to read disk usage value");
    }

    auto loadMerkleTreeIndexDBStoppedAt = std::chrono::high_resolution_clock::now();
    auto loadMerkleTreeIndexDBDuration = std::chrono::duration_cast<std::chrono::milliseconds>(loadMerkleTreeIndexDBStoppedAt - loadMerkleTreeIndexDBStartedAt);

    LogPrintf("LoadMerkleTreeIndexDB() : CMerkleTreeIndexDB loaded in " + std::to_string(loadMerkleTreeIndexDBDuration.count())  + "ms\n");

    return true;
}

void CMerkleTreeStore::ResetStateNL()
{
    AssertLockHeld(cs_merkleTreeStore);
    diskPositionMap.clear();
    nextDiskPosition.fileOffset = 0;
    nextDiskPosition.fileSuffix = 0;
    fileInfoMap.clear();
    diskUsage = 0;
}

CMerkleTreeFactory::CMerkleTreeFactory(const fs::path& storePath, size_t databaseCacheSize, size_t maxNumberOfThreadsForCalculations)
    :cacheSizeBytes(0), merkleTreeStore(CMerkleTreeStore(storePath, databaseCacheSize)),
    merkleTreeThreadPool(std::make_unique<CThreadPool<CQueueAdaptor>>("MerkleTreeThreadPool", maxNumberOfThreadsForCalculations))
{
    LogPrintf("Using up to %u additional threads for Merkle tree computation\n", maxNumberOfThreadsForCalculations - 1);

    // Load index data from the database
    merkleTreeStore.LoadMerkleTreeIndexDB();
};

CMerkleTreeRef CMerkleTreeFactory::GetMerkleTree(const Config& config, CBlockIndex& blockIndex, const int32_t currentChainHeight)
{
    {
        LOCK(cs_merkleTreeFactory);
        // Try to get Merkle Tree from memory cache
        auto merkleTreeMapIterator = merkleTreeMap.find(blockIndex.GetBlockHash());
        if (merkleTreeMapIterator != merkleTreeMap.cend())
        {
            return merkleTreeMapIterator->second;
        }
    }

    // Merkle Tree for this block not found in cache, read it from disk
    auto merkleTreePtr = merkleTreeStore.GetMerkleTree(blockIndex.GetBlockHash());
    if (!merkleTreePtr)
    {
        /* Merkle Tree of this block was not found or cannot be read from data files on disk.
         * Calculate it from block stream and store it to the disk.
         */
        auto stream = GetDiskBlockStreamReader(blockIndex.GetBlockPos());
        if (!stream)
        {
            // This should be handled by the caller - block cannot be read from the disk
            return nullptr;
        }

        merkleTreePtr = std::make_unique<CMerkleTree>(*stream, merkleTreeThreadPool.get());
        merkleTreeStore.StoreMerkleTree(config, blockIndex.GetBlockHash(), static_cast<int32_t>(blockIndex.nHeight), *merkleTreePtr, currentChainHeight);
    }

    // Put the requested Merkle Tree into the cache
    CMerkleTreeRef merkleTreeRef = std::move(merkleTreePtr);
    Insert(blockIndex.GetBlockHash(), merkleTreeRef, config);
    return merkleTreeRef;
}

void CMerkleTreeFactory::Insert(const uint256& blockHash, CMerkleTreeRef merkleTree, const Config& config)
{
    LOCK(cs_merkleTreeFactory);
    if (merkleTreeMap.count(blockHash))
    {
        // Skip if Merkle Tree is already in the cache
        return;
    }
    // Get merkle tree size and add size of two block hashes (key in map and queue) 
    uint64_t merkleTreeSizeInCache = merkleTree->GetSizeInBytes() + 2 * sizeof(uint256);
    if (merkleTreeSizeInCache > config.GetMaxMerkleTreeMemoryCacheSize())
    {
        // Skip if Merkle Tree is too big
        return;
    }
    while (cacheSizeBytes + merkleTreeSizeInCache > config.GetMaxMerkleTreeMemoryCacheSize())
    {
        // Remove first Merkle Tree in cache and subtract its size and size (+ two keys in map and queue)
        cacheSizeBytes -= merkleTreeMap[merkleTreeQueue.front()]->GetSizeInBytes() + 2 * sizeof(uint256);
        merkleTreeMap.erase(merkleTreeQueue.front());
        merkleTreeQueue.pop();
    }
    merkleTreeMap.insert(std::make_pair(blockHash, merkleTree));
    merkleTreeQueue.push(blockHash);
    cacheSizeBytes += merkleTreeSizeInCache;
}
