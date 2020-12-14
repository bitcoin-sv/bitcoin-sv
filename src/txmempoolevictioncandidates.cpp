// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#include "txmempoolevictioncandidates.h"

bool CEvictionCandidateTracker::CompareResult(const EvalResult& first, const EvalResult& second)
{
    return first.Score() > second.Score();
}

void CEvictionCandidateTracker::InsertEntry(CTxMemPool::txiter entry)
{
    auto [iter, success] = entries.insert({entry->GetTxId(), nullptr});
    assert(success); // successful insertion
    heap.emplace_back(entry, evaluator, iter->second);

    std::push_heap(heap.begin(), heap.end(), CompareResult);
}

void CEvictionCandidateTracker::ExpireEntry(const TxId& txId)
{
    auto iter = entries.find(txId);
    if (iter == entries.end())
    {
        return;
    }
    iter->second->MarkExpired();
    entries.erase(iter);
}

void CEvictionCandidateTracker::PopExpired()
{
    if((entries.size() == 0) || 
        ((double(heap.size() - entries.size()) / entries.size()) >  MAX_INVALID_TO_VALID_RATIO))
    {
        heap.erase(
            std::remove_if(heap.begin(), 
                           heap.end(),
                           [](const EvalResult& x){return x.IsExpired();}),
            heap.end());

        std::make_heap(heap.begin(), heap.end(), CompareResult);
    }
    else
    {
        while (!heap.empty() && heap.front().IsExpired())
        {
            std::pop_heap(heap.begin(), heap.end(), CompareResult);
            heap.pop_back();
        }
    }
}

const CTxMemPool::setEntries& CEvictionCandidateTracker::GetParentsNoGroup(CTxMemPool::txiter entry) const
{
    return links.get().at(entry).parents;
}

const CTxMemPool::setEntries& CEvictionCandidateTracker::GetChildrenNoGroup(CTxMemPool::txiter entry) const
{
    return links.get().at(entry).children;
}

bool CEvictionCandidateTracker::HasChildren(const CPFPGroup& group) const
{
    // here we are trying to find if any member of the group has child outside of the group
    CTxMemPool::setEntries groupMembers(group.Transactions().begin(), group.Transactions().end());
    for (auto entry : group.Transactions())
    {
        for (auto child : GetChildrenNoGroup(entry))
        {
            if (groupMembers.find(child) == groupMembers.end())
            {
                // child is not member of the group
                return true;
            }
        }
    }
    return false;
}

bool CEvictionCandidateTracker::HasChildren(CTxMemPool::txiter entry) const
{
    if (entry->IsCPFPGroupMember())
    {
        return HasChildren(*(entry->GetCPFPGroup()));
    }
    return !GetChildrenNoGroup(entry).empty();
}

CEvictionCandidateTracker::CEvictionCandidateTracker(CTxMemPool::txlinksMap& _links, Evaluator _evaluator)
    : links{_links}
    , evaluator{_evaluator}
{
    heap.reserve(links.get().size());
    entries.reserve(links.get().size());
    for (const auto& [entry, connections] : links.get())
    {
        if (!connections.children.empty())
        {
            continue;
        }

        if (entry->IsCPFPGroupMember() && HasChildren(*(entry->GetCPFPGroup())))
        {
            continue;
        }

        auto[iter, success] = entries.insert({entry->GetTxId(), nullptr});
        assert(success);
        heap.emplace_back(entry, evaluator, iter->second);
    }
    std::make_heap(heap.begin(), heap.end(), CompareResult);
}


void CEvictionCandidateTracker::EntryAdded(CTxMemPool::txiter entry)
{
    for (const auto& parent : GetParentsNoGroup(entry))
    {
        if(parent->IsCPFPGroupMember())
        {
            ExpireEntry(parent->GetCPFPGroup()->PayingTransactionId());
        }
        else
        {
            ExpireEntry(parent->GetTxId());
        }    
    }
    
    PopExpired();
    InsertEntry(entry);
}

void CEvictionCandidateTracker::EntryRemoved(const TxId& txId, const CTxMemPool::setEntries& immediateParents)
{
    ExpireEntry(txId);
    PopExpired();

    for (const auto& parent : immediateParents)
    {
        if (HasChildren(parent))
        {
            continue;
        }
        if(parent->IsCPFPGroupMember())
        {
            InsertEntry(parent->GetCPFPGroup()->Transactions().back());
        }
        else
        {
            InsertEntry(parent);
        }
        
    }
}

void CEvictionCandidateTracker::EntryModified(CTxMemPool::txiter entry)
{
    auto it = entries.find(entry->GetTxId());
    if (it == entries.end())
    {
        return;
    }
    ExpireEntry(entry->GetTxId());
    PopExpired();
    InsertEntry(entry);
}

CTxMemPool::txiter CEvictionCandidateTracker::GetMostWorthless() const
{
    assert(entries.size() != 0);
    return heap.front().Entry();
}

CTxMemPool::setEntries CEvictionCandidateTracker::GetAllCandidates() const
{
    CTxMemPool::setEntries candidates;
    for(const auto& entry: entries)
    {
        candidates.insert(entry.second->Entry());
    }
    return candidates;
}

size_t CEvictionCandidateTracker::DynamicMemoryUsage() const
{
    return 
        memusage::DynamicUsage(heap) + 
        memusage::DynamicUsage(entries);
}
