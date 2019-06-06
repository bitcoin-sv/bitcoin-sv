// Copyright (c) 2019 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include <txmempool.h>

namespace mining
{

/**
* What we actually store for each journal entry.
* Contains a pointer to the transaction itself, plus ancestor count information.
*/
class CJournalEntry
{
  public:

    // Constructor
    CJournalEntry(const CTransactionRef& txn, const AncestorDescendantCountsPtr& count)
    : mTxn{txn}, mAncestorCount{count}
    {}

    // Accessors
    const CTransactionRef& getTxn() const { return mTxn; }
    const AncestorDescendantCountsPtr& getAncestorCount() const { return mAncestorCount; }

  private:

    // Shared pointer to the transaction itself
    CTransactionRef mTxn {};

    // Shared pointer to the ancestor count information
    AncestorDescendantCountsPtr mAncestorCount {};
};

}
