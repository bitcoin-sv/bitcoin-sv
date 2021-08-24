// Copyright (c) 2015-2016 The Bitcoin Core developers
// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "consensus/merkle.h"
#include "test/test_bitcoin.h"
#include "task_helpers.h"
#include "merkleproof.h"
#include "merkletree.h"

#include <boost/test/unit_test.hpp>

#include <tuple>

BOOST_FIXTURE_TEST_SUITE(merkle_tests, TestingSetup)

// Older version of the merkle root computation code, for comparison.
static uint256 BlockBuildMerkleTree(const CBlock &block, bool *fMutated,
                                    std::vector<uint256> &vMerkleTree) {
    vMerkleTree.clear();
    // Safe upper bound for the number of total nodes.
    vMerkleTree.reserve(block.vtx.size() * 2 + 16);
    for (std::vector<CTransactionRef>::const_iterator it(block.vtx.begin());
         it != block.vtx.end(); ++it)
        vMerkleTree.push_back((*it)->GetId());
    int j = 0;
    bool mutated = false;
    for (int nSize = block.vtx.size(); nSize > 1; nSize = (nSize + 1) / 2) {
        for (int i = 0; i < nSize; i += 2) {
            int i2 = std::min(i + 1, nSize - 1);
            if (i2 == i + 1 && i2 + 1 == nSize &&
                vMerkleTree[j + i] == vMerkleTree[j + i2]) {
                // Two identical hashes at the end of the list at a particular
                // level.
                mutated = true;
            }
            vMerkleTree.push_back(
                Hash(vMerkleTree[j + i].begin(), vMerkleTree[j + i].end(),
                     vMerkleTree[j + i2].begin(), vMerkleTree[j + i2].end()));
        }
        j += nSize;
    }
    if (fMutated) {
        *fMutated = mutated;
    }
    return (vMerkleTree.empty() ? uint256() : vMerkleTree.back());
}

// Older version of the merkle branch computation code, for comparison.
static std::vector<uint256>
BlockGetMerkleBranch(const CBlock &block,
                     const std::vector<uint256> &vMerkleTree, int nIndex) {
    std::vector<uint256> vMerkleBranch;
    int j = 0;
    for (int nSize = block.vtx.size(); nSize > 1; nSize = (nSize + 1) / 2) {
        int i = std::min(nIndex ^ 1, nSize - 1);
        vMerkleBranch.push_back(vMerkleTree[j + i]);
        nIndex >>= 1;
        j += nSize;
    }
    return vMerkleBranch;
}

static inline size_t ctz(uint32_t i) {
    if (i == 0) return 0;
    size_t j = 0;
    while (!(i & 1)) {
        j++;
        i >>= 1;
    }
    return j;
}

BOOST_AUTO_TEST_CASE(merkle_test) {
    for (size_t i = 0; i < 32; i++) {
        // Try 32 block sizes: all sizes from 0 to 16 inclusive, and then 15
        // random sizes.
        size_t ntx = (i <= 16) ? i : 17 + (InsecureRandRange(4000));
        // Try up to 3 mutations.
        for (size_t mutate = 0; mutate <= 3; mutate++) {
            // The last how many transactions to duplicate first.
            size_t duplicate1 = mutate >= 1 ? 1 << ctz(ntx) : 0;
            if (duplicate1 >= ntx) {
                // Duplication of the entire tree results in a different root
                // (it adds a level).
                break;
            }

            // The resulting number of transactions after the first duplication.
            size_t ntx1 = ntx + duplicate1;
            // Likewise for the second mutation.
            size_t duplicate2 = mutate >= 2 ? 1 << ctz(ntx1) : 0;
            if (duplicate2 >= ntx1)
                break;
            size_t ntx2 = ntx1 + duplicate2;
            // And for the third mutation.
            size_t duplicate3 = mutate >= 3 ? 1 << ctz(ntx2) : 0;
            if (duplicate3 >= ntx2)
                break;
            size_t ntx3 = ntx2 + duplicate3;
            // Build a block with ntx different transactions.
            CBlock block;
            block.vtx.resize(ntx);
            for (size_t j = 0; j < ntx; j++) {
                CMutableTransaction mtx;
                mtx.nLockTime = j;
                block.vtx[j] = MakeTransactionRef(std::move(mtx));
            }
            // Compute the root of the block before mutating it.
            bool unmutatedMutated = false;
            uint256 unmutatedRoot = BlockMerkleRoot(block, &unmutatedMutated);
            BOOST_CHECK(unmutatedMutated == false);
            uint256 newestUnmutatedRoot;
            {
                CMerkleTree newestMerkleTree(block.vtx, uint256(), 0);
                newestUnmutatedRoot = newestMerkleTree.GetMerkleRoot();
            }
            // Optionally mutate by duplicating the last transactions, resulting
            // in the same merkle root.
            block.vtx.resize(ntx3);
            for (size_t j = 0; j < duplicate1; j++) {
                block.vtx[ntx + j] = block.vtx[ntx + j - duplicate1];
            }
            for (size_t j = 0; j < duplicate2; j++) {
                block.vtx[ntx1 + j] = block.vtx[ntx1 + j - duplicate2];
            }
            for (size_t j = 0; j < duplicate3; j++) {
                block.vtx[ntx2 + j] = block.vtx[ntx2 + j - duplicate3];
            }
            // Compute the merkle root and merkle tree using the old mechanism.
            bool oldMutated = false;
            std::vector<uint256> merkleTree;
            uint256 oldRoot =
                BlockBuildMerkleTree(block, &oldMutated, merkleTree);
            // Compute the merkle root using the new mechanism.
            bool newMutated = false;
            uint256 newRoot = BlockMerkleRoot(block, &newMutated);
            uint256 newestRoot;
            {
                CMerkleTree newestMerkleTree(block.vtx, uint256(), 0);
                newestRoot = newestMerkleTree.GetMerkleRoot();
            }
            BOOST_CHECK(oldRoot == newRoot);
            BOOST_CHECK(newRoot == unmutatedRoot);
            BOOST_CHECK((newRoot == uint256()) == (ntx == 0));
            BOOST_CHECK(oldMutated == newMutated);
            BOOST_CHECK(newMutated == !!mutate);
            BOOST_CHECK(newestUnmutatedRoot == oldRoot);
            BOOST_CHECK(newestRoot == oldRoot);
            // If no mutation was done (once for every ntx value), try up to 16
            // branches.
            if (mutate == 0) {
                for (size_t loop = 0; loop < std::min(ntx, size_t{16}); loop++) {
                    // If ntx <= 16, try all branches. Otherwise, try 16 random
                    // ones.
                    size_t mtx = loop;
                    if (ntx > 16) {
                        mtx = InsecureRandRange(ntx);
                    }
                    std::vector<uint256> newBranch =
                        BlockMerkleBranch(block, mtx);
                    std::vector<uint256> oldBranch =
                        BlockGetMerkleBranch(block, merkleTree, mtx);

                    CMerkleTree newestMerkleTree(block.vtx, uint256(), 0);
                    CMerkleTree::MerkleProof newestBranch = newestMerkleTree.GetMerkleProof(block.vtx[mtx]->GetId(), false);
                    BOOST_CHECK(newestBranch.transactionIndex == mtx);

                    BOOST_CHECK(oldBranch == newBranch);
                    BOOST_CHECK(oldBranch == newestBranch.merkleTreeHashes);
                    BOOST_CHECK(
                        ComputeMerkleRootFromBranch(block.vtx[mtx]->GetId(),
                                                    newBranch, mtx) == oldRoot);
                    BOOST_CHECK(
                        ComputeMerkleRootFromBranch(block.vtx[mtx]->GetId(), 
                                                    newestBranch.merkleTreeHashes, mtx) == oldRoot);
                }
            }
        }
    }
}

BOOST_AUTO_TEST_CASE(merkle_tree_test)
{
    /* Test blocks with different number of transactions
       Minimum CMerkleTree batch size is set to 4096 transaction ids. That means any merkle tree
       with more than 4096 transaction ids (leaves) will be split into subtrees, each calculated
       in parallel and merged together.
       In this test we use up to 9192 leaves, causing three subtrees to be calculated and merged with
       these 1000 combinations of leaves:
       4096 + 4096 + 1
       4096 + 4096 + 2
       4096 + 4096 + 3
       4096 + 4096 + 4
       ...
       4096 + 4096 + 1000
     */
    int minBatchSize = 4096;
    // Initialize thread pool using 3 threads
    std::unique_ptr<CThreadPool<CQueueAdaptor>> pMerkleTreeThreadPool = std::make_unique<CThreadPool<CQueueAdaptor>>("MerkleTreeThreadPoolTest", 3);
    for (int numberOfTransactions = 2*minBatchSize + 1; numberOfTransactions <= (2*minBatchSize + 1000); ++numberOfTransactions)
    {
        CBlock block;
        block.vtx.resize(numberOfTransactions);
        for (int txIndex = 0; txIndex < numberOfTransactions; ++txIndex)
        {
            CMutableTransaction mtx;
            mtx.nLockTime = txIndex;
            block.vtx[txIndex] = MakeTransactionRef(std::move(mtx));
        }
        // Constructor will create merkle tree by splitting it into two subtrees, parallel calculation and merging them
        CMerkleTree merkleTree(block.vtx, uint256(), 0, pMerkleTreeThreadPool.get());
        uint256 originalMerkleRoot = BlockMerkleRoot(block);
        uint256 newMerkleRoot = merkleTree.GetMerkleRoot();
        // root from CMerkleTree instance must be same as legacy merkle root
        BOOST_CHECK(originalMerkleRoot == newMerkleRoot);
    }
}

BOOST_AUTO_TEST_CASE(merkle_proof)
{
    // Build a block
    constexpr unsigned NumTx {100};
    CBlock block {};
    block.vtx.resize(NumTx);
    for(size_t j = 0; j < NumTx; j++)
    {
        CMutableTransaction mtx {};
        mtx.nLockTime = j;
        block.vtx[j] = MakeTransactionRef(std::move(mtx));
    }

    // lambda to create CMerkleTree version of proof and return a nodes list
    // suitable for the TSC version, the merkle root from the proof and the entire proof
    auto CMerkleTreeLambda = [&block](const CTransactionRef& txn)
    {
        // Create and check CMerkleTree version
        CMerkleTree merkleTree { block.vtx, uint256(), 0 };
        CMerkleTree::MerkleProof treeProof { merkleTree.GetMerkleProof(txn->GetId(), false) };
        uint256 checkRoot { ComputeMerkleRootFromBranch(txn->GetId(), treeProof.merkleTreeHashes, treeProof.transactionIndex) };
        BOOST_CHECK_EQUAL(merkleTree.GetMerkleRoot().ToString(), checkRoot.ToString());

        // Return nodes list
        std::vector<MerkleProof::Node> nodes {};
        for(const auto& node : treeProof.merkleTreeHashes)
        {
            nodes.push_back({node});
        }

        return std::make_tuple(nodes, checkRoot, treeProof);
    };

    // Create CMerkleTree and TSC versions of the proof and validate them
    for(size_t txnIndex = 0; txnIndex < NumTx; ++txnIndex)
    {
        const CTransactionRef& txn { block.vtx[txnIndex] };

        // Check CMerkleTree version and get what we need to create the TSC versions
        const auto& [ nodes, checkRoot, treeProof ] { CMerkleTreeLambda(txn) };

        // Create some TSC proofs in different ways
        std::vector<MerkleProof> merkleProofs {
            { txn->GetId(), txnIndex, checkRoot, nodes },
            { txn, txnIndex, checkRoot, nodes },
            { treeProof, txn->GetId(), checkRoot }
        };

        for(const auto& merkleProof : merkleProofs)
        {
            // Check good proof validates
            BOOST_CHECK(merkleProof.RecomputeAndCheckTarget());

            // Test serialising/deserialising
            CDataStream ss { SER_NETWORK, 0 };
            ss << merkleProof;
            MerkleProof deserialised {};
            ss >> deserialised;
            BOOST_CHECK(deserialised.RecomputeAndCheckTarget());
        }

        // Check invalid proof
        MerkleProof badProof { TxId { GetRandHash() }, txnIndex, checkRoot, nodes };
        BOOST_CHECK(! badProof.RecomputeAndCheckTarget());
    }

    // Check JSON formatting of TSC proof
    const CTransactionRef& txn { block.vtx[0] };
    const auto& [ nodes, checkRoot, treeProof ] { CMerkleTreeLambda(txn) };
    MerkleProof merkleProof { treeProof, txn->GetId(), checkRoot };
    UniValue json { merkleProof.ToJSON() };
    BOOST_CHECK_EQUAL(json["index"].get_int(), 0);
    BOOST_CHECK_EQUAL(json["txOrId"].get_str(), "4ebd325a4b394cff8c57e8317ccf5a8d0e2bdf1b8526f8aad6c8e43d8240621a");
    BOOST_CHECK_EQUAL(json["targetType"].get_str(), "merkleRoot");
    BOOST_CHECK_EQUAL(json["target"].get_str(), "ebea82c40a534011e25c6114a87475847e0451fcd68e6d2e98bda5db96b81219");
    BOOST_CHECK_EQUAL(json["nodes"].get_array().size(), 7);
}

BOOST_AUTO_TEST_SUITE_END()
