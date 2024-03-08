// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2019-2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "txmempool.h"
#include "txmempoolevictioncandidates.h"
#include "clientversion.h"
#include "config.h"
#include "consensus/consensus.h"
#include "consensus/validation.h"
#include "frozentxo.h"
#include "mempooltxdb.h"
#include "miner_id/miner_id.h"
#include "miner_id/dataref_index.h"
#include "policy/fees.h"
#include "policy/policy.h"
#include "timedata.h"
#include "txdb.h"
#include "util.h"
#include "utilmoneystr.h"
#include "utiltime.h"
#include "validation.h"
#include "validationinterface.h"
#include "txn_validator.h"
#include <boost/range/adaptor/reversed.hpp>
#include <boost/uuid/random_generator.hpp>
#include <exception>
#include <mutex>

using namespace mining;

    /**
     * Special mempool coins provider for internal CTxMemPool use where smtx
     * mutex is expected to be locked.
     */
    class CoinsViewLockedMemPoolNL : public ICoinsView
    {
    public:
        CoinsViewLockedMemPoolNL(
            const CTxMemPool& mempoolIn,
            const CoinsDBView& DBView)
            : mempool{ mempoolIn }
            , mDBView{ DBView }
        {}

        std::optional<CoinImpl> GetCoin(const COutPoint &outpoint, uint64_t maxScriptSize) const override
        {
            CTransactionRef ptx = mempool.GetNL(outpoint.GetTxId());
            if (ptx) {
                if (outpoint.GetN() < ptx->vout.size()) {
                    return CoinImpl::MakeNonOwningWithScript(ptx->vout[outpoint.GetN()], MEMPOOL_HEIGHT, false, CFrozenTXOCheck::IsConfiscationTx(*ptx));
                }
                return {};
            }

            return mDBView.GetCoin(outpoint, maxScriptSize);
        }

        void CacheAllCoins(const std::vector<CTransactionRef>& txns) const override
        {
            mDBView.CacheAllCoins(txns);
        }

        std::optional<CoinWithScript> GetCoinWithScript(const COutPoint& outpoint) const
        {
            auto coinData = GetCoin(outpoint, std::numeric_limits<size_t>::max());
            if(coinData.has_value())
            {
                assert(coinData->HasScript());

                return std::move(coinData.value());
            }

            return {};
        }

    private:
        uint256 GetBestBlock() const override { assert(!"Should not be used!"); return {}; }

        const CTxMemPool& mempool;
        const CoinsDBView& mDBView;
    };

/**
 * class CTxPrioritizer
 */
CTxPrioritizer::CTxPrioritizer(CTxMemPool& mempool, const TxId& txnToPrioritise)
    : mMempool(mempool)
{
    // A nulness detection.
    if (!txnToPrioritise.IsNull()) {
        mTxnsToPrioritise.push_back(txnToPrioritise);
        mMempool.PrioritiseTransaction(mTxnsToPrioritise, MAX_MONEY);
    }
}

CTxPrioritizer::CTxPrioritizer(CTxMemPool& mempool, std::vector<TxId> txnsToPrioritise)
    : mMempool(mempool), mTxnsToPrioritise(std::move(txnsToPrioritise))
{
    // An early emptiness check.
    if (!mTxnsToPrioritise.empty()) {
        mMempool.PrioritiseTransaction(mTxnsToPrioritise, MAX_MONEY);
    }
}

CTxPrioritizer::~CTxPrioritizer()
{
    try {
        // An early emptiness check.
        if (!mTxnsToPrioritise.empty()) {
            mMempool.ClearPrioritisation(mTxnsToPrioritise);
        }
    } catch (...) {
        LogPrint(BCLog::MEMPOOL, "~CTxPrioritizer: Unexpected exception during destruction.\n");
    }
}

/**
 * class CTxMemPoolEntry
 */
CTxMemPoolEntry::CTxMemPoolEntry(const CTransactionRef& _tx,
                                 const Amount _nFee,
                                 int64_t _nTime,
                                 int32_t _entryHeight,
                                 bool _spendsCoinbaseOrConfiscation,
                                 LockPoints lp)
    : tx{std::make_shared<CTransactionWrapper>(_tx, nullptr)},
      nFee{_nFee},
      nTxSize{_tx->GetTotalSize()},
      nUsageSize{RecursiveDynamicUsage(_tx)},
      nTime{_nTime},
      feeDelta{Amount{0}},
      lockPoints{lp},
      entryHeight{_entryHeight},
      spendsCoinbaseOrConfiscation{_spendsCoinbaseOrConfiscation}
{}

// CPFP group, if any that this transaction belongs to.
mining::GroupID CTxMemPoolEntry::GetCPFPGroupId() const
{ 
    if(group)
    {
        return mining::GroupID{ group->Id() };
    }
    return std::nullopt; 
}

void CTxMemPoolEntry::UpdateFeeDelta(Amount newFeeDelta) {
    feeDelta = newFeeDelta;
}

void CTxMemPoolEntry::UpdateLockPoints(const LockPoints &lp) {
    lockPoints = lp;
}

std::atomic<uint64_t> CPFPGroup::counter = 1;

namespace {
// Takes given change set if not empty, creates new otherwise
class CEnsureNonNullChangeSet
{
    CJournalChangeSetPtr replacement;
    const CJournalChangeSetPtr& cs;
public:
    CEnsureNonNullChangeSet(CTxMemPool& theMempool,  const CJournalChangeSetPtr& changeSet)
        : replacement(changeSet ? nullptr : theMempool.getJournalBuilder().getNewChangeSet(JournalUpdateReason::UNKNOWN))
        , cs(changeSet ? changeSet : replacement) 
    {}

    CJournalChangeSet& Get() { return *cs; }
};
}

bool CTxMemPool::CheckAncestorLimits(
    const CTxMemPoolEntry& entry,
    uint64_t limitAncestorCount,
    uint64_t limitSecondaryMempoolAncestorCount,
    std::optional<std::reference_wrapper<std::string>> errString) const
{
    std::shared_lock lock{smtx};
    return CheckAncestorLimitsNL(entry,
                                 limitAncestorCount,
                                 limitSecondaryMempoolAncestorCount,
                                 errString);
}

bool CTxMemPool::CheckAncestorLimitsNL(
    const CTxMemPoolEntry& entry,
    uint64_t limitAncestorCount,
    uint64_t limitSecondaryMempoolAncestorCount,
    std::optional<std::reference_wrapper<std::string>> errString) const
{
    // Get parents of this transaction that are in the mempool
    // GetMemPoolParentsNL() is only valid for entries in the mempool, so we
    // iterate mapTx to find parents.
    setEntries parents;
    const auto tx = entry.GetSharedTx();
    size_t ancestorsCount = 0;
    size_t secondaryMempoolAncestorsCount = 0;

    for (const auto& in : tx->vin) 
    {
        const auto piter = mapTx.find(in.prevout.GetTxId());
        if (piter == mapTx.end()) 
        {
            continue;
        }
        auto[it, inserted] = parents.emplace(piter);
        if(inserted)
        {
            ancestorsCount = std::max(ancestorsCount, (*it)->ancestorsCount + 1);

            if(!(*it)->IsInPrimaryMempool())
            {
                secondaryMempoolAncestorsCount += (*it)->groupingData.value().ancestorsCount + 1;
            }
            
            if(ancestorsCount >= limitAncestorCount)
            {
                if(errString.has_value())
                {
                    errString.value().get() = strprintf("too many unconfirmed parents, %u [limit: %lu]", ancestorsCount, limitAncestorCount);
                }
                return false;
            }
            
            if(secondaryMempoolAncestorsCount >= limitSecondaryMempoolAncestorCount)
            {
                if(errString.has_value())
                {
                    errString.value().get() = strprintf("too many unconfirmed parents which we are not willing to mine, %lu [limit: %lu]", secondaryMempoolAncestorsCount, limitSecondaryMempoolAncestorCount);
                }
                return false;
            }

        }
    }
    return true;
}

void CTxMemPool::GetMemPoolAncestorsNL(
        const txiter& entryIter,
        setEntries& setAncestors) const
{
    auto parentHashes = GetMemPoolParentsNL(entryIter);
    
    while (!parentHashes.empty()) {
        txiter stageit = *parentHashes.begin();
        parentHashes.erase(parentHashes.begin());

        setAncestors.insert(stageit);
        
        const setEntries &setMemPoolParents = GetMemPoolParentsNL(stageit);
        for (const txiter &phash : setMemPoolParents)
        {
            // If this is a new ancestor, add it.
            if(setAncestors.count(phash) == 0)
            {
                parentHashes.insert(phash);
            }
        }
    }
}


void CTxMemPool::updateAncestorsOfNL(bool add, txiter it) {
    setEntries parentIters = GetMemPoolParentsNL(it);
    // add or remove this tx as a child of each parent
    for (txiter piter : parentIters) {
        updateChildNL(piter, it, add);
    }
}

CTxMemPool::CTxMemPool()
{
    // lock free clear
    clearNL();
}

CTxMemPool::~CTxMemPool() {
}

bool CTxMemPool::IsSpent(const COutPoint &outpoint) {
    std::shared_lock lock{smtx};
    return IsSpentNL(outpoint);
}

bool CTxMemPool::IsSpentNL(const COutPoint &outpoint) const {
    return mapNextTx.count(outpoint);
}

CTransactionWrapperRef CTxMemPool::IsSpentBy(const COutPoint &outpoint) const {
    std::shared_lock lock{ smtx };

    auto it = mapNextTx.find(outpoint);
    if (it == mapNextTx.end())
    {
        return nullptr;
    }
    return it->spentBy->tx;
}

unsigned int CTxMemPool::GetTransactionsUpdated() const {
    return nTransactionsUpdated;
}

void CTxMemPool::AddTransactionsUpdated(unsigned int n) {
    nTransactionsUpdated += n;
}

void CTxMemPool::AddUnchecked(
    const uint256 &hash,
    const CTxMemPoolEntry &entry,
    const TxStorage txStorage,
    const CJournalChangeSetPtr& changeSet,
    size_t* pnPrimaryMempoolSize,
    size_t* pnSecondaryMempoolSize,
    size_t* pnDynamicMemoryUsage) {

    {
        std::unique_lock lock{smtx};
        
        AddUncheckedNL(
            hash,
            entry,
            txStorage,
            changeSet,
            std::nullopt,
            pnPrimaryMempoolSize,
            pnSecondaryMempoolSize,
            pnDynamicMemoryUsage);
    }
    // Notify entry added without holding the mempool's lock
    NotifyEntryAdded(*entry.tx);
}

CTxMemPool::setEntriesTopoSorted CTxMemPool::GetSecondaryMempoolAncestorsNL(CTxMemPool::txiter payingTx) const
{
    setEntriesTopoSorted ancestors;
    setEntries toVisit{payingTx};

    // recursivly visit ancestors and collect all which are in the secondary mempool
    while(!toVisit.empty())
    {
        txiter entry = *toVisit.begin();
        toVisit.erase(toVisit.begin());
        bool isInserted =  ancestors.insert(entry).second;
        if (isInserted)
        {
            for(txiter parent: GetMemPoolParentsNL(entry))
            {
                if (!parent->IsInPrimaryMempool())
                {
                    toVisit.insert(parent);
                }
            }
        }
    }
    return ancestors;
}

SecondaryMempoolEntryData CTxMemPool::FillGroupingDataNL(const CTxMemPool::setEntriesTopoSorted& groupMembers) const
{
    SecondaryMempoolEntryData data;
    data.ancestorsCount = groupMembers.size();

    // precisely calculate groups data
    for(auto entry: groupMembers)
    {
        data.size += entry->GetTxSize();
        data.fee += entry->GetFee();
        data.feeDelta += entry->GetFeeDelta();
    }

    return data;
}

void CTxMemPool::AcceptSingleGroupNL(const CTxMemPool::setEntriesTopoSorted& groupMembers, mining::CJournalChangeSet& changeSet)
{
    auto groupingData = FillGroupingDataNL(groupMembers);
    auto group = std::make_shared<CPFPGroup>(groupingData, std::vector<CTxMemPool::txiter>{groupMembers.begin(), groupMembers.end()});
    
    // assemble the group
    for(auto entry: groupMembers)
    {
        // moving from secondary mempool to the primary
        mapTx.modify(entry, [&group](CTxMemPoolEntry& entry) {
                                entry.group = group;
                                entry.groupingData = std::nullopt;
                            });
        secondaryMempoolStats.Remove(entry);
        TrackEntryModified(entry);
    }

    // submit the group
    for(auto entry: groupMembers)
    {
        changeSet.addOperation(mining::CJournalChangeSet::Operation::ADD, CJournalEntry{*entry});
    }
}

bool CTxMemPool::IsPayingEnough(const SecondaryMempoolEntryData& groupingData) const
{
    return groupingData.fee + groupingData.feeDelta >= blockMinTxfee.GetFee(groupingData.size);
}

SecondaryMempoolEntryData CTxMemPool::CalculateSecondaryMempoolData(txiter entryIt) const
{
    SecondaryMempoolEntryData groupingData({
            entryIt->GetFee(), entryIt->GetFeeDelta(), entryIt->GetTxSize()});

    for (txiter parent : GetMemPoolParentsNL(entryIt)) {
        if (!parent->IsInPrimaryMempool()) {
            groupingData.fee += parent->groupingData->fee;
            groupingData.feeDelta += parent->groupingData->feeDelta;
            groupingData.size += parent->groupingData->size;
            groupingData.ancestorsCount += parent->groupingData->ancestorsCount + 1;
        }
    }

    return groupingData;
}

void CTxMemPool::SetGroupingDataNL(CTxMemPool::txiter entryIt, std::optional<SecondaryMempoolEntryData> groupingData)
{
    // NOTE: We use modify() here because it returns a mutable reference to
    //       the entry in the index, whereas dereferencing the iterator
    //       returns an immutable reference, which would require a
    //       const_cast<> and also would not update the index. Not that we
    //       expect any of the index keys to change here.
    mapTx.modify(entryIt, [&groupingData](CTxMemPoolEntry& entry) {
        entry.groupingData = groupingData;});

}


CTxMemPool::ResultOfUpdateEntryGroupingDataNL CTxMemPool::UpdateEntryGroupingDataNL(CTxMemPool::txiter entryIt)
{
    SecondaryMempoolEntryData groupingData = CalculateSecondaryMempoolData(entryIt);

    assert(!entryIt->IsInPrimaryMempool());

    if(!IsPayingEnough(groupingData))
    {
        if(entryIt->groupingData.value() == groupingData)
        {
            return ResultOfUpdateEntryGroupingDataNL::NOTHING;
        }

        SetGroupingDataNL(entryIt, groupingData);
        return ResultOfUpdateEntryGroupingDataNL::GROUPING_DATA_MODIFIED;
    }
    else
    {
        if(groupingData.ancestorsCount == 0)
        {
            SetGroupingDataNL(entryIt, std::nullopt);
            return ResultOfUpdateEntryGroupingDataNL::ADD_TO_PRIMARY_STANDALONE;
        }

        SetGroupingDataNL(entryIt, groupingData);
        return ResultOfUpdateEntryGroupingDataNL::ADD_TO_PRIMARY_GROUP_PAYING_TX;
    }
}

void CTxMemPool::TryAcceptToPrimaryMempoolNL(CTxMemPool::setEntriesTopoSorted toUpdate, 
                                             mining::CJournalChangeSet& changeSet, 
                                             bool limitTheLoop)
{
    // we want to limit the number of txs that could be updated at once to mitigate potential attack where
    // creating of one group results in accepting arbitrary number of txs to the mempool.
    // 
    // for example:  payFor0  payFor1  payFor2  payFor3  payFor4  payFor5  payFor6  
    //                      \   |    \   |    \   |    \   |    \   |    \   |    \ ....      
    //                        tx0      tx1      tx2      tx3      tx4      tx5     
    //
    //  if the payFor0 is submitted last, it will trigger acceptance of all transaction
    int countOfVisitedTxs = 0;

    while(!toUpdate.empty() && !(limitTheLoop && countOfVisitedTxs > MAX_NUMBER_OF_TX_TO_VISIT_IN_ONE_GO))
    {
        // take first item from the topo-sorted set, this transactions does not
        // depend on any other tx in the set
        txiter entry = *toUpdate.begin();
        toUpdate.erase(toUpdate.begin());

        assert(!entry->IsInPrimaryMempool());

        // update grouping data of this entry and see what happend
        ResultOfUpdateEntryGroupingDataNL whatHappend = UpdateEntryGroupingDataNL(entry);
            
        switch (whatHappend)
        {
            case ResultOfUpdateEntryGroupingDataNL::ADD_TO_PRIMARY_GROUP_PAYING_TX:
            {
                // this is paying tx of the group, we should create CPFP group
                // find groups members
                auto groupMembers = GetSecondaryMempoolAncestorsNL(entry);
                // put it to primary mempool (in the journal also)
                AcceptSingleGroupNL(groupMembers, changeSet);

                // let see if any of the group member has children outside of the group
                // if it has, put them in the toUpdate set, their grouping data should be updated
                // because its parent is accepted to the group. it may result in accepting it to the 
                // primary mempool because it got rid of its parents debt
                for(auto member: groupMembers)
                {
                    for(auto child: GetMemPoolChildrenNL(mapTx.project<transaction_id>(member)))
                    {
                        if(groupMembers.find(child) == groupMembers.end())
                        {
                            toUpdate.insert(child);
                        }
                    }
                }
                countOfVisitedTxs += groupMembers.size();
                TrackEntryModified(entry);
                break;
            }
            case ResultOfUpdateEntryGroupingDataNL::ADD_TO_PRIMARY_STANDALONE:
            {
                // accept it to primary mempool
                changeSet.addOperation(mining::CJournalChangeSet::Operation::ADD, CJournalEntry{*entry});
                secondaryMempoolStats.Remove(entry);
                // enqueue children to update
                for(auto child: GetMemPoolChildrenNL(entry))
                {
                    toUpdate.insert(child);
                }
                countOfVisitedTxs += 1;
                TrackEntryModified(entry);
                break;
            }
            case ResultOfUpdateEntryGroupingDataNL::GROUPING_DATA_MODIFIED:
            {
                // enqueue children to update
                for(auto child: GetMemPoolChildrenNL(entry))
                {
                    toUpdate.insert(child);
                }
                countOfVisitedTxs += 1;
                TrackEntryModified(entry);
                break;
            }
            case ResultOfUpdateEntryGroupingDataNL::NOTHING:  
            { 
                countOfVisitedTxs += 1; 
                break;
            }
            default:
            {
	            assert(false);
            }
        }
    }
}

void CTxMemPool::TryAcceptChildlessTxToPrimaryMempoolNL(CTxMemPool::txiter entry, mining::CJournalChangeSet& changeSet)
{
    if(nCheckFrequency)
    {
        assert(GetMemPoolChildrenNL(entry).empty());
    }

    SecondaryMempoolEntryData data = CalculateSecondaryMempoolData(entry);
    if(IsPayingEnough(data))
    {
        if(data.ancestorsCount == 0)
        {
            // accept it to primary mempool as standalone tx
            SetGroupingDataNL(entry, std::nullopt);
            changeSet.addOperation(mining::CJournalChangeSet::Operation::ADD, CJournalEntry{*entry});
            secondaryMempoolStats.Remove(entry);
        }
        else
        {
            // accept it to primary mempool as group paying tx
            setEntriesTopoSorted toUpdate;
            toUpdate.insert(entry);
            TryAcceptToPrimaryMempoolNL(std::move(toUpdate), changeSet, true);
        }
    }
    else
    {
        SetGroupingDataNL(entry, data);
    }
}

CTxMemPool::setEntriesTopoSorted CTxMemPool::RemoveFromPrimaryMempoolNL(CTxMemPool::setEntriesTopoSorted toRemove, mining::CJournalChangeSet& changeSet, 
    bool putDummyGroupingData, const setEntries* entriesToIgnore)
{
    setEntriesTopoSorted removed;

    while(!toRemove.empty())
    {
        // take first item from the topo-sorted set, this transactions is not
        // descedant of any other tx in the set
        txiter entry = *toRemove.begin();
        toRemove.erase(toRemove.begin());

        // already in secondary, skip it
        if(!entry->IsInPrimaryMempool())
        {
            continue;
        }

        if(entry->IsCPFPGroupMember())
        {
            // if the entry is a member of the group, we will disband whole group but not here
            // we will just remove grouping data from every group member so they will look like 
            // they are accepted to primary as standalone txs, and re-schedule them for removal

            // keep one reference while iterating to prevent deletion
            auto group = entry->group;
            for(txiter groupMember: group->Transactions())
            {
                // if the entry is in the entriesToIgnore skip it
                if(entriesToIgnore == nullptr || entriesToIgnore->find(groupMember) == entriesToIgnore->end())
                {
                    mapTx.modify(groupMember, 
                        [](CTxMemPoolEntry& entry) {entry.group.reset();});
                    // we have removed group object so it will look like it is accepted as standalone in next round
                    toRemove.insert(groupMember);
                }
                
            }
        }
        else
        {
            // add to change set, removin them from journal
            changeSet.addOperation(CJournalChangeSet::Operation::REMOVE, CJournalEntry{*entry});
            // removing from primary to secondary mempool
            secondaryMempoolStats.Add(entry);
            // add to set of removed
            removed.insert(entry);

            // add children to the set of removed
            for(txiter child: GetMemPoolChildrenNL(entry))
            {
                toRemove.insert(child);
            }

            // update grouping data, this marks them as secondary mempool tx
            if(putDummyGroupingData)
            {
                SetGroupingDataNL(entry, SecondaryMempoolEntryData());
            }
            else
            {
                SecondaryMempoolEntryData groupingData = CalculateSecondaryMempoolData(entry);
                SetGroupingDataNL(entry, groupingData);
            }
            TrackEntryModified(entry);
        }
    }
    return removed;
}

void CTxMemPool::UpdateAncestorsCountNL(CTxMemPool::setEntriesTopoSorted entries)
{
    while(!entries.empty())
    {
        txiter entry = *entries.begin();
        entries.erase(entries.begin());

        for(auto child: GetMemPoolChildrenNL(entry))
        {
            entries.insert(child);
        }
        
        size_t ancestorsCount = 0;
        for(auto parent: GetMemPoolParentsNL(entry))
        {
            ancestorsCount = std::max(ancestorsCount, parent->ancestorsCount + 1);
        }
        mapTx.modify(entry, [ancestorsCount](CTxMemPoolEntry& entry) {
                                entry.ancestorsCount = ancestorsCount;
                             });
    }
}

void CTxMemPool::AddUncheckedNL(
    const uint256 &hash,
    const CTxMemPoolEntry &originalEntry,
    const TxStorage txStorage,
    const CJournalChangeSetPtr& changeSet,
    SpentOutputs spentOutputs,
    size_t* pnPrimaryMempoolSize,
    size_t* pnSecondaryMempoolSize,
    size_t* pnDynamicMemoryUsage)
{
    static const auto nullTxDB = std::shared_ptr<CMempoolTxDBReader>{nullptr};

    // Make sure the transaction database is initialized so that we have
    // a valid mempoolTxDB for the following checks.
    OpenMempoolTxDB();

    // Copy the entry because we'll modify it before insertion.
    auto entry {originalEntry};

    // During reorg, we could be re-adding an entry whose transaction was
    // previously moved to disk, in which case we must make sure that the entry
    // belongs to the same mempool.
    const auto thisTxDB = mempoolTxDB->GetDatabase();
    assert(entry.tx->HasDatabase(nullTxDB) || entry.tx->HasDatabase(thisTxDB));

    // Update the insertion order index for this entry.
    entry.SetInsertionIndex(insertionIndex.GetNext());

    // Update the transaction wrapper.
    if (txStorage == TxStorage::memory) {
        entry.tx = std::make_shared<CTransactionWrapper>(entry.GetSharedTx(), thisTxDB);
    }
    else {
        entry.tx = std::make_shared<CTransactionWrapper>(entry.GetTxId(), thisTxDB);
    }

    // Update transaction for any feeDelta created by PrioritiseTransaction.
    const auto pos = mapDeltas.find(hash);
    if (pos != mapDeltas.end()) {
        const auto& amount = pos->second;
        if (amount != Amount(0)) {
            entry.UpdateFeeDelta(amount);
        }
    }

    // Insert the new entry
    const auto [newit, inserted] = mapTx.insert(entry);
    assert(inserted);
    const auto[linksit, success] = mapLinks.insert(make_pair(newit, TxLinks()));

    // Update cachedInnerUsage to include contained transaction's usage.
    // (When we update the entry for in-mempool parents, memory usage will be
    // further updated.)
    cachedInnerUsage += newit->DynamicMemoryUsage();
    if(success)
    {
        cachedInnerUsage += memusage::DynamicUsage(linksit->second.parents);
        cachedInnerUsage += memusage::DynamicUsage(linksit->second.children);
    }

    std::set<uint256> setParentTransactions;
    if (spentOutputs.has_value())
    {
        for (const auto& prevout: spentOutputs->get())
        {
            mapNextTx.insert(OutpointTxPair{prevout, newit});
            setParentTransactions.insert(prevout.GetTxId());
        }
    }
    else
    {
        const auto sharedTx = newit->GetSharedTx();
        for (const auto& in : sharedTx->vin)
        {
            mapNextTx.insert(OutpointTxPair{in.prevout, newit});
            setParentTransactions.insert(in.prevout.GetTxId());
        }
    }
    // Don't bother worrying about child transactions of this one. Normal case
    // of a new transaction arriving is that there can't be any children,
    // because such children would be orphans.

    // Update ancestors with information about this tx
    // and collect information about parent's ancestors count
    size_t ancestorsCount = 0;
    for (const uint256 &phash : setParentTransactions) {
        txiter pit = mapTx.find(phash);
        if (pit != mapTx.end()) {
            ancestorsCount = std::max(ancestorsCount, pit->ancestorsCount + 1);
            updateParentNL(newit, pit, true);
        }
    }
    mapTx.modify(newit, [ancestorsCount](CTxMemPoolEntry& entry) {
        entry.ancestorsCount = ancestorsCount;
    });

    updateAncestorsOfNL(true, newit);

    CEnsureNonNullChangeSet nonNullChangeSet{*this, changeSet};

    // set dummy data that it looks like it is in the secondary mempool
    SetGroupingDataNL(newit, SecondaryMempoolEntryData());
    // now see if it can be accepted to primary mempool
    // this will set correct groupin data if it stays in the secondary mempool
    secondaryMempoolStats.Add(newit);
    TryAcceptChildlessTxToPrimaryMempoolNL(newit, nonNullChangeSet.Get());

    nTransactionsUpdated++;
    totalTxSize += newit->GetTxSize();

    // If it is required calculate mempool size & dynamic memory usage.
    if (pnPrimaryMempoolSize) {
        *pnPrimaryMempoolSize = PrimaryMempoolSizeNL();
    }
    if (pnSecondaryMempoolSize) {
        *pnSecondaryMempoolSize = secondaryMempoolStats.Size();
    }
    if (pnDynamicMemoryUsage) {
        *pnDynamicMemoryUsage = DynamicMemoryUsageNL();
    }

    // Update the eviction candidate tracker.
    TrackEntryAdded(newit);
}

void CTxMemPool::removeUncheckedNL(
    const setEntries& entries,
    CJournalChangeSet& changeSet,
    const CTransactionConflict& conflictedWith,
    MemPoolRemovalReason reason)
{
    OpenMempoolTxDB();
    for (const auto& entry : entries)
    {
        NotifyEntryRemoved(*entry->tx, reason);

        auto [itBegin, itEnd] = mapNextTx.get<by_txiter>().equal_range(entry);
        mapNextTx.get<by_txiter>().erase(itBegin, itEnd);

        // Apply to the current journal, but only if it is in the journal (primary mempool) already
        if(entry->IsInPrimaryMempool())
        {
            changeSet.addOperation(CJournalChangeSet::Operation::REMOVE, CJournalEntry{*entry});
        }
        else
        {
            secondaryMempoolStats.Remove(entry);
        }

        totalTxSize -= entry->GetTxSize();
        cachedInnerUsage -= entry->DynamicMemoryUsage();
        cachedInnerUsage -= memusage::DynamicUsage(mapLinks[entry].parents) +
                            memusage::DynamicUsage(mapLinks[entry].children);

        const auto txid = entry->GetTxId();
        const auto size = entry->GetTxSize();
        const auto removeFromDisk = !entry->IsInMemory();

        setEntries parents;
        if (evictionTracker) {
            parents = std::move(mapLinks.at(entry).parents);
        }

        mapLinks.erase(entry);
        mapTx.erase(entry);
        nTransactionsUpdated++;

        if (reason == MemPoolRemovalReason::BLOCK || reason == MemPoolRemovalReason::REORG)
        {
            GetMainSignals().TransactionRemovedFromMempoolBlock(txid, reason);
        }
        else
        {
            GetMainSignals().TransactionRemovedFromMempool(txid, reason, conflictedWith);
        }

        if (removeFromDisk)
        {
            mempoolTxDB->Remove({txid, size});
        }

        // Update the eviction candidate tracker.
        TrackEntryRemoved(txid, parents);
    }
}

// Calculates descendants of entry that are not already in setDescendants, and
// adds to setDescendants. Assumes entryit is already a tx in the mempool and
// setDescendants is correct for tx and all descendants. Also assumes that
// if an entry is in setDescendants already, then all in-mempool descendants of
// it are already in setDescendants as well, so that we can save time by not
// iterating over those entries.
void CTxMemPool::GetDescendantsNL(txiter entryit,
                                  setEntries &setDescendants) const {
    setEntries stage;
    if (setDescendants.count(entryit) == 0) {
        stage.insert(entryit);
    }
    // Traverse down the children of entry, only adding children that are not
    // accounted for in setDescendants already (because those children have
    // either already been walked, or will be walked in this iteration).
    while (!stage.empty()) {
        txiter it = *stage.begin();
        setDescendants.insert(it);
        stage.erase(it);

        const setEntries &setChildren = GetMemPoolChildrenNL(it);
        for (const txiter &childiter : setChildren) {
            if (!setDescendants.count(childiter)) {
                stage.insert(childiter);
            }
        }
    }
}

void CTxMemPool::removeRecursiveNL(
    const CTransaction& origTx,
    const CJournalChangeSetPtr& changeSet,
    const CTransactionConflict& conflictedWith,
    MemPoolRemovalReason reason)
{
    CEnsureNonNullChangeSet nonNullChangeSet{*this, changeSet};
    setEntries txToRemove;
    txiter origit = mapTx.find(origTx.GetId());
    if (origit != mapTx.end()) {
        txToRemove.insert(origit);
    } else {
        // When recursively removing but origTx isn't in the mempool be sure
        // to remove any children that are in the pool. This can happen during
        // chain re-orgs if origTx isn't re-accepted into the mempool for any
        // reason.
        const uint32_t outputCount = origTx.vout.size();
        for(uint32_t ndx = 0; ndx < outputCount; ndx++)
        {
            auto nextit = mapNextTx.find(COutPoint{origTx.GetId(), ndx});
            if(nextit != mapNextTx.end())
            {
                txToRemove.insert(nextit->spentBy);
            }
        }
    }
    setEntries setAllRemoves;
    for (txiter it : txToRemove) {
        GetDescendantsNL(it, setAllRemoves);
    }

    removeStagedNL(setAllRemoves, nonNullChangeSet.Get(), conflictedWith, reason);
}

void CTxMemPool::RemoveForReorgNL(
    const Config &config,
    const CoinsDB& coinsTip,
    const CJournalChangeSetPtr& changeSet,
    const CBlockIndex& tip,
    int flags) {

    const int32_t nMemPoolHeight = tip.GetHeight() + 1;
    const int nMedianTimePast = tip.GetMedianTimePast();
    // Remove transactions spending a coinbase which are now immature and
    // no-longer-final transactions.
    setEntries txToRemove;
    for (txiter it = mapTx.begin(); it != mapTx.end(); it++) {
        const auto tx = it->GetSharedTx();
        LockPoints lp = it->GetLockPoints();
        bool validLP = TestLockPointValidity(&lp);

        CoinsDBView tipView{ *pcoinsTip };
        CoinsViewLockedMemPoolNL view{ *this, tipView };
        CCoinsViewCache viewMemPool{ view };

        CValidationState state;
        if (!ContextualCheckTransactionForCurrentBlock(
                config,
                *tx,
                tip.GetHeight(),
                nMedianTimePast,
                state,
                flags) ||
                !CheckSequenceLocks(
                    tip,
                    *tx,
                    config,
                    flags,
                    &lp,
                    validLP ? nullptr : &viewMemPool)) {
            // Note if CheckSequenceLocks fails the LockPoints may still be
            // invalid. So it's critical that we remove the tx and not depend on
            // the LockPoints.
            txToRemove.insert(it);
        } else if (it->GetSpendsCoinbaseOrConfiscation()) {
            for (const auto& prevout: GetOutpointsSpentByNL(it)) {
                txiter it2 = mapTx.find(prevout.GetTxId());
                if (it2 != mapTx.end()) {
                    continue;
                }

                auto coin = tipView.GetCoin(prevout);
                assert( coin.has_value() );
                if (nCheckFrequency != 0) {
                    assert(coin.has_value() && !coin->IsSpent());
                }

                if (!coin.has_value() || coin->IsSpent() ||
                    (coin->IsCoinBase() &&
                     nMemPoolHeight - coin->GetHeight() <
                         COINBASE_MATURITY) ||
                    (coin->IsConfiscation() &&
                     nMemPoolHeight - coin->GetHeight() <
                         CONFISCATION_MATURITY)) {
                    txToRemove.insert(it);
                    break;
                }
            }
        }
        if (!validLP) {
            mapTx.modify(it, update_lock_points(lp));
        }
    }

    setEntries setAllRemoves;
    for (txiter it : txToRemove) {
        GetDescendantsNL(it, setAllRemoves);
    }
    CEnsureNonNullChangeSet nonNullChangeSet{*this, changeSet};
    removeStagedNL(setAllRemoves, nonNullChangeSet.Get(), noConflict, MemPoolRemovalReason::REORG);
}

void CTxMemPool::RemoveForBlock(
    const std::vector<CTransactionRef> &vtx,
    const CJournalChangeSetPtr& changeSet,
    const uint256& blockhash,
    std::vector<CTransactionRef>& txNew,
    const Config& config) {

    CEnsureNonNullChangeSet nonNullChangeSet{*this, changeSet};

    std::unique_lock lock{smtx};

    evictionTracker.reset();

    int64_t last_block_tx_time = 0;
    CFeeRate minBlockFeeRate{Amount{std::numeric_limits<int64_t>::max()}};

    setEntries toRemove; // entries which should be removed
    setEntriesTopoSorted childrenOfToRemove; // we must collect all transaction which parents we have removed to update its ancestorCount
    setEntriesTopoSorted childrenOfToRemoveGroupMembers; // immediate children of entries we will remove that are members of the cpfp group, need to be updated after removal
    setEntriesTopoSorted childrenOfToRemoveSecondaryMempool; // immediate children of entries we will remove that are in the secondary mempool, need to be updated after removal

    for(auto vtxIter = vtx.rbegin();  vtxIter != vtx.rend();  vtxIter++)
    {
        auto& tx = *vtxIter;
        
        auto found = mapTx.find(tx->GetId()); // see if we have this transaction from block

        if(found != mapTx.end()) 
        {
            toRemove.insert(found);

            // get block's latest transaction time
            int64_t foundTxTime = (&*found)->GetTime();
            if (foundTxTime > last_block_tx_time) {
                last_block_tx_time = foundTxTime;
            }
            // get block's lowest transaction fee rate
            CFeeRate foundTxFeeRate{(&*found)->GetFee(), (&*found)->GetTxSize()};
            if (foundTxFeeRate < minBlockFeeRate) {
                minBlockFeeRate = foundTxFeeRate;
            }

            for(auto child: GetMemPoolChildrenNL(found))
            {
                // we are iterating block transactions backwards (we will always first visit child than its parent) 
                // all children that are scheduled for removal are in txToRemove set already
                if(toRemove.find(child) == toRemove.end())
                {
                    // we will not remove this child
                    // let's see if it need to be updated later on
                    // it needs to be updated in two cases: 
                    //     1. parent and child are part of the same group (group needs to be disbanded)
                    //     2. child is in secondary mempool, as well as it's parent (grouping data needs to be updated)
                    if(found->IsCPFPGroupMember() && child->IsCPFPGroupMember() && (found->GetCPFPGroup() == child->GetCPFPGroup()))
                    {
                        childrenOfToRemoveGroupMembers.insert(child);
                    }
                    else if(!found->IsInPrimaryMempool() && !child->IsInPrimaryMempool())
                    {
                        childrenOfToRemoveSecondaryMempool.insert(child);
                    }

                    childrenOfToRemove.insert(child);
                }
            }
        }
        else
        {
            // walk through all inputs by spent tx
            for (const CTxIn &txin : tx->vin) 
            {
                // and see if we already spending them
                auto it = mapNextTx.find(txin.prevout);
                if (it != mapNextTx.end()) 
                {
                    // found double-spend

                    // collect all descendants of the tx which made double-spend
                    setEntries conflictedWithDescendants;
                    GetDescendantsNL(mapTx.find(it->spentBy->GetTxId()), conflictedWithDescendants);

                    for(txiter inConflict: conflictedWithDescendants)
                    {
                        // inConflict will be erased so remove it from the sets of txs that needs to be updated
                        childrenOfToRemove.erase(inConflict);
                        childrenOfToRemoveGroupMembers.erase(inConflict);
                        childrenOfToRemoveSecondaryMempool.erase(inConflict);
                    }

                    // remove conflicted tx from mempool (together with all descendants)
                    auto conflict = CTransactionConflict{{tx.get(), &blockhash}};
                    removeStagedNL(conflictedWithDescendants, nonNullChangeSet.Get(), conflict, MemPoolRemovalReason::CONFLICT);
                }
            }
            txNew.push_back(tx);
        }
    }

    if (config.GetDetectSelfishMining() && !mapTx.empty())
    {
        if (CheckSelfishNL(toRemove, last_block_tx_time, minBlockFeeRate, config))
        {
            LogPrint(BCLog::MEMPOOL, "Selfish mining detected.\n");
        }
    }

    // remove affected groups from primary mempool
    // we are ignoring members of "toRemove" (we will remove them in the removeUncheckedNL), so that "removedFromPrimary" can not contain transactions that will be removed
    auto removedFromPrimary = RemoveFromPrimaryMempoolNL(std::move(childrenOfToRemoveGroupMembers), nonNullChangeSet.Get(), true, &toRemove);

    std::vector<txiter> tempEntries;
    // disconnect children from its soon-to-be-removed parents
    for(txiter child: childrenOfToRemove)
    {
        for(txiter parent: GetMemPoolParentsNL(child))
        {            
            // if soon-to-be-removed parent is in the parent's set collect it
            if(toRemove.find(parent) != toRemove.end())
            {
                tempEntries.push_back(parent);
            }
        }

        // now disconnect all parents that we found
        for(txiter parentToRemove: tempEntries)
        {
            updateParentNL(child, parentToRemove, false);
        }
        tempEntries.clear();
    }

    // now remove transactions from mempool
    removeUncheckedNL(toRemove, nonNullChangeSet.Get(), noConflict, MemPoolRemovalReason::BLOCK);

    UpdateAncestorsCountNL(std::move(childrenOfToRemove));

    setEntriesTopoSorted toRecheck;
    // we will recheck all disbanded groups members and secondary mempool children together
    std::set_union(removedFromPrimary.begin(), removedFromPrimary.end(), 
                   childrenOfToRemoveSecondaryMempool.begin(), childrenOfToRemoveSecondaryMempool.end(),
                   std::inserter(toRecheck, toRecheck.begin()), InsrtionOrderComparator());

    // we want to try re-accept all transactions that were removed from primary mempool
    TryAcceptToPrimaryMempoolNL(std::move(toRecheck), nonNullChangeSet.Get(), false);

    lastRollingFeeUpdate = GetTime();
    blockSinceLastRollingFeeBump = true;
}

bool CTxMemPool::CheckSelfishNL(const setEntries& block_entries, int64_t last_block_tx_time, 
                                const CFeeRate& minBlockFeeRate, const Config& config) 
{
    auto last = mapTx.get<entry_time>().end();
    last--;

    int64_t last_mempool_tx_time = (&*last)->GetTime();
    // If time difference between the last transaction that was included in the
    // block and the last transaction that was left in the mempool is smaller
    // than threshold, block is not considered selfish.
    if (last_mempool_tx_time - last_block_tx_time < config.GetMinBlockMempoolTimeDifferenceSelfish()) 
    {
        return false;
    }

    // Value from -blockmintxfee parameter
    Amount blockMinTxFee{mempool.GetBlockMinTxFee().GetFeePerK()};
    uint64_t aboveBlockTxFeeCount = 0;
    // If received block is not empty check if there are txs in mempool that doesn't exists in received block
    if (!block_entries.empty())
    {
        // Measure duration of for loop of mempool Txs
        int64_t forLoopOfMempoolTxsDuration = GetTimeMicros();
        std::vector<const CTxMemPoolEntry*> entries;
        // Loop backwards through mempool only to tx's time > last_block_tx_time
        for ( ; last != mapTx.get<entry_time>().begin() && last->GetTime()>last_block_tx_time; --last) 
        {
            if (block_entries.find(mapTx.project<transaction_id>(last)) != block_entries.end()) 
            {
                continue;
            }
            entries.push_back(&*last);
        }
        forLoopOfMempoolTxsDuration = GetTimeMicros() - forLoopOfMempoolTxsDuration;
        LogPrint(BCLog::BENCH, "    - CheckSelfishNL() : for loop of mempool's %u Txs completed in %.2fms \n", (unsigned int)mapTx.size(), forLoopOfMempoolTxsDuration * 0.001);

        // Measure duration of for loop of block Txs
        int64_t count_ifDurationOfBlockTxs = GetTimeMicros();
        // Take higher value between the init value and received block tx fee
        Amount maxBlockFeeRate {std::max(blockMinTxFee, minBlockFeeRate.GetFeePerK())};
        aboveBlockTxFeeCount = std::count_if(entries.begin(), entries.end(), [&maxBlockFeeRate](const auto& ent)
            {
                // If fee rate one of the entries is above the max(BlockMinFeePerKB, minBlockFeeRate), then it should be in block
                // It was not included in block --> sth is wrong
                return CFeeRate{ent->GetFee(), ent->GetTxSize()}.GetFeePerK() >= maxBlockFeeRate;
            });

        count_ifDurationOfBlockTxs = GetTimeMicros() - count_ifDurationOfBlockTxs;
        LogPrint(BCLog::BENCH, "    - CheckSelfishNL() : count_if of block's %u Txs completed in %.2fms \n", (unsigned int)entries.size(), count_ifDurationOfBlockTxs * 0.001);
        LogPrint(BCLog::MEMPOOL, "%u/%u transactions in mempool were not included in block. %u/%u have a fee above the block's fee rate.\n",
            (unsigned int)entries.size(), (unsigned int)mapTx.size(), aboveBlockTxFeeCount, (unsigned int)entries.size());
    } 
    else
    {
        // If the transactions of the received block do not exist in the mempool or block is empty,
        // check if txs in mempool have sufficient fee rate to be in block
        aboveBlockTxFeeCount = std::count_if(mapTx.begin(), mapTx.end(), [&blockMinTxFee](const auto& tx)
                {
                    return CFeeRate{tx.GetFee(), tx.GetTxSize()}.GetFeePerK() >= blockMinTxFee;
                });
        LogPrint(BCLog::MEMPOOL, "%u/%u transactions have a fee above the config blockmintxfee value."
            " Block was either empty or none of its transactions are in our mempool.\n ", 
            aboveBlockTxFeeCount, (unsigned int)mapTx.size());
    }

    // Check if percentage of txs in mempool that are not included in block is above TH
    if (aboveBlockTxFeeCount > 0 && (aboveBlockTxFeeCount*100 / mapTx.size()) >= config.GetSelfishTxThreshold())
    {
        return true;
    }
    return false;
}

void CTxMemPool::RemoveFrozen(const mining::CJournalChangeSetPtr& changeSet)
{
    std::unique_lock lock{smtx};
    RemoveFrozenNL( changeSet );
}

void CTxMemPool::RemoveFrozenNL(const mining::CJournalChangeSetPtr& changeSet)
{
    CEnsureNonNullChangeSet nonNullChangeSet{*this, changeSet};

    CFrozenTXOCheck frozenTXOCheck{
        chainActive.Tip()->GetHeight() + 1,
        "mempool",
        chainActive.Tip()->GetBlockHash()};

    // Find transactions in mempool that spend frozen outputs
    setEntries spendingFrozenTXOs;
    for(const auto& spentTXO: mapNextTx.get<by_prevout>())
    {
        std::uint8_t effectiveBlacklist;
        if(!frozenTXOCheck.Check(spentTXO.outpoint, effectiveBlacklist))
        {
            // Is this input spent by confiscation transaction?
            // NOTE: This is inefficient because we're loading every tx to memory and we're doing
            //       it more than once for txs with several inputs. But since there will not be
            //       many transactions in mempool that spend frozen TXOs, this is not an issue.
            auto ptx = spentTXO.spentBy->GetSharedTx();
            const bool isConfiscationTx = CFrozenTXOCheck::IsConfiscationTx(*ptx);
            if(!isConfiscationTx)
            {
                // For normal transaction input must not be frozen.
                frozenTXOCheck.LogAttemptToSpendFrozenTXO(spentTXO.outpoint, *ptx, effectiveBlacklist, spentTXO.spentBy->GetTime());

                // Store iterator to this tx and all its descendants
                GetDescendantsNL(spentTXO.spentBy, spendingFrozenTXOs);
            }
            // For confiscation transaction all inputs must be frozen, but this is implicitly guaranteed if
            // confiscation transaction is whitelisted, which will be checked elsewhere. Consequently, here we
            // do not need to check that every confiscation transaction in mempool only spends frozen inputs.
        }
    }

    if(!spendingFrozenTXOs.empty())
    {
        removeStagedNL(spendingFrozenTXOs, nonNullChangeSet.Get(), noConflict, MemPoolRemovalReason::FROZEN_INPUT);

        mFrozenTxnUpdatedAt = nTransactionsUpdated.load();
    }
}

void CTxMemPool::RemoveInvalidCTXs(const mining::CJournalChangeSetPtr& changeSet)
{
    std::unique_lock lock{smtx};
    RemoveInvalidCTXsNL( changeSet );
}

void CTxMemPool::RemoveInvalidCTXsNL(const mining::CJournalChangeSetPtr& changeSet)
{
    CEnsureNonNullChangeSet nonNullChangeSet{*this, changeSet};

    CFrozenTXOCheck frozenTXOCheck{
        chainActive.Tip()->GetHeight() + 1,
        "mempool",
        chainActive.Tip()->GetBlockHash()};

    // Find confiscation transactions in mempool that are now no longer valid. Possible reasons:
    //  - confiscation transaction is no longer valid because mempool height is <enforceAtHeight
    //  - confiscation transaction is no longer whitelisted
    setEntries nonWhitelistedConfiscationTxs;
    for (txiter it = mapTx.begin(); it != mapTx.end(); it++)
    {
        const CTransactionRef tx = it->GetSharedTx();
        if(!CFrozenTXOCheck::IsConfiscationTx(*tx))
        {
            // Not a confiscation transaction
            continue;
        }

        // Confiscation transaction must still be whitelisted at mempool height
        if(!frozenTXOCheck.CheckConfiscationTxWhitelisted(*tx, it->GetTime()))
        {
            GetDescendantsNL(it, nonWhitelistedConfiscationTxs);
        }

        // NOTE: It is assumed that confiscation transaction is otherwise valid (i.e. correct contents) otherwise it would not be in mempool.
        //       Since confiscation transaction is whitelisted, it is also guaranteed that all of its inputs are confiscated and therefore
        //       consensus frozen at all heights so we do not need to check this.
    }

    if(!nonWhitelistedConfiscationTxs.empty())
    {
        removeStagedNL(nonWhitelistedConfiscationTxs, nonNullChangeSet.Get(), noConflict, MemPoolRemovalReason::NOT_WHITELISTED);

        // We can use same mempool modification flag as for frozen transactions, because semantics are similar for confiscation transactions
        mFrozenTxnUpdatedAt = nTransactionsUpdated.load();
    }
}

void CTxMemPool::clearNL(bool skipTransactionDatabase/* = false*/) {
    evictionTracker.reset();
    mapLinks.clear();
    mapTx.clear();
    mapNextTx.clear();
    totalTxSize = 0;
    secondaryMempoolStats.Clear();
    cachedInnerUsage = 0;
    lastRollingFeeUpdate = GetTime();
    blockSinceLastRollingFeeBump = false;
    rollingMinimumFeeRate = 0;
    ++nTransactionsUpdated;
    mJournalBuilder.clearJournal();

    if (!skipTransactionDatabase && mempoolTxDB)
    {
        mempoolTxDB->Clear();
    }
}

void CTxMemPool::trackPackageRemovedNL(const CFeeRate &rate, bool haveSecondaryMempoolTxs) 
{
    if (rate.GetFeePerK().GetSatoshis() > rollingMinimumFeeRate) 
    {
        // if we still have secondary mempool transactions we should not bump the rolling fee
        // above blockMinTxfee
        if (haveSecondaryMempoolTxs && rate > blockMinTxfee)
        {
            rollingMinimumFeeRate = blockMinTxfee.GetFeePerK().GetSatoshis();
        }
        else
        {
            rollingMinimumFeeRate = rate.GetFeePerK().GetSatoshis();
        }
        blockSinceLastRollingFeeBump = false;
    }
}

void CTxMemPool::Clear() {
    std::unique_lock lock{smtx};
    clearNL();
}

void CTxMemPool::CheckMempool(
    CoinsDB& db,
    const mining::CJournalChangeSetPtr& changeSet) const
{

    if (ShouldCheckMempool())
    {
        CoinsDBView view{ db };
        std::shared_lock lock{smtx};
        CheckMempoolImplNL(view, changeSet);
    }
}

// A non-locking version of CheckMempool
void CTxMemPool::CheckMempoolNL(
    CoinsDBView& view,
    const mining::CJournalChangeSetPtr& changeSet) const
{

    if (ShouldCheckMempool())
    {
        CheckMempoolImplNL(view, changeSet);
    }
}

bool CTxMemPool::ShouldCheckMempool() const
{
    if (nCheckFrequency == 0) {
        return false;
    }

    if (suspendSanityCheck.load()) {
        return false;
    }

    if (GetRand(std::numeric_limits<uint32_t>::max()) >= nCheckFrequency) {
        return false;
    }
    return true;
}

void CTxMemPool::CheckMempoolImplNL(
    CoinsDBView& view,
    const mining::CJournalChangeSetPtr& changeSet) const
{
    CCoinsViewCache mempoolDuplicate{view};
    // Get spend height and MTP
    const auto [ nSpendHeight, medianTimePast] = GetSpendHeightAndMTP(mempoolDuplicate);

    {
        const auto primary = PrimaryMempoolSizeNL();
        const auto secondary = secondaryMempoolStats.Size();
        LogPrint(BCLog::MEMPOOL,
                 "Checking mempool with %lu transactions "
                 "(%lu primary, %lu secondary) and %zu inputs\n",
                 primary + secondary, primary, secondary,
                 size_t(mapNextTx.size()));
    }

    size_t primaryMempoolSize = 0;
    size_t secondaryMempoolSize = 0;

    uint64_t checkTotal = 0;
    uint64_t innerUsage = 0;

    std::list<const CTxMemPoolEntry *> waitingOnDependants;
    for (txiter it = mapTx.begin(); it != mapTx.end(); it++) {
        if(it->IsInPrimaryMempool())
        {
            ++primaryMempoolSize;
        }
        else
        {
            ++secondaryMempoolSize;
        }
        checkTotal += it->GetTxSize();
        innerUsage += it->DynamicMemoryUsage();
        const auto tx = it->GetSharedTx();
        txlinksMap::const_iterator linksiter = mapLinks.find(it);
        assert(linksiter != mapLinks.end());
        const TxLinks &links = linksiter->second;
        innerUsage += memusage::DynamicUsage(links.parents) +
                      memusage::DynamicUsage(links.children);
        bool fDependsWait = false;
        setEntries setParentCheck;
        size_t ancestorsCount = 0;
        size_t secondaryMempoolAncestorsCount = 0;
        for (const CTxIn &txin : tx->vin) {
            // Check that every mempool transaction's inputs refer to available
            // coins, or other mempool tx's.
            txiter it2 = mapTx.find(txin.prevout.GetTxId());
            if (it2 != mapTx.end()) {
                const auto tx2 = it2->GetSharedTx();
                assert(tx2->vout.size() > txin.prevout.GetN() &&
                       !tx2->vout[txin.prevout.GetN()].IsNull());
                fDependsWait = true;
                if (setParentCheck.insert(it2).second) {
                    ancestorsCount = std::max(ancestorsCount, it2->ancestorsCount + 1);
                    if(!it2->IsInPrimaryMempool())
                    {
                        secondaryMempoolAncestorsCount += 1;
                        secondaryMempoolAncestorsCount += it2->groupingData.value().ancestorsCount;
                    }
                }
            } else {
                assert(view.GetCoin(txin.prevout).has_value());
            }
            // Check whether its inputs are marked in mapNextTx.
            auto it3 = mapNextTx.find(txin.prevout);
            assert(it3 != mapNextTx.end());
            assert(it3->outpoint == txin.prevout);
            assert(it3->spentBy->GetTxId() == tx->GetId());
        }
        assert(setParentCheck == GetMemPoolParentsNL(it));
        assert(ancestorsCount == it->ancestorsCount);
        if(secondaryMempoolAncestorsCount)
        {
            assert(!it->IsInPrimaryMempool());
            assert(secondaryMempoolAncestorsCount == it->groupingData.value().ancestorsCount);
        }
        else
        {
            if(!it->IsInPrimaryMempool())
            {
                assert(secondaryMempoolAncestorsCount == 0);
            }
        }

        //TODO: check fee and other stuff aftrer groups are implemented

        // Check children against mapNextTx
        setEntries setChildrenCheck;

        const uint32_t outputCount = tx->vout.size();
        for(uint32_t ndx = 0; ndx < outputCount; ndx++)
        {
            auto nextit = mapNextTx.find(COutPoint{tx->GetId(), ndx});
            if(nextit != mapNextTx.end())
            {
                setChildrenCheck.insert(nextit->spentBy);
            }
        }
        assert(setChildrenCheck == GetMemPoolChildrenNL(it));

        if (fDependsWait) {
            waitingOnDependants.push_back(&(*it));
        } else {
            CFrozenTXOCheck frozenTXOCheck{
                chainActive.Tip()->GetHeight() + 1,
                "mempool",
                chainActive.Tip()->GetBlockHash(),
                it->GetTime()};

            CValidationState state;
            bool fCheckResult = tx->IsCoinBase() ||
                                Consensus::CheckTxInputs(
                                    *tx, state, mempoolDuplicate, nSpendHeight,
                                    frozenTXOCheck);
            assert(fCheckResult);
            UpdateCoins(*tx, mempoolDuplicate, 1000000);
        }

        // Check we haven't let any non-final txns in
        assert(IsFinalTx(*tx, nSpendHeight, medianTimePast));
    }

    assert(primaryMempoolSize == PrimaryMempoolSizeNL());
    assert(secondaryMempoolSize == secondaryMempoolStats.Size());

    unsigned int stepsSinceLastRemove = 0;
    while (!waitingOnDependants.empty()) {
        const CTxMemPoolEntry *entry = waitingOnDependants.front();
        waitingOnDependants.pop_front();
        CValidationState state;
        const auto entryTx = entry->GetSharedTx();
        if (!mempoolDuplicate.HaveInputs(*entryTx)) {
            waitingOnDependants.push_back(entry);
            stepsSinceLastRemove++;
            assert(stepsSinceLastRemove < waitingOnDependants.size());
        } else {
            CFrozenTXOCheck frozenTXOCheck{
                chainActive.Tip()->GetHeight() + 1,
                "mempool",
                chainActive.Tip()->GetBlockHash(),
                entry->GetTime()};

            bool fCheckResult =
                entryTx->IsCoinBase() ||
                Consensus::CheckTxInputs(*entryTx, state,
                                         mempoolDuplicate, nSpendHeight,
                                         frozenTXOCheck);
            assert(fCheckResult);
            UpdateCoins(*entryTx, mempoolDuplicate, 1000000);
            stepsSinceLastRemove = 0;
        }
    }

    for (const auto& item: mapNextTx) {
        const auto& txid = item.spentBy->GetTxId();
        const auto it2 = mapTx.find(txid);
        assert(it2 != mapTx.end());
        assert(it2->GetTxId() == txid);
    }

    assert(totalTxSize == checkTotal);
    assert(innerUsage == cachedInnerUsage);

    /* Journal checking. */
    // NOTE: Verify mapNextTx first because checkJournalNL() relies on it.
    if(changeSet)
    {
        // Check that the change set respects the toposort
        bool changeSetSorted { changeSet->CheckTopoSort() };
        assert(changeSetSorted);
        // Make journal consitent with mempool & check
        changeSet->apply();
        std::string journalResult { checkJournalNL() };
        assert(journalResult.empty());
    }

    // Check the mempool transaction database.
    assert(CheckMempoolTxDBNL());
}

bool CTxMemPool::CheckMempoolTxDBNL(bool hardErrors) const
{
#define ASSERT_OR_FAIL(condition) ASSERT_OR_FAIL_HELPER(condition, "", "")
#define ASSERT_OR_FAIL_TX(condition, txid) ASSERT_OR_FAIL_HELPER(condition, " / ", txid)
#define ASSERT_OR_FAIL_HELPER(condition, separator, txid)     \
    do {                                                      \
        if (!(condition)) {                                   \
            if (hardErrors) {                                 \
                assert(condition);                            \
            }                                                 \
            else {                                            \
                LogPrintf("Checking mempool TxDB: assertion failed: %s%s%s\n", \
                          #condition, separator, txid);       \
            }                                                 \
            return false;                                     \
        }                                                     \
    } while(0)

    mempoolTxDB->Sync();
    auto keys = mempoolTxDB->GetTxKeys();
    ASSERT_OR_FAIL(keys.size() == mempoolTxDB->GetTxCount());
    LogPrintf("Checking mempool TxDB: found %zu transactions\n", keys.size());
    uint64_t totalSize = 0;
    for (const auto& e : mapTx)
    {
        const auto key = keys.find(e.GetTxId());
        if (e.IsInMemory())
        {
            ASSERT_OR_FAIL_TX(key == keys.end(), e.GetTxId().ToString());
        }
        else
        {
            ASSERT_OR_FAIL_TX(key != keys.end(), e.GetTxId().ToString());
            keys.erase(key);
            totalSize += e.GetTxSize();
        }
    }
    ASSERT_OR_FAIL(keys.size() == 0);
    ASSERT_OR_FAIL(totalSize == mempoolTxDB->GetDiskUsage());
    return true;
#undef ASSERT_OR_FAIL
#undef ASSERT_OR_FAIL_TX
#undef ASSERT_OR_FAIL_HELPER
}

std::string CTxMemPool::CheckJournal() const {
    std::shared_lock lock{smtx};
    return checkJournalNL();
}

void CTxMemPool::ClearPrioritisation(const uint256 &hash) {
    std::unique_lock lock{smtx};
    clearPrioritisationNL(hash);
}

void CTxMemPool::ClearPrioritisation(const std::vector<TxId>& vTxIds) {
    if (vTxIds.empty()) {
        return;
    }
    std::unique_lock lock{smtx};
    for (const TxId& txid: vTxIds) {
        if (!ExistsNL(txid)) {
            clearPrioritisationNL(txid);
        }
    }
}

std::string CTxMemPool::checkJournalNL() const
{
    LogPrint(BCLog::JOURNAL, "Checking mempool against journal\n");
    std::stringstream res {};

    CJournalTester tester { mJournalBuilder.getCurrentJournal() };

    // Check mempool & journal agree on contents
    for(txiter it = mapTx.begin(); it != mapTx.end(); ++it)
    {
        // Check this mempool txn also appears in the journal
        const CJournalEntry entry { *it };
        const auto txid = entry.getTxn()->GetId().ToString();

        if(it->IsInPrimaryMempool() && !tester.checkTxnExists(entry))
        {
            res << "Txn " << txid << " is in the primary mempool but not the journal\n";
        }

        if(!it->IsInPrimaryMempool() && tester.checkTxnExists(entry))
        {
            res << "Txn " << txid << " is not in the primary mempool but it is in the journal\n";
        }

        if(it->IsInPrimaryMempool())
        {
            size_t countInputs = 0;
            for (auto [outpair, end] = mapNextTx.get<by_txiter>().equal_range(it);
                 outpair != end; ++outpair)
            {
                ++countInputs;
                if (const auto prevoutit = mapTx.find(outpair->outpoint.GetTxId());
                    prevoutit != mapTx.end())
                {
                    // Check this in mempool ancestor appears before its descendent in the journal
                    const CJournalEntry prevout { *prevoutit };
                    CJournalTester::TxnOrder order { tester.checkTxnOrdering(prevout, entry) };
                    if(order != CJournalTester::TxnOrder::BEFORE)
                    {
                        res << "Ancestor " << prevout.getTxn()->GetId().ToString()
                            << " of " << txid << " appears "
                            << enum_cast<std::string>(order) << " in the journal\n";
                    }
                }
            }

            if (countInputs == 0)
            {
                res << "Txn " << txid << " seems to have no inputs\n";
            }
        }
    }

    const auto result = res.str();
    LogPrint(BCLog::JOURNAL, "Result of journal check:%s\n%s",
             (result.empty() ? " Ok" : ""),
             (result.empty() ? "" : result.c_str()));
    return result;
}

// Rebuild the journal contents so they match the mempool
mining::CJournalChangeSetPtr CTxMemPool::RebuildMempool()
{
    LogPrint(BCLog::JOURNAL, "Rebuilding journal\n");

    CJournalChangeSetPtr changeSet { mJournalBuilder.getNewChangeSet(JournalUpdateReason::RESET) };
    {
        CoinsDBView coinsView{ *pcoinsTip };
        std::lock_guard lock{smtx};

        auto resubmitContext = PrepareResubmitContextAndClearNL(changeSet);
        // submit backed-up transactions
        ResubmitEntriesToMempoolNL(resubmitContext, changeSet);
        
        CheckMempoolNL(coinsView, changeSet);
    }
    return changeSet;
}

void CTxMemPool::SetSanityCheck(double dFrequency) {
    nCheckFrequency = dFrequency * 4294967295.0;
}

std::atomic_int CTxMemPool::mempoolTxDB_uniqueInit {0};

void CTxMemPool::OpenMempoolTxDB(const bool clearDatabase) {
    static constexpr auto cacheSize = 1 << 20; /*TODO: remove constant*/
    static constexpr auto hexDigits = int(2 * sizeof(mempoolTxDB_uniqueSuffix));
    std::call_once(
        db_initialized,
        [this, clearDatabase] {
            const auto dbName =
                (mempoolTxDB_unique
                 ? strprintf("mempoolTxDB-%0*X", hexDigits, mempoolTxDB_uniqueSuffix)
                 : std::string{"mempoolTxDB"});
            mempoolTxDB = std::make_shared<CAsyncMempoolTxDB>(
                GetDataDir() / dbName, cacheSize, mempoolTxDB_inMemory);
            if (clearDatabase) {
                mempoolTxDB->Clear();
            }
        });
}

void CTxMemPool::RemoveTxFromDisk(const CTransactionRef& transaction) {
    assert(!Exists(transaction->GetId()));
    OpenMempoolTxDB();
    mempoolTxDB->Remove({transaction->GetId(), transaction->GetTotalSize()});
}

uint64_t CTxMemPool::GetDiskUsage() {
    OpenMempoolTxDB();
    return mempoolTxDB->GetDiskUsage();
};

uint64_t CTxMemPool::GetDiskTxCount() {
    OpenMempoolTxDB();
    return mempoolTxDB->GetTxCount();
};

void CTxMemPool::SaveTxsToDisk(uint64_t requiredSize) {
    OpenMempoolTxDB();
    uint64_t movedToDiskSize = 0;
    {
        // For a discussion of interactions between writing transactions
        // to disk and transaction wrappers, see the comment at
        // CTransactionWrapper::GetTx() in tx_mempool_info.cpp and the 'add'
        // lambda in CAsyncMempoolTxDB::Work() in mempooltxdb.cpp.
        std::shared_lock lock{smtx};
        for (auto mi = mapTx.get<entry_time>().begin();
             mi != mapTx.get<entry_time>().end() && movedToDiskSize < requiredSize;
             ++mi) {
            if (mi->IsInMemory()) {
                auto tx = mi->tx;
                mempoolTxDB->Add(std::move(tx));
                movedToDiskSize += mi->GetTxSize();
            }
        }
    }

    if (movedToDiskSize < requiredSize)
    {
        LogPrint(BCLog::MEMPOOL,
                 "Less than required amount of memory was freed. Required: %d,  freed: %d\n",
                 requiredSize, movedToDiskSize);
    }
}

void CTxMemPool::QueryHashes(std::vector<uint256> &vtxid) {
    std::shared_lock lock{smtx};

    vtxid.clear();
    vtxid.reserve(mapTx.size());

    for (const auto& entry : mapTx.get<insertion_order>()) {
        vtxid.emplace_back(entry.GetTxId());
    }
}

std::vector<TxMempoolInfo> CTxMemPool::InfoAll() const {
    std::shared_lock lock{smtx};
    return InfoAllNL();
}

std::vector<TxMempoolInfo> CTxMemPool::InfoAllNL() const {
    std::vector<TxMempoolInfo> ret;
    ret.reserve(mapTx.size());
    for (const auto& entry : mapTx.get<insertion_order>()) {
        ret.emplace_back(TxMempoolInfo{entry});
    }
    return ret;
}

CTransactionRef CTxMemPool::Get(const uint256 &txid) const {
    std::shared_lock lock{smtx};
    return GetNL(txid);
}

CTransactionRef CTxMemPool::GetNL(const uint256 &txid) const {
    txiter i = mapTx.find(txid);
    if (i == mapTx.end()) {
        return nullptr;
    }
    return i->GetSharedTx();
}

TxMempoolInfo CTxMemPool::Info(const uint256 &txid) const {
    std::shared_lock lock{smtx};
    txiter i = mapTx.find(txid);
    if (i == mapTx.end()) {
        return TxMempoolInfo();
    }

    return TxMempoolInfo{*i};
}

CFeeRate CTxMemPool::estimateFee() const {
    uint64_t maxMempoolSize =
        GlobalConfig::GetConfig().GetMaxMempool();

    // return maximum of min fee per KB from config, min fee calculated from mempool 
    return std::max(GlobalConfig::GetConfig().GetMinFeePerKB(), GetMinFee(maxMempoolSize));

  }


void CTxMemPool::PrioritiseTransaction(
    const uint256& hash,
    const std::string& strHash,
    const Amount nFeeDelta) {

    {
        std::unique_lock lock{smtx};
        prioritiseTransactionNL(hash, nFeeDelta);
    }
    LogPrint(BCLog::MEMPOOL, "PrioritiseTransaction: %s fee += %d\n", strHash, FormatMoney(nFeeDelta));
}

void CTxMemPool::PrioritiseTransaction(
    const std::vector<TxId>& vTxToPrioritise,
    const Amount nFeeDelta) {

    if (vTxToPrioritise.empty()) {
        return;
    }
    {
        std::unique_lock lock{smtx};
        for(const TxId& txid: vTxToPrioritise) {
            prioritiseTransactionNL(txid, nFeeDelta);
        }
    }
    for(const TxId& txid: vTxToPrioritise) {
        LogPrint(BCLog::MEMPOOL, "PrioritiseTransaction: %s fee += %d\n",
            txid.ToString(),
            FormatMoney(nFeeDelta));
    }
}

void CTxMemPool::ApplyDeltas(const uint256& hash, Amount &nFeeDelta) const 
{
    std::shared_lock lock{smtx};
    ApplyDeltasNL(hash, nFeeDelta);
}

void CTxMemPool::ApplyDeltasNL(
        const uint256& hash,
        Amount &nFeeDelta) const 
{
    const auto pos = mapDeltas.find(hash);
    if (pos == mapDeltas.end()) {
        return;
    }
    nFeeDelta += pos->second;
}

void CTxMemPool::prioritiseTransactionNL(
    const uint256& hash,
    const Amount nFeeDelta)
{
    auto& delta = mapDeltas[hash];
    delta = std::min(MAX_MONEY, delta + nFeeDelta); // do not allow bigger delta than MAX_MONEY
    txiter it = mapTx.find(hash);
    if (it != mapTx.end())
    {
        mapTx.modify(it, update_fee_delta(delta));
        TrackEntryModified(it);

        // Ensure CPFP groups maintain correct average fee calculations across the group
        auto changeSet = mJournalBuilder.getNewChangeSet(JournalUpdateReason::PRIORITISATION);
        setEntriesTopoSorted entries {it};

        if(it->IsInPrimaryMempool())
        {
            // If this txn is a member of a CPFP group in the main mempool, disband and recreate the group
            // with accurately calculated new fees.
            // Also, if we're reducing the fees on any txn in the main mempool then remove and re-add it
            // because it may no longer be in the primary pool.
            if(it->IsCPFPGroupMember() || nFeeDelta < Amount{0})
            {
                entries = RemoveFromPrimaryMempoolNL(entries, *changeSet, false);
                TryAcceptToPrimaryMempoolNL(std::move(entries), *changeSet, false);
            }
        }
        else
        {
            TryAcceptToPrimaryMempoolNL(std::move(entries), *changeSet, false);
        }
    }
}

void CTxMemPool::clearPrioritisationNL(const uint256& hash) {
    mapDeltas.erase(hash);
}


void CTxMemPool::GetDeltasAndInfo(std::map<uint256, Amount>& deltas,
                                  std::vector<TxMempoolInfo>& info) const
{
    std::shared_lock lock {smtx};
    deltas = mapDeltas;
    info = InfoAllNL();
}


bool CTxMemPool::HasNoInputsOf(const CTransaction &tx) const {
    std::shared_lock lock{smtx};
    for (const CTxIn &in : tx.vin) {
        if (ExistsNL(in.prevout.GetTxId())) {
            return false;
        }
    }
    return true;
}

void CTxMemPool::OnUnspentCoinsWithScript(
    const CoinsDBView& tip,
    const std::vector<COutPoint>& outpoints,
    const std::function<void(const CoinWithScript&, size_t)>& callback) const
{
    std::shared_lock lock{ smtx };

    CoinsViewLockedMemPoolNL viewMemPool{ *this, tip };

    std::size_t idx = 0;

    for(const auto& out : outpoints)
    {
        if (!IsSpentNL( out ))
        {
            if (auto coin = viewMemPool.GetCoinWithScript( out );
                coin.has_value() && !coin->IsSpent())
            {
                callback( coin.value(), idx );
            }
        }

        ++idx;
    }
}

CCoinsViewMemPool::CCoinsViewMemPool(const CoinsDBView& DBView,
                                     const CTxMemPool &mempoolIn)
    : mempool(mempoolIn)
    , mDBView{DBView}
{}

std::optional<Coin> CCoinsViewMemPool::GetCoinFromDB(const COutPoint& outpoint) const
{
    if(auto coin = mDBView.GetCoin(outpoint, 0); coin.has_value())
    {
        return Coin{coin.value()};
    }

    return {};
}

uint256 CCoinsViewMemPool::GetBestBlock() const
{
    return mDBView.GetBestBlock();
}

CTransactionRef CCoinsViewMemPool::GetCachedTransactionRef(const COutPoint& outpoint) const
{
    std::unique_lock lock{mMutex};

    // Local cache makes sure that once we read the coin we have guaranteed
    // coin stability until the provider is destroyed even in case mempool
    // changes during task execution.
    if (auto it = mCache.find(outpoint.GetTxId()); it != mCache.end())
    {
        return it->second;
    }

    CTransactionRef tx = mempool.Get(outpoint.GetTxId());

    if (tx)
    {
        mCache.emplace(outpoint.GetTxId(), tx);
    }

    return tx;
}

std::optional<CoinImpl> CCoinsViewMemPool::GetCoin(const COutPoint &outpoint, uint64_t maxScriptSize) const
{
    // If an entry in the mempool exists, always return that one, as it's
    // guaranteed to never conflict with the underlying view, and it cannot
    // have pruned entries (as it contains full) transactions. First checking
    // the underlying provider risks returning a pruned entry instead.
    CTransactionRef ptx = GetCachedTransactionRef(outpoint);
    if (ptx) {
        if (outpoint.GetN() < ptx->vout.size()) {
            return CoinImpl::MakeNonOwningWithScript(ptx->vout[outpoint.GetN()], MEMPOOL_HEIGHT, false, CFrozenTXOCheck::IsConfiscationTx(*ptx));
        }
        return {};
    }

    return mDBView.GetCoin(outpoint, maxScriptSize);
}

void CCoinsViewMemPool::CacheAllCoins(const std::vector<CTransactionRef>& txns) const
{
    mDBView.CacheAllCoins(txns);
}

size_t CTxMemPool::DynamicMemoryUsage() const {
    std::shared_lock lock{smtx};
    return DynamicMemoryUsageNL();
}

size_t CTxMemPool::DynamicMemoryIndexUsageNL() const {
    // Estimate the overhead of mapTx to be 12 pointers + two allocations, as
    // no exact formula for boost::multi_index_container is implemented.
    return mapTx.size() * memusage::MallocUsage(sizeof(CTxMemPoolEntry) +
                                                sizeof(CTransactionWrapper) +
                                                12 * sizeof(void *)) +
           mapNextTx.size() * memusage::MallocUsage(sizeof(OutpointTxPair) +
                                                    12 * sizeof(void *)) +
           memusage::DynamicUsage(mapDeltas) +
           memusage::DynamicUsage(mapLinks);
}

size_t CTxMemPool::DynamicMemoryUsageNL() const {
    return DynamicMemoryIndexUsageNL() + cachedInnerUsage;
}

size_t CTxMemPool::SecondaryMempoolUsage() const {
    std::shared_lock lock{smtx};
    return SecondaryMempoolUsageNL();
}

size_t CTxMemPool::SecondaryMempoolUsageNL() const {
    // assume secondary mempool entries consume a proportional amount of index space.
    // worst case is secondary mempool entries consume more index which will slightly
    // increase memory pressure for primary mempool writeout instead of triggering
    // eviction from the secondary mempool
    double secondaryMempoolRatio = static_cast<double>(secondaryMempoolStats.Size()) / (mapTx.size() + 1);
    size_t indexSize = DynamicMemoryIndexUsageNL();
    return indexSize * secondaryMempoolRatio + secondaryMempoolStats.InnerUsage();
}

void CTxMemPool::removeStagedNL(
    setEntries& stage,
    mining::CJournalChangeSet& changeSet,
    const CTransactionConflict& conflictedWith,
    MemPoolRemovalReason reason)
{
    // first remove groups from primary mempool because when we remove groups we might need 
    // to remove transactions which are not in the staged for deletion
    setEntriesTopoSorted toRemoveFromPrimaryMempool;
    for(auto it: stage)
    {
        if(it->IsCPFPGroupMember())
        {
            toRemoveFromPrimaryMempool.insert(it);
        }
        
    }
    
    // collect transactions which are removed from the primary mempool as consequence of removing groups
    // we will revisit these transactions when we remove all staged transactions
    setEntriesTopoSorted toUpdateAfterDeletion = RemoveFromPrimaryMempoolNL(toRemoveFromPrimaryMempool, changeSet, true);
    
    for(auto it: stage)
    {
        // cut connections to the transactions which remain in the mempool
        for(auto parent: GetMemPoolParentsNL(it))
        {
            if(stage.find(parent) == stage.end())
            {
                // remove "it" from "parents" children list
                updateChildNL(parent, it, false);
            }
        }

        // we don't want to update transactions which will not be in the mempool any more
        if(!toUpdateAfterDeletion.empty())
        {
            toUpdateAfterDeletion.erase(it);
        }
        
        if(nCheckFrequency)
        {
            // check that every child is staged for removal
            for(auto child: GetMemPoolChildrenNL(it))
            {
                assert(stage.find(child) != stage.end());
            }
        }
    }

    // now actually remove transactions
    removeUncheckedNL(stage, changeSet, conflictedWith, reason);

    // check if removed transactions can be re-accepted to the primary mempool
    TryAcceptToPrimaryMempoolNL(std::move(toUpdateAfterDeletion), changeSet, false);
}

int CTxMemPool::Expire(int64_t time, const mining::CJournalChangeSetPtr& changeSet)
{
    std::unique_lock lock{smtx};
    indexed_transaction_set::index<entry_time>::type::iterator it =
        mapTx.get<entry_time>().begin();
    setEntries toremove;
    while (it != mapTx.get<entry_time>().end() && it->GetTime() < time) {
        toremove.insert(mapTx.project<transaction_id>(it));
        it++;
    }

    setEntries stage;
    for (txiter removeit : toremove) {
        GetDescendantsNL(removeit, stage);
    }

    CEnsureNonNullChangeSet nonNullChangeSet(*this, changeSet);
    removeStagedNL(stage, nonNullChangeSet.Get(), noConflict, MemPoolRemovalReason::EXPIRY);
    return stage.size();
}

int CTxMemPool::RemoveTxAndDescendants(const TxId & txid, const mining::CJournalChangeSetPtr& changeSet)
{
    std::unique_lock lock{smtx};
    auto it = mapTx.get<transaction_id>().find(txid);
    if (it != mapTx.get<transaction_id>().end()) {
        setEntries stage;
        stage.insert(it);
        GetDescendantsNL(it, stage);
        CEnsureNonNullChangeSet nonNullChangeSet(*this, changeSet);
        removeStagedNL(stage, nonNullChangeSet.Get(), noConflict, MemPoolRemovalReason::EXPIRY);
        return stage.size();
    }
    return 0;
}

std::vector<TxId> CTxMemPool::RemoveTxnsAndDescendants(const std::vector<TxId>& txids, const mining::CJournalChangeSetPtr& changeSet)
{
    std::unique_lock lock{smtx};
    setEntries stage;
    std::vector<TxId> stagedIds;
    for (const TxId& txid: txids) {
        auto it = mapTx.get<transaction_id>().find(txid);
        if (it != mapTx.get<transaction_id>().end()) {
            stage.insert(it);
            stagedIds.push_back(it->GetTxId());
            GetDescendantsNL(it, stage);
        }
    }
    if (!stage.empty()) {
        CEnsureNonNullChangeSet nonNullChangeSet(*this, changeSet);
        removeStagedNL(stage, nonNullChangeSet.Get(), noConflict, MemPoolRemovalReason::EXPIRY);
    }
    return stagedIds;
}

std::set<CTransactionRef> CTxMemPool::CheckTxConflicts(const CTransactionRef& tx, bool isFinal) const
{
    std::shared_lock lock{smtx};
    std::set<CTransactionRef> conflictsWith;

    // Check our locked UTXOs
    for (const CTxIn &txin : tx->vin) {
        if (auto it = mapNextTx.find(txin.prevout); it != mapNextTx.end()) {
            conflictsWith.insert(GetNL(it->spentBy->GetTxId()));
        }
    }

    if(isFinal)
    {
        // Check non-final pool locked UTXOs
        auto tlConflictsWith = mTimeLockedPool.checkForDoubleSpend(tx);

        if(!tlConflictsWith.empty() && !mTimeLockedPool.finalisesExistingTransaction(tx))
        {
            conflictsWith.merge( std::move(tlConflictsWith) );
        }
    }

    return conflictsWith;
}

CTxMemPool::ResubmitContext CTxMemPool::PrepareResubmitContextAndClearNL(const CJournalChangeSetPtr &changeSet)
{
    ResubmitContext resubmitContext;
    // we are about to delete journal, changes in the changeSet no sense now
    if(changeSet)
    {
        changeSet->clear();
    }

    for (auto iter = mapTx.begin(); iter != mapTx.end(); ++iter)
    {
        if(!iter->IsInMemory())
        {
            resubmitContext.outpointsSpentByStored.emplace(iter->GetTxId(), GetOutpointsSpentByNL(iter));
        }
    }

    std::swap(resubmitContext.oldMapTx, mapTx);
    clearNL(true);          // Do not clear the transaction database
    return resubmitContext;
}

void CTxMemPool::ResubmitEntriesToMempoolNL(CTxMemPool::ResubmitContext& resubmitContext, const CJournalChangeSetPtr& changeSet)
{
    auto& tempMapTxSequenced = resubmitContext.oldMapTx.get<insertion_order>();
    for (auto itTemp = tempMapTxSequenced.begin(); itTemp != tempMapTxSequenced.end();)
    {
        tempMapTxSequenced.modify(
            itTemp,
            [](CTxMemPoolEntry& entry)
            {
                entry.groupingData = std::nullopt;
                entry.group.reset();
            }
        );

        SpentOutputs spentOutputs;
        if (auto foundOutpoints = resubmitContext.outpointsSpentByStored.find(itTemp->GetTxId());
            foundOutpoints != resubmitContext.outpointsSpentByStored.end())
        {
            spentOutputs = foundOutpoints->second;
        }
        AddUncheckedNL(itTemp->GetTxId(), *itTemp, itTemp->GetTxStorage(), changeSet, spentOutputs);
        tempMapTxSequenced.erase(itTemp++);
    }
}

void CTxMemPool::AddToMempoolForReorg(const Config &config,
    DisconnectedBlockTransactions &disconnectpool,
    const CJournalChangeSetPtr& changeSet)
{
    // NOTE: cs_main lock is needed because this function is not completely thread safe.
    //       smtx is released for tx validation which causes a gap of mempool stability.
    //.      During that time other functions that are using mempool with intent to add
    //.      new transactions to it must not run (they would cause mempool invariance break)
    //.      and that is guaranteed by abusing cs_main lock.
    AssertLockHeld(cs_main);
    TxInputDataSPtrVec vTxInputData {};

    CEnsureNonNullChangeSet nonNullChangeSet{*this, changeSet};
    ResubmitContext resubmitContext;

    {
        std::unique_lock lock{smtx};

        // disconnectpool's insertion_order index sorts the entries from oldest to
        // newest, but the oldest entry will be the last tx from the latest mined
        // block that was disconnected.
        // Iterate disconnectpool in reverse, so that we add transactions back to
        // the mempool starting with the earliest transaction that had been
        // previously seen in a block.
        for (auto it = disconnectpool.queuedTx.get<insertion_order>().rbegin();
             it != disconnectpool.queuedTx.get<insertion_order>().rend();
             ++it)
        {
            // Filter out coinbase and special miner ID txns
            if(disconnectpool.isFiltered((*it)->GetId())) {
                // If the transaction doesn't make it in to the mempool, remove any
                // transactions that depend on it (which would now be orphans).
                removeRecursiveNL(**it, changeSet, noConflict, MemPoolRemovalReason::REORG);
            }
            else {
                vTxInputData.emplace_back(
                    std::make_shared<CTxInputData>(
                        TxIdTrackerWPtr{}, // TxIdTracker is not used during reorgs
                        *it,              // a pointer to the tx
                        TxSource::reorg,  // tx source
                        TxValidationPriority::normal,  // tx validation priority
                        TxStorage::memory, // tx storage
                        GetTime()));        // nAcceptTime
            }
        }

        disconnectpool.queuedTx.clear();

        // Clear the mempool, but save the current index, mapNextTx, entries and the
        // transaction database, since we'll re-add the entries later.
        resubmitContext = PrepareResubmitContextAndClearNL(changeSet);
    }

    // Validate the set of transactions from the disconnectpool and add them to the mempool
    g_connman->getTxnValidator()->processValidation(vTxInputData, changeSet, true);

    // Add original mempool contents on top to preserve toposort
    {
        std::unique_lock lock {smtx};

        // now put all transactions that were in the mempool before
        ResubmitEntriesToMempoolNL(resubmitContext, changeSet);

        // Disconnectpool related updates
        for (const auto& txInputData : vTxInputData) {
            const auto& tx = txInputData->GetTxnPtr();
            if (!ExistsNL(tx->GetId())) {
                // If the transaction doesn't make it in to the mempool, remove any
                // transactions that depend on it (which would now be orphans).
                removeRecursiveNL(*tx, changeSet, noConflict, MemPoolRemovalReason::REORG);
            }
        }

        // We also need to remove any now-immature transactions
        LogPrint(BCLog::MEMPOOL, "Removing any now-immature transactions\n");
        const CBlockIndex& tip = *chainActive.Tip();
        RemoveForReorgNL(
                config,
                *pcoinsTip,
                changeSet,
                tip,
                StandardNonFinalVerifyFlags(IsGenesisEnabled(config, tip.GetHeight())));

        if(tip.GetHeight() + 1 < CFrozenTXOCheck::Get_max_FrozenTXOData_enforceAtHeight_stop())
        {
            // Remove any transactions from mempool that spend TXOs, which were previously not considered policy frozen, but now are.
            // Note that this can only happen if all of the following is true:
            //   - TXO is consensus frozen up to (and not including) height H with policyExpiresWithConsensus=true.
            //   - Transaction spending this TXO was added to mempool when mempool height was H or above.
            //   - Active chain was reorged back so that mempool height is now below H.
            // NOTE: To avoid re-checking whole mempool every time, we only do this if it is theoretically possible that mempool could
            //       contain such transactions. Specifically, if maximum height, at which any consensus frozen TXO is un-frozen,
            //       is below or at current mempool height, there is simply no such TXO and we can safely skip the expensive re-check.
            LogPrint(BCLog::MEMPOOL, "Removing any transactions that spend TXOs, which were previously not considered policy frozen, but now are because the mempool height has become lower.\n");
            RemoveFrozenNL(changeSet);
        }

        if(tip.GetHeight() + 1 < CFrozenTXOCheck::Get_max_WhitelistedTxData_enforceAtHeight())
        {
            // Remove any confiscation transactions that are now no longer valid.
            // Here we use the same trick as in the case of regular transaction spending frozen TXO to avoid
            // unneeded scans. If maximum enforceAtHeight of all confiscation transactions is at or below current
            // mempool height, it is not possible that a confiscation transaction in mempool has become invalid.
            LogPrint(BCLog::MEMPOOL, "Removing any confiscation transactions, which were previously valid, but are now not because the mempool height has become lower.\n");
            RemoveInvalidCTXsNL(changeSet);
        }
    }

    // Check mempool & journal
    CheckMempool(*pcoinsTip, changeSet);

    // Mempool is now consistent. Synchronize with journal.
    changeSet->apply();
}


void CTxMemPool::RemoveFromMempoolForReorg(const Config &config,
    DisconnectedBlockTransactions &disconnectpool,
    const CJournalChangeSetPtr& changeSet)
{
    AssertLockHeld(cs_main);

    {
        std::unique_lock lock {smtx};

        // disconnectpool's insertion_order index sorts the entries from oldest to
        // newest, but the oldest entry will be the last tx from the latest mined
        // block that was disconnected.
        // Iterate disconnectpool in reverse, so that we add transactions back to
        // the mempool starting with the earliest transaction that had been
        // previously seen in a block.
        auto it = disconnectpool.queuedTx.get<insertion_order>().rbegin();
        while (it != disconnectpool.queuedTx.get<insertion_order>().rend()) {
            removeRecursiveNL(**it, changeSet, noConflict, MemPoolRemovalReason::REORG);
            ++it;
        }

        disconnectpool.queuedTx.clear();
        // We also need to remove any now-immature transactions
        LogPrint(BCLog::MEMPOOL, "Removing any now-immature transactions\n");

        const CBlockIndex& tip = *chainActive.Tip();
        RemoveForReorgNL(
            config,
            *pcoinsTip,
            changeSet,
            tip,
            StandardNonFinalVerifyFlags(IsGenesisEnabled(config, tip.GetHeight())));
    }

    // Check mempool & journal
    CheckMempool(*pcoinsTip, changeSet);

    // Mempool is now consistent. Synchronize with journal.
    changeSet->apply();
}

void CTxMemPool::AddToDisconnectPoolUpToLimit(
    const mining::CJournalChangeSetPtr& changeSet,
    DisconnectedBlockTransactions* disconnectpool,
    uint64_t maxDisconnectedTxPoolSize,
    const CBlock& block,
    int32_t height)
{
    // If this is a miner ID enabled block, there may be txns contained in it
    // we should filter out from the mempool.
    std::optional<MinerId> minerID { FindMinerId(block, height) };
    std::set<TxId> dataRefIds {};
    if(minerID)
    {
        auto index = g_dataRefIndex->CreateLockingAccess();
        const std::optional<TxId> & minerInfoTxId = minerID->GetMinerInfoTx();
        if (minerInfoTxId) {
            // will do nothing if it is not in the database
            index.DeleteMinerInfoTxn(*minerInfoTxId);
        }
        if(minerID->GetCoinbaseDocument().GetDataRefs())
        {
            // Build set of dataref txids for speedy lookup
            for(const auto& dataref : minerID->GetCoinbaseDocument().GetDataRefs().value())
            {
                dataRefIds.insert(dataref.txid);
                index.DeleteDatarefTxn(dataref.txid);
            }
        }
    }

    for(const auto& tx : boost::adaptors::reverse(block.vtx))
    {
        bool filter {false};
        if(minerID)
        {
            const auto& txid { tx->GetId() };
            // Filter miner ID miner-info and dataref txns
            const auto& minerInfoTx { minerID->GetMinerInfoTx() };
            if(minerInfoTx)
            {
                filter |= (minerInfoTx == txid);
            }
            filter |= (dataRefIds.count(txid) > 0);
        }

        // Also filter coinbase txns
        disconnectpool->addTransaction(tx, filter || tx->IsCoinBase());
    }

    // FIXME: SVDEV-460 add only upto limit and drop the rest. Figure out all this reversal and what to drop

    std::unique_lock lock{ smtx };

    while(disconnectpool->DynamicMemoryUsage() > maxDisconnectedTxPoolSize)
    {
        // Drop the earliest entry, and remove its children from the
        // mempool.
        auto it = disconnectpool->queuedTx.get<insertion_order>().begin();
        removeRecursiveNL(**it, changeSet, noConflict, MemPoolRemovalReason::REORG);
        disconnectpool->removeEntry(it);
    }
}

namespace {
    enum class WhatToDoWithTheEntry : bool
    {
        INSERT = true,
        ERASE = false
    };

    template <typename Accumulator, typename Container, typename Entry>
    void UpdateMemoryUsage(Accumulator& accumulator, Container& entries, Entry entry,
                           WhatToDoWithTheEntry operation)
    {
        const auto before = memusage::DynamicUsage(entries);
        if ((operation == WhatToDoWithTheEntry::INSERT && entries.insert(entry).second)
            ||
            (operation == WhatToDoWithTheEntry::ERASE && entries.erase(entry)))
        {
            // The correctness of the size computation below relies on
            // well-defined unsigned integer overflow.
            static_assert(std::is_integral<Accumulator>::value &&
                          std::is_unsigned<Accumulator>::value);

            accumulator += memusage::DynamicUsage(entries);
            accumulator -= before;
        }
    }
}

void CTxMemPool::updateChildNL(txiter entry, txiter child, bool add) {
    UpdateMemoryUsage(cachedInnerUsage, mapLinks[entry].children, child,
                      static_cast<WhatToDoWithTheEntry>(add));
}

void CTxMemPool::updateParentNL(txiter entry, txiter parent, bool add) {
    UpdateMemoryUsage(cachedInnerUsage, mapLinks[entry].parents, parent,
                      static_cast<WhatToDoWithTheEntry>(add));
}

const CTxMemPool::setEntries &
CTxMemPool::GetMemPoolParentsNL(txiter entry) const {
    assert(entry != mapTx.end());
    txlinksMap::const_iterator it = mapLinks.find(entry);
    assert(it != mapLinks.end());
    return it->second.parents;
}

const CTxMemPool::setEntries &
CTxMemPool::GetMemPoolChildrenNL(txiter entry) const {
    assert(entry != mapTx.end());
    txlinksMap::const_iterator it = mapLinks.find(entry);
    assert(it != mapLinks.end());
    return it->second.children;
}

bool CTxMemPool::SetRollingMinFee(int64_t fee)
{ 
    if(fee < MIN_ROLLING_FEE_HALFLIFE || fee > MAX_ROLLING_FEE_HALFLIFE)
        return false;

    halflife_ = fee;
    return true;
}

CFeeRate CTxMemPool::GetMinFee(size_t sizelimit) const 
{
    std::lock_guard lock{smtx};
    if (blockSinceLastRollingFeeBump && rollingMinimumFeeRate != 0) {
        int64_t time = GetTime();
        if (time > lastRollingFeeUpdate + 10) {
            double halflife = halflife_;
            if (DynamicMemoryUsageNL() < sizelimit / 4) {
                halflife /= 4;
            } else if (DynamicMemoryUsageNL() < sizelimit / 2) {
                halflife /= 2;
            }

            rollingMinimumFeeRate =
                rollingMinimumFeeRate /
                pow(2.0, (time - lastRollingFeeUpdate) / halflife);
            lastRollingFeeUpdate = time;
        }
    }
    // minDebugRejectionFee is always zero in mainnet
    return std::max(
        CFeeRate(Amount(int64_t(rollingMinimumFeeRate))),
        GetMinDebugRejectionFee());
}

int64_t CTxMemPool::evaluateEvictionCandidateNL(txiter entry)
{
    if(entry->IsCPFPGroupMember())
    {
        const auto& evalParams = entry->GetCPFPGroup()->EvaluationParams();
        return (evalParams.fee + evalParams.feeDelta).GetSatoshis() * 1000 / evalParams.size;
    }
    
    int64_t score = entry->GetModifiedFee().GetSatoshis() * 1000 / entry->GetTxSize();
    if(!entry->IsInPrimaryMempool())
    {
        // tx is secondary mempool, decrement its score
        score += std::numeric_limits<int64_t>::min();
    }
    return score;

}

void CTxMemPool::TrackEntryAdded(CTxMemPool::txiter entry)
{
    if (evictionTracker)
    {
        evictionTracker->EntryAdded(entry);
    }
}

void CTxMemPool::TrackEntryRemoved(const TxId& txId, const setEntries& immediateParents)
{
    if (evictionTracker)
    {
        evictionTracker->EntryRemoved(txId, immediateParents);
    }
}

void CTxMemPool::TrackEntryModified(CTxMemPool::txiter entry)
{
    if (evictionTracker)
    {
        evictionTracker->EntryModified(entry);
    }
}

std::vector<COutPoint> CTxMemPool::GetOutpointsSpentByNL(CTxMemPool::txiter entry) const
{
    std::vector<COutPoint> toReturn;
    for(auto [it, itEnd] = mapNextTx.get<by_txiter>().equal_range(entry); it != itEnd; it++)
    {
        toReturn.push_back(it->outpoint);
    }
    return toReturn;
}

std::vector<TxId> CTxMemPool::TrimToSize(
    size_t sizelimit,
    const mining::CJournalChangeSetPtr& changeSet,
    std::vector<COutPoint>* pvNoSpendsRemaining)
{
    std::unique_lock lock{smtx};

    unsigned nTxnRemoved = 0;
    CFeeRate maxFeeRateRemoved{Amount{0}};
    std::vector<TxId> vRemovedTxIds {};

    if (!evictionTracker) {
        evictionTracker = std::make_shared<CEvictionCandidateTracker>(
            mapLinks,
            [](txiter entry)
            {
                return evaluateEvictionCandidateNL(entry);
            });
    }

    auto getFeeRate = [](txiter entry)
    {
        if(entry->IsCPFPGroupMember())
        {
            const auto& groupParams = entry->GetCPFPGroup()->EvaluationParams();
            return CFeeRate(groupParams.fee + groupParams.feeDelta, groupParams.size);
        }
        return CFeeRate(entry->GetModifiedFee(), entry->GetTxSize());
    };

    CEnsureNonNullChangeSet nonNullChangeSet(*this, changeSet);
    bool weHaveEvictedSomething = false;
    while (!mapTx.empty() && DynamicMemoryUsageNL() > sizelimit) {
        const auto it = evictionTracker->GetMostWorthless();

        // We set the new mempool min fee to the feerate of the removed set,
        // plus the "minimum reasonable fee rate" (ie some value under which we
        // consider txn to have 0 fee). This way, we don't allow txn to enter
        // mempool with feerate equal to txn which were removed with no block in
        // between.

        CFeeRate removed = getFeeRate(it);
        maxFeeRateRemoved = std::max(maxFeeRateRemoved, removed);

        setEntries stage;
        GetDescendantsNL(it, stage);
        nTxnRemoved += stage.size();


        std::vector<COutPoint> unspentOutpoints;
        if (pvNoSpendsRemaining) {
            for (txiter iter : stage) {
                vRemovedTxIds.emplace_back(iter->GetTxId());
                // find and collect outpoints spent by "iter"
                auto [itBegin, itEnd] = mapNextTx.get<by_txiter>().equal_range(iter);
                std::transform(itBegin, itEnd, std::back_inserter(unspentOutpoints),
                               [](const auto& value){return value.outpoint;});
            }
        }
        removeStagedNL(stage, nonNullChangeSet.Get(), noConflict, MemPoolRemovalReason::SIZELIMIT);
        if (pvNoSpendsRemaining) {
            for (const auto& outpoint : unspentOutpoints) {
                if (ExistsNL(outpoint.GetTxId())) {
                    continue;
                }
                if (!mapNextTx.count(outpoint)) {
                    pvNoSpendsRemaining->push_back(outpoint);
                }
            }
        }
        weHaveEvictedSomething = true;
    }

    if (weHaveEvictedSomething) 
    {
        if(mapTx.size() != 0)
        {
            auto nextToBeRemoved = evictionTracker->GetMostWorthless();
            maxFeeRateRemoved = std::max(maxFeeRateRemoved, getFeeRate(nextToBeRemoved));
            maxFeeRateRemoved += MEMPOOL_FULL_FEE_INCREMENT;
            
            bool haveSecondaryMempoolTransactions = !nextToBeRemoved->IsInPrimaryMempool();
            
            trackPackageRemovedNL(maxFeeRateRemoved, haveSecondaryMempoolTransactions);
        }
        else
        {
            trackPackageRemovedNL(CFeeRate(), false);
        }

        LogPrint(BCLog::MEMPOOL,
                 "Removed %u txn, rolling minimum fee bumped to %s\n",
                 nTxnRemoved, maxFeeRateRemoved.ToString());
    }
    return vRemovedTxIds;
}

bool CTxMemPool::TransactionWithinChainLimit(const uint256 &txid, int64_t maxAncestorCount, 
                                             int64_t maxSecondaryMempoolAncestorCount) const 
{
    std::shared_lock lock{smtx};
    auto it = mapTx.find(txid);
    if(it == mapTx.end())
    {
        return true;
    }

    if(it->IsInPrimaryMempool())
    {
        return static_cast<int64_t>(it->ancestorsCount) < maxAncestorCount; 
    }

    return (static_cast<int64_t>(it->ancestorsCount) < maxAncestorCount) &&
           (static_cast<int64_t>(it->groupingData->ancestorsCount) < maxSecondaryMempoolAncestorCount);
}

unsigned long CTxMemPool::Size() {
    std::shared_lock lock{smtx};
    return mapTx.size();
}

unsigned long CTxMemPool::PrimaryMempoolSizeNL() const {
    return mapTx.size() - secondaryMempoolStats.Size();
}

uint64_t CTxMemPool::GetTotalTxSize() {
    std::shared_lock lock{smtx};
    return totalTxSize;
}

bool CTxMemPool::Exists(const uint256& hash) const {
    std::shared_lock lock{smtx};
    return ExistsNL(hash);
}

bool CTxMemPool::ExistsNL(const uint256& hash) const {
    return mapTx.count(hash) != 0;
}

bool CTxMemPool::Exists(const COutPoint &outpoint) const {
    std::shared_lock lock{smtx};
    return ExistsNL(outpoint);
}

bool CTxMemPool::ExistsNL(const COutPoint &outpoint) const {
    auto it = mapTx.find(outpoint.GetTxId());
    return it != mapTx.end() && outpoint.GetN() < it->GetSharedTx()->vout.size();
}

CTxMemPool::Snapshot::Snapshot(Contents&& contents,
                               CachedTxIdsRef&& relevantTxIds)
    : mValid(true),
      mContents(std::move(contents)),
      mRelevantTxIds(std::move(relevantTxIds))
{}

CTxMemPool::Snapshot::const_iterator CTxMemPool::Snapshot::find(const uint256& hash) const
{
    if (mValid) {
        CreateIndex();
        const auto iter = mIndex.find(hash);
        if (iter != mIndex.end()) {
            return iter->second;
        }
    }
    return cend();
}

bool CTxMemPool::Snapshot::TxIdExists(const uint256& hash) const
{
    if (mValid) {
        CreateIndex();
        return (1 == mIndex.count(hash));
    }
    return false;
}

void CTxMemPool::Snapshot::CreateIndex() const
{
    std::call_once(
        mCreateIndexOnce,
        [this]() {
            assert(IsValid());
            assert(mIndex.empty());

            // Build the transaction index from the slice contents and
            // additional relevant transaction IDs.
            mIndex.reserve(mContents.size()
                           + (mRelevantTxIds ? mRelevantTxIds->size() : 0));
            for (auto it = cbegin(); it != cend(); ++it) {
                mIndex.emplace(it->GetTxId(), it);
            }
            if (mRelevantTxIds) {
                for (const auto& txid : *mRelevantTxIds) {
                    mIndex.emplace(txid, cend());
                }
            }
        });
}

CTxMemPool::Snapshot CTxMemPool::GetSnapshot() const
{
    std::shared_lock lock{smtx};

    Snapshot::Contents contents;
    contents.reserve(mapTx.size());
    for (const auto& entry : mapTx) {
        contents.emplace_back(entry);
    }
    return Snapshot(std::move(contents), nullptr);
}

CTxMemPool::Snapshot CTxMemPool::GetTxSnapshot(const uint256& hash, TxSnapshotKind kind) const
{
    std::shared_lock lock{smtx};

    const auto baseTx = mapTx.find(hash);
    if (baseTx == mapTx.end()) {
        return Snapshot();
    }

    Snapshot::Contents contents;
    auto relevantTxIds = std::make_unique<Snapshot::CachedTxIds>();
    // This closure is essentially a local function that stores
    // information about a single transaction and its inputs.
    const auto recordTransaction =
        [this, &contents, &relevantTxIds](txiter entry)
        {
            contents.emplace_back(*entry);
            for (const auto& prevout : GetOutpointsSpentByNL(entry)) {
                const auto& id = prevout.GetTxId();
                if (ExistsNL(id)) {
                    relevantTxIds->emplace_back(id);
                }
            }
        };

    if (kind == TxSnapshotKind::SINGLE)
    {
        // Store the single transaction of the snapshot.
        recordTransaction(baseTx);
    }
    else if (kind == TxSnapshotKind::TX_WITH_ANCESTORS
             || kind == TxSnapshotKind::ONLY_ANCESTORS
             || kind == TxSnapshotKind::TX_WITH_DESCENDANTS
             || kind == TxSnapshotKind::ONLY_DESCENDANTS)
    {
        // Find other related transactions, depending on the invocation mode.
        setEntries related;
        if (kind == TxSnapshotKind::TX_WITH_DESCENDANTS
            || kind == TxSnapshotKind::ONLY_DESCENDANTS) {
            GetDescendantsNL(baseTx, related);
        }
        else {
            GetMemPoolAncestorsNL(baseTx, related);
        }
        // Quirks mode: GetDescendantsNL() and CalculateMemPoolAncestors()
        // are not symmetric, the former includes the base transaction in the
        // results, but the latter does not.
        if (kind == TxSnapshotKind::TX_WITH_ANCESTORS) {
            recordTransaction(baseTx);
        }
        else if (kind == TxSnapshotKind::ONLY_DESCENDANTS) {
            related.erase(baseTx);
        }
        for (const auto& iter : related) {
            recordTransaction(iter);
        }
    }
    else
    {
        // Oops. Someone changed the enum without updating this function.
        assert(!"CTxMemPool::GetTxSnapshot(): invalid 'kind'");
    }

    return Snapshot(std::move(contents), std::move(relevantTxIds));
}

std::vector<CTransactionRef> CTxMemPool::GetTransactions() const
{
    std::shared_lock lock{smtx};

    std::vector<CTransactionRef> result;
    result.reserve(mapTx.size());
    for (const auto& entry : mapTx) {
        result.emplace_back(entry.GetSharedTx());
    }
    return result;
}

/*
 * Format of the serialized mempool.dat file
 * =========================================
 *
 * Overall file structure:
 *
 *     uint64   format-version
 *     (uuid    file-instance)              if format-version >= 2
 *     uint64   transaction-count
 *     array    transaction-data            transaction-count elements
 *     map      fee-deltas-map              {txid -> Amount}
 *
 * Version 2 transaction-data:
 *
 *     bool     transaction-in-memory
 *     (txdata  transaction)                if transaction-in-memory
 *     (uint256 transaction-id)             if not transaction-in-memory
 *     int64    entry-time
 *     int64    fee-delta
 *
 * Version 1 transaction-data:
 *
 *     txdata   transaction
 *     int64    entry-time
 *     int64    fee-delta
 */

namespace {
    const uint64_t MEMPOOL_DUMP_VERSION = 2;
    const uint64_t MEMPOOL_DUMP_COMPAT_VERSION = 1;
    const uint64_t MEMPOOL_DUMP_HAS_INSTANCE_ID = 2;
    const uint64_t MEMPOOL_DUMP_HAS_ON_DISK_TXS = 2;
} // namespace

void CTxMemPool::DoInitMempoolTxDB()
{
    if (!gArgs.GetArg("-persistmempool", DEFAULT_PERSIST_MEMPOOL))
    {
        // If we're not going to load mempool.dat, we have to start with an
        // empty mempool database.
        OpenMempoolTxDB(true);
        return;
    }

    OpenMempoolTxDB();
    try
    {
        uint64_t version;
        DumpFileID instanceId;
        CAutoFile file{OpenDumpFile(version, instanceId), SER_DISK, CLIENT_VERSION};

        std::unique_lock lock{smtx};
        if (file.IsNull())
        {
            LogPrint(BCLog::MEMPOOL,
                     "No mempool file from previous session,"
                     " clearing the transaction database.\n");
            mempoolTxDB->Clear();
            return;
        }

        if (version < MEMPOOL_DUMP_HAS_INSTANCE_ID)
        {
            LogPrint(BCLog::MEMPOOL,
                     "Mempool file has format version %d,"
                     " clearing the transaction database.\n",
                     (int)version);
            mempoolTxDB->Clear();
            return;
        }

        CMempoolTxDB::XrefKey xrefKey;
        if (!mempoolTxDB->GetXrefKey(xrefKey) || xrefKey != instanceId)
        {
            LogPrint(BCLog::MEMPOOL,
                     "Mempool file and transaction database do not match,"
                     " clearing the transaction database.\n");
            mempoolTxDB->Clear();
            return;
        }

        // Remove the UUID from the database until the next regular node shutdown.
        mempoolTxDB->RemoveXrefKey();
    }
    catch (const std::exception &e)
    {
        LogPrintf("Cross-check of mempool file and transaction database failed: %s."
                  " Continuing with empty mempool and transaction database.\n",
                  e.what());
        Clear();
    }
}

void CTxMemPool::InitUniqueMempoolTxDB()
{
    mempoolTxDB_inMemory = false;
    mempoolTxDB_unique = true;
    DoInitMempoolTxDB();
}

void CTxMemPool::InitInMemoryMempoolTxDB()
{
    mempoolTxDB_inMemory = true;
    mempoolTxDB_unique = true;
    DoInitMempoolTxDB();
}

void CTxMemPool::InitMempoolTxDB()
{
    mempoolTxDB_inMemory = false;
    mempoolTxDB_unique = false;
    DoInitMempoolTxDB();
}


UniqueCFile CTxMemPool::OpenDumpFile(uint64_t& version_, DumpFileID& instanceId_)
{
    CAutoFile file{fsbridge::fopen(GetDataDir() / "mempool.dat", "rb"), SER_DISK, CLIENT_VERSION};
    if (file.IsNull())
    {
        throw std::runtime_error("Failed to open mempool file from disk");
    }

    uint64_t version;
    DumpFileID instanceId {0};

    file >> version;
    if (version > MEMPOOL_DUMP_VERSION || version < MEMPOOL_DUMP_COMPAT_VERSION)
    {
        std::stringstream msg;
        msg << "Bad mempool dump version: " << version;
        throw std::runtime_error(msg.str());
    }

    if (version >= MEMPOOL_DUMP_HAS_INSTANCE_ID)
    {
        file >> instanceId;
    }

    version_ = version;
    instanceId_ = instanceId;
    return file.release();
}

bool CTxMemPool::LoadMempool(const Config &config,
                             const task::CCancellationToken& shutdownToken)
{
    const auto& txValidator = g_connman->getTxnValidator();
    const auto processValidation =
        [&txValidator](const TxInputDataSPtr& txInputData,
                       const mining::CJournalChangeSetPtr& changeSet,
                       bool limitMempoolSize) -> CValidationState
        {
            return txValidator->processValidation(txInputData, changeSet, limitMempoolSize);
        };
    return LoadMempool(config, shutdownToken, processValidation);
}

bool CTxMemPool::LoadMempool(const Config &config,
                             const task::CCancellationToken& shutdownToken,
                             const std::function<CValidationState(
                                 const TxInputDataSPtr& txInputData,
                                 const mining::CJournalChangeSetPtr& changeSet,
                                 bool limitMempoolSize)>& processValidation)
{
    try {
        int64_t nExpiryTimeout = config.GetMemPoolExpiry();

        uint64_t version;
        DumpFileID instanceId;
        CAutoFile file{OpenDumpFile(version, instanceId), SER_DISK, CLIENT_VERSION};

        int64_t count = 0;
        int64_t skipped = 0;
        int64_t failed = 0;
        int64_t nNow = GetTime();

        uint64_t num;
        file >> num;
        // A pointer to the TxIdTracker.
        const auto& pTxIdTracker = g_connman->GetTxIdTracker();
        const auto txdb = mempoolTxDB->GetDatabase();
        while (num--) {
            bool txFromMemory = true;
            CTransactionRef tx;
            int64_t nTime;
            int64_t nFeeDelta;

            if (version >= MEMPOOL_DUMP_HAS_ON_DISK_TXS) {
                file >> txFromMemory;
            }
            if (!txFromMemory) {
                uint256 txid;
                file >> txid;
                if (!txdb->GetTransaction(txid, tx)) {
                    std::stringstream msg;
                    msg << "Transaction was not in mempool database: "
                        << txid.ToString();
                    throw std::runtime_error(msg.str());
                }
            }
            else {
                file >> tx;
            }
            file >> nTime;
            file >> nFeeDelta;
            if (nFeeDelta != 0) {
                const auto& txid = tx->GetId();
                PrioritiseTransaction(txid, txid.ToString(), Amount{nFeeDelta});
            }
            if (nTime + nExpiryTimeout > nNow) {
                // Mempool Journal ChangeSet should be nullptr for simple mempool operations
                CJournalChangeSetPtr changeSet {nullptr};
                const auto txStorage = (txFromMemory ? TxStorage::memory : TxStorage::txdb);
                const CValidationState state {
                    // Execute txn validation synchronously.
                    processValidation(
                        std::make_shared<CTxInputData>(
                            pTxIdTracker, // a pointer to the TxIdTracker
                            tx,    // a pointer to the tx
                            TxSource::file, // tx source
                            TxValidationPriority::normal,  // tx validation priority
                            txStorage, // tx storage
                            nTime), // nAcceptTime
                        changeSet, // an instance of the mempool journal
                        true) // fLimitMempoolSize
                };
                // Check results
                if (state.IsValid()) {
                    ++count;
                } else {
                    ++failed;
                    if (!txFromMemory) {
                        mempoolTxDB->Remove({tx->GetId(), tx->GetTotalSize()});
                    }
                }
            } else {
                ++skipped;
                if (!txFromMemory) {
                    mempoolTxDB->Remove({tx->GetId(), tx->GetTotalSize()});
                }
            }
            if (shutdownToken.IsCanceled()) {
                return false;
            }
        }

        std::map<uint256, Amount> mapDeltas;
        file >> mapDeltas;

        for (const auto &i : mapDeltas) {
            PrioritiseTransaction(i.first, i.first.ToString(), i.second);
        }

        // Check that the mempool and the database are in sync.
        std::shared_lock lock{smtx};
        if (!CheckMempoolTxDBNL(false)) {
            throw std::runtime_error("Mempool and transaction database contents do not match");
        }

        LogPrintf("Imported mempool transactions from disk: %i successes, %i "
                  "failed, %i expired\n",
                  count, failed, skipped);

    }
    catch (const std::exception &e) {
        LogPrintf("Failed to deserialize mempool data on disk: %s."
                  " Continuing anyway with empty mempool.\n",
                  e.what());
        Clear();
    }

    // Restore non-final transactions
    return getNonFinalPool().loadMempool(shutdownToken);
}

void CTxMemPool::DumpMempool() {
    DumpMempool(MEMPOOL_DUMP_VERSION);
}

void CTxMemPool::DumpMempool(uint64_t version) {
    int64_t start = GetTimeMicros();

    std::map<uint256, Amount> mapDeltas;
    std::vector<TxMempoolInfo> vinfo;
    GetDeltasAndInfo(mapDeltas, vinfo);

    int64_t mid = GetTimeMicros();

    try {
        FILE *filestr = fsbridge::fopen(GetDataDir() / "mempool.dat.new", "wb");
        if (!filestr) {
            return;
        }

        CAutoFile file{filestr, SER_DISK, CLIENT_VERSION};

        file << version;
        if (version >= MEMPOOL_DUMP_HAS_INSTANCE_ID) {
            boost::uuids::random_generator gen;
            DumpFileID instanceId = gen();
            mempoolTxDB->SetXrefKey(instanceId);
            file << instanceId;
        }

        file << (uint64_t)vinfo.size();
        size_t count = 0;
        size_t txdb = 0;
        for (const auto &i : vinfo) {
            if (version >= MEMPOOL_DUMP_HAS_ON_DISK_TXS) {
                const bool txFromMemory = i.GetTxStorage() == TxStorage::memory;
                file << txFromMemory;
                if (!txFromMemory) {
                    file << i.GetTxId();
                    ++txdb;
                }
                else {
                    file << *i.GetTx();
                }
            }
            else {
                file << *i.GetTx();
            }
            file << static_cast<int64_t>(i.nTime);
            file << static_cast<int64_t>(i.nFeeDelta.GetSatoshis());
            mapDeltas.erase(i.GetTxId());
            ++count;
        }

        file << mapDeltas;
        FileCommit(file.Get());
        file.reset();
        RenameOver(GetDataDir() / "mempool.dat.new",
                   GetDataDir() / "mempool.dat");
        int64_t last = GetTimeMicros();
        LogPrintf("Dumped mempool: %.6fs to copy, %.6fs to dump (%zu txs, %zu in txdb)\n",
                  (mid - start) * 0.000001, (last - mid) * 0.000001,
                  count, txdb);
    } catch (const std::exception &e) {
        LogPrintf("Failed to dump mempool: %s. Continuing anyway.\n", e.what());
        mempoolTxDB->RemoveXrefKey();
    }

    // Dump non-final pool
    getNonFinalPool().dumpMempool();
}

DisconnectedBlockTransactions::~DisconnectedBlockTransactions()
{
    if (!queuedTx.empty())
    {
        LogPrintf(
            "ERROR in ~DisconnectedBlockTransactions: queuedTx not empty!"
            " Some transactions will be dropped from mempool.\n");
    }
}

std::ostream& operator<<(std::ostream& os, const MemPoolRemovalReason& reason)
{
    os << enum_cast<std::string>(reason);
    return os;
}

