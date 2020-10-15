// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2019-2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "txmempool.h"

#include "chainparams.h" // for GetConsensus.
#include "clientversion.h"
#include "consensus/consensus.h"
#include "consensus/validation.h"
#include "policy/fees.h"
#include "policy/policy.h"
#include "streams.h"
#include "timedata.h"
#include "txdb.h"
#include "util.h"
#include "utilmoneystr.h"
#include "utiltime.h"
#include "validation.h"
#include "validationinterface.h"
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

        bool HaveCoin(const COutPoint &outpoint) const override
        {
            return mempool.GetNL(outpoint.GetTxId()) || mDBView.HaveCoin(outpoint);
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
 * class CTxMemPoolEntry
 */
CTxMemPoolEntry::CTxMemPoolEntry(const CTransactionRef& _tx,
                                 const Amount _nFee,
                                 int64_t _nTime,
                                 double _entryPriority,
                                 int32_t _entryHeight,
                                 Amount _inChainInputValue,
                                 bool _spendsCoinbase,
                                 int64_t _sigOpsCount,
                                 LockPoints lp)
    : tx(_tx), nFee(_nFee), nTime(_nTime), entryPriority(_entryPriority),
      inChainInputValue(_inChainInputValue), sigOpCount(_sigOpsCount),
      lockPoints(lp), entryHeight(_entryHeight), spendsCoinbase(_spendsCoinbase)
{
    nTxSize = tx->GetTotalSize();
    nModSize = tx->CalculateModifiedSize(GetTxSize());
    nUsageSize = RecursiveDynamicUsage(tx);

    ancestorDescendantCounts = std::make_shared<AncestorDescendantCounts>(1, 1);
    nSizeWithDescendants = GetTxSize();
    nModFeesWithDescendants = nFee;
    Amount nValueIn = tx->GetValueOut() + nFee;
    assert(inChainInputValue <= nValueIn);

    feeDelta = Amount(0);

    nSizeWithAncestors = GetTxSize();
    nModFeesWithAncestors = nFee;
    nSigOpCountWithAncestors = sigOpCount;
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
    nModFeesWithDescendants += newFeeDelta - feeDelta;
    nModFeesWithAncestors += newFeeDelta - feeDelta;
    feeDelta = newFeeDelta;
}

void CTxMemPoolEntry::UpdateLockPoints(const LockPoints &lp) {
    lockPoints = lp;
}

// Update the given tx for any in-mempool descendants.
// Assumes that setMemPoolChildren is correct for the given tx and all
// descendants.
void CTxMemPool::updateForDescendantsNL(txiter updateIt,
                                        cacheMap &cachedDescendants,
                                        const std::set<uint256> &setExclude) {
    setEntries stageEntries, setAllDescendants;
    stageEntries = GetMemPoolChildrenNL(updateIt);

    while (!stageEntries.empty()) {
        const txiter cit = *stageEntries.begin();
        setAllDescendants.insert(cit);
        stageEntries.erase(cit);
        const setEntries &setChildren = GetMemPoolChildrenNL(cit);
        for (const txiter childEntry : setChildren) {
            cacheMap::iterator cacheIt = cachedDescendants.find(childEntry);
            if (cacheIt != cachedDescendants.end()) {
                // We've already calculated this one, just add the entries for
                // this set but don't traverse again.
                for (const txiter cacheEntry : cacheIt->second) {
                    setAllDescendants.insert(cacheEntry);
                }
            } else if (!setAllDescendants.count(childEntry)) {
                // Schedule for later processing
                stageEntries.insert(childEntry);
            }
        }
    }
    // setAllDescendants now contains all in-mempool descendants of updateIt.
    // Update and add to cached descendant map
    int64_t modifySize = 0;
    Amount modifyFee(0);
    int64_t modifyCount = 0;
    for (txiter cit : setAllDescendants) {
        if (!setExclude.count(cit->GetTx().GetId())) {
            modifySize += cit->GetTxSize();
            modifyFee += cit->GetModifiedFee();
            modifyCount++;
            cachedDescendants[updateIt].insert(cit);
            // Update ancestor state for each descendant
            mapTx.modify(cit,
                         update_ancestor_state(updateIt->GetTxSize(),
                                               updateIt->GetModifiedFee(), 1,
                                               updateIt->GetSigOpCount()));
        }
    }
    mapTx.modify(updateIt,
                 update_descendant_state(modifySize, modifyFee, modifyCount));
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


// vHashesToUpdate is the set of transaction hashes from a disconnected block
// which has been re-added to the mempool. For each entry, look for descendants
// that are outside hashesToUpdate, and add fee/size information for such
// descendants to the parent. For each such descendant, also update the ancestor
// state to include the parent.
void CTxMemPool::UpdateTransactionsFromBlock(
    const std::vector<uint256> &vHashesToUpdate,
    const CJournalChangeSetPtr &changeSet) {
    std::unique_lock lock(smtx);
    // For each entry in vHashesToUpdate, store the set of in-mempool, but not
    // in-vHashesToUpdate transactions, so that we don't have to recalculate
    // descendants when we come across a previously seen entry.
    cacheMap mapMemPoolDescendantsToUpdate;

    // Use a set for lookups into vHashesToUpdate (these entries are already
    // accounted for in the state of their ancestors)
    std::set<uint256> setAlreadyIncluded(vHashesToUpdate.begin(),
                                         vHashesToUpdate.end());

    // Iterate in reverse, so that whenever we are looking at at a transaction
    // we are sure that all in-mempool descendants have already been processed.
    // This maximizes the benefit of the descendant cache and guarantees that
    // setMemPoolChildren will be updated, an assumption made in
    // updateForDescendantsNL.
    setEntries addedTransactions;
    for (const uint256 &hash : boost::adaptors::reverse(vHashesToUpdate)) {
        // we cache the in-mempool children to avoid duplicate updates
        setEntries setChildren;
        // calculate children from mapNextTx
        txiter it = mapTx.find(hash);
        if (it == mapTx.end()) {
            continue;
        }
        addedTransactions.insert(it);
        auto iter = mapNextTx.lower_bound(COutPoint(hash, 0));
        // First calculate the children, and update setMemPoolChildren to
        // include them, and update their setMemPoolParents to include this tx.
        for (; iter != mapNextTx.end() && iter->first->GetTxId() == hash;
             ++iter) {
            const uint256 &childHash = iter->second->GetId();
            txiter childIter = mapTx.find(childHash);
            assert(childIter != mapTx.end());
            // We can skip updating entries we've encountered before or that are
            // in the block (which are already accounted for).
            if (setChildren.insert(childIter).second &&
                !setAlreadyIncluded.count(childHash)) {
                updateChildNL(it, childIter, true);
                updateParentNL(childIter, it, true);
            }
        }
        updateForDescendantsNL(it, mapMemPoolDescendantsToUpdate,
                             setAlreadyIncluded);
    }

    CEnsureNonNullChangeSet nonNullChangeSet(*this, changeSet);

    // Now we will check transactions connected with newly added transactions 
    // We are doing this because it could be that a newly added transaction
    // did not end up in the journal but have descendants which are in the journal already.
    setEntries affected = getConnectedNL(addedTransactions);
    checkJournalAcceptanceNL(affected, nonNullChangeSet.Get());
}

bool CTxMemPool::CalculateMemPoolAncestors(
    const CTxMemPoolEntry &entry,
    setEntries &setAncestors,
    uint64_t limitAncestorCount,
    uint64_t limitAncestorSize,
    uint64_t limitDescendantCount,
    uint64_t limitDescendantSize,
    std::string &errString,
    bool fSearchForParents /* = true */) const {

    std::shared_lock lock(smtx);
    return CalculateMemPoolAncestorsNL(entry,
                                       setAncestors,
                                       limitAncestorCount,
                                       limitAncestorSize,
                                       limitDescendantCount,
                                       limitDescendantSize,
                                       errString,
                                       fSearchForParents);
}

bool CTxMemPool::CalculateMemPoolAncestorsNL(
    const CTxMemPoolEntry &entry,
    setEntries &setAncestors,
    uint64_t limitAncestorCount,
    uint64_t limitAncestorSize,
    uint64_t limitDescendantCount,
    uint64_t limitDescendantSize,
    std::string &errString,
    bool fSearchForParents /* = true */) const {

    setEntries parentHashes;
    const CTransaction &tx = entry.GetTx();

    if (fSearchForParents) {
        // Get parents of this transaction that are in the mempool
        // GetMemPoolParentsNL() is only valid for entries in the mempool, so we
        // iterate mapTx to find parents.
        for (const CTxIn &in : tx.vin) {
            txiter piter = mapTx.find(in.prevout.GetTxId());
            if (piter == mapTx.end()) {
                continue;
            }
            parentHashes.insert(piter);
            if (parentHashes.size() + 1 > limitAncestorCount) {
                errString =
                    strprintf("too many unconfirmed parents [limit: %u]",
                              limitAncestorCount);
                return false;
            }
        }
    } else {
        // If we're not searching for parents, we require this to be an entry in
        // the mempool already.
        txiter it = mapTx.iterator_to(entry);
        parentHashes = GetMemPoolParentsNL(it);
    }

    size_t totalSizeWithAncestors = entry.GetTxSize();

    while (!parentHashes.empty()) {
        txiter stageit = *parentHashes.begin();

        setAncestors.insert(stageit);
        parentHashes.erase(stageit);
        totalSizeWithAncestors += stageit->GetTxSize();

        if (stageit->GetSizeWithDescendants() + entry.GetTxSize() >
            limitDescendantSize) {
            errString = strprintf(
                "exceeds descendant size limit for tx %s [limit: %u]",
                stageit->GetTx().GetId().ToString(), limitDescendantSize);
            return false;
        }

        if (stageit->GetCountWithDescendants() + 1 > limitDescendantCount) {
            errString = strprintf("too many descendants for tx %s [limit: %u]",
                                  stageit->GetTx().GetId().ToString(),
                                  limitDescendantCount);
            return false;
        }

        if (totalSizeWithAncestors > limitAncestorSize) {
            errString = strprintf("exceeds ancestor size limit [limit: %u]",
                                  limitAncestorSize);
            return false;
        }

        const setEntries &setMemPoolParents = GetMemPoolParentsNL(stageit);
        for (const txiter &phash : setMemPoolParents) {
            // If this is a new ancestor, add it.
            if (setAncestors.count(phash) == 0) {
                parentHashes.insert(phash);
            }
            if (parentHashes.size() + setAncestors.size() + 1 >
                limitAncestorCount) {
                errString =
                    strprintf("too many unconfirmed ancestors [limit: %u]",
                              limitAncestorCount);
                return false;
            }
        }
    }

    return true;
}

void CTxMemPool::updateAncestorsOfNL(bool add,
                                     txiter it,
                                     setEntries &setAncestors) {
    setEntries parentIters = GetMemPoolParentsNL(it);
    // add or remove this tx as a child of each parent
    for (txiter piter : parentIters) {
        updateChildNL(piter, it, add);
    }
    const int64_t updateCount = (add ? 1 : -1);
    const int64_t updateSize = updateCount * it->GetTxSize();
    const Amount updateFee = updateCount * it->GetModifiedFee();
    for (txiter ancestorIt : setAncestors) {
        mapTx.modify(ancestorIt, update_descendant_state(updateSize, updateFee,
                                                         updateCount));
    }
}

void CTxMemPool::updateEntryForAncestorsNL(txiter it,
                                           const setEntries &setAncestors) {
    int64_t updateCount = setAncestors.size();
    int64_t updateSize = 0;
    Amount updateFee(0);
    int64_t updateSigOpsCount = 0;
    for (txiter ancestorIt : setAncestors) {
        updateSize += ancestorIt->GetTxSize();
        updateFee += ancestorIt->GetModifiedFee();
        updateSigOpsCount += ancestorIt->GetSigOpCount();
    }
    mapTx.modify(it, update_ancestor_state(updateSize, updateFee, updateCount,
                                           updateSigOpsCount));
}

void CTxMemPool::updateChildrenForRemovalNL(txiter it) {
    const setEntries &setMemPoolChildren = GetMemPoolChildrenNL(it);
    for (txiter updateIt : setMemPoolChildren) {
         updateParentNL(updateIt, it, false);
    }
}

void CTxMemPool::updateForRemoveFromMempoolNL(const setEntries &entriesToRemove,
                                            bool updateDescendants) {
    // For each entry, walk back all ancestors and decrement size associated
    // with this transaction.
    const uint64_t nNoLimit = std::numeric_limits<uint64_t>::max();
    if (updateDescendants) {
        // updateDescendants should be true whenever we're not recursively
        // removing a tx and all its descendants, eg when a transaction is
        // confirmed in a block. Here we only update statistics and not data in
        // mapLinks (which we need to preserve until we're finished with all
        // operations that need to traverse the mempool).
        for (txiter removeIt : entriesToRemove) {
            setEntries setDescendants;
            CalculateDescendantsNL(removeIt, setDescendants);
            setDescendants.erase(removeIt); // don't update state for self
            int64_t modifySize = -((int64_t)removeIt->GetTxSize());
            Amount modifyFee = -1 * removeIt->GetModifiedFee();
            int modifySigOps = -removeIt->GetSigOpCount();
            for (txiter dit : setDescendants) {
                mapTx.modify(dit, update_ancestor_state(modifySize, modifyFee,
                                                        -1, modifySigOps));
            }
        }
    }

    for (txiter removeIt : entriesToRemove) {
        setEntries setAncestors;
        const CTxMemPoolEntry &entry = *removeIt;
        std::string dummy;
        // Since this is a tx that is already in the mempool, we can call CMPA
        // with fSearchForParents = false.  If the mempool is in a consistent
        // state, then using true or false should both be correct, though false
        // should be a bit faster.
        // However, if we happen to be in the middle of processing a reorg, then
        // the mempool can be in an inconsistent state. In this case, the set of
        // ancestors reachable via mapLinks will be the same as the set of
        // ancestors whose packages include this transaction, because when we
        // add a new transaction to the mempool in AddUnchecked(), we assume it
        // has no children, and in the case of a reorg where that assumption is
        // false, the in-mempool children aren't linked to the in-block tx's
        // until UpdateTransactionsFromBlock() is called. So if we're being
        // called during a reorg, ie before UpdateTransactionsFromBlock() has
        // been called, then mapLinks[] will differ from the set of mempool
        // parents we'd calculate by searching, and it's important that we use
        // the mapLinks[] notion of ancestor transactions as the set of things
        // to update for removal.
        CalculateMemPoolAncestorsNL(entry,
                                    setAncestors,
                                    nNoLimit,
                                    nNoLimit,
                                    nNoLimit,
                                    nNoLimit,
                                    dummy,
                                    false);
        // Note that updateAncestorsOfNL severs the child links that point to
        // removeIt in the entries for the parents of removeIt.
        updateAncestorsOfNL(false,
                            removeIt,
                            setAncestors);
    }
    // After updating all the ancestor sizes, we can now sever the link between
    // each transaction being removed and any mempool children (ie, update
    // setMemPoolParents for each direct child of a transaction being removed).
    for (txiter removeIt : entriesToRemove) {
        updateChildrenForRemovalNL(removeIt);
    }
}

void CTxMemPoolEntry::UpdateDescendantState(int64_t modifySize,
                                            Amount modifyFee,
                                            int64_t modifyCount) {
    nSizeWithDescendants += modifySize;
    assert(int64_t(nSizeWithDescendants) > 0);
    nModFeesWithDescendants += modifyFee;
    ancestorDescendantCounts->nCountWithDescendants += modifyCount;
    assert(int64_t(ancestorDescendantCounts->nCountWithDescendants) > 0);
}

void CTxMemPoolEntry::UpdateAncestorState(int64_t modifySize, Amount modifyFee,
                                          int64_t modifyCount,
                                          int modifySigOps) {
    nSizeWithAncestors += modifySize;
    assert(int64_t(nSizeWithAncestors) > 0);
    nModFeesWithAncestors += modifyFee;
    ancestorDescendantCounts->nCountWithAncestors += modifyCount;
    assert(int64_t(ancestorDescendantCounts->nCountWithAncestors) > 0);
    nSigOpCountWithAncestors += modifySigOps;
    assert(int(nSigOpCountWithAncestors) >= 0);
}

CTxMemPool::CTxMemPool() : nTransactionsUpdated(0) {
    // lock free clear
    clearNL();

    // Sanity checks off by default for performance, because otherwise accepting
    // transactions becomes O(N^2) where N is the number of transactions in the
    // pool
    nCheckFrequency = 0;
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

const CTransaction* CTxMemPool::IsSpentBy(const COutPoint &outpoint) const {
    std::shared_lock lock{ smtx };

    auto it = mapNextTx.find(outpoint);
    if (it == mapNextTx.end())
    {
        return nullptr;
    }
    return it->second;
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
    setEntries &setAncestors,
    const CJournalChangeSetPtr& changeSet,
    size_t* pnMempoolSize,
    size_t* pnDynamicMemoryUsage) {

    {
        std::unique_lock lock(smtx);
        // Add to memory pool without checking anything.
        AddUncheckedNL(
             hash,
             entry,
             setAncestors,
             changeSet,
             pnMempoolSize,
             pnDynamicMemoryUsage);
    }
    // Notify entry added without holding the mempool's lock
    NotifyEntryAdded(entry.GetSharedTx());
}

void CTxMemPool::AddUncheckedNL(
    const uint256 &hash,
    const CTxMemPoolEntry &entry,
    setEntries &setAncestors,
    const CJournalChangeSetPtr& changeSet,
    size_t* pnMempoolSize,
    size_t* pnDynamicMemoryUsage) {

    indexed_transaction_set::iterator newit = mapTx.insert(entry).first;
    mapLinks.insert(make_pair(newit, TxLinks()));

    // Update transaction for any feeDelta created by PrioritiseTransaction
    // TODO: refactor so that the fee delta is calculated before inserting into
    // mapTx.
    std::map<uint256, std::pair<double, Amount>>::const_iterator pos =
        mapDeltas.find(hash);
    if (pos != mapDeltas.end()) {
        const std::pair<double, Amount> &deltas = pos->second;
        if (deltas.second != Amount(0)) {
            mapTx.modify(newit, update_fee_delta(deltas.second));
        }
    }

    // Update cachedInnerUsage to include contained transaction's usage.
    // (When we update the entry for in-mempool parents, memory usage will be
    // further updated.)
    cachedInnerUsage += entry.DynamicMemoryUsage();

    const CTransaction &tx = newit->GetTx();
    std::set<uint256> setParentTransactions;
    for (const CTxIn &in : tx.vin) {
        mapNextTx.insert(std::make_pair(&in.prevout, &tx));
        setParentTransactions.insert(in.prevout.GetTxId());
    }
    // Don't bother worrying about child transactions of this one. Normal case
    // of a new transaction arriving is that there can't be any children,
    // because such children would be orphans. An exception to that is if a
    // transaction enters that used to be in a block. In that case, our
    // disconnect block logic will call UpdateTransactionsFromBlock to clean up
    // the mess we're leaving here.

    // Update ancestors with information about this tx
    for (const uint256 &phash : setParentTransactions) {
        txiter pit = mapTx.find(phash);
        if (pit != mapTx.end()) {
            updateParentNL(newit, pit, true);
        }
    }
    updateAncestorsOfNL(true, newit, setAncestors);
    updateEntryForAncestorsNL(newit, setAncestors);

    nTransactionsUpdated++;
    totalTxSize += entry.GetTxSize();

    vTxHashes.emplace_back(tx.GetHash(), newit);
    newit->vTxHashesIdx = vTxHashes.size() - 1;

    // If it is required calculate mempool size & dynamic memory usage.
    if (pnMempoolSize) {
        *pnMempoolSize = mapTx.size();
    }
    if (pnDynamicMemoryUsage) {
        *pnDynamicMemoryUsage = DynamicMemoryUsageNL();
    }
    
    // Check if the transaction by itself pays enough for mining.
    // If not it will not enter the journal nor any other transaction will be affected, so
    // we can skip journal related stuff
    if (blockMinTxfee.GetFee(newit->GetTxSize()) <= newit->GetModifiedFee())
    {
        // transaction pays enough for mining
        
        CEnsureNonNullChangeSet nonNullChangeSet(*this, changeSet);

        auto filterOutAlreadyAccepted = [this, &changeSet](txiter entry)
        {
            auto txid = entry->GetTx().GetId();
            if (mJournalBuilder.getCurrentJournal()->checkTxnExists(txid))
            {
                return false;
            }
                
            if (changeSet && changeSet->checkTxnAdded(txid))
            {
                return false;
            }
            return true;
        };
        // we will limit ourself to check limited number of transactions in order 
        // to prevent possible attacks. 
        constexpr size_t MAX_TX_TO_CONSIDER = 100;

        // transaction connected to newit which did not make to the journal
        setEntries affected = getConnectedNL(setEntries{newit}, filterOutAlreadyAccepted, MAX_TX_TO_CONSIDER);
        affected.insert(newit);
        
        checkJournalAcceptanceNL(affected, nonNullChangeSet.Get());
    }
}

void CTxMemPool::removeUncheckedNL(
    txiter it,
    const CJournalChangeSetPtr& changeSet,
    MemPoolRemovalReason reason,
    const CTransaction* conflictedWith) {


    CTransactionRef txn { it->GetSharedTx() };
    NotifyEntryRemoved(txn, reason);
    for (const CTxIn &txin : txn->vin) {
        mapNextTx.erase(txin.prevout);
    }

    if (vTxHashes.size() > 1) {
        vTxHashes[it->vTxHashesIdx] = std::move(vTxHashes.back());
        vTxHashes[it->vTxHashesIdx].second->vTxHashesIdx = it->vTxHashesIdx;
        vTxHashes.pop_back();
        if (vTxHashes.size() * 2 < vTxHashes.capacity()) {
            vTxHashes.shrink_to_fit();
        }
    } else {
        vTxHashes.clear();
    }

    // Apply to the current journal, either via the passed in change set or directly ourselves
    CEnsureNonNullChangeSet nonNullChangeSet(*this, changeSet);
    nonNullChangeSet.Get().addOperation(CJournalChangeSet::Operation::REMOVE, { *it });

    totalTxSize -= it->GetTxSize();
    cachedInnerUsage -= it->DynamicMemoryUsage();
    cachedInnerUsage -= memusage::DynamicUsage(mapLinks[it].parents) +
                        memusage::DynamicUsage(mapLinks[it].children);
    mapLinks.erase(it);
    mapTx.erase(it);

    if (reason == MemPoolRemovalReason::BLOCK || reason == MemPoolRemovalReason::REORG)
    {
        GetMainSignals().TransactionRemovedFromMempoolBlock(txn->GetId(), reason);
    }
    else
    {   
        GetMainSignals().TransactionRemovedFromMempool(txn->GetId(), reason, conflictedWith);
    }

    nTransactionsUpdated++;
}

// Calculates descendants of entry that are not already in setDescendants, and
// adds to setDescendants. Assumes entryit is already a tx in the mempool and
// setMemPoolChildren is correct for tx and all descendants. Also assumes that
// if an entry is in setDescendants already, then all in-mempool descendants of
// it are already in setDescendants as well, so that we can save time by not
// iterating over those entries.
void CTxMemPool::CalculateDescendants(txiter entryit,
                                      setEntries &setDescendants) {
    std::shared_lock lock(smtx);
    CalculateDescendantsNL(entryit, setDescendants);
}

void CTxMemPool::CalculateDescendantsNL(txiter entryit,
                                        setEntries &setDescendants) {
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
            origTx,
            changeSet,
            reason);
    }
}

void CTxMemPool::removeRecursiveNL(
    const CTransaction &origTx,
    const CJournalChangeSetPtr& changeSet,
    MemPoolRemovalReason reason,
    const CTransaction* conflictedWith) {

    setEntries txToRemove;
    txiter origit = mapTx.find(origTx.GetId());
    if (origit != mapTx.end()) {
        txToRemove.insert(origit);
    } else {
        // When recursively removing but origTx isn't in the mempool be sure to
        // remove any children that are in the pool. This can happen during
        // chain re-orgs if origTx isn't re-accepted into the mempool for any
        // reason.
        for (size_t i = 0; i < origTx.vout.size(); i++) {
            auto it = mapNextTx.find(COutPoint(origTx.GetId(), i));
            if (it == mapNextTx.end()) {
                continue;
            }

            txiter nextit = mapTx.find(it->second->GetId());
            assert(nextit != mapTx.end());
            txToRemove.insert(nextit);
        }
    }
    setEntries setAllRemoves;
    for (txiter it : txToRemove) {
        CalculateDescendantsNL(it, setAllRemoves);
    }

    removeStagedNL(setAllRemoves, false, changeSet, reason, true, conflictedWith);
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
    for (indexed_transaction_set::const_iterator it = mapTx.begin();
         it != mapTx.end(); it++) {
        const CTransaction &tx = it->GetTx();
        LockPoints lp = it->GetLockPoints();
        bool validLP = TestLockPointValidity(&lp);

        CoinsDBView tipView{ *pcoinsTip };
        CoinsViewLockedMemPoolNL view{ *this, tipView };
        CCoinsViewCache viewMemPool{ view };

        CValidationState state;
        if (!ContextualCheckTransactionForCurrentBlock(
                config,
                tx,
                tip.nHeight,
                nMedianTimePast,
                state,
                flags) ||
                !CheckSequenceLocks(
                    tip,
                    tx,
                    config,
                    flags,
                    &lp,
                    validLP ? nullptr : &viewMemPool)) {
            // Note if CheckSequenceLocks fails the LockPoints may still be
            // invalid. So it's critical that we remove the tx and not depend on
            // the LockPoints.
            txToRemove.insert(it);
        } else if (it->GetSpendsCoinbase()) {
            for (const CTxIn &txin : tx.vin) {
                indexed_transaction_set::const_iterator it2 =
                    mapTx.find(txin.prevout.GetTxId());
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
        CalculateDescendantsNL(it, setAllRemoves);
    }
    removeStagedNL(setAllRemoves, false, changeSet, MemPoolRemovalReason::REORG);
}

void CTxMemPool::removeConflictsNL(
    const CTransaction &tx,
    const CJournalChangeSetPtr& changeSet) {

    // Remove transactions which depend on inputs of tx, recursively
    for (const CTxIn &txin : tx.vin) {
        auto it = mapNextTx.find(txin.prevout);
        if (it != mapNextTx.end()) {
            const CTransaction &txConflict = *it->second;
            if (txConflict != tx) {
                clearPrioritisationNL(txConflict.GetId());
                removeRecursiveNL(txConflict, changeSet, MemPoolRemovalReason::CONFLICT, &tx);
            }
        }
    }
}

/**
 * Called when a block is connected. Removes from mempool.
 */
void CTxMemPool::RemoveForBlock(
    const std::vector<CTransactionRef> &vtx,
    int32_t nBlockHeight,
    const CJournalChangeSetPtr& changeSet) {

    std::unique_lock lock(smtx);
    std::vector<const CTxMemPoolEntry *> entries;
    setEntries toBeRemoved;
    for (const auto &tx : vtx) {
        uint256 txid = tx->GetId();

        indexed_transaction_set::iterator i = mapTx.find(txid);
        if (i != mapTx.end()) {
            entries.push_back(&*i);
            toBeRemoved.insert(i);
        }
    }
    
    // returns true if tx is not in the journal nor it is added to changeset
    auto isTxOutsideJournal = [this, &changeSet](txiter entry)
    {
        auto txid = entry->GetTx().GetId();
        if (mJournalBuilder.getCurrentJournal()->checkTxnExists(txid))
        {
            return false;
        }
                
        if (changeSet && changeSet->checkTxnAdded(txid))
        {
            return false;
        }
        return true;
    };

    setEntries affectedStillInMempool = getConnectedNL(toBeRemoved, isTxOutsideJournal);

    // Before the txs in the new block have been removed from the mempool,
    for (const auto &tx : vtx) {
        txiter it = mapTx.find(tx->GetId());
        if (it != mapTx.end()) {
            setEntries stage;
            stage.insert(it);
            removeStagedNL(stage, true, changeSet, MemPoolRemovalReason::BLOCK, false);
        }
    }

    CEnsureNonNullChangeSet nonNullChangeSet(*this, changeSet);
    checkJournalAcceptanceNL(affectedStillInMempool, nonNullChangeSet.Get());

    for (const auto &tx : vtx) {
        removeConflictsNL(*tx, changeSet);
        clearPrioritisationNL(tx->GetId());
    }

    lastRollingFeeUpdate = GetTime();
    blockSinceLastRollingFeeBump = true;
}

void CTxMemPool::clearNL() {
    mapLinks.clear();
    mapTx.clear();
    mapNextTx.clear();
    vTxHashes.clear();
    totalTxSize = 0;
    cachedInnerUsage = 0;
    lastRollingFeeUpdate = GetTime();
    blockSinceLastRollingFeeBump = false;
    rollingMinimumFeeRate = 0;
    ++nTransactionsUpdated;
    mJournalBuilder.clearJournal();
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
    CoinsDB* pcoins,
    const mining::CJournalChangeSetPtr& changeSet) const {

    if (nCheckFrequency == 0) {
        return;
    }

    if (GetRand(std::numeric_limits<uint32_t>::max()) >= nCheckFrequency) {
        return;
    }
    CoinsDBView view{ *pcoins };
    CCoinsViewCache mempoolDuplicate{view};

    // Get spend height and MTP
    const auto [ nSpendHeight, medianTimePast] = GetSpendHeightAndMTP(mempoolDuplicate);

    std::shared_lock lock(smtx);

    LogPrint(BCLog::MEMPOOL,
             "Checking mempool with %u transactions and %u inputs\n",
             (unsigned int)mapTx.size(), (unsigned int)mapNextTx.size());

    uint64_t checkTotal = 0;
    uint64_t innerUsage = 0;

    std::list<const CTxMemPoolEntry *> waitingOnDependants;
    for (indexed_transaction_set::const_iterator it = mapTx.begin();
         it != mapTx.end(); it++) {
        unsigned int i = 0;
        checkTotal += it->GetTxSize();
        innerUsage += it->DynamicMemoryUsage();
        const CTransaction &tx = it->GetTx();
        txlinksMap::const_iterator linksiter = mapLinks.find(it);
        assert(linksiter != mapLinks.end());
        const TxLinks &links = linksiter->second;
        innerUsage += memusage::DynamicUsage(links.parents) +
                      memusage::DynamicUsage(links.children);
        bool fDependsWait = false;
        setEntries setParentCheck;
        int64_t parentSizes = 0;
        int64_t parentSigOpCount = 0;
        for (const CTxIn &txin : tx.vin) {
            // Check that every mempool transaction's inputs refer to available
            // coins, or other mempool tx's.
            indexed_transaction_set::const_iterator it2 =
                mapTx.find(txin.prevout.GetTxId());
            if (it2 != mapTx.end()) {
                const CTransaction &tx2 = it2->GetTx();
                assert(tx2.vout.size() > txin.prevout.GetN() &&
                       !tx2.vout[txin.prevout.GetN()].IsNull());
                fDependsWait = true;
                if (setParentCheck.insert(it2).second) {
                    parentSizes += it2->GetTxSize();
                    parentSigOpCount += it2->GetSigOpCount();
                }
            } else {
                assert(view.HaveCoin(txin.prevout));
            }
            // Check whether its inputs are marked in mapNextTx.
            auto it3 = mapNextTx.find(txin.prevout);
            assert(it3 != mapNextTx.end());
            assert(it3->first == &txin.prevout);
            assert(it3->second == &tx);
            i++;
        }
        assert(setParentCheck == GetMemPoolParentsNL(it));
        // Verify ancestor state is correct.
        setEntries setAncestors;
        uint64_t nNoLimit = std::numeric_limits<uint64_t>::max();
        std::string dummy;
        CalculateMemPoolAncestorsNL(*it,
                                    setAncestors,
                                    nNoLimit,
                                    nNoLimit,
                                    nNoLimit,
                                    nNoLimit,
                                    dummy);
        uint64_t nCountCheck = setAncestors.size() + 1;
        uint64_t nSizeCheck = it->GetTxSize();
        Amount nFeesCheck = it->GetModifiedFee();
        int64_t nSigOpCheck = it->GetSigOpCount();

        for (txiter ancestorIt : setAncestors) {
            nSizeCheck += ancestorIt->GetTxSize();
            nFeesCheck += ancestorIt->GetModifiedFee();
            nSigOpCheck += ancestorIt->GetSigOpCount();
        }

        assert(it->GetCountWithAncestors() == nCountCheck);
        assert(it->GetSizeWithAncestors() == nSizeCheck);
        assert(it->GetSigOpCountWithAncestors() == nSigOpCheck);
        assert(it->GetModFeesWithAncestors() == nFeesCheck);

        // Check children against mapNextTx
        CTxMemPool::setEntries setChildrenCheck;
        auto iter = mapNextTx.lower_bound(COutPoint(it->GetTx().GetId(), 0));
        int64_t childSizes = 0;
        for (; iter != mapNextTx.end() &&
               iter->first->GetTxId() == it->GetTx().GetId();
             ++iter) {
            txiter childit = mapTx.find(iter->second->GetId());
            // mapNextTx points to in-mempool transactions
            assert(childit != mapTx.end());
            if (setChildrenCheck.insert(childit).second) {
                childSizes += childit->GetTxSize();
            }
        }
        assert(setChildrenCheck == GetMemPoolChildrenNL(it));
        // Also check to make sure size is greater than sum with immediate
        // children. Just a sanity check, not definitive that this calc is
        // correct...
        assert(it->GetSizeWithDescendants() >= childSizes + it->GetTxSize());

        if (fDependsWait) {
            waitingOnDependants.push_back(&(*it));
        } else {
            CValidationState state;
            bool fCheckResult = tx.IsCoinBase() ||
                                Consensus::CheckTxInputs(
                                    tx, state, mempoolDuplicate, nSpendHeight);
            assert(fCheckResult);
            UpdateCoins(tx, mempoolDuplicate, 1000000);
        }

        // Check we haven't let any non-final txns in
        assert(IsFinalTx(tx, nSpendHeight, medianTimePast));
    }

    unsigned int stepsSinceLastRemove = 0;
    while (!waitingOnDependants.empty()) {
        const CTxMemPoolEntry *entry = waitingOnDependants.front();
        waitingOnDependants.pop_front();
        CValidationState state;
        if (!mempoolDuplicate.HaveInputs(entry->GetTx())) {
            waitingOnDependants.push_back(entry);
            stepsSinceLastRemove++;
            assert(stepsSinceLastRemove < waitingOnDependants.size());
        } else {
            bool fCheckResult =
                entry->GetTx().IsCoinBase() ||
                Consensus::CheckTxInputs(entry->GetTx(), state,
                                         mempoolDuplicate, nSpendHeight);
            assert(fCheckResult);
            UpdateCoins(entry->GetTx(), mempoolDuplicate, 1000000);
            stepsSinceLastRemove = 0;
        }
    }

    for (auto it = mapNextTx.cbegin(); it != mapNextTx.cend(); it++) {
        uint256 txid = it->second->GetId();
        indexed_transaction_set::const_iterator it2 = mapTx.find(txid);
        const CTransaction &tx = it2->GetTx();
        assert(it2 != mapTx.end());
        assert(&tx == it->second);
    }

    assert(totalTxSize == checkTotal);
    assert(innerUsage == cachedInnerUsage);

    /* Journal checking */
    if(changeSet)
    {
        // Make journal consitent with mempool & check
        changeSet->apply();
        std::string journalResult { checkJournalNL() };
        assert(journalResult.empty());
    }
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
    for(indexed_transaction_set::const_iterator it = mapTx.begin(); it != mapTx.end(); ++it)
    {
        // Check this mempool txn also appears in the journal
        const CJournalEntry tx { *it };
        if(!tester.checkTxnExists(tx))
        {
            // it is not in the journal so we will not check it's ancestors
            continue;
        }

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

    LogPrint(BCLog::JOURNAL, "Result of journal check: %s\n", res.str().empty()? "Ok" : res.str().c_str());
    return res.str();
}

// Rebuild the journal contents so they match the mempool
void CTxMemPool::RebuildJournal() const
{
    LogPrint(BCLog::JOURNAL, "Rebuilding journal\n");

    CJournalChangeSetPtr changeSet { mJournalBuilder.getNewChangeSet(JournalUpdateReason::RESET) };

    std::shared_lock lock(smtx);

    setEntries allTxs;
    for(txiter entry = mapTx.cbegin(); entry != mapTx.cend(); entry++)
    {
        allTxs.insert(entry);
    }
    
    checkJournalAcceptanceNL(allTxs, *changeSet);

    // Apply the changes
    changeSet->apply();
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

/**
* Compare 2 transactions to determine their relative priority.
* Does it wothout taking the mutex; it is up to the caller to
* ensure this is thread safe.
*/
bool CTxMemPool::CompareDepthAndScoreNL(const uint256 &hasha,
                                        const uint256 &hashb)
{
    indexed_transaction_set::const_iterator i = mapTx.find(hasha);
    if (i == mapTx.end()) {
        return false;
    }
    indexed_transaction_set::const_iterator j = mapTx.find(hashb);
    if (j == mapTx.end()) {
        return true;
    }
    uint64_t counta = i->GetCountWithAncestors();
    uint64_t countb = j->GetCountWithAncestors();
    if (counta == countb) {
        return CompareTxMemPoolEntryByScore()(*i, *j);
    }
    return counta < countb;
}

namespace {
class DepthAndScoreComparator {
public:
    bool
    operator()(const CTxMemPool::indexed_transaction_set::const_iterator &a,
               const CTxMemPool::indexed_transaction_set::const_iterator &b) {
        uint64_t counta = a->GetCountWithAncestors();
        uint64_t countb = b->GetCountWithAncestors();
        if (counta == countb) {
            return CompareTxMemPoolEntryByScore()(*a, *b);
        }
        return counta < countb;
    }
};
} // namespace

std::vector<CTxMemPool::indexed_transaction_set::const_iterator>
CTxMemPool::getSortedDepthAndScoreNL() const {
    std::vector<indexed_transaction_set::const_iterator> iters;
    iters.reserve(mapTx.size());
    for (indexed_transaction_set::iterator mi = mapTx.begin();
         mi != mapTx.end(); ++mi) {
        iters.push_back(mi);
    }

    std::sort(iters.begin(), iters.end(), DepthAndScoreComparator());
    return iters;
}

void CTxMemPool::QueryHashes(std::vector<uint256> &vtxid) {
    std::shared_lock lock(smtx);
    auto iters = getSortedDepthAndScoreNL();

    vtxid.clear();
    vtxid.reserve(mapTx.size());

    for (auto it : iters) {
        vtxid.push_back(it->GetTx().GetId());
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
    indexed_transaction_set::const_iterator i = mapTx.find(txid);
    if (i == mapTx.end()) {
        return nullptr;
    }
    return i->GetSharedTx();
}

TxMempoolInfo CTxMemPool::Info(const uint256 &txid) const {
    std::shared_lock lock(smtx);
    indexed_transaction_set::const_iterator i = mapTx.find(txid);
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
        // Now update all ancestors' modified fees with descendants
        setEntries setAncestors;
        uint64_t nNoLimit = std::numeric_limits<uint64_t>::max();
        std::string dummy;
        CalculateMemPoolAncestorsNL(
            *it,
            setAncestors,
            nNoLimit,
            nNoLimit,
            nNoLimit,
            nNoLimit,
            dummy,
            false);
        for (txiter ancestorIt : setAncestors) {
            mapTx.modify(ancestorIt,
                         update_descendant_state(0, nFeeDelta, 0));
        }

        // Now update all descendants' modified fees with ancestors
        setEntries setDescendants;
        CalculateDescendantsNL(it, setDescendants);
        setDescendants.erase(it);
        for (txiter descendantIt : setDescendants) {
            mapTx.modify(descendantIt,
                         update_ancestor_state(0, nFeeDelta, 0, 0));
        }

        setEntries affectedTxs = getConnectedNL(setEntries{it});
        affectedTxs.insert(it);
        CJournalChangeSetPtr tmpChangeSet { getJournalBuilder().getNewChangeSet(JournalUpdateReason::UNKNOWN) };
        checkJournalAcceptanceNL(affectedTxs, *tmpChangeSet);
    }
}

void CTxMemPool::clearPrioritisationNL(const uint256& hash) {
    mapDeltas.erase(hash);
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

bool CCoinsViewMemPool::HaveCoin(const COutPoint &outpoint) const {
    return GetCachedTransactionRef(outpoint) || mDBView.HaveCoin(outpoint);
}

size_t CTxMemPool::DynamicMemoryUsage() const {
    std::shared_lock lock(smtx);
    return DynamicMemoryUsageNL();
}

size_t CTxMemPool::DynamicMemoryUsageNL() const {
    // Estimate the overhead of mapTx to be 15 pointers + an allocation, as no
    // exact formula for boost::multi_index_contained is implemented.
    return memusage::MallocUsage(sizeof(CTxMemPoolEntry) +
                                 12 * sizeof(void *)) *
               mapTx.size() +
           memusage::DynamicUsage(mapNextTx) +
           memusage::DynamicUsage(mapDeltas) +
           memusage::DynamicUsage(mapLinks) +
           memusage::DynamicUsage(vTxHashes) + cachedInnerUsage;
}

CTxMemPool::setEntries CTxMemPool::getConnectedNL(const CTxMemPool::setEntries& entries, std::function<bool(txiter)> filter, std::optional<size_t> limit) const
{
    setEntries connected;

    // tracking of the unvisited children with preserving order of insertion 
    // these variables and lambdas deserve their own class but as this is one-release change
    // it is better to have it here, will be easier to remove them later
    setEntries unvisitedChildren;
    std::queue<txiter> unvisitedChildrenOrder;

    auto pushUnvisitedChild =  [&unvisitedChildren, &unvisitedChildrenOrder](txiter entry){
        auto success = unvisitedChildren.insert(entry);
        if(success.second)
        {
            unvisitedChildrenOrder.push(entry);
        }
    };

    auto popUnvisitedChild =  [&unvisitedChildren, &unvisitedChildrenOrder](){
        txiter entry = unvisitedChildrenOrder.front();
        unvisitedChildrenOrder.pop();
        unvisitedChildren.erase(entry);
        return entry;
    };

    auto hasUnvisitedChildren =  [&unvisitedChildren](){
        return !unvisitedChildren.empty();
    };

    // adds children to the list of the unvisited children
    auto addChildrenToTheUnvisited = [this, &connected, &entries, &pushUnvisitedChild](txiter entry){
        for(txiter child: GetMemPoolChildrenNL(entry))
        {
            if(entries.find(child) != entries.end())
            {
                continue;
            }
            
            if(connected.find(child) != connected.end())
            {
                continue;
            }

            pushUnvisitedChild(child);
        }
    };

    // adds all ancestors of the given entry to the set, and their children to the list of the unvisited children
    auto addAncestors = [this, &connected, &entries, &filter, &addChildrenToTheUnvisited](txiter child){
        setEntries nextStep = {child};
        while(!nextStep.empty())
        {
            setEntries newNextStep;
            for(txiter entry: nextStep)
            {
                for(txiter parent: GetMemPoolParentsNL(entry))
                {
                    if(!filter(parent))
                    {
                        continue;
                    }

                    if(entries.find(parent) != entries.end())
                    {
                        continue;
                    }

                    auto success = connected.insert(parent);
                    if(success.second)
                    {
                        newNextStep.insert(parent);
                        addChildrenToTheUnvisited(parent);
                    }                
                }
            }
            nextStep = std::move(newNextStep);
        }
    };


    // first find any direct children that we have in order 
    // to add them first to list of the unvisited children
    for(txiter entry: entries)
    {
        addChildrenToTheUnvisited(entry);
    }

    // ensure that we add all parents
    for(txiter entry: entries)
    {
        addAncestors(entry);
    }


    // continue adding unvisited entries (with their parents) until we have them
    while(hasUnvisitedChildren())
    {
        if(limit.has_value() && (connected.size() >= limit.value()) )
        {
            break;
        }

        txiter entry = popUnvisitedChild();
        
        if(!filter(entry))
        {
            continue;
        }

        if(entries.find(entry) != entries.end())
        {
            continue;
        }

        auto success = connected.insert(entry);
        if(success.second)
        {
            addChildrenToTheUnvisited(entry);
            addAncestors(entry);
        }
    }

    return connected;
}

CTxMemPool::setEntries CTxMemPool::getConnectedNL(const CTxMemPool::setEntries& entries) const
{
    auto allwaysTrue = [](txiter){ return true; };
    return getConnectedNL(entries, allwaysTrue);
}

void CTxMemPool::checkJournalAcceptanceNL(const CTxMemPool::setEntries& affectedTransactions, CJournalChangeSet& changeSet) const
{
    // ensures that transaction will end up in the journal if it is not there already, and that it will not be removed if it is inside.
    auto ensureAdded = [this, &changeSet](txiter entry){
        auto txid = entry->GetTx().GetId();
        bool alreadyInJournal = mJournalBuilder.getCurrentJournal()->checkTxnExists(txid);
        if(!alreadyInJournal)
        {
            if(!changeSet.checkTxnAdded(txid))
            {
                changeSet.addOperation(CJournalChangeSet::Operation::ADD, { *entry });
            }
        }
        else
        {
            if(changeSet.checkTxnRemoved(txid))
            {
                changeSet.addOperation(CJournalChangeSet::Operation::ADD, { *entry });
            }
        }
    };

    // ensures that transaction will be removed from journal if it is inside, and if not inside journal it will not be added
    auto ensureRemoved = [this, &changeSet](txiter entry){
        auto txid = entry->GetTx().GetId();
        bool isInJournal = mJournalBuilder.getCurrentJournal()->checkTxnExists(txid);
        if(isInJournal)
        {
            if(!changeSet.checkTxnRemoved(txid))
            {
                changeSet.addOperation(CJournalChangeSet::Operation::REMOVE, { *entry });
            }
        }
        else
        {
            if(changeSet.checkTxnAdded(txid))
            {
                changeSet.addOperation(CJournalChangeSet::Operation::REMOVE, { *entry });
            }
        }
    };

    // returns topo sorted vector of transactions
    auto topoSortedTxFromSet = [](const setEntries& txSet){
        std::vector<txiter> txs(txSet.begin(), txSet.end());
        std::sort(txs.begin(), txs.end(),
            [](const txiter& entry1, const txiter& entry2)
            {
                return entry1->GetCountWithAncestors() < entry2->GetCountWithAncestors();
            }
        );
        return txs;
    };

    // returns debt of the transaction, regardless of the parents
    auto calculateDebt = [this](txiter entry){
        auto fee = entry->GetModifiedFee();
        auto neededFee = blockMinTxfee.GetFee(entry->GetTxSize());
        if(fee < neededFee)
        {
            return neededFee - fee;
        }
        return Amount(0);
    };


    
    auto txsToCheck = topoSortedTxFromSet(affectedTransactions);
    
    // limit ourself to fixed number of recalculation rounds
    constexpr int MAX_RECHECK_COUNT = 3;

    for(int round = 0; round < MAX_RECHECK_COUNT; round++)
    {
        // flag that signals that we have detected cpfp (child pays for parent) scenario and we have added
        // previously visited transactions (parents) to the journal. this can result in not accepting transaction
        // that could be accepted
        // for example: we have two transactions (tx1, tx2) that share a common low-paying parent. tx1 does not pay
        // enough for the parent but pays enough for itself. tx2 pays enough for itself and the parent. 
        // in this case if we visit tx1 first, we will not add it to the journal. but after tx2 and its parent 
        // has been moved to the journal, tx1  is now free of it parent's debt and can be moved to the journal. 
        // so if we accepted transactions through cpfp we will revisit all transactions that did not make it to the journal
        bool cpfpExecuted = false;

        // keys are entries that do not pay enough, values are sets of the their's ancestors which do not pay enough
        std::map<txiter, setEntries, CompareIteratorByHash> nonPayingTxWithAncestors;

        for(txiter ent: txsToCheck)
        {
            // let's collect all non-paying ancestors
            setEntries nonpayingAncestors;
            for(txiter parent: GetMemPoolParentsNL(ent))
            {
                // is parent in debt?
                auto parentDebtIt = nonPayingTxWithAncestors.find(parent);
                if (parentDebtIt == nonPayingTxWithAncestors.end())
                {
                    continue;
                }

                // our parent is in debt, let's collect theirs non paying ancestors too
                nonpayingAncestors.insert(parentDebtIt->second.cbegin(), parentDebtIt->second.cend());
                nonpayingAncestors.insert(parent);
            }

            // now sum up all ancestor's debt (if any)
            Amount ancestorsDebt(0);
            for(txiter ancestor: nonpayingAncestors)
            {
                ancestorsDebt += calculateDebt(ancestor);
            }

            Amount fee = ent->GetModifiedFee();
            Amount excessFee = fee - blockMinTxfee.GetFee(ent->GetTxSize()); 

            if(excessFee >= ancestorsDebt)
            {
                // Great! This transactions pays enough, first remove ancestors from the debt list (if any) 
                // and add them to the journal (in topo-order)
                for(auto ancestor: topoSortedTxFromSet(nonpayingAncestors))
                {
                    ensureAdded(ancestor);
                    nonPayingTxWithAncestors.erase(ancestor);
                    cpfpExecuted = true;
                }
                
                ensureAdded(ent);
            }
            else
            {
                // Lets put the tx on the debt list
                nonPayingTxWithAncestors.insert(std::make_pair(ent, nonpayingAncestors));
            }
        }

        bool lastRound = (round + 1) >= MAX_RECHECK_COUNT;

        if(cpfpExecuted && !lastRound)
        {
            // we have executed cpfp (child pays for parent) so we should revisit all transaction
            // that did not make it to the journal, see explanation in comment at the beginning of the outer loop
            // optimization suggestion: we could limit re-check on the transaction that are connected with transactions in the cpfp groups
            setEntries nonPayingTxs;
            for(const auto& nonPaying: nonPayingTxWithAncestors)
            {
                nonPayingTxs.insert(nonPaying.first);
            }
            txsToCheck = topoSortedTxFromSet(nonPayingTxs);
        }
        else
        {
            // cpfp is not executed in this round or this is the last round, 
            // transactions that do not pay enough
            // have no more chance to enter the journal (and be mined)
            // so lets ensure that they are out of the journal
            for(const auto& nonPaying: nonPayingTxWithAncestors)
            {
                ensureRemoved(nonPaying.first);
            }
            break;
        }
    }
}

void CTxMemPool::removeStagedNL(
    setEntries &stage,
    bool updateDescendants,
    const CJournalChangeSetPtr& changeSet,
    MemPoolRemovalReason reason,
    bool updateJournal,
    const CTransaction* conflictedWith) {

    updateForRemoveFromMempoolNL(stage, updateDescendants);

    if(updateJournal)
    {
        // let's find all transactions which acceptance to the journal could be affected with removing these transactions
        setEntries affectedStillInMempool = getConnectedNL(stage);

        for (const txiter &it : stage) {
            removeUncheckedNL(it, changeSet, reason, conflictedWith);
        }
    
        CEnsureNonNullChangeSet nonNullChangeSet(*this, changeSet);
        checkJournalAcceptanceNL(affectedStillInMempool, nonNullChangeSet.Get());
    }
    else
    {
        for (const txiter &it : stage) {
            removeUncheckedNL(it, changeSet, reason, conflictedWith);
        }
    }
}

int CTxMemPool::Expire(int64_t time, const mining::CJournalChangeSetPtr& changeSet)
{
    std::unique_lock lock(smtx);
    indexed_transaction_set::index<entry_time>::type::iterator it =
        mapTx.get<entry_time>().begin();
    setEntries toremove;
    while (it != mapTx.get<entry_time>().end() && it->GetTime() < time) {
        toremove.insert(mapTx.project<0>(it));
        it++;
    }

    setEntries stage;
    for (txiter removeit : toremove) {
        CalculateDescendantsNL(removeit, stage);
    }

    removeStagedNL(stage, false, changeSet, MemPoolRemovalReason::EXPIRY);
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

void CTxMemPool::AddUnchecked(
    const uint256 &hash,
    const CTxMemPoolEntry &entry,
    const CJournalChangeSetPtr& changeSet,
    size_t* pnMempoolSize,
    size_t* pnDynamicMemoryUsage) {

    {
        std::unique_lock lock(smtx);
        setEntries setAncestors;
        uint64_t nNoLimit = std::numeric_limits<uint64_t>::max();
        std::string dummy;
        CalculateMemPoolAncestorsNL(
            entry,
            setAncestors,
            nNoLimit,
            nNoLimit,
            nNoLimit,
            nNoLimit,
            dummy);

        AddUncheckedNL(
             hash,
             entry,
             setAncestors,
             changeSet,
             pnMempoolSize,
             pnDynamicMemoryUsage);
    }
    // Notify entry added without holding the mempool's lock
    NotifyEntryAdded(entry.GetSharedTx());
}

void CTxMemPool::updateChildNL(txiter entry, txiter child, bool add) {
    setEntries s;
    if (add && mapLinks[entry].children.insert(child).second) {
        cachedInnerUsage += memusage::IncrementalDynamicUsage(s);
    } else if (!add && mapLinks[entry].children.erase(child)) {
        cachedInnerUsage -= memusage::IncrementalDynamicUsage(s);
    }
}

void CTxMemPool::updateParentNL(txiter entry, txiter parent, bool add) {
    setEntries s;
    if (add && mapLinks[entry].parents.insert(parent).second) {
        cachedInnerUsage += memusage::IncrementalDynamicUsage(s);
    } else if (!add && mapLinks[entry].parents.erase(parent)) {
        cachedInnerUsage -= memusage::IncrementalDynamicUsage(s);
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

std::vector<TxId> CTxMemPool::TrimToSize(
    size_t sizelimit,
    const mining::CJournalChangeSetPtr& changeSet,
    std::vector<COutPoint>* pvNoSpendsRemaining) {

    std::unique_lock lock(smtx);

    unsigned nTxnRemoved = 0;
    CFeeRate maxFeeRateRemoved(Amount(0));
    std::vector<TxId> vRemovedTxIds {};
    while (!mapTx.empty() && DynamicMemoryUsageNL() > sizelimit) {
        indexed_transaction_set::index<descendant_score>::type::iterator it =
            mapTx.get<descendant_score>().begin();

        // We set the new mempool min fee to the feerate of the removed set,
        // plus the "minimum reasonable fee rate" (ie some value under which we
        // consider txn to have 0 fee). This way, we don't allow txn to enter
        // mempool with feerate equal to txn which were removed with no block in
        // between.
        CFeeRate removed(it->GetModFeesWithDescendants(),
                         it->GetSizeWithDescendants());
        removed += MEMPOOL_FULL_FEE_INCREMENT;

        trackPackageRemovedNL(removed);
        maxFeeRateRemoved = std::max(maxFeeRateRemoved, removed);

        setEntries stage;
        CalculateDescendantsNL(mapTx.project<0>(it), stage);
        nTxnRemoved += stage.size();

        std::vector<CTransaction> txn;
        if (pvNoSpendsRemaining) {
            txn.reserve(stage.size());
            for (txiter iter : stage) {
                txn.push_back(iter->GetTx());
                vRemovedTxIds.emplace_back(iter->GetTx().GetId());
            }
        }
        removeStagedNL(stage, false, changeSet, MemPoolRemovalReason::SIZELIMIT);
        if (pvNoSpendsRemaining) {
            for (const CTransaction &tx : txn) {
                for (const CTxIn &txin : tx.vin) {
                    if (ExistsNL(txin.prevout.GetTxId())) {
                        continue;
                    }
                    if (!mapNextTx.count(txin.prevout)) {
                        pvNoSpendsRemaining->push_back(txin.prevout);
                    }
                }
            }
        }
    }

    if (maxFeeRateRemoved > CFeeRate(Amount(0))) {
        LogPrint(BCLog::MEMPOOL,
                 "Removed %u txn, rolling minimum fee bumped to %s\n",
                 nTxnRemoved, maxFeeRateRemoved.ToString());
    }
    return vRemovedTxIds;
}

bool CTxMemPool::TransactionWithinChainLimit(const uint256 &txid,
                                             size_t chainLimit) const {
    std::shared_lock lock(smtx);
    auto it = mapTx.find(txid);
    return it == mapTx.end() || (it->GetCountWithAncestors() < chainLimit &&
                                 it->GetCountWithDescendants() < chainLimit);
}

unsigned long CTxMemPool::Size() {
    std::shared_lock lock(smtx);
    return mapTx.size();
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
    return it != mapTx.end() && outpoint.GetN() < it->GetTx().vout.size();
}

SaltedTxidHasher::SaltedTxidHasher()
    : k0(GetRand(std::numeric_limits<uint64_t>::max())),
      k1(GetRand(std::numeric_limits<uint64_t>::max())) {}
