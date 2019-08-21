// Copyright (c) 2019 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include <mining/journal.h>
#include <mining/journal_builder.h>
#include <mining/journal_change_set.h>
#include <logging.h>

using mining::CJournalBuilder;
using mining::CJournalChangeSetPtr;
using mining::CJournalPtr;
using mining::JournalUpdateReason;
using mining::CJournalChangeSet;

// Fetch a new empty change set
CJournalChangeSetPtr CJournalBuilder::getNewChangeSet(JournalUpdateReason updateReason)
{
    return std::make_unique<CJournalChangeSet>(*this, updateReason);
}

// Get our current journal
CJournalPtr CJournalBuilder::getCurrentJournal() const
{
    std::shared_lock<std::shared_mutex> lock { mMtx };
    return mJournal;
}

// Clear the current journal
void CJournalBuilder::clearJournal()
{
    std::unique_lock<std::shared_mutex> lock { mMtx };
    clearJournalUnlocked();
}

// Apply a change set
void CJournalBuilder::applyChangeSet(const CJournalChangeSet& changeSet)
{
    // If the cause of this change is a new block arriving or a reorg, then
    // create a new journal based on the old journal. This is for no other
    // reason than to maintain the desired model of having journals linked
    // to blocks.
    JournalUpdateReason updateReason { changeSet.getUpdateReason() };
    if(updateReason == JournalUpdateReason::NEW_BLOCK || updateReason == JournalUpdateReason::REORG)
    {
        LogPrint(BCLog::JOURNAL, "Journal builder creating new journal for %s\n",
            enum_cast<std::string>(changeSet.getUpdateReason()).c_str());

        // Replace old journal
        std::unique_lock<std::shared_mutex> lock { mMtx };
        CJournalPtr oldJournal { mJournal };
        mJournal = std::make_shared<CJournal>(*oldJournal);
        oldJournal->setCurrent(false);
    }

    // Don't log for every individual transaction, it'll swamp the log
    if(changeSet.getChangeSet().size() > 1)
    {
        LogPrint(BCLog::JOURNAL, "Journal builder applying change set size %d for %s\n",
            changeSet.getChangeSet().size(), enum_cast<std::string>(changeSet.getUpdateReason()).c_str());
    }

    if(updateReason == JournalUpdateReason::RESET)
    {
        // RESET is both a clear and apply operation
        std::unique_lock<std::shared_mutex> lock { mMtx };
        clearJournalUnlocked();
        mJournal->applyChanges(changeSet);
    }
    else
    {
        // Pass changes down to journal for it to apply to itself
        std::shared_lock<std::shared_mutex> lock { mMtx };
        mJournal->applyChanges(changeSet);
    }
}

// Clear the current journal - caller holds mutex
void CJournalBuilder::clearJournalUnlocked()
{
    CJournalPtr oldJournal { mJournal };
    mJournal = std::make_shared<CJournal>();
    oldJournal->setCurrent(false);
}

