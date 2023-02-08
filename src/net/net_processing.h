// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#ifndef BITCOIN_NET_PROCESSING_H
#define BITCOIN_NET_PROCESSING_H

#include "net.h"
#include "validationinterface.h"

class Config;

/** Max broadcast delay duration in milliseconds */
static const int64_t MAX_INV_BROADCAST_DELAY = 50 * 1000;
/** Default broadcast delay duration in milliseconds **/
static const int64_t DEFAULT_INV_BROADCAST_DELAY = 150;
/** Delay for not downloading blocks from a peer if it sends us REJECT_TOOBUSY message **/
static const int64_t TOOBUSY_RETRY_DELAY = 5000000;
/** Disable bloom filtering by default */
static const bool DEFAULT_PEERBLOOMFILTERS = false;

/** Register with a network node to receive its signals */
void RegisterNodeSignals(CNodeSignals &nodeSignals);
/** Unregister a network node */
void UnregisterNodeSignals(CNodeSignals &nodeSignals);
/** Set inventory broadcasting delay time in seconds*/
bool SetInvBroadcastDelay(const int64_t& nDelayMillisecs);

class PeerLogicValidation final : public CValidationInterface {
private:
    CConnman *connman;
    std::vector<boost::signals2::scoped_connection> slotConnections {};

public:
    PeerLogicValidation(CConnman *connmanIn);

    void RegisterValidationInterface() override;
    void UnregisterValidationInterface() override;

    void
    BlockConnected(const std::shared_ptr<const CBlock> &pblock,
                   const CBlockIndex *pindexConnected,
                   const std::vector<CTransactionRef> &vtxConflicted) override;
    void UpdatedBlockTip(const CBlockIndex *pindexNew,
                         const CBlockIndex *pindexFork,
                         bool fInitialDownload) override;
    void BlockChecked(const CBlock &block,
                      const CValidationState &state) override;
    void NewPoWValidBlock(const CBlockIndex *pindex,
                          const std::shared_ptr<const CBlock> &pblock) override;
};

struct CNodeStateStats {
    int nMisbehavior;
    int nSyncHeight;
    int nCommonHeight;
    std::vector<int> vHeightInFlight;
};

/** Check if inv already known (txn or block) */
bool AlreadyHave(const CInv &inv);
/** Check if txn is already known */
bool IsTxnKnown(const CInv &inv);
/** Check if block is already known */
bool IsBlockKnown(const CInv &inv);

/** Possibly ban a misbehaving peer */
void Misbehaving(NodeId pnode, int howmuch, const std::string& reason);

/** Relay transaction */
void RelayTransaction(const CTransaction &tx, CConnman &connman);

/** Get statistics from node state */
bool GetNodeStateStats(NodeId nodeid, CNodeStateStats &stats);
/** Increase a node's misbehavior score. */
void Misbehaving(NodeId nodeid, int howmuch, const std::string &reason);

/** Process protocol messages received from a given node */
bool ProcessMessages(const Config &config, const CNodePtr& pfrom, CConnman &connman,
                     const std::atomic<bool> &interrupt,
                     std::chrono::milliseconds debugP2PTheadStallsThreshold);
/**
 * Send queued protocol messages to be sent to a give node.
 *
 * @param[in]   pto             The node which we are sending messages to.
 * @param[in]   connman         The connection manager for that node.
 * @param[in]   interrupt       Interrupt condition for processing threads
 * @return                      True if there is more work to be done
 */
bool SendMessages(const Config &config, const CNodePtr& pto, CConnman &connman,
                  const std::atomic<bool> &interrupt);

#endif // BITCOIN_NET_PROCESSING_H
