// Copyright (c) 2019 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include <enum_cast.h>
#include <mining/journal_entry.h>

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/identity.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>

#include <atomic>
#include <memory>
#include <ostream>
#include <shared_mutex>

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

    // Get time we were last updated by an invalidating change
    int64_t getLastInvalidatingTime() const { return mInvalidatingTime; }

    // Get/set whether we are still the current best journal
    bool getCurrent() const { return mCurrent; }
    void setCurrent(bool current) { mCurrent = current; }

    // Apply changes to the journal
    void applyChanges(const CJournalChangeSet& changeSet);

  private:

    // Protect our data structures
    mutable std::shared_mutex mMtx {};

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

    // Convenience accessor to fetch the given multi-index index
    template<int I>
    const typename TransactionList::nth_index<I>::type& index() const
    {
        return mTransactions.get<I>();
    }

    // Time of last invalidating change
    std::atomic_int64_t mInvalidatingTime {0};

    // Are we still current?
    std::atomic_bool mCurrent {true};

  public:

    // An index into our transaction list to read them in sequence and check
    // whether our position in the sequence can still be considered valid.
    // Indexes provide the funtionality we need that is missing from the
    // (non random-access) iterators provided by the underlying boost
    // multi-index container.
    //
    // NOTE: It is only safe to test/read/update an Index while the journal
    // it came from is locked by holding a ReadLock (see below).
    class Index
    {
        using Underlying = TransactionList::nth_index<1>::type::const_iterator;

      public:
        Index() = default;
        Index(const CJournal* journal, const Underlying& begin);

        bool valid() const;
        const CJournalEntry& at() const { return *mCurrItem; }
        void reset();

        Index& operator++();
        bool operator==(const Index& that) const { return (mCurrItem == that.mCurrItem); }
        bool operator!=(const Index& that) const { return !(*this == that); }

      private:

        const CJournal* mJournal {nullptr};
        int64_t mValidTime       {-1};
        Underlying mCurrItem     {};
        Underlying mPrevItem     {};
    };

    // An RAII wrapper for holding a read lock on the journal
    class ReadLock final
    {
      public:
        ReadLock() = default;
        ReadLock(const std::shared_ptr<CJournal>& journal);
        ~ReadLock() = default;

        ReadLock(const ReadLock&) = delete;
        ReadLock& operator=(const ReadLock&) = delete;

        // NOLINTNEXTLINE(cppcoreguidelines-noexcept-move-operations, performance-noexcept-move-constructor)
        ReadLock(ReadLock&& that);
        // NOLINTNEXTLINE(cppcoreguidelines-noexcept-move-operations, performance-noexcept-move-constructor)
        ReadLock& operator=(ReadLock&& that);

        // Get start/end indexes for our underlying sequence
        Index begin() const;
        Index end() const;

      private:
        // Order of declaration is important; we need the lock to be destroyed
        // and the mutex unlocked before the journal that owns it.
        std::shared_ptr<CJournal> mJournal {};
        std::shared_lock<std::shared_mutex> mLock {};
    };

};

using CJournalPtr = std::shared_ptr<CJournal>;


/**
* A class to aid testing of the journal, so that we don't have to expose lots
* of testing methods on the journal itself.
*/
class CJournalTester final
{
  public:

    CJournalTester(const CJournalPtr& journal);

    // Get size of journal
    size_t journalSize() const;

    // Check the given transaction exists in the journal
    bool checkTxnExists(const CJournalEntry& txn) const;

    // Enumeration for txn order checking
    enum class TxnOrder { UNKNOWN, BEFORE, AFTER, NOTFOUND, DUPLICATETX };

    // Report on the relative ordering within the journal of txn1 compared to txn2.
    // If txn1 comes first it will return BEFORE, if txn1 comes later it will return AFTER,
    // if either txn1 or txn2 are not found it will return NOTFOUND,
    // if txn1 and txn2 are the same it will return DUPLICATETX.
    TxnOrder checkTxnOrdering(const CJournalEntry& txn1, const CJournalEntry& txn2) const;

    // Dump out the contents of the journal
    void dumpJournalContents(std::ostream& str) const;

    // Get journal content
    std::set<TxId> getContents() const;

  private:

    // For speed of checking we need to rebuild the journal using a random access
    // ordered index.
    using TesterTransactionList = boost::multi_index_container<
        CJournalEntry,
        boost::multi_index::indexed_by<
            // Unique transaction
            boost::multi_index::ordered_unique<
                boost::multi_index::identity<CJournalEntry>,
                CJournal::EntrySorter
            >,
            // Order of replay
            boost::multi_index::random_access<>
        >
    >;
    TesterTransactionList mTransactions {};
};

/// Enable enum_cast of TxnOrder for testing and debugging
const enumTableT<CJournalTester::TxnOrder>& enumTable(CJournalTester::TxnOrder);

}
