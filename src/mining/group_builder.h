// Copyright (c) 2022 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include "mining/journal_entry.h"
#include "primitives/transaction.h"

#include <optional>
#include <unordered_map>
#include <vector>

class Config;

namespace mining
{

// Transaction group ID
using TxnGroupID = uint64_t;

/**
* A group of transactions that should all be added to a block template
* together or none at all.
*/
class TxnGroup
{
  public:
    TxnGroup(TxnGroupID id) : mID{id} {}
    TxnGroup(TxnGroupID id, const CJournalEntry& txn);

    // Add single txn
    void AddTxn(const CJournalEntry& txn);
    // Move a group of txns
    void AddGroup(TxnGroup&& group);

    // Accessors
    [[nodiscard]] TxnGroupID GetID() const noexcept { return mID; }
    [[nodiscard]] auto begin() const noexcept { return mTxns.cbegin(); }
    [[nodiscard]] auto end() const noexcept { return mTxns.cend(); }
    [[nodiscard]] size_t size() const noexcept { return mTxns.size(); }

    // Check if this group contains only selfish transactions
    [[nodiscard]] bool IsSelfish(const Config& config) const;

  private:

    // ID for this group
    TxnGroupID mID {0};

    // Ordered list of txn journal entries in the group
    std::vector<CJournalEntry> mTxns {};
};

/**
* Build and manage transaction groups.
*
* Transactions are either grouped explictly by the caller, or are grouped
* together where they have a parent / child spending relationship with other
* managed transactions.
*/
class TxnGroupBuilder final
{
  public:

    /**
     * Add a txn and its journal entry into a group.
     *
     * If the txn doesn't spend any outputs from other txns managed in other
     * groups then it gets placed in its own group.
     *
     * If it does spend outputs from other txns managed in other groups then
     * it gets placed in the same group as those other transactions, possibly
     * combining several groups together.
     *
     * The caller can optionally tell us which group to place the txn in, even
     * if the txn doesn't depend on (spend) a txn from that group.
     *
     * Returns the ID for the group the transaction is placed in.
     */
    TxnGroupID AddTxn(const CJournalEntry& journalEntry,
                      std::optional<TxnGroupID> txnGroup = std::nullopt);

    // Lookup and return the specified group. Throws if not found.
    [[nodiscard]] const TxnGroup& GetGroup(TxnGroupID groupID) const;

    // Remove the specified group and all its transactions. Throws if not found.
    void RemoveGroup(TxnGroupID groupID);

    // Clear and reset
    void Clear();

    // Unit test access
    template<typename T> struct UnitTestAccess;

  private:

    // Move members of an old group to a new group
    void MoveGroup(TxnGroup& newGroup, TxnGroup&& oldGroup);

    // Fetch new group ID
    TxnGroupID NewGroupID();

    // Map of transaction IDs managed here and the groups they are in
    std::unordered_map<TxId, TxnGroupID> mTxnMap {};

    // Map of groups managed here
    std::unordered_map<TxnGroupID, TxnGroup> mGroupMap {};

    // Next free group ID
    TxnGroupID mNextGroupID {0};

};

}   // namespace mining

