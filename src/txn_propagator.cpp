// Copyright (c) 2018-2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "txn_propagator.h"
#include "net.h"
#include "util.h"

// When we get C++17 we should loose this redundant definition, until then it's required.
constexpr unsigned CTxnPropagator::DEFAULT_RUN_FREQUENCY_MILLIS;

/** Constructor */
CTxnPropagator::CTxnPropagator()
{
    // Configure our running frequency
    auto runFreq { gArgs.GetArg("-txnpropagationfreq", DEFAULT_RUN_FREQUENCY_MILLIS) };
    mRunFrequency = std::chrono::milliseconds {runFreq};

    // Launch our threads
    mNewTxnsThread = std::thread(&CTxnPropagator::threadNewTxnHandler, this);
}

/** Destructor */
CTxnPropagator::~CTxnPropagator()
{
    shutdown();
}

/** Get the frequency we run */
std::chrono::milliseconds CTxnPropagator::getRunFrequency() const
{
    std::unique_lock<std::mutex> lock { mNewTxnsMtx };
    return mRunFrequency;
}

/** Set the frequency we run */
void CTxnPropagator::setRunFrequency(const std::chrono::milliseconds& freq)
{
    std::unique_lock<std::mutex> lock { mNewTxnsMtx };
    mRunFrequency = freq;

    // Also wake up the processing thread so that it is then rescheduled at the right frequency
    mNewTxnsCV.notify_one();
}

/** Get the number of queued new transactions awaiting processing */
size_t CTxnPropagator::getNewTxnQueueLength() const
{
    std::unique_lock<std::mutex> lock { mNewTxnsMtx };
    return mNewTxns.size();
}

/** Handle a new transaction */
void CTxnPropagator::newTransaction(const CTxnSendingDetails& txn)
{
    // Add it to the list of new transactions
    std::unique_lock<std::mutex> lock { mNewTxnsMtx };
    mNewTxns.push_back(txn);
}

/** Remove some old transactions */
void CTxnPropagator::removeTransactions(const std::vector<CTransactionRef>& txns)
{
    LogPrint(BCLog::TXNPROP, "Purging %d transactions\n", txns.size());

    // Create list of CTxnSendingDetails as required to remove them from
    // a nodes list.
    std::vector<CTxnSendingDetails> txnDetails {};
    txnDetails.reserve(txns.size());
    for(const CTransactionRef& txn : txns)
    {   
        CInv inv { MSG_TX, txn->GetId() };
        txnDetails.emplace_back(inv, txn);
    }
   
    // Filter list of new transactions
    {
        std::unique_lock<std::mutex> lock { mNewTxnsMtx };

        for (const auto& el : txnDetails)
        {
            mNewTxns.erase(std::remove_if(mNewTxns.begin(), mNewTxns.end(), [&el](const CTxnSendingDetails& i) {
              return i.getInv() == el.getInv(); }), mNewTxns.end());
        }

    }

    // Update lists of pending transactions for each node
    auto results { g_connman->ParallelForEachNode([&txnDetails](const CNodePtr& node) { node->RemoveTxnsFromInventory(txnDetails); }) };

    // Wait for all nodes to finish since they depend on local variable txnDetails
    for(auto& result : results)
         result.wait();

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
    RenameThread("bitcoin-txnpropagator");
    try
    {
        LogPrint(BCLog::TXNPROP, "New transaction handling thread starting\n");

        while(mRunning)
        {
            // Run every few seconds or until stopping
            std::unique_lock<std::mutex> lock { mNewTxnsMtx };
            mNewTxnsCV.wait_for(lock, mRunFrequency);
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

    auto results { g_connman->ParallelForEachNode([this](const CNodePtr& node) { node->AddTxnsToInventory(mNewTxns); }) };

    // Wait for all nodes to finish before clearing mNewTxns
    for(auto& result : results)
        result.wait();

    // Clear new transactions list
    mNewTxns.clear();
}

