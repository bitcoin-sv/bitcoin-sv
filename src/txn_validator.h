// Copyright (c) 2019 The Bitcoin SV developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include "orphan_txns.h"
#include "txn_double_spend_detector.h"
#include "txn_handlers.h"
#include "txn_validation_data.h"
#include "txn_recent_rejects.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <vector>

/**
 * A class representing txn Validator.
 *
 * It supports new transactions that need to be validated.
 * It provides synchronous and asynchronous validation interface.
 * - synchronous (blocking) calls are supported by processValidation method
 * - asynchronous (non-blocking) calls are supported by newTransaction method
 */
class CTxnValidator final
{
  public:
    // Default run frequency in asynch mode
    static constexpr unsigned DEFAULT_ASYNCH_RUN_FREQUENCY_MILLIS {100};
    // Default maximum validation duration for async tasks in a single run
    static constexpr std::chrono::milliseconds DEFAULT_MAX_ASYNC_TASKS_RUN_DURATION {
        std::chrono::seconds(10)
    };
    // Default maximum memory usage (in MB) for the transaction queues
    static constexpr uint64_t DEFAULT_MAX_MEMORY_TRANSACTION_QUEUES {2048};

    // Construction/destruction
    CTxnValidator(
        const Config& mConfig,
        CTxMemPool& mpool,
        TxnDoubleSpendDetectorSPtr dsDetector);
    ~CTxnValidator();

    // Forbid copying/assignment
    CTxnValidator(const CTxnValidator&) = delete;
    CTxnValidator(CTxnValidator&&) = delete;
    CTxnValidator& operator=(const CTxnValidator&) = delete;
    CTxnValidator& operator=(CTxnValidator&&) = delete;

    /** Shutdown and clean up */
    void shutdown();

    /** Get/set the frequency we run */
    std::chrono::milliseconds getRunFrequency() const;
    void setRunFrequency(const std::chrono::milliseconds& freq);

    /**
     * Asynchronous txn validation interface.
     */
    /** Handle a new transaction */
    void newTransaction(TxInputDataSPtr pTxInputData);
    void newTransaction(TxInputDataSPtrVec pTxInputData);
    void resubmitTransaction(TxInputDataSPtr pTxInputData);

    /**
     * Synchronous txn validation interface.
     */
    /** Process a new txn with wait */
    CValidationState processValidation(
        const TxInputDataSPtr& txInputData,
        const mining::CJournalChangeSetPtr& changeSet,
        bool fLimitMempoolSize=false);
    /** Process a set of txns */
    void processValidation(
        TxInputDataSPtrVec vTxInputData,
        const mining::CJournalChangeSetPtr& changeSet,
        bool fLimitMempoolSize=false);

    /**
     * Orphan & rejected txns handlers.
     */
    /** Get a pointer to the object with orphan txns */
    std::shared_ptr<COrphanTxns> getOrphanTxnsPtr();

    /** Get a pointer to the object which controls recently rejected txns */
    std::shared_ptr<CTxnRecentRejects> getTxnRecentRejectsPtr();

    /** Wait for the Validator to process all queued txns (through asynch interface) */
    void waitForEmptyQueue(bool fCheckOrphanQueueEmpty=true);

    /** Get number of transactions that are still unvalidated */
    size_t GetTransactionsInQueueCount() const;

    /** Get memory usage for still unvalidated transactions */
    uint64_t GetStdQueueMemUsage() const { return mStdTxnsMemSize; }
    uint64_t GetNonStdQueueMemUsage() const { return mNonStdTxnsMemSize; }

    /** Check if the given txn is already queued for processing (or being processed)
     *  in asynch mode by the Validator */
    bool isTxnKnown(const uint256& txid) const;

  private:
    /** Thread entry point for new transaction queue handling */
    void threadNewTxnHandler() noexcept;

    /** Process all newly arrived transactions. Return txns accepted by the mempool */
    std::tuple<TxInputDataSPtrVec, TxInputDataSPtrVec, TxInputDataSPtrVec> processNewTransactionsNL(
        std::vector<TxInputDataSPtr>& txns,
        CTxnHandlers& handlers,
        bool fUseLimits,
        std::chrono::milliseconds maxasynctasksrunduration);

    /** Post validation step for txns before limit mempool size is done*/
    void postValidationStepsNL(
        const std::pair<CTxnValResult, CTask::Status>& result,
        std::vector<TxInputDataSPtr>& vAcceptedTxns,
        std::vector<TxInputDataSPtr>& vNonStdTxns,
        std::vector<TxInputDataSPtr>& vCancelledTxns) const;

    /** Post processing step for txns when limit mempool size is done */
    void postProcessingStepsNL(
        const std::vector<TxInputDataSPtr>& vAcceptedTxns,
        const std::vector<TxId>& vRemovedTxIds,
        CTxnHandlers& handlers);

    /** Schedule orphan p2p txns for retry into the main queue */
    size_t scheduleOrphanP2PTxnsForRetry();

    /** Check if the given txn is known in the given set of txns */
	template<typename T>
    bool isTxnKnownInSetNL(
        const uint256& txid,
        const T& vTxns) const {
        return findIfTxnIsInSetNL(txid, vTxns) != vTxns.end();
    }

    /** Find if the given txn is known in the given set of txns */
	template<typename T>
    typename T::const_iterator findIfTxnIsInSetNL(
        const uint256& txid,
        const T& vTxns) const {
		return std::find_if(
				vTxns.begin(),
				vTxns.end(),
				[&txid](const TxInputDataSPtr& ptxInputData){
					return ptxInputData->mpTx->GetId() == txid; });
	}

    /** Increase memory used counters for queued transactions */
    void incMemUsedNL(std::atomic<uint64_t>& mem, const TxInputDataSPtr& txn) {
        mem += txn->mpTx->GetTotalSize();
    }
    /** Decrease memory used counters for queued transactions */
    void decMemUsedNL(std::atomic<uint64_t>& mem, const TxInputDataSPtr& txn) {
        auto txnSize { txn->mpTx->GetTotalSize() };
        if(mem <= txnSize) {
            mem = 0;
        }
        else {
            mem -= txnSize;
        }
    }

    /** Return whether there is space in our queues for the given transaction */
    inline bool isSpaceForTxnNL(const TxInputDataSPtr& txn, const std::atomic<uint64_t>& currMemUsage) const;

    /** Add a standard txn to the queue */
    void enqueueStdTxnNL(const TxInputDataSPtr& txn);
    /** Add a non-standard txn to the queue */
    void enqueueNonStdTxnNL(const TxInputDataSPtr& txn);

    /** Add some txns (standard or non-standard) to the queue */
    template<typename Iterator, typename Callable>
    void enqueueTxnsNL(Iterator begin, const Iterator& end, Callable&& func) {
        std::for_each(begin, end, func);
    }

    template<typename T1, typename T2>
    void collectTxns(T1& dest, T2& src, size_t nNumOfTxns, size_t nMaxNumOfTxnsToSchedule, std::atomic<uint64_t>& mem) {
        auto end { nMaxNumOfTxnsToSchedule > nNumOfTxns ? src.end() : src.begin() + nMaxNumOfTxnsToSchedule };

        // Update memory tracking for collected txns
        std::for_each(src.begin(), end,
            [this, &mem](const TxInputDataSPtr& txn) mutable { decMemUsedNL(mem, txn); });

        // Move them to the destination list
        dest.insert(dest.end(),
                    std::make_move_iterator(src.begin()),
                    std::make_move_iterator(end));

        // Tidy up source
        src.erase(src.begin(), end);
    }

    /** List of new transactions that need processing */
    std::vector<TxInputDataSPtr> mStdTxns {};
    std::atomic<uint64_t> mStdTxnsMemSize {0};
    /** A dedicated mutex to protect an exclusive access to mStdTxns */
    mutable std::shared_mutex mStdTxnsMtx {};
    /** List of new non-standard transactions that need processing */
    std::deque<TxInputDataSPtr> mNonStdTxns {};
    std::atomic<uint64_t> mNonStdTxnsMemSize {0};
    /** A dedicated mutex to protect an exclusive access to mStdTxns */
    mutable std::shared_mutex mNonStdTxnsMtx {};
    /** A vector of txns which are currently being processed */
    std::vector<TxInputDataSPtr> mProcessingQueue {};
    /** A dedicated mutex to protect an access to mTxnsProcessingQueue */
    mutable std::shared_mutex mProcessingQueueMtx {};

    /** A common mutex used for:
     * - protecting mAsynchRunFrequency
     * - protecting shutdown of mNewTxnsThread
     * - controlling sync and async validation mode
     *   (only one can run at the same time)
     */
    mutable std::shared_mutex mMainMtx {};
    std::condition_variable_any mMainCV {};
    // A condition variable used to signal when all currently queued txns were processed.
    std::condition_variable_any mTxnsProcessedCV {} ;

    // A reference to the configuration
    const Config& mConfig;

    // The maximum transaction queue size in bytes. Applies to both the standard & non-standard queues.
    uint64_t mMaxQueueMemSize {DEFAULT_MAX_MEMORY_TRANSACTION_QUEUES};

    // A reference to the mempool
    CTxMemPool& mMempool;

    /** Handle orphan transactions */
    OrphanTxnsSPtr mpOrphanTxnsP2PQ {nullptr};
    /** Filter for transactions that were recently rejected */
    TxnRecentRejectsSPtr mpTxnRecentRejects {nullptr};
    /** Double spend detector */
    TxnDoubleSpendDetectorSPtr mpTxnDoubleSpendDetector {nullptr};

    /** Our main thread */
    std::thread mNewTxnsThread {};

    /** Flag to indicate we are running */
    std::atomic<bool> mRunning {true};

    /** Frequency we run in asynchronous mode */
    std::chrono::milliseconds mAsynchRunFrequency {DEFAULT_ASYNCH_RUN_FREQUENCY_MILLIS};
};
