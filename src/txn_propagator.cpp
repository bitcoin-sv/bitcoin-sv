// Copyright (c) 2018-2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "txn_propagator.h"
#include "net/net.h"
#include "util.h"

#include <algorithm>
#include <unordered_set>

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

    // When nothing is queued for announcement anywhere - the norm during IBD,
    // there is nothing to purge, so don't pay for building it.
    if(getNewTxnQueueLength() == 0)
    {
        bool anyQueued{};
        g_connman->ForEachNode([&anyQueued](const CNodePtr& node)
        {
            anyQueued = anyQueued || node->HasTxnsInInventory();
        });
        if(!anyQueued)
            return;
    }

    const std::unordered_set<CInv, SaltedInvHasher> toRemove{[&txns]
    {
        std::unordered_set<CInv, SaltedInvHasher> s;
        s.reserve(txns.size());
        for(const CTransactionRef& txn : txns)
        {
            s.emplace(MSG_TX, txn->GetId());
        }
        return s;
    }()};

    // Filter list of new transactions
    {
        std::lock_guard lock{mNewTxnsMtx};

        mNewTxns.erase(
            std::remove_if(mNewTxns.begin(), mNewTxns.end(),
                [&toRemove](const CTxnSendingDetails& i) {
                    return toRemove.contains(i.getInv());
                }),
            mNewTxns.end());
    }

    // Update lists of pending transactions for each node
    const auto results { g_connman->ParallelForEachNode([&toRemove](const CNodePtr& node)
    {
        node->RemoveTxnsFromInventory(toRemove);
    })};

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
    RenameThread("txnpropagator");
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

