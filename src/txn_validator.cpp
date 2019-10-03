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
        maxExtraTxnsForCompactBlock,
        config.GetMaxTxSize(true, false) /*orphan tx before genesis might not get accepted by mempool */);
    
    // Max memory usage for transaction queues
    mMaxQueueMemSize =
        static_cast<uint64_t>(
                gArgs.GetArg("-txnvalidationqueuesmaxmemory",
                        DEFAULT_MAX_MEMORY_TRANSACTION_QUEUES))
        * 1024 * 1024;
 
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
    const TxValidationPriority& txpriority = pTxInputData->mTxValidationPriority;
    // Check if exists in mStdTxns
    if (TxValidationPriority::high == txpriority || TxValidationPriority::normal == txpriority) {
        std::unique_lock lock { mStdTxnsMtx };
        if (!isTxnKnownInSetNL(txid, mStdTxns)) {
            // Check if exists in mProcessingQueue
            std::shared_lock lock2 { mProcessingQueueMtx };
            if (!isTxnKnownInSetNL(txid, mProcessingQueue)) {
                // Add the given txn to the list of new standard transactions.
                enqueueStdTxnNL(pTxInputData);
            }
        }
    }
    // Check if exists in mNonStdTxns
    else if (TxValidationPriority::low == txpriority) {
        std::unique_lock lock { mNonStdTxnsMtx };
        if (!isTxnKnownInSetNL(txid, mNonStdTxns)) {
            // Check if exists in mProcessingQueue
            std::shared_lock lock2 { mProcessingQueueMtx };
            if (!isTxnKnownInSetNL(txid, mProcessingQueue)) {
                // Add the given txn to the list of new non-standard transactions.
                enqueueNonStdTxnNL(pTxInputData);
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

/** Resubmit a transaction for reprocessing */
void CTxnValidator::resubmitTransaction(TxInputDataSPtr pTxInputData) {
    const TxId& txid = pTxInputData->mpTx->GetId();
    const TxValidationPriority& txpriority = pTxInputData->mTxValidationPriority;

    // Check if exists in mStdTxns
    if (TxValidationPriority::high == txpriority || TxValidationPriority::normal == txpriority) {
        std::unique_lock lock { mStdTxnsMtx };
        if(!isTxnKnownInSetNL(txid, mStdTxns)) {
            enqueueStdTxnNL(pTxInputData);
        }
    }
    // Check if exists in mNonStdTxns
    else if (TxValidationPriority::low == txpriority) {
        std::unique_lock lock { mNonStdTxnsMtx };
        if (!isTxnKnownInSetNL(txid, mNonStdTxns)) {
            enqueueNonStdTxnNL(pTxInputData);
        }
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
    // Special handlers
    CTxnHandlers handlers {
        changeSet, // Mempool Journal ChangeSet
        mpTxnDoubleSpendDetector, // Double Spend Detector
        TxSource::p2p == pTxInputData->mTxSource ? mpOrphanTxnsP2PQ : nullptr, // Orphan txns queue
        mpTxnRecentRejects // Recent rejects queue
    };
    try
    {
        // Execute txn validation (timed cancellation is not set).
        result = TxnValidation(
                    pTxInputData,
                    mConfig,
                    mMempool,
                    mpTxnDoubleSpendDetector,
                    false);
        // Process validated results for the given txn
        ProcessValidatedTxn(mMempool, result, handlers, fLimitMempoolSize);
    } catch (const std::exception& e) {
        return HandleTxnProcessingException("An exception thrown in txn processing: " + std::string(e.what()),
                    pTxInputData,
                    result,
                    mMempool,
                    handlers);
    } catch (...) {
        return HandleTxnProcessingException("Unexpected exception in txn processing",
                    pTxInputData,
                    result,
                    mMempool,
                    handlers);
    }
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
    // Special handlers
    CTxnHandlers handlers {
        changeSet, // Mempool Journal ChangeSet
        mpTxnDoubleSpendDetector, // Double Spend Detector
        std::make_shared<COrphanTxns>(0, 0, 0), // A temporary orphan txns queue (unlimited)
        std::make_shared<CTxnRecentRejects>() // A temporary recent rejects queue
    };
    // Process a set of given txns
    do {
        // Execute parallel validation.
        // The result is a tuple of three vectors:
        // - the first one contains accepted (by the mempool) txns
        // - the second one contains detected low priority txns
        // - the third one contains cancelled txns
        // There will be no detected non-standard and cancelled txns as:
        // - timed cancellation is not set
        // - maxasynctasksrunduration is not set to non-zero value
        auto result {
            processNewTransactionsNL(
                vTxInputData,
                handlers,
                false,
                std::chrono::milliseconds(0))
        };
        vAcceptedTxns.insert(vAcceptedTxns.end(),
            std::make_move_iterator(std::get<0>(result).begin()),
            std::make_move_iterator(std::get<0>(result).end()));
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
        // Get an expected duration for async tasks.
        std::chrono::milliseconds nMaxTxnValidatorAsyncTasksRunDuration {
            static_cast<std::chrono::milliseconds>(
                gArgs.GetArg("-maxtxnvalidatorasynctasksrunduration",
                    DEFAULT_MAX_ASYNC_TASKS_RUN_DURATION.count()))
        };
        // Ensure, that the last - long running task - won't exceed the limit.
        nMaxTxnValidatorAsyncTasksRunDuration -= mConfig.GetMaxNonStdTxnValidationDuration();
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
                    // The result is a tuple of three vectors:
                    // - the first one contains accepted (by the mempool) txns
                    // - the second one contains detected low priority txns
                    // - the third one contains cancelled txns
                    std::tuple<TxInputDataSPtrVec, TxInputDataSPtrVec, TxInputDataSPtrVec> result {};
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
                                LogPrint(BCLog::TXNVAL, "Txnval-asynch: The Standard queue, size= %d, mem= %ld\n",
                                         nNumOfStdTxns, mStdTxnsMemSize);
                                collectTxns(mProcessingQueue, mStdTxns, nNumOfStdTxns, nMaxNumOfStdTxnsToSchedule, mStdTxnsMemSize);
                            }
                            // Get a required number of non-standard txns if any exists
                            size_t nNumOfNonStdTxns = mNonStdTxns.size();
                            size_t nMaxNumOfNonStdTxnsToSchedule = nMaxNonStdTxnsPerThreadRatio * nNumNonStdTxValidationThreads;
                            if (nNumOfNonStdTxns && (mProcessingQueue.size() < nMaxNumOfStdTxnsToSchedule + nMaxNumOfNonStdTxnsToSchedule)) {
                                LogPrint(BCLog::TXNVAL, "Txnval-asynch: The Non-standard queue, size= %d, mem= %ld\n",
                                         nNumOfNonStdTxns, mNonStdTxnsMemSize);
                                collectTxns(mProcessingQueue, mNonStdTxns, nNumOfNonStdTxns, nMaxNumOfNonStdTxnsToSchedule, mNonStdTxnsMemSize);
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
                                mining::CJournalChangeSetPtr changeSet {
                                    mMempool.getJournalBuilder()->getNewChangeSet(mining::JournalUpdateReason::NEW_TXN) };
                                CTxnHandlers handlers {
                                    changeSet,
                                    mpTxnDoubleSpendDetector,
                                    mpOrphanTxnsP2PQ,
                                    mpTxnRecentRejects
                                };
                                // Validate txns and try to submit them to the mempool
                                result =
                                    processNewTransactionsNL(
                                        mProcessingQueue,
                                        handlers,
                                        true,
                                        nMaxTxnValidatorAsyncTasksRunDuration);
                                // Trim mempool if it's size exceeds the limit.
                                std::vector<TxId> vRemovedTxIds {
                                    LimitMempoolSize(
                                        mMempool,
                                        handlers.mJournalChangeSet,
                                        nMaxMempoolSize,
                                        nMempoolExpiry)
                                };
                                // Execute post processing steps.
                                postProcessingStepsNL(std::get<0>(result), vRemovedTxIds, handlers);
                                // After we've (potentially) uncached entries, ensure our coins cache is
                                // still within its size limits
                                CValidationState dummyState;
                                FlushStateToDisk(mConfig.GetChainParams(), dummyState, FLUSH_STATE_PERIODIC);
                            }
                        }
                    }
                    // If there are any low priority transactions then move them to the low priority queue.
                    size_t nDetectedLowPriorityTxnsNum = std::get<1>(result).size();
                    if (nDetectedLowPriorityTxnsNum) {
                        LogPrint(BCLog::TXNVAL,
                                "Txnval-asynch: Validation timeout occurred for %d txn(s) received from the Standard queue "
                                "(forwarding them to the Non-standard queue)\n",
                                 nDetectedLowPriorityTxnsNum);
                        std::unique_lock lock { mNonStdTxnsMtx };
                        enqueueTxnsNL(std::get<1>(result).begin(), std::get<1>(result).end(),
                            [this](const TxInputDataSPtr& txn){ enqueueNonStdTxnNL(txn); }
                        );
                    }
                    // Move back into the processing queue any tasks which were cancelled or clean the queue otherwise.
                    size_t nCancelledTxnsNum = std::get<2>(result).size();
                    if (nCancelledTxnsNum) {
                        LogPrint(BCLog::TXNVAL,
                                "Txnval-asynch: The number of %s txn(s) which were cancelled and moved to the next iteration is %d\n",
                                 enum_cast<std::string>(TxSource::p2p),
                                 nCancelledTxnsNum);
                        std::unique_lock lockPQ { mProcessingQueueMtx };
                        mProcessingQueue = std::move_if_noexcept(std::get<2>(result));
                    } else {
                        std::unique_lock lockPQ { mProcessingQueueMtx };
                        mProcessingQueue.clear();
                    }
                    // Copy orphan p2p txns for re-try (if any exists)
                    size_t nOrphanP2PTxnsNum = scheduleOrphanP2PTxnsForRetry();
                    if (nOrphanP2PTxnsNum) {
                        LogPrint(BCLog::TXNVAL,
                                "Txnval-asynch: The number of orphan %s txns that need to be reprocessed is %d\n",
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
std::tuple<TxInputDataSPtrVec, TxInputDataSPtrVec, TxInputDataSPtrVec> CTxnValidator::processNewTransactionsNL(
    std::vector<TxInputDataSPtr>& txns,
    CTxnHandlers& handlers,
    bool fUseLimits,
    std::chrono::milliseconds maxasynctasksrunduration) {

    // Trigger parallel validation
    auto results {
        g_connman->
            ParallelTxnValidation(
                [](const TxInputDataSPtr& pTxInputData,
                    const Config* config,
                    CTxMemPool *pool,
                    CTxnHandlers& handlers,
                    bool fUseLimits,
                    std::chrono::steady_clock::time_point end_time_point) {
                    return TxnValidationProcessingTask(
                                pTxInputData,
                               *config,
                               *pool,
                                handlers,
                                fUseLimits,
                                end_time_point);
                },
                &mConfig,
                &mMempool,
                txns,
                handlers,
                fUseLimits,
                maxasynctasksrunduration)
    };
    // All txns accepted by the mempool and not removed from there.
    std::vector<TxInputDataSPtr> vAcceptedTxns {};
    // If there is any standard 'high' priority txn for which validation timeout occurred, then
    // change it's priority to 'low' and forward it to the low priority queue.
    std::vector<TxInputDataSPtr> vDetectedLowPriorityTxns {};
    // A vector of cancelled txns.
    std::vector<TxInputDataSPtr> vCancelledTxns {};
    // Process validation results
    for(auto& result : results) {
        postValidationStepsNL(result.get(), vAcceptedTxns, vDetectedLowPriorityTxns, vCancelledTxns);
    }
    return {vAcceptedTxns, vDetectedLowPriorityTxns, vCancelledTxns};
}

void CTxnValidator::postValidationStepsNL(
    const std::pair<CTxnValResult, CTask::Status>& result,
    std::vector<TxInputDataSPtr>& vAcceptedTxns,
    std::vector<TxInputDataSPtr>& vDetectedLowPriorityTxns,
    std::vector<TxInputDataSPtr>& vCancelledTxns) const {

    const CTxnValResult& txStatus = result.first;
    const CValidationState& state = txStatus.mState;
    // Check task's status
    if (CTask::Status::Faulted == result.second) {
        return;
    }
    else if (CTask::Status::Canceled == result.second) {
        vCancelledTxns.emplace_back(txStatus.mTxInputData);
        return;
    }
    // Check validation state
    if (state.IsValid()) {
        // Txns accepted by the mempool
        vAcceptedTxns.emplace_back(txStatus.mTxInputData);
    } else if (state.IsValidationTimeoutExceeded()) {
        // If validation timeout occurred for 'high' priority txn then change it's priority to 'low'.
        TxValidationPriority& txpriority = txStatus.mTxInputData->mTxValidationPriority;
        if (TxValidationPriority::high == txpriority) {
            txpriority = TxValidationPriority::low;
            vDetectedLowPriorityTxns.emplace_back(txStatus.mTxInputData);
        }
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
        enqueueTxnsNL(vOrphanTxns.begin(), vOrphanTxns.end(),
            [this](const TxInputDataSPtr& txn){
                const TxId& txid = txn->mpTx->GetId();
                // Enqueue orphan txn if it is not already queued.
                if (!isTxnKnownInSetNL(txid, mStdTxns)) {
                    std::shared_lock lock1 { mNonStdTxnsMtx };
                    if (!isTxnKnownInSetNL(txid, mNonStdTxns)) {
                        std::shared_lock lock2 { mProcessingQueueMtx };
                        if(!isTxnKnownInSetNL(txid, mProcessingQueue)) {
                            enqueueStdTxnNL(txn);
                        }
                    }
                }
            }
        );
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

inline bool CTxnValidator::isSpaceForTxnNL(const TxInputDataSPtr& txn, const std::atomic<uint64_t>& currMemUsage) const {
    return (currMemUsage + txn->mpTx->GetTotalSize()) <= mMaxQueueMemSize;
}

void CTxnValidator::enqueueStdTxnNL(const TxInputDataSPtr& txn) {
    if(isSpaceForTxnNL(txn, mStdTxnsMemSize)) {
        // Add the given txn to the list of new standard transactions.
        mStdTxns.emplace_back(std::move(txn));
        // Increase memory tracking
        incMemUsedNL(mStdTxnsMemSize, txn);
    }
    else {
        LogPrint(BCLog::TXNVAL, "Dropping txn %s due to full std txn queue\n", txn->mpTx->GetId().ToString());
    }
}

void CTxnValidator::enqueueNonStdTxnNL(const TxInputDataSPtr& txn) {
    if(isSpaceForTxnNL(txn, mNonStdTxnsMemSize)) {
        // Add the given txn to the list of new non-standard transactions.
        mNonStdTxns.emplace_back(std::move(txn));
        // Increase memory tracking
        incMemUsedNL(mNonStdTxnsMemSize, txn);
    }
    else {
        LogPrint(BCLog::TXNVAL, "Dropping txn %s due to full non-std txn queue\n", txn->mpTx->GetId().ToString());
    }
}

