// Copyright (c) 2019 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include <txmempool.h>

namespace mining
{

/**
* What we actually store for each journal entry.
* Contains a pointer to the transaction itself, group id and fee accounting details.
*/
class CJournalEntry
{
  public:

    // Constructors
    CJournalEntry(const CTransactionRef& txn, const Amount& fee, GroupID groupId)
    : mTxn{txn}, mFee{fee}, mGroupId{groupId}
    {}

    CJournalEntry(const CTxMemPoolEntry& entry)
    : CJournalEntry{entry.GetSharedTx(), entry.GetFee(), entry.GetCPFPGroupId()}
    {}

    // Accessors
    const CTransactionRef& getTxn() const { return mTxn; }
    const Amount& getFee() const { return mFee; }

    // Which group of transactions if any does this entry belong to
    const GroupID& getGroupId() const { return mGroupId; }

    bool isPaying() const { return !mGroupId || mGroupId == mTxn->GetId(); }

  private:

    // Shared pointer to the transaction itself
    CTransactionRef mTxn {};

    // Fee for the transaction
    Amount mFee {0};

    // Group id for the transaction
    GroupID mGroupId {};
};

}
