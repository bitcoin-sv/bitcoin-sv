// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "double_spend/dsdetected_message.h"
#include "config.h"
#include "consensus/merkle.h"
#include "hash.h"
#include "merkleproof.h"
#include "merkletree.h"
#include "streams.h"

#include <boost/test/unit_test.hpp>

namespace
{
    // Unique ID only
    class dsdetected_tests_uid;

    // Hash a MerkleProof
    uint256 HashMerkleProof(const MerkleProof& mp)
    {
        return (CHashWriter { SER_GETHASH, 0 } << mp).GetHash();
    }

    // Create a reproducable MerkleProof
    MerkleProof CreateMerkleProof()
    {
        // Create a block
        constexpr unsigned NumTx {10};
        CBlock block {};
        block.vtx.resize(NumTx);
        for(size_t j = 0; j < NumTx; j++)
        {   
            CMutableTransaction mtx {};
            mtx.nLockTime = j;
            block.vtx[j] = MakeTransactionRef(std::move(mtx));
        }

        // Create proof for coinbase txn from block
        const CTransactionRef& txn { block.vtx[0] };
        CMerkleTree merkleTree { block.vtx, uint256(), 0 };
        CMerkleTree::MerkleProof treeProof { merkleTree.GetMerkleProof(txn->GetId(), false) };
        uint256 checkRoot { ComputeMerkleRootFromBranch(txn->GetId(), treeProof.merkleTreeHashes, treeProof.transactionIndex) };

        std::vector<MerkleProof::Node> nodes {};
        for(const auto& node : treeProof.merkleTreeHashes)
        {   
            nodes.push_back({node});
        }

        return { txn, 0, checkRoot, nodes };
    }
}

// Comparison operator for CBlockHeader
bool operator==(const CBlockHeader& bh1, const CBlockHeader& bh2)
{
    return bh1.GetHash() == bh2.GetHash();
}

// Comparison operator for DSDetected::BlockDetails
bool operator==(const DSDetected::BlockDetails& bd1, const DSDetected::BlockDetails& bd2)
{
    return bd1.mHeaderList == bd2.mHeaderList &&
           HashMerkleProof(bd1.mMerkleProof) == HashMerkleProof(bd2.mMerkleProof);
}

// Comparison operator for DSDetected
bool operator==(const DSDetected& ds1, const DSDetected& ds2)
{
    return ds1.GetVersion() == ds2.GetVersion() &&
           ds2.GetBlockList() == ds2.GetBlockList();
}

// Print DSDetected
std::ostream& operator<<(std::ostream& str, const DSDetected& msg)
{
    str << "Version: " << msg.GetVersion() << ", ";
    str << "BlockList: [ ";
    for(const auto& block : msg.GetBlockList())
    {
        str << "HeaderList: [";
        for(const auto& header : block.mHeaderList)
        {
            str << header.GetHash().ToString() << ",";
        }
        str << "], ";
        str << "MerkleProof: " << HashMerkleProof(block.mMerkleProof).ToString();
    }
    str << "]";
    return str;
}

template <>
struct DSDetected::UnitTestAccess<dsdetected_tests_uid>
{
    static void SetVersion(DSDetected& msg, uint16_t version)
    {
        msg.mVersion = version;
    }

    static void SetBlockList(DSDetected& msg, const std::vector<BlockDetails>& blocks)
    {
        msg.mBlockList = blocks;
    }
};
using UnitTestAccess = DSDetected::UnitTestAccess<dsdetected_tests_uid>;


BOOST_AUTO_TEST_SUITE(dsdetected)

BOOST_AUTO_TEST_CASE(CreationSerialisation)
{
    // Test good DSDetected message creation and serialisation/deserialisation
    DSDetected msg {};
    BOOST_CHECK_EQUAL(msg.GetVersion(), DSDetected::MSG_VERSION);
    BOOST_CHECK(msg.GetBlockList().empty());

    {
        CDataStream ss { SER_NETWORK, 0 };
        ss << msg;
        DSDetected deserialised {};
        ss >> deserialised;
        BOOST_CHECK_EQUAL(msg, deserialised);
    }

    // Add some block details
    std::vector<CBlockHeader> headers { CBlockHeader{} };
    std::vector<DSDetected::BlockDetails> blocks {};
    blocks.push_back(DSDetected::BlockDetails { headers, CreateMerkleProof() });
    UnitTestAccess::SetBlockList(msg, blocks);
    BOOST_CHECK_EQUAL(msg.GetBlockList().size(), 1);
    {
        CDataStream ss { SER_NETWORK, 0 };
        ss << msg;
        DSDetected deserialised {};
        ss >> deserialised;
        BOOST_CHECK_EQUAL(msg, deserialised);
    }

    // JSON serialisation for small transaction
    auto& config { GlobalConfig::GetModifiableGlobalConfig() };
    UniValue json { msg.ToJSON(config) };
    std::string jsonStr { json.write(2) };
    BOOST_CHECK_EQUAL(jsonStr, R"!({
  "version": 1,
  "blocks": [
    {
      "headers": [
        {
          "version": 0,
          "hashPrevBlock": "0000000000000000000000000000000000000000000000000000000000000000",
          "hashMerkleRoot": "0000000000000000000000000000000000000000000000000000000000000000",
          "time": 0,
          "bits": 0,
          "nonce": 0
        }
      ],
      "merkleProof": {
        "index": 0,
        "txOrId": "02000000000000000000",
        "targetType": "merkleRoot",
        "target": "b9d4ad1b47f176c83ca56ca0c4cff7af1f976119f4cc3e036c7a835f1da3bf29",
        "nodes": [
          "d877b150e2f2cb183f38643fca5169da842be9c8fb841570d6fdf496bd56e829",
          "7c9d845b0df91f64cdba247a30fe0457951e89bf5d59129c1e22160e9a4d1ec3",
          "fb95795a028885a9e63fee9d55dd5690adb382c4573c18385662c085f083aff6",
          "e05430a9d32cce4d0f352a0ac6ecea74e8f9b96f5d91c944ed1299fd25bafdf3"
        ]
      }
    }
  ]
})!");

    // JSON serialisation for oversize transaction
    BOOST_CHECK(config.SetDoubleSpendDetectedWebhookMaxTxnSize(8));
    json = msg.ToJSON(config);
    jsonStr = json.write(2);
    BOOST_CHECK_EQUAL(jsonStr, R"!({
  "version": 1,
  "blocks": [
    {
      "headers": [
        {
          "version": 0,
          "hashPrevBlock": "0000000000000000000000000000000000000000000000000000000000000000",
          "hashMerkleRoot": "0000000000000000000000000000000000000000000000000000000000000000",
          "time": 0,
          "bits": 0,
          "nonce": 0
        }
      ],
      "merkleProof": {
        "index": 0,
        "txOrId": "4ebd325a4b394cff8c57e8317ccf5a8d0e2bdf1b8526f8aad6c8e43d8240621a",
        "targetType": "merkleRoot",
        "target": "b9d4ad1b47f176c83ca56ca0c4cff7af1f976119f4cc3e036c7a835f1da3bf29",
        "nodes": [
          "d877b150e2f2cb183f38643fca5169da842be9c8fb841570d6fdf496bd56e829",
          "7c9d845b0df91f64cdba247a30fe0457951e89bf5d59129c1e22160e9a4d1ec3",
          "fb95795a028885a9e63fee9d55dd5690adb382c4573c18385662c085f083aff6",
          "e05430a9d32cce4d0f352a0ac6ecea74e8f9b96f5d91c944ed1299fd25bafdf3"
        ]
      }
    }
  ]
})!");
}

BOOST_AUTO_TEST_CASE(MsgMalformed)
{
    // Test wrong version
    DSDetected msg {};
    UnitTestAccess::SetVersion(msg, 0x02);
    CDataStream ss { SER_NETWORK, 0 };
    ss << msg;
    DSDetected deserialised {};
    BOOST_CHECK_THROW(ss >> msg; , std::runtime_error);
}

BOOST_AUTO_TEST_SUITE_END()

