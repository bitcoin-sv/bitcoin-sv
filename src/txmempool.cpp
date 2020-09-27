// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2019-2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "txmempool.h"
#include "txmempoolevictioncandidates.h"
#include "clientversion.h"
#include "consensus/consensus.h"
#include "consensus/validation.h"
#include "mempooltxdb.h"
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
#include "version.h"
#include <boost/range/adaptor/reversed.hpp>
#include <config.h>

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
                    return CoinImpl::MakeNonOwningWithScript(ptx->vout[outpoint.GetN()], MEMPOOL_HEIGHT, false);
                }
                return {};
            }

            return mDBView.GetCoin(outpoint, maxScriptSize);
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
        mMempool.PrioritiseTransaction(mTxnsToPrioritise, 0.0, MAX_MONEY);
    }
}

CTxPrioritizer::CTxPrioritizer(CTxMemPool& mempool, std::vector<TxId> txnsToPrioritise)
    : mMempool(mempool), mTxnsToPrioritise(std::move(txnsToPrioritise))
{
    // An early emptiness check.
    if (!mTxnsToPrioritise.empty()) {
        mMempool.PrioritiseTransaction(mTxnsToPrioritise, 0.0, MAX_MONEY);
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
 * class CTransactionWrapper
 */
CTransactionWrapper::CTransactionWrapper()
{
}

CTransactionWrapper::CTransactionWrapper(const CTransactionRef &_tx,
                                         const std::shared_ptr<CMempoolTxDBReader>& txDB)
    : tx{_tx},
      txid{_tx->GetId()},
      mempoolTxDB{txDB}
{
}

CTransactionRef CTransactionWrapper::GetTxFromDB() const {
    CTransactionRef tmp;
    if (mempoolTxDB != nullptr) {
        mempoolTxDB->GetTransaction(txid, tmp);
        std::atomic_store(&tx, tmp);
    }
    return tmp;
}


const TxId& CTransactionWrapper::GetId() const {
    return txid;
}

CTransactionRef CTransactionWrapper::GetTx() const {
    CTransactionRef tmp = std::atomic_load(&tx);
    if (tmp != nullptr) {
        return tmp;
    }
    return GetTxFromDB();
}

void CTransactionWrapper::UpdateTxMovedToDisk() const {
    std::atomic_store(&tx, CTransactionRef(nullptr));
}

bool CTransactionWrapper::IsInMemory() const
{
    return std::atomic_load(&tx) != nullptr;
}

bool CTransactionWrapper::HasDatabase(const std::shared_ptr<CMempoolTxDBReader>& txDB) const noexcept
{
    return mempoolTxDB == txDB;
}

/**
 * class CTxMemPoolEntry
 */
CTxMemPoolEntry::CTxMemPoolEntry(const CTransactionRef& _tx,
                                 const Amount _nFee,
                                 int64_t _nTime,
                                 double _entryPriority,
                                 int32_t _entryHeight,
                                 Amount _inChainInputValue,
                                 bool _spendsCoinbase,
                                 LockPoints lp)
    : tx{std::make_shared<CTransactionWrapper>(_tx, nullptr)},
      nFee{_nFee},
      nTime{_nTime},
      entryPriority{_entryPriority},
      inChainInputValue{_inChainInputValue},
      lockPoints{lp},
      entryHeight{_entryHeight},
      spendsCoinbase{_spendsCoinbase}
{
    nTxSize = _tx->GetTotalSize();
    nModSize = _tx->CalculateModifiedSize(GetTxSize());
    nUsageSize = RecursiveDynamicUsage(_tx);

    Amount nValueIn = _tx->GetValueOut() + nFee;
    assert(inChainInputValue <= nValueIn);

    feeDelta = Amount {0};
}

// CPFP group, if any that this transaction belongs to.

GroupID CTxMemPoolEntry::GetCPFPGroupId() const 
{ 
    if(group)
    {
        return GroupID{ group->PayingTransaction()->GetTxId() };
    }
    return std::nullopt; 
}

double CTxMemPoolEntry::GetPriority(int32_t currentHeight) const {
    double deltaPriority = double((currentHeight - entryHeight) *
                                  inChainInputValue.GetSatoshis()) /
                           nModSize;
    double dResult = entryPriority + deltaPriority;
    // This should only happen if it was called with a height below entry height
    if (dResult < 0) {
        dResult = 0;
    }
    return dResult;
}

void CTxMemPoolEntry::UpdateFeeDelta(Amount newFeeDelta) {
    feeDelta = newFeeDelta;
}

void CTxMemPoolEntry::UpdateLockPoints(const LockPoints &lp) {
    lockPoints = lp;
}

bool CTxMemPoolEntry::IsInMemory() const {
    return tx->IsInMemory();
}

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
    std::shared_lock lock(smtx);
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
            ancestorsCount += 1;
            ancestorsCount += (*it)->ancestorsCount;

            if(!(*it)->IsInPrimaryMempool())
            {
                secondaryMempoolAncestorsCount += 1;
                secondaryMempoolAncestorsCount += (*it)->groupingData.value().ancestorsCount;
            }
            
            if(ancestorsCount >= limitAncestorCount)
            {
                if(errString.has_value())
                {
                    errString.value().get() = strprintf("too many unconfirmed parents [limit: %u]", limitAncestorCount);
                }
                return false;
            }
            
            if(secondaryMempoolAncestorsCount >= limitSecondaryMempoolAncestorCount)
            {
                if(errString.has_value())
                {
                    errString.value().get() = strprintf("too many unconfirmed parents which we are not willing to mine [limit: %u]", limitSecondaryMempoolAncestorCount);
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
    std::shared_lock lock(smtx);
    return IsSpentNL(outpoint);
}

bool CTxMemPool::IsSpentNL(const COutPoint &outpoint) const {
    return mapNextTx.count(outpoint);
}

CTransactionRef CTxMemPool::IsSpentBy(const COutPoint &outpoint) const {
    std::shared_lock lock{ smtx };

    auto it = mapNextTx.find(outpoint);
    if (it == mapNextTx.end())
    {
        return nullptr;
    }
    return it->second->GetTx();
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
    const CJournalChangeSetPtr& changeSet,
    size_t* pnMempoolSize,
    size_t* pnDynamicMemoryUsage) {

    {
        std::unique_lock lock(smtx);
        
        AddUncheckedNL(
            hash,
            entry,
            changeSet,
            pnMempoolSize,
            pnDynamicMemoryUsage);
    }
    // Notify entry added without holding the mempool's lock
    NotifyEntryAdded(entry.GetSharedTx());
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
        ancestors.insert(entry);
        for(txiter parent: GetMemPoolParentsNL(entry))
        {
            if (!parent->IsInPrimaryMempool())
            {
                toVisit.insert(parent);
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
    auto group = std::make_shared<CPFPGroup>();
    group->evaluationParams = groupingData;
    group->transactions = {groupMembers.begin(), groupMembers.end()};

    // assemble the group
    for(auto entry: groupMembers)
    {
        mapTx.modify(entry, [&group](CTxMemPoolEntry& entry) {
                                entry.group = group;
                                entry.groupingData = std::nullopt;
                            });
        secondaryMempoolSize--; // moving from secondary mempool to the primary
        TrackEntryModified(entry);
    }

    // submit the group
    for(auto entry: groupMembers)
    {
        changeSet.addOperation(mining::CJournalChangeSet::Operation::ADD, {*entry});
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

void CTxMemPool::SetGroupingData(CTxMemPool::txiter entryIt, std::optional<SecondaryMempoolEntryData> groupingData)
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

        SetGroupingData(entryIt, groupingData);
        return ResultOfUpdateEntryGroupingDataNL::GROUPING_DATA_MODIFIED;
    }
    else
    {
        if(groupingData.ancestorsCount == 0)
        {
            SetGroupingData(entryIt, std::nullopt);
            return ResultOfUpdateEntryGroupingDataNL::ADD_TO_PRIMARY_STANDALONE;
        }

        SetGroupingData(entryIt, groupingData);
        return ResultOfUpdateEntryGroupingDataNL::ADD_TO_PRIMARY_GROUP_PAYING_TX;
    }
}

void CTxMemPool::TryAcceptToPrimaryMempoolNL(CTxMemPool::setEntriesTopoSorted toUpdate, mining::CJournalChangeSet& changeSet)
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

    while(!toUpdate.empty() && countOfVisitedTxs < MAX_NUMBER_OF_TX_TO_VISIT_IN_ONE_GO)
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
                changeSet.addOperation(mining::CJournalChangeSet::Operation::ADD, {*entry} );
                secondaryMempoolSize--;
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
            SetGroupingData(entry, std::nullopt);
            changeSet.addOperation(mining::CJournalChangeSet::Operation::ADD, {*entry} );
            secondaryMempoolSize--;
        }
        else
        {
            // accept it to primary mempool as group paying tx
            setEntriesTopoSorted toUpdate;
            toUpdate.insert(entry);
            TryAcceptToPrimaryMempoolNL(std::move(toUpdate), changeSet);
        }
    }
    else
    {
        SetGroupingData(entry, data);
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
            for(txiter groupMember: group->transactions)
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
            changeSet.addOperation(CJournalChangeSet::Operation::REMOVE, {*entry});
            secondaryMempoolSize++; // removing from primary to secondary mempool

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
                SetGroupingData(entry, SecondaryMempoolEntryData());
            }
            else
            {
                SecondaryMempoolEntryData groupingData = CalculateSecondaryMempoolData(entry);
                SetGroupingData(entry, groupingData);
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
            ancestorsCount += 1;
            ancestorsCount += parent->ancestorsCount;
        }
        mapTx.modify(entry, [ancestorsCount](CTxMemPoolEntry& entry) {
                                entry.ancestorsCount = ancestorsCount;
                             });
    }
}

void CTxMemPool::AddUncheckedNL(
    const uint256 &hash,
    const CTxMemPoolEntry &originalEntry,
    const CJournalChangeSetPtr& changeSet,
    size_t* pnMempoolSize,
    size_t* pnDynamicMemoryUsage)
{
    static const auto nullTxDB = std::shared_ptr<CMempoolTxDBReader>(nullptr);

    // Make sure the transaction database is initialized so that we have
    // a valid mempoolTxDB for the following checks.
    InitMempoolTxDB();

    auto newit = mapTx.insert(originalEntry).first;

    // During reorg, we could be re-adding an entry whose transaction was
    // previously moved to disk, in which case we must make sure that the entry
    // belongs to the same mempool.
    const auto mustUpdateDatabase = newit->tx->HasDatabase(nullTxDB);
    const auto thisTxDB = mempoolTxDB->GetDatabase();
    assert((mustUpdateDatabase && newit->IsInMemory()) || newit->tx->HasDatabase(thisTxDB));

    mapLinks.insert(make_pair(newit, TxLinks()));
    mapTx.modify(newit, [this, &mustUpdateDatabase, &thisTxDB](CTxMemPoolEntry& entry) {
        // Update insertion order indes for this entry.
        entry.SetInsertionIndex(insertionIndex.GetNext());
        if (mustUpdateDatabase) {
            // Update transaction database for this entry.
            // This should not affect the mapTx index.
            entry.tx = std::make_shared<CTransactionWrapper>(entry.GetSharedTx(), thisTxDB);
        }
    });

    // Update transaction for any feeDelta created by PrioritiseTransaction
    // TODO: refactor so that the fee delta is calculated before inserting into
    // mapTx.
    auto pos = mapDeltas.find(hash);
    if (pos != mapDeltas.end()) {
        const std::pair<double, Amount> &deltas = pos->second;
        if (deltas.second != Amount(0)) {
            mapTx.modify(newit, update_fee_delta(deltas.second));
        }
    }

    // Update cachedInnerUsage to include contained transaction's usage.
    // (When we update the entry for in-mempool parents, memory usage will be
    // further updated.)
    cachedInnerUsage += newit->DynamicMemoryUsage();

    const auto sharedTx = newit->GetSharedTx();
    std::set<uint256> setParentTransactions;
    for (const CTxIn &in : sharedTx->vin) {
        mapNextTx.insert(std::make_pair(in.prevout, newit->tx));
        setParentTransactions.insert(in.prevout.GetTxId());
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
            ancestorsCount += 1;
            ancestorsCount += pit->ancestorsCount;
            updateParentNL(newit, pit, true);
        }
    }
    mapTx.modify(newit, [ancestorsCount](CTxMemPoolEntry& entry) {
        entry.ancestorsCount = ancestorsCount;
    });

    updateAncestorsOfNL(true, newit);

    CEnsureNonNullChangeSet nonNullChangeSet{*this, changeSet};

    // set dummy data that it looks like it is in the secondary mempool
    SetGroupingData(newit, SecondaryMempoolEntryData());
    secondaryMempoolSize++;
    // now see if it can be accepted to primary mempool
    // this will set correct groupin data if it stays in the secondary mempool
    TryAcceptChildlessTxToPrimaryMempoolNL(newit, nonNullChangeSet.Get());

    nTransactionsUpdated++;
    totalTxSize += newit->GetTxSize();

    // If it is required calculate mempool size & dynamic memory usage.
    if (pnMempoolSize) {
        *pnMempoolSize = PrimaryMempoolSizeNL();
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
    MemPoolRemovalReason reason,
    const CTransaction* conflictedWith)
{
    std::vector<TxId> transactionsToRemove;
    std::uint64_t diskUsageRemoved = 0;

    transactionsToRemove.reserve(entries.size());
    for (auto entry : entries)
    {
        if (!entry->IsInMemory())
        {
            transactionsToRemove.emplace_back(entry->GetTxId());
            diskUsageRemoved += entry->GetTxSize();
        }

        const auto txn = entry->GetSharedTx();
        NotifyEntryRemoved(txn, reason);
        for (const auto& txin : txn->vin) {
            mapNextTx.erase(txin.prevout);
        }

        // Apply to the current journal, but only if it is in the journal (primary mempool) already
        if(entry->IsInPrimaryMempool())
        {
            changeSet.addOperation(CJournalChangeSet::Operation::REMOVE, { *entry });
        }
        else
        {
            secondaryMempoolSize--;
        }


        totalTxSize -= entry->GetTxSize();
        cachedInnerUsage -= entry->DynamicMemoryUsage();
        // FIXME: We are not implemented IncrementalDynamicMemoryUsage for unordered set.
        //        See: CTxMemPool::updateChildNL and CTxMemPool::updateParentNL
        //    cachedInnerUsage -= memusage::DynamicUsage(mapLinks[it].parents) +
        //                        memusage::DynamicUsage(mapLinks[it].children);
        setEntries parents;
        TxId txid;
        if (evictionTracker) {
            txid = entry->GetTxId();
            parents = std::move(mapLinks.at(entry).parents);
        }

        mapLinks.erase(entry);
        mapTx.erase(entry);
        nTransactionsUpdated++;

        if (reason == MemPoolRemovalReason::BLOCK || reason == MemPoolRemovalReason::REORG)
        {
            GetMainSignals().TransactionRemovedFromMempoolBlock(txn->GetId(), reason);
        }
        else
        {
            GetMainSignals().TransactionRemovedFromMempool(txn->GetId(), reason, conflictedWith);
        }

        // Update the eviction candidate tracker.
        TrackEntryRemoved(txid, parents);
    }

    if (transactionsToRemove.size() > 0)
    {
        InitMempoolTxDB();
        mempoolTxDB->Remove(std::move(transactionsToRemove), diskUsageRemoved);
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

void CTxMemPool::RemoveRecursive(
    const CTransaction &origTx,
    const CJournalChangeSetPtr& changeSet,
    MemPoolRemovalReason reason) {

    {
        std::unique_lock lock(smtx);
        // Remove transaction from memory pool.
        removeRecursiveNL(
            origTx.GetId(),
            changeSet,
            reason);
    }
}

void CTxMemPool::removeRecursiveNL(
    const TxId &origTxId,
    const CJournalChangeSetPtr& changeSet,
    MemPoolRemovalReason reason,
    const CTransaction* conflictedWith) {

    CEnsureNonNullChangeSet nonNullChangeSet{*this, changeSet};
    setEntries txToRemove;
    txiter origit = mapTx.find(origTxId);
    if (origit != mapTx.end()) {
        txToRemove.insert(origit);
    } else {
        // When recursively removing but origTxId isn't in the mempool be sure
        // to remove any children that are in the pool. This can happen during
        // chain re-orgs if origTxId isn't re-accepted into the mempool for any
        // reason.
        for (auto it = mapNextTx.lower_bound(COutPoint(origTxId, 0));
             it != mapNextTx.end() && it->first.GetTxId() == origTxId;
             ++it) {
            txiter nextit = mapTx.find(it->second->GetId());
            assert(nextit != mapTx.end());
            txToRemove.insert(nextit);
        }
    }
    setEntries setAllRemoves;
    for (txiter it : txToRemove) {
        GetDescendantsNL(it, setAllRemoves);
    }

    removeStagedNL(setAllRemoves, nonNullChangeSet.Get(), reason, conflictedWith);
}

void CTxMemPool::RemoveForReorg(
    const Config &config,
    const CoinsDB& coinsTip,
    const CJournalChangeSetPtr& changeSet,
    const CBlockIndex& tip,
    int flags) {

    const int32_t nMemPoolHeight = tip.nHeight + 1;
    const int nMedianTimePast = tip.GetMedianTimePast();
    // Remove transactions spending a coinbase which are now immature and
    // no-longer-final transactions.
    std::unique_lock lock(smtx);
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
                tip.nHeight,
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
        } else if (it->GetSpendsCoinbase()) {
            for (const CTxIn &txin : tx->vin) {
                txiter it2 = mapTx.find(txin.prevout.GetTxId());
                if (it2 != mapTx.end()) {
                    continue;
                }

                auto coin = tipView.GetCoin(txin.prevout);
                assert( coin.has_value() );
                if (nCheckFrequency != 0) {
                    assert(coin.has_value() && !coin->IsSpent());
                }

                if (!coin.has_value() || coin->IsSpent() ||
                    (coin->IsCoinBase() &&
                     nMemPoolHeight - coin->GetHeight() <
                         COINBASE_MATURITY)) {
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
    removeStagedNL(setAllRemoves, nonNullChangeSet.Get(), MemPoolRemovalReason::REORG);
}

void CTxMemPool::RemoveForBlock(
    const std::vector<CTransactionRef> &vtx,
    int32_t nBlockHeight,
    const CJournalChangeSetPtr& changeSet) {

    CEnsureNonNullChangeSet nonNullChangeSet{*this, changeSet};

    std::unique_lock lock(smtx);

    evictionTracker.reset();

    setEntries toRemove; // entries which should be removed
    setEntriesTopoSorted childrenOfToRemove; // we must collect all transaction which parents we have removed to update its ancestorCount
    setEntriesTopoSorted childrenOfToRemoveGroupMembers; // immediate children of entries we will remove that are members of the cpfp group, need to be updated after removal
    setEntriesTopoSorted childrenOfToRemoveSecondaryMempool; // immediate children of entries we will remove that are in the secondary mempool, need to be updated after removal

    std::unordered_set<TxId, SaltedTxidHasher> txidFromBlock; // spent outputs by tx from block

    for(auto vtxIter = vtx.rbegin();  vtxIter != vtx.rend();  vtxIter++)
    {
        auto& tx = *vtxIter;
        
        auto found = mapTx.find(tx->GetId()); // see if we have this transaction from block

        if(found != mapTx.end()) 
        {
            toRemove.insert(found);
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

                    // cutting connection from child tx to soon to be removed parent
                    updateParentNL(child, found, false);
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
                    GetDescendantsNL(mapTx.find(it->second->GetId()), conflictedWithDescendants);

                    for(txiter inConflict: conflictedWithDescendants)
                    {
                        // inConflict will be erased so remove it from the sets of txs that needs to be updated
                        childrenOfToRemoveGroupMembers.erase(inConflict);
                        childrenOfToRemoveSecondaryMempool.erase(inConflict);
                    }

                    // remove conflicted tx from mempool (together with all descendants)
                    removeStagedNL(conflictedWithDescendants, nonNullChangeSet.Get(), MemPoolRemovalReason::CONFLICT, tx.get());
                }
            }
        }
    }

    // remove affected groups from primary mempool
    // we are ignoring members of "toRemove" (we will remove them in the removeUncheckedNL), so that "removedFromPrimary" can not contain transactions that will be removed
    auto removedFromPrimary = RemoveFromPrimaryMempoolNL(std::move(childrenOfToRemoveGroupMembers), nonNullChangeSet.Get(), true, &toRemove);

    // now remove transactions from mempool
    removeUncheckedNL(toRemove, nonNullChangeSet.Get(), MemPoolRemovalReason::BLOCK);

    UpdateAncestorsCountNL(std::move(childrenOfToRemove));

    setEntriesTopoSorted toRecheck;
    // we will recheck all disbanded groups members and secondary mempool children together
    std::set_union(removedFromPrimary.begin(), removedFromPrimary.end(), 
                   childrenOfToRemoveSecondaryMempool.begin(), childrenOfToRemoveSecondaryMempool.end(),
                   std::inserter(toRecheck, toRecheck.begin()), InsrtionOrderComparator());

    TryAcceptToPrimaryMempoolNL(std::move(toRecheck), nonNullChangeSet.Get());

    lastRollingFeeUpdate = GetTime();
    blockSinceLastRollingFeeBump = true;
}

void CTxMemPool::clearNL(bool skipTransactionDatabase/* = false*/) {
    evictionTracker.reset();
    mapLinks.clear();
    mapTx.clear();
    mapNextTx.clear();
    totalTxSize = 0;
    secondaryMempoolSize = 0;
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

void CTxMemPool::trackPackageRemovedNL(const CFeeRate &rate) {
    if (rate.GetFeePerK().GetSatoshis() > rollingMinimumFeeRate) {
        rollingMinimumFeeRate = rate.GetFeePerK().GetSatoshis();
        blockSinceLastRollingFeeBump = false;
    }
}

void CTxMemPool::Clear() {
    std::unique_lock lock(smtx);
    clearNL();
}

void CTxMemPool::CheckMempool(
    CoinsDB& db,
    const mining::CJournalChangeSetPtr& changeSet) const
{

    if (ShouldCheckMempool())
    {
        CoinsDBView view{ db };
        std::shared_lock lock(smtx);
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

    LogPrint(BCLog::MEMPOOL,
             "Checking mempool with %u transactions and %u inputs\n",
             (unsigned int)PrimaryMempoolSizeNL(), (unsigned int)mapNextTx.size());

    size_t primaryMempoolSize = 0;

    uint64_t checkTotal = 0;
    uint64_t innerUsage = 0;

    std::list<const CTxMemPoolEntry *> waitingOnDependants;
    for (txiter it = mapTx.begin(); it != mapTx.end(); it++) {
        if(it->IsInPrimaryMempool())
        {
            primaryMempoolSize++;
        }
        unsigned int i = 0;
        checkTotal += it->GetTxSize();
        innerUsage += it->DynamicMemoryUsage();
        const auto tx = it->GetSharedTx();
        txlinksMap::const_iterator linksiter = mapLinks.find(it);
        assert(linksiter != mapLinks.end());
        // FIXME: we are not implemented IncrementalDynamicMemoryUsage for unordered set. see: CTxMemPool::updateChildNL and CTxMemPool::updateParentNL
        //innerUsage += memusage::DynamicUsage(links.parents) +
        //              memusage::DynamicUsage(links.children);
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
                    ancestorsCount += 1;
                    ancestorsCount += it2->ancestorsCount;
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
            assert(it3->first == txin.prevout);
            assert(it3->second->GetId() == tx->GetId());
            i++;
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
        auto iter = mapNextTx.lower_bound(COutPoint(it->GetTxId(), 0));

        int64_t childSizes = 0;
        for (; iter != mapNextTx.end() &&
               iter->first.GetTxId() == it->GetTxId();
             ++iter) {
            txiter childit = mapTx.find(iter->second->GetId());
            // mapNextTx points to in-mempool transactions
            assert(childit != mapTx.end());
            if (setChildrenCheck.insert(childit).second) {
                childSizes += childit->GetTxSize();
            }
        }
        assert(setChildrenCheck == GetMemPoolChildrenNL(it));

        if (fDependsWait) {
            waitingOnDependants.push_back(&(*it));
        } else {
            CValidationState state;
            bool fCheckResult = tx->IsCoinBase() ||
                                Consensus::CheckTxInputs(
                                    *tx, state, mempoolDuplicate, nSpendHeight);
            assert(fCheckResult);
            UpdateCoins(*tx, mempoolDuplicate, 1000000);
        }

        // Check we haven't let any non-final txns in
        assert(IsFinalTx(*tx, nSpendHeight, medianTimePast));
    }

    assert(primaryMempoolSize == PrimaryMempoolSizeNL());

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
            bool fCheckResult =
                entryTx->IsCoinBase() ||
                Consensus::CheckTxInputs(*entryTx, state,
                                         mempoolDuplicate, nSpendHeight);
            assert(fCheckResult);
            UpdateCoins(*entryTx, mempoolDuplicate, 1000000);
            stepsSinceLastRemove = 0;
        }
    }

    for (const auto& item: mapNextTx) {
        const auto& txid = item.second->GetId();
        const auto it2 = mapTx.find(txid);
        assert(it2 != mapTx.end());
        assert(it2->GetTxId() == txid);
    }

    assert(totalTxSize == checkTotal);
    assert(innerUsage == cachedInnerUsage);

    /* Journal checking */
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
#define ASSERT_OR_FAIL(condition)               \
    do {                                        \
        if (!(condition)) {                     \
            if (hardErrors) {                   \
                assert(condition);              \
            }                                   \
            return false;                       \
        }                                       \
    } while(0)

    mempoolTxDB->Sync();
    auto keys = mempoolTxDB->GetTxKeys();
    ASSERT_OR_FAIL(keys.size() == mempoolTxDB->GetTxCount());
    uint64_t totalSize = 0;
    for (const auto& e : mapTx)
    {
        const auto key = keys.find(e.GetTxId());
        if (e.IsInMemory())
        {
            ASSERT_OR_FAIL(key == keys.end());
        }
        else
        {
            ASSERT_OR_FAIL(key != keys.end());
            keys.erase(key);
            totalSize += e.GetTxSize();
        }
    }
    ASSERT_OR_FAIL(keys.size() == 0);
    ASSERT_OR_FAIL(totalSize == mempoolTxDB->GetDiskUsage());
    return true;
#undef ASSERT_OR_FAIL
}

std::string CTxMemPool::CheckJournal() const {
    std::shared_lock lock(smtx);
    return checkJournalNL();
}

void CTxMemPool::ClearPrioritisation(const uint256 &hash) {
    std::unique_lock lock(smtx);
    clearPrioritisationNL(hash);
}

void CTxMemPool::ClearPrioritisation(const std::vector<TxId>& vTxIds) {
    if (vTxIds.empty()) {
        return;
    }
    std::unique_lock lock(smtx);
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
        const CJournalEntry tx { *it };
        if(it->IsInPrimaryMempool() && !tester.checkTxnExists(tx))
        {
            res << "Txn " << tx.getTxn()->GetId().ToString() << " is in the primary mempool but not the journal" << std::endl;
        }

        if(!it->IsInPrimaryMempool() && tester.checkTxnExists(tx))
        {
            res << "Txn " << tx.getTxn()->GetId().ToString() << " is not in the primary mempool but it is in the journal" << std::endl;
        }

        if(it->IsInPrimaryMempool())
        {
            for(const CTxIn& txin : tx.getTxn()->vin)
            {
                auto prevoutit { mapTx.find(txin.prevout.GetTxId()) };
                if(prevoutit != mapTx.end())
                {
                    // Check this in mempool ancestor appears before its descendent in the journal
                    const CJournalEntry prevout { *prevoutit };
                    CJournalTester::TxnOrder order { tester.checkTxnOrdering(prevout, tx) };
                    if(order != CJournalTester::TxnOrder::BEFORE)
                    {
                        res << "Ancestor " << prevout.getTxn()->GetId().ToString() << " of "
                            << tx.getTxn()->GetId().ToString() << " appears "
                            << enum_cast<std::string>(order) << " in the journal" << std::endl;
                    }
                }
            }
        }
    }

    LogPrint(BCLog::JOURNAL, "Result of journal check: %s\n", res.str().empty()? "Ok" : res.str().c_str());
    return res.str();
}

// Rebuild the journal contents so they match the mempool
mining::CJournalChangeSetPtr CTxMemPool::RebuildMempool()
{
    LogPrint(BCLog::JOURNAL, "Rebuilding journal\n");

    CJournalChangeSetPtr changeSet { mJournalBuilder.getNewChangeSet(JournalUpdateReason::RESET) };
    {
        std::shared_lock lock(smtx);
        CoinsDBView coinsView{ *pcoinsTip };

        // back-up txs currently in the mempool and clear the mempool
        auto oldMapTx = std::move(mapTx);
        clearNL();

        // submit backed-up transactions
        ResubmitEntriesToMempoolNL(oldMapTx, changeSet);
        
        CheckMempoolNL(coinsView, changeSet);
    }
    return changeSet;
}

void CTxMemPool::SetSanityCheck(double dFrequency) {
    nCheckFrequency = dFrequency * 4294967295.0;
}

/**
* Compare 2 transactions to determine their relative priority.
*/
bool CTxMemPool::CompareDepthAndScore(const uint256 &hasha,
                                      const uint256 &hashb)
{
    std::shared_lock lock(smtx);
    return CompareDepthAndScoreNL(hasha, hashb);
}

namespace {
    //TODO: probably not needed any more
class DepthAndScoreComparator {
public:
    template<typename MempoolEntryIterator>
    bool operator()(const MempoolEntryIterator& a,
                    const MempoolEntryIterator& b) const
    {
        return compare(*a, *b);
    }

private:
    static bool compare(const CTxMemPoolEntry& a,
                        const CTxMemPoolEntry& b)
    {
        return CompareTxMemPoolEntryByScore()(a, b);
    }
};
} // namespace

/**
* Compare 2 transactions to determine their relative priority.
* Does it wothout taking the mutex; it is up to the caller to
* ensure this is thread safe.
*/
bool CTxMemPool::CompareDepthAndScoreNL(const uint256 &hasha,
                                        const uint256 &hashb)
{
    const auto i = mapTx.find(hasha);
    if (i == mapTx.end()) {
        return false;
    }
    const auto j = mapTx.find(hashb);
    if (j == mapTx.end()) {
        return true;
    }
    return DepthAndScoreComparator()(i, j);
}

std::vector<CTxMemPool::txiter>
CTxMemPool::getSortedDepthAndScoreNL() const {
    std::vector<txiter> iters;
    iters.reserve(mapTx.size());
    for (txiter mi = mapTx.begin(); mi != mapTx.end(); ++mi) {
        iters.push_back(mi);
    }

    std::sort(iters.begin(), iters.end(), DepthAndScoreComparator());
    return iters;
}


void CTxMemPool::InitMempoolTxDB() {
    static constexpr auto cacheSize = 1 << 20; /*TODO: remove constant*/
    std::call_once(db_initialized,
                   [this] {
                       mempoolTxDB = std::make_shared<CAsyncMempoolTxDB>(cacheSize);
                   });
}

uint64_t CTxMemPool::GetDiskUsage() {
    InitMempoolTxDB();
    return mempoolTxDB->GetDiskUsage();
};

uint64_t CTxMemPool::GetDiskTxCount() {
    InitMempoolTxDB();
    return mempoolTxDB->GetTxCount();
};

void CTxMemPool::SaveTxsToDisk(uint64_t requiredSize) {
    uint64_t movedToDiskSize = 0;
    std::vector<CTransactionWrapperRef> toBeMoved;

    for (auto mi = mapTx.get<entry_time>().begin();
         mi != mapTx.get<entry_time>().end() && movedToDiskSize < requiredSize;
         ++mi) {
        if (mi->IsInMemory()) {
            toBeMoved.push_back(mi->tx);
            movedToDiskSize += mi->GetTxSize();
        }
    }

    InitMempoolTxDB();
    mempoolTxDB->Add(std::move(toBeMoved));

    if (movedToDiskSize < requiredSize)
    {
        LogPrint(BCLog::MEMPOOL,
                 "Less than required amount of memory was freed. Required: %d,  freed: %d\n",
                 requiredSize, movedToDiskSize);
    }
}

void CTxMemPool::QueryHashes(std::vector<uint256> &vtxid) {
    std::shared_lock lock(smtx);
    auto iters = getSortedDepthAndScoreNL();

    vtxid.clear();
    vtxid.reserve(mapTx.size());

    for (auto it : iters) {
        vtxid.push_back(it->GetTxId());
    }
}

std::vector<TxMempoolInfo> CTxMemPool::InfoAll() const {
    std::shared_lock lock(smtx);
    return InfoAllNL();
}

std::vector<TxMempoolInfo> CTxMemPool::InfoAllNL() const {
    auto iters = getSortedDepthAndScoreNL();
    std::vector<TxMempoolInfo> ret;
    ret.reserve(mapTx.size());
    for (auto it : iters) {
        ret.push_back(TxMempoolInfo{*it});
    }
    return ret;
}

CTransactionRef CTxMemPool::Get(const uint256 &txid) const {
    std::shared_lock lock(smtx);
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
    std::shared_lock lock(smtx);
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
    double dPriorityDelta,
    const Amount nFeeDelta) {

    {
        std::unique_lock lock(smtx);
        prioritiseTransactionNL(hash, dPriorityDelta, nFeeDelta);
    }
    LogPrintf("PrioritiseTransaction: %s priority += %f, fee += %d\n", strHash,
        dPriorityDelta, FormatMoney(nFeeDelta));
}

void CTxMemPool::PrioritiseTransaction(
    const std::vector<TxId>& vTxToPrioritise,
    double dPriorityDelta,
    const Amount nFeeDelta) {

    if (vTxToPrioritise.empty()) {
        return;
    }
    {
        std::unique_lock lock(smtx);
        for(const TxId& txid: vTxToPrioritise) {
            prioritiseTransactionNL(txid, dPriorityDelta, nFeeDelta);
        }
    }
    for(const TxId& txid: vTxToPrioritise) {
        LogPrintf("PrioritiseTransaction: %s priority += %f, fee += %d\n",
            txid.ToString(),
            dPriorityDelta,
            FormatMoney(nFeeDelta));
    }
}

void CTxMemPool::ApplyDeltas(const uint256& hash, double &dPriorityDelta,
                             Amount &nFeeDelta) const {
    std::shared_lock lock(smtx);
    ApplyDeltasNL(hash, dPriorityDelta, nFeeDelta);
}

void CTxMemPool::ApplyDeltasNL(
        const uint256& hash,
        double &dPriorityDelta,
        Amount &nFeeDelta) const {

    std::map<uint256, std::pair<double, Amount>>::const_iterator pos =
        mapDeltas.find(hash);
    if (pos == mapDeltas.end()) {
        return;
    }
    const std::pair<double, Amount> &deltas = pos->second;
    dPriorityDelta += deltas.first;
    nFeeDelta += deltas.second;
}

void CTxMemPool::prioritiseTransactionNL(
    const uint256& hash,
    double dPriorityDelta,
    const Amount nFeeDelta) {

    std::pair<double, Amount> &deltas = mapDeltas[hash];
    deltas.first += dPriorityDelta;
    deltas.second += nFeeDelta;
    txiter it = mapTx.find(hash);
    if (it != mapTx.end()) {
        mapTx.modify(it, update_fee_delta(deltas.second));
        auto changeSet = mJournalBuilder.getNewChangeSet(JournalUpdateReason::UNKNOWN); // TODO: add new update reason (PRIORITY?)

        setEntriesTopoSorted entries;
        entries.insert(it);

        if(it->IsInPrimaryMempool())
        {
            entries = RemoveFromPrimaryMempoolNL(entries, *changeSet, false);
        }

        TryAcceptToPrimaryMempoolNL(std::move(entries), *changeSet);
    }
}

void CTxMemPool::clearPrioritisationNL(const uint256& hash) {
    mapDeltas.erase(hash);
}


void CTxMemPool::GetDeltasAndInfo(std::map<uint256, Amount>& deltas,
                                  std::vector<TxMempoolInfo>& info) const
{
    deltas.clear();
    std::shared_lock lock {smtx};
    for (const auto &i : mapDeltas) {
        deltas[i.first] = i.second.second;
    }
    info = InfoAllNL();
}


bool CTxMemPool::HasNoInputsOf(const CTransaction &tx) const {
    std::shared_lock lock(smtx);
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
            return CoinImpl::MakeNonOwningWithScript(ptx->vout[outpoint.GetN()], MEMPOOL_HEIGHT, false);
        }
        return {};
    }

    return mDBView.GetCoin(outpoint, maxScriptSize);
}

size_t CTxMemPool::DynamicMemoryUsage() const {
    std::shared_lock lock(smtx);
    return DynamicMemoryUsageNL();
}

size_t CTxMemPool::DynamicMemoryUsageNL() const {
    // Estimate the overhead of mapTx to be 12 pointers + two allocations, as
    // no exact formula for boost::multi_index_container is implemented.
    return mapTx.size() * memusage::MallocUsage(sizeof(CTxMemPoolEntry) +
                                                sizeof(CTransactionWrapper) +
                                                12 * sizeof(void *)) +
           memusage::DynamicUsage(mapNextTx) +
           memusage::DynamicUsage(mapDeltas) +
           memusage::DynamicUsage(mapLinks) + cachedInnerUsage;
}

void CTxMemPool::removeStagedNL(
    setEntries& stage,
    mining::CJournalChangeSet& changeSet,
    MemPoolRemovalReason reason,
    const CTransaction* conflictedWith)
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
    removeUncheckedNL(stage, changeSet, reason, conflictedWith);

    // check if removed transactions can be re-accepted to the primary mempool
    TryAcceptToPrimaryMempoolNL(std::move(toUpdateAfterDeletion), changeSet);
}

int CTxMemPool::Expire(int64_t time, const mining::CJournalChangeSetPtr& changeSet)
{
    std::unique_lock lock(smtx);
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
    removeStagedNL(stage, nonNullChangeSet.Get(), MemPoolRemovalReason::EXPIRY);
    return stage.size();
}

std::set<CTransactionRef> CTxMemPool::CheckTxConflicts(const CTransactionRef& tx, bool isFinal) const
{
    std::shared_lock lock(smtx);
    std::set<CTransactionRef> conflictsWith;

    // Check our locked UTXOs
    for (const CTxIn &txin : tx->vin) {
        if (auto it = mapNextTx.find(txin.prevout); it != mapNextTx.end()) {
            conflictsWith.insert(GetNL(it->second->GetId()));
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

void CTxMemPool::ResubmitEntriesToMempoolNL(CTxMemPool::indexed_transaction_set& oldMapTx, const CJournalChangeSetPtr& changeSet)
{
    auto& tempMapTxSequenced = oldMapTx.get<insertion_order>();
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
        AddUncheckedNL(itTemp->GetTxId(), *itTemp, changeSet);
        tempMapTxSequenced.erase(itTemp++);
    }
}

void CTxMemPool::AddToMempoolForReorg(const Config &config,
    DisconnectedBlockTransactions &disconnectpool,
    const CJournalChangeSetPtr& changeSet)
{
    AssertLockHeld(cs_main);
    TxInputDataSPtrVec vTxInputData {};
    // disconnectpool's insertion_order index sorts the entries from oldest to
    // newest, but the oldest entry will be the last tx from the latest mined
    // block that was disconnected.
    // Iterate disconnectpool in reverse, so that we add transactions back to
    // the mempool starting with the earliest transaction that had been
    // previously seen in a block.
    auto it = disconnectpool.queuedTx.get<insertion_order>().rbegin();
    while (it != disconnectpool.queuedTx.get<insertion_order>().rend()) {
        if ((*it)->IsCoinBase()) {
            // If the transaction doesn't make it in to the mempool, remove any
            // transactions that depend on it (which would now be orphans).
            RemoveRecursive(**it, changeSet, MemPoolRemovalReason::REORG);
        } else {
            vTxInputData.emplace_back(
                std::make_shared<CTxInputData>(
                    TxIdTrackerWPtr{}, // TxIdTracker is not used during reorgs
                    *it,              // a pointer to the tx
                    TxSource::reorg,  // tx source
                    TxValidationPriority::normal,  // tx validation priority
                    TxStorage::memory, // tx storage
                    GetTime(),        // nAcceptTime
                    false));          // fLimitFree
        }
        ++it;
    }

    disconnectpool.queuedTx.clear();

    // we are about to delete journal, changes in the changeSet make no sense now
    if(changeSet)
    {
        changeSet->clear();
    }

    // Clear the mempool, but save the current index, entries and the
    // transaction database, since we'll re-add the entries later.
    indexed_transaction_set tempMapTx;
    {
        std::unique_lock lock(smtx);
        std::swap(tempMapTx, mapTx);
        clearNL(true);          // Do not clear the transaction database
    }

    // Validate the set of transactions from the disconnectpool and add them to the mempool
    g_connman->getTxnValidator()->processValidation(vTxInputData, changeSet, true);

    // Add original mempool contents on top to preserve toposort
    {
        std::unique_lock lock {smtx};

        // now put all transactions that were in the mempool before
        ResubmitEntriesToMempoolNL(tempMapTx, changeSet);

        // Disconnectpool related updates
        for (const auto& txInputData : vTxInputData) {
            auto const txid = txInputData->GetTxnPtr()->GetId();
            if (!ExistsNL(txid)) {
                // If the transaction doesn't make it in to the mempool, remove any
                // transactions that depend on it (which would now be orphans).
                removeRecursiveNL(txid, changeSet, MemPoolRemovalReason::REORG);
            }
        }
    }

    // We also need to remove any now-immature transactions
    LogPrint(BCLog::MEMPOOL, "Removing any now-immature transactions\n");
    const CBlockIndex& tip = *chainActive.Tip();
    RemoveForReorg(
            config,
            *pcoinsTip,
            changeSet,
            tip,
            StandardNonFinalVerifyFlags(IsGenesisEnabled(config, tip.nHeight)));

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
    // disconnectpool's insertion_order index sorts the entries from oldest to
    // newest, but the oldest entry will be the last tx from the latest mined
    // block that was disconnected.
    // Iterate disconnectpool in reverse, so that we add transactions back to
    // the mempool starting with the earliest transaction that had been
    // previously seen in a block.
    auto it = disconnectpool.queuedTx.get<insertion_order>().rbegin();
    while (it != disconnectpool.queuedTx.get<insertion_order>().rend()) {
        RemoveRecursive(**it, changeSet, MemPoolRemovalReason::REORG);
        ++it;
    }
    disconnectpool.queuedTx.clear();
    // We also need to remove any now-immature transactions
    LogPrint(BCLog::MEMPOOL, "Removing any now-immature transactions\n");
    const CBlockIndex& tip = *chainActive.Tip();
    RemoveForReorg(
            config,
            *pcoinsTip,
            changeSet,
            tip,
            StandardNonFinalVerifyFlags(IsGenesisEnabled(config, tip.nHeight)));

    // Check mempool & journal
    CheckMempool(*pcoinsTip, changeSet);

    // Mempool is now consistent. Synchronize with journal.
    changeSet->apply();
}

void CTxMemPool::AddToDisconnectPoolUpToLimit(
    const mining::CJournalChangeSetPtr &changeSet,
    DisconnectedBlockTransactions *disconnectpool,
    uint64_t maxDisconnectedTxPoolSize,
    const std::vector<CTransactionRef> &vtx) {
    for (const auto &tx : boost::adaptors::reverse(vtx)) {
        disconnectpool->addTransaction(tx);
    }
    // FIXME: SVDEV-460 add only upto limit and drop the rest. Figure out all this reversal and what to drop

    while (disconnectpool->DynamicMemoryUsage() > maxDisconnectedTxPoolSize) {
        // Drop the earliest entry, and remove its children from the
        // mempool.
        auto it = disconnectpool->queuedTx.get<insertion_order>().begin();
        RemoveRecursive(**it, changeSet, MemPoolRemovalReason::REORG);
        disconnectpool->removeEntry(it);
    }
}


void CTxMemPool::updateChildNL(txiter entry, txiter child, bool add) {
    // FIXME: Implement IncrementalDynamicUsage for std::unordered_set
    // before it was: static size_t memUsage = memusage::IncrementalDynamicUsage(setEntries());
    static size_t memUsage = 0;
    if (add && mapLinks[entry].children.insert(child).second) {
        cachedInnerUsage += memUsage;
    } else if (!add && mapLinks[entry].children.erase(child)) {
        cachedInnerUsage -= memUsage;
    }
}

void CTxMemPool::updateParentNL(txiter entry, txiter parent, bool add) {
    // FIXME: Implement IncrementalDynamicUsage for std::unordered_set
    // before it was: static size_t memUsage = memusage::IncrementalDynamicUsage(setEntries());
    static size_t memUsage = 0;
    if (add && mapLinks[entry].parents.insert(parent).second) {
        cachedInnerUsage += memUsage;
    } else if (!add && mapLinks[entry].parents.erase(parent)) {
        cachedInnerUsage -= memUsage;
    }
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

CFeeRate CTxMemPool::GetMinFee(size_t sizelimit) const {
    std::shared_lock lock(smtx);
    if (!blockSinceLastRollingFeeBump || rollingMinimumFeeRate == 0) {
        return CFeeRate(Amount(int64_t(rollingMinimumFeeRate)));
    }

    int64_t time = GetTime();
    if (time > lastRollingFeeUpdate + 10) {
        double halflife = ROLLING_FEE_HALFLIFE;
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
    return CFeeRate(Amount(int64_t(rollingMinimumFeeRate)));
}

int64_t CTxMemPool::evaluateEvictionCandidateNL(txiter entry)
{
    if(entry->IsCPFPGroupMember())
    {
        const auto& evalParams = entry->GetCPFPGroup()->evaluationParams;
        return (evalParams.fee + evalParams.feeDelta).GetSatoshis() * 1000 / entry->GetTxSize();
    }
    
    int64_t score = entry->GetFee().GetSatoshis() * 1000 / entry->GetTxSize();
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
            auto groupParams = entry->GetCPFPGroup()->evaluationParams;
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
        GetDescendantsNL(mapTx.project<0>(it), stage);
        nTxnRemoved += stage.size();

        std::vector<CTransactionRef> txn;
        if (pvNoSpendsRemaining) {
            txn.reserve(stage.size());
            for (txiter iter : stage) {
                auto txref = iter->GetSharedTx();
                txn.push_back(txref);
                vRemovedTxIds.emplace_back(txref->GetId());
            }
        }
        removeStagedNL(stage, nonNullChangeSet.Get(), MemPoolRemovalReason::SIZELIMIT);
        if (pvNoSpendsRemaining) {
            for (const auto& txref : txn) {
                for (const auto& txin : txref->vin) {
                    if (ExistsNL(txin.prevout.GetTxId())) {
                        continue;
                    }
                    if (!mapNextTx.count(txin.prevout)) {
                        pvNoSpendsRemaining->push_back(txin.prevout);
                    }
                }
            }
        }
        weHaveEvictedSomething = true;
    }

    if (weHaveEvictedSomething) 
    {
        if(mapTx.size() != 0)
        {
            maxFeeRateRemoved = std::max(maxFeeRateRemoved, getFeeRate(evictionTracker->GetMostWorthless()));
        }
        maxFeeRateRemoved += MEMPOOL_FULL_FEE_INCREMENT;
        trackPackageRemovedNL(maxFeeRateRemoved);

        LogPrint(BCLog::MEMPOOL,
                 "Removed %u txn, rolling minimum fee bumped to %s\n",
                 nTxnRemoved, maxFeeRateRemoved.ToString());
    }
    return vRemovedTxIds;
}

bool CTxMemPool::TransactionWithinChainLimit(const uint256 &txid, int64_t maxAncestorCount, 
                                             int64_t maxSecondaryMempoolAncestorCount) const 
{
    std::shared_lock lock(smtx);
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
    std::shared_lock lock(smtx);
    return mapTx.size();
}

unsigned long CTxMemPool::PrimaryMempoolSizeNL() const {
    return mapTx.size() - secondaryMempoolSize;
}

uint64_t CTxMemPool::GetTotalTxSize() {
    std::shared_lock lock(smtx);
    return totalTxSize;
}

bool CTxMemPool::Exists(const uint256& hash) const {
    std::shared_lock lock(smtx);
    return ExistsNL(hash);
}

bool CTxMemPool::ExistsNL(const uint256& hash) const {
    return mapTx.count(hash) != 0;
}

bool CTxMemPool::Exists(const COutPoint &outpoint) const {
    std::shared_lock lock(smtx);
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
    std::shared_lock lock(smtx);

    Snapshot::Contents contents;
    contents.reserve(mapTx.size());
    for (const auto& entry : mapTx) {
        contents.emplace_back(entry);
    }
    return Snapshot(std::move(contents), nullptr);
}

CTxMemPool::Snapshot CTxMemPool::GetTxSnapshot(const uint256& hash, TxSnapshotKind kind) const
{
    std::shared_lock lock(smtx);

    const auto baseTx = mapTx.find(hash);
    if (baseTx == mapTx.end()) {
        return Snapshot();
    }

    Snapshot::Contents contents;
    auto relevantTxIds = std::make_unique<Snapshot::CachedTxIds>();
    // This closure is essentially a local function that stores
    // information about a single transaction and its inputs.
    const auto recordTransaction =
        [this, &contents, &relevantTxIds](const CTxMemPoolEntry& entry)
        {
            contents.emplace_back(entry);
            const auto tx = entry.GetSharedTx();
            for (const auto& input : tx->vin) {
                const auto& id = input.prevout.GetTxId();
                if (ExistsNL(id)) {
                    relevantTxIds->emplace_back(id);
                }
            }
        };

    if (kind == TxSnapshotKind::SINGLE)
    {
        // Store the single transaction of the snapshot.
        recordTransaction(*baseTx);
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
            recordTransaction(*baseTx);
        }
        else if (kind == TxSnapshotKind::ONLY_DESCENDANTS) {
            related.erase(baseTx);
        }
        for (const auto& iter : related) {
            recordTransaction(*iter);
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
    std::shared_lock lock(smtx);

    std::vector<CTransactionRef> result;
    result.reserve(mapTx.size());
    for (const auto& entry : mapTx) {
        result.emplace_back(entry.GetSharedTx());
    }
    return result;
}

static const uint64_t MEMPOOL_DUMP_VERSION = 1;

bool CTxMemPool::LoadMempool(const Config &config, const task::CCancellationToken& shutdownToken)
{
    try {
        int64_t nExpiryTimeout = config.GetMemPoolExpiry();
        FILE *filestr = fsbridge::fopen(GetDataDir() / "mempool.dat", "rb");
        CAutoFile file(filestr, SER_DISK, CLIENT_VERSION);
        if (file.IsNull()) {
            throw std::runtime_error("Failed to open mempool file from disk");
        }

        int64_t count = 0;
        int64_t skipped = 0;
        int64_t failed = 0;
        int64_t nNow = GetTime();

        uint64_t version;
        file >> version;
        if (version != MEMPOOL_DUMP_VERSION) {
            throw std::runtime_error("Bad mempool dump version");
        }
        uint64_t num;
        file >> num;
        double prioritydummy = 0;
        // Take a reference to the validator.
        const auto& txValidator = g_connman->getTxnValidator();
        // A pointer to the TxIdTracker.
        const TxIdTrackerWPtr& pTxIdTracker = g_connman->GetTxIdTracker();
        while (num--) {
            CTransactionRef tx;
            int64_t nTime;
            int64_t nFeeDelta;
            file >> tx;
            file >> nTime;
            file >> nFeeDelta;
            Amount amountdelta(nFeeDelta);
            if (amountdelta != Amount(0)) {
                PrioritiseTransaction(tx->GetId(),
                                              tx->GetId().ToString(),
                                              prioritydummy, amountdelta);
            }
            if (nTime + nExpiryTimeout > nNow) {
                // Mempool Journal ChangeSet
                CJournalChangeSetPtr changeSet {
                    getJournalBuilder().getNewChangeSet(JournalUpdateReason::INIT)
                };
                const CValidationState& state {
                    // Execute txn validation synchronously.
                    txValidator->processValidation(
                        std::make_shared<CTxInputData>(
                            pTxIdTracker, // a pointer to the TxIdTracker
                            tx,    // a pointer to the tx
                            TxSource::file, // tx source
                            TxValidationPriority::normal,  // tx validation priority
                            TxStorage::memory, // tx storage
                            nTime, // nAcceptTime
                            true),  // fLimitFree
                        changeSet, // an instance of the mempool journal
                        true) // fLimitMempoolSize
                };
                // Check results
                if (state.IsValid()) {
                    ++count;
                } else {
                    ++failed;
                }
            } else {
                ++skipped;
            }
            if (shutdownToken.IsCanceled()) {
                return false;
            }
        }
        std::map<uint256, Amount> mapDeltas;
        file >> mapDeltas;

        for (const auto &i : mapDeltas) {
            PrioritiseTransaction(i.first, i.first.ToString(),
                                          prioritydummy, i.second);
        }

        LogPrintf("Imported mempool transactions from disk: %i successes, %i "
                  "failed, %i expired\n",
                  count, failed, skipped);

    }
    catch (const std::exception &e) {
        LogPrintf("Failed to deserialize mempool data on disk: %s. Continuing anyway.\n",
                  e.what());
    }

    // Restore non-final transactions
    return getNonFinalPool().loadMempool(shutdownToken);
}

void CTxMemPool::DumpMempool(void) {
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

        CAutoFile file(filestr, SER_DISK, CLIENT_VERSION);

        uint64_t version = MEMPOOL_DUMP_VERSION;
        file << version;

        file << (uint64_t)vinfo.size();
        for (const auto &i : vinfo) {
            file << *(i.tx);
            file << (int64_t)i.nTime;
            file << (int64_t)i.nFeeDelta.GetSatoshis();
            mapDeltas.erase(i.tx->GetId());
        }

        file << mapDeltas;
        FileCommit(file.Get());
        file.fclose();
        RenameOver(GetDataDir() / "mempool.dat.new",
                   GetDataDir() / "mempool.dat");
        int64_t last = GetTimeMicros();
        LogPrintf("Dumped mempool: %.6fs to copy, %.6fs to dump\n",
                  (mid - start) * 0.000001, (last - mid) * 0.000001);
    } catch (const std::exception &e) {
        LogPrintf("Failed to dump mempool: %s. Continuing anyway.\n", e.what());
    }

    // Dump non-final pool
    getNonFinalPool().dumpMempool();
}

