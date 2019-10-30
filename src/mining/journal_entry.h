// Copyright (c) 2019 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include <txmempool.h>

namespace mining
{

/**
* What we actually store for each journal entry.
* Contains a pointer to the transaction itself, ancestor count information,
* and fee/sigops accounting details.
*/
class CJournalEntry
{
  public:

    // Constructors
    CJournalEntry(const CTransactionRef& txn, const AncestorDescendantCountsPtr& count,
                  const Amount& fee, int64_t sigOps)
    : mTxn{txn}, mAncestorCount{count}, mFee{fee}, mSigOpsCount{sigOps}
    {}

    CJournalEntry(const CTxMemPoolEntry& entry)
    : CJournalEntry{entry.GetSharedTx(), entry.GetAncestorDescendantCounts(), entry.GetFee(),
                    entry.GetSigOpCount()}
    {}

    // Accessors
    const CTransactionRef& getTxn() const { return mTxn; }
    const AncestorDescendantCountsPtr& getAncestorCount() const { return mAncestorCount; }
    const Amount& getFee() const { return mFee; }
    int64_t getSigOpsCount() const { return mSigOpsCount; }

  private:

    // Shared pointer to the transaction itself
    CTransactionRef mTxn {};

    // Shared pointer to the ancestor count information
    AncestorDescendantCountsPtr mAncestorCount {};

    // Fee and sig ops count for the transaction
    Amount mFee {0};
    int64_t mSigOpsCount {0};
};

}
