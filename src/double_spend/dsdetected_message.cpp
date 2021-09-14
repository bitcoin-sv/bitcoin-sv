// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#include "double_spend/dsdetected_message.h"

#include "config.h"
#include "merkleproof.h"
#include "primitives/block.h"
#include "uint256.h"
#include <algorithm>
#include <boost/functional/hash.hpp>
#include <iterator>

using namespace std;

// Comparison operator for DSDetected
bool operator==(const DSDetected& a, const DSDetected& b)
{
    return a.GetVersion() == b.GetVersion() &&
           a.mBlockList == b.mBlockList;
}

// Comparison operator for DSDetected::BlockDetails
bool operator==(const DSDetected::BlockDetails& a,
                const DSDetected::BlockDetails& b)
{
    return a.mBlockHeaders == b.mBlockHeaders &&
           a.mMerkleProof == b.mMerkleProof;
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

bool IsValid(const DSDetected::BlockDetails& fork)
{
    return FormsChain(fork.mBlockHeaders) &&
           !ContainsDuplicateHeaders(fork.mBlockHeaders) &&
           fork.mMerkleProof.Verify();
    
    // TODO: Check POW for each fork
}

bool IsValid(const DSDetected& msg)
{
    const bool all_forks_valid = std::all_of(
        msg.begin(), msg.end(), [](const DSDetected::BlockDetails& fork) {
            return IsValid(fork);
        });
    if(!all_forks_valid)
        return false;

    // Check all forks have the same common ancestor
    const auto it = adjacent_find(
        msg.begin(), msg.end(), [](const auto& fork1, const auto& fork2) {
            assert(!fork1.mBlockHeaders.empty());
            assert(!fork2.mBlockHeaders.empty());
            return fork1.mBlockHeaders.back().hashPrevBlock !=
                   fork2.mBlockHeaders.back().hashPrevBlock;
        });
    if(it != msg.end())
        return false;

    return true;
}

namespace
{
    bool linked(const CBlockHeader& a, const CBlockHeader& b)
    {
        return a.hashPrevBlock == b.GetHash();
    }
}

bool FormsChain(const std::vector<CBlockHeader>& headers)
{
    if(headers.empty())
        return false;

    if(headers.size() == 1)
        return true;

    const auto it{mismatch(headers.begin(),
                           headers.end() - 1,
                           headers.begin() + 1,
                           headers.end(),
                           linked)};
    return it.second == headers.end();
}

bool ContainsDuplicateHeaders(const std::vector<CBlockHeader>& headers)
{
    vector<size_t> hashes;
    hashes.reserve(headers.size());
    transform(headers.begin(),
              headers.end(),
              back_inserter(hashes),
              [](const auto& h) { return hash_value(h); });

    sort(hashes.begin(), hashes.end());

    return adjacent_find(hashes.begin(), hashes.end()) != hashes.end();
}

