// Copyright (c) 2019 The Bitcoin SV developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "txn_validator.h"
#include "txn_validation_config.h"
#include "config.h"
#include "net_processing.h"

/** Constructor */
CTxnValidator::CTxnValidator(
    const Config& config,
    CTxMemPool& mpool,
    TxnDoubleSpendDetectorSPtr dsDetector)
    : mConfig(config),
      mMempool(mpool),
      mpTxnDoubleSpendDetector(dsDetector) {
    // Configure our running frequency
    auto runFreq { gArgs.GetArg("-txnvalidationasynchrunfreq", DEFAULT_ASYNCH_RUN_FREQUENCY_MILLIS) };
    mAsynchRunFrequency = std::chrono::milliseconds {runFreq};
    LogPrint(BCLog::TXNVAL,
            "Txnval: Run frequency in asynchronous mode: %u milisec\n",
             runFreq);
    // Create a shared object for orphan transaction
    size_t maxCollectedOutpoints {
        static_cast<size_t>(
            gArgs.GetArg("-maxcollectedoutpoints",
                        COrphanTxns::DEFAULT_MAX_COLLECTED_OUTPOINTS))
    };
    size_t maxExtraTxnsForCompactBlock {
        static_cast<size_t>(
                gArgs.GetArg("-blockreconstructionextratxn",
                        COrphanTxns::DEFAULT_BLOCK_RECONSTRUCTION_EXTRA_TXN))
    };
    mpOrphanTxnsP2PQ = std::make_shared<COrphanTxns>(
                                            maxCollectedOutpoints,
                                            maxExtraTxnsForCompactBlock);
    // Create a shared object for rejected transaction
    mpTxnRecentRejects = std::make_shared<CTxnRecentRejects>();
    // Launch our thread
    mNewTxnsThread = std::thread(&CTxnValidator::threadNewTxnHandler, this);
}

/** Destructor */
CTxnValidator::~CTxnValidator() {
    shutdown();
}

/** Shutdown and clean up */
void CTxnValidator::shutdown() {
    // Only shutdown once
    bool expected {true};
    if(mRunning.compare_exchange_strong(expected, false)) {
        // Shutdown thread
        {
            std::unique_lock lock { mMainMtx };
            mMainCV.notify_one();
        }
        mNewTxnsThread.join();
    }
}

/** Get the frequency we run */
std::chrono::milliseconds CTxnValidator::getRunFrequency() const {
    std::shared_lock lock { mMainMtx };
    return mAsynchRunFrequency;
}

/** Set the frequency we run */
void CTxnValidator::setRunFrequency(const std::chrono::milliseconds& freq) {
    std::unique_lock lock { mMainMtx };
    mAsynchRunFrequency = freq;
    // Also wake up the processing thread so that it is then rescheduled at the right frequency
    mMainCV.notify_one();
}

/** Get orphan txn object */
std::shared_ptr<COrphanTxns> CTxnValidator::getOrphanTxnsPtr() {
    return mpOrphanTxnsP2PQ;
}

/** Get recent txn rejects object */
std::shared_ptr<CTxnRecentRejects> CTxnValidator::getTxnRecentRejectsPtr() {
    return mpTxnRecentRejects;
}

/** Wait for the Validator to process all queued txns. Used to support testing. */
void CTxnValidator::waitForEmptyQueue(bool fCheckOrphanQueueEmpty) {
    std::shared_lock lock { mNewTxnsMtx };
    mTxnsProcessedCV.wait(lock,
            [&] { return !mNewTxns.size() &&
                         (fCheckOrphanQueueEmpty ? !mpOrphanTxnsP2PQ->getTxnsNumber() : true); });
}

/** Handle a new transaction */
void CTxnValidator::newTransaction(TxInputDataSPtr pTxInputData) {
    const TxId& txid = pTxInputData->mpTx->GetId();
    // Check if exists in mNewTxns
    std::unique_lock lock { mNewTxnsMtx };
    if (!isTxnKnownInSetNL(txid, mNewTxns)) {
        // Check if exists in mProcessingQueue
        std::shared_lock lock2 { mProcessingQueueMtx };
        if (!isTxnKnownInSetNL(txid, mProcessingQueue)) {
            // Add the given txn to the list of new transactions.
            mNewTxns.emplace_back(std::move(pTxInputData));
        }
    }
}

/** Handle a batch of new transactions */
void CTxnValidator::newTransaction(std::vector<TxInputDataSPtr> vTxInputData) {
    // Add it to the list of new transactions
    for (auto&& txInputData : vTxInputData) {
        newTransaction(std::move(txInputData));
    }
}

/** Process a new txn in synchronous mode */
CValidationState CTxnValidator::processValidation(
    const TxInputDataSPtr& pTxInputData,
    const mining::CJournalChangeSetPtr& changeSet,
    bool fLimitMempoolSize) {

    const CTransactionRef& ptx = pTxInputData->mpTx;
    const CTransaction &tx = *ptx;
    LogPrint(BCLog::TXNVAL,
            "Txnval-synch: Got a new txn= %s \n",
             tx.GetId().ToString());
    // TODO: A temporary workaroud uses cs_main lock to control pcoinsTip change
    // A synchronous interface locks mtxs in the following order:
    // - first: cs_main
    // - second: mMainMtx
    // It needs to be in that way as the wallet itself (and it's rpc interface) locks
    // cs_main in many places and holds it (mostly rpc interface) for an entire duration of the call.
    // A synchronous interface is called from a different threads:
    // - bitcoin-main, bitcoin-loadblk. bitcoin-httpwor.
    LOCK(cs_main);
    std::unique_lock lock { mMainMtx };
    CTxnValResult result {};
    // Execute txn validation
    result = TxnValidation(
                pTxInputData,
                mConfig,
                mMempool,
                mpTxnDoubleSpendDetector);
    // Special handlers
    CTxnHandlers handlers {
        // Mempool Journal ChangeSet
        changeSet,
        // Double Spend Detector
        mpTxnDoubleSpendDetector,
        // Orphan p2p txns queue
        mpOrphanTxnsP2PQ,
        // Recent rejects queue
        mpTxnRecentRejects,
    };
    // Process validated results for the given txn
    ProcessValidatedTxn(mMempool, result, handlers, fLimitMempoolSize);
    // Notify subscribers that a new txn was added to the mempool and not
    // removed from there due to LimitMempoolSize.
    if (result.mState.IsValid()) {
        GetMainSignals().TransactionAddedToMempool(result.mTxInputData->mpTx);
    }
    // After we've (potentially) uncached entries, ensure our coins cache is
    // still within its size limits
    CValidationState dummyState;
    FlushStateToDisk(mConfig.GetChainParams(), dummyState, FLUSH_STATE_PERIODIC);

    return result.mState;
}

/** Thread entry point for new transaction queue handling */
void CTxnValidator::threadNewTxnHandler() noexcept {
    try {
        RenameThread("bitcoin-txnvalidator");
        LogPrint(BCLog::TXNVAL, "New transaction handling thread. Starting validator.\n");
        while(mRunning) {
            // Run every few seconds or until stopping
            std::unique_lock lock { mMainMtx };
            mMainCV.wait_for(lock, mAsynchRunFrequency);
            // Check if we are still running
            if(mRunning) {
                // Catch an exception if it occurs
                try {
                    {
                        // TODO: A temporary workaroud uses cs_main lock to control pcoinsTip change
                        // An asynchronous interface locks mtxs in the following order:
                        // - first: mMainMtx
                        // - second: TRY_LOCK(cs_main) (only if cs_main is not used by anyone)
                        // This approach allows to:
                        // - avoid race conditions between synchronous and asynchronous interface.
                        // - it gives priority to synchronous interface.
                        // - avoid changes in the wallet itself as it strongly relies on cs_main.
                        TRY_LOCK(cs_main, lockMain);
                        if (!lockMain) {
                            continue;
                        }
                        // Lock mNewTxnsMtx & mProcessingQueueMtx for a minimal duration to get all queued txns.
                        {
                            std::unique_lock<std::shared_mutex> lock1(mNewTxnsMtx, std::defer_lock);
                            std::unique_lock<std::shared_mutex> lock2(mProcessingQueueMtx, std::defer_lock);
                            std::lock(lock1, lock2);
                            mProcessingQueue = std::move_if_noexcept(mNewTxns);
                        }
                        // Lock processing queue in a shared mode as it might be queried during processing.
                        {
                            std::shared_lock lockPQ { mProcessingQueueMtx };
                            // Process all new transactions (if any exists)
                            if(!mProcessingQueue.empty()) {
                                LogPrint(BCLog::TXNVAL, "Txnval-asynch: Got %d new transactions\n",
                                         mProcessingQueue.size());
                                // Mempool Journal ChangeSet
                                mining::CJournalChangeSetPtr changeSet {
                                    mempool.getJournalBuilder()->getNewChangeSet(mining::JournalUpdateReason::NEW_TXN)
                                };
                                // Validate txns and try to submit them to the mempool
                                std::vector<TxInputDataSPtr> vAcceptedTxns {
                                    processNewTransactionsNL(mProcessingQueue, changeSet)
                                };
                                // Trim mempool if it's size exceeds the limit.
                                std::vector<TxId> vRemovedTxIds {
                                    LimitMempoolSize(
                                        mMempool,
                                        changeSet,
                                        gArgs.GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * 1000000,
                                        gArgs.GetArg("-mempoolexpiry", DEFAULT_MEMPOOL_EXPIRY) * 60 * 60)
                                };
                                // Execute post processing steps.
                                postProcessingP2PStepsNL(vAcceptedTxns, vRemovedTxIds);
                                // After we've (potentially) uncached entries, ensure our coins cache is
                                // still within its size limits
                                CValidationState dummyState;
                                FlushStateToDisk(mConfig.GetChainParams(), dummyState, FLUSH_STATE_PERIODIC);
                            }
                        }
                    }
                    // Clear processing queue.
                    {
                        std::unique_lock lockPQ { mProcessingQueueMtx };
                        mProcessingQueue.clear();
                    }
                    // Copy orphan p2p txns for re-try (if any exists)
                    size_t nOrphanP2PTxnsNum = scheduleOrphanP2PTxnsForRetry();
                    if (nOrphanP2PTxnsNum) {
                        LogPrint(BCLog::TXNVAL,
                                "%s: Number of orphan txns scheduled for retry %d\n",
                                 enum_cast<std::string>(TxSource::p2p),
                                 nOrphanP2PTxnsNum);
                    } else {
                        mTxnsProcessedCV.notify_one();
                    }
                } catch (const std::exception& e) {
                    LogPrint(BCLog::TXNVAL,
                            "An exception thrown in new txn thread: %s\n",
                             e.what());
                } catch (...) {
                    LogPrint(BCLog::TXNVAL,
                            "Unexpected exception in new txn thread (in the loop)\n");
                }
            }
        }
        LogPrint(BCLog::TXNVAL, "New transaction handling thread. Stopping validator.\n");
    } catch (...) {
        LogPrint(BCLog::TXNVAL, "Unexpected exception in new txn thread\n");
    }
}

/**
* Process all new transactions.
*/
std::vector<TxInputDataSPtr>
CTxnValidator::processNewTransactionsNL(
    std::vector<TxInputDataSPtr>& txns,
    mining::CJournalChangeSetPtr& journalChangeSet) {

    auto tx_validation = [](const TxInputDataSPtrRefVec& vTxInputData,
                            const Config* config,
                            CTxMemPool *pool,
                            CTxnHandlers& handlers) {
        return TxnValidationBatchProcessing(
                     vTxInputData,
                    *config,
                    *pool,
                     handlers);
    };
    // Special handlers
    CTxnHandlers handlers {
        journalChangeSet,
        mpTxnDoubleSpendDetector,
        mpOrphanTxnsP2PQ,
        mpTxnRecentRejects
    };
    // Set number of txns per thread threshold
    size_t nTxnsPerTaskThreshold {
        static_cast<size_t>(gArgs.GetArg("-txnspertaskthreshold", DEFAULT_TXNS_PER_TASK_THRESHOLD))
    };
    // Trigger parallel validation for txns
    auto results {
        g_connman->ParallelTxValidationBatchProcessing(
                        tx_validation,
                        nTxnsPerTaskThreshold,
                        &mConfig,
                        &mMempool,
                        txns,
                        handlers)
    };
    // Process validation results for transactions.
    std::vector<TxInputDataSPtr> vAcceptedTxns {};
    for(auto& task_result : results) {
        auto vBatchResults = task_result.get();
        for (auto& result : vBatchResults) {
            postValidationP2PStepsNL(result, vAcceptedTxns);
        }
    }
    return vAcceptedTxns;
}

void CTxnValidator::postValidationP2PStepsNL(
    const CTxnValResult& txStatus,
    std::vector<TxInputDataSPtr>& vAcceptedTxns) const {

    const CValidationState& state = txStatus.mState;
    if (state.IsValid()) {
        // Txns accepted by the mempool
        vAcceptedTxns.emplace_back(txStatus.mTxInputData);
    } else {
        // Misbehaving needs to be called by the Validator thread due to cs_main lock.
        // It is used internally by the method to protect an access to mapNodeState.
        // mapNodeState needs to be decoupled from cs_main, then we can move it back to p2p hadling methods.
        if (TxSource::p2p == txStatus.mTxInputData->mTxSource) {
            if (!state.IsMissingInputs()) {
                int nDoS = 0;
                if (state.IsInvalid(nDoS) && nDoS > 0) {
                    const CNodePtr& pNode = txStatus.mTxInputData->mpNode;
                    // Punish the peer that gave us an invalid orphan tx
                    if (pNode) {
                        Misbehaving(
                            pNode->GetId(),
                            nDoS,
                            txStatus.mTxInputData->mfOrphan ? "invalid-orphan-tx" : state.GetRejectReason());
                    } else {
                        LogPrint(BCLog::TXNVAL, "An invalid reference: Node doesn't exist");
                    }
                }
            }
        }
    }
}

void CTxnValidator::postProcessingP2PStepsNL(
    const std::vector<TxInputDataSPtr>& vAcceptedTxns,
    const std::vector<TxId>& vRemovedTxIds) {

    /**
     * 1. Send tx reject message if txn was accepted by the mempool
     * and then removed from there because of insufficient fee.
     *
     * 2. Notify subscribers if a new txn is accepted and not removed.
     */
    for (const auto& pTxInputDataSPtr: vAcceptedTxns) {
        if (std::find(
                vRemovedTxIds.begin(),
                vRemovedTxIds.end(),
                pTxInputDataSPtr->mpTx->GetId()) != vRemovedTxIds.end()) {
            // Create a reject message for the removed txn
            CreateTxRejectMsgForP2PTxn(
                pTxInputDataSPtr,
                REJECT_INSUFFICIENTFEE,
                std::string("mempool full"));
        } else {
            // Notify subscribers that a new txn was added to the mempool.
            // At this stage we do know that the signal won't be triggered for removed txns.
            // This needs to be here due to cs_main lock held by wallet's implementation of the signal
            GetMainSignals().TransactionAddedToMempool(pTxInputDataSPtr->mpTx);
        }
    }
    /**
     * We don't want to keep outpoints from txns which were
     * removed from the mempool (because of insufficient fee).
     * It could schedule false-possitive orphans for re-try.
     */
    mpOrphanTxnsP2PQ->eraseCollectedOutpointsFromTxns(vRemovedTxIds);
}

// The method needs to take mNewTxnsMtx lock to move collected orphan txns
// to the mNewTxns queue.
// Collected orphnas are created as copies and not removed from the orphan's queue.
size_t CTxnValidator::scheduleOrphanP2PTxnsForRetry() {
    /** Get p2p orphan txns */
    auto vOrphanTxns { mpOrphanTxnsP2PQ->collectDependentTxnsForRetry() };
    size_t nOrphanTxnsNum { vOrphanTxns.size() };
    if (nOrphanTxnsNum) {
        // Move p2p orphan txns into the main queue
        std::unique_lock lock { mNewTxnsMtx };
        mNewTxns.insert(mNewTxns.end(),
            std::make_move_iterator(vOrphanTxns.begin()),
            std::make_move_iterator(vOrphanTxns.end()));
    }
    return nOrphanTxnsNum;
}

bool CTxnValidator::isTxnKnown(const uint256& txid) const {
    // Check if exists in mNewTxns
    std::shared_lock lock { mNewTxnsMtx };
    if (!isTxnKnownInSetNL(txid, mNewTxns)) {
        // Check if exists in mProcessingQueue
        std::shared_lock lock2 { mProcessingQueueMtx };
        return isTxnKnownInSetNL(txid, mProcessingQueue);
    }
    // Txn is already known
    return true;
}

bool CTxnValidator::isTxnKnownInSetNL(
    const uint256& txid,
    const std::vector<TxInputDataSPtr>& vTxns) const {

    return findIfTxnIsInSetNL(txid, vTxns) != vTxns.end();
}

std::vector<TxInputDataSPtr>::const_iterator CTxnValidator::findIfTxnIsInSetNL(
    const uint256& txid,
    const std::vector<TxInputDataSPtr>& vTxns) const {

    return std::find_if(
            vTxns.begin(),
            vTxns.end(),
            [&txid](const TxInputDataSPtr& ptxInputData){
                return ptxInputData->mpTx->GetId() == txid; });
}
