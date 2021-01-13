// Copyright (c) 2015-2016 The Bitcoin Core developers
// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "merkle.h"
#include "hash.h"
#include "utilstrencodings.h"
#include <algorithm>
#include "task_helpers.h"
#include "blockstreams.h"

/*     WARNING! If you're reading this because you're learning about crypto
       and/or designing a new system that will use merkle trees, keep in mind
       that the following merkle tree algorithm has a serious flaw related to
       duplicate txids, resulting in a vulnerability (CVE-2012-2459).

       The reason is that if the number of hashes in the list at a given time
       is odd, the last one is duplicated before computing the next level (which
       is unusual in Merkle trees). This results in certain sequences of
       transactions leading to the same merkle root. For example, these two
       trees:

                    A               A
                  /  \            /   \
                B     C         B       C
               / \    |        / \     / \
              D   E   F       D   E   F   F
             / \ / \ / \     / \ / \ / \ / \
             1 2 3 4 5 6     1 2 3 4 5 6 5 6

       for transaction lists [1,2,3,4,5,6] and [1,2,3,4,5,6,5,6] (where 5 and
       6 are repeated) result in the same root hash A (because the hash of both
       of (F) and (F,F) is C).

       The vulnerability results from being able to send a block with such a
       transaction list, with the same merkle root, and the same block hash as
       the original without duplication, resulting in failed validation. If the
       receiving node proceeds to mark that block as permanently invalid
       however, it will fail to accept further unmodified (and thus potentially
       valid) versions of the same block. We defend against this by detecting
       the case where we would hash two identical hashes at the end of the list
       together, and treating that identically to the block having an invalid
       merkle root. Assuming no double-SHA256 collisions, this will detect all
       known ways of changing the transactions without affecting the merkle
       root.
*/

/* This implements a constant-space merkle root/path calculator, limited to 2^32
 * leaves. */
static void MerkleComputation(const std::vector<uint256> &leaves,
                              uint256 *proot, bool *pmutated,
                              uint32_t branchpos,
                              std::vector<uint256> *pbranch) {
    if (pbranch) pbranch->clear();
    if (leaves.size() == 0) {
        if (pmutated) *pmutated = false;
        if (proot) *proot = uint256();
        return;
    }
    bool mutated = false;
    // count is the number of leaves processed so far.
    uint32_t count = 0;
    // inner is an array of eagerly computed subtree hashes, indexed by tree
    // level (0 being the leaves).
    // For example, when count is 25 (11001 in binary), inner[4] is the hash of
    // the first 16 leaves, inner[3] of the next 8 leaves, and inner[0] equal to
    // the last leaf. The other inner entries are undefined.
    uint256 inner[32];
    // Which position in inner is a hash that depends on the matching leaf.
    int matchlevel = -1;
    // First process all leaves into 'inner' values.
    while (count < leaves.size()) {
        uint256 h = leaves[count];
        bool matchh = count == branchpos;
        count++;
        int level;
        // For each of the lower bits in count that are 0, do 1 step. Each
        // corresponds to an inner value that existed before processing the
        // current leaf, and each needs a hash to combine it.
        for (level = 0; !(count & (((uint32_t)1) << level)); level++) {
            if (pbranch) {
                if (matchh) {
                    pbranch->push_back(inner[level]);
                } else if (matchlevel == level) {
                    pbranch->push_back(h);
                    matchh = true;
                }
            }
            mutated |= (inner[level] == h);
            CHash256()
                .Write(inner[level].begin(), 32)
                .Write(h.begin(), 32)
                .Finalize(h.begin());
        }
        // Store the resulting hash at inner position level.
        inner[level] = h;
        if (matchh) {
            matchlevel = level;
        }
    }
    // Do a final 'sweep' over the rightmost branch of the tree to process
    // odd levels, and reduce everything to a single top value.
    // Level is the level (counted from the bottom) up to which we've sweeped.
    int level = 0;
    // As long as bit number level in count is zero, skip it. It means there
    // is nothing left at this level.
    while (!(count & (((uint32_t)1) << level))) {
        level++;
    }
    uint256 h = inner[level];
    bool matchh = matchlevel == level;
    while (count != (((uint32_t)1) << level)) {
        // If we reach this point, h is an inner value that is not the top.
        // We combine it with itself (Bitcoin's special rule for odd levels in
        // the tree) to produce a higher level one.
        if (pbranch && matchh) {
            pbranch->push_back(h);
        }
        CHash256()
            .Write(h.begin(), 32)
            .Write(h.begin(), 32)
            .Finalize(h.begin());
        // Increment count to the value it would have if two entries at this
        // level had existed.
        count += (((uint32_t)1) << level);
        level++;
        // And propagate the result upwards accordingly.
        while (!(count & (((uint32_t)1) << level))) {
            if (pbranch) {
                if (matchh) {
                    pbranch->push_back(inner[level]);
                } else if (matchlevel == level) {
                    pbranch->push_back(h);
                    matchh = true;
                }
            }
            CHash256()
                .Write(inner[level].begin(), 32)
                .Write(h.begin(), 32)
                .Finalize(h.begin());
            level++;
        }
    }
    // Return result.
    if (pmutated) *pmutated = mutated;
    if (proot) *proot = h;
}

uint256 ComputeMerkleRoot(const std::vector<uint256> &leaves, bool *mutated) {
    uint256 hash;
    MerkleComputation(leaves, &hash, mutated, -1, nullptr);
    return hash;
}

std::vector<uint256> ComputeMerkleBranch(const std::vector<uint256> &leaves,
                                         uint32_t position) {
    std::vector<uint256> ret;
    MerkleComputation(leaves, nullptr, nullptr, position, &ret);
    return ret;
}

uint256 ComputeMerkleRootFromBranch(const uint256 &leaf,
                                    const std::vector<uint256> &vMerkleBranch,
                                    uint32_t nIndex) {
    uint256 hash = leaf;
    for (std::vector<uint256>::const_iterator it = vMerkleBranch.begin();
         it != vMerkleBranch.end(); ++it) {
        if ((*it).IsNull())
        {
            // Duplicated node
            hash = Hash(BEGIN(hash), END(hash), BEGIN(hash), END(hash));
        }
        else if (nIndex & 1) {
            hash = Hash(BEGIN(*it), END(*it), BEGIN(hash), END(hash));
        } else {
            hash = Hash(BEGIN(hash), END(hash), BEGIN(*it), END(*it));
        }
        nIndex >>= 1;
    }
    return hash;
}

uint256 BlockMerkleRoot(const CBlock &block, bool *mutated) {
    std::vector<uint256> leaves;
    leaves.resize(block.vtx.size());
    for (size_t s = 0; s < block.vtx.size(); s++) {
        leaves[s] = block.vtx[s]->GetId();
    }
    return ComputeMerkleRoot(leaves, mutated);
}

std::vector<uint256> BlockMerkleBranch(const CBlock &block, uint32_t position) {
    std::vector<uint256> leaves;
    leaves.resize(block.vtx.size());
    for (size_t s = 0; s < block.vtx.size(); s++) {
        leaves[s] = block.vtx[s]->GetId();
    }
    return ComputeMerkleBranch(leaves, position);
}

CMerkleTree::CMerkleTree(const std::vector<CTransactionRef>& transactions, const uint256& blockHashIn, int32_t blockHeightIn, CThreadPool<CQueueAdaptor>* pThreadPool)
    : numberOfLeaves(transactions.size()), blockHash(blockHashIn), blockHeight(blockHeightIn)
{
    if (transactions.empty())
    {
        return;
    }

    CalculateMerkleTree<CTransactionRef>(transactions, pThreadPool);
}

CMerkleTree::CMerkleTree(CBlockStreamReader<CFileReader>& stream, const uint256& blockHashIn, int32_t blockHeightIn, CThreadPool<CQueueAdaptor>* pThreadPool)
    : blockHash(blockHashIn), blockHeight(blockHeightIn)
{
    size_t numberOfRemainingTransactions = stream.GetRemainingTransactionsCount();
    if (!numberOfRemainingTransactions)
    {
        return;
    }

    std::vector<uint256> transactionIds;
    transactionIds.reserve(numberOfRemainingTransactions);

    do
    {
        transactionIds.push_back(stream.ReadTransaction().GetId());
    }
    while (!stream.EndOfStream());

    numberOfLeaves = transactionIds.size();
    CalculateMerkleTree<uint256>(transactionIds, pThreadPool);
}

template <typename elementType>
std::future<CMerkleTree> CMerkleTree::CreateBatchTask(typename std::vector<elementType>::const_iterator batchBeginIter,
    typename std::vector<elementType>::const_iterator batchEndIter, 
    CThreadPool<CQueueAdaptor>& threadPool) const
{
    auto calculateSubTree = [batchBeginIter, batchEndIter]()
    {
        CMerkleTree subTree(batchEndIter - batchBeginIter);
        for (auto it = batchBeginIter; it != batchEndIter; ++it)
        {
            subTree.AddTransactionId(*it);
        }
        return subTree;
    };
    return (make_task(threadPool, calculateSubTree));
}

template <typename elementType>
void CMerkleTree::CalculateMerkleTree(const std::vector<elementType>& vTransactions, CThreadPool<CQueueAdaptor>* pThreadPool)
{
    // Number of threads depends on the given thread pool, otherwise the whole calculation will be done in the current thread.
    const size_t numberOfThreads = pThreadPool != nullptr ? pThreadPool->getPoolSize() : 1;

    /* Number of all transactions is split into batches. These are used to calculate
       Merkle subtrees in parallel which are then merged together into a complete
       Merkle tree. One batch defines number of transactions/leaves in a subtree. This
       number must be a power of two number to make merge possible. Starting with batch
       size 2^12 means all Merkle Trees with <= 4096 transactions/leaves will be
       calculated in a single thread.
    */
    //std::distance can return a negative value, int prevents signed/unsigned mismatch
    const int intBatchSize = [&]
    {
        size_t batchSize(0x1000);
        while (batchSize * numberOfThreads < numberOfLeaves)
        {
            batchSize <<= 1;
        }
        return (std::min(batchSize, numberOfLeaves));
    } ();

    /* Split transactions/leaves into batches/tasks.
     * Start with the second batch because first batch will be calculated in the current thread.
     */
    auto batchBeginIter = vTransactions.cbegin();
    std::advance(batchBeginIter, intBatchSize);
    auto batchEndIter = batchBeginIter;
    std::vector<std::future<CMerkleTree>> futures;
    //Distance can return a negative value, prevent signed/unsigned mismatch
    //Note that the following null check is redundant, but it is clearer like this
    if (pThreadPool) {
        while (std::distance(batchBeginIter, vTransactions.cend()) > intBatchSize) {
            std::advance(batchEndIter, intBatchSize);
            futures.push_back(CreateBatchTask<elementType>(batchBeginIter, batchEndIter, *pThreadPool));
            batchBeginIter = batchEndIter;
        }
        // The last batch
        if (std::distance(batchBeginIter, vTransactions.cend()) > 0) {
            futures.push_back(CreateBatchTask<elementType>(batchBeginIter, vTransactions.cend(), *pThreadPool));
        }
    }
    //In the meantime, calculate subtree of the first batch
    batchBeginIter = vTransactions.cbegin();
    batchEndIter = batchBeginIter;
    std::advance(batchEndIter, intBatchSize);
    for (auto it = batchBeginIter; it != batchEndIter; ++it)
    {
        AddTransactionId(*it);
    }

    // Tasks must be ordered to make sure Merkle Tree is merged properly with other subtrees
    for (auto &f : futures)
    {
        if (!MergeSubTree(f.get()))
        {
            throw std::runtime_error("Unexpected error during merkle tree calculation: cannot merge with higher subtree.");
        }
    }
}

void CMerkleTree::AddTransactionId(const CTransactionRef& transactionRef)
{
    AddNodeAtLevel(transactionRef->GetId(), 0);
}

void CMerkleTree::AddTransactionId(const uint256& transactionId)
{
    AddNodeAtLevel(transactionId, 0);
}

void CMerkleTree::AddNodeAtLevel(const uint256& hash, size_t level)
{
    uint256 currentNode = hash;
    for (size_t currentLevel = level; currentLevel < merkleTreeLevelsWithNodeHashes.size(); ++currentLevel)
    {
        if (merkleTreeLevelsWithNodeHashes[currentLevel].size() & 1)
        {
            /* We are adding new node at level that has odd numbers of nodes, meaning we can
               make a new pair (siblings) and calculate their parent.
             */
            uint256 leftNode = merkleTreeLevelsWithNodeHashes[currentLevel].back();
            uint256 rightNode = currentNode;

            CHash256()
                .Write(leftNode.begin(), 32)
                .Write(rightNode.begin(), 32)
                .Finalize(currentNode.begin());
           
            merkleTreeLevelsWithNodeHashes[currentLevel].push_back(rightNode);
        }
        else
        {
            /* Because this level has even number of nodes, new node is just added.
             */
            merkleTreeLevelsWithNodeHashes[currentLevel].push_back(currentNode);
            return;
        }
    }

    //Store the first node on current top level
    merkleTreeLevelsWithNodeHashes.emplace_back();
    //Reserve for allocation if number of leaves is known
    if (numberOfLeaves > 0)
    {
        //Number of nodes for each level can be calculated
        merkleTreeLevelsWithNodeHashes.back().reserve(numberOfLeaves >> (merkleTreeLevelsWithNodeHashes.size() - 1));
    }
    merkleTreeLevelsWithNodeHashes.back().push_back(currentNode);
}

/*
    Parallel computation is based on splitting Merkle Tree into smaller subtrees and
    then merging them together.

    Subtree 1  Subtree 2
      0123        4567     Level 2
      / \         / \
     01  23      45  67    Level 1
    / \ / \     / \ / \
    0 1 2 3     4 5 6 7    Level 0

    Merge is done by appending subtree nodes at each level. Merge is always done to the
    right side. If sibling are found at the last level, their parent is calculated making
    a new node on the upper level. In the example above Level 2 has two nodes 0123 and
    4567 after the merge. They become siblings and their parent is calculated and stored
    to Level 3.
*/
bool CMerkleTree::MergeSubTree(const CMerkleTree& subTree)
{
    size_t currentTreeHeight = merkleTreeLevelsWithNodeHashes.size();
    size_t subTreeHeight = subTree.merkleTreeLevelsWithNodeHashes.size();

    //Merge only if current height is same or greater than subtree we want to merge with
    if (currentTreeHeight >= subTreeHeight)
    {
        // Add subtree's root node. This will also calculate nodes in upper levels if needed.
        size_t currentLevel = subTreeHeight - 1;
        AddNodeAtLevel(subTree.merkleTreeLevelsWithNodeHashes[currentLevel].back(), currentLevel);

        // All other levels are concatenated
        while (currentLevel)
        {
            --currentLevel;
            merkleTreeLevelsWithNodeHashes[currentLevel].insert(merkleTreeLevelsWithNodeHashes[currentLevel].cend(),
                std::make_move_iterator(subTree.merkleTreeLevelsWithNodeHashes[currentLevel].cbegin()),
                std::make_move_iterator(subTree.merkleTreeLevelsWithNodeHashes[currentLevel].cend()));
        }
    }
    else
    {
        // Unexpected, this should not happen
        return false;
    }
    return true;
}

void CMerkleTree::CalculateMissingParentNode(const size_t currentLevel, uint256& additionalNodeInOut) const
{
    if (!additionalNodeInOut.IsNull())
    {
        // Duplicate node
        uint256 leftNode = additionalNodeInOut;
        uint256 rightNode = additionalNodeInOut;
        if (merkleTreeLevelsWithNodeHashes[currentLevel].size() & 1)
        {
            // With additionalNode and level with odd numbers of nodes, we can calculate with normal left and right siblings
            leftNode = merkleTreeLevelsWithNodeHashes[currentLevel].back();
        }
        CHash256()
            .Write(leftNode.begin(), 32)
            .Write(rightNode.begin(), 32)
            .Finalize(additionalNodeInOut.begin());
    }
    else if (merkleTreeLevelsWithNodeHashes[currentLevel].size() > 1 &&
             merkleTreeLevelsWithNodeHashes[currentLevel].size() & 1)
    {
        // Without additionalNode missing parentNode is calculated only on levels with odd numbers of nodes by duplication of the last node
        CHash256()
            .Write(merkleTreeLevelsWithNodeHashes[currentLevel].back().begin(), 32)
            .Write(merkleTreeLevelsWithNodeHashes[currentLevel].back().begin(), 32)
            .Finalize(additionalNodeInOut.begin());
    }
}

uint256 CMerkleTree::GetMerkleRoot() const
{
    if (merkleTreeLevelsWithNodeHashes.empty())
    {
        return {};
    }

    uint256 missingParentNode;
    for (size_t currentLevel = 0; currentLevel < merkleTreeLevelsWithNodeHashes.size(); ++currentLevel)
    {
        // We need to go through all levels and calculate missing nodes if any
        CalculateMissingParentNode(currentLevel, missingParentNode);
    }

    if (!missingParentNode.IsNull())
    {
        return missingParentNode;
    }
    return merkleTreeLevelsWithNodeHashes.back().back();
}

CMerkleTree::MerkleProof CMerkleTree::GetMerkleProof(const TxId& transactionId, bool skipDuplicates) const
{
    if (merkleTreeLevelsWithNodeHashes.empty())
    {
        return MerkleProof(0);
    }

    // Find transaction index
    auto foundTxId = std::find(merkleTreeLevelsWithNodeHashes[0].cbegin(), merkleTreeLevelsWithNodeHashes[0].cend(), transactionId);
    size_t currentIndex = std::distance(merkleTreeLevelsWithNodeHashes[0].cbegin(), foundTxId);
    if (currentIndex == merkleTreeLevelsWithNodeHashes[0].size())
    {
        // Transaction id not found in this Merkle Tree
        return MerkleProof(0);
    }
    MerkleProof merkleProof(currentIndex);
    uint256 missingParentNode;
    for (size_t currentLevel = 0; currentLevel < merkleTreeLevelsWithNodeHashes.size(); ++currentLevel)
    {
        //Calculate index of a sibling (either left or right)
        size_t siblingIndex = (currentIndex & 1) ? (currentIndex - 1) : (currentIndex + 1);

        if (siblingIndex < merkleTreeLevelsWithNodeHashes[currentLevel].size())
        {
            //Add a sibling as part of the proof
            merkleProof.merkleTreeHashes.push_back(merkleTreeLevelsWithNodeHashes[currentLevel][siblingIndex]);
        }
        else if (!missingParentNode.IsNull())
        {
            //Add missing node
            if (skipDuplicates && merkleProof.merkleTreeHashes.back().IsNull())
            {
                // In getmerkleproof RPC "empty" uint256 is represented as "*" to avoid duplicating values in the output
                merkleProof.merkleTreeHashes.emplace_back();
            }
            else
            {
                merkleProof.merkleTreeHashes.push_back(missingParentNode);
            }
        }
        else if (siblingIndex > 1)
        {
            //Add last node (duplicate) on level with odd numbers of nodes
            if (skipDuplicates)
            {
                // Add "empty" uint256 to represent it as "*" in getmerkleproof RPC output
                merkleProof.merkleTreeHashes.emplace_back();
            }
            else
            {
                merkleProof.merkleTreeHashes.push_back(merkleTreeLevelsWithNodeHashes[currentLevel].back());
            }
        }
        else
        {
            //We reached root
            break;
        }

        //Calculate missing parent node
        CalculateMissingParentNode(currentLevel, missingParentNode);

        //Move to the parent
        currentIndex >>= 1;
    }
    return merkleProof;
}

uint64_t CMerkleTree::GetSizeInBytes() const
{
    uint64_t numberOfNodes = 0;
    for (const auto& curentLevel : merkleTreeLevelsWithNodeHashes)
    {
        numberOfNodes += curentLevel.size();
    }
    return numberOfNodes * sizeof(uint256);
}
