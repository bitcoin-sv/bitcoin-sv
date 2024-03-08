// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "merkletreestore.h"

#include "block_file_access.h"
#include "util.h"
#include "config.h"
#include "clientversion.h"
#include <regex>

/* Global state of Merkle Tree factory.
 * Merkle Trees are stored in memory cache and on disk when requested (RPC).
 */
std::unique_ptr<CMerkleTreeFactory> pMerkleTreeFactory = nullptr;

CMerkleTreeStore::CMerkleTreeStore(const fs::path& storePath, size_t leveldbCacheSize)
    : diskUsage(0), merkleStorePath(storePath), writeIndexToDatabase(false), indexNotLoaded(true), databaseCacheSize(leveldbCacheSize)
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

MerkleTreeFileInfoMap::const_iterator CMerkleTreeStore::RemoveOldDataNL(MerkleTreeFileInfoMap::const_iterator fileInfoToRemove, std::vector<uint256>& blockHashesOfMerkleTreesRemovedOut)
{
    if (fileInfoToRemove == fileInfoMap.cend())
    {
        return fileInfoToRemove;
    }

    AssertLockHeld(cs_merkleTreeStore);
    const int suffixOfDataFileToRemove = fileInfoToRemove->first;
    // Remove file info and decrease datafile usage
    diskUsage -= fileInfoToRemove->second.fileSize;
    auto nextFileInfo = fileInfoMap.erase(fileInfoToRemove);

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

    return nextFileInfo;
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

bool CMerkleTreeStore::PruneDataFilesNL(const Config& config, uint64_t newDataSizeInBytesToAdd, const int32_t chainHeight)
{
    AssertLockHeld(cs_merkleTreeStore);

    const uint64_t maxDiskSpace { config.GetMaxMerkleTreeDiskSpace() };

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

    // Mark index as out of sync when we need to prune data files
    if (writeIndexToDatabase && !merkleTreeIndexDB->SetIndexOutOfSync(true))
    {
        //Don't prune data files if we can't mark index as out of sync
        return error("PruneDataFilesNL: Cannot mark index as out of sync. Merkle Tree data files will not be pruned.");
    }

    /* Prune until usage is below the limit and there are still candidates to prune
     * For database synchronization, store block hashes of Merkle Trees removed and
     * suffixes of data files removed
     */
    std::vector<uint256> blockHashesOfMerkleTreesRemoved;
    std::vector<int> suffixesOfDataFilesRemoved;
    auto pruningCandidate = fileInfoMap.cbegin();
    while ((diskUsage + newDataSizeInBytesToAdd) > maxDiskSpace && pruningCandidate != fileInfoMap.cend())
    {
        // We don't want to prune data files that contain merkle trees from unpruned recent blocks
        if ((chainHeight - pruningCandidate->second.greatestBlockHeight) > config.GetMinBlocksToKeep())
        {
            boost::system::error_code errorCode;
            int removeFileWithSuffix = pruningCandidate->first;
            fs::remove(GetDataFilename(removeFileWithSuffix), errorCode);

            if (errorCode)
            {
                LogPrintf("PruneDataFilesNL: cannot delete mrk file at the moment (%08u): error code %d - %s.\n", removeFileWithSuffix, errorCode.value(), errorCode.message());
                ++pruningCandidate;
            }
            else
            {
                LogPrintf("PruneDataFilesNL: deleted mrk file (%08u)\n", removeFileWithSuffix);
                pruningCandidate = RemoveOldDataNL(pruningCandidate, blockHashesOfMerkleTreesRemoved);
                suffixesOfDataFilesRemoved.push_back(removeFileWithSuffix);
            }
        }
        else
        {
            ++pruningCandidate;
        }
    }

    // Sync with the database
    if (writeIndexToDatabase)
    {
        bool databaseUpdateFailed = !merkleTreeIndexDB->RemoveMerkleTreeData(suffixesOfDataFilesRemoved, blockHashesOfMerkleTreesRemoved, nextDiskPosition, diskUsage);
        ResetIndexOutOfSyncNL(databaseUpdateFailed, "PruneDataFilesNL");
    }

    if ((diskUsage + newDataSizeInBytesToAdd) > maxDiskSpace)
    {
        //Even after prune, writing newDataSizeInBytesToAdd will cause to go over the disk space limit
        return false;
    }
    return true;
}

bool CMerkleTreeStore::StoreMerkleTree(const Config& config, const CMerkleTree& merkleTreeIn, const int32_t chainHeight)
{
    LOCK(cs_merkleTreeStore);
    // Continue only if index was successfully loaded or rebuilt and merkle tree was not yet written
    if (indexNotLoaded || diskPositionMap.count(merkleTreeIn.GetBlockHash()))
    {
        return false;
    }

    uint64_t merkleTreeSizeBytes = ::GetSerializeSize(merkleTreeIn, SER_DISK, CLIENT_VERSION);
    
    // Prune data files if needed, to stay below the disk usage limit
    if (!PruneDataFilesNL(config, merkleTreeSizeBytes, chainHeight))
    {
        return error("StoreMerkleTree: Merkle Tree of size %u will not be written to keep disk size hard limit", merkleTreeSizeBytes);
    }

    // Check disk space before write, there should be at least nMinDiskSpace available
    uint64_t diskFreeBytesAvailable = fs::space(merkleStorePath).available;
    if (diskFreeBytesAvailable < (nMinDiskSpace + merkleTreeSizeBytes))
    {
        return error("StoreMerkleTree: Disk space is low (%lu bytes available for directory '%s' %lu required), "
                     "Merkle Trees will not be written.",
                     diskFreeBytesAvailable, merkleStorePath.string(), (nMinDiskSpace + merkleTreeSizeBytes));
    }

    // Mark index as out of sync when writing to data files
    if (writeIndexToDatabase && !merkleTreeIndexDB->SetIndexOutOfSync(true))
    {
        //Don't store to disk if we can't mark index as out of sync
        return error("StoreMerkleTree: Cannot mark index as out of sync. Merkle Tree will not be stored to disk.");
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

    AddNewDataNL(merkleTreeIn.GetBlockHash(), merkleTreeIn.GetBlockHeight(), writeAtPosition, merkleTreeSizeBytes);

    //Sync with the database
    MerkleTreeFileInfo updatedFileInfo = fileInfoMap[writeAtPosition.fileSuffix];
    if (writeIndexToDatabase)
    {
        bool databaseUpdateFailed = !merkleTreeIndexDB->AddMerkleTreeData(merkleTreeIn.GetBlockHash(), writeAtPosition, nextDiskPosition, updatedFileInfo, diskUsage);
        ResetIndexOutOfSyncNL(databaseUpdateFailed, "StoreMerkleTree");
    }

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

    // Check if Merkle Trees index is out of sync
    bool isIndexOutOfSync = true;
    if (!merkleTreeIndexDB->GetIndexOutOfSync(isIndexOutOfSync))
    {
        LogPrintf("LoadMerkleTreeIndexDB() : cannot check if index is out of sync\n");
    }
    if (isIndexOutOfSync || (!isIndexOutOfSync && !LoadDBIndexNL()))
    {
        // Rebuild index if database is not in sync or it cannot be loaded
        LogPrintf("LoadMerkleTreeIndexDB() : Index in the database is out of sync, rebuilding index from current data files\n");
        return ReindexMerkleTreeStoreNL();
    }
    return true;
}

bool CMerkleTreeStore::LoadDBIndexNL()
{
    AssertLockHeld(cs_merkleTreeStore);
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
            return error("LoadDBIndexNL() : failed to read disk position value");
        }
        diskPositionMap[blockHash] = diskPosition;
        diskPositionsIterator.Next();
    }

    // Load Merkle Tree disk position that marks position of next write
    if (!merkleTreeIndexDB->GetNextDiskPosition(nextDiskPosition))
    {
        ResetStateNL();
        return error("LoadDBIndexNL() : failed to read next disk position value");
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
            return error("LoadDBIndexNL() : failed to read file info value");
        }
        fileInfoMap[fileSuffix] = fileInfo;
        fileInfosIterator.Next();
    }

    // Load Merkle Trees disk usage
    if (!merkleTreeIndexDB->GetDiskUsage(diskUsage))
    {
        ResetStateNL();
        return error("LoadDBIndexNL() : failed to read disk usage value");
    }

    auto loadMerkleTreeIndexDBStoppedAt = std::chrono::high_resolution_clock::now();
    auto loadMerkleTreeIndexDBDuration = std::chrono::duration_cast<std::chrono::milliseconds>(loadMerkleTreeIndexDBStoppedAt - loadMerkleTreeIndexDBStartedAt);

    LogPrintf("LoadDBIndexNL() : CMerkleTreeIndexDB loaded in " + std::to_string(loadMerkleTreeIndexDBDuration.count())  + "ms\n");

    writeIndexToDatabase = true;
    indexNotLoaded = false;
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
    writeIndexToDatabase = false;
    indexNotLoaded = true;
}

void CMerkleTreeStore::ResetIndexOutOfSyncNL(bool databaseUpdateFailed, const std::string& logPrefix)
{
    AssertLockHeld(cs_merkleTreeStore);
    if (databaseUpdateFailed)
    {
        // Index could not be updated on database
        LogPrintf("%s: Could not update the index. Index will no longer be updated and will be rebuilt on next initialization.\n", logPrefix);
        writeIndexToDatabase = false;
    }
    else
    {
        if (!merkleTreeIndexDB->SetIndexOutOfSync(false))
        {
            // Database was successfully updated but index could not be marked as in sync
            LogPrintf("%s: Cannot mark index as in sync. Index will no longer be updated and will be rebuilt on next initialization.\n", logPrefix);
            writeIndexToDatabase = false;
        }
    }
}

bool CMerkleTreeStore::ReindexMerkleTreeStoreNL()
{
    AssertLockHeld(cs_merkleTreeStore);
    // Measure duration of index creation
    auto reindexStartedAt = std::chrono::high_resolution_clock::now();
    // Check all mrk<suffix>.dat files in merkleStorePath to get min and max suffixes first
    // RegEx to check data file base name (name without the extension)
    static const std::regex merkleDataFileBaseNameRegEx("^mrk([0-9]+)$");
    int minSuffix = std::numeric_limits<int>::max();
    int maxSuffix = 0;
    for (fs::directory_iterator it(merkleStorePath); it != fs::directory_iterator(); ++it)
    {
        std::string merkleDataFileBaseName = it->path().stem().string();
        std::smatch merkleDataFileBaseNameMatch;
        if (!fs::is_directory(*it) && it->path().extension() == ".dat" && std::regex_search(merkleDataFileBaseName, merkleDataFileBaseNameMatch, merkleDataFileBaseNameRegEx))
        {
            int currentFileSuffix = 0;
            try
            {
                currentFileSuffix = std::stoi(merkleDataFileBaseNameMatch[1].str());
            }
            catch (std::exception& e)
            {
                LogPrintf("ReindexMerkleTreeStoreNL() : Cannot get suffix from %s data file, skipping\n", it->path().string());
                continue;
            }

            if (currentFileSuffix < minSuffix)
            {
                minSuffix = currentFileSuffix;
            }
            if (currentFileSuffix > maxSuffix)
            {
                maxSuffix = currentFileSuffix;
            }
        }
    }

    // Clear current data and wipe the database
    ResetStateNL();
    merkleTreeIndexDB.reset();
    merkleTreeIndexDB = std::make_unique<CMerkleTreeIndexDB>(merkleStorePath / "index", databaseCacheSize, false, true);

    // Open current data files in order from minSuffix to maxSuffix
    for (int currentSuffix = minSuffix; currentSuffix <= maxSuffix; ++currentSuffix)
    {
        MerkleTreeDiskPosition currentPosition{currentSuffix, 0};
        fs::path currentFilePath = GetDataFilename(currentPosition.fileSuffix);
        if (!fs::exists(currentFilePath))
        {
            // data file with this suffix does not exist, move to next candidate
            continue;
        }
        uint64_t currentFileSize = fs::file_size(currentFilePath);
        CAutoFile readFromFile{OpenMerkleTreeFile(currentPosition, true), SER_DISK, CLIENT_VERSION};
        if (readFromFile.IsNull())
        {
            ResetStateNL();
            return error("ReindexMerkleTreeStoreNL() : failed to open an existing data file");
        }
        while(readFromFile.Get())
        {
            CMerkleTree merkleTree;
            try
            {
                readFromFile >> merkleTree;
            }
            catch (const std::runtime_error& e)
            {
                // Error reading merkle tree
                ResetStateNL();
                return error("ReindexMerkleTreeStoreNL() : failed to read merkle tree from file %s at position %u", currentFilePath.string(), currentPosition.fileOffset);
            }

            // Update index
            uint64_t merkleTreeSizeBytes = ::GetSerializeSize(merkleTree, SER_DISK, CLIENT_VERSION);
            AddNewDataNL(merkleTree.GetBlockHash(), merkleTree.GetBlockHeight(), currentPosition, merkleTreeSizeBytes);
            MerkleTreeFileInfo updatedFileInfo = fileInfoMap[currentPosition.fileSuffix];
            if (!merkleTreeIndexDB->AddMerkleTreeData(merkleTree.GetBlockHash(), currentPosition, nextDiskPosition, updatedFileInfo, diskUsage))
            {
                // Failed to update index in the database
                ResetStateNL();
                return error("ReindexMerkleTreeStoreNL() : failed to update index in the database");
            }

            // Move to the next position
            currentPosition = nextDiskPosition;
            // Check if we are at the end of a file
            if (static_cast<uint64_t>(ftell(readFromFile.Get())) == currentFileSize)
            {
                //End of file, move to the next data file
                readFromFile.reset();
            }
        }
    }

    //Set index as in sync when all data files were read and index was updated
    if (merkleTreeIndexDB->SetIndexOutOfSync(false))
    {
        writeIndexToDatabase = true;
        indexNotLoaded = false;
        auto reindexStoppedAt = std::chrono::high_resolution_clock::now();
        auto reindexDuration = std::chrono::duration_cast<std::chrono::milliseconds>(reindexStoppedAt - reindexStartedAt);
        LogPrintf("ReindexMerkleTreeStoreNL() : Merkle Trees index creation took " + std::to_string(reindexDuration.count()) + "ms\n");
        return true;
    }

    return error("ReindexMerkleTreeStoreNL() : Cannot mark index as in sync.");
}

CMerkleTreeFactory::CMerkleTreeFactory(const fs::path& storePath, size_t databaseCacheSize, size_t maxNumberOfThreadsForCalculations)
    :cacheSizeBytes(0), merkleTreeStore(CMerkleTreeStore(storePath, databaseCacheSize)),
    merkleTreeThreadPool(std::make_unique<CThreadPool<CQueueAdaptor>>(true, "MerkleTreeThreadPool", maxNumberOfThreadsForCalculations))
{
    LogPrintf("Using up to %u additional threads for Merkle tree computation\n", maxNumberOfThreadsForCalculations - 1);

    // Try to load index data from the database or rebuild index if needed
    if (!merkleTreeStore.LoadMerkleTreeIndexDB())
    {
        LogPrintf("Merkle Trees will not be stored to disk until next successful initialization.\n");
    }
}

CMerkleTreeRef CMerkleTreeFactory::GetMerkleTree(const Config& config, const CBlockIndex& blockIndex, const int32_t currentChainHeight)
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
        auto stream = blockIndex.GetDiskBlockStreamReader();

        if (!stream)
        {
            // This should be handled by the caller - block cannot be read from the disk
            return nullptr;
        }

        merkleTreePtr = std::make_unique<CMerkleTree>(*stream, blockIndex.GetBlockHash(), static_cast<int32_t>(blockIndex.GetHeight()), merkleTreeThreadPool.get());
        merkleTreeStore.StoreMerkleTree(config, *merkleTreePtr, currentChainHeight);
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
