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
{
    mGenerator.seed(std::chrono::system_clock::now().time_since_epoch().count());
}

void COrphanTxns::addTxn(const TxInputDataSPtr& pTxInputData) {
    if (!pTxInputData) {
        return;
    }
    // Mark txn as orphan
    pTxInputData->mfOrphan = true;
    const CTransactionRef& ptx = pTxInputData->mpTx;
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
        if (TxSource::p2p == pTxInputData->mTxSource) {
            if (sz > mMaxStandardTxSize /*mMaxStandardTxSize is always set to after genesis value. If non default value is used for policy tx size then orphan tx before genesis might not get accepted by mempool */) {
                LogPrint(BCLog::MEMPOOL,
                         "ignoring large orphan tx (size: %u, hash: %s)\n", sz,
                         txid.ToString());
                return;
            }
            addToCompactExtraTxns(ptx);
        }
        auto ret = mOrphanTxns.emplace(
            txid, COrphanTxnEntry{pTxInputData, GetTime() + ORPHAN_TX_EXPIRE_TIME, sz});
        assert(ret.second);
        for (const CTxIn &txin : tx.vin) {
            mOrphanTxnsByPrev[txin.prevout].insert(ret.first);
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
            auto pNode = maybeErase->second.pTxInputData->mpNode.lock();
            if (pNode && pNode->GetId() == peer) {
                nErased += eraseTxnNL(maybeErase->second.pTxInputData->mpTx->GetId());
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
        const CTransactionRef& ptx = (*mi)->second.pTxInputData->mpTx;
        const CTransaction &orphanTx = *ptx;
        const uint256 &orphanHash = orphanTx.GetHash();
        vOrphanErase.emplace_back(orphanHash);
    }
    return vOrphanErase;
}

CompactExtraTxnsVec COrphanTxns::getCompactExtraTxns() const {
    std::shared_lock lock {mExtraTxnsForCompactMtx};
    return mExtraTxnsForCompact;
}

unsigned int COrphanTxns::limitTxnsSize(uint64_t nMaxOrphanTxnsSize,
                                          bool fSkipRndEviction) {
    unsigned int nEvicted {0};
    uint64_t nOrphanTxnsSize {0};
    int64_t nNow {0};
    int64_t nMinExpTime {0};
    int nErasedTimeLimit {0};
    {
        std::unique_lock lock {mOrphanTxnsMtx};
        nNow = GetTime();
        nMinExpTime = nNow + ORPHAN_TX_EXPIRE_TIME - ORPHAN_TX_EXPIRE_INTERVAL;
        // Sweep out expired orphan pool entries:
        OrphanTxnsIter iter = mOrphanTxns.begin();
        while (iter != mOrphanTxns.end()) {
            OrphanTxnsIter maybeErase = iter++;
            unsigned int txSize =  maybeErase->second.size;
            const CTransactionRef& ptx = maybeErase->second.pTxInputData->mpTx;
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
            uint256 randomhash = GetRandHash();
            OrphanTxnsIter it = mOrphanTxns.lower_bound(randomhash);
            if (it == mOrphanTxns.end()) {
                it = mOrphanTxns.begin();
            }
            
            const CTransactionRef& ptx = it->second.pTxInputData->mpTx;
            const CTransaction& tx = *ptx;
            // Make sure we never go below 0 (causing overflow in uint)
            unsigned int txTotalSize = tx.GetTotalSize();
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

std::vector<TxInputDataSPtr> COrphanTxns::collectDependentTxnsForRetry() {
    std::vector<TxInputDataSPtr> vRetryTxns {};
    std::set<TxInputDataSPtr, CTxnIdComparator> setRetryTxns {};
    {
        std::unique_lock<std::shared_mutex> lock1(mOrphanTxnsMtx, std::defer_lock);
        std::unique_lock<std::mutex> lock2(mCollectedOutpointsMtx, std::defer_lock);
        std::lock(lock1, lock2);
        // Return immediately if there is nothing to find.
        if (mCollectedOutpoints.empty()) {
            return vRetryTxns;
        }
        // If there is no orphan txns then remove any collected outpoints.
        if (mOrphanTxns.empty()) {
            mCollectedOutpoints.clear();
            return vRetryTxns;
        }

        // Iterate over all collected outpoints to find dependent orphan txns.
        std::vector<std::pair<COutPoint, OrphanTxnsByPrevIter>> vDependentOrphans {};
        auto collectedOutpointIter = mCollectedOutpoints.begin();
        while (collectedOutpointIter != mCollectedOutpoints.end()) {
             // Find if there is any dependent orphan txn.
             auto outpointFoundIter = mOrphanTxnsByPrev.find(*collectedOutpointIter);
             if (outpointFoundIter == mOrphanTxnsByPrev.end()) {
                 ++collectedOutpointIter;
                 continue;
             }
             vDependentOrphans.emplace_back(std::make_pair(*collectedOutpointIter, outpointFoundIter));
             ++collectedOutpointIter;
        }

        // Only outpoints for which a dependency was found will be kept.
        mCollectedOutpoints.clear();

        // Iterate over found dependent orphans and schedule them for retry.
        // - due to descendant size & counter calculations we can take only one outpoint,
        //   of the given parent, in the current call.
        // - take all orphans only for the first found outpoint of the given parent
        // - the remaining child txns of the given parent will be used by the next invocation
        auto dependentOrphanIter = vDependentOrphans.begin();
        while (dependentOrphanIter != vDependentOrphans.end()) {
            // Take all matching orphans for the current outpoint.
            // In this way we do not prioritize which orphan should be scheduled for retry.
            // In batch processing, the Double Spend Detector (DSD) will allow to pass through validation only the first seen orphan
            // (and reject the rest of them). The rejected orphans will be processed sequentially when batch processing is finished.
            for (const auto& iterOrphanTxn : dependentOrphanIter->second->second) {
                const auto& pTxInputData {
                    (*iterOrphanTxn).second.pTxInputData
                };
                // Add txn to the result set if it's not there yet. Otherwise, a multiple entry of the same txn would be added,
                // if it has more than one parent, for which outpoints were collected during the current interval.
                setRetryTxns.
                    insert(
                        std::make_shared<CTxInputData>(
                                           pTxInputData->mTxSource,   // tx source
                                           pTxInputData->mTxValidationPriority,     // tx validation priority
                                           pTxInputData->mpTx,        // a pointer to the tx
                                           GetTime(),                 // nAcceptTime
                                           pTxInputData->mfLimitFree, // fLimitFree
                                           pTxInputData->mnAbsurdFee, // nAbsurdFee
                                           pTxInputData->mpNode,      // pNode
                                           pTxInputData->mfOrphan));  // fOrphan
            }
            // We cannot simply return all dependent orphan txns to the given tx.vout of the parent tx.
            // The limit for descendant size & counter would not be properly calculated/updated.
            // Any remaining outpoints (of the current txid - the parent) will be used by the next invocation
            // of the method.
            // At this stage we don't want to allow the current outpoint to be used again.
            auto txid = dependentOrphanIter->first.GetTxId();
            auto nextTxnIter {
                std::find_if(
                   ++dependentOrphanIter,
                   vDependentOrphans.end(),
                   [&txid](const std::pair<COutPoint, OrphanTxnsByPrevIter>& elem) {
                       return txid != elem.first.GetTxId(); })
            };
            // The remaining outpoints should be randomly shuffled.
            std::shuffle(dependentOrphanIter, nextTxnIter, mGenerator);
            // Store outpoints for a later usage.
            while (dependentOrphanIter != nextTxnIter) {
                mCollectedOutpoints.emplace_back(dependentOrphanIter->first);
                ++dependentOrphanIter;
            }
        }
    }
    // Move elements into vector.
    vRetryTxns.insert(vRetryTxns.end(),
        std::make_move_iterator(setRetryTxns.begin()),
        std::make_move_iterator(setRetryTxns.end()));
    return vRetryTxns;
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

size_t COrphanTxns::getTxnsNumber() {
    std::shared_lock lock {mOrphanTxnsMtx};
    return mOrphanTxns.size();
}

std::vector<COutPoint> COrphanTxns::getCollectedOutpoints() {
    std::lock_guard lock {mCollectedOutpointsMtx};
    return mCollectedOutpoints;
}

TxInputDataSPtr COrphanTxns::getRndOrphanByLowerBound(const uint256& key) {
    std::shared_lock lock {mOrphanTxnsMtx};
    if (mOrphanTxns.empty()) {
        return {nullptr};
    }
    auto txIter = mOrphanTxns.lower_bound(key);
    if (txIter == mOrphanTxns.end()) {
        txIter = mOrphanTxns.begin();
    }
    return txIter->second.pTxInputData;
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
    for (const CTxIn &txin : it->second.pTxInputData->mpTx->vin) {
        auto itPrev = mOrphanTxnsByPrev.find(txin.prevout);
        if (itPrev == mOrphanTxnsByPrev.end()) {
            continue;
        }
        itPrev->second.erase(it);
        if (itPrev->second.empty()) {
            mOrphanTxnsByPrev.erase(itPrev);
        }
    }
    mOrphanTxns.erase(it);
    return 1;
}
