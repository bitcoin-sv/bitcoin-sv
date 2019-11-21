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
    // Check the standard queue.
    {
        std::shared_lock lock { mStdTxnsMtx };
        mTxnsProcessedCV.wait(lock,
                [&] { return !mStdTxns.size() &&
                             (fCheckOrphanQueueEmpty ? !mpOrphanTxnsP2PQ->getTxnsNumber() : true); });
    }
    // Check the non-standard queue.
    {
        std::shared_lock lock { mNonStdTxnsMtx };
        mTxnsProcessedCV.wait(lock,
                [&] { return !mNonStdTxns.size() &&
                             (fCheckOrphanQueueEmpty ? !mpOrphanTxnsP2PQ->getTxnsNumber() : true); });
    }
}

size_t CTxnValidator::GetTransactionsInQueueCount() const {
    // Take shared locks in the following order.
    std::shared_lock lock1 { mStdTxnsMtx };
    std::shared_lock lock2 { mNonStdTxnsMtx };
    std::shared_lock lock3 { mProcessingQueueMtx };
    return mStdTxns.size() + mNonStdTxns.size() + mProcessingQueue.size();
}

/** Handle a new transaction */
void CTxnValidator::newTransaction(TxInputDataSPtr pTxInputData) {

    const TxId& txid = pTxInputData->mpTx->GetId();
    const TxType& txtype = pTxInputData->mTxType;
    // Check if exists in mStdTxns
    if (TxType::standard == txtype || TxType::unknown == txtype) {
        std::unique_lock lock { mStdTxnsMtx };
        if (!isTxnKnownInSetNL(txid, mStdTxns)) {
            // Check if exists in mProcessingQueue
            std::shared_lock lock2 { mProcessingQueueMtx };
            if (!isTxnKnownInSetNL(txid, mProcessingQueue)) {
                // Add the given txn to the list of new standard transactions.
                mStdTxns.emplace_back(std::move(pTxInputData));
            }
        }
    }
    // Check if exists in mNonStdTxns
    else if (TxType::nonstandard == txtype) {
        std::unique_lock lock { mNonStdTxnsMtx };
        if (!isTxnKnownInSetNL(txid, mNonStdTxns)) {
            // Check if exists in mProcessingQueue
            std::shared_lock lock2 { mProcessingQueueMtx };
            if (!isTxnKnownInSetNL(txid, mProcessingQueue)) {
                // Add the given txn to the list of new non-standard transactions.
                mNonStdTxns.emplace_back(std::move(pTxInputData));
            }
        }
    }
}

/** Handle a batch of new transactions */
void CTxnValidator::newTransaction(TxInputDataSPtrVec vTxInputData) {
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
            "Txnval-synch: Got a new txn= %s \n", tx.GetId().ToString());
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
                mpTxnDoubleSpendDetector,
                IsCurrentForFeeEstimation());
    // Special handlers
    CTxnHandlers handlers {
        changeSet, // Mempool Journal ChangeSet
        mpTxnDoubleSpendDetector, // Double Spend Detector
        TxSource::p2p == pTxInputData->mTxSource ? mpOrphanTxnsP2PQ : nullptr, // Orphan txns queue
        mpTxnRecentRejects // Recent rejects queue
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

/** Process a set of txns in synchronous mode */
void CTxnValidator::processValidation(
    TxInputDataSPtrVec vTxInputData,
    const mining::CJournalChangeSetPtr& changeSet,
    bool fLimitMempoolSize) {

    size_t vTxInputDataSize = vTxInputData.size();
    LogPrint(BCLog::TXNVAL,
            "Txnval-synch-batch: Got a set of %d new txns\n", vTxInputDataSize);
    // Check if there is anything to process
    if (!vTxInputDataSize) {
        return;
    }
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
    // A vector of accepted txns
    std::vector<TxInputDataSPtr> vAcceptedTxns {};
    // Check fee estimation requirements
    bool fReadyForFeeEstimation = IsCurrentForFeeEstimation();
    // Special handlers
    CTxnHandlers handlers {
        changeSet, // Mempool Journal ChangeSet
        mpTxnDoubleSpendDetector, // Double Spend Detector
        std::make_shared<COrphanTxns>(0, 0), // A temporary orphan txns queue (unlimited)
        std::make_shared<CTxnRecentRejects>() // A temporary recent rejects queue
    };
    // Process a set of given txns
    do {
        // Execute parallel validation
        auto vCurrAcceptedTxns {
            processNewTransactionsNL(vTxInputData, handlers, fReadyForFeeEstimation)
        };
        vAcceptedTxns.insert(vAcceptedTxns.end(),
            std::make_move_iterator(vCurrAcceptedTxns.begin()),
            std::make_move_iterator(vCurrAcceptedTxns.end()));
        // Get dependent orphans (if any exists)
        vTxInputData = handlers.mpOrphanTxns->collectDependentTxnsForRetry();
        vTxInputDataSize = vTxInputData.size();
        if (vTxInputDataSize) {
            LogPrint(BCLog::TXNVAL,
                    "Txnval-synch-batch: Reprocess a set of %d orphan txns\n", vTxInputDataSize);
        }
    } while (vTxInputDataSize);
    // Limit mempool size if required
    std::vector<TxId> vRemovedTxIds {};
    if (fLimitMempoolSize) {
        // Trim mempool if it's size exceeds the limit.
        vRemovedTxIds =
            LimitMempoolSize(
                mMempool,
                changeSet,
                gArgs.GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * 1000000,
                gArgs.GetArg("-mempoolexpiry", DEFAULT_MEMPOOL_EXPIRY) * 60 * 60);
    }
    // Execute post processing steps.
    postProcessingStepsNL(vAcceptedTxns, vRemovedTxIds, handlers);
    // After we've (potentially) uncached entries, ensure our coins cache is
    // still within its size limits
    CValidationState dummyState;
    FlushStateToDisk(mConfig.GetChainParams(), dummyState, FLUSH_STATE_PERIODIC);
}

/** Thread entry point for new transaction queue handling */
void CTxnValidator::threadNewTxnHandler() noexcept {
    try {
        RenameThread("bitcoin-txnvalidator");
        LogPrint(BCLog::TXNVAL, "New transaction handling thread. Starting validator.\n");
        // Get a number of High and Low priority threads.
        size_t nNumStdTxValidationThreads {
            static_cast<size_t>(
                    gArgs.GetArg("-numstdtxvalidationthreads", GetNumHighPriorityValidationThrs()))
        };
        size_t nNumNonStdTxValidationThreads {
            static_cast<size_t>(
                    gArgs.GetArg("-numnonstdtxvalidationthreads", GetNumLowPriorityValidationThrs()))
        };
        // Get a ratio for std and nonstd txns to be scheduled for validation in a single iteration.
        size_t nMaxStdTxnsPerThreadRatio {
            static_cast<size_t>(
                    gArgs.GetArg("-maxstdtxnsperthreadratio", DEFAULT_MAX_STD_TXNS_PER_THREAD_RATIO))
        };
        size_t nMaxNonStdTxnsPerThreadRatio {
            static_cast<size_t>(
                    gArgs.GetArg("-maxnonstdtxnsperthreadratio", DEFAULT_MAX_NON_STD_TXNS_PER_THREAD_RATIO))
        };
        // Get mempool limits.
        size_t nMaxMempoolSize = gArgs.GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * 1000000;
        unsigned long nMempoolExpiry = gArgs.GetArg("-mempoolexpiry", DEFAULT_MEMPOOL_EXPIRY) * 60 * 60;
        // The main running loop
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
                        // Lock mStdTxnsMtx, mNonStdTxnsMtx & mProcessingQueueMtx for a minimal duration to get queued txns.
                        {
                            std::unique_lock<std::shared_mutex> lock1(mStdTxnsMtx, std::defer_lock);
                            std::unique_lock<std::shared_mutex> lock2(mNonStdTxnsMtx, std::defer_lock);
                            std::unique_lock<std::shared_mutex> lock3(mProcessingQueueMtx, std::defer_lock);
                            std::lock(lock1, lock2, lock3);
                            // Get a required number of standard txns if any exists
                            size_t nNumOfStdTxns = mStdTxns.size();
                            size_t nMaxNumOfStdTxnsToSchedule = nMaxStdTxnsPerThreadRatio * nNumStdTxValidationThreads;
                            if (nNumOfStdTxns) {
                                LogPrint(BCLog::TXNVAL, "Txnval-asynch: The Standard queue, size= %d\n",
                                         nNumOfStdTxns);
                                if (nNumOfStdTxns < nMaxNumOfStdTxnsToSchedule) {
                                    mProcessingQueue = std::move_if_noexcept(mStdTxns);
                                } else {
                                    collectTxns(mProcessingQueue, mStdTxns, nNumOfStdTxns, nMaxNumOfStdTxnsToSchedule);
                                }
                            }
                            // Get a required number of non-standard txns if any exists
                            size_t nNumOfNonStdTxns = mNonStdTxns.size();
                            size_t nMaxNumOfNonStdTxnsToSchedule = nMaxNonStdTxnsPerThreadRatio * nNumNonStdTxValidationThreads;
                            if (nNumOfNonStdTxns) {
                                LogPrint(BCLog::TXNVAL, "Txnval-asynch: The Non-standard queue, size= %d\n",
                                         nNumOfNonStdTxns);
                                collectTxns(mProcessingQueue, mNonStdTxns, nNumOfNonStdTxns, nMaxNumOfNonStdTxnsToSchedule);
                            }
                        }
                        // Lock processing queue in a shared mode as it might be queried during processing.
                        {
                            std::shared_lock lockPQ { mProcessingQueueMtx };
                            // Process all new transactions (if any exists)
                            if(!mProcessingQueue.empty()) {
                                LogPrint(BCLog::TXNVAL, "Txnval-asynch: Got %d new transactions\n",
                                         mProcessingQueue.size());
                                // Special handlers
                                CTxnHandlers handlers {
                                    mMempool.getJournalBuilder()->getNewChangeSet(mining::JournalUpdateReason::NEW_TXN),
                                    mpTxnDoubleSpendDetector,
                                    mpOrphanTxnsP2PQ,
                                    mpTxnRecentRejects
                                };
                                // Check fee estimation requirements
                                bool fReadyForFeeEstimation = IsCurrentForFeeEstimation();
                                // Validate txns and try to submit them to the mempool
                                std::vector<TxInputDataSPtr> vAcceptedTxns {
                                    processNewTransactionsNL(
                                            mProcessingQueue,
                                            handlers,
                                            fReadyForFeeEstimation)
                                };
                                // Trim mempool if it's size exceeds the limit.
                                std::vector<TxId> vRemovedTxIds {
                                    LimitMempoolSize(
                                        mMempool,
                                        handlers.mJournalChangeSet,
                                        nMaxMempoolSize,
                                        nMempoolExpiry)
                                };
                                // Execute post processing steps.
                                postProcessingStepsNL(vAcceptedTxns, vRemovedTxIds, handlers);
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
    CTxnHandlers& handlers,
    bool fReadyForFeeEstimation) {

    // Trigger parallel validation
    auto results {
        g_connman->
            ParallelTxnValidation(
                [](const TxInputDataSPtr& pTxInputData,
                    const Config* config,
                    CTxMemPool *pool,
                    CTxnHandlers& handlers,
                    bool fReadyForFeeEstimation) {
                    return TxnValidationProcessingTask(
                                pTxInputData,
                               *config,
                               *pool,
                                handlers,
                                fReadyForFeeEstimation);
                },
                &mConfig,
                &mMempool,
                txns,
                handlers,
                fReadyForFeeEstimation)
    };
    // Process validation results
    std::vector<TxInputDataSPtr> vAcceptedTxns {};
    for(auto& result : results) {
        postValidationStepsNL(result.get(), vAcceptedTxns);
    }
    return vAcceptedTxns;
}

void CTxnValidator::postValidationStepsNL(
    const CTxnValResult& txStatus,
    std::vector<TxInputDataSPtr>& vAcceptedTxns) const {

    const CValidationState& state = txStatus.mState;
    if (state.IsValid()) {
        // Txns accepted by the mempool
        vAcceptedTxns.emplace_back(txStatus.mTxInputData);
    }
}

void CTxnValidator::postProcessingStepsNL(
    const std::vector<TxInputDataSPtr>& vAcceptedTxns,
    const std::vector<TxId>& vRemovedTxIds,
    CTxnHandlers& handlers) {

    /**
     * 1. Send tx reject message if p2p txn was accepted by the mempool
     * and then removed from there because of insufficient fee.
     *
     * 2. Notify subscribers if a new txn is accepted and not removed.
     *
     * 3. Do not keep outpoints from txns which were added to the mempool and then removed from there.
     */
    for (const auto& pTxInputDataSPtr: vAcceptedTxns) {
        if (!vRemovedTxIds.empty() &&
            std::find(
                vRemovedTxIds.begin(),
                vRemovedTxIds.end(),
                pTxInputDataSPtr->mpTx->GetId()) != vRemovedTxIds.end()) {
            // Removed p2p txns from the mempool
            if (TxSource::p2p == pTxInputDataSPtr->mTxSource) {
                // Create a reject message for the removed txn
                CreateTxRejectMsgForP2PTxn(
                    pTxInputDataSPtr,
                    REJECT_INSUFFICIENTFEE,
                    std::string("mempool full"));
            }
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
    if (handlers.mpOrphanTxns && !vRemovedTxIds.empty()) {
        handlers.mpOrphanTxns->eraseCollectedOutpointsFromTxns(vRemovedTxIds);
    }
}

// The method needs to take mStdTxnsMtx lock to move collected orphan txns
// to the mStdTxns queue.
// Collected orphnas are created as copies and not removed from the orphan's queue.
size_t CTxnValidator::scheduleOrphanP2PTxnsForRetry() {
    /** Get p2p orphan txns */
    auto vOrphanTxns { mpOrphanTxnsP2PQ->collectDependentTxnsForRetry() };
    size_t nOrphanTxnsNum { vOrphanTxns.size() };
    if (nOrphanTxnsNum) {
        // Move p2p orphan txns into the main queue
        std::unique_lock lock { mStdTxnsMtx };
        mStdTxns.insert(mStdTxns.end(),
            std::make_move_iterator(vOrphanTxns.begin()),
            std::make_move_iterator(vOrphanTxns.end()));
    }
    return nOrphanTxnsNum;
}

bool CTxnValidator::isTxnKnown(const uint256& txid) const {
    // Check if exists in standard queue
    std::shared_lock lock1 { mStdTxnsMtx };
    if (!isTxnKnownInSetNL(txid, mStdTxns)) {
        // Check if exists in non-standard queue
        std::shared_lock lock2 { mNonStdTxnsMtx };
        if (!isTxnKnownInSetNL(txid, mNonStdTxns)) {
            // Check if exists in mProcessingQueue
            std::shared_lock lock3 { mProcessingQueueMtx };
            return isTxnKnownInSetNL(txid, mProcessingQueue);
        }
    }
    // Txn is already known
    return true;
}
