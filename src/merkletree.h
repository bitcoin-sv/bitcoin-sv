// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#ifndef BITCOIN_MERKLETREE_H
#define BITCOIN_MERKLETREE_H

#include "consensus/consensus.h"
#include "primitives/transaction.h"
#include <future>

/*
    Class MerkleTree allows incremental construction and parallel calculation of a
    Merkle Tree from the list of transaction ids. Compared to ComputeMerkleRoot,
    ComputeMerkleBranch or CPartialMerkleTree, this Merkle Tree can be stored (on disk
    and/or memory) and used later to extract either Merkle root or proof of any
    transaction in this tree without the need of calculating the Merkle Tree again.
    Bellow is an example of a Merkle Tree presentation that is stored in this class.

       01234567            Level 3
        /     \
     0123     4567         Level 2
      / \     / \
    01  23  45  67  89     Level 1
    / \ / \ / \ / \ / \
    0 1 2 3 4 5 6 7 8 9    Level 0

    CMerkleTree keeps a list of levels and each level is a list of hash values. Level 0
    stores the leaves, which are the transaction id's. These nodes are called leaves.
    When first leaf (node 0 on example above) is added it is stored to Level 0. Adding
    leaves incrementally builds a Merkle Tree. When leaf 1 is added, 01 node is calculated
    and added to Level 1. Leaves 0 and 1 become siblings and node 01 is their parent.
    When leaf 2 comes in, it is just added to Level 0. With leaf 3, same as before,
    node 23 is calculated and added to Level 1. Because Level 1 has two nodes now, they
    become siblings and their parent 0123 is calculated. As long as other leaves are
    being added incrementally the same process continues. On the example above it can
    be seen that Merkle Tree is not completed due to odd number of nodes at Level 1.
    To make it complete, node 89 has to be duplicated to calculate its parent on Level
    2 and again to calculate parent at Level 3. Two nodes on Level 3 can then be used
    to calculate Merkle root.
 */

 /** Forward declarations of needed types */
class CQueueAdaptor;
template<typename QueueAdapter>
class CThreadPool;
class CFileReader;
template<typename Reader>
class CBlockStreamReader;

/** Some estmates:
 * 4000B:
 *     Average transaction size in bytes in a big block > 4GB
 *     i.e. on average we expect a 4GB block to have one million transactions
 * 5%:
 *     Average block size in percent of maximum block size
 *
 * 32:
 *     Exact size of transaction id
 *
 * 2 * 32 * nbtransactions:
 *     Size of a merkle tree in bytes (64MB for a 4GB block containing one million txns)
 */

uint64_t constexpr CalculatePreferredMerkleTreeSize (uint64_t maxBlockSize)
{
    constexpr uint64_t avgTxnSize = 4'000;
    return (maxBlockSize / avgTxnSize)
                    * sizeof(uint256) // size of txid, i.e. 32 byte
                    * 2;
}

uint64_t constexpr CalculateMinDiskSpaceForMerkleFiles (uint64_t maxBlockSize)
{
    return 288 * CalculatePreferredMerkleTreeSize(maxBlockSize)
           / 20; // assuming average block size to be 5% of maximum block size
}

/** The default preferred size of a Merkle Tree datafile (mrk????????.dat) */
static constexpr uint64_t DEFAULT_PREFERRED_MERKLETREE_FILE_SIZE{ 32 * ONE_MEBIBYTE }; // 32 MiB

/**
 * The user should allocate at least 176 MiB for Merkle tree data files (mrk????????.dat)
 * With average 0.5 MiB (8192 transactions) per block/tree, 288 blocks = 144 MiB.
 * Pruning process will by default remove one of 32 MiB file (DEFAULT_PREFERRED_MERKLETREE_FILE_SIZE)
 * We need at least 176 MiB of free space for Merkle Tree files.
 */
static constexpr uint64_t MIN_DISK_SPACE_FOR_MERKLETREE_FILES{ 288 / 2 * ONE_MEBIBYTE + DEFAULT_PREFERRED_MERKLETREE_FILE_SIZE };

/** The default maximum size of a Merkle Tree memory cache */
static constexpr uint64_t DEFAULT_MAX_MERKLETREE_MEMORY_CACHE_SIZE{ 32 * ONE_MEBIBYTE }; // 32 MiB

class CMerkleTree
{
private:
    size_t numberOfLeaves{ 0 };
    std::vector<std::vector<uint256>> merkleTreeLevelsWithNodeHashes;
    /**
     * Hash of a block from which this Merkle Tree was stored. Used in (de)serialization when merkle tree is written to or read from a data file.
     */
    uint256 blockHash{ uint256() };
    /**
     * Height of a block from which this Merkle Tree was stored. Used in (de)serialization when merkle tree is written to or read from a data file.
     */
    int32_t blockHeight{ 0 };

    /* Deleted copy constructor and assignment operator.
     * We want to avoid copy and assignment for performance reasons.
     */
    CMerkleTree(const CMerkleTree&) = delete;
    CMerkleTree& operator=(const CMerkleTree&) = delete;

    /**
     * Creates and starts a new task on separate thread. Used to calculate
     * a Merkle subtree from given begin and end vector iterators. Vector can only
     * hold elements of type from which transaction id can be extracted using
     * AddTransactionId function. Existing thread pool and vector of futures must
     * be provided to properly handle tasks (order of subtree calculations is important)
     */
    template <typename elementType>
    std::future<CMerkleTree> CreateBatchTask(typename std::vector<elementType>::const_iterator batchBeginIter,
        typename std::vector<elementType>::const_iterator batchEndIter,
        CThreadPool<CQueueAdaptor>& threadPool) const;

    /**
     * Calculates Merkle Tree from a given list of transactions vTransactions.
     * Vector elements must be of type from which transaction id can be extracted
     * with AddTransactionId function. Smaller Merkle subtrees can be calculated
     * simultaneously on different threads and merged together into a final Merkle
     * Tree if thread pool pThreadPool is used
     */
    template <typename elementType>
    void CalculateMerkleTree(const std::vector<elementType>& vTransactions, CThreadPool<CQueueAdaptor>* pThreadPool = nullptr);

    /**
     * Adds a transaction id into a Merkle Tree as its new leaf.
     * Function is used to incrementally construct a Merkle Tree. This is useful when
     * we don't yet have a complete list of transactions in a block or if we want to
     * split calculation of Merkle Tree into smaller subtrees which can be processed
     * in parallel manner. Function calls AddNodeAtLevel for level 0.
     */
    void AddTransactionId(const CTransactionRef& transactionRef);
    void AddTransactionId(const uint256& transactionId);

    /**
     * Adds node at specific level into the Merkle Tree.
     * Used by AddNode and MergeSubTree functions.
     * When node is added to a specific level and there is an odd number of nodes
     * at that level, nodes become siblings and their parent is calculated.
     * Parent is then added to upper level and the process is repeated until we
     * reach a level where no sibling is left.
     */
    void AddNodeAtLevel(const uint256& hash, size_t level);

    /**
     * Merge Merkle Tree with another subtree.
     * Height of a subtree must not be higher than the current one. At each level
     * it simply appends nodes from a subtree. At the last highest subtree level
     * it executes AddNodeAtLevel that checks if nodes from current tree and
     * subtree can be used to calculate a parent node and upper levels if needed.
     * Returns false if subtree is higher.
     */
    bool MergeSubTree(CMerkleTree&& subTree);

    /**
     * Trees that do not have exactly 2^N leaves/transactions are incomplete.
     * This is a helper function for GetMerkleRoot and GetMerkleProof and it
     * calculates a missing parent for the next level (currentLevel + 1) once
     * we know all nodes in the currentLevel.
     * additionalNodeInOut is used to add additional node to currentLevel and
     * to return the missing parent node on the next level.
     */
    void CalculateMissingParentNode(const size_t currentLevel, uint256& additionalNodeInOut) const;

public:

    /* Structure MerkleProof contains a list of merkle tree hashes, one for each tree level and
     * a transaction index of the transaction we want to prove.
     * The structure is returned by the function GetMerkleProof and it is used to calculate the
     * merkle root.
     */
    struct MerkleProof
    {
        MerkleProof(size_t index) : transactionIndex(index) {};

        std::vector<uint256> merkleTreeHashes;
        const size_t transactionIndex;
    };

    CMerkleTree() {};

    /**
     * When number of transactions is known, numberOfTransactions parameter will be
     * used to pre-allocate memory needed to store the Merkle Tree when transactions
     * are added incrementally. For example in parallel calculation.
     *
     */
    CMerkleTree(const size_t numberOfTransactions) : numberOfLeaves(numberOfTransactions) {};

    /**
     * Constructor used to calculate the Merkle Tree from given transaction references.
     * When Merkle Tree is written and stored to disk, blockHashIn and blockHeightIn must be
     * set to hash and height of a block from which this Merkle Tree was stored respectively.
     * This is needed when rebuilding the index from data files.
     * Optionally use thread pool pThreadPool for parallel calculation.
     */
    CMerkleTree(const std::vector<CTransactionRef>& transactions, const uint256& blockHashIn, int32_t blockHeightIn, CThreadPool<CQueueAdaptor>* pThreadPool = nullptr);

    // Default move constructor and move assignment operator
    CMerkleTree(CMerkleTree &&) = default;
    CMerkleTree & operator=(CMerkleTree &&) = default;

    /**
     * Constructor used to create the Merkle Tree from given file stream.
     * When Merkle Tree is written and stored to disk, blockHashIn and blockHeightIn must be
     * set to hash and height of a block from which this Merkle Tree was stored respectively.
     * This is needed when rebuilding the index from data files.
     * Optionally use thread pool pThreadPool for parallel calculation.
     */
    CMerkleTree(CBlockStreamReader<CFileReader>& stream, const uint256& blockHashIn, int32_t blockHeightIn, CThreadPool<CQueueAdaptor>* pThreadPool = nullptr);

    /**
     * Returns Merkle root of this tree. If tree has no nodes it returns an empty hash.
     */
    uint256 GetMerkleRoot() const;

    /**
     * Computes and returns the Merkle proof for a given transactionId.
     * If skipDuplicates is set to true, uint256() (zero) is stored in the proof for duplicated nodes.
     * This is used in getmerkleproof RPC where we want to mark a duplicate as "*" instead of the actual hash value.
     * The returned Merkle proof contains a list of merkle tree hashes and a transaction's index in the tree/block.
     * For example, transaction at index 0 is a coinbase transaction.
     */
    MerkleProof GetMerkleProof(const TxId& transactionId, bool skipDuplicates) const;

    /**
     * Same as the GetMerkleProof(const TxId&, bool) function, except that the transaction is specified by its index in this tree/block.
     */
    MerkleProof GetMerkleProof(size_t transactionIndex, bool skipDuplicates) const;

    /*
     * Returns size of Merkle Tree in bytes by calculating number of all hashes stored
     * multiplied by 32 bytes (uint256).
     */
    uint64_t GetSizeInBytes() const;

    uint256 GetBlockHash() const { return blockHash; };
    int32_t GetBlockHeight() const { return blockHeight; };

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(blockHash);
        READWRITE(blockHeight);
        READWRITE(merkleTreeLevelsWithNodeHashes);
    }
};

#endif // MERKLETREE_H
