// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#include "merkleproof.h"

#include "consensus/merkle.h"
#include "core_io.h"
#include "primitives/transaction.h"
#include "uint256.h"

#include <boost/array.hpp>
#include <iterator>

#include "boost/functional/hash.hpp"

MerkleProof::MerkleProof(const CMerkleTree::MerkleProof& treeProof,
                         const TxId& txnid,
                         const uint256& target)
    : mIndex{treeProof.transactionIndex}, mTxnId{txnid}, mTarget{target}
{
    // Build nodes
    for(const auto& hash : treeProof.merkleTreeHashes)
    {
        mNodes.push_back(Node{hash});
    }
}

// Recompute our target and check if it matches the expected value
bool MerkleProof::Verify() const
{
    // Convert our nodes into a list of hashes
    std::vector<uint256> hashes{};
    for(const auto& node : mNodes)
    {
        hashes.push_back(node.mValue);
    }

    // Calculated expected merkle root and see if it matches
    uint256 checkRoot{ComputeMerkleRootFromBranch(mTxnId, hashes, mIndex)};
    return checkRoot == mTarget;
}

// Convert to JSON
UniValue MerkleProof::ToJSON(uint64_t maxTxnSize) const
{
    UniValue document{UniValue::VOBJ};

    document.push_back(Pair("index", mIndex));

    // If we have a full transaction check it's not too large to serialise
    if(mTxn && mTxn->GetTotalSize() <= maxTxnSize)
    {
        document.push_back(Pair("txOrId", EncodeHexTx(*mTxn)));
    }
    else
    {
        document.push_back(Pair("txOrId", mTxnId.ToString()));
    }

    document.push_back(Pair("targetType", "merkleRoot"));
    document.push_back(Pair("target", mTarget.ToString()));

    UniValue nodes{UniValue::VARR};
    for(const auto& node : mNodes)
    {
        nodes.push_back(node.mValue.ToString());
    }
    document.push_back(Pair("nodes", nodes));

    return document;
}

static bool operator==(const MerkleProof::Node& a, const MerkleProof::Node& b)
{
    return a.mType == b.mType &&
           a.mValue == b.mValue;
}

bool operator==(const MerkleProof& a, const MerkleProof& b)
{
    if(a.mTxn && !b.mTxn)
        return false;

    if(b.mTxn && !a.mTxn)
        return false;

    return a.mFlags == b.mFlags &&
           a.mIndex == b.mIndex &&
           a.mTxnId == b.mTxnId && 
           a.mTxLen == b.mTxLen &&
           (a.mTxn && b.mTxn ? *a.mTxn == *b.mTxn : true) &&
           a.mTarget == b.mTarget &&
           a.mNodes == b.mNodes;
}

bool operator!=(const MerkleProof& a, const MerkleProof& b)
{
    return !(a == b);
}

std::ostream& operator<<(std::ostream& os, const MerkleProof::Node& node)
{
    os << "Type: " << static_cast<int>(node.mType)
       << "\n\tValue: ";

    for(const auto x : node.mValue) 
        os << static_cast<int>(x);
 
    return os;
}

std::ostream& operator<<(std::ostream& os, const MerkleProof& mp)
{
    os << "Flags: " << static_cast<int>(mp.Flags())
       << "\nIndex: " << static_cast<int>(mp.Index())
       << "\nTxId: " << mp.mTxnId.ToString()
       << "\nTx Length: " << mp.mTxLen
       << "\nTx*: " << mp.mTxn
       << "\nTarget: " << mp.mTarget.ToString()
       << "\nNode Count: " << mp.mNodes.size();

    for(const auto& node : mp.mNodes)
        os << "\n\t" << node;
    
    return os;
}

std::size_t hash_value(const MerkleProof::Node& node)
{
    std::size_t seed{0};
    boost::hash_combine(seed, node.mType);
    boost::hash_combine(seed, node.mValue);
    return seed;
}

std::size_t hash_value(const MerkleProof& mp)
{
    size_t seed{0};
    boost::hash_combine(seed, mp.mFlags);
    boost::hash_combine(seed, mp.mIndex);
    boost::hash_combine(seed, mp.mTarget);
    boost::hash_combine(seed, mp.mTxLen);
    boost::hash_combine(seed, mp.mTxn->GetId());
    boost::hash_combine(seed, mp.mTxnId);

    boost::hash_range(seed, mp.mNodes.begin(), mp.mNodes.end());
    
    return seed;
}

