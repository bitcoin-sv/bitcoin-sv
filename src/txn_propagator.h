// Copyright (c) 2018-2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include "txn_sending_details.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

/**
* A class for tracking new transactions that need propagating out to
* our peers.
*/
class CTxnPropagator final
{
  public:

    // Construction/destruction
    CTxnPropagator();
    ~CTxnPropagator();

    // Forbid copying/assignment
    CTxnPropagator(const CTxnPropagator&) = delete;
    CTxnPropagator(CTxnPropagator&&) = delete;
    CTxnPropagator& operator=(const CTxnPropagator&) = delete;
    CTxnPropagator& operator=(CTxnPropagator&&) = delete;

    /** Handle a new transaction */
    void newTransaction(const CTxnSendingDetails& txn);

    /** Remove some old transactions */
    void removeTransactions(const std::vector<CTransactionRef>& txns);

    /** Shutdown and clean up */
    void shutdown();

    /** Get/set the frequency we run */
    std::chrono::milliseconds getRunFrequency() const;
    void setRunFrequency(const std::chrono::milliseconds& freq);

    /** Get the number of queued new transactions awaiting processing */
    size_t getNewTxnQueueLength() const;

  private:

    /** Thread entry point for new transaction queue handling */
    void threadNewTxnHandler() noexcept;

    /** Process all newly arrived transactions */
    void processNewTransactions();


    /** List of new transactions that need processing */
    std::vector<CTxnSendingDetails> mNewTxns {};
    mutable std::mutex mNewTxnsMtx {};

    /** Our main thread */
    std::thread mNewTxnsThread {};
    std::condition_variable mNewTxnsCV {} ;

    /** Flag to indicate we are running */
    std::atomic<bool> mRunning {true};

    /** Frequency we run (defaults to 1 second) */
    static constexpr unsigned DEFAULT_RUN_FREQUENCY_MILLIS {250};
    std::chrono::milliseconds mRunFrequency {DEFAULT_RUN_FREQUENCY_MILLIS};

};


