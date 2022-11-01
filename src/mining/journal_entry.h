// Copyright (c) 2019 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include "tx_mempool_info.h"

class CTxMemPoolEntry;

namespace mining
{

/**
 * \class GroupID
 *
 * GroupID identifies consecutive transactions in the journal that belong to
 * the same CPFP group that should all be mined in the same block.
 *
 * The block assembler should not accept a partial group into the block template.
 */
using GroupID = std::optional<uint64_t>;

/**
* What we actually store for each journal entry.
* Contains a pointer to the transaction itself, group id and fee accounting details.
*/
class CJournalEntry
{
  public:

    // Constructor
    explicit CJournalEntry(const CTxMemPoolEntry& entry);

    // Accessors
    const CTransactionWrapperRef& getTxn() const { return mTxn; }
    uint64_t getTxnSize() const { return mTxnSize; }
    const Amount& getFee() const { return mFee; }
    int64_t getTime() const { return mTime; }

    // Which group of transactions if any does this entry belong to
    const GroupID& getGroupId() const { return mGroupId; }

    // Is this the paying transaction of its group (if any)
    bool isPaying() const { return isCpfpPayingTx; }

    // Unit test access
    template<typename T> struct UnitTestAccess;

  private:

    // Delegated constructor and used in unit tests
    CJournalEntry(const CTransactionWrapperRef& txn,
                  uint64_t txnSize,
                  const Amount& fee,
                  int64_t time,
                  GroupID groupId,
                  bool isCpfpGroupPayingTx);

    // Shared pointer to the transaction wrapper
    CTransactionWrapperRef mTxn {};

    // Transaction size.
    uint64_t mTxnSize;

    // Fee for the transaction
    Amount mFee {0};

    // Time txn was seen
    int64_t mTime {0};

    // Group id for the transaction
    GroupID mGroupId {};

    // is groups paying transaction
    bool isCpfpPayingTx;
};

}
