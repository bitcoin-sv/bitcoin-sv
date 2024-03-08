// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#ifndef BITCOIN_MERKLETREESTORE_H
#define BITCOIN_MERKLETREESTORE_H

#include "block_hasher.h"
#include "validation.h"
#include "merkletree.h"
#include "fs.h"
#include "merkletreedb.h"
#include <queue>

typedef std::unordered_map<uint256, MerkleTreeDiskPosition, BlockHasher> MerkleTreeDiskPositionMap;
typedef std::map<int, MerkleTreeFileInfo> MerkleTreeFileInfoMap;

/*
 * Class used to store Merkle Trees into data files and to keep information about their data files
 * Data is synchronized with levedb on every update (write and prune).
 * Merkle Tree data (CMerkleTree) is serialized and stored to a merkle tree data file in "merkle" folder.
 * The maximum file size is limited and can be configured with -preferredmerkletreefilesize (by default 32 MiB).
 * For every Merkle Tree stored we keep its position (file suffix and offset) in a map with block has as a key.
 * We also keep disk size and biggest block height for each data file on a disk.
 " The maximum total size of all files is limited and can be configured with -maxmerkletreediskspace.
 * Before we save Merkle Tree to a data file we need to prune older data files if we reach disk size limitation.
 * Data files that contain Merkle Trees from one of the configured minimum number of recent blocks to keep
 * are not pruned. That is why we need to keep the biggest block height for each data file.
 * Every time we need to prune and/or write data to disk, we synchronize MerkleTree data files state to leveldb.
 */
class CMerkleTreeStore
{
private:
    CCriticalSection cs_merkleTreeStore;
    // Merkle Tree disk position map with block's hash as key
    MerkleTreeDiskPositionMap diskPositionMap;
    // disk position into which we can write new Merkle Tree
    MerkleTreeDiskPosition nextDiskPosition;
    // File info map with file's suffix as key
    MerkleTreeFileInfoMap fileInfoMap;
    // Disk size in bytes taken by all Merkle Tree data files
    uint64_t diskUsage;
    // Absolute path to the folder containing Merkle Tree data files
    const fs::path merkleStorePath;
    /**
     * Defines if we can write index to the database.
     * Value is set to false when:
     * - index cannot be updated after data files were changed
     * - index can be updated after data files were changed, but is still marked as out of sync
     * - index cannot be rebuilt from data files
     * When set to false, no changes are done on the database anymore. Merkle trees
     * are still written to data files and index is kept in memory.
     * Index in the database can be rebuilt when node is restarted.
     * Value is set to true only on initialization when:
     * - index was successfully loaded from database
     * - index was successfully rebuilt from data files
     */
    bool writeIndexToDatabase;
    /**
     * Defines if index was successfully loaded from database or rebuilt from data files.
     * Value is set to true when:
     * - index cannot be rebuilt from data files
     * Index is rebuilt from data files during initialization when it cannot be loaded
     * from database or when it is marked as out of sync.
     * When indexNotLoaded is set to true, merkle trees will not be stored to data files.
     * Calling StoreMerkleTree will have no affect.
     * Value is set to false only on initialization when:
     * - index was successfully loaded from the database
     * - index was successfully rebuilt from data files
     */
    bool indexNotLoaded;
    // LevelDB cache size
    size_t databaseCacheSize;
    // Merkle Tree data files information stored in the database
    std::unique_ptr<CMerkleTreeIndexDB> merkleTreeIndexDB;

    /*
     * Returns absolute path of Merkle Tree data file with specified suffix.
     * This function does not check file existence.
     */
    fs::path GetDataFilename(int merkleTreeFileSuffix) const;

    /*
     * Opens Merkle Tree file. By default file is opened for writing.
     * Returns handle to a file with specified number suffix at byte offset
     * Returns nullptr in case of issues
     */
    FILE* OpenMerkleTreeFile(const MerkleTreeDiskPosition& merkleTreeDiskPosition, bool fReadOnly = false) const;

    /*
     * Removes all data file's disk positions for the removed file specified by fileInfoToRemove
     * Block hashes of removed Merkle Trees will be put into blockHashesOfMerkleTreesRemovedOut
     * Returns the iterator to the next data file.
     */
    [[nodiscard]] MerkleTreeFileInfoMap::const_iterator RemoveOldDataNL(MerkleTreeFileInfoMap::const_iterator fileInfoToRemove, std::vector<uint256>& blockHashesOfMerkleTreesRemovedOut);

    /*
     * Adds new disk position
     */
    void AddNewDataNL(const uint256& newBlockHash, const int32_t newBlockHeight, const MerkleTreeDiskPosition& newDiskPosition, uint64_t writtenDataInBytes);

    /*
     * If adding new data of size newDataSizeInBytesToAdd causes to go over the limit (configured with
     * -maxmerkletreediskspace), this function removes older data files to release more disk space.
     * chainHeight should be set to current chain height to prevent pruning of Merkle Trees from still kept
     * recent blocks.
     * Returns false if newDataSizeInBytesToAdd still causes disk size to go over the limit even after
     * the purge. 
     */
    bool PruneDataFilesNL(const Config& config, uint64_t newDataSizeInBytesToAdd, const int32_t chainHeight);

    /**
     * Clears Merkle Trees index and sets it back to initial state.
     * This is used before or when index data cannot be loaded from the database.
     */
    void ResetStateNL();

    /**
     * Helper function used after write or prune of data files. 
     * Depending on databaseUpdateFailed it either:
     * - marks index as NOT out of sync
     *   - when databaseeUpdateFailed is false
     * - sets writeIndexToDatabase to false to prevent future database changes
     *   - when databaseeUpdateFailed is true
     *   - when it cannot mark index as NOT out of sync
     *   - index is already marked as out of sync and it will be rebuilt on next node start
     * logPrefix is used as prefix for log entries.
     */
    void ResetIndexOutOfSyncNL(bool databaseUpdateFailed, const std::string& logPrefix);

    /**
     * Creates new index from existing Merkle Tree data files.
     * Returns false if index could not be created.
     */
    bool ReindexMerkleTreeStoreNL();

    /**
     * Loads index data from the database.
     * Returns false if index could not be read from the database.
     */
    bool LoadDBIndexNL();

public:
    /**
     * Constructs a Merkle Tree store on specified path and with configured Merkle tree index database cache.
     */
    CMerkleTreeStore(const fs::path& storePath, size_t leveldbCacheSize);

    /**
     * Stores given merkleTreeIn data to disk.
     * merkleTreeIn must have proper blockHash and blockHeight set.
     * chainHeight should be set to the current chain height to prevent pruning of latest Merkle Trees
     * Returns false if Merkle Tree with given blockHash was already written or in case of errors.
     */
    bool StoreMerkleTree(const Config& config, const CMerkleTree& merkleTreeIn, const int32_t chainHeight);

    /**
     * Reads Merkle Tree data represented by blockHash.
     * Returns a unique pointer of the Merkle Tree read from the data file or nullptr in case of errors.
     */
    std::unique_ptr<CMerkleTree> GetMerkleTree(const uint256& blockHash);

    /**
     * Loads Merkle Tree data files information from the database.
     * Returns false if loading data from the database was not successful.
     */
    bool LoadMerkleTreeIndexDB();
};

/*
 * CMerkleTreeFactory is used to handle cached Merkle Trees. Merkle Trees that were recently requested are 
 * kept in a memory cache. This is a FIFO map with keys (block hashes) stored in a queue.
 * Cache size is limited to 32 MiB by default and can be configured with -maxmerkletreememcachesize parameter.
 * Oldest Merkle Trees are removed to keep cache size limitation.
 * Additionally Merkle trees are stored in data files on disk and information on these data files is stored
 * in the database.
 */

typedef std::shared_ptr<const CMerkleTree> CMerkleTreeRef;

class CMerkleTreeFactory
{
private:
    CCriticalSection cs_merkleTreeFactory;
    std::unordered_map<uint256, CMerkleTreeRef, BlockHasher> merkleTreeMap;
    std::queue<uint256> merkleTreeQueue;
    uint64_t cacheSizeBytes;
    CMerkleTreeStore merkleTreeStore;
    std::unique_ptr<CThreadPool<CQueueAdaptor>> merkleTreeThreadPool;

public:
    /**
     * Constructs a Merkle Tree factory instance used to manage creation and storage of Merkle Trees.
     * storePath is an absolute path to the folder where merkle tree data files are stored.
     * databaseCacheSize should be set to leveldb cache size for merkle trees index.
     * maxNumberOfThreadsForCalculations should be set to maximum number of threads used in parallel
     * Merkle Tree calculations.
     */
    CMerkleTreeFactory(const fs::path& storePath, size_t databaseCacheSize, size_t maxNumberOfThreadsForCalculations);
    /**
     * Returns CMerkleTreeRef from Merkle Tree cache. If it is not found in the memory cache,
     * Merkle Tree is read from the disk. If it is not found on the disk, Merkle Tree is calculated
     * first, stored to disk and in memory cache. Memory cache size is limited and can be configured
     * with -maxmerkletreememcachesize parameter. Function takes config to retrieve configured limitations
     * and blockIndex needed to read and/or create related Merkle Tree. Additionally, currentChainHeight
     * must be set to height of an active chain. This is needed during purging of Merkle tree data files
     * to prevent removal of Merkle trees from unpruned recent blocks.
     * Returns null if block could not be read from disk to create a Merkle Tree.
     */
    CMerkleTreeRef GetMerkleTree(const Config& config, const CBlockIndex& blockIndex, const int32_t currentChainHeight);
private:
    /**
     * Inserts merkleTree into a cached map with key blockHash.
     * By default cache size is limited to 32 MiB and can be configured with
     * -maxmerkletreememcachesize. If cache size limitation is reached,
     * Merkle Trees that were added first are removed (FIFO).
     */
    void Insert(const uint256& blockHash, CMerkleTreeRef merkleTree, const Config& config);
};

/** Access to global Merkle Tree factory */
extern std::unique_ptr<CMerkleTreeFactory> pMerkleTreeFactory;

#endif // MERKLETREESTORE_H
