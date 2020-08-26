// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2019-2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "txmempool.h"
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
 * class CTransactionRefWrapper
 */
CTransactionRefWrapper::CTransactionRefWrapper() {
}

CTransactionRefWrapper::CTransactionRefWrapper(const CTransactionRef &_tx, const std::shared_ptr<CMempoolTxDB>& txDB)
    : tx{_tx}
    , txid{_tx->GetId()}
    , mempoolTxDB{txDB}
{
}

CTransactionRef CTransactionRefWrapper::GetTxFromDB() const {
    CTransactionRef tmp;
    if (mempoolTxDB != nullptr) {
        mempoolTxDB->GetTransaction(txid, tmp);
        std::atomic_store(&tx, tmp);
    }
    return tmp;
}


const TxId& CTransactionRefWrapper::GetId() const {
    return txid;
}

CTransactionRef CTransactionRefWrapper::GetTx() const {
    CTransactionRef tmp = std::atomic_load(&tx);
    if (tmp != nullptr) {
        return tmp;
    }
    return GetTxFromDB();
}

void CTransactionRefWrapper::MoveTxToDisk() const {
    CTransactionRef tmp = std::atomic_load(&tx);
    if (tmp) 
    {
        if (mempoolTxDB)
        {
            if (mempoolTxDB->AddTransaction(txid, tmp)) {
                tmp = nullptr;
                std::atomic_store(&tx, tmp);
            }
        }
        else
        {
            LogPrint(BCLog::MEMPOOL, "Transaction %s has no DB configured\n", txid.ToString());
        }
    }
    else
    {
        LogPrint(BCLog::MEMPOOL, "Transaction %s is already on disk\n", txid.ToString());
    }
}

void CTransactionRefWrapper::UpdateMoveTxToDisk() const {
    std::atomic_store(&tx, CTransactionRef(nullptr));
}

bool CTransactionRefWrapper::IsInMemory() const
{
    return std::atomic_load(&tx) != nullptr;
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
                                 LockPoints lp, CTxMemPoolBase &mempoolIn)
    : tx{_tx, mempoolIn.GetMempoolTxDB()},
      nFee{_nFee}, nTime{_nTime}, entryPriority{_entryPriority},
      inChainInputValue{_inChainInputValue},
      lockPoints{lp}, entryHeight{_entryHeight}, spendsCoinbase{_spendsCoinbase}
{
    nTxSize = _tx->GetTotalSize();
    nModSize = _tx->CalculateModifiedSize(GetTxSize());
    nUsageSize = RecursiveDynamicUsage(_tx);

    Amount nValueIn = _tx->GetValueOut() + nFee;
    assert(inChainInputValue <= nValueIn);

    feeDelta = Amount {0};
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

void CTxMemPoolEntry::MoveTxToDisk() const {
    tx.MoveTxToDisk();
}

void CTxMemPoolEntry::UpdateMoveTxToDisk() const {
    tx.UpdateMoveTxToDisk();
}

bool CTxMemPoolEntry::IsInMemory() const {
    return tx.IsInMemory();
}

bool CTxMemPool::CheckAncestorLimits(
    const CTxMemPoolEntry& entry,
    uint64_t limitAncestorCount,
    uint64_t limitAncestorSize,
    uint64_t limitDescendantCount,
    uint64_t limitDescendantSize,
    std::optional<std::reference_wrapper<std::string>> errString) const
{
    std::shared_lock lock(smtx);
    return CalculateMemPoolAncestorsNL(entry,
                                       std::nullopt,
                                       limitAncestorCount,
                                       limitAncestorSize,
                                       limitDescendantCount,
                                       limitDescendantSize,
                                       errString);
}

bool CTxMemPool::CalculateMemPoolAncestorsNL(
    const CTxMemPoolEntry& entry,
    std::optional<std::reference_wrapper<setEntries>> setAncestors,
    uint64_t limitAncestorCount,
    uint64_t limitAncestorSize,
    uint64_t limitDescendantCount,
    uint64_t limitDescendantSize,
    std::optional<std::reference_wrapper<std::string>> errString) const
{
    // Get parents of this transaction that are in the mempool
    // GetMemPoolParentsNL() is only valid for entries in the mempool, so we
    // iterate mapTx to find parents.
    setEntries parentHashes;
    const auto tx = entry.GetSharedTx();
    for (const auto& in : tx->vin) {
        const auto piter = mapTx.find(in.prevout.GetTxId());
        if (piter == mapTx.end()) {
            continue;
        }
        parentHashes.emplace(piter);
        if (parentHashes.size() + 1 > limitAncestorCount) {
            if (errString) {
                errString->get() = strprintf("too many unconfirmed parents [limit: %u]",
                                             limitAncestorCount);
            }
            return false;
        }
    }

    return GetMemPoolAncestorsNL(setAncestors,
                                 parentHashes,
                                 entry.GetTxSize(),
                                 limitAncestorCount,
                                 limitAncestorSize,
                                 limitDescendantCount,
                                 limitDescendantSize,
                                 errString);
}

bool CTxMemPool::GetMemPoolAncestorsNL(
        const txiter& entryIter,
        std::optional<std::reference_wrapper<setEntries>> setAncestors,
        uint64_t limitAncestorCount,
        uint64_t limitAncestorSize,
        uint64_t limitDescendantCount,
        uint64_t limitDescendantSize,
        std::optional<std::reference_wrapper<std::string>> errString) const
{
    // If we're not searching for parents, we require this to be an entry in
    // the mempool already.
    auto parentHashes = GetMemPoolParentsNL(entryIter);
    return GetMemPoolAncestorsNL(setAncestors,
                                 parentHashes,
                                 entryIter->GetTxSize(),
                                 limitAncestorCount,
                                 limitAncestorSize,
                                 limitDescendantCount,
                                 limitDescendantSize,
                                 errString);
}

bool CTxMemPool::GetMemPoolAncestorsNL(
    std::optional<std::reference_wrapper<setEntries>> setAncestors,
    setEntries& parentHashes,
    size_t totalSizeWithAncestors,
    uint64_t limitAncestorCount,
    uint64_t limitAncestorSize,
    uint64_t limitDescendantCount,
    uint64_t limitDescendantSize,
    std::optional<std::reference_wrapper<std::string>> errString) const
{
    setEntries localAncestors;
    setEntries& allAncestors = (setAncestors ? setAncestors->get() : localAncestors);

    while (!parentHashes.empty()) {
        txiter stageit = *parentHashes.begin();

        allAncestors.insert(stageit);
        parentHashes.erase(stageit);
        totalSizeWithAncestors += stageit->GetTxSize();

        if (totalSizeWithAncestors > limitAncestorSize) {
            if (errString) {
                errString->get() = strprintf("exceeds ancestor size limit [limit: %u]",
                                             limitAncestorSize);
            }
            return false;
        }

        const setEntries &setMemPoolParents = GetMemPoolParentsNL(stageit);
        for (const txiter &phash : setMemPoolParents) {
            // If this is a new ancestor, add it.
            if (allAncestors.count(phash) == 0) {
                parentHashes.insert(phash);
            }
            if (parentHashes.size() + allAncestors.size() + 1 > limitAncestorCount) {
                if (errString) {
                    errString->get() = strprintf("too many unconfirmed ancestors [limit: %u]",
                                                 limitAncestorCount);
                }
                return false;
            }
        }
    }

    return true;
}

void CTxMemPool::updateAncestorsOfNL(bool add, txiter it) {
    setEntries parentIters = GetMemPoolParentsNL(it); // MARK: also used by legacy
    // add or remove this tx as a child of each parent
    for (txiter piter : parentIters) {
        updateChildNL(piter, it, add);
    }
}

void CTxMemPool::updateChildrenForRemovalNL(txiter it) {
    const setEntries &setMemPoolChildren = GetMemPoolChildrenNL(it); // MARK: also used by legacy
    for (txiter updateIt : setMemPoolChildren) {
         updateParentNL(updateIt, it, false);
    }
}

void CTxMemPool::updateForRemoveFromMempoolNL(const setEntries &entriesToRemove,
                                            bool updateDescendants) {
    // For each entry, walk back all ancestors and decrement size associated
    // with this transaction.
    if (updateDescendants) {
        // updateDescendants should be true whenever we're not recursively
        // removing a tx and all its descendants, eg when a transaction is
        // confirmed in a block. Here we only update statistics and not data in
        // mapLinks (which we need to preserve until we're finished with all
        // operations that need to traverse the mempool).
        for (txiter removeIt : entriesToRemove) {
            setEntries setDescendants;
            GetDescendantsNL(removeIt, setDescendants);
            setDescendants.erase(removeIt); // don't update state for self
            int64_t modifySize = -((int64_t)removeIt->GetTxSize());
            Amount modifyFee = -1 * removeIt->GetModifiedFee();
        }
    }

    for (txiter removeIt : entriesToRemove) {
        // Note that updateAncestorsOfNL severs the child links that point to
        // removeIt in the entries for the parents of removeIt.
        updateAncestorsOfNL(false, removeIt);
    }
    // After updating all the ancestor sizes, we can now sever the link between
    // each transaction being removed and any mempool children (ie, update
    // setMemPoolParents for each direct child of a transaction being removed).
    for (txiter removeIt : entriesToRemove) {
        updateChildrenForRemovalNL(removeIt);
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

void CTxMemPool::AddUncheckedNL(
    const uint256 &hash,
    const CTxMemPoolEntry &entry,
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

    const auto tx = newit->GetSharedTx();
    std::set<uint256> setParentTransactions;
    for (const CTxIn &in : tx->vin) {
        mapNextTx.insert(std::make_pair(in.prevout, &newit->tx));
        setParentTransactions.insert(in.prevout.GetTxId());
    }
    // Don't bother worrying about child transactions of this one. Normal case
    // of a new transaction arriving is that there can't be any children,
    // because such children would be orphans.

    // Update ancestors with information about this tx
    for (const uint256 &phash : setParentTransactions) {
        txiter pit = mapTx.find(phash);
        if (pit != mapTx.end()) {
            updateParentNL(newit, pit, true);
        }
    }
    updateAncestorsOfNL(true, newit);

    // Calculate CPFP statistics.
    SecondaryMempoolEntryData groupingData{
        newit->GetFee(), newit->GetFeeDelta(), newit->GetTxSize(), 0};
    for (const auto& input : tx->vin) {
        auto parent = mapTx.find(input.prevout.GetTxId());
        if (parent != mapTx.end() && !parent->IsInPrimaryMempool()) {
            groupingData.fee += parent->groupingData->fee;
            groupingData.feeDelta += parent->groupingData->feeDelta;
            groupingData.size += parent->groupingData->size;
            groupingData.ancestorsCount += parent->groupingData->ancestorsCount + 1;
        }
    }

    if (groupingData.fee + groupingData.feeDelta
        >= GetPrimaryMempoolMinFeeNL().GetFee(groupingData.size)) {
        // This transaction will go directly into the primary mempool.
        if (groupingData.ancestorsCount > 0) {
            // TODO: Construct the CPFP group and move it from the secondary to
            // the priomary mempool. Currently this should never happen given
            // how GetPrimaryMempoolMinFeeNL() is implemented.
            assert(!"Construct CPFP group");
        }
    }
    else {
        // This transaction is not paying enough, it goes into the secondary mempool.
        // NOTE: We use modify() here because it returns a mutable reference to
        //       the entry in the index, whereas dereferencing the iterator
        //       returns an immutable reference, which would require a
        //       const_cast<> and also would not update the index. Not that we
        //       expect any of the index keys to change here.
        mapTx.modify(newit, [&groupingData](CTxMemPoolEntry& entry) {
                                entry.groupingData.emplace(groupingData);
                            });
        ++secondaryMempoolSize;
    }

    nTransactionsUpdated++;
    totalTxSize += entry.GetTxSize();

    // If it is required calculate mempool size & dynamic memory usage.
    if (pnMempoolSize) {
        *pnMempoolSize = PrimaryMempoolSizeNL();
    }
    if (pnDynamicMemoryUsage) {
        *pnDynamicMemoryUsage = DynamicMemoryUsageNL();
    }

    // Apply to the current journal, either via the passed in change set or directly ourselves
    if(changeSet)
    {
        changeSet->addOperation(CJournalChangeSet::Operation::ADD, { entry });
    }
    else
    {
        CJournalChangeSetPtr tmpChangeSet { getJournalBuilder().getNewChangeSet(JournalUpdateReason::UNKNOWN) };
        tmpChangeSet->addOperation(CJournalChangeSet::Operation::ADD, { entry });
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

    // Apply to the current journal, either via the passed in change set or directly ourselves
    if(changeSet)
    {
        changeSet->addOperation(CJournalChangeSet::Operation::REMOVE, { *it });
    }
    else
    {
        CJournalChangeSetPtr tmpChangeSet { getJournalBuilder().getNewChangeSet(JournalUpdateReason::UNKNOWN) };
        tmpChangeSet->addOperation(CJournalChangeSet::Operation::REMOVE, { *it });
    }

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

        const setEntries &setChildren = GetMemPoolChildrenNL(it); // MARK: also used by legacy
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

    removeStagedNL(setAllRemoves, false, changeSet, reason, conflictedWith);
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
    removeStagedNL(setAllRemoves, false, changeSet, MemPoolRemovalReason::REORG);
}

void CTxMemPool::removeConflictsNL(
    const CTransaction &tx,
    const CJournalChangeSetPtr& changeSet) {

    // Remove transactions which depend on inputs of tx, recursively
    for (const CTxIn &txin : tx.vin) {
        auto it = mapNextTx.find(txin.prevout);
        if (it != mapNextTx.end()) {
            const auto& conflictTxId = it->second->GetId();
            if (conflictTxId != tx.GetId()) {
                clearPrioritisationNL(conflictTxId);
                removeRecursiveNL(conflictTxId, changeSet, MemPoolRemovalReason::CONFLICT, &tx);
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
    for (const auto &tx : vtx) {
        uint256 txid = tx->GetId();

        indexed_transaction_set::iterator i = mapTx.find(txid);
        if (i != mapTx.end()) {
            entries.push_back(&*i);
        }
    }

    // Before the txs in the new block have been removed from the mempool,
    for (const auto &tx : vtx) {
        txiter it = mapTx.find(tx->GetId());
        if (it != mapTx.end()) {
            setEntries stage;
            stage.insert(it);
            removeStagedNL(stage, true, changeSet, MemPoolRemovalReason::BLOCK);
        }
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
    totalTxSize = 0;
    secondaryMempoolSize = 0;
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
        const TxLinks &links = linksiter->second;
        innerUsage += memusage::DynamicUsage(links.parents) +
                      memusage::DynamicUsage(links.children);
        bool fDependsWait = false;
        setEntries setParentCheck;
        int64_t parentSizes = 0;
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
                    parentSizes += it2->GetTxSize();
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
        assert(setParentCheck == GetMemPoolParentsNL(it)); // MARK: also used by legacy
        // Verify ancestor state is correct.
        //
        // Because we're doing sanity checking, we do *not* assume that the
        // mapLinks are correct, so we call CalculateMemPoolAncestorsNL()
        // instead of GetMemPoolAncestorsNL() (which we could, given that we
        // already have a valid iterator to an in-mempool entry).
        setEntries setAncestors;
        uint64_t nNoLimit = std::numeric_limits<uint64_t>::max();
        CalculateMemPoolAncestorsNL(*it,
                                    std::ref(setAncestors),
                                    nNoLimit,
                                    nNoLimit,
                                    nNoLimit,
                                    nNoLimit,
                                    std::nullopt);

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
        assert(setChildrenCheck == GetMemPoolChildrenNL(it)); // MARK: also used by legacy

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
void CTxMemPool::RebuildJournal() const
{
    LogPrint(BCLog::JOURNAL, "Rebuilding journal\n");

    CJournalChangeSetPtr changeSet { mJournalBuilder.getNewChangeSet(JournalUpdateReason::RESET) };

    {
        std::shared_lock lock(smtx);
        CoinsDBView coinsView{ *pcoinsTip };

        for (const auto& entry: mapTx.get<insertion_order>())
        {
            changeSet->addOperation(CJournalChangeSet::Operation::ADD, { entry });
        }

        CheckMempoolNL(coinsView, changeSet);
    }

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
                       mempoolTxDB = std::make_shared<CMempoolTxDB>(cacheSize);
                   });
}

std::shared_ptr<CMempoolTxDB> CTxMemPool::GetMempoolTxDB() {
    InitMempoolTxDB();
    return mempoolTxDB;
};

uint64_t CTxMemPool::GetDiskUsage() {
    InitMempoolTxDB();
    return mempoolTxDB->GetDiskUsage();
};

void CTxMemPool::SaveTxsToDisk(uint64_t requiredSize)
{
    /* Decide which transactions we want to store first */
    auto mi = mapTx.get<entry_time>().begin();
    uint64_t movedToDiskSize = 0;
    InitMempoolTxDB();
    while (movedToDiskSize < requiredSize && !mapTx.empty() && mi != mapTx.get<entry_time>().end())
    {
        if (mi->IsInMemory())
        {
            mi->MoveTxToDisk();
            movedToDiskSize += mi->GetTxSize();
            mi++;
        }

    }
}

void CTxMemPool::UpdateMoveTxsToDisk(std::vector<const CTxMemPoolEntry*> toBeUpdated) {
    for (const CTxMemPoolEntry* entry : toBeUpdated)
    {
        entry->UpdateMoveTxToDisk();
    }
}

void CTxMemPool::SaveTxsToDiskBatch(uint64_t requiredSize) {
    /* Decide which transactions we want to store first */
    auto mi = mapTx.get<entry_time>().begin();
    uint64_t movedToDiskSize = 0;
    std::vector<CTransactionRef> toBeMoved;
    std::vector<const CTxMemPoolEntry*> toBeUpdated;
    InitMempoolTxDB();
    while (movedToDiskSize < requiredSize && !mapTx.empty() &&
           mi != mapTx.get<entry_time>().end()) {
        if (mi->IsInMemory()) {
            toBeMoved.push_back(mi->GetSharedTx());
            toBeUpdated.push_back(&*mi);
            movedToDiskSize += mi->GetTxSize();
            mi++;
        }
    }
    if (mempoolTxDB->AddTransactions(toBeMoved))
    {
        UpdateMoveTxsToDisk(toBeUpdated);
    }
    else
    {
        LogPrint(BCLog::MEMPOOL, "WriteBatch failed. Transactions were not moved to DB successfully.");
    }
    
    if (movedToDiskSize < requiredSize)
    {
        LogPrint(BCLog::MEMPOOL, "Less than required amount of memory was freed. Required: %d,  freed: %d\n", requiredSize, movedToDiskSize);
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
    ApplyDeltasNL(hash, dPriorityDelta, nFeeDelta); // MARK: also used by legacy
}

void CTxMemPool::ApplyDeltasNL(  // MARK: used by legacy
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

        // Now update all descendants' modified fees with ancestors
        setEntries setDescendants;
        GetDescendantsNL(it, setDescendants);
        setDescendants.erase(it);
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
    // Estimate the overhead of mapTx to be 15 pointers + an allocation, as no
    // exact formula for boost::multi_index_contained is implemented.
    return memusage::MallocUsage(sizeof(CTxMemPoolEntry) +
                                 12 * sizeof(void *)) *
               mapTx.size() +
           memusage::DynamicUsage(mapNextTx) +
           memusage::DynamicUsage(mapDeltas) +
           memusage::DynamicUsage(mapLinks) + cachedInnerUsage;
}

void CTxMemPool::removeStagedNL(
    setEntries &stage,
    bool updateDescendants,
    const CJournalChangeSetPtr& changeSet,
    MemPoolRemovalReason reason,
    const CTransaction* conflictedWith) {

    updateForRemoveFromMempoolNL(stage, updateDescendants);
    for (const txiter &it : stage) {
        removeUncheckedNL(it, changeSet, reason, conflictedWith);
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
        GetDescendantsNL(removeit, stage);
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
                    GetTime(),        // nAcceptTime
                    false));          // fLimitFree
        }
        ++it;
    }

    disconnectpool.queuedTx.clear();

    // we will reset the journal soon, we should clear the changeSet also
    if(changeSet)
    {
        changeSet->clear();
    }

    // rebuild mempool
    indexed_transaction_set tempMapTx;
    {
        std::unique_lock lock {smtx};

        // save old mempool contents
        tempMapTx = std::move(mapTx);

        clearNL();
    }

    // Validate the set of transactions from the disconnectpool and add them to the mempool
    g_connman->getTxnValidator()->processValidation(vTxInputData, changeSet, true);

    // Add original mempool contents on top to preserve toposort
    {
        std::unique_lock lock {smtx};
        auto& tempMapTxSequenced = tempMapTx.get<insertion_order>();
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

// FIXME: Currently this implementation is just a non-locking copy of GetMinFee().
// TODO: CORE-130
CFeeRate CTxMemPool::GetPrimaryMempoolMinFeeNL() const {
    if (!blockSinceLastRollingFeeBump || rollingMinimumFeeRate == 0) {
        return CFeeRate(Amount(int64_t(rollingMinimumFeeRate)));
    }

    int64_t time = GetTime();
    if (time > lastRollingFeeUpdate + 10) {
        // FIXME: Size limit is calculated as per estimateFee().
        const auto sizelimit =
            gArgs.GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * ONE_MEGABYTE;
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

    // FIXME: Disabled to remove references on mempool descendant score.
    // TODO: CORE-130

    // std::unique_lock lock(smtx);

    // unsigned nTxnRemoved = 0;
    // CFeeRate maxFeeRateRemoved(Amount(0));
    // std::vector<TxId> vRemovedTxIds {};
    // while (!mapTx.empty() && DynamicMemoryUsageNL() > sizelimit) {
    //     indexed_transaction_set::index<descendant_score>::type::iterator it =
    //         mapTx.get<descendant_score>().begin();

    //     // We set the new mempool min fee to the feerate of the removed set,
    //     // plus the "minimum reasonable fee rate" (ie some value under which we
    //     // consider txn to have 0 fee). This way, we don't allow txn to enter
    //     // mempool with feerate equal to txn which were removed with no block in
    //     // between.
    //     CFeeRate removed(it->GetModFeesWithDescendants(),
    //                      it->GetSizeWithDescendants());
    //     removed += MEMPOOL_FULL_FEE_INCREMENT;

    //     trackPackageRemovedNL(removed);
    //     maxFeeRateRemoved = std::max(maxFeeRateRemoved, removed);

    //     setEntries stage;
    //     GetDescendantsNL(mapTx.project<0>(it), stage);
    //     nTxnRemoved += stage.size();

    //     std::vector<CTransaction> txn;
    //     if (pvNoSpendsRemaining) {
    //         txn.reserve(stage.size());
    //         for (txiter iter : stage) {
    //             txn.push_back(iter->GetTx());
    //             vRemovedTxIds.emplace_back(iter->GetTx().GetId());
    //         }
    //     }
    //     removeStagedNL(stage, false, changeSet, MemPoolRemovalReason::SIZELIMIT);
    //     if (pvNoSpendsRemaining) {
    //         for (const CTransaction &tx : txn) {
    //             for (const CTxIn &txin : tx.vin) {
    //                 if (ExistsNL(txin.prevout.GetTxId())) {
    //                     continue;
    //                 }
    //                 if (!mapNextTx.count(txin.prevout)) {
    //                     pvNoSpendsRemaining->push_back(txin.prevout);
    //                 }
    //             }
    //         }
    //     }
    // }

    // if (maxFeeRateRemoved > CFeeRate(Amount(0))) {
    //     LogPrint(BCLog::MEMPOOL,
    //              "Removed %u txn, rolling minimum fee bumped to %s\n",
    //              nTxnRemoved, maxFeeRateRemoved.ToString());
    // }
    // return vRemovedTxIds;

    return std::vector<TxId>();
}

bool CTxMemPool::TransactionWithinChainLimit(const uint256 &txid,
                                             size_t chainLimit) const {
    std::shared_lock lock(smtx);
    auto it = mapTx.find(txid);
    // TODO: check lengtht of cahin in the secondary mempool
    return true;
}

unsigned long CTxMemPool::Size() {
    std::shared_lock lock(smtx);
    return PrimaryMempoolSizeNL();
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

SaltedTxidHasher::SaltedTxidHasher()
    : k0(GetRand(std::numeric_limits<uint64_t>::max())),
      k1(GetRand(std::numeric_limits<uint64_t>::max())) {}


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
            static constexpr auto noLimit = std::numeric_limits<uint64_t>::max();
            GetMemPoolAncestorsNL(baseTx, related,
                                  noLimit, noLimit, noLimit, noLimit,
                                  std::nullopt);
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



