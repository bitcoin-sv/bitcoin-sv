// Copyright (c) 2018 The Bitcoin SV developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "txn_propagator.h"
#include "net.h"

#include <chrono>

namespace
{
    /** Frequency we check the new transaction queue (1 second) */
    constexpr std::chrono::milliseconds NewTxnPeriod { 1000 };
}

/** Constructor */
CTxnPropagator::CTxnPropagator()
{
    // Launch our threads
    mNewTxnsThread = std::thread(&CTxnPropagator::threadNewTxnHandler, this);
}

/** Destructor */
CTxnPropagator::~CTxnPropagator()
{
    shutdown();
}

/** Handle a new transaction */
void CTxnPropagator::newTransaction(const CTxnSendingDetails& txn)
{
    // Add it to the list of new transactions
    std::unique_lock<std::mutex> lock { mNewTxnsMtx };
    mNewTxns.push_back(txn);
}

/** Shutdown and clean up */
void CTxnPropagator::shutdown()
{
    // Only shutdown once
    bool expected {true};
    if(mRunning.compare_exchange_strong(expected, false))
    {
        // Shutdown threads
        {
            std::unique_lock<std::mutex> lock { mNewTxnsMtx };
            mNewTxnsCV.notify_one();
        }

        mNewTxnsThread.join();
    }
}

/** Thread entry point for new transaction queue handling */
void CTxnPropagator::threadNewTxnHandler() noexcept
{
    try
    {
        LogPrint(BCLog::TXNPROP, "New transaction handling thread starting\n");

        while(mRunning)
        {
            // Run every second or until stopping
            std::unique_lock<std::mutex> lock { mNewTxnsMtx };
            mNewTxnsCV.wait_for(lock, NewTxnPeriod, [this]{return !this->mRunning;});
            if(mRunning && !mNewTxns.empty())
            {
                // Process all new transactions
                LogPrint(BCLog::TXNPROP, "Got %d new transactions\n", mNewTxns.size());
                processNewTransactions();
            }
        }

        LogPrint(BCLog::TXNPROP, "New transaction handling thread stopping\n");
    }
    catch(...)
    {
        LogPrint(BCLog::TXNPROP, "Unexpected exception in new transaction thread\n");
    }
}

/**
* Process all new transactions.
* Already holds mNewTxnsMtx.
*/
void CTxnPropagator::processNewTransactions()
{
    // Take the mempool lock so we can do all the difficult txn sorting and node updating
    // in parallel.
    LOCK(mempool.cs);
    g_connman->ForEachNode([this](const CNodePtr& node) { node->AddTxnsToInventory(this->mNewTxns); });

    // Clear new transactions list
    mNewTxns.clear();
}

