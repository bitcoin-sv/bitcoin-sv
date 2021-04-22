// Copyright (c) 2019 The Bitcoin SV developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "orphan_txns.h"
#include "policy/policy.h"
#include "config.h"

COrphanTxns::COrphanTxns(
    size_t maxCollectedOutpoints,
    size_t maxExtraTxnsForCompactBlock,
    size_t maxTxSizePolicy)
: mMaxCollectedOutpoints(maxCollectedOutpoints),
  mMaxExtraTxnsForCompactBlock(maxExtraTxnsForCompactBlock),
  mMaxStandardTxSize(maxTxSizePolicy)
{}

void COrphanTxns::addTxn(const TxInputDataSPtr& pTxInputData) {
    if (!pTxInputData || pTxInputData->IsDeleted()) {
        return;
    }
    // Mark txn as orphan
    pTxInputData->SetOrphanTxn();
    const CTransactionRef& ptx = pTxInputData->GetTxnPtr();
    const CTransaction &tx = *ptx;
    const uint256 &txid = tx.GetId();
    // Emplace a new txn in the buffer
    size_t orphanTxnsTotal {0};
    size_t orphanTxnsByPrevTotal {0};
    {
        std::unique_lock lock {mOrphanTxnsMtx};
        // Check if already present
        if (checkTxnExistsNL(txid)) {
            return;
        }

        // Ignore if transaction is bigger than MAX_STANDARD_TX_SIZE. 
        // Since we support big transactions we do not limit number of orphan transactions
        // but combined size of those transactions. limitTxnsSize is called after adding.
        unsigned int sz = tx.GetTotalSize();
        if (TxSource::p2p == pTxInputData->GetTxSource()) {
            if (mMaxStandardTxSize &&
                sz > mMaxStandardTxSize /* mMaxStandardTxSize is always set to after genesis value. If non default value is used for policy tx size then orphan tx before genesis might not get accepted by mempool */) {
                LogPrint(BCLog::MEMPOOL,
                         "ignoring large orphan tx (size: %u, hash: %s)\n", sz,
                         txid.ToString());
                return;
            }
            addToCompactExtraTxns(ptx);
        }
        mUntrimmedSize += sz;
        auto ret = mOrphanTxns.emplace(
            txid, COrphanTxnEntry{pTxInputData, GetTime() + ORPHAN_TX_EXPIRE_TIME, sz});
        assert(ret.second);
        for (const CTxIn &txin : tx.vin) {
            mOrphanTxnsByPrev[txin.prevout].insert(&((*(ret.first)).second));
        }
        orphanTxnsTotal = mOrphanTxns.size();
        orphanTxnsByPrevTotal = mOrphanTxnsByPrev.size();
    }
    // A log message
    LogPrint(BCLog::MEMPOOL,
            "stored orphan txn= %s (mapsz %u outsz %u)\n",
             txid.ToString(),
             orphanTxnsTotal,
             orphanTxnsByPrevTotal);
}

void COrphanTxns::addToCompactExtraTxns(const CTransactionRef &tx) {
    std::unique_lock lock {mExtraTxnsForCompactMtx};
    addToCompactExtraTxnsNL(tx);
}

int COrphanTxns::eraseTxn(const uint256& hash) {
    int count = 0;
    size_t orphanTxnsTotal {0};
    size_t orphanTxnsByPrevTotal {0};
    {
        std::unique_lock lock {mOrphanTxnsMtx};
        count = eraseTxnNL(hash);
        if (count) {
            orphanTxnsTotal = mOrphanTxns.size();
            orphanTxnsByPrevTotal = mOrphanTxnsByPrev.size();
        }
    }
    if (count) {
        LogPrint(BCLog::MEMPOOL,
                "removed orphan txn= %s (mapsz %u outsz %u)\n",
                 hash.ToString(),
                 orphanTxnsTotal,
                 orphanTxnsByPrevTotal);
    }
    return count;
}

void COrphanTxns::eraseTxnsFromPeer(NodeId peer) {
    int nErased = 0;
    {
        std::unique_lock lock {mOrphanTxnsMtx};
        OrphanTxnsIter iter = mOrphanTxns.begin();
        while (iter != mOrphanTxns.end()) {
            // Increment to avoid iterator becoming invalid.
            OrphanTxnsIter maybeErase = iter++;
            auto pNode = maybeErase->second.pTxInputData->GetNodePtr().lock();
            if (pNode && pNode->GetId() == peer) {
                nErased += eraseTxnNL(maybeErase->second.pTxInputData->GetTxnPtr()->GetId());
            }
        }
    }
    if (nErased > 0) {
        LogPrint(BCLog::MEMPOOL,
                "Erased %d orphan txn from peer=%d\n",
                 nErased,
                 peer);
    }
}

void COrphanTxns::eraseTxns() {
    std::unique_lock lock {mOrphanTxnsMtx};
    mOrphanTxns.clear();
    mOrphanTxnsByPrev.clear();
}

bool COrphanTxns::checkTxnExists(const COutPoint& prevout) const {
    std::shared_lock lock {mOrphanTxnsMtx};
    return  mOrphanTxnsByPrev.find(prevout) != mOrphanTxnsByPrev.end();
}

bool COrphanTxns::checkTxnExists(const uint256& txHash) const {
    std::shared_lock lock {mOrphanTxnsMtx};
    return checkTxnExistsNL(txHash);
}

std::vector<uint256> COrphanTxns::getTxnsHash(const COutPoint& prevout) const {
    std::shared_lock lock {mOrphanTxnsMtx};
    std::vector<uint256> vOrphanErase {};
    auto itByPrev = mOrphanTxnsByPrev.find(prevout);
    if (itByPrev == mOrphanTxnsByPrev.end()) {
        return vOrphanErase;
    }
    for (auto mi = itByPrev->second.begin(); mi != itByPrev->second.end(); ++mi) {
        vOrphanErase.emplace_back((*mi)->pTxInputData->GetTxnPtr()->GetHash());
    }
    return vOrphanErase;
}

CompactExtraTxnsVec COrphanTxns::getCompactExtraTxns() const {
    std::shared_lock lock {mExtraTxnsForCompactMtx};
    return mExtraTxnsForCompact;
}

unsigned int COrphanTxns::limitTxnsSize(uint64_t nMaxOrphanTxnsSize,
                                        uint64_t nMaxOrphanTxnsHysteresis,
                                        bool fSkipRndEviction) {
    unsigned int nEvicted {0};
    uint64_t nOrphanTxnsSize {0};
    int64_t nNow {0};
    int64_t nMinExpTime {0};
    int nErasedTimeLimit {0};
    assert(nMaxOrphanTxnsHysteresis <= nMaxOrphanTxnsSize);
    if ( mUntrimmedSize < nMaxOrphanTxnsHysteresis ) {
        // exit early while we're below the hysteresis
        return 0;
    }
    {
        std::unique_lock lock {mOrphanTxnsMtx};
        if ( mUntrimmedSize < nMaxOrphanTxnsHysteresis ) {
            // we lost the race, exit early
            return 0;
        } else {
            // mUntrimmedSize is only read outside the lock, we are not losing any updates
            mUntrimmedSize = 0;
        }
        // this algo is really expensive in terms of number of entries, not the number
        // of entries removed. Calling it per transaction kills paralelism.
        // Trim more than requested to make headroom for skipping it in further calls, see above.
        nMaxOrphanTxnsSize -= nMaxOrphanTxnsHysteresis;

        // TODO: Replace the following loop with two separate dedicated data structures:
        //       a good timer wheel to track expiry
        //       move the accounting of size to add and remove so it's always up-to-date
        // Note: it is too early to do this optimisation now as there are much bigger bottlenecks
        // in the code so no JIRA is created for this for now.
        nNow = GetTime();
        nMinExpTime = nNow + ORPHAN_TX_EXPIRE_TIME - ORPHAN_TX_EXPIRE_INTERVAL;
        // Sweep out expired orphan pool entries:
        OrphanTxnsIter iter = mOrphanTxns.begin();
        while (iter != mOrphanTxns.end()) {
            OrphanTxnsIter maybeErase = iter++;
            unsigned int txSize =  maybeErase->second.size;
            const CTransactionRef& ptx = maybeErase->second.pTxInputData->GetTxnPtr();
            if (mNextSweep <= nNow) {
                if (maybeErase->second.nTimeExpire <= nNow) {
                    nErasedTimeLimit += eraseTxnNL(ptx->GetId());
                } else {
                    nMinExpTime = std::min(maybeErase->second.nTimeExpire, nMinExpTime);
                    // Calculate size of all transactions
                    nOrphanTxnsSize += txSize;
                }
            }
            else
            {
                nOrphanTxnsSize +=txSize;
            }
        }
        if (mNextSweep <= nNow) {
            // Sweep again 5 minutes after the next entry that expires in order to
            // batch the linear scan.
            mNextSweep = nMinExpTime + ORPHAN_TX_EXPIRE_INTERVAL;
        }

        // If the limit is still not reached then remove a random txn
        while (!fSkipRndEviction && nOrphanTxnsSize > nMaxOrphanTxnsSize) {
            OrphanTxnsIter it = mOrphanTxns.begin();
            // Make sure we never go below 0 (causing overflow in uint)
            const unsigned int txTotalSize {
                it->second.pTxInputData->GetTxnPtr()->GetTotalSize()
            };
            if (txTotalSize >= nOrphanTxnsSize)
            {
                nOrphanTxnsSize = 0;
            }
            else
            {
                nOrphanTxnsSize -= txTotalSize;
            }
            eraseTxnNL(it->first);
            ++nEvicted;
        }
    }
    // Log a message
    if (nErasedTimeLimit) {
        LogPrint(BCLog::MEMPOOL,
                "Erased %d orphan txn due to expiration\n",
                 nErasedTimeLimit);
    }

    return nEvicted;
}

// The method is used to get any awaiting orphan txs that can be reprocessed.
// - this decision is made based on outpoints which were produced by newly accepted txs
// - the algorithm does not check if all missing outpoints are available
// - the order of returned orphans is non-deterministic
//   (including those from the direct parent)
std::vector<TxInputDataSPtr> COrphanTxns::collectDependentTxnsForRetry() {
    std::unordered_set<TxInputDataSPtr> usetTxnsToReprocess {};
    {
        std::unique_lock<std::shared_mutex> lock1(mOrphanTxnsMtx, std::defer_lock);
        std::unique_lock<std::mutex> lock2(mCollectedOutpointsMtx, std::defer_lock);
        std::lock(lock1, lock2);
        // Return immediately if there is nothing to find.
        if (mCollectedOutpoints.empty()) {
            return {};
        }
        // If there is no orphan txns then remove any collected outpoints.
        if (mOrphanTxns.empty()) {
            mCollectedOutpoints.clear();
            return {};
        }
        // Iterate over all collected outpoints to find dependent orphan txns.
        auto collectedOutpointIter = mCollectedOutpoints.begin();
        while (collectedOutpointIter != mCollectedOutpoints.end()) {
            // Find if there is any dependent orphan txn.
            auto outpointFoundIter = mOrphanTxnsByPrev.find(*collectedOutpointIter);
            if (outpointFoundIter == mOrphanTxnsByPrev.end()) {
                ++collectedOutpointIter;
                continue;
            }
            for (const COrphanTxnEntry* pOrphanEntry : outpointFoundIter->second) {
               const TxInputDataSPtr& pTxInputData { pOrphanEntry->pTxInputData };
               if(usetTxnsToReprocess.insert(pTxInputData).second) {
                   pTxInputData->SetAcceptTime(GetTime());
               }
            }
            ++collectedOutpointIter;
        }
        mCollectedOutpoints.clear();
    }
    return {usetTxnsToReprocess.begin(), usetTxnsToReprocess.end()};
}

void COrphanTxns::collectTxnOutpoints(const CTransaction& tx) {
    size_t nTxOutpointsNum = tx.vout.size();
    std::lock_guard lock {mCollectedOutpointsMtx};
    // Check if we need to make a room for new outpoints before adding them.
    if (mMaxCollectedOutpoints &&
        (mCollectedOutpoints.size() + nTxOutpointsNum > mMaxCollectedOutpoints)) {
        if (nTxOutpointsNum < mMaxCollectedOutpoints) {
            // Discard a set of the oldest elements (estimated by nTxOutpointsNum value)
            std::rotate(
                    mCollectedOutpoints.begin(),
                    mCollectedOutpoints.begin() + nTxOutpointsNum,
                    mCollectedOutpoints.end());
            // Remove old elements to make a room for new outpoints.
            mCollectedOutpoints.resize(mCollectedOutpoints.size() - nTxOutpointsNum);
        } else {
            mCollectedOutpoints.clear();
        }
    }
    // Add new outpoints
    auto txhash = tx.GetId();
    for (size_t i=0; i<nTxOutpointsNum; ++i) {
        mCollectedOutpoints.emplace_back(COutPoint{txhash, (uint32_t)i});
    }
}

void COrphanTxns::eraseCollectedOutpoints() {
    std::lock_guard lock {mCollectedOutpointsMtx};
    mCollectedOutpoints.clear();
}

void COrphanTxns::eraseCollectedOutpointsFromTxns(const std::vector<TxId>& vRemovedTxIds) {
    std::lock_guard lock {mCollectedOutpointsMtx};
    for (const auto& txid : vRemovedTxIds) {
        auto firstElemIter {
            std::find_if(
               mCollectedOutpoints.begin(),
               mCollectedOutpoints.end(),
               [&txid](const COutPoint& outpoint) {
                   return txid == outpoint.GetTxId(); })
        };
        if (firstElemIter == mCollectedOutpoints.end()) {
            continue;
        }
        // Find the first non-matching element (starting from firstElemIter)
        // The outpoints are stored continously in the vector.
        auto endOfRangeIter {
            std::find_if(
               firstElemIter,
               mCollectedOutpoints.end(),
               [&txid](const COutPoint& outpoint) {
                   return txid != outpoint.GetTxId(); })
        };
        // Erase all elements from the range: [firstElemIter, endOfRangeIter)
        mCollectedOutpoints.erase(firstElemIter, endOfRangeIter);
    }
}

/** Get TxIds of known orphan transactions */
std::vector<TxId> COrphanTxns::getTxIds() const {
    std::shared_lock lock {mOrphanTxnsMtx};
    if (mOrphanTxns.empty()) {
        return {};
    }
    std::vector<TxId> vTxIds;
    vTxIds.reserve(mOrphanTxns.size());
    for (const auto& elem: mOrphanTxns) {
        vTxIds.emplace_back(elem.first);
    }
    return vTxIds;
}

size_t COrphanTxns::getTxnsNumber() {
    std::shared_lock lock {mOrphanTxnsMtx};
    return mOrphanTxns.size();
}

std::vector<COutPoint> COrphanTxns::getCollectedOutpoints() {
    std::lock_guard lock {mCollectedOutpointsMtx};
    return mCollectedOutpoints;
}

TxInputDataSPtr COrphanTxns::getRndOrphan() {
    std::shared_lock lock {mOrphanTxnsMtx};
    return !mOrphanTxns.empty() ? mOrphanTxns.begin()->second.pTxInputData : nullptr;
}

void COrphanTxns::addToCompactExtraTxnsNL(const CTransactionRef &tx) {
    if (!mMaxExtraTxnsForCompactBlock) {
        return;
    }
    if (!mExtraTxnsForCompact.size()) {
        mExtraTxnsForCompact.resize(mMaxExtraTxnsForCompactBlock);
    }
    mExtraTxnsForCompact[mExtraTxnsForCompactIdx] =
        std::make_pair(tx->GetId(), tx);
    mExtraTxnsForCompactIdx = (mExtraTxnsForCompactIdx + 1) % mMaxExtraTxnsForCompactBlock;
}

bool COrphanTxns::checkTxnExistsNL(const uint256& txHash) const {
    return mOrphanTxns.find(txHash) != mOrphanTxns.end();
}

int COrphanTxns::eraseTxnNL(const uint256& hash) {
    OrphanTxnsIter it = mOrphanTxns.find(hash);
    if (it == mOrphanTxns.end()) {
        return 0;
    }
    const COrphanTxnEntry* pOrphanEntry { &it->second };
    for (const CTxIn &txin : pOrphanEntry->pTxInputData->GetTxnPtr()->vin) {
        auto itPrev = mOrphanTxnsByPrev.find(txin.prevout);
        if (itPrev == mOrphanTxnsByPrev.end()) {
            continue;
        }
        itPrev->second.erase(pOrphanEntry);
        if (itPrev->second.empty()) {
            mOrphanTxnsByPrev.erase(itPrev);
        }
    }
    mOrphanTxns.erase(it);
    return 1;
}
