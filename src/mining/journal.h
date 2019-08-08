// Copyright (c) 2019 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include <enum_cast.h>
#include <mining/journal_entry.h>

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/identity.hpp>
#include <boost/multi_index/ordered_index.hpp>

#include <memory>
#include <mutex>
#include <ostream>

namespace mining
{

class CJournalChangeSet;
class CJournalTester;

/**
* A class that tracks changes to the mempool and by association changes to the
* next mining candidate.
*
* Transactions to be included in the next mining candidate can be fetched by
* simply replaying the journal.
*/
class CJournal final
{
    // Make the tester our friend so it can inspect us properly
    friend class CJournalTester;

  public:

    // Construction/destruction
    CJournal() = default;
    ~CJournal() = default;

    CJournal(const CJournal&);

    CJournal& operator=(const CJournal&) = delete;
    CJournal(CJournal&&) = delete;
    CJournal& operator=(CJournal&&) = delete;

    // Get size of journal
    size_t size() const;

    // Apply changes to the journal
    void applyChanges(const CJournalChangeSet& changeSet);

  private:

    // Protect our data structures
    mutable std::mutex mMtx {};

    // Compare journal entries
    struct EntrySorter
    {
        bool operator()(const CJournalEntry& entry1, const CJournalEntry& entry2) const
        {
            return entry1.getTxn()->GetId() < entry2.getTxn()->GetId();
        }
    };

    // The journal itself is a multi-index of transactions and the order they
    // should be read/replayed from the journal.
    using TransactionList = boost::multi_index_container<
        CJournalEntry,
        boost::multi_index::indexed_by<
            // Unique transaction
            boost::multi_index::ordered_unique<
                boost::multi_index::identity<CJournalEntry>,
                EntrySorter
            >,
            // Order of replay
            boost::multi_index::sequenced<>
        >
    >;
    TransactionList mTransactions {};

};

using CJournalPtr = std::shared_ptr<CJournal>;


/**
* A class to aid testing of the journal, so that we don't have to expose lots
* of testing methods on the journal itself.
*/
class CJournalTester final
{
  public:

    CJournalTester(const CJournalPtr& journal) : mJournal{journal} {}

    // Update journal we track
    void updateJournal(const CJournalPtr& journal) { mJournal = journal; }

    // Get size of journal
    size_t journalSize() const;

    // Check the given transaction exists in the journal
    bool checkTxnExists(const CJournalEntry& txn) const;

    // Enumeration for txn order checking
    enum class TxnOrder { UNKNOWN, BEFORE, AFTER, NOTFOUND, DUPLICATE };

    // Report on the relative ordering within the journal of txn1 compared to txn2.
    // If txn1 comes first it will return BEFORE, if txn1 comes later it will return AFTER,
    // if either txn1 or txn2 are not found it will return NOTFOUND,
    // if txn1 and txn2 are the same it will return DUPLICATE.
    TxnOrder checkTxnOrdering(const CJournalEntry& txn1, const CJournalEntry& txn2) const;

    // Dump out the contents of the journal
    void dumpJournalContents(std::ostream& str) const;

  private:

    // Journal we want to check
    CJournalPtr mJournal {nullptr};
};

/// Enable enum_cast of TxnOrder for testing and debugging
const enumTableT<CJournalTester::TxnOrder>& enumTable(CJournalTester::TxnOrder);

}
