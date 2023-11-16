// Copyright (c) 2015-2016 The Bitcoin Core developers
// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.
#include <algorithm>
#include <stdexcept>
#include <string>
#include <tuple>
#include <iostream>

#include "boost/algorithm/hex.hpp"
#include <boost/test/tools/old/interface.hpp>
#include <boost/test/unit_test.hpp>

#include "consensus/merkle.h"
#include "double_spend/dsdetected_message.h"
#include "merkleproof.h"
#include "merkletree.h"
#include "serialize.h"
#include "support/allocators/zeroafterfree.h"
#include "task_helpers.h"
#include "test/test_bitcoin.h"
#include "uint256.h"

namespace ba = boost::algorithm;

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
    std::unique_ptr<CThreadPool<CQueueAdaptor>> pMerkleTreeThreadPool = std::make_unique<CThreadPool<CQueueAdaptor>>(false, "MerkleTreeThreadPoolTest", 3);
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

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(merkle_proof_tests)

BOOST_AUTO_TEST_CASE(default_construction)
{
    MerkleProof mp;
    BOOST_CHECK_EQUAL(0, mp.Flags());
    BOOST_CHECK_EQUAL(0U, mp.Index());
    BOOST_CHECK(mp.empty());
    BOOST_CHECK_EQUAL(0U, mp.size());
    const uint256 target;
    BOOST_CHECK(target == mp.Target());

    BOOST_CHECK(!contains_tx(mp));
    BOOST_CHECK(contains_txid(mp));
    BOOST_CHECK(contains_coinbase_tx(mp));
}
            
BOOST_AUTO_TEST_CASE(txid_construction)
{
    const TxId txid{uint256S("1")};
    size_t index{2};
    const uint256 target{uint256S("3")};
    const std::vector<MerkleProof::Node> nodes{{}};
    MerkleProof mp{txid, index, target, nodes};
    BOOST_CHECK(contains_txid(mp));
    BOOST_CHECK_EQUAL(index, mp.Index());
    BOOST_CHECK(target == mp.Target());
    BOOST_CHECK(!mp.empty());
    BOOST_CHECK_EQUAL(1U, mp.size());
    
    BOOST_CHECK(!contains_tx(mp));
    BOOST_CHECK(contains_txid(mp));
    BOOST_CHECK(!contains_coinbase_tx(mp));
}

BOOST_AUTO_TEST_CASE(tx_construction)
{
    CMutableTransaction mtx;
    const auto sp = std::make_shared<const CTransaction>(std::move(mtx));
    size_t index{2};
    const uint256 target{uint256S("3")};
    const std::vector<MerkleProof::Node> nodes{{}};
    const MerkleProof mp{sp, index, target, nodes};
    BOOST_CHECK(contains_tx(mp));
    BOOST_CHECK_EQUAL(index, mp.Index());
    BOOST_CHECK(target == mp.Target());
    BOOST_CHECK(!mp.empty());
    BOOST_CHECK_EQUAL(1U, mp.size());
}

BOOST_AUTO_TEST_CASE(deserialize_txid)
{
    // clang-format off
    const CSerializeData data{0x0, /* flags */
                              0x0, /* index */
                              /* txid */
                              0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                              0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
                              0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
                              0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
                              /* target */
                              0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                              0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
                              0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
                              0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
                              0x1, /* node count */
                              0x0, /* type */
                              /* hash */
                              0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                              0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
                              0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
                              0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
                              };
    // clang-format on

    CDataStream ds{data.begin(), data.end(), SER_NETWORK, 0};
    MerkleProof actual{};
    ds >> actual;

    // clang-format off
    const std::vector<uint8_t> data32{
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 
        0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f};
    // clang-format on

    const uint256 tmp{data32};
    const TxId txid{tmp};
    const size_t index{0};
    const uint256 target{tmp};
    const std::vector<MerkleProof::Node> nodes{MerkleProof::Node{tmp}};
    const MerkleProof expected{txid, index, target, nodes};
    BOOST_CHECK_EQUAL(expected, actual);
}

BOOST_AUTO_TEST_CASE(deserialize_tx)
{
    // clang-format off
    const CSerializeData data{0x5, /* flags */
                              /* index use CompactSize format */
                              static_cast<char>(0xfd), 
                              static_cast<char>(0xfd),
                              static_cast<char>(0x0),
                              0xa, /* tx len */
                              /* tx */
                              0x02, 0x0, 0x0, 0x0, /* version */
                              0x0, /* ip count */
                              0x0, /* op count */
                              0x0, 0x0, 0x0, 0x0, /* Lock time */ 
                              /* target */
                              0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                              0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
                              0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
                              0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
                              0x1, /* node count */
                              0x0, /* type */
                              /* hash */
                              0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                              0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
                              0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
                              0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
                              };
    // clang-format on

    CDataStream ds{data.begin(), data.end(), SER_NETWORK, 0};
    MerkleProof actual{};
    ds >> actual;

    // clang-format off
    const std::vector<uint8_t> data32{
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 
        0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f};
    // clang-format on

    const uint256 tmp{data32};
    const TxId txid{tmp};
    const size_t index{253};
    const uint256 target{tmp};
    const std::vector<MerkleProof::Node> nodes{MerkleProof::Node{tmp}};

    CMutableTransaction mtx;
    auto sp = std::make_shared<const CTransaction>(std::move(mtx));

    const MerkleProof expected{sp, index, target, nodes};
    BOOST_CHECK_EQUAL(expected, actual);
}

BOOST_AUTO_TEST_CASE(deserialize_std_example)
{
    // Taken from: github.com/bitcoin-sv-specs/merkle-proof-standard-example
    using namespace std;

    // clang-format off
    vector<uint8_t> v;
    ba::unhex("00" // flags
              "0c" // index
              "ef65a4611570303539143dabd6aa64dbd0f41ed89074406dc0e7cd251cf1efff" // txid
              "69f17b44cfe9c2a23285168fe05084e1254daa5305311ed8cd95b19ea6b0ed75" // target
              "05" // node count
              "00" // node type
              "8e66d81026ddb2dae0bd88082632790fc6921b299ca798088bef5325a607efb9" // hash
              "00"
              "4d104f378654a25e35dbd6a539505a1e3ddbba7f92420414387bb5b12fc1c10f"
              "00"
              "472581a20a043cee55edee1c65dd6677e09903f22992062d8fd4b8d55de7b060"
              "00"
              "6fcc978b3f999a3dbb85a6ae55edc06dd9a30855a030b450206c3646dadbd8c0"
              "00"
              "423ab0273c2572880cdc0030034c72ec300ec9dd7bbc7d3f948a9d41b3621e39",
              back_inserter(v));
    // clang-format on

    const CSerializeData data{v.begin(), v.end()};
    CDataStream ss{data.begin(), data.end(), SER_NETWORK, 0};
    MerkleProof actual{};
    ss >> actual;
   
    BOOST_CHECK_EQUAL(0, actual.Flags());
    BOOST_CHECK_EQUAL(12U, actual.Index());
    BOOST_CHECK_EQUAL(5U, actual.size());
    
    //const MerkleProof expected{};
    //BOOST_CHECK_EQUAL(expected, actual);
    //BOOST_CHECK(actual.Verify());
}

BOOST_AUTO_TEST_CASE(default_serialisation)
{
    // Test serialising/deserialising
    CDataStream ss{SER_NETWORK, 0};
    MerkleProof mp;
    ss << mp;
    MerkleProof deserialised{};
    ss >> deserialised;
    BOOST_CHECK_EQUAL(mp, deserialised); 
}
            
BOOST_AUTO_TEST_CASE(txid_serialisation)
{
    const TxId txid{uint256S("1")};
    size_t index{2};
    const uint256 target{uint256S("3")};
    const std::vector<MerkleProof::Node> nodes{{}};
    MerkleProof mp{txid, index, target, nodes};
    
    CDataStream ss{SER_NETWORK, 0};
    ss << mp;
    MerkleProof deserialised{};
    ss >> deserialised;
    BOOST_CHECK_EQUAL(mp, deserialised); 
}

BOOST_AUTO_TEST_CASE(tx_serialization)
{
    CMutableTransaction mtx;
    const auto sp = std::make_shared<const CTransaction>(std::move(mtx));
    size_t index{2};
    const uint256 target{uint256S("3")};
    const std::vector<MerkleProof::Node> nodes{{}};
    const MerkleProof mp{sp, index, target, nodes};
    
    CDataStream ss{SER_NETWORK, 0};
    ss << mp;
    MerkleProof deserialised{};
    ss >> deserialised;
    BOOST_CHECK_EQUAL(mp, deserialised); 
}

BOOST_AUTO_TEST_CASE(merkle_proof)
{
    using namespace std;

    // Build a block
    constexpr unsigned NumTx{100};
    CBlock block{};
    block.vtx.resize(NumTx);
    for(size_t j = 0; j < NumTx; j++)
    {
        CMutableTransaction mtx{};
        mtx.nLockTime = j;
        block.vtx[j] = make_shared<const CTransaction>(std::move(mtx));
    }

    // lambda to create CMerkleTree version of proof and return a nodes list
    // suitable for the TSC version, the merkle root from the proof and the
    // entire proof
    auto CMerkleTreeLambda = [&block](const CTransaction& txn) {
        // Create and check CMerkleTree version
        CMerkleTree merkleTree{block.vtx, uint256(), 0};
        CMerkleTree::MerkleProof treeProof{
            merkleTree.GetMerkleProof(txn.GetId(), false)};
        uint256 checkRoot{
            ComputeMerkleRootFromBranch(txn.GetId(),
                                        treeProof.merkleTreeHashes,
                                        treeProof.transactionIndex)};
        BOOST_CHECK_EQUAL(merkleTree.GetMerkleRoot().ToString(),
                          checkRoot.ToString());

        // Return nodes list
        vector<MerkleProof::Node> nodes{};
        for(const auto& node : treeProof.merkleTreeHashes)
        {
            nodes.push_back(MerkleProof::Node{node});
        }

        return make_tuple(nodes, checkRoot, treeProof);
    };

    // Create CMerkleTree and TSC versions of the proof and validate them
    for(size_t txnIndex = 0; txnIndex < NumTx; ++txnIndex)
    {
        const shared_ptr<const CTransaction>& txn{block.vtx[txnIndex]};

        // Check CMerkleTree version and get what we need to create the TSC
        // versions
        const auto& [nodes, checkRoot, treeProof]{CMerkleTreeLambda(*txn)};

        // Create some TSC proofs in different ways
        vector<MerkleProof> merkleProofs{
            {txn->GetId(), txnIndex, checkRoot, nodes},
            {txn, txnIndex, checkRoot, nodes},
            {treeProof, txn->GetId(), checkRoot}};

        for(const auto& merkleProof : merkleProofs)
        {
            // Check good proof validates
            BOOST_CHECK(merkleProof.Verify());

            // Test serialising/deserialising
            CDataStream ss{SER_NETWORK, 0};
            ss << merkleProof;
            MerkleProof deserialised{};
            ss >> deserialised;
            BOOST_CHECK(deserialised.Verify());
        }

        // Check invalid proof
        MerkleProof badProof{TxId{InsecureRand256()}, txnIndex, checkRoot, nodes};
        BOOST_CHECK(!badProof.Verify());
    }

    // Check JSON formatting of TSC proof
    const shared_ptr<const CTransaction>& txn{block.vtx[0]};
    const auto& [nodes, checkRoot, treeProof]{CMerkleTreeLambda(*txn)};
    MerkleProof merkleProof{treeProof, txn->GetId(), checkRoot};
    UniValue json{merkleProof.ToJSON()};
    BOOST_CHECK_EQUAL(json["index"].get_int(), 0);
    BOOST_CHECK_EQUAL(
        json["txOrId"].get_str(),
        "4ebd325a4b394cff8c57e8317ccf5a8d0e2bdf1b8526f8aad6c8e43d8240621a");
    BOOST_CHECK_EQUAL(json["targetType"].get_str(), "merkleRoot");
    BOOST_CHECK_EQUAL(
        json["target"].get_str(),
        "ebea82c40a534011e25c6114a87475847e0451fcd68e6d2e98bda5db96b81219");
    BOOST_CHECK_EQUAL(json["nodes"].get_array().size(), 7U);
}

BOOST_AUTO_TEST_SUITE_END()
