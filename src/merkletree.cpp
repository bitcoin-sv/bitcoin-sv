// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "merkletree.h"
#include "task_helpers.h"
#include "blockstreams.h"

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
    if (pThreadPool)
    {
        while (std::distance(batchBeginIter, vTransactions.cend()) > intBatchSize)
        {
            std::advance(batchEndIter, intBatchSize);
            futures.push_back(CreateBatchTask<elementType>(batchBeginIter, batchEndIter, *pThreadPool));
            batchBeginIter = batchEndIter;
        }
        // The last batch
        if (std::distance(batchBeginIter, vTransactions.cend()) > 0)
        {
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
            throw std::runtime_error("Unexpected error during merkle tree calculation.");
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
bool CMerkleTree::MergeSubTree(CMerkleTree&& subTree)
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
                std::make_move_iterator(subTree.merkleTreeLevelsWithNodeHashes[currentLevel].begin()),
                std::make_move_iterator(subTree.merkleTreeLevelsWithNodeHashes[currentLevel].end()));
        }
    }
    else
    {
        // Unexpected, this should not happen
        LogPrintf("CMerkleTree::MergeSubTree: Error calculating Merkle Tree. Cannot merge with higher subtree.\n");
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

    return GetMerkleProof(currentIndex, skipDuplicates);
}

CMerkleTree::MerkleProof CMerkleTree::GetMerkleProof(size_t transactionIndex, bool skipDuplicates) const
{
    size_t currentIndex = transactionIndex;
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
