// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#include "double_spend/dsdetected_message.h"
#include "config.h"
#include "merkleproof.h"
#include "primitives/block.h"
#include "uint256.h"
#include <bits/c++config.h>
#include <boost/functional/hash.hpp>

// Comparison operator for DSDetected
bool operator==(const DSDetected& a, const DSDetected& b)
{
    return a.GetVersion() == b.GetVersion() &&
           a.GetBlockList() == b.GetBlockList();
}

// Comparison operator for DSDetected::BlockDetails
bool operator==(const DSDetected::BlockDetails& a,
                const DSDetected::BlockDetails& b)
{
    return a.mBlockHeaders == b.mBlockHeaders &&
           a.mMerkleProof == b.mMerkleProof;
//           HashMerkleProof(a.mMerkleProof) ==
//               HashMerkleProof(b.mMerkleProof); // cjg
}

std::size_t hash_value(const uint256& i)
{
    return boost::hash_range(i.begin(), i.end());
}

std::size_t hash_value(const CBlockHeader& header)
{
    size_t seed{0};
    boost::hash_combine(seed, header.nVersion);
    boost::hash_combine(seed, header.hashPrevBlock);
    boost::hash_combine(seed, header.hashPrevBlock);
    boost::hash_combine(seed, header.nTime);
    boost::hash_combine(seed, header.nBits);
    boost::hash_combine(seed, header.nNonce);
    return seed;
}

std::size_t hash_value(const DSDetected::BlockDetails& blocks)
{
    size_t seed{0};
    boost::hash_range(seed, blocks.mBlockHeaders.begin(), blocks.mBlockHeaders.end());
    boost::hash_combine(seed, blocks.mMerkleProof);
    return seed;
}

namespace std
{
    std::size_t hash<DSDetected>::operator()(const DSDetected& ds) const
    {
        std::size_t seed{0};
        boost::hash_combine(seed, ds.mVersion);
        boost::hash_range(seed, ds.mBlockList.begin(), ds.mBlockList.end());
        return seed;
    };
}

// Convert to JSON suitable for sending to a remote webhoo
UniValue DSDetected::ToJSON(const Config& config) const
{
    UniValue document{UniValue::VOBJ};

    document.push_back(Pair("version", mVersion));

    UniValue blocks{UniValue::VARR};
    for(const auto& block : mBlockList)
    {
        UniValue blockJson{UniValue::VOBJ};

        UniValue headers{UniValue::VARR};
        for(const auto& header : block.mBlockHeaders)
        {
            UniValue headerJson{UniValue::VOBJ};
            headerJson.push_back(Pair("version", header.nVersion));
            headerJson.push_back(
                Pair("hashPrevBlock", header.hashPrevBlock.ToString()));
            headerJson.push_back(
                Pair("hashMerkleRoot", header.hashMerkleRoot.ToString()));
            headerJson.push_back(
                Pair("time", static_cast<uint64_t>(header.nTime)));
            headerJson.push_back(
                Pair("bits", static_cast<uint64_t>(header.nBits)));
            headerJson.push_back(
                Pair("nonce", static_cast<uint64_t>(header.nNonce)));
            headers.push_back(headerJson);
        }
        blockJson.push_back(Pair("headers", headers));

        blockJson.push_back(
            Pair("merkleProof",
                 block.mMerkleProof.ToJSON(
                     config.GetDoubleSpendDetectedWebhookMaxTxnSize())));

        blocks.push_back(blockJson);
    }
    document.push_back(Pair("blocks", blocks));

    return document;
}
