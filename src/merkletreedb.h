// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#ifndef BITCOIN_MERKLEDB_H
#define BITCOIN_MERKLEDB_H

#include "dbwrapper.h"
#include "chain.h"

/**
  * Struct that holds info about file location of one Merkle Tree
  * Files are stored in 'merkle' folder
  * fileSuffix points to an actual file in which MerkleTree was stored. Name of the file is:
  * mrk<formatedFileSuffix>.dat
  *
  */
struct MerkleTreeDiskPosition
{
    int fileSuffix{0};
    uint64_t fileOffset{0};

    ADD_SERIALIZE_METHODS

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(VARINT(fileSuffix));
        READWRITE(VARINT(fileOffset));
    }
};

/* MerkleTreeFileInfo represents one of the data files used to store Merkle Trees.
 * Because one data file can store multiple Merkle Trees, greatestBlockHeight will
 * contain height of a block that is greatest among all Merkle Trees stored in this
 * data file. This is needed to prevent pruning of this data file because we want to
 * keep Merkle Trees from the latest configured minimum number of blocks to keep.
 */
struct MerkleTreeFileInfo
{
    int32_t greatestBlockHeight{0};
    uint64_t fileSize{0};

    ADD_SERIALIZE_METHODS

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(VARINT(greatestBlockHeight));
        READWRITE(VARINT(fileSize));
    }
};

/**
 * CMerkleTreeIndexDBIterator is used to iterate through different key/value record types
 * in the Merkle tree index database.
 * In merkle tree index database we can iterate through list of disk positions with block
 * hashes as key and list of data files information with file suffix as key.
 " Typname T represents record key and must support serialization and deserialization. This
 * class uses the following record key types:
 * - uint256 (block hash)
 * - int (data file suffix)
 */
template <typename T>
class CMerkleTreeIndexDBIterator
{
private:

    std::unique_ptr<CDBIterator> iterator;
    const char recordPrefix;

public:
    /**
     * Constructs a merkle tree index database iterator.
     * wrapper is a merkle tree index database wrapper.
     * key is a pair of database prefix that represents stored records and
     * type of a record key.
     */
    CMerkleTreeIndexDBIterator(CDBWrapper& wrapper, const std::pair<char, T>& key)
        : iterator(wrapper.NewIterator()), recordPrefix(key.first)
    {
        iterator->Seek(key);
    }

    /**
     * Returns true if iterator points to a proper record.
     * Proper record is pointed by a key that holds record prefix
     * defined in the constructor.
     * In this case keyOut will hold key of a record that iterator points to.
     */
    bool Valid(T& keyOut)
    {
        std::pair<char, T> currentKey;
        if (iterator->Valid() && iterator->GetKey(currentKey) && currentKey.first == recordPrefix)
        {
            keyOut = currentKey.second;
            return true;
        }
        return false;
    }

    /**
     * Sets valueOut with the record value iterator points to.
     * Returns false if value can not be retrieved from the record.
     * Typname V represents record value and must support serialization and deserialization.
     * This method is used for the following record value types:
     * - MerkleTreeDiskPosition
     * - MerkleTreeFileInfo
     */
    template <typename V> bool GetValue(V& valueOut)
    {
        return iterator->GetValue(valueOut);
    }

    /**
     * Moves iterator to the next record in the database.
     */
    void Next()
    {
        iterator->Next();
    }
};

/** Access to the merkle tree index database (merkle/index/) */
class CMerkleTreeIndexDB
{
private:
    // Prefix to store map of MerkleTreeDiskPosition values with uint256 (block hash) as a key.
    static constexpr char DB_MERKLE_TREE_DISK_POSITIONS = 'm';
    // Prefix to store single MerkleTreeDiskPosition value
    static constexpr char DB_NEXT_MERKLE_TREE_DISK_POSITION = 'n';
    // Prefix to store map of MerkleTreeFileInfo values with int (data file prefix) as a key
    static constexpr char DB_MERKLE_TREE_FILE_INFOS = 'i';
    // Prefix to store single uint64_t (Merkle Trees disk usage) value
    static constexpr char DB_MERKLE_TREES_DISK_USAGE = 'd';
    // Prefix to store single bool (Merkle Trees index is out of sync) value
    static constexpr char DB_MERKLE_TREES_INDEX_OUT_OF_SYNC = 's';
    // Database wrapper
    CDBWrapper merkleTreeIndexDB;

public:

    /**
     * Initializes Merkle tree index database.
     * databasePath is an absolute path of a folder where the database is written to.
     * leveldbCacheSize is leveldb cache size for this database.
     * fMemory is false by default. If set to true, leveldb's memory environment will be used.
     * fWipe is false by default. If set to true it will remove all existing data in this database.
     */
    CMerkleTreeIndexDB(const fs::path& databasePath, size_t leveldbCacheSize, bool fMemory = false, bool fWipe = false);

    /**
     * Returns iterator used to move through and read Merkle Tree disk positions stored in the database
     */
    CMerkleTreeIndexDBIterator<uint256> GetDiskPositionsIterator();

    /**
     * Reads next disk position stored in the database to nextDiskPositionOut. It marks position to which next
     * merkle tree will be written. Returns false in case record could not be read from the database.
     */
    bool GetNextDiskPosition(MerkleTreeDiskPosition& nextDiskPositionOut);

    /**
     * Returns iterator used to move through and read Merkle Tree file information stored in the database
     */
    CMerkleTreeIndexDBIterator<int> GetFileInfosIterator();

    /**
     * Reads disk usage value stored in the database to diskUsageOut.
     * Returns false in case record could not be read from the database.
     */
    bool GetDiskUsage(uint64_t& diskUsageOut);

    /**
     * Used to add new Merkle Tree info into the database to sync it with written data.
     * When Merkle tree is written to a data file its disk position newDiskPosition is
     * inserted under newBlockHash key. Next disk position must be updated with
     * updatedNextDiskPosition. We keep data file size and top block height. This
     * is set in updatedFileInfo. updatedDiskUsage sets the complete disk size taken by
     * all Merkle Tree data files.
     */
    bool AddMerkleTreeData(const uint256& newBlockHash, const MerkleTreeDiskPosition& newDiskPosition,
                           const MerkleTreeDiskPosition &updatedNextDiskPosition,
                           const MerkleTreeFileInfo& updatedFileInfo, const uint64_t updatedDiskUsage);

    /**
     * When data files are pruned, this function is used to sync changed data with the database.
     * List of data files' suffixes that were removed should be put into suffixesOfDataFilesRemoved and list
     * of all block hashes that represent removed Merkle Trees should be put in blockHashesOfMerkleTreesRemoved.
     * Next disk position and disk size must be updated with updatedNextDiskPosition and updatedDiskUsage respectively.
     */
    bool RemoveMerkleTreeData(const std::vector<int>& suffixesOfDataFilesRemoved, const std::vector<uint256>& blockHashesOfMerkleTreesRemoved,
                              const MerkleTreeDiskPosition& updatedNextDiskPosition, const uint64_t updatedDiskUsage);

    /**
     * Sets whether index is (true) or is not (false) out of sync. Value is set in the database to provide
     * proper synchronization with data written in the data files. Index is not out of sync only when it
     * corresponds with the written data files.
     * Returns false if value could not be set in the database.
     */
    bool SetIndexOutOfSync(bool isIndexOutOfSyncIn);

    /**
     * Sets isIndexOutOfSyncOut with boolean value that represents if index is (true) or in not (false) out of
     * sync with written files.
     * If index is out of sync it must be recreated from current data files during initialization.
     */
    bool GetIndexOutOfSync(bool& isIndexOutOfSyncOut);
};

#endif // BITCOIN_MERKLEDB_H
