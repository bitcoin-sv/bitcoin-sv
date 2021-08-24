// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "merkleproof.h"
#include "consensus/merkle.h"
#include "core_io.h"

MerkleProof::MerkleProof(
    const CMerkleTree::MerkleProof& treeProof,
    const TxId& txnid,
    const uint256& target)
: mFlags{0x00}, mIndex{treeProof.transactionIndex}, mTxnId{txnid}, mTarget{target}
{
    // Build nodes
    for(const auto& hash : treeProof.merkleTreeHashes)
    {
        mNodes.push_back(Node{hash});
    }
}

// Recompute our target and check if it matches the expected value
bool MerkleProof::RecomputeAndCheckTarget() const
{
    // Convert our nodes into a list of hashes
    std::vector<uint256> hashes {};
    for(const auto& node : mNodes)
    {
        hashes.push_back(node.mValue);
    }

    // Calculated expected merkle root and see if it matches
    uint256 checkRoot { ComputeMerkleRootFromBranch(mTxnId, hashes, mIndex) };
    return checkRoot == mTarget;
}

// Convert to JSON
UniValue MerkleProof::ToJSON(uint64_t maxTxnSize) const
{
    UniValue document { UniValue::VOBJ };

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

    UniValue nodes { UniValue::VARR };
    for(const auto& node : mNodes)
    {
        nodes.push_back(node.mValue.ToString());
    }
    document.push_back(Pair("nodes", nodes));

    return document;
}

