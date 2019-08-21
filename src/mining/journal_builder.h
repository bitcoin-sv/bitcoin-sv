// Copyright (c) 2019 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include <memory>
#include <shared_mutex>

namespace mining
{

// Required forward declarations
class CJournal;
class CJournalChangeSet;
enum class JournalUpdateReason;
using CJournalPtr = std::shared_ptr<CJournal>;
using CJournalChangeSetPtr = std::unique_ptr<CJournalChangeSet>;

/**
* A class to handle building & updating mining journals.
*/
class CJournalBuilder final
{
    // Make CJournalChangeSet a friend so that it, and only it, can call applyChangeSet()
    friend class CJournalChangeSet;

  public:

    // Construction/destruction
    CJournalBuilder() = default;
    ~CJournalBuilder() = default;
    CJournalBuilder(const CJournalBuilder&) = delete;
    CJournalBuilder(CJournalBuilder&&) = delete;
    CJournalBuilder& operator=(const CJournalBuilder&) = delete;
    CJournalBuilder& operator=(CJournalBuilder&&) = delete;

    // Fetch a new empty change set
    CJournalChangeSetPtr getNewChangeSet(JournalUpdateReason updateReason);

    // Get our current journal
    CJournalPtr getCurrentJournal() const;

    // Clear the current journal
    void clearJournal();

  private:

    // Apply a change set
    void applyChangeSet(const CJournalChangeSet& changeSet);

    // Clear the current journal - caller holds mutex
    void clearJournalUnlocked();

    // Protect our data
    mutable std::shared_mutex mMtx {};

    // The current active journal
    CJournalPtr mJournal { std::make_shared<CJournal>() };

};

using CJournalBuilderPtr = std::unique_ptr<CJournalBuilder>;

}
