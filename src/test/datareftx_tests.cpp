// Copyright (c) 2022 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "miner_id/datareftx.h"
#include "consensus/merkle.h"
#include "univalue.h"

#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>

#include <random>

namespace
{
    // Create a block with some fake dataref txns at random locations
    std::pair<CBlock, std::vector<size_t>> MakeBlock()
    {
        constexpr unsigned NumTx{10};
        constexpr unsigned NumDatarefTx{2};

        CBlock block {};
        block.vtx.resize(NumTx);
        for(size_t j = 0; j < NumTx; j++)
        {
            CMutableTransaction mtx {};
            mtx.nLockTime = j;
            block.vtx[j] = MakeTransactionRef(std::move(mtx));
        }

        std::mt19937 mt { insecure_rand() };
        std::uniform_real_distribution<float> dist { 1, static_cast<float>(block.vtx.size()) };
        std::vector<size_t> indexes {};
        for(size_t i = 0; i < NumDatarefTx; ++i)
        {
            size_t index {0};
            do
            {
                index = static_cast<size_t>(dist(mt));
                // Have to check random index is in range due to common library bug
                if(index >= block.vtx.size())
                {
                    index = 1;
                }
            } while(std::find(indexes.begin(), indexes.end(), index) != indexes.end());
            indexes.push_back(index);

            UniValue document { UniValue::VOBJ };
            UniValue data { UniValue::VOBJ };
            UniValue brfcJson { UniValue::VOBJ };
            brfcJson.push_back(Pair("example", "value"));
            data.push_back(Pair("Id", brfcJson));
            document.push_back(Pair("data", data));
            std::string dataRefJson { document.write() };

            CMutableTransaction mtx {};
            mtx.vin.resize(1);
            mtx.vin[0].prevout = COutPoint { uint256{}, static_cast<uint32_t>(i) };
            mtx.vout.resize(1);
            mtx.vout[0].nValue = Amount{i};
            const std::vector<uint8_t> MinerIDProtocolPrefix   { 0xac, 0x1e, 0xed, 0x88 };
            mtx.vout[0].scriptPubKey = CScript() << OP_FALSE << OP_RETURN << MinerIDProtocolPrefix
                                                 << std::vector<uint8_t> { dataRefJson.begin(), dataRefJson.end() };

            block.vtx[index] = MakeTransactionRef(std::move(mtx));
        }

        return { block, indexes };
    }

    // Until merkle proof Verify is updated to work for block hash target, we must check it manually
    bool CheckMerkleProof(const MerkleProof& merkleProof, const CTransactionRef& txn, const CBlock& block)
    {
        std::vector<uint256> hashes {};
        for(const auto& node : merkleProof)
        {
            hashes.push_back(node.mValue);
        }
        uint256 checkRoot { ComputeMerkleRootFromBranch(txn->GetId(), hashes, merkleProof.Index()) };
        return checkRoot == BlockMerkleRoot(block);
    }
}

std::ostream& operator<<(std::ostream& str, const DataRefTx& msg)
{
    str << "TxnId: " << msg.GetTxn()->GetId().ToString() << std::endl;
    str << "MerkleProof: " << msg.GetProof().ToJSON().write() << std::endl;
    return str;
}

BOOST_FIXTURE_TEST_SUITE(datareftx, BasicTestingSetup)

// Creation and serialising/deserialising
BOOST_AUTO_TEST_CASE(CreateAndSerialise)
{
    // Create fake block with a couple of dataref txns
    const auto& [ block, indexes] { MakeBlock() };

    // Check we can create and serialise datareftx messages for each dataref txn
    for(size_t index : indexes)
    {
        const CTransactionRef& txn { block.vtx[index] };

        // Get merkle proof
        const CMerkleTree merkleTree { block.vtx, uint256(), 0 };
        const CMerkleTree::MerkleProof treeProof { merkleTree.GetMerkleProof(txn->GetId(), false) };
        MerkleProof merkleProof { treeProof, txn->GetId(), block.GetHash() };

        BOOST_CHECK_EQUAL(merkleProof.Flags(), 0x00);
        BOOST_CHECK(CheckMerkleProof(merkleProof, txn, block));

        // Create datareftx message
        DataRefTx msg { txn, merkleProof };
        BOOST_CHECK(CheckMerkleProof(msg.GetProof(), msg.GetTxn(), block));

        // Serialise and deserialise
        CDataStream ss { SER_NETWORK, 0 };
        ss << msg;
        DataRefTx deserialised {};
        ss >> deserialised;
        BOOST_CHECK_EQUAL(msg, deserialised);
        BOOST_CHECK_EQUAL(deserialised.GetProof().Flags(), 0x00);
        BOOST_CHECK(CheckMerkleProof(deserialised.GetProof(), deserialised.GetTxn(), block));
    }
}

BOOST_AUTO_TEST_SUITE_END()

