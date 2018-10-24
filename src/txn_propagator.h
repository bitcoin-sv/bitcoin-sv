// Copyright (c) 2018 The Bitcoin SV developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include "txn_sending_details.h"

#include <atomic>
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

    /** Shutdown and clean up */
    void shutdown();

  private:

    /** Thread entry point for new transaction queue handling */
    void threadNewTxnHandler() noexcept;

    /** Process all newly arrived transactions */
    void processNewTransactions();


    /** List of new transactions that need processing */
    std::vector<CTxnSendingDetails> mNewTxns {};
    std::mutex mNewTxnsMtx {};

    /** Our main thread */
    std::thread mNewTxnsThread {};
    std::condition_variable mNewTxnsCV {} ;

    /** Flag to indicate we are running */
    std::atomic<bool> mRunning {true};

};


