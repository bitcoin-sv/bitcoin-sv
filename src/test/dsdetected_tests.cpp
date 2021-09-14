// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#include "config.h"
#include "consensus/merkle.h"
#include "double_spend/dsdetected_message.h"
#include "hash.h"
#include "merkleproof.h"
#include "merkletree.h"
#include "primitives/block.h"
#include "streams.h"
#include "uint256.h"

#include <boost/test/tools/old/interface.hpp>
#include <boost/test/unit_test.hpp>

using namespace std;

namespace
{
    // Unique ID only
    class dsdetected_tests_uid;

    // Hash a MerkleProof
    uint256 HashMerkleProof(const MerkleProof& mp)
    {
        return (CHashWriter{SER_GETHASH, 0} << mp).GetHash();
    }

    // Create a reproducable MerkleProof
    MerkleProof CreateMerkleProof()
    {
        // Create a block
        constexpr unsigned NumTx{10};
        CBlock block{};
        block.vtx.resize(NumTx);
        for(size_t j = 0; j < NumTx; j++)
        {
            CMutableTransaction mtx{};
            mtx.nLockTime = j;
            block.vtx[j] = MakeTransactionRef(std::move(mtx));
        }

        // Create proof for coinbase txn from block
        const CTransactionRef& txn{block.vtx[0]};
        CMerkleTree merkleTree{block.vtx, uint256(), 0};
        CMerkleTree::MerkleProof treeProof{
            merkleTree.GetMerkleProof(txn->GetId(), false)};
        uint256 checkRoot{
            ComputeMerkleRootFromBranch(txn->GetId(),
                                        treeProof.merkleTreeHashes,
                                        treeProof.transactionIndex)};

        std::vector<MerkleProof::Node> nodes{};
        for(const auto& node : treeProof.merkleTreeHashes)
        {
            nodes.push_back({node});
        }

        return {txn, 0, checkRoot, nodes};
    }
}

// Print DSDetected
std::ostream& operator<<(std::ostream& str, const DSDetected& msg)
{
    str << "Version: " << msg.GetVersion() << ", ";
    str << "BlockList: [ ";
    for(const auto& fork : msg)
    {
        str << "HeaderList: [";
        for(const auto& header : fork.mBlockHeaders)
        {
            str << header.GetHash().ToString() << ",";
        }
        str << "], ";
        str << "MerkleProof: "
            << HashMerkleProof(fork.mMerkleProof).ToString();
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

    static void SetBlockList(DSDetected& msg,
                             const std::vector<BlockDetails>& blocks)
    {
        msg.mBlockList = blocks;
    }
};
using UnitTestAccess = DSDetected::UnitTestAccess<dsdetected_tests_uid>;

BOOST_AUTO_TEST_SUITE(dsdetected)

BOOST_AUTO_TEST_CASE(default_construction)
{
    DSDetected msg;
    BOOST_CHECK_EQUAL(msg.GetVersion(), DSDetected::MSG_VERSION);
    BOOST_CHECK(msg.empty());
}

BOOST_AUTO_TEST_CASE(default_hash)
{
    const std::hash<DSDetected> hasher;
    const DSDetected msg1;
    const auto h11 = hasher(msg1); 
    const auto h12 = hasher(msg1); 
    BOOST_CHECK_EQUAL(h11, h12);
    
    DSDetected msg2;
    std::vector<CBlockHeader> headers{CBlockHeader{}};
    std::vector<DSDetected::BlockDetails> blocks{};
    blocks.push_back(DSDetected::BlockDetails{headers, CreateMerkleProof()});
    blocks.push_back(DSDetected::BlockDetails{headers, CreateMerkleProof()});
    UnitTestAccess::SetBlockList(msg2, blocks);
    const auto h21 = hasher(msg2); 
    BOOST_CHECK_NE(h11, h21);
}

BOOST_AUTO_TEST_CASE(CreationSerialisation)
{
    // Test good DSDetected message creation and serialisation/deserialisation
    DSDetected msg;

    {
        CDataStream ss{SER_NETWORK, 0};
        ss << msg;
        DSDetected deserialised{};
        ss >> deserialised;
        BOOST_CHECK_EQUAL(msg, deserialised);
    }

    // Add some block details
    std::vector<CBlockHeader> headers{CBlockHeader{}};
    std::vector<DSDetected::BlockDetails> blocks{};
    blocks.push_back(DSDetected::BlockDetails{headers, CreateMerkleProof()});
    UnitTestAccess::SetBlockList(msg, blocks);
    BOOST_CHECK_EQUAL(msg.size(), 1);
    {
        CDataStream ss{SER_NETWORK, 0};
        ss << msg;
        DSDetected deserialised{};
        ss >> deserialised;
        BOOST_CHECK_EQUAL(msg, deserialised);
    }

    // JSON serialisation for small transaction
    auto& config{GlobalConfig::GetModifiableGlobalConfig()};
    UniValue json{msg.ToJSON(config)};
    std::string jsonStr{json.write(2)};
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
    DSDetected msg{};
    UnitTestAccess::SetVersion(msg, 0x02);
    CDataStream ss{SER_NETWORK, 0};
    ss << msg;
    DSDetected deserialised{};
    BOOST_CHECK_THROW(ss >> msg;, std::runtime_error);
}

BOOST_AUTO_TEST_CASE(headers_form_chain)
{
    vector<CBlockHeader> headers;
    BOOST_CHECK(!FormsChain(headers));

    const CBlockHeader h1;
    headers.push_back(h1);
    BOOST_CHECK(FormsChain(headers));

    CBlockHeader h2;
    h2.hashPrevBlock = h1.GetHash();
    headers.insert(headers.begin(), h2);
    // BOOST_CHECK(FormsChain(headers));

    CBlockHeader h3;
    h3.hashPrevBlock = h2.GetHash();
    headers.insert(headers.begin(), h3);
    BOOST_CHECK(FormsChain(headers));

    CBlockHeader h4;
    h4.hashPrevBlock = h1.GetHash();
    headers.insert(headers.begin(), h4);
    BOOST_CHECK(!FormsChain(headers));
}

BOOST_AUTO_TEST_CASE(contains_duplicate_headers)
{
    vector<CBlockHeader> headers;
    BOOST_CHECK(!ContainsDuplicateHeaders(headers));

    const CBlockHeader h1;
    headers.push_back(h1);
    BOOST_CHECK(!ContainsDuplicateHeaders(headers));

    CBlockHeader h2;
    h2.nVersion = 42;
    headers.push_back(h2);
    BOOST_CHECK(!ContainsDuplicateHeaders(headers));

    headers.push_back(h2);
    BOOST_CHECK(ContainsDuplicateHeaders(headers));
}

BOOST_AUTO_TEST_CASE(common_ancestor)
{
    DSDetected msg;

    // Add some block details
    std::vector<DSDetected::BlockDetails> blocks{};

    std::vector<CBlockHeader> headers_1{CBlockHeader{}};
    blocks.push_back(DSDetected::BlockDetails{headers_1, CreateMerkleProof()});

    std::vector<CBlockHeader> headers_2{CBlockHeader{}};
    blocks.push_back(DSDetected::BlockDetails{headers_2, CreateMerkleProof()});

    UnitTestAccess::SetBlockList(msg, blocks);
    BOOST_CHECK_EQUAL(msg.size(), 2);

    BOOST_CHECK(IsValid(msg));

    CBlockHeader h;
    const uint256 i = uint256S("42");
    h.hashPrevBlock = i;
    std::vector<CBlockHeader> headers_3{h};
    blocks.push_back(DSDetected::BlockDetails{headers_3, CreateMerkleProof()});
    UnitTestAccess::SetBlockList(msg, blocks);
    BOOST_CHECK_EQUAL(msg.size(), 3);

    BOOST_CHECK(!IsValid(msg));
}

BOOST_AUTO_TEST_SUITE_END()
