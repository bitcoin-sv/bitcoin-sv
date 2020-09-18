// Copyright (c) 2015 The Bitcoin Core developers
// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#ifndef BITCOIN_MERKLE
#define BITCOIN_MERKLE

#include <cstdint>
#include <vector>

#include "primitives/block.h"
#include "uint256.h"
#include <future>

uint256 ComputeMerkleRoot(const std::vector<uint256> &leaves,
                          bool *mutated = nullptr);
std::vector<uint256> ComputeMerkleBranch(const std::vector<uint256> &leaves,
                                         uint32_t position);
uint256 ComputeMerkleRootFromBranch(const uint256 &leaf,
                                    const std::vector<uint256> &branch,
                                    uint32_t position);

/**
 * Compute the Merkle root of the transactions in a block.
 * *mutated is set to true if a duplicated subtree was found.
 */
uint256 BlockMerkleRoot(const CBlock &block, bool *mutated = nullptr);

/**
 * Compute the Merkle branch for the tree of transactions in a block, for a
 * given position. This can be verified using ComputeMerkleRootFromBranch.
 */
std::vector<uint256> BlockMerkleBranch(const CBlock &block, uint32_t position);

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

 /** The default preferred size of a Merkle Tree datafile (mrk????????.dat) */
static constexpr uint64_t DEFAULT_PREFERRED_MERKLETREE_FILE_SIZE{32 * ONE_MEBIBYTE}; // 32 MiB

/**
 * The user should allocate at least 176 MiB for Merkle tree data files (mrk????????.dat)
 * With average 0.5 MiB (8192 transactions) per block/tree, 288 blocks = 144 MiB.
 * Pruning process will by default remove one of 32 MiB file (DEFAULT_PREFERRED_MERKLETREE_FILE_SIZE)
 * We need at least 176 MiB of free space for Merkle Tree files.
 */
static constexpr uint64_t MIN_DISK_SPACE_FOR_MERKLETREE_FILES{288 / 2 * ONE_MEBIBYTE + DEFAULT_PREFERRED_MERKLETREE_FILE_SIZE};

class CMerkleTree
{
private:
    size_t numberOfLeaves{ 0 };
    std::vector<std::vector<uint256>> merkleTreeLevelsWithNodeHashes;

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
    bool MergeSubTree(const CMerkleTree& subTree);

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
     * Optionally use thread pool pThreadPool for parallel calculation.
     */
    CMerkleTree(const std::vector<CTransactionRef>& transactions, CThreadPool<CQueueAdaptor>* pThreadPool = nullptr);

    // Default move constructor and move assignment operator
    CMerkleTree(CMerkleTree &&) = default;
    CMerkleTree & operator=(CMerkleTree &&) = default;

    /**
     * Constructor used to create the Merkle Tree from given file stream.
     * Optionally use thread pool pThreadPool for parallel calculation.
     */
    CMerkleTree(CBlockStreamReader<CFileReader>& stream, CThreadPool<CQueueAdaptor>* pThreadPool = nullptr);

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

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(merkleTreeLevelsWithNodeHashes);
    }
};

#endif
