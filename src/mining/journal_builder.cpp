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

// Fetch a new empty change set
CJournalChangeSetPtr CJournalBuilder::getNewChangeSet(JournalUpdateReason updateReason)
{
    return std::make_shared<CJournalChangeSet>(*this, updateReason);
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
    mJournal = std::make_shared<CJournal>();
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

        std::unique_lock<std::shared_mutex> lock { mMtx };
        mJournal = std::make_shared<CJournal>(*mJournal);
    }

    // Don't log for every individual transaction, it'll swamp the log
    if(changeSet.getChangeSet().size() > 1)
    {
        LogPrint(BCLog::JOURNAL, "Journal builder applying change set size %d for %s\n",
            changeSet.getChangeSet().size(), enum_cast<std::string>(changeSet.getUpdateReason()).c_str());
    }

    // Pass changes down to journal for it to apply to itself
    std::shared_lock<std::shared_mutex> lock { mMtx };
    mJournal->applyChanges(changeSet);
}

