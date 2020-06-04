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
    CJournalEntry(const CTransactionRef& txn, const AncestorCountsPtr& count,
                  const Amount& fee, int64_t sigOps, GroupID groupId)
    : mTxn{txn}, mAncestorCount{count}, mFee{fee}, mSigOpsCount{sigOps}
    , mGroupId(groupId)
    {}

    CJournalEntry(const CTxMemPoolEntry& entry)
    : CJournalEntry{entry.GetSharedTx(), entry.GetAncestorCounts(), entry.GetFee(),
                    entry.GetSigOpCount(), entry.GetCPFPGroupId()}
    {}

    // Accessors
    const CTransactionRef& getTxn() const { return mTxn; }
    const AncestorCountsPtr& getAncestorCount() const { return mAncestorCount; }
    const Amount& getFee() const { return mFee; }
    int64_t getSigOpsCount() const { return mSigOpsCount; }

    // Which group of transactions if any does this entry belong to
    const GroupID& getGroupId() const { return mGroupId; }

  private:

    // Shared pointer to the transaction itself
    CTransactionRef mTxn {};

    // Shared pointer to the ancestor count information
    AncestorCountsPtr mAncestorCount {};

    // Fee and sig ops count for the transaction
    Amount mFee {0};
    int64_t mSigOpsCount {0};

    GroupID mGroupId {};
};

}
