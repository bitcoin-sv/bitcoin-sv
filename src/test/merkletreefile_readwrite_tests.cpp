// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "test/test_bitcoin.h"
#include "consensus/merkle.h"
#include "config.h"
#include "merkletreestore.h"

#include <boost/test/unit_test.hpp>

namespace
{
    struct RegtestingSetup : public BasicTestingSetup
    {
        RegtestingSetup() : BasicTestingSetup(CBaseChainParams::REGTEST)
        {
        }
    };

    // WrittenData holds information of Merkle Tree we want to use in later checks.
    struct WrittenData
    {
        // blockHash is needed to read Merkle Tree from the disk
        uint256 blockHash; 
        // Calculated Merkle root which is compared in later checks with root calculated from the Merkle proof
        uint256 writtenMerkleRoot;
        // Hash of a block's transaction we choose randomly. It is used to calculate Merkle proof in later checks.
        uint256 writtenRandomTxHash;
        // Index of a block's transaction we choose randomly. It is used to check proper positions in calculated Merkle proof.
        uint64_t writtenRandomTxIndex;
    };

    // NOTE function returns value in range [1, range]
    // use with caution on array indices
    template<uint64_t range>
    uint64_t InsecureRandRangeNonZero()
    {
        static_assert(range >= 1, "Invalid range");
        return InsecureRandRange(range) + 1;
    }

    CBlock CreateRandomBlock(const uint64_t numberOfTransactions)
    {
        CBlock block;
        block.vtx.resize(numberOfTransactions);
        block.nVersion = 42;
        block.hashPrevBlock = InsecureRand256();
        block.nBits = 0x207fffff;
        BOOST_REQUIRE(numberOfTransactions > 0);
        for (size_t i = 0; i < numberOfTransactions; ++i)
        {
            CMutableTransaction tx;
            tx.vin.resize(1);
            tx.vin[0].scriptSig.resize(InsecureRandRange(50));
            tx.vin[0].prevout = COutPoint(InsecureRand256(), 0);
            tx.vout.resize(1);
            tx.vout[0].nValue = Amount(InsecureRandRange(50));
            block.vtx[i] = MakeTransactionRef(tx);
        }

        bool mutated;
        block.hashMerkleRoot = BlockMerkleRoot(block, &mutated);
        return block;
    }

    void StoreTestData(const CBlock& block, const uint256& merkleRoot, std::vector<WrittenData>& writtenData)
    {
        WrittenData dataToStore;
        dataToStore.blockHash = block.GetHash();
        dataToStore.writtenMerkleRoot = merkleRoot;
        BOOST_CHECK(dataToStore.writtenMerkleRoot == BlockMerkleRoot(block));
        uint64_t indexOfRandomTx = InsecureRandRange(block.vtx.size());
        dataToStore.writtenRandomTxIndex = indexOfRandomTx;
        BOOST_REQUIRE(indexOfRandomTx < block.vtx.size());
        dataToStore.writtenRandomTxHash = block.vtx[indexOfRandomTx]->GetId();
        writtenData.push_back(dataToStore);
    }

    void CheckTestData(const std::vector<WrittenData>& writtenData, CMerkleTreeStore& merkleTreeStore)
    {
        // Check that merkle trees were successfully written
        for (WrittenData dataToCheck : writtenData)
        {
            auto merkleTreeReadFromDisk = merkleTreeStore.GetMerkleTree(dataToCheck.blockHash);
            BOOST_CHECK(merkleTreeReadFromDisk != nullptr);

            // Calculate merkle proof and merkle root from read data using previously randomly chosen transaction id.
            CMerkleTree::MerkleProof checkProof = merkleTreeReadFromDisk->GetMerkleProof(TxId(dataToCheck.writtenRandomTxHash), false);
            BOOST_CHECK(checkProof.transactionIndex == dataToCheck.writtenRandomTxIndex);
            // Calculate the root from Merkle proof and compare it with the root we calculated before the write
            BOOST_CHECK(ComputeMerkleRootFromBranch(
                dataToCheck.writtenRandomTxHash,
                checkProof.merkleTreeHashes,
                dataToCheck.writtenRandomTxIndex) == dataToCheck.writtenMerkleRoot);
        }
    }

    size_t GetMerkleTreesDataSize()
    {
        size_t dataSize = 0;
        fs::path path = GetDataDir() / "merkle";
        
        for (fs::recursive_directory_iterator it(path); it != fs::recursive_directory_iterator(); ++it)
        {
            if (!fs::is_directory(*it))
            {
                dataSize += fs::file_size(*it);
            }
        }
        return dataSize;
    }
}

BOOST_FIXTURE_TEST_SUITE(merkletreefile_readwrite_tests, RegtestingSetup)

BOOST_AUTO_TEST_CASE(write_read_test)
{
    // Maximum size of disk space for Merkle Trees is 500 MiB 
    testConfig.SetMaxMerkleTreeDiskSpace(500 * ONE_MEBIBYTE);
    CMerkleTreeStore merkleTreeStore(GetDataDir() / "merkle", 1 << 20);

    // Load data from the database
    BOOST_CHECK(merkleTreeStore.LoadMerkleTreeIndexDB());

    std::vector<WrittenData> writtenDataToCheck;
    // Create some random blocks and write their Merkle Trees to disk
    int32_t numberOfBlocks = static_cast<int32_t>(InsecureRandRangeNonZero<100>());
    for (int32_t i = 0; i < numberOfBlocks; ++i)
    {
        CBlock block = CreateRandomBlock(InsecureRandRangeNonZero<20000>());
        CMerkleTree merkleTree(block.vtx, block.GetHash(), i);
        BOOST_CHECK(merkleTreeStore.StoreMerkleTree(testConfig, merkleTree, i));

        // For later checks, save block hash, merkle root, hash and index of one of the transactions
        StoreTestData(block, merkleTree.GetMerkleRoot(), writtenDataToCheck);
    }

    CheckTestData(writtenDataToCheck, merkleTreeStore);
}

BOOST_AUTO_TEST_CASE(write_prune_load_test)
{
    // Maximum size of disk space for Merkle Trees is 200 MiB 
    testConfig.SetMaxMerkleTreeDiskSpace(200 * ONE_MEBIBYTE);
    CMerkleTreeStore merkleTreeStore(GetDataDir() / "merkle", 1 << 20);

    // Load data from the database
    BOOST_CHECK(merkleTreeStore.LoadMerkleTreeIndexDB());

    std::vector<WrittenData> writtenDataToCheck;
    // Create 1000 blocks, each with 4000 transactions and write their Merkle Trees to disk
    // One Merkle Tree takes around 250 kB. This will make around 130 Merkle Trees in each data file (32 MiB)
    // As soon as 200 MiB limit is reached, pruning will happen on every 130 blocks/Merkle Trees.
    int32_t numberOfBlocks = 1000;
    for (int32_t i = 0; i < numberOfBlocks; ++i)
    {
        CBlock block = CreateRandomBlock(4000);
        CMerkleTree merkleTree(block.vtx, block.GetHash(), i);
        BOOST_CHECK(merkleTreeStore.StoreMerkleTree(testConfig, merkleTree, i));

        uint256 merkleRoot = merkleTree.GetMerkleRoot();
        BOOST_CHECK(merkleRoot == BlockMerkleRoot(block));
        // For later checks, save block hashes, their merkle roots, hashes and indices of one of the transactions
        // We save last number of blocks that for sure were not pruned
        if (i > (numberOfBlocks - testConfig.GetMinBlocksToKeep()))
        {
            StoreTestData(block, merkleTree.GetMerkleRoot(), writtenDataToCheck);
        }
    }

    // Load data from the database again
    BOOST_CHECK(merkleTreeStore.LoadMerkleTreeIndexDB());
    CheckTestData(writtenDataToCheck, merkleTreeStore);

    BOOST_CHECK(GetMerkleTreesDataSize() <= testConfig.GetMaxMerkleTreeDiskSpace());

}
BOOST_AUTO_TEST_SUITE_END()

static_assert(0 == CalculatePreferredMerkleTreeSize(std::numeric_limits<uint64_t>::min()));
static_assert(295'147'905'179'352'768 == CalculatePreferredMerkleTreeSize(std::numeric_limits<uint64_t>::max()));
static_assert(64 == CalculatePreferredMerkleTreeSize(4'000));
