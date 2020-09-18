// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "merkletreestore.h"
#include "util.h"
#include "config.h"
#include "clientversion.h"

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

void CMerkleTreeStore::RemoveOldDataNL(const int suffixOfDataFileToRemove)
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

    // Prune until usage is below the limit and there are still candidates to prune
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
                RemoveOldDataNL(pruningCandidate->first);
            }
        }
        pruningCandidate = nextCandidate;
    }

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
