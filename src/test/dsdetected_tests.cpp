// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#include "config.h"
#include "consensus/merkle.h"
#include "double_spend/dsdetected_message.h"
#include "hash.h"
#include "limited_cache.h"
#include "merkleproof.h"
#include "merkletree.h"
#include "primitives/block.h"
#include "primitives/transaction.h"
#include "streams.h"
#include "uint256.h"

#include <boost/algorithm/hex.hpp>
#include <boost/test/tools/old/interface.hpp>
#include <boost/test/unit_test.hpp>

#include <stdexcept>
#include <string_view>

namespace ba = boost::algorithm;

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
    MerkleProof CreateMerkleProof(const vector<pair<string, uint32_t>>& inputs)
    {
        // Create a block
        constexpr unsigned NumTx{10};
        CBlock block{};
        block.vtx.resize(NumTx);
        for(size_t j = 0; j < NumTx; j++)
        {
            CMutableTransaction mtx{};
            mtx.nLockTime = j;
            for(const auto& input : inputs)
            {
                CTxIn ip;
                const uint256 txid{uint256S(input.first)};
                ip.prevout = COutPoint{txid, input.second};
                mtx.vin.push_back(ip);
            }
            block.vtx[j] = make_shared<const CTransaction>(std::move(mtx));
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
            nodes.push_back(MerkleProof::Node{node});
        }

        return {txn, 1, checkRoot, nodes};
    }

    MerkleProof CreateMerkleProof()
    {
        return CreateMerkleProof(vector<pair<string, uint32_t>>{});
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
        str << "MerkleProof: " << HashMerkleProof(fork.mMerkleProof).ToString();
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

BOOST_AUTO_TEST_CASE(sorted_hasher)
{
    using OutPoints = vector<pair<string, uint32_t>>;
    
    std::vector<CBlockHeader> headers{CBlockHeader{}};

    DSDetected msg1;
    std::vector<DSDetected::BlockDetails> blocks1{};
    blocks1.push_back(
        DSDetected::BlockDetails{headers,
                                 CreateMerkleProof(OutPoints{{"42", 0}})});
    blocks1.push_back(
        DSDetected::BlockDetails{headers,
                                 CreateMerkleProof(OutPoints{{"42", 1}})});
    UnitTestAccess::SetBlockList(msg1, blocks1);
    const auto h1 = sort_hasher(msg1);

    DSDetected msg2;
    std::vector<DSDetected::BlockDetails> blocks2{};
    blocks2.push_back(
        DSDetected::BlockDetails{headers,
                                 CreateMerkleProof(OutPoints{{"42", 1}})});
    blocks2.push_back(
        DSDetected::BlockDetails{headers,
                                 CreateMerkleProof(OutPoints{{"42", 0}})});
    UnitTestAccess::SetBlockList(msg2, blocks2);

    const auto h2 = sort_hasher(msg2);
    BOOST_CHECK_EQUAL(h1, h2);
                                 
}

BOOST_AUTO_TEST_CASE(CreationSerialisation)
{
    // Test good DSDetected message creation and serialisation/deserialisation
    DSDetected msg;

    // Add some block details
    std::vector<CBlockHeader> headers{CBlockHeader{}};
    std::vector<DSDetected::BlockDetails> blocks{};
    blocks.push_back(DSDetected::BlockDetails{headers, CreateMerkleProof()});
    blocks.push_back(DSDetected::BlockDetails{headers, CreateMerkleProof()});
    UnitTestAccess::SetBlockList(msg, blocks);
    BOOST_CHECK_EQUAL(msg.size(), 2U);
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
      "divergentBlockHash": "14508459b221041eab257d2baaa7459775ba748246c8403609eb708f0e57e74b",
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
        "index": 1,
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
    },
    {
      "divergentBlockHash": "14508459b221041eab257d2baaa7459775ba748246c8403609eb708f0e57e74b",
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
        "index": 1,
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
      "divergentBlockHash": "14508459b221041eab257d2baaa7459775ba748246c8403609eb708f0e57e74b",
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
        "index": 1,
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
    },
    {
      "divergentBlockHash": "14508459b221041eab257d2baaa7459775ba748246c8403609eb708f0e57e74b",
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
        "index": 1,
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
    BOOST_CHECK_THROW(ss >> msg, std::runtime_error);
}

BOOST_AUTO_TEST_CASE(deserialize_happy_case)
{
    vector<uint8_t> v;
    ba::unhex(
        "0100" // version
        "02"   // block count
        // block 0
        "01" // header count
        // header 0
        "02000000" // version
        "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f" // h(prev)
        "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f" // h(root)
        "00000000" // time
        "00000000" // bits
        "00000000" // nonce
        // Merkle proof
        "05" // flags
        "01" // index
        "0a" // tx length
        // tx
        "02000000" // version
        "00"       // ip count
        "00"       // op count
        "00000000" // Lock time
        // Merkle root
        "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f" // h(prev)
        "00" // node count
        // block 1
        "01" // header count
        // header 0
        "02000000" // version
        "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f" // h(prev)
        "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f" // h(root)
        "00000000" // time
        "00000000" // bits
        "00000000" // nonce
        // Merkle proof
        "05" // flags
        "01" // index
        "0a" // tx length
        // tx
        "02000000" // version
        "00"       // ip count
        "00"       // op count
        "00000000" // Lock time
        // Merkle root
        "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f" // h(prev)
        "01" // node count
        // node 0
        "00" // node type
        "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f", // hash
        back_inserter(v));

    const CSerializeData data{v.begin(), v.end()};
    CDataStream ss{data.begin(), data.end(), SER_NETWORK, 0};
    DSDetected actual;
    ss >> actual;
}

BOOST_AUTO_TEST_CASE(deserialize_invalid_dsdetected_version)
{
    vector<uint8_t> v;
    ba::unhex("0200", // <- invalid version
              back_inserter(v));

    const CSerializeData data{v.begin(), v.end()};
    CDataStream ss{data.begin(), data.end(), SER_NETWORK, 0};
    DSDetected msg;
    try
    {
        ss >> msg;
        BOOST_FAIL("Expected runtime_error");
    }
    catch(const runtime_error& e)
    {
        const string_view expected{"Unsupported DSDetected message version"};
        BOOST_CHECK_EQUAL(expected, e.what());
    }
}

BOOST_AUTO_TEST_CASE(deserialize_too_few_block_details)
{
    vector<uint8_t> v;
    ba::unhex(
        "0100"
        "01" // invalid block count
        // block 0
        "01" // header count
        // header 0
        "02000000" // version
        "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f" // h(prev)
        "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f" // h(root)
        "00000000" // time
        "00000000" // bits
        "00000000" // nonce
        // Merkle proof
        "05" // flags
        "01" // index
        "0a" // tx length
        // tx
        "02000000" // version
        "00"       // ip count
        "00"       // op count
        "00000000" // Lock time
        // Merkle root
        "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f" // h(prev)
        "00", // node count
        back_inserter(v));

    const CSerializeData data{v.begin(), v.end()};
    CDataStream ss{data.begin(), data.end(), SER_NETWORK, 0};
    DSDetected msg;
    try
    {
        ss >> msg;
        BOOST_FAIL("Expected runtime_error");
    }
    catch(const runtime_error& e)
    {
        const string_view expected{"DSDetected invalid block count"};
        BOOST_CHECK_EQUAL(expected, e.what());
    }
}

BOOST_AUTO_TEST_CASE(deserialize_no_block_headers)
{
    vector<uint8_t> v;
    ba::unhex(
        "0100" // version
        "02"   // fork count
        // fork 0
        "00", // <- invalid header count
        back_inserter(v));

    const CSerializeData data{v.begin(), v.end()};
    CDataStream ss{data.begin(), data.end(), SER_NETWORK, 0};
    DSDetected msg;
    try
    {
        ss >> msg;
        BOOST_FAIL("Expected runtime_error");
    }
    catch(const runtime_error& e)
    {
        const string_view expected{"Invalid DSDetected message - no block headers"};
        BOOST_CHECK_EQUAL(expected, e.what());
    }
}

BOOST_AUTO_TEST_CASE(deserialize_invalid_invalid_merkle_proof_flags)
{
    vector<uint8_t> v;
    ba::unhex(
        "0100" // version
        "02"   // fork count
        // fork 0
        "01" // header count
        // header 0
        "02000000" // version
        "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f" // h(prev)
        "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f" // h(root)
        "00000000" // time
        "00000000" // bits
        "00000000" // nonce
        // Merkle proof
        "01" // <- invalid flags
        "01" // index
        "0a" // tx length
        // tx
        "02000000" // version
        "00"       // ip count
        "00"       // op count
        "00000000" // Lock time
        // Merkle root
        "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f" // h(prev)
        "00" // node count
        // fork 1
        "01" // header count
        // header 0
        "02000000" // version
        "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f" // h(prev)
        "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f" // h(root)
        "00000000" // time
        "00000000" // bits
        "00000000" // nonce
        // Merkle proof
        "05" // flags
        "00" // index
        "0a" // tx length
        // tx
        "02000000" // version
        "00"       // ip count
        "00"       // op count
        "00000000" // Lock time
        // Merkle root
        "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f" // h(prev)
        "00", // node count
        back_inserter(v));

    const CSerializeData data{v.begin(), v.end()};
    CDataStream ss{data.begin(), data.end(), SER_NETWORK, 0};
    DSDetected msg;
    try
    {
        ss >> msg;
        BOOST_FAIL("Expected runtime_error");
    }
    catch(const runtime_error& e)
    {
        const string_view expected{"Unsupported DSDetected merkle proof flags"};
        BOOST_CHECK_EQUAL(expected, e.what());
    }
}

BOOST_AUTO_TEST_CASE(deserialize_invalid_invalid_merkle_proof_index)
{
    vector<uint8_t> v;
    ba::unhex(
        "0100" // version
        "02"   // fork count
        // fork 0
        "01" // header count
        // header 0
        "02000000" // version
        "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f" // h(prev)
        "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f" // h(root)
        "00000000" // time
        "00000000" // bits
        "00000000" // nonce
        // Merkle proof
        "05" // flags
        "00" // <- invalid index
        "0a" // tx length
        // tx
        "02000000" // version
        "00"       // ip count
        "00"       // op count
        "00000000" // Lock time
        // Merkle root
        "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f" // h(prev)
        "00" // node count
        // fork 1
        "01" // header count
        // header 0
        "02000000" // version
        "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f" // h(prev)
        "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f" // h(root)
        "00000000" // time
        "00000000" // bits
        "00000000" // nonce
        // Merkle proof
        "05" // flags
        "00" // index
        "0a" // tx length
        // tx
        "02000000" // version
        "00"       // ip count
        "00"       // op count
        "00000000" // Lock time
        // Merkle root
        "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f" // h(prev)
        "00", // node count
        back_inserter(v));

    const CSerializeData data{v.begin(), v.end()};
    CDataStream ss{data.begin(), data.end(), SER_NETWORK, 0};
    DSDetected msg;
    try
    {
        ss >> msg;
        BOOST_FAIL("Expected runtime_error");
    }
    catch(const runtime_error& e)
    {
        const string_view expected{"Unsupported DSDetected merkle proof index"};
        BOOST_CHECK_EQUAL(expected, e.what());
    }
}

BOOST_AUTO_TEST_CASE(deserialize_invalid_invalid_merkle_proof_node_type)
{
    vector<uint8_t> v;
    ba::unhex(
        "0100" // version
        "02"   // fork count
        // fork 0
        "01" // header count
        // header 0
        "02000000" // version
        "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f" // h(prev)
        "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f" // h(root)
        "00000000" // time
        "00000000" // bits
        "00000000" // nonce
        // Merkle proof
        "05" // flags
        "01" // index
        "0a" // tx length
        // tx
        "02000000" // version
        "00"       // ip count
        "00"       // op count
        "00000000" // Lock time
        // Merkle root
        "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f" // h(prev)
        "00" // node count
        // fork 1
        "01" // header count
        // header 0
        "02000000" // version
        "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f" // h(prev)
        "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f" // h(root)
        "00000000" // time
        "00000000" // bits
        "00000000" // nonce
        // Merkle proof
        "05" // flags
        "01" // index
        "0a" // tx length
        // tx
        "02000000" // version
        "00"       // ip count
        "00"       // op count
        "00000000" // Lock time
        // Merkle root
        "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f" // h(prev)
        "01" // node count
        // node 0
        "01" // <- invalid node type
        "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f", // hash
        back_inserter(v));

    const CSerializeData data{v.begin(), v.end()};
    CDataStream ss{data.begin(), data.end(), SER_NETWORK, 0};
    DSDetected msg;
    try
    {
        ss >> msg;
        BOOST_FAIL("Expected runtime_error");
    }
    catch(const runtime_error& e)
    {
        const string_view expected{"Unsupported DSDetected merkle proof type"};
        BOOST_CHECK_EQUAL(expected, e.what());
    }
}

BOOST_AUTO_TEST_CASE(validate_fork_count)
{
    DSDetected msg;
    BOOST_CHECK(!ValidateForkCount(msg));

    std::vector<DSDetected::BlockDetails> blocks{};

    std::vector<CBlockHeader> headers_1{CBlockHeader{}};
    blocks.push_back(DSDetected::BlockDetails{headers_1, CreateMerkleProof()});
    UnitTestAccess::SetBlockList(msg, blocks);
    BOOST_CHECK_EQUAL(msg.size(), 1U);
    BOOST_CHECK(!ValidateForkCount(msg));

    std::vector<CBlockHeader> headers_2{CBlockHeader{}};
    blocks.push_back(DSDetected::BlockDetails{headers_2, CreateMerkleProof()});
    UnitTestAccess::SetBlockList(msg, blocks);
    BOOST_CHECK_EQUAL(msg.size(), 2U);
    BOOST_CHECK(ValidateForkCount(msg));
}

BOOST_AUTO_TEST_CASE(IsValid_no_headers)
{
    const DSDetected::BlockDetails v;
    BOOST_CHECK(!IsValid(v));
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
    BOOST_CHECK(FormsChain(headers));

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
    BOOST_CHECK_EQUAL(msg.size(), 2U);

    BOOST_CHECK(ValidateCommonAncestor(msg));

    CBlockHeader h;
    const uint256 i = uint256S("42");
    h.hashPrevBlock = i;
    std::vector<CBlockHeader> headers_3{h};
    blocks.push_back(DSDetected::BlockDetails{headers_3, CreateMerkleProof()});
    UnitTestAccess::SetBlockList(msg, blocks);
    BOOST_CHECK_EQUAL(msg.size(), 3U);

    BOOST_CHECK(!ValidateCommonAncestor(msg));
}

BOOST_AUTO_TEST_CASE(ds_outpoints)
{
    DSDetected msg;

    std::vector<DSDetected::BlockDetails> blocks{};

    using OutPoints = vector<pair<string, uint32_t>>;
    std::vector<CBlockHeader> headers_0{CBlockHeader{}};
    blocks.push_back(
        DSDetected::BlockDetails{headers_0,
                                 CreateMerkleProof(OutPoints{{"42", 0}})});

    std::vector<CBlockHeader> headers_1{CBlockHeader{}};
    blocks.push_back(
        DSDetected::BlockDetails{headers_1,
                                 CreateMerkleProof(OutPoints{{"42", 1}})});

    UnitTestAccess::SetBlockList(msg, blocks);
    BOOST_CHECK(!ValidateDoubleSpends(msg));

    std::vector<CBlockHeader> headers_2{CBlockHeader{}};
    blocks.push_back(
        DSDetected::BlockDetails{headers_2,
                                 CreateMerkleProof(OutPoints{{"42", 0}})});
    UnitTestAccess::SetBlockList(msg, blocks);
    BOOST_CHECK(!ValidateDoubleSpends(msg));

    std::vector<CBlockHeader> headers_3{CBlockHeader{}};
    blocks.push_back(
        DSDetected::BlockDetails{headers_3,
                                 CreateMerkleProof(OutPoints{{"42", 0},{"42", 1}})});
    UnitTestAccess::SetBlockList(msg, blocks);
    BOOST_CHECK(ValidateDoubleSpends(msg));
}

BOOST_AUTO_TEST_CASE(tx_uniqueness)
{
    DSDetected msg;

    std::vector<DSDetected::BlockDetails> blocks{};

    using OutPoints = vector<pair<string, uint32_t>>;
    std::vector<CBlockHeader> headers_0{CBlockHeader{}};
    blocks.push_back(
        DSDetected::BlockDetails{headers_0,
                                 CreateMerkleProof(OutPoints{{"42", 0}})});

    std::vector<CBlockHeader> headers_1{CBlockHeader{}};
    blocks.push_back(
        DSDetected::BlockDetails{headers_1,
                                 CreateMerkleProof(OutPoints{{"42", 1}})});
    UnitTestAccess::SetBlockList(msg, blocks);
    BOOST_CHECK(AreTxsUnique(msg));

    std::vector<CBlockHeader> headers_2{CBlockHeader{}};
    blocks.push_back(
        DSDetected::BlockDetails{headers_2,
                                 CreateMerkleProof(OutPoints{{"42", 0}})});
    UnitTestAccess::SetBlockList(msg, blocks);
    BOOST_CHECK(!AreTxsUnique(msg));
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(limited_cache_tests)

BOOST_AUTO_TEST_CASE(default_construction)
{
    limited_cache lc{2};
    BOOST_CHECK(!lc.contains(1));
    lc.insert(1);
    BOOST_CHECK(lc.contains(1));
    BOOST_CHECK(!lc.contains(2));
    lc.insert(2);
    BOOST_CHECK(lc.contains(1));
    BOOST_CHECK(lc.contains(2));
    lc.insert(3);
    BOOST_CHECK(!lc.contains(1));
    BOOST_CHECK(lc.contains(2));
    BOOST_CHECK(lc.contains(3));
    lc.insert(4);
    BOOST_CHECK(!lc.contains(2));
    BOOST_CHECK(lc.contains(3));
    BOOST_CHECK(lc.contains(4));
    lc.insert(5);
    BOOST_CHECK(!lc.contains(3));
    BOOST_CHECK(lc.contains(4));
    BOOST_CHECK(lc.contains(5));
}

BOOST_AUTO_TEST_SUITE_END()
