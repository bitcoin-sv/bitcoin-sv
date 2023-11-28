// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#ifndef BITCOIN_VALIDATIONINTERFACE_H
#define BITCOIN_VALIDATIONINTERFACE_H

#include "primitives/transaction.h" // CTransaction(Ref)
#include "txmempool.h"

#include <boost/signals2/signal.hpp>

#include <memory>
#include <string_view>

class CBlock;
class CBlockIndex;
struct CBlockLocator;
class CBlockIndex;
class CConnman;
class CReserveScript;
class CValidationInterface;
class CValidationState;
class uint256;

// These functions dispatch to one or all registered wallets

/** Unregister all wallets from core */
void UnregisterAllValidationInterfaces();

// NOLINTNEXTLINE(cppcoreguidelines-virtual-class-destructor)
class CValidationInterface {
public:
    // Register / unregister this wallet
    virtual void RegisterValidationInterface() = 0;
    virtual void UnregisterValidationInterface() = 0;

protected:
    virtual void UpdatedBlockTip(const CBlockIndex *pindexNew,
                                 const CBlockIndex *pindexFork,
                                 bool fInitialDownload) {}
    virtual void TransactionAddedToMempool(const CTransactionRef &ptxn) {}
    virtual void TransactionRemovedFromMempool(const uint256& txid,
                                               MemPoolRemovalReason reason,
                                               const CTransactionConflict& conflictedWith) {}
    virtual void TransactionRemovedFromMempoolBlock(const uint256& txid, MemPoolRemovalReason reason) {}
    virtual void TransactionAdded(const CTransactionRef& ptxn) {}
    virtual void BlockConnected(const std::shared_ptr<const CBlock> &block,
                   const CBlockIndex *pindex,
                   const std::vector<CTransactionRef> &txnConflicted) {}
    virtual void BlockConnected2(const CBlockIndex* pindex, const std::vector<CTransactionRef>& txnNew) {}
    virtual void BlockDisconnected(const std::shared_ptr<const CBlock> &block) {}
    virtual void SetBestChain(const CBlockLocator &locator) {}
    virtual void Inventory(const uint256 &hash) {}
    virtual void ResendWalletTransactions(int64_t nBestBlockTime, CConnman *connman) {}
    virtual void BlockChecked(const CBlock &, const CValidationState &) {}
    virtual void GetScriptForMining(std::shared_ptr<CReserveScript> &){};
    virtual void NewPoWValidBlock(const CBlockIndex *pindex, const std::shared_ptr<const CBlock> &block){};
    // This function is called only when there is an active ZMQ subscription of invalid transacion ("-zmqpubinvalidtx")
    virtual void InvalidTxMessageZMQ(std::string_view message) {};

    friend void ::UnregisterAllValidationInterfaces();
};

struct CMainSignals {
    /** Notifies listeners of updated block chain tip */
    boost::signals2::signal<void(const CBlockIndex *, const CBlockIndex *,
                                 bool fInitialDownload)>
        UpdatedBlockTip;
    /** Notifies listeners of a transaction having been added to mempool. */
    boost::signals2::signal<void(const CTransactionRef &)>
        TransactionAddedToMempool;
    /** Notifies listeners of a transaction having been removed from mempool. */
    boost::signals2::signal<void(const uint256 &, MemPoolRemovalReason reason, const CTransactionConflict& conflictedWith)>
        TransactionRemovedFromMempool;
    /**
     * Notifies listeners of a transaction having been removed from mempool.
     * Some events for removing transactions from mempool are more frequent such as transaction
     * being include in block hence the need for two different signals.
     */
    boost::signals2::signal<void(const uint256 &, MemPoolRemovalReason reason)>
        TransactionRemovedFromMempoolBlock;
    /**
     * Notifies listeners of a block being connected.
     * Provides a vector of transactions evicted from the mempool as a result.
     */
    boost::signals2::signal<void(const std::shared_ptr<const CBlock> &,
                                 const CBlockIndex *pindex,
                                 const std::vector<CTransactionRef> &)>
        BlockConnected;
    /**
     * Notifies listeners of a block being connected.
     * Provides a vector of transactions evicted from the mempool without those which were already in the mempool.
     */
    boost::signals2::signal<void(const CBlockIndex* pindex,
                                 const std::vector<CTransactionRef> &)>
        BlockConnected2;
    /** Notifies listeners of a block being disconnected */
    boost::signals2::signal<void(const std::shared_ptr<const CBlock> &)> BlockDisconnected;
    /** Notifies listeners of a new active block chain. */
    boost::signals2::signal<void(const CBlockLocator &)> SetBestChain;
    /** Notifies listeners about an inventory item being seen on the network. */
    boost::signals2::signal<void(const uint256 &)> Inventory;
    /** Tells listeners to broadcast their data. */
    boost::signals2::signal<void(int64_t nBestBlockTime, CConnman *connman)> Broadcast;
    /** Notifies listeners of a block validation result */
    boost::signals2::signal<void(const CBlock &, const CValidationState &)> BlockChecked;
    /** Notifies listeners that a key for mining is required (coinbase) */
    boost::signals2::signal<void(std::shared_ptr<CReserveScript> &)> ScriptForMining;
    /** Notifies listeners that a message part of the invalid transaction dump is ready to send */
    boost::signals2::signal<void(std::string_view)> InvalidTxMessageZMQ;

    /**
     * Notifies listeners that a block which builds directly on our current tip
     * has been received and connected to the headers tree, though not validated
     * yet.
     */
    boost::signals2::signal<void(const CBlockIndex *,
                                 const std::shared_ptr<const CBlock> &)>
        NewPoWValidBlock;
};

CMainSignals &GetMainSignals();

#endif // BITCOIN_VALIDATIONINTERFACE_H
