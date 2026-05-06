// Copyright (c) 2026 BSV Blockchain
// Distributed under the Open BSV software license, see the accompanying file LICENSE.
#pragma once

#include <memory>

#include "consensus/validation.h"
#include "primitives/transaction.h"
#include "uint256.h"

class CBlockIndex;

/** A transaction with a merkle branch linking it to the block chain. */
class CMerkleTx
{
private:
    /** Constant used in hashBlock to indicate tx has been abandoned */
    static const uint256 ABANDON_HASH;

public:
    std::shared_ptr<const CTransaction> tx;
    uint256 hashBlock;

    /**
     * An nIndex == -1 means that hashBlock (in nonzero) refers to the earliest
     * block in the chain we know this or any in-wallet dependency conflicts
     * with. Older clients interpret nIndex == -1 as unconfirmed for backward
     * compatibility.
     */
    int nIndex{-1};

    CMerkleTx() : tx{std::make_shared<const CTransaction>()} {}

    CMerkleTx(std::shared_ptr<const CTransaction> arg) : tx(std::move(arg)) {}

    /**
     * Helper conversion operator to allow passing CMerkleTx where CTransaction
     * is expected.
     * TODO: adapt callers and remove this operator.
     */
    operator const CTransaction&() const { return *tx; }

    void SetTx(CTransactionRef arg) { tx = std::move(arg); }

    ADD_SERIALIZE_METHODS

    template<typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        // For compatibility with older versions.
        std::vector<uint256> vMerkleBranch;
        READWRITE(tx);
        READWRITE(hashBlock);
        READWRITE(vMerkleBranch);
        READWRITE(nIndex);
    }

    void SetMerkleBranch(const CBlockIndex* pIndex, int posInBlock);

    /**
     * Return depth of transaction in blockchain:
     * <0  : conflicts with a transaction this deep in the blockchain
     *  0  : in memory pool, waiting to be included in a block
     * >=1 : this many blocks deep in the main chain
     */
    int GetDepthInMainChain() const;
    bool IsInMainChain() const { return GetDepthInMainChain() > 0; }
    int GetBlocksToMaturity() const;
    /**
     * Return height of transaction in blockchain. If in mempool returs MEMPOOL_HEIGHT
     */
    int32_t GetHeightInMainChain() const;
    /**
     * Return is the transaction height larger or equal to the given protocol activation
     * height. If in mempool, we assume that it will be mined in next block.
     */
    bool IsProtocolActive(ProtocolName name) const;
    /**
     * Get protocol activation state for the transaction.
     * If in mempool, we assume that it will be mined in next block.
     */
    ProtocolEra GetProtocolEra() const;

    /**
     * Pass this transaction to the mempool. Fails if absolute fee exceeds
     * absurd fee.
     */
    bool SubmitTxToMempool(const Amount nAbsurdFee, CValidationState& state);
    bool hashUnset() const { return (hashBlock.IsNull() || hashBlock == ABANDON_HASH); }
    bool isAbandoned() const { return (hashBlock == ABANDON_HASH); }
    void setAbandoned() { hashBlock = ABANDON_HASH; }

    const TxId GetId() const { return tx->GetId(); }
    bool IsCoinBase() const { return tx->IsCoinBase(); }
};
