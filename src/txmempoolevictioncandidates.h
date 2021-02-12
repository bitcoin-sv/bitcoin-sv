// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#pragma once
#include "txhasher.h"
#include "txmempool.h"

// CEvictionCandidateTracker is class that tracks which transaction should be removed. candidates for the removal
// are childless transactions. they are internally arranged in the heap structure with tx with lowest score on the top.
// for all calls to this class mempool should be locked
class CEvictionCandidateTracker
{
public:
    // the function object that assigns the score for the given transaction
    // transactions with lower score will be evicted first
    using Evaluator = std::function<int64_t(CTxMemPool::txiter)>;

private:
    // maximal ratio between expired and non-expired transactions
    static constexpr double MAX_INVALID_TO_VALID_RATIO = 1.0;

    // mempool's "mapLinks"
    std::reference_wrapper<const CTxMemPool::txlinksMap> links;
    // function calculates transaction worth, tx with lower worth will be evicted first
    Evaluator evaluator;
    
    // class that represents estimated worth of the transaction
    class EvalResult
    {
        // the score of the entry, transactions with lower score will be evicted first.
        // score should not be modified after it is initialized, if the score needs to be changed, a new EvalResult should be added and this one marked as invalid
        int64_t score = 0;
        // the entry for which the score is calculated, should not be modified. this iterator might be invalid if the entry is removed from the mempool
        CTxMemPool::txiter entry;
        // pointer to pointer to this object in the "entries" map. it tracks our position in the vector (heap). 
        // if the value is nullptr the object is expired (not tracked any more) and not referenced in the map any more
        EvalResult** ptrToMe; 
    public:
        
        EvalResult(CTxMemPool::txiter _entry, Evaluator& evaluator, EvalResult*& _ptrToMe )
            :score{evaluator(_entry)}
            ,entry{_entry}
            ,ptrToMe{&_ptrToMe}
        {
            *ptrToMe = this;
        }

        EvalResult(EvalResult&& other)
            :score{other.score}
            ,entry{other.entry}
            ,ptrToMe{other.ptrToMe}
        {
            if(ptrToMe != nullptr)
            {
                *ptrToMe = this;
                other.ptrToMe = nullptr;
            }
        }

        EvalResult& operator=(EvalResult&& other)
        {
            score = other.score;
            entry = other.entry;
            ptrToMe = other.ptrToMe;
            if(ptrToMe != nullptr)
            {
                *ptrToMe = this;
                other.ptrToMe = nullptr;
            }
            return *this;
        }

        EvalResult(const EvalResult&) = delete;
        EvalResult& operator=(const EvalResult&) = delete;

        CTxMemPool::txiter Entry() const { return entry; }
        void MarkExpired()               { ptrToMe = nullptr;}
        bool IsExpired() const           { return ptrToMe == nullptr; }
        int64_t Score() const            { return score; }
    };

    // the comparison function used for the creation of the heap, if the "first" is larger than
    // the second it returns true. used for the heap creation to make result with lowest score on the top of the heap
    static bool CompareResult(const EvalResult& first, const EvalResult& second);

    // vector that represents the heap of the evaluation results, the most worthless entry is on the top
    std::vector<EvalResult> heap;
    // map txid to the representing evaluation result. as the evaluation result moves inside heap this ptr will be changed. 
    // when removing a result from this map it should be marked as expired first
    std::unordered_map<TxId, EvalResult*, SaltedTxidHasher> entries;
    
    // adds entry to the "heap" and "entries"
    void InsertEntry(CTxMemPool::txiter entry);
    // marks the entry as expired
    void ExpireEntry(const TxId& tx);
    // pops expired transactions from the top of the heap, if the ratio between the size 
    // of the expired and non-expired entries is larger than MAX_INVALID_TO_VALID_RATIO the "heap" will
    // be filtered for the expired transaction and heap structure will be recreated
    void PopExpired(); 

    // direct parents of the tx
    const CTxMemPool::setEntries& GetParentsNoGroup(CTxMemPool::txiter entry) const; 
    // direct children of the tx
    const CTxMemPool::setEntries& GetChildrenNoGroup(CTxMemPool::txiter entry) const; 

    // returns true if any of the group members has non-group child
    bool HasChildren(const CPFPGroup& group) const; 
    // if entry is not a group member returns true an entry has children, calls overload for CPFPGroup otherwise
    bool HasChildren(CTxMemPool::txiter entry) const; 


public:
    // constructor, Takes reference to the mempool's mapLinks, and evaluator which is the function
    // that maps transaction iterator to double, where resulting double is worth of the transaction.
    // transaction with lower worth will be evicted sooner
    CEvictionCandidateTracker(CTxMemPool::txlinksMap& _links, Evaluator _evaluator);

    CEvictionCandidateTracker(CEvictionCandidateTracker&&) = default;
    CEvictionCandidateTracker& operator=(CEvictionCandidateTracker&&) = default;
    CEvictionCandidateTracker(const CEvictionCandidateTracker&) = delete;
    CEvictionCandidateTracker& operator=(const CEvictionCandidateTracker&) = delete;

    // recreates structure
    void Reset();

    // notifies the tracker that a new entry (transaction) is added to the mempool
    // call AFTER mapLinks and groups are updated
    void EntryAdded(CTxMemPool::txiter entry);

    // notifies the tracker that an entry (transaction) is removed from the mempool
    // call AFTER mapLinks and groups are updated
    void EntryRemoved(const TxId& txId, const CTxMemPool::setEntries& immediateParents);

    // notifies the tracker that an entry (transaction) is modified in such way that 
    // that it might change transactions worth (modified fee, added or removed from the primary mempool)
    void EntryModified(CTxMemPool::txiter entry);

    // returns most worthless transaction which does not have children, if the transaction
    // is the paying transaction of the group and any group member has non-group child, paying transaction 
    // will not be considered
    CTxMemPool::txiter GetMostWorthless() const;

    // returns all transactions that could be evicted
    CTxMemPool::setEntries GetAllCandidates() const;

    size_t DynamicMemoryUsage() const;
};
