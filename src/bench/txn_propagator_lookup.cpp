// Copyright (c) 2026 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "bench.h"
#include "chainparams.h"
#include "config.h"
#include "net/net.h"
#include "primitives/transaction.h"
#include "txn_propagator.h"
#include "txn_sending_details.h"
#include "util.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <memory>
#include <set>
#include <vector>

/**
 * Benchmark the purge of newly-confirmed transactions from the pending
 * announcement queues, as run on the block-connect path: a whole block's
 * worth of transactions is turned into a membership lookup, which is then
 * used to sweep the propagator's own queue (CTxnPropagator::
 * removeTransactions) and each peer's queue (CNode::RemoveTxnsFromInventory).
 *
 * Each benchmark runs at three block sizes so a change of lookup container
 * shows its scaling behaviour, not just its performance at one size: the
 * lookup construction scales with the block; the queues being swept do not.
 *
 * The queued announcements are all for transactions NOT in the block, so the
 * sweeps remove nothing and every iteration sees an identical queue, with no
 * setup inside the timed loops. Cost the sweeps consequently do not exercise:
 * compacting the queue and destroying removed entries, which is the same
 * whatever the lookup container. No peers are connected, so the per-node
 * purge fan-out inside removeTransactions is a no-op.
 */
namespace
{
    // Benchmarked block sizes; the largest approximates a 4GB block.
    constexpr size_t SMALL_BLOCK_TXNS { 100'000 };
    constexpr size_t MEDIUM_BLOCK_TXNS { 1'000'000 };
    constexpr size_t LARGE_BLOCK_TXNS { 10'000'000 };
    // Pending announcements probed against (but absent from) the blocks.
    constexpr size_t QUEUE_TXNS { 1'000 };

    // Both benchmarks need a live g_connman: CTxnPropagator's purge fans out
    // through it, and even a standalone CNode consults it on construction.
    void EnsureBenchConnman()
    {
        // CConnman's internals consult the chain parameters, which
        // benchmarks don't otherwise select.
        SelectParams(CBaseChainParams::MAIN);
        // Park the propagator's handler thread so it can't drain the queue
        // mid-iteration. Must be set before CConnman creates the propagator.
        gArgs.ForceSetArg("-txnpropagationfreq", "3600000");
        if(!g_connman)
        {
            g_connman = std::make_unique<CConnman>(GlobalConfig::GetConfig(),
                                                   0x1337, 0x1337,
                                                   std::chrono::milliseconds{0});
        }
    }

    CTransactionRef MakeBenchTxn(uint32_t n)
    {
        CMutableTransaction tx {};
        tx.nLockTime = n; // distinct per n, so every txid is distinct
        return MakeTransactionRef(std::move(tx));
    }

    // One shared pool of transactions; the per-size blocks are prefixes of
    // it, so the txid hashing cost is paid once for the largest size.
    const std::vector<CTransactionRef>& BenchTxnPool()
    {
        static const std::vector<CTransactionRef> pool { []
        {
            std::vector<CTransactionRef> v {};
            v.reserve(LARGE_BLOCK_TXNS);
            for(size_t i = 0; i < LARGE_BLOCK_TXNS; ++i)
            {
                v.push_back(MakeBenchTxn(i));
            }
            return v;
        }() };
        return pool;
    }

    // The block being connected: the first blockTxnCount pool transactions.
    std::vector<CTransactionRef> MakeBlockTxns(size_t blockTxnCount)
    {
        const std::vector<CTransactionRef>& pool { BenchTxnPool() };
        return {pool.begin(),
                pool.begin() + static_cast<std::ptrdiff_t>(blockTxnCount)};
    }

    // Announcements for transactions beyond every block's range.
    std::vector<CTxnSendingDetails> MakeQueuedTxns()
    {
        std::vector<CTxnSendingDetails> queued {};
        queued.reserve(QUEUE_TXNS);
        for(size_t i = 0; i < QUEUE_TXNS; ++i)
        {
            const CTransactionRef txn { MakeBenchTxn(LARGE_BLOCK_TXNS + i) };
            queued.emplace_back(CInv{MSG_TX, txn->GetId()}, txn);
        }
        return queued;
    }

    void PropagatorRemoveTransactions(benchmark::State& state,
                                      size_t blockTxnCount)
    {
        EnsureBenchConnman();
        const std::shared_ptr<CTxnPropagator>& propagator {
            g_connman->getTransactionPropagator() };

        // The three sizes share the propagator; only fill its queue once.
        if(propagator->getNewTxnQueueLength() == 0)
        {
            for(const CTxnSendingDetails& txn : MakeQueuedTxns())
            {
                propagator->newTransaction(txn);
            }
        }

        const std::vector<CTransactionRef> blockTxns {
            MakeBlockTxns(blockTxnCount) };

        while(state.KeepRunning())
        {
            propagator->removeTransactions(blockTxns);
        }

        assert(propagator->getNewTxnQueueLength() == QUEUE_TXNS);
    }

    void NodeRemoveTxnsFromInventory(benchmark::State& state,
                                     size_t blockTxnCount)
    {
        EnsureBenchConnman();

        CConnman::CAsyncTaskPool asyncTaskPool { GlobalConfig::GetConfig() };
        const CNodePtr node { CNode::Make(0, NODE_NETWORK, 0, INVALID_SOCKET,
                                          CAddress{}, 0u, 0u, asyncTaskPool,
                                          "", true) };
        node->fRelayTxes = true;
        node->AddTxnsToInventory(MakeQueuedTxns());

        // The lookup removeTransactions builds from the block.
        std::set<CInv> toRemove {};
        for(size_t i = 0; i < blockTxnCount; ++i)
        {
            toRemove.emplace(MSG_TX, BenchTxnPool()[i]->GetId());
        }

        while(state.KeepRunning())
        {
            node->RemoveTxnsFromInventory(toRemove);
        }
    }
}

static void TxnPropagatorRemoveTransactions100k(benchmark::State& state)
{
    PropagatorRemoveTransactions(state, SMALL_BLOCK_TXNS);
}
BENCHMARK(TxnPropagatorRemoveTransactions100k);

static void TxnPropagatorRemoveTransactions1M(benchmark::State& state)
{
    PropagatorRemoveTransactions(state, MEDIUM_BLOCK_TXNS);
}
BENCHMARK(TxnPropagatorRemoveTransactions1M);

static void TxnPropagatorRemoveTransactions10M(benchmark::State& state)
{
    PropagatorRemoveTransactions(state, LARGE_BLOCK_TXNS);
}
BENCHMARK(TxnPropagatorRemoveTransactions10M);

static void CNodeRemoveTxnsFromInventory100k(benchmark::State& state)
{
    NodeRemoveTxnsFromInventory(state, SMALL_BLOCK_TXNS);
}
BENCHMARK(CNodeRemoveTxnsFromInventory100k);

static void CNodeRemoveTxnsFromInventory1M(benchmark::State& state)
{
    NodeRemoveTxnsFromInventory(state, MEDIUM_BLOCK_TXNS);
}
BENCHMARK(CNodeRemoveTxnsFromInventory1M);

static void CNodeRemoveTxnsFromInventory10M(benchmark::State& state)
{
    NodeRemoveTxnsFromInventory(state, LARGE_BLOCK_TXNS);
}
BENCHMARK(CNodeRemoveTxnsFromInventory10M);
