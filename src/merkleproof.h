// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include "merkletree.h"
#include "primitives/transaction.h"
#include "serialize.h"
#include "uint256.h"
#include "univalue.h"

#include <limits>

/**
 * Class to model a merkle proof conforming to the TSC standard:
 * https://tsc.bitcoinassociation.net/standards/merkle-proof-standardised-format/
 *
 * Currently only supports a target of type merkle root.
 */
class MerkleProof
{
  public:

    // A node within the proof
    struct Node
    {
        Node() = default;
        Node(const uint256 &value) : mValue{value} {}

        // Serialisation/deserialisation
        ADD_SERIALIZE_METHODS
        template <typename Stream, typename Operation>
        void SerializationOp(Stream& s, Operation ser_action)
        {
            READWRITE(mType);
            READWRITE(mValue);
        }

        // Only type=0 currently used
        uint8_t mType {0};

        // Since we only support type=0 just make this a uint256 in all cases
        uint256 mValue {};
    };
    
    MerkleProof() = default;

    // Construct for a full transaction
    MerkleProof(const std::shared_ptr<const CTransaction>& txn,
                size_t index,
                const uint256& target,
                const std::vector<Node>& nodes)
        : mFlags{0x01},
          mIndex{index},
          mTxn{txn},
          mTxnId{txn->GetId()},
          mTarget{target},
          mNodes{nodes}
    {
    }

    // Construct for a transaction ID
    MerkleProof(const TxId& txnid, size_t index, const uint256& target,
        const std::vector<Node>& nodes)
    : mIndex{index}, mTxnId{txnid}, mTarget{target}, mNodes{nodes}
    {}

    // Construct from a CMerkleTree::MerkleProof
    MerkleProof(const CMerkleTree::MerkleProof& treeProof, const TxId& txnid,
        const uint256& target);

    // Recompute our target and check if it matches the expected value
    bool RecomputeAndCheckTarget() const;

    // Serialisation/deserialisation
    ADD_SERIALIZE_METHODS
    template <typename Stream, typename Operation>
    void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(mFlags);
        READWRITE(VARINT(mIndex));

        if(ser_action.ForRead())
        {
            // Expecting a full transaction or just an ID?
            if(mFlags & 0x01)
            {
                CMutableTransaction mtx {};
                READWRITE(mtx);
                mTxn = MakeTransactionRef(std::move(mtx));
                mTxnId = mTxn->GetId();
            }
            else
            {
                READWRITE(mTxnId);
            }
        }
        else
        {
            // Full transaction or just ID?
            if(mTxn)
            {
                mTxn->Serialize(s);
            }
            else
            {
                mTxnId.Serialize(s);
            }
        }

        READWRITE(mTarget);
        READWRITE(mNodes);
    }

    // Convert to JSON
    [[nodiscard]] UniValue ToJSON(uint64_t maxTxnSize = std::numeric_limits<uint64_t>::max()) const;

  private:

    // Flags to indicate the format of the rest of the proof
    uint8_t mFlags { 0x0 };

    // Index of transaction this proof is for
    size_t mIndex {0};

    // Transaction and/or transaction ID
    std::shared_ptr<const CTransaction> mTxn { nullptr };
    TxId mTxnId {};

    // Target of proof (a merkle root)
    uint256 mTarget {};

    // List of nodes making up the proof
    std::vector<Node> mNodes {};
};

