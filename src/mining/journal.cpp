// Copyright (c) 2019 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include <mining/journal.h>
#include <mining/journal_change_set.h>
#include <utiltime.h>
#include <logging.h>

using mining::CJournal;
using mining::CJournalTester;
using mining::CJournalChangeSet;
using mining::CJournalEntry;
using mining::CJournalPtr;

// Copy constructor, only required by journal builder
CJournal::CJournal(const CJournal& that)
{
    // Lock journal we are copying from, and copy its contents
    std::shared_lock lock { that.mMtx };
    mTransactions = that.mTransactions;
}

// Get size of journal
size_t CJournal::size() const
{
    std::shared_lock lock { mMtx };
    return mTransactions.size();
}

// Apply changes to the journal
void CJournal::applyChanges(const CJournalChangeSet& changeSet)
{
    std::unique_lock lock { mMtx };

    using TransactionListByName = TransactionList::nth_index<0>::type;
    using TransactionListByPosition = TransactionList::nth_index<1>::type;
    TransactionListByName& index0 { mTransactions.get<0>() };
    TransactionListByPosition& index1 { mTransactions.get<1>() };

    // For REORGs we need to remember the current start position
    TransactionListByPosition::const_iterator begin1 {};
    TransactionListByName::const_iterator begin0 {};
    bool isReorg { changeSet.getUpdateReason() == JournalUpdateReason::REORG };
    if(isReorg)
    {
        begin1 = index1.begin();
        begin0 = mTransactions.project<0>(begin1);
    }

    for(const auto& [ op, txn ] : changeSet.getChangeSet())
    {
        if(op == CJournalChangeSet::Operation::ADD)
        {
            // Reorgs need to be added to the start of the journal, other reasons add to the end
            if(isReorg)
            {
                index1.emplace(begin1, txn);
            }
            else
            {
                index1.emplace_back(txn);
            }
        }
        else if(op == CJournalChangeSet::Operation::REMOVE)
        {
            // Lookup txn
            auto txnit { index0.find(txn) };
            if(txnit != index0.end())
            {
                // If this is a REORG and if we're erasing the first transaction in the journal
                // then we need to update our saved iterator to the start of the list.
                if(isReorg && txnit == begin0)
                {
                    ++begin1;
                    begin0 = mTransactions.project<0>(begin1);
                }

                // Remove txn
                index0.erase(txnit);
            }
            else
            {
                LogPrint(BCLog::JOURNAL, "ERROR: Failed to find and remove txn %s from journal\n",
                    txn.getTxn()->GetId().ToString().c_str());
            }
        }
    }

    // Do we need to invalidate any observers after this change?
    if(!changeSet.getTailAppendOnly())
    {
        mInvalidatingTime = GetTimeMicros();
    }
}

// Get start index for our underlying sequence
CJournal::Index CJournal::ReadLock::begin() const
{
    return Index { mJournal.get(), mJournal->index<1>().begin() };
}

// Get end index for our underlying sequence
CJournal::Index CJournal::ReadLock::end() const
{
    return Index { mJournal.get(), mJournal->index<1>().end() };
}


/** Journal Index **/

// Constructor
CJournal::Index::Index(const CJournal* journal, const Underlying& begin)
: mJournal{journal}, mValidTime{GetTimeMicros()}, mCurrItem{begin}
{
    // Work out what to do for previous item pointer
    if(mCurrItem == mJournal->index<1>().begin())
    {
        // Can't point before the start
        mPrevItem = mJournal->index<1>().end();
    }
    else if(mCurrItem == mJournal->index<1>().end())
    {
        if(!mJournal->index<1>().empty())
        {
            // Point 1 before the end
            mPrevItem = mJournal->index<1>().end();
            --mPrevItem;
        }
        else
        {
            mPrevItem = mJournal->index<1>().end();
        }
    }
    else
    {
        // Point 1 before current position
        mPrevItem = mCurrItem;
        --mPrevItem;
    }
}

// Are we still valid?
bool CJournal::Index::valid() const
{
    // We're valid if we were initialised after the last invalidating time
    return ( mJournal && mValidTime > mJournal->getLastInvalidatingTime() );
}

// Increment
CJournal::Index& CJournal::Index::operator++()
{
    // Set previous to current, and move on current
    mPrevItem = mCurrItem++;
    return *this;
}

// Reset the index to ensure it points to the next item. This needs to happen
// for example if the index had previously reached the end, and then some more
// items were subsequently added.
void CJournal::Index::reset()
{
    if(!valid())
    {
        // Can't reset once we're invalid
        throw std::runtime_error("Can't reset invalidated index");
    }

    if(mCurrItem == mJournal->index<1>().end())
    {
        if(mPrevItem != mJournal->index<1>().end())
        {
            Underlying prevNext { mPrevItem };
            ++prevNext;
            if(prevNext != mJournal->index<1>().end())
            {
                // New items have arrived, reset current pointer
                mCurrItem = prevNext;
            }
        }
        else if(!mJournal->index<1>().empty())
        {
            // Previously the journal must have been empty, but now items have
            // arrived. Reset current pointer.
            mCurrItem = mJournal->index<1>().begin();
        }
    }
}


/** Journal read lock **/

// Standard constructor
CJournal::ReadLock::ReadLock(const std::shared_ptr<CJournal>& journal)
: mJournal{journal}, mLock{mJournal->mMtx}
{
}

// Move constructor
CJournal::ReadLock::ReadLock(ReadLock&& that)
: mJournal{std::move(that.mJournal)}, mLock{std::move(that.mLock)}
{
}

// Move assignment
CJournal::ReadLock& CJournal::ReadLock::operator=(ReadLock&& that)
{
    if(this != &that)
    {
        // Need to make sure the old lock gets unlocked before the old journal
        // gets destroyed.
        mLock = std::move(that.mLock);
        mJournal = std::move(that.mJournal);
    }

    return *this;
}


/** Journal Tester **/

CJournalTester::CJournalTester(const CJournalPtr& journal)
{
    using TransactionListByPosition = TesterTransactionList::nth_index<1>::type;
    TransactionListByPosition& index1 { mTransactions.get<1>() };

    // Lock journal while we copy it
    std::shared_lock lock { journal->mMtx };

    // Rebuild the journal in our faster iterating (but slower updating) format.
    const auto& journalIndex { journal->index<1>() };
    for(auto it = journalIndex.begin(); it != journalIndex.end(); ++it)
    {
        index1.emplace_back(*it);
    }
}

// Get size of journal
size_t CJournalTester::journalSize() const
{
    return mTransactions.size();
}

// Check the given transaction exists in the journal
bool CJournalTester::checkTxnExists(const CJournalEntry& txn) const
{
    // Get view on index 0, which is based on unique Id
    const auto& index { mTransactions.get<0>() };

    // Lookup requested txn Id
    return (index.count(txn) > 0);
}

// Report on the relative ordering within the journal of txn1 compared to txn2.
CJournalTester::TxnOrder CJournalTester::checkTxnOrdering(const CJournalEntry& txn1, const CJournalEntry& txn2) const
{
    // Get view on index 0, which is based on unique Id
    const auto& index0 { mTransactions.get<0>() };

    // Lookup txn1 and txn2
    auto it1 { index0.find(txn1) };
    auto it2 { index0.find(txn2) };
    if(it1 == index0.end() || it2 == index0.end())
    {
        return CJournalTester::TxnOrder::NOTFOUND;
    }
    else if(it1 == it2)
    {
        return CJournalTester::TxnOrder::DUPLICATETX;
    }

    // Project onto index 1 to find them in the ordered view
    auto orderedIt1 { mTransactions.project<1>(it1) };
    auto orderedIt2 { mTransactions.project<1>(it2) };

    // Work out which comes first
    if(std::distance(orderedIt1, orderedIt2) > 0)
    {
        return CJournalTester::TxnOrder::BEFORE;
    }
    else
    {
        return CJournalTester::TxnOrder::AFTER;
    }
}

// Dump out the contents of the journal
void CJournalTester::dumpJournalContents(std::ostream& str) const
{
    // Get view on index 1, which is based on ordering
    const auto& index { mTransactions.get<1>() };

    // Dump out the contents
    for(const auto& txn : index)
    {
        str << txn.getTxn()->GetId().ToString() << std::endl;
    }
}

const enumTableT<CJournalTester::TxnOrder>& mining::enumTable(CJournalTester::TxnOrder)
{   
    static enumTableT<CJournalTester::TxnOrder> table
    {   
        { CJournalTester::TxnOrder::UNKNOWN,     "UNKNOWN" },
        { CJournalTester::TxnOrder::BEFORE,      "BEFORE" },
        { CJournalTester::TxnOrder::AFTER,       "AFTER" },
        { CJournalTester::TxnOrder::NOTFOUND,    "NOTFOUND" },
        { CJournalTester::TxnOrder::DUPLICATETX, "DUPLICATETX" }
    };
    return table;
}

