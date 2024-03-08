// Copyright (c) 2022 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "mining/group_builder.h"

#include "config.h"

#include <stdexcept>
#include <unordered_set>

namespace mining
{

// TxnGroup constructor
TxnGroup::TxnGroup(TxnGroupID id, const CJournalEntry& txn)
: mID{id}
{
    AddTxn(txn);
}

// Add single txn
void TxnGroup::AddTxn(const CJournalEntry& txn)
{
    mTxns.push_back(txn);
}

// Move a group of txns
void TxnGroup::AddGroup(TxnGroup&& group)
{
    mTxns.insert(end(),
                 std::make_move_iterator(group.begin()),
                 std::make_move_iterator(group.end()));
}

// Check if this group contains only selfish transactions
bool TxnGroup::IsSelfish(const Config& config) const
{
    int64_t selfishCutoffTime { GetTime() - config.GetMinBlockMempoolTimeDifferenceSelfish() };

    return std::all_of(begin(), end(), [&](const auto& txn)
        { return txn.getTime() < selfishCutoffTime; }
    );
}

// Add a txn to a group
TxnGroupID TxnGroupBuilder::AddTxn(const CJournalEntry& journalEntry,
                                   std::optional<TxnGroupID> txnGroup)
{
    // Fetch txn from journal entry
    const CTransactionRef& txn { journalEntry.getTxn()->GetTx() };
    if(!txn)
    {
        throw std::runtime_error("TxnGroupBuilder failed to fetch txn from wrapper");
    }

    // Check we don't already know about this txn
    const TxId& txid { txn->GetId() };
    if(mTxnMap.count(txid) != 0)
    {
        throw std::runtime_error("TxnGroupBuilder TxId " + txid.ToString() + " already known");
    }

    // Does this txn spend outputs from any other txns we manage?
    std::unordered_set<TxnGroupID> groupSpends {};
    for(const CTxIn& in : txn->vin)
    {
        const auto& txit { mTxnMap.find(in.prevout.GetTxId()) };
        if(txit != mTxnMap.end())
        {
            groupSpends.insert(txit->second);
        }
    }

    // If the caller has told us which group to place this txn in,
    // add that group as another dependency.
    if(txnGroup)
    {
        groupSpends.insert(txnGroup.value());
    }

    // Store details for this txn
    if(! groupSpends.empty())
    {
        if(groupSpends.size() == 1)
        {
            // Add txn to existing group
            TxnGroupID groupID { *groupSpends.begin() };
            const auto& it { mGroupMap.find(groupID) };
            if(it != mGroupMap.end())
            {
                it->second.AddTxn(journalEntry);
            }
            else
            {
                throw std::runtime_error("TxnGroupBuilder unknown group ID " + std::to_string(groupID));
            }

            // Record new txn we're managing
            mTxnMap.emplace(txid, groupID);
            return groupID;
        }
        else
        {
            // groupSpends contains the IDs of all groups containing txns this
            // new txn spends (or otherwise depends on), so combine those groups
            // together and add this txn to the new combined group.
            TxnGroupID newGroupID { NewGroupID() };
            TxnGroup superGroup { newGroupID };
            for(TxnGroupID groupID : groupSpends)
            {
                const auto& it { mGroupMap.find(groupID) };
                if(it != mGroupMap.end())
                {
                    // Move group members from old group to new super group
                    MoveGroup(superGroup, std::move(it->second));
                    // Erase old group
                    mGroupMap.erase(it);
                }
                else
                {
                    throw std::runtime_error("TxnGroupBuilder unknown group ID " + std::to_string(groupID));
                }
            }
            superGroup.AddTxn(journalEntry);
            mGroupMap.emplace(newGroupID, std::move(superGroup));

            // Record new txn we're managing
            mTxnMap.emplace(txid, newGroupID);
            return newGroupID;
        }
    }
    else
    {
        // Create new group for this standalone txn
        TxnGroupID groupID { NewGroupID() };
        mGroupMap.emplace(groupID, TxnGroup { groupID, journalEntry });

        // Record new txn we're managing
        mTxnMap.emplace(txid, groupID);
        return groupID;
    }
}

// Lookup and return the specified group
const TxnGroup& TxnGroupBuilder::GetGroup(TxnGroupID groupID) const
{
    const auto& it { mGroupMap.find(groupID) };
    if(it == mGroupMap.end())
    {
        throw std::runtime_error("TxnGroupBuilder Unknown txn group ID");
    }

    return it->second;
}

// Remove the specified group and all its transactions
void TxnGroupBuilder::RemoveGroup(TxnGroupID groupID)
{
    // Lookup group
    const auto& it { mGroupMap.find(groupID) };
    if(it == mGroupMap.end())
    {
        throw std::runtime_error("TxnGroupBuilder unknown txn group ID");
    }

    // Purge all txns from this group
    for(const auto& txn : it->second)
    {
        mTxnMap.erase(txn.getTxn()->GetId());
    }

    // Remove the group itself
    mGroupMap.erase(it);
}

// Clear and reset
void TxnGroupBuilder::Clear()
{
    mTxnMap.clear();
    mGroupMap.clear();
    mNextGroupID = 0;
}

// Move members of an old group to a new group
void TxnGroupBuilder::MoveGroup(TxnGroup& newGroup, TxnGroup&& oldGroup)
{
    // Update txn map so all members of the old group are now recorded as
    // members of the new group.
    TxnGroupID newGroupID { newGroup.GetID() };
    for(const auto& entry : oldGroup)
    {
        const TxId& txid { entry.getTxn()->GetId() };
        const auto& txit { mTxnMap.find(txid) };
        if(txit != mTxnMap.end())
        {
            txit->second = newGroupID;
        }
        else
        {
            throw std::runtime_error("TxnGroupBuilder failed to lookup txid " + txid.ToString());
        }
    }

    // Move members of the old group into the new group
    newGroup.AddGroup(std::move(oldGroup));
}

// Fetch new group ID
TxnGroupID TxnGroupBuilder::NewGroupID()
{
    TxnGroupID res { mNextGroupID };

    // Bump next group ID, ensure we don't try to reuse an ID we still have
    // txns for (unlikely but better safe)
    do
    {
        ++mNextGroupID;
    }
    while(mGroupMap.count(mNextGroupID) != 0);

    return res;
}

}   // namespace mining

