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
    void newTransaction(std::vector<TxInputDataSPtr> pTxInputData);

    /**
     * Synchronous txn validation interface.
     */
    /** Process a new txn with wait */
    CValidationState processValidation(
        const TxInputDataSPtr& txInputData,
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

    /** Check if the given txn is already queued for processing (or being processed)
     *  in asynch mode by the Validator */
    bool isTxnKnown(const uint256& txid) const;

  private:
    /** Thread entry point for new transaction queue handling */
    void threadNewTxnHandler() noexcept;

    /** Process all newly arrived transactions. Return txns accepted by the mempool */
    std::vector<TxInputDataSPtr> processNewTransactionsNL(
        std::vector<TxInputDataSPtr>& txns,
        mining::CJournalChangeSetPtr& journalChangeSet);

    /** Post validation step for p2p txns before limit mempool size is done*/
    void postValidationP2PStepsNL(
        const CTxnValResult& txStatus,
        std::vector<TxInputDataSPtr>& vAcceptedTxns) const;

    /** Post processing step for p2p txns when limit mempool size is done */
    void postProcessingP2PStepsNL(
        const std::vector<TxInputDataSPtr>& vAcceptedTxns,
        const std::vector<TxId>& vRemovedTxIds);

    /** Schedule orphan p2p txns for retry into the main queue */
    size_t scheduleOrphanP2PTxnsForRetry();

    /** Check if the given txn is known in the given set of txns */
    bool isTxnKnownInSetNL(
        const uint256& txid,
        const std::vector<TxInputDataSPtr>& vTxns) const;

    /** Find if the given txn is known in the given set of txns */
    std::vector<TxInputDataSPtr>::const_iterator findIfTxnIsInSetNL(
        const uint256& txid,
        const std::vector<TxInputDataSPtr>& vTxns) const;

    /** List of new transactions that need processing */
    std::vector<TxInputDataSPtr> mNewTxns {};
    /** A dedicated mutex to protect an exclusive access to mNewTxns */
    mutable std::shared_mutex mNewTxnsMtx {};
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
