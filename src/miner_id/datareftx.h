// Copyright (c) 2022 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include "merkleproof.h"
#include "primitives/transaction.h"
#include "serialize.h"

/**
 * A class to encapsulate a datareftx P2P message.
 */
class DataRefTx
{
  public:

    DataRefTx() = default;

    DataRefTx(const CTransactionRef& txn, const MerkleProof& proof)
    : mTxn{txn}, mMerkleProof{proof}
    {}

    friend bool operator==(const DataRefTx&, const DataRefTx&);

    // Serialisation/deserialisation
    ADD_SERIALIZE_METHODS
    template <typename Stream, typename Operation>
    void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(mTxn);
        READWRITE(mMerkleProof);
    }

    // Accessors
    [[nodiscard]] const CTransactionRef& GetTxn() const { return mTxn; }
    [[nodiscard]] const MerkleProof& GetProof() const { return mMerkleProof; }

  private:

    // The dataref transaction
    CTransactionRef mTxn {nullptr};

    // Proof the transaction is contained in a block
    MerkleProof mMerkleProof {};

};

bool operator==(const DataRefTx& msg1, const DataRefTx& msg2);
inline bool operator!=(const DataRefTx& msg1, const DataRefTx& msg2)
{
    return !(msg1 == msg2);
}

