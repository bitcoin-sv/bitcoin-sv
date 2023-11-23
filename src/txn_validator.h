// Copyright (c) 2019 The Bitcoin SV developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include "orphan_txns.h"
#include "txn_double_spend_detector.h"
#include "txn_handlers.h"
#include "txn_recent_rejects.h"
#include "txn_util.h"
#include "txn_validation_data.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
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
  // Public type aliases
  public:
    using InvalidTxnStateUMap = std::unordered_map<TxId, CValidationState, std::hash<TxId>>;
    using RemovedTxns = std::vector<TxId>;
    using RejectedTxns = std::pair<InvalidTxnStateUMap, RemovedTxns>;

    class QueueCounts {
        size_t mStd {0};
        size_t mNonStd {0};
        size_t mProcessing {0};
    public:
        QueueCounts(size_t std, size_t nonStd, size_t processing)
        : mStd {std}
        , mNonStd {nonStd}
        , mProcessing {processing}
        {}
        size_t GetStdQueueCount() const { return mStd; }
        size_t GetNonStdQueueCount() const { return mNonStd; }
        size_t GetProcessingQueueCount() const { return mProcessing; }

        size_t GetTotal() const { return mStd + mNonStd + mProcessing; }
    };

  private:
    /**
     * A local structure used to extend lifetime of CTxInputData objects (controlled by shared ptrs)
     * which are being returned by processNewTransactionsNL call.
     * Then, an additional actions are executed on those results, as a part of:
     * - post porcessing steps
     * - txn reprocessing
     * - tracking invalid txns (for instance, rpc interface support)
     */
	struct CIntermediateResult final
	{
        // Txns accepted by the mempool and not removed from there.
        TxInputDataSPtrVec mAcceptedTxns {};
        // Low priority txns detected during processing.
        TxInputDataSPtrVec mDetectedLowPriorityTxns {};
        // Cancelled txns.
        TxInputDataSPtrVec mCancelledTxns {};
        // Txns that need to be re-submitted.
        TxInputDataSPtrVec mResubmittedTxns {};
        // Txns that were detected as invalid
        // - we need to track a reason of failure as it might be used at the later stage
        InvalidTxnStateUMap mInvalidTxns {};
	};

  public:
    // Default run frequency in asynch mode
    static constexpr unsigned DEFAULT_ASYNCH_RUN_FREQUENCY_MILLIS {10};
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
        TxnDoubleSpendDetectorSPtr dsDetector,
        TxIdTrackerWPtr pTxIdTracker);
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

    /**
     * Synchronous txn validation interface.
     */
    /** Process a new txn with wait */
    CValidationState processValidation(
        const TxInputDataSPtr& txInputData,
        const mining::CJournalChangeSetPtr& changeSet,
        bool fLimitMempoolSize=false);
    /** Process a set of txns */
    CTxnValidator::RejectedTxns processValidation(
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

    /**
     * An interface to query validator's state.
     */
    /** Get number of transactions that are still unvalidated */
    QueueCounts GetTransactionsInQueueCounts() const;
    size_t GetTransactionsInQueueCount() const;

    /** Get memory usage for still unvalidated transactions */
    uint64_t GetStdQueueMemUsage() const { return mStdTxnsMemSize; }
    uint64_t GetNonStdQueueMemUsage() const { return mNonStdTxnsMemSize; }

    /**
     * An interface to facilitate Unit Tests.
     */
    /** Wait for the Validator until the predicate returns true (through asynch interface) */
    void waitUntil(std::function<bool(const QueueCounts&)> predicate, bool fCheckOrphanQueueEmpty=true);
    /** Wait for the Validator to process all queued txns (through asynch interface) */
    void waitForEmptyQueue(bool fCheckOrphanQueueEmpty=true);

    /** Check if the given txn is already queued for processing (or being processed)
     *  in asynch mode by the Validator. */
    bool isTxnKnown(const uint256& txid) const;

  private:
    /** Thread entry point for new transaction queue handling */
    void threadNewTxnHandler() noexcept;

    /** Execute txn validation for a single transaction */
    CTxnValResult executeTxnValidationNL(
        const TxInputDataSPtr& pTxInputData,
        CTxnHandlers& handlers,
        bool fLimitMempoolSize,
        bool fUseLimits);

    /** Process all newly arrived transactions. */
    CTxnValidator::CIntermediateResult processNewTransactionsNL(
        std::vector<TxInputDataSPtr>& txns,
        CTxnHandlers& handlers,
        bool fUseLimits,
        std::chrono::milliseconds maxasynctasksrunduration);

    /** Post validation step for txns before limit mempool size is done*/
    void postValidationStepsNL(
        const std::pair<CTxnValResult, CTask::Status>& result,
        CIntermediateResult& processedTxns);

    /** Post processing step for txns when limit mempool size is done */
    void postProcessingStepsNL(
        const std::vector<TxInputDataSPtr>& vAcceptedTxns,
        const std::vector<TxId>& vRemovedTxIds,
        CTxnHandlers& handlers);

    /** Schedule orphan p2p txns for retry into the result vector */
    size_t scheduleOrphanP2PTxnsForReprocessing(
        const TxInputDataSPtrVec& vCancelledTxns,
        TxInputDataSPtrVec& vNextProcessingQueue);

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
					return ptxInputData->GetTxnPtr()->GetId() == txid; });
	}

    /** Increase memory used counters for queued transactions */
    void incMemUsedNL(std::atomic<uint64_t>& mem, const TxInputDataSPtr& txn) {
        mem += txn->GetTxnPtr()->GetTotalSize();
    }
    /** Decrease memory used counters for queued transactions */
    void decMemUsedNL(std::atomic<uint64_t>& mem, const TxInputDataSPtr& txn) {
        auto txnSize { txn->GetTxnPtr()->GetTotalSize() };
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
    bool enqueueStdTxnNL(const TxInputDataSPtr& txn);
    /** Add a non-standard txn to the queue */
    bool enqueueNonStdTxnNL(const TxInputDataSPtr& txn);

    /** Add some txns (standard or non-standard) to the queue */
    template<typename Iterator, typename Callable>
    // NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward)
    void enqueueTxnsNL(Iterator begin, const Iterator& end, Callable&& func) {
        std::for_each(begin, end, func);
    }

    /** Eliminate elements, from src, that fulfill a certain criterion defined by func predicate. */
    template<typename T, typename Callable>
    // NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward)
    void eraseTxnIfNL(T& src, Callable&& func) {
        src.erase(
            std::remove_if(
                src.begin(),
                src.end(),
                func),
            src.end());
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
    std::deque<TxInputDataSPtr> mStdTxns {};
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
