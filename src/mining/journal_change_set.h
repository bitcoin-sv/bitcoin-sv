// Copyright (c) 2019 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include <enum_cast.h>
#include <mining/journal.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace mining
{

class CJournalBuilder;

/// Enumerate possible reasons for changes to the journal
enum class JournalUpdateReason
{
    UNKNOWN,
    NEW_TXN,
    REMOVE_TXN,
    REPLACE_TXN,
    NEW_BLOCK,
    REORG,
    INIT,
    RESET,
    PRIORITISATION
};
/// Enable enum_cast for JournalUpdateReason, so we can log informatively
const enumTableT<JournalUpdateReason>& enumTable(JournalUpdateReason);

/**
* A class for recording a set of changes to make to a journal.
*
* Changes to the journal may need to be applied as a set (like a database
* transaction) to ensure the journal accurately reflects the state
* of the mempool at all times without having "intermediate" states.
*/
// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions)
class CJournalChangeSet final
{
  public:

    // Constructor
    CJournalChangeSet(CJournalBuilder& builder, JournalUpdateReason reason);

    // RAII like destructor
    ~CJournalChangeSet();

    // An individual operation can either add or remove a transaction
    enum class Operation { ADD, REMOVE };

    // Types to support the change set
    using Change = std::pair<Operation, CJournalEntry>;
    using ChangeSet = std::vector<Change>;

    // Add a new operation to the set
    void addOperation(Operation op, CJournalEntry&& txn);
    void addOperation(Operation op, const CJournalEntry& txn);

    // Update ourselves to be for a reorg
    void updateForReorg() { mUpdateReason = JournalUpdateReason::REORG; }

    // Get why we were created
    JournalUpdateReason getUpdateReason() const { return mUpdateReason; }

    // Is our reason for the update a basic one?
    bool isUpdateReasonBasic() const;

    // Get reference to our change set
    const ChangeSet& getChangeSet() const { return mChangeSet; }

    // Is this a simple tail additative only change set?
    bool getTailAppendOnly() const { return mTailAppendOnly; }

    // Apply our changes to the journal
    void apply();

    // Clear the changeset without applying it
    void clear();

    // Check that changeset is topologically sorted
    bool CheckTopoSort() const;

  private:

    // Common post operation addition steps
    void addOperationCommon(Operation op);

    // Apply our changes (caller holds mutex)
    void applyNL();

    mutable std::mutex mMtx {};

    // Reference to the journal builder
    CJournalBuilder& mBuilder;

    // Reason we exist
    std::atomic<JournalUpdateReason> mUpdateReason { JournalUpdateReason::UNKNOWN };

    // The set of operations to apply
    ChangeSet mChangeSet {};

    // Is this change set a simple one that just appends to the end, or is a more
    // complicated one that removes as well or does something else complicated?
    std::atomic_bool mTailAppendOnly {true};

};

using CJournalChangeSetPtr = std::unique_ptr<CJournalChangeSet>;

}
