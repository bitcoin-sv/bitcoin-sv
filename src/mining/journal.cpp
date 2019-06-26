// Copyright (c) 2019 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include <mining/journal.h>
#include <mining/journal_change_set.h>
#include <utiltime.h>

using mining::CJournal;
using mining::CJournalTester;
using mining::CJournalChangeSet;
using mining::CJournalEntry;

// Copy constructor, only required by journal builder
CJournal::CJournal(const CJournal& that)
{
    // Lock journal we are copying from, and copy its contents
    std::unique_lock<std::mutex> lock { that.mMtx };
    mTransactions = that.mTransactions;
}

// Get size of journal
size_t CJournal::size() const
{
    std::lock_guard<std::mutex> lock { mMtx };
    return mTransactions.size();
}

// Apply changes to the journal
void CJournal::applyChanges(const CJournalChangeSet& changeSet)
{
    std::lock_guard<std::mutex> lock { mMtx };

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
        }
    }

    // Do we need to invalidate any observers after this change?
    if(!changeSet.getSimple())
    {
        mInvalidatingTime = GetTimeMicros();
    }
}

// Get start index for our underlying sequence
CJournal::Index CJournal::begin() const
{
    return Index { this, index<1>().begin() };
}

// Get end index for our underlying sequence
CJournal::Index CJournal::end() const
{
    return Index { this, index<1>().end() };
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
    return { mJournal && mValidTime > mJournal->getLastInvalidatingTime() };
}

// Increment
CJournal::Index& CJournal::Index::operator++()
{
    // Set previous to current, and move on current
    mPrevItem = mCurrItem++;
    return *this;
}

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


/** Journal Tester **/

// Get size of journal
size_t CJournalTester::journalSize() const
{
    return mJournal->size();
}

// Check the given transaction exists in the journal
bool CJournalTester::checkTxnExists(const CJournalEntry& txn) const
{
    // Lock journal while we probe it
    std::lock_guard<std::mutex> lock { mJournal->mMtx };

    // Get view on index 0, which is based on unique Id
    const auto& index { mJournal->index<0>() };

    // Lookup requested txn Id
    return (index.count(txn) > 0);
}

// Report on the relative ordering within the journal of txn1 compared to txn2.
CJournalTester::TxnOrder CJournalTester::checkTxnOrdering(const CJournalEntry& txn1, const CJournalEntry& txn2) const
{
    // Lock journal while we probe it
    std::lock_guard<std::mutex> lock { mJournal->mMtx };

    // Get view on index 0, which is based on unique Id
    const auto& index0 { mJournal->index<0>() };

    // Lookup txn1 and txn2
    auto it1 { index0.find(txn1) };
    auto it2 { index0.find(txn2) };
    if(it1 == index0.end() || it2 == index0.end())
    {
        return CJournalTester::TxnOrder::NOTFOUND;
    }
    else if(it1 == it2)
    {
        return CJournalTester::TxnOrder::DUPLICATE;
    }

    // Project onto index 1 to find them in the ordered view
    auto orderedIt1 { mJournal->mTransactions.project<1>(it1) };
    auto orderedIt2 { mJournal->mTransactions.project<1>(it2) };

    // Now iterate from the start of the ordered view and see which one we hit first. Without
    // random access iterators this is the best we can do.
    const auto& index1 { mJournal->index<1>() };
    for(auto it = index1.begin(); it != index1.end(); ++it)
    {
        if(it == orderedIt1)
        {
            return CJournalTester::TxnOrder::BEFORE;
        }
        else if(it == orderedIt2)
        {
            return CJournalTester::TxnOrder::AFTER;
        }
    }

    // Should never happen
    return CJournalTester::TxnOrder::UNKNOWN;
}

// Dump out the contents of the journal
void CJournalTester::dumpJournalContents(std::ostream& str) const
{
    // Lock journal while we probe it
    std::lock_guard<std::mutex> lock { mJournal->mMtx };

    // Get view on index 1, which is based on ordering
    const auto& index { mJournal->index<1>() };

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
        { CJournalTester::TxnOrder::UNKNOWN,   "UNKNOWN" },
        { CJournalTester::TxnOrder::BEFORE,    "BEFORE" },
        { CJournalTester::TxnOrder::AFTER,     "AFTER" },
        { CJournalTester::TxnOrder::NOTFOUND,  "NOTFOUND" },
        { CJournalTester::TxnOrder::DUPLICATE, "DUPLICATE" }
    };
    return table;
}

