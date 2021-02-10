// Copyright (c) 2019 The Bitcoin SV developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "txn_validator.h"
#include "txn_validation_config.h"
#include "config.h"
#include "net/net_processing.h"

/** Constructor */
CTxnValidator::CTxnValidator(
    const Config& config,
    CTxMemPool& mpool,
    TxnDoubleSpendDetectorSPtr dsDetector,
    TxIdTrackerWPtr pTxIdTracker)
    : mConfig(config),
      mMempool(mpool),
      mpTxnDoubleSpendDetector(dsDetector),
      mpTxIdTracker(pTxIdTracker) {
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
                gArgs.GetArgAsBytes("-txnvalidationqueuesmaxmemory",
                        DEFAULT_MAX_MEMORY_TRANSACTION_QUEUES, ONE_MEBIBYTE));
 
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

/** Get the number of transactions waiting to be processed. */
size_t CTxnValidator::GetTransactionsInQueueCount() const {
    // Take shared locks in the following order.
    std::shared_lock lock1 { mStdTxnsMtx };
    std::shared_lock lock2 { mNonStdTxnsMtx };
    std::shared_lock lock3 { mProcessingQueueMtx };
    return mStdTxns.size() + mNonStdTxns.size() + mProcessingQueue.size();
}

/** Handle a new transaction */
void CTxnValidator::newTransaction(TxInputDataSPtr pTxInputData) {
    const TxValidationPriority& txpriority = pTxInputData->GetTxValidationPriority();
    // Add transaction to the right queue based on priority.
    if (TxValidationPriority::high == txpriority || TxValidationPriority::normal == txpriority) {
        std::unique_lock lock { mStdTxnsMtx };
        enqueueStdTxnNL(pTxInputData);
    }
    else if (TxValidationPriority::low == txpriority) {
        std::unique_lock lock { mNonStdTxnsMtx };
        enqueueNonStdTxnNL(pTxInputData);
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

    const CTransactionRef& ptx = pTxInputData->GetTxnPtr();
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
        TxSource::p2p == pTxInputData->GetTxSource() ? mpOrphanTxnsP2PQ : nullptr, // Orphan txns queue
        mpTxnRecentRejects // Recent rejects queue
    };
    try
    {
        // Execute txn validation (timed cancellation is not set).
        result =
            executeTxnValidationNL(
                pTxInputData,
                handlers,
                fLimitMempoolSize,
                false);
        // Check if txn is resubmitted for revalidation
        // - currently only finalised txn can be re-submitted
        if (result.mState.IsResubmittedTx()) {
            LogPrint(BCLog::TXNVAL,
                    "Txnval-synch: Reprocess txn= %s\n", tx.GetId().ToString());
            result =
                executeTxnValidationNL(
                    pTxInputData,
                    handlers,
                    fLimitMempoolSize,
                    false);
        }
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
        GetMainSignals().TransactionAddedToMempool(result.mTxInputData->GetTxnPtr());
    }
    // After we've (potentially) uncached entries, ensure our coins cache is
    // still within its size limits
    CValidationState dummyState;
    FlushStateToDisk(mConfig.GetChainParams(), dummyState, FLUSH_STATE_PERIODIC);

    return result.mState;
}

/** Process a set of txns in synchronous mode */
CTxnValidator::RejectedTxns CTxnValidator::processValidation(
    TxInputDataSPtrVec vTxInputData,
    const mining::CJournalChangeSetPtr& changeSet,
    bool fLimitMempoolSize) {

    size_t vTxInputDataSize = vTxInputData.size();
    LogPrint(BCLog::TXNVAL,
            "Txnval-synch-batch: Got a set of %d new txns\n", vTxInputDataSize);
    // Check if there is anything to process
    if (!vTxInputDataSize) {
        return {};
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
    // A hash table containing invalid transacions, including their validation state.
    CTxnValidator::InvalidTxnStateUMap mInvalidTxns {};
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
        // The imdResult is an object containing four vectors of pointers, to:
        // - accepted (by the mempool) txns
        // - detected low priority txns
        // - cancelled txns
        // - txns that need to be re-submitted
        // There will be no detected non-standard and cancelled txns as:
        // - timed cancellation is not set
        // - maxasynctasksrunduration is not set to non-zero value
        CIntermediateResult imdResult {
            processNewTransactionsNL(
                vTxInputData,
                handlers,
                false,
                std::chrono::milliseconds(0))
        };
        vAcceptedTxns.insert(vAcceptedTxns.end(),
            std::make_move_iterator(imdResult.mAcceptedTxns.begin()),
            std::make_move_iterator(imdResult.mAcceptedTxns.end()));
        // Move invalid txns into the result hash table (if any exists)
        mInvalidTxns.insert(imdResult.mInvalidTxns.begin(), imdResult.mInvalidTxns.end());
        // Get resubmitted transactions (if any exists)
        size_t numResubmittedTxns = imdResult.mResubmittedTxns.size();
        if (numResubmittedTxns) {
            vTxInputData = std::move_if_noexcept(imdResult.mResubmittedTxns);
        }
        // Get dependent orphans (if any exists)
        TxInputDataSPtrVec vOrphanTxns = handlers.mpOrphanTxns->collectDependentTxnsForRetry();
        size_t numOrphanTxns = vOrphanTxns.size();
        if (numOrphanTxns) {
            if (numResubmittedTxns) {
                vTxInputData.insert(vTxInputData.end(),
                    std::make_move_iterator(vOrphanTxns.begin()),
                    std::make_move_iterator(vOrphanTxns.end()));
            } else {
                vTxInputData = std::move_if_noexcept(vOrphanTxns);
            }
        } else if (!numResubmittedTxns) {
            // There are no resubmitted or orphan transactions, so clear the vector.
            vTxInputData.clear();
        }
        // Add data logging.
        vTxInputDataSize = vTxInputData.size();
        if (vTxInputDataSize) {
            LogPrint(BCLog::TXNVAL,
                    "Txnval-synch-batch: Reprocess a set of %ld txns (resubmitted: %ld, orphans: %ld)\n",
                    vTxInputDataSize,
                    numResubmittedTxns,
                    numOrphanTxns);
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
                MempoolSizeLimits::FromConfig());
    } /*TODO*/
    // Execute post processing steps.
    postProcessingStepsNL(vAcceptedTxns, vRemovedTxIds, handlers);
    // After we've (potentially) uncached entries, ensure our coins cache is
    // still within its size limits
    CValidationState dummyState;
    FlushStateToDisk(mConfig.GetChainParams(), dummyState, FLUSH_STATE_PERIODIC);
    // If there are any orphan transactions, then include them in the result set
    std::vector<TxId> vOrphanTxIds = handlers.mpOrphanTxns->getTxIds();
    for (auto&& txid: vOrphanTxIds) {
        CValidationState state;
        state.SetMissingInputs();
        mInvalidTxns.try_emplace(std::move(txid), std::move(state));
    }
    return {mInvalidTxns, vRemovedTxIds};
}

/** Thread entry point for new transaction queue handling */
void CTxnValidator::threadNewTxnHandler() noexcept {
    try {
        RenameThread("txnvalidator");
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
        MempoolSizeLimits nLimits(MempoolSizeLimits::FromConfig());

        // The main running loop
        while(mRunning) {
            // Run every few seconds or until stopping
            std::unique_lock lock { mMainMtx };
            mMainCV.wait_for(lock, mAsynchRunFrequency);
            // Check if we are still running
            if(mRunning) {
                // Catch an exception if it occurs
                try {
                    // The imdResult is an object containing four vectors of pointers, to:
                    // - accepted (by the mempool) txns
                    // - detected low priority txns
                    // - cancelled txns
                    // - txns that need to be re-submitted
                    CIntermediateResult imdResult {};
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
                            size_t nNumOfStdTxns = mStdTxns.size();
                            size_t nMaxNumOfStdTxnsToSchedule = nMaxStdTxnsPerThreadRatio * nNumStdTxValidationThreads;
                            size_t nNumOfNonStdTxns = mNonStdTxns.size();
                            size_t nMaxNumOfNonStdTxnsToSchedule = nMaxNonStdTxnsPerThreadRatio * nNumNonStdTxValidationThreads;
                            // Get a required number of standard txns if any exists
                            // - due to cancelled txns (from the previous run), get new txns only if the threshold allows.
                            if (nNumOfStdTxns && (mProcessingQueue.size() < nMaxNumOfStdTxnsToSchedule + nMaxNumOfNonStdTxnsToSchedule)) {
                                LogPrint(BCLog::TXNVAL, "Txnval-asynch: The Standard queue, size= %d, mem= %ld\n",
                                         nNumOfStdTxns, mStdTxnsMemSize);
                                collectTxns(mProcessingQueue, mStdTxns, nNumOfStdTxns, nMaxNumOfStdTxnsToSchedule, mStdTxnsMemSize);
                            }
                            // Get a required number of non-standard txns if any exists
                            // - due to cancelled txns (from the previous run), get new txns only if the threshold allows.
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
                                    mMempool.getJournalBuilder().getNewChangeSet(mining::JournalUpdateReason::NEW_TXN) };
                                CTxnHandlers handlers {
                                    changeSet,
                                    mpTxnDoubleSpendDetector,
                                    mpOrphanTxnsP2PQ,
                                    mpTxnRecentRejects
                                };
                                // Validate txns and try to submit them to the mempool
                                imdResult =
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
                                        nLimits)
                                };
                                // Execute post processing steps.
                                postProcessingStepsNL(imdResult.mAcceptedTxns, vRemovedTxIds, handlers);
                                // After we've (potentially) uncached entries, ensure our coins cache is
                                // still within its size limits
                                CValidationState dummyState;
                                FlushStateToDisk(mConfig.GetChainParams(), dummyState, FLUSH_STATE_PERIODIC);
                            }
                        }
                        // Clear the processing queue, as a result:
                        // - destroy any CTxInputData objects which are no longer being referenced by the owning shared ptr
                        {
                            std::unique_lock lockPQ { mProcessingQueueMtx };
                            mProcessingQueue.clear();
                        }
                    }
                    // If there are any low priority transactions then move them to the low priority queue.
                    size_t nDetectedLowPriorityTxnsNum = imdResult.mDetectedLowPriorityTxns.size();
                    if (nDetectedLowPriorityTxnsNum) {
                        LogPrint(BCLog::TXNVAL,
                                "Txnval-asynch: Validation timeout occurred for %d txn(s) received from the Standard queue "
                                "(forwarding them to the Non-standard queue)\n",
                                 nDetectedLowPriorityTxnsNum);
                        std::unique_lock lock { mNonStdTxnsMtx };
                        enqueueTxnsNL(imdResult.mDetectedLowPriorityTxns.begin(), imdResult.mDetectedLowPriorityTxns.end(),
                            [this](const TxInputDataSPtr& txn){ enqueueNonStdTxnNL(txn); }
                        );
                    }
                    // Move back into the processing queue any txns which were re-submitted.
                    size_t nResubmittedTxnsNum = imdResult.mResubmittedTxns.size();
                    if (nResubmittedTxnsNum) {
                        LogPrint(BCLog::TXNVAL,
                                "Txnval-asynch: The number of re-submitted txns that need to be reprocessed is %d\n",
                                 nResubmittedTxnsNum);
                        std::unique_lock lockPQ { mProcessingQueueMtx };
                        mProcessingQueue = std::move_if_noexcept(imdResult.mResubmittedTxns);
                    }
                    // Copy orphan p2p txns for reprocessing (if any exists)
                    size_t nOrphanP2PTxnsNum = scheduleOrphanP2PTxnsForReprocessing(imdResult.mCancelledTxns);
                    if (nOrphanP2PTxnsNum) {
                        LogPrint(BCLog::TXNVAL,
                                "Txnval-asynch: The number of orphan %s txns that need to be reprocessed is %d\n",
                                 enum_cast<std::string>(TxSource::p2p),
                                 nOrphanP2PTxnsNum);
                    }
                    // Move back into the processing queue any txns which were cancelled.
                    size_t nCancelledTxnsNum = imdResult.mCancelledTxns.size();
                    if (nCancelledTxnsNum) {
                        LogPrint(BCLog::TXNVAL,
                                "Txnval-asynch: The number of %s txn(s) which were cancelled and moved to the next iteration is %d\n",
                                 enum_cast<std::string>(TxSource::p2p),
                                 nCancelledTxnsNum);
                        std::unique_lock lockPQ { mProcessingQueueMtx };
                        if (nOrphanP2PTxnsNum || nResubmittedTxnsNum) {
                            mProcessingQueue.insert(mProcessingQueue.end(),
                                std::make_move_iterator(imdResult.mCancelledTxns.begin()),
                                std::make_move_iterator(imdResult.mCancelledTxns.end()));
                        } else {
                            mProcessingQueue = std::move_if_noexcept(imdResult.mCancelledTxns);
                        }
                    }
                    // If no orphan, cancelled and resubmitted transactions were detected, then:
                    // - the processing queue is empty
                    // - unblock one of the waiting threads (if any exists)
                    if (!nOrphanP2PTxnsNum && !nCancelledTxnsNum && !nResubmittedTxnsNum) {
                        std::shared_lock lockPQ { mProcessingQueueMtx };
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
 * Execute txn validation for a single transaction.
 */
CTxnValResult CTxnValidator::executeTxnValidationNL(
    const TxInputDataSPtr& pTxInputData,
    CTxnHandlers& handlers,
    bool fLimitMempoolSize,
    bool fUseLimits) {

    // Execute txn validation.
    CTxnValResult result =
        TxnValidation(
            pTxInputData,
            mConfig,
            mMempool,
            mpTxnDoubleSpendDetector,
            fUseLimits);
    // Process validated results for the given txn
    ProcessValidatedTxn(mMempool, result, handlers, fLimitMempoolSize, mConfig);
    return result;
}

/**
* Process all new transactions.
*/
CTxnValidator::CIntermediateResult CTxnValidator::processNewTransactionsNL(
    std::vector<TxInputDataSPtr>& txns,
    CTxnHandlers& handlers,
    bool fUseLimits,
    std::chrono::milliseconds maxasynctasksrunduration) {

    // Trigger parallel validation
    auto results {
        g_connman->
            ParallelTxnValidation(
                [](const TxInputDataSPtrRefVec& vTxInputData,
                    const Config* config,
                    CTxMemPool *pool,
                    CTxnHandlers& handlers,
                    bool fUseLimits,
                    std::chrono::steady_clock::time_point end_time_point) {
                    return TxnValidationProcessingTask(
                                vTxInputData,
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
    CIntermediateResult imdResult {};
    // Process validation results
    for(auto& task_result : results) {
        auto vBatchResults = task_result.get();
        for (auto& result : vBatchResults) {
            postValidationStepsNL(result, imdResult);
        }
    }
    return imdResult;
}

void CTxnValidator::postValidationStepsNL(
    const std::pair<CTxnValResult, CTask::Status>& result,
    CIntermediateResult& imdResult) {

    const CTxnValResult& txStatus = result.first;
    const CValidationState& state = txStatus.mState;
    // Check task's status
    if (CTask::Status::Faulted == result.second) {
        imdResult.mInvalidTxns.try_emplace(txStatus.mTxInputData->GetTxnPtr()->GetId(), state);
        return;
    }
    else if (CTask::Status::Canceled == result.second) {
        imdResult.mCancelledTxns.emplace_back(txStatus.mTxInputData);
        return;
    }
    // Check validation state
    if (state.IsValid()) {
        if (state.IsResubmittedTx()) {
            // Txns resubmitted for revalidation
            // - currently only finalised txns can be re-submitted
            imdResult.mResubmittedTxns.emplace_back(txStatus.mTxInputData);
        } else {
            // Txns accepted by the mempool
            imdResult.mAcceptedTxns.emplace_back(txStatus.mTxInputData);
        }
    } else if (state.IsValidationTimeoutExceeded()) {
        // If validation timeout occurred for 'high' priority txn then change it's priority to 'low'.
        TxValidationPriority& txpriority = txStatus.mTxInputData->GetTxValidationPriority();
        if (TxValidationPriority::high == txpriority) {
            txpriority = TxValidationPriority::low;
            imdResult.mDetectedLowPriorityTxns.emplace_back(txStatus.mTxInputData);
        } else {
            imdResult.mInvalidTxns.try_emplace(txStatus.mTxInputData->GetTxnPtr()->GetId(), state);
        }
    } else if (!state.IsMissingInputs()) {
        imdResult.mInvalidTxns.try_emplace(txStatus.mTxInputData->GetTxnPtr()->GetId(), state);
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
                pTxInputDataSPtr->GetTxnPtr()->GetId()) != vRemovedTxIds.end()) {
            // Removed p2p txns from the mempool
            if (TxSource::p2p == pTxInputDataSPtr->GetTxSource()) {
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
            GetMainSignals().TransactionAddedToMempool(pTxInputDataSPtr->GetTxnPtr());
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

// A p2p orphan txn can be scheduled if:
// - it is not present in the set of cancelled txns.
// - it was not detected before and scheduled as a non-stdandard txn.
// Collected orphnas are created as copies and not removed from the orphan's queue.
size_t CTxnValidator::scheduleOrphanP2PTxnsForReprocessing(const TxInputDataSPtrVec& vCancelledTxns) {
    /** Get p2p orphan txns */
    auto vOrphanTxns { mpOrphanTxnsP2PQ->collectDependentTxnsForRetry(mpTxIdTracker) };
    size_t nOrphanTxnsNum { vOrphanTxns.size() };
    if (nOrphanTxnsNum) {
        // Remove those orphans which are present in the set of cancelled txns or already enqueued.
        eraseTxnIfNL(vOrphanTxns,
            [this, &vCancelledTxns](const TxInputDataSPtr& txn){
                const TxId& txid = txn->GetTxnPtr()->GetId();
                return isTxnKnownInSetNL(txid, vCancelledTxns); });
        // Move txns into the processing queue.
        nOrphanTxnsNum = vOrphanTxns.size();
        if (nOrphanTxnsNum) {
            std::unique_lock lockPQ { mProcessingQueueMtx };
            if (mProcessingQueue.empty()) {
                mProcessingQueue = std::move_if_noexcept(vOrphanTxns);
            } else {
                mProcessingQueue.insert(mProcessingQueue.end(),
                    std::make_move_iterator(vOrphanTxns.begin()),
                    std::make_move_iterator(vOrphanTxns.end()));
            }
        }
    }
    return nOrphanTxnsNum;
}

inline bool CTxnValidator::isSpaceForTxnNL(const TxInputDataSPtr& txn, const std::atomic<uint64_t>& currMemUsage) const {
    return (currMemUsage + txn->GetTxnPtr()->GetTotalSize()) <= mMaxQueueMemSize;
}

bool CTxnValidator::enqueueStdTxnNL(const TxInputDataSPtr& txn) {
    if (!txn->IsTxIdStored()) {
        LogPrint(BCLog::TXNVAL, "Dropping known std txn= %s\n", txn->GetTxnPtr()->GetId().ToString());
        return false;
    }
    if(isSpaceForTxnNL(txn, mStdTxnsMemSize)) {
        // Add the given txn to the list of new standard transactions.
        mStdTxns.emplace_back(std::move(txn));
        // Increase memory tracking
        incMemUsedNL(mStdTxnsMemSize, txn);
        return true;
    }
    else {
        LogPrint(BCLog::TXNVAL, "Dropping txn %s due to full std txn queue\n", txn->GetTxnPtr()->GetId().ToString());
    }
    return false;
}

bool CTxnValidator::enqueueNonStdTxnNL(const TxInputDataSPtr& txn) {
    if (!(txn->IsTxIdStored() || txn->IsOrphanTxn())) {
        LogPrint(BCLog::TXNVAL, "Dropping known non-std txn= %s\n", txn->GetTxnPtr()->GetId().ToString());
        return false;
    }
    if(isSpaceForTxnNL(txn, mNonStdTxnsMemSize)) {
        // Add the given txn to the list of new non-standard transactions.
        mNonStdTxns.emplace_back(std::move(txn));
        // Increase memory tracking
        incMemUsedNL(mNonStdTxnsMemSize, txn);
        return true;
    }
    else {
        LogPrint(BCLog::TXNVAL, "Dropping txn %s due to full non-std txn queue\n", txn->GetTxnPtr()->GetId().ToString());
    }
    return false;
}

/** Check if the given txn is already queued for processing (an expensive check).
 * An interface to facilitate Unit Tests. */
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

/** Wait for the Validator to process all queued txns.
 * An interface to facilitate Unit Tests.*/
void CTxnValidator::waitForEmptyQueue(bool fCheckOrphanQueueEmpty) {
    do {
       std::shared_lock lock { mProcessingQueueMtx };
       // Block the calling thread until notification is received and the predicate is not satisfied
       // (excluding the fact of being unblocked spuriously).
       mTxnsProcessedCV.wait(lock,
               [&] { return (fCheckOrphanQueueEmpty ? !mpOrphanTxnsP2PQ->getTxnsNumber() : true); });
    // Check that there is no transactions in the ptv queues after getting a notification from the processing queue.
    } while (GetTransactionsInQueueCount());
}
