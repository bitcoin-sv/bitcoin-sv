// Copyright (c) 2019 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include <boost/iterator/filter_iterator.hpp>
#include <mining/journal_builder.h>
#include <mining/journal_change_set.h>
#include <txmempool.h>
#include <validation.h>

using mining::CJournalEntry;
using mining::CJournalChangeSet;
using mining::JournalUpdateReason;
using mining::CJournalBuilder;

namespace
{
    bool CheckTopoSort(
        CJournalChangeSet::ChangeSet&& changeSet,
        JournalUpdateReason updateReason)
    {
        auto filterAndSort = [&changeSet](auto predicate) {
            std::vector<uint256> transactionIds;
            transactionIds.reserve(changeSet.size());
            auto selections = boost::make_filter_iterator(predicate, changeSet.cbegin(), changeSet.cend());
            auto selectionsEnd =boost::make_filter_iterator(predicate, changeSet.cend(), changeSet.cend());
            std::transform(selections, selectionsEnd,
                           std::inserter(transactionIds, transactionIds.begin()),
                           [](auto& change) { return change.second.getTxn()->GetId(); });
            std::sort(transactionIds.begin(), transactionIds.end());
            return transactionIds;
        };

        auto isAddition = [](auto& change) { return change.first == CJournalChangeSet::Operation::ADD; };
        std::vector<uint256> addedTransactions = filterAndSort(isAddition);
        std::vector<uint256> removedTransactions = filterAndSort(
            [](auto& change) {return change.first == CJournalChangeSet::Operation::REMOVE; });

        std::unordered_set<uint256> laterTransactions;
        laterTransactions.reserve(addedTransactions.size());
        std::set_difference(addedTransactions.cbegin(), addedTransactions.cend(),
                            removedTransactions.cbegin(), removedTransactions.cend(),
                            std::inserter(laterTransactions, laterTransactions.begin()));

        const auto effectiveTransactionsSize = laterTransactions.size();

        bool sorted = true;

        for(auto i = changeSet.cbegin(); i != changeSet.cend(); ++i) {
            if (!isAddition(*i)) {
                continue;
            }
            // FIXME: We may read the transaction from disk, but for now
            //        this method is only called from CheckMempool().
            auto txn = i->second.getTxn()->GetTx();
            auto unsorted = std::find_if(txn->vin.cbegin(), txn->vin.cend(),
                                         [&laterTransactions](const CTxIn& txInput) {
                                             return laterTransactions.count(txInput.prevout.GetTxId());
                                         });
            // subsequent entries are allowed to see us
            laterTransactions.erase(txn->GetHash());

            if (unsorted != txn->vin.cend()) {
                if (sorted) {
                    LogPrintf("=x===== Toposort violation in ChangeSet %s with %d changes %d effective %d ADD %d REMOVE\n",
                              enum_cast<std::string>( updateReason ),
                              std::distance(changeSet.cbegin(), changeSet.cend()),
                              effectiveTransactionsSize,
                              addedTransactions.size(),
                              removedTransactions.size());
                    sorted = false;
                }
                uint256 prevTxId(unsorted->prevout.GetTxId());
                const auto prevTx = std::find_if(changeSet.cbegin(), changeSet.cend(), [&prevTxId](const auto &change) {
                    return change.second.getTxn()->GetId() == prevTxId;
                });
                size_t prevTxIdx = std::distance(changeSet.cbegin(), prevTx);
                LogPrintf("=x== ChangeSet[%d] %s input %d"
                          " references a later ChangeSet[%d] %s\n",
                          (std::distance(changeSet.cbegin(), i)),
                          txn->GetHash().GetHex(),
                          std::distance(txn->vin.cbegin(), unsorted),
                          prevTxIdx,
                          prevTxId.GetHex()
                          );
            }
        }

        return sorted;
    }
}

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
    std::scoped_lock lock{ mMtx };
    mChangeSet.emplace_back(op, std::move(txn));
    addOperationCommon(op);
}

void CJournalChangeSet::addOperation(Operation op, const CJournalEntry& txn)
{
    std::scoped_lock lock{ mMtx };
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
    std::scoped_lock lock{ mMtx };
    applyNL();
}

// Clear the changeset without applying it
void CJournalChangeSet::clear()
{
    std::scoped_lock lock{ mMtx };
    mChangeSet.clear();
}


// Try to disprove toposort by trying to find an ADD Change in the changeset
// that references another ADD transaction that appears later in the changeset.
bool CJournalChangeSet::CheckTopoSort() const
{
    ChangeSet changeSet;
    {
        std::scoped_lock lock{ mMtx };

        changeSet = mChangeSet;
    }

    return ::CheckTopoSort( std::move(changeSet), getUpdateReason() );
}

// Apply our changes to the journal - Caller holds mutex
void CJournalChangeSet::applyNL()
{
    if(!mChangeSet.empty())
    {
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
    if(isUpdateReasonBasic() && mChangeSet.back().second.isPaying())
    {
        applyNL();
    }
}

