// Copyright (c) 2019 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "journal_entry.h"
#include "txmempool.h"

namespace mining
{
CJournalEntry::CJournalEntry(const CTransactionWrapperRef& txn,
                             uint64_t txnSize,
                             const Amount& fee,
                             GroupID groupId)
    : mTxn{txn},
      mTxnSize{txnSize},
      mFee{fee},
      mGroupId{groupId}
{}

CJournalEntry::CJournalEntry(const CTxMemPoolEntry& entry)
    : CJournalEntry{entry.tx,
                    entry.GetTxSize(),
                    entry.GetFee(),
                    entry.GetCPFPGroupId()}
{}
}
