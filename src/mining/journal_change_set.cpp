// Copyright (c) 2019 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include <mining/journal_builder.h>
#include <mining/journal_change_set.h>
#include <txmempool.h>
#include <validation.h>

using mining::CJournalEntry;
using mining::CJournalChangeSet;
using mining::JournalUpdateReason;
using mining::CJournalBuilder;

// Enable enum_cast for JournalUpdateReason, so we can log informatively
const enumTableT<JournalUpdateReason>& mining::enumTable(JournalUpdateReason)
{
    static enumTableT<JournalUpdateReason> table
    {   
        { JournalUpdateReason::UNKNOWN,     "UNKNOWN" },
        { JournalUpdateReason::NEW_TXN,     "NEW_TXN" },
        { JournalUpdateReason::REMOVE_TXN,  "REMOVE_TXN" },
        { JournalUpdateReason::REPLACE_TXN, "REPLACE_TXN" },
        { JournalUpdateReason::NEW_BLOCK,   "NEW_BLOCK" },
        { JournalUpdateReason::REORG,       "REORG" },
        { JournalUpdateReason::INIT,        "INIT" },
        { JournalUpdateReason::RESET,       "RESET" }
    };
    return table;
}

// Constructor
CJournalChangeSet::CJournalChangeSet(CJournalBuilder& builder, JournalUpdateReason reason)
: mBuilder{builder}, mUpdateReason{reason}
{
    // Reorgs can remove as well as add, and they add to the front not the tail
    if(mUpdateReason == JournalUpdateReason::REORG)
    {
        mTailAppendOnly = false;
    }
}

// RAII like destructor. Ensures that once finished with, this journal change
// set gets applied to the current journal even in the case of exceptions
// and other error return paths from the creator of the change set.
CJournalChangeSet::~CJournalChangeSet()
{
    apply();
}

// Add a new operation to the set
void CJournalChangeSet::addOperation(Operation op, CJournalEntry&& txn)
{
    std::unique_lock<std::mutex> lock { mMtx };
    mChangeSet.emplace_back(op, std::move(txn));
    addOperationCommon(op);
}

void CJournalChangeSet::addOperation(Operation op, const CJournalEntry& txn)
{
    std::unique_lock<std::mutex> lock { mMtx };
    mChangeSet.emplace_back(op, txn);
    addOperationCommon(op);
}

// Is our reason for the update a basic one? By "basic", we mean a change that
// can be applied immediately to the journal without having to wait fo the full
// change set to be compiled. So, NEW_TXN and INIT for example are basic, whereas
// NEW_BLOCK and REORG are not.
bool CJournalChangeSet::isUpdateReasonBasic() const
{
    switch(mUpdateReason)
    {
        case(JournalUpdateReason::NEW_BLOCK):
        case(JournalUpdateReason::REORG):
        case(JournalUpdateReason::RESET):
            return false;
        default:
            return true;
    }
}

// Apply our changes to the journal
void CJournalChangeSet::apply()
{
    std::unique_lock<std::mutex> lock { mMtx };
    applyNL();
}

// Clear the changeset without applying it
void CJournalChangeSet::clear()
{
    std::unique_lock<std::mutex> lock { mMtx };
    mChangeSet.clear();
}

// Apply our changes to the journal - Caller holds mutex
void CJournalChangeSet::applyNL()
{
    if(!mChangeSet.empty())
    {
        // There are a lot of corner cases that can happen with REORG change sets,
        // particularly to do with error handling, that can result in surprising
        // ordering of the transactions within the change set. Rather than trying
        // to handle them all individually just do a sort of the change set to
        // ensure it is always right.
        //
        // Also sort RESET change sets here because they have been created directly
        // from the mempool with no attempt to put things in the correct order.
        if(mUpdateReason == JournalUpdateReason::REORG || mUpdateReason == JournalUpdateReason::RESET)
        {
            // FIXME: Once C++17 parallel algorithms are widely supported, make this
            // use them.
            std::stable_sort(mChangeSet.begin(), mChangeSet.end(),
                [](const Change& change1, const Change& change2)
                {
                    const AncestorDescendantCountsPtr& count1 { change1.second.getAncestorCount() };
                    const AncestorDescendantCountsPtr& count2 { change2.second.getAncestorCount() };
                    return count1->nCountWithAncestors < count2->nCountWithAncestors;
                }
            );
        }

        mBuilder.applyChangeSet(*this);

        // Make sure we don't get applied again if we are later called by the destructor
        mChangeSet.clear();
    }
}


// Common post operation addition steps - caller holds mutex
void CJournalChangeSet::addOperationCommon(Operation op)
{
    // If this was a remove operation then we're no longer a simply appending
    if(op != Operation::ADD)
    {
        mTailAppendOnly = false;
    }

    // If it's safe to do so, immediately apply this change to the journal
    if(isUpdateReasonBasic())
    {
        applyNL();
    }
}

