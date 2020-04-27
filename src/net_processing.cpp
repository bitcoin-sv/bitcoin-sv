// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2019-2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include <chrono>
#include <optional>
#include <shared_mutex>
#include "net_processing.h"

#include "addrman.h"
#include "arith_uint256.h"
#include "blockencodings.h"
#include "blockstreams.h"
#include "chainparams.h"
#include "clientversion.h"
#include "config.h"
#include "consensus/validation.h"
#include "hash.h"
#include "init.h"
#include "locked_ref.h"
#include "merkleblock.h"
#include "mining/journal_builder.h"
#include "net.h"
#include "netbase.h"
#include "netmessagemaker.h"
#include "policy/fees.h"
#include "policy/policy.h"
#include "primitives/block.h"
#include "primitives/transaction.h"
#include "random.h"
#include "taskcancellation.h"
#include "tinyformat.h"
#include "txmempool.h"
#include "ui_interface.h"
#include "util.h"
#include "utilmoneystr.h"
#include "utilstrencodings.h"
#include "validation.h"
#include "protocol.h"
#include "validationinterface.h"

#include <boost/range/adaptor/reversed.hpp>
#include <boost/thread.hpp>

#include "blockfileinfostore.h"

#if defined(NDEBUG)
#error "Bitcoin cannot be compiled without assertions."
#endif

using namespace std;
using namespace mining;

// Used only to inform the wallet of when we last received a block.
std::atomic<int64_t> nTimeBestReceived(0);

// SHA256("main address relay")[0:8]
static const uint64_t RANDOMIZER_ID_ADDRESS_RELAY = 0x3cac0035b5866b90ULL;

// Internal stuff
namespace {
/** Number of nodes with fSyncStarted. */
std::atomic<int> nSyncStarted = 0;

/**
 * Sources of received blocks, saved to be able to send them reject messages or
 * ban them when processing happens afterwards. Protected by cs_main.
 * Set mapBlockSource[hash].second to false if the node should not be punished
 * if the block is invalid.
 */
std::map<uint256, std::pair<NodeId, bool>> mapBlockSource;

uint256 hashRecentRejectsChainTip;

/**
 * Blocks that are in flight, and that are in the queue to be downloaded.
 * Protected by cs_main.
 */
struct QueuedBlock {
    uint256 hash;
    const CBlockIndex& blockIndex;
    //!< Whether this block has validated headers at the time of request.
    bool fValidatedHeaders;
    //!< Optional, used for CMPCTBLOCK downloads
    std::unique_ptr<PartiallyDownloadedBlock> partialBlock;
};
std::map<uint256, std::pair<NodeId, std::list<QueuedBlock>::iterator>>
    mapBlocksInFlight;

/** Stack of nodes which we have set to announce using compact blocks */
std::list<NodeId> lNodesAnnouncingHeaderAndIDs;

/** Number of preferable block download peers. */
std::atomic<int> nPreferredDownload = 0;

/** Number of peers from which we're downloading blocks. */
std::atomic<int> nPeersWithValidatedDownloads = 0;

/** Relay map, protected by cs_main. */
typedef std::map<uint256, CTransactionRef> MapRelay;
MapRelay mapRelay;
/** Expiration-time ordered list of (expire time, relay map entry) pairs,
 * protected by cs_main). */
std::deque<std::pair<int64_t, MapRelay::iterator>> vRelayExpiration;
} // namespace

//////////////////////////////////////////////////////////////////////////////
//
// Registration of network node signals.
//

namespace {

struct CBlockReject {
    uint8_t chRejectCode;
    std::string strRejectReason;
    uint256 hashBlock;
};

/**
 * Maintain validation-specific state about nodes, protected by cs_main, instead
 * by CNode's own locks. This simplifies asynchronous operation, where
 * processing of incoming data is done after the ProcessMessage call returns,
 * and we're no longer holding the node's locks.
 */
struct CNodeState {
    //! The peer's address
    const CService address {};
    //! Whether we have a fully established connection.
    bool fCurrentlyConnected {false};
    //! Accumulated misbehaviour score for this peer.
    int nMisbehavior {0};
    //! Whether this peer should be disconnected and banned (unless
    //! whitelisted).
    bool fShouldBan {false};
    //! String name of this peer (debugging/logging purposes).
    const std::string name {};
    //! List of asynchronously-determined block rejections to notify this peer
    //! about.
    std::vector<CBlockReject> rejects {};
    //! The best known block we know this peer has announced.
    const CBlockIndex* pindexBestKnownBlock {nullptr};
    //! The hash of the last unknown block this peer has announced.
    uint256 hashLastUnknownBlock {};
    //! The last full block we both have.
    const CBlockIndex* pindexLastCommonBlock {nullptr};
    //! The best header we have sent our peer.
    const CBlockIndex* pindexBestHeaderSent {nullptr};
    //! Length of current-streak of unconnecting headers announcements
    int nUnconnectingHeaders {0};
    //! Whether we've started headers synchronization with this peer.
    bool fSyncStarted {false};
    //! Since when we're stalling block download progress (in microseconds), or
    //! 0.
    int64_t nStallingSince {0};
    std::list<QueuedBlock> vBlocksInFlight {};
    //! When the first entry in vBlocksInFlight started downloading. Don't care
    //! when vBlocksInFlight is empty.
    int64_t nDownloadingSince {0};
    int nBlocksInFlight {0};
    int nBlocksInFlightValidHeaders {0};
    //! Whether we consider this a preferred download peer.
    bool fPreferredDownload {false};
    //! Whether this peer wants invs or headers (when possible) for block
    //! announcements.
    bool fPreferHeaders {false};
    //! Whether this peer wants invs or cmpctblocks (when possible) for block
    //! announcements.
    bool fPreferHeaderAndIDs {false};
    /**
     * Whether this peer will send us cmpctblocks if we request them.
     * This is not used to gate request logic, as we really only care about
     * fSupportsDesiredCmpctVersion, but is used as a flag to "lock in" the
     * version of compact blocks we send.
     */
    bool fProvidesHeaderAndIDs {false};
    /**
     * If we've announced NODE_WITNESS to this peer: whether the peer sends
     * witnesses in cmpctblocks/blocktxns, otherwise: whether this peer sends
     * non-witnesses in cmpctblocks/blocktxns.
     */
    bool fSupportsDesiredCmpctVersion {false};

    /*
    * Capture the number and frequency of Invalid checksum
    */
    double dInvalidChecksumFrequency {0};
    std::chrono::system_clock::time_point nTimeOfLastInvalidChecksumHeader { std::chrono::system_clock::now() };

    int64_t nextSendThresholdTime {0};

    /**
    * A mutex for locking these details. Needs (at least for now) to be
    * recursive because of all the legacy structure.
    */
    std::recursive_mutex mMtx {};

    CNodeState(CAddress addrIn, std::string addrNameIn)
        : address(addrIn), name(addrNameIn) {
        hashLastUnknownBlock.SetNull();
    }

    bool CanSend() const {
        return nextSendThresholdTime < GetTimeMicros();
    }
};

using CNodeStatePtr = std::shared_ptr<CNodeState>;
using CNodeStateRef = CLockedRef<CNodeStatePtr, std::unique_lock<std::recursive_mutex>>;

/** Map maintaining per-node state. */
std::map<NodeId, CNodeStatePtr> mapNodeState;
std::shared_mutex mapNodeStateMtx {};

// Fetch node state
CNodeStateRef GetState(NodeId pnode) {
    // Lock access for reading to the map of node states
    std::shared_lock<std::shared_mutex> lock { mapNodeStateMtx };

    auto it { mapNodeState.find(pnode) };
    if(it == mapNodeState.end()) {
        // Not found, return null
        return {};
    }
    // Return a shared ref to the item in the map, locked appropriately
    return { it->second, it->second->mMtx };
}

void UpdatePreferredDownload(const CNodePtr& pnode) {
    // Try to obtain an access to the node's state data.
    const CNodeStateRef stateRef { GetState(pnode->GetId()) };
    const CNodeStatePtr& state { stateRef.get() };
    if (!state) {
        return;
    }
    nPreferredDownload -= state->fPreferredDownload;
    // Whether this node should be marked as a preferred download node.
    state->fPreferredDownload = (!pnode->fInbound || pnode->fWhitelisted) &&
                                       !pnode->fOneShot &&
                                       !pnode->fClient;
    nPreferredDownload += state->fPreferredDownload;
}

void PushNodeVersion(const CNodePtr& pnode, CConnman &connman,
                     int64_t nTime) {
    ServiceFlags nLocalNodeServices = pnode->GetLocalServices();
    uint64_t nonce = pnode->GetLocalNonce();
    int nNodeStartingHeight = pnode->GetMyStartingHeight();
    NodeId nodeid = pnode->GetId();
    CAddress addr = pnode->addr;

    CAddress addrYou = (addr.IsRoutable() && !IsProxy(addr)
                            ? addr
                            : CAddress(CService(), addr.nServices));
    CAddress addrMe = CAddress(CService(), nLocalNodeServices);

    connman.PushMessage(pnode,
                        CNetMsgMaker(INIT_PROTO_VERSION)
                            .Make(NetMsgType::VERSION, PROTOCOL_VERSION,
                                  (uint64_t)nLocalNodeServices, nTime, addrYou,
                                  addrMe, nonce, userAgent(),
                                  nNodeStartingHeight, ::fRelayTxes));

    if (fLogIPs) {
        LogPrint(BCLog::NET, "send version message: version %d, blocks=%d, "
                             "us=%s, them=%s, peer=%d\n",
                 PROTOCOL_VERSION, nNodeStartingHeight, addrMe.ToString(),
                 addrYou.ToString(), nodeid);
    } else {
        LogPrint(
            BCLog::NET,
            "send version message: version %d, blocks=%d, us=%s, peer=%d\n",
            PROTOCOL_VERSION, nNodeStartingHeight, addrMe.ToString(), nodeid);
    }
}

void PushProtoconf(const CNodePtr& pnode, CConnman &connman) {
    connman.PushMessage(
            pnode, CNetMsgMaker(INIT_PROTO_VERSION).Make(NetMsgType::PROTOCONF, CProtoconf(MAX_PROTOCOL_RECV_PAYLOAD_LENGTH)));

    LogPrint(BCLog::NET, "send protoconf message: max size %d, number of fields =%d, ", MAX_PROTOCOL_RECV_PAYLOAD_LENGTH, 1);
}


void InitializeNode(const CNodePtr& pnode, CConnman &connman) {
    CAddress addr = pnode->addr;
    std::string addrName = pnode->GetAddrName();
    NodeId nodeid = pnode->GetId();
    {
        std::unique_lock<std::shared_mutex> lock { mapNodeStateMtx };
        mapNodeState.emplace_hint(
            mapNodeState.end(), std::piecewise_construct,
            std::forward_as_tuple(nodeid),
            std::forward_as_tuple(std::make_shared<CNodeState>(addr, std::move(addrName))));
    }

    if (!pnode->fInbound) {
        PushNodeVersion(pnode, connman, GetTime());
    }
}

void FinalizeNode(NodeId nodeid, bool &fUpdateConnectionTime) {

    fUpdateConnectionTime = false;
    LOCK(cs_main);
    {
        // Try to obtain an access to the node's state data.
        const CNodeStateRef stateRef { GetState(nodeid) };
        const CNodeStatePtr& state { stateRef.get() };
        assert(state);

        if (state->fSyncStarted) {
            nSyncStarted--;
        }

        if (state->nMisbehavior == 0 && state->fCurrentlyConnected) {
            fUpdateConnectionTime = true;
        }

        for (const QueuedBlock &entry : state->vBlocksInFlight) {
            mapBlocksInFlight.erase(entry.hash);
        }
        // Get rid of stale mapBlockSource entries for this peer as they may leak
        // if we don't clean them up (I saw on the order of ~100 stale entries on
        // a full resynch in my testing -- these entries stay forever).
        // Performance note: most of the time mapBlockSource has 0 or 1 entries.
        // During synch of blockchain it may end up with as many as 1000 entries,
        // which still only takes ~1ms to iterate through on even old hardware.
        // So this memleak cleanup is not expensive and worth doing since even
        // small leaks are bad. :)
        for (auto it = mapBlockSource.begin(); it != mapBlockSource.end(); /*NA*/) {
            if (it->second.first == nodeid) {
                mapBlockSource.erase(it++);
            } else {
                ++it;
            }
        }
        // Erase orphan txns received from the given nodeId
        g_connman->EraseOrphanTxnsFromPeer(nodeid);
        nPreferredDownload -= state->fPreferredDownload;
        nPeersWithValidatedDownloads -= (state->nBlocksInFlightValidHeaders != 0);
        assert(nPeersWithValidatedDownloads >= 0);
    }
    // Modify mapNodeState in an exclusive mode.
    {
        std::unique_lock<std::shared_mutex> lock { mapNodeStateMtx };
        mapNodeState.erase(nodeid);
        if (mapNodeState.empty()) {
            // Do a consistency check after the last peer is removed.
            assert(mapBlocksInFlight.empty());
            assert(nPreferredDownload == 0);
            assert(nPeersWithValidatedDownloads == 0);
        }
    }
}

// Requires cs_main.
// Returns a bool indicating whether we requested this block.
// Also used if a block was /not/ received and timed out or started with another
// peer.
bool MarkBlockAsReceived(const uint256 &hash) {

    AssertLockHeld(cs_main);
    std::map<uint256,
             std::pair<NodeId, std::list<QueuedBlock>::iterator>>::iterator
        itInFlight = mapBlocksInFlight.find(hash);
    if (itInFlight != mapBlocksInFlight.end()) {
        // Try to obtain an access to the node's state data.
        const CNodeStateRef stateRef { GetState(itInFlight->second.first) };
        const CNodeStatePtr& state { stateRef.get() };
        assert(state);
        state->nBlocksInFlightValidHeaders -=
            itInFlight->second.second->fValidatedHeaders;
        if (state->nBlocksInFlightValidHeaders == 0 &&
            itInFlight->second.second->fValidatedHeaders) {
            // Last validated block on the queue was received.
            nPeersWithValidatedDownloads--;
        }
        if (state->vBlocksInFlight.begin() == itInFlight->second.second) {
            // First block on the queue was received, update the start download
            // time for the next one
            state->nDownloadingSince =
                std::max(state->nDownloadingSince, GetTimeMicros());
        }
        state->vBlocksInFlight.erase(itInFlight->second.second);
        state->nBlocksInFlight--;
        state->nStallingSince = 0;
        mapBlocksInFlight.erase(itInFlight);
        return true;
    }

    return false;
}

// Requires cs_main.
// returns false, still setting pit, if the block was already in flight from the
// same peer pit will only be valid as long as the same cs_main lock is being
// held.
static bool
MarkBlockAsInFlight(const Config &config, NodeId nodeid, const uint256 &hash,
                    const Consensus::Params &consensusParams,
					const CNodeStatePtr& state,
                    const CBlockIndex& pindex,
                    std::list<QueuedBlock>::iterator **pit = nullptr) {

    AssertLockHeld(cs_main);
    // Short-circuit most stuff in case its from the same node.
    std::map<uint256,
             std::pair<NodeId, std::list<QueuedBlock>::iterator>>::iterator
        itInFlight = mapBlocksInFlight.find(hash);
    if (itInFlight != mapBlocksInFlight.end() &&
        itInFlight->second.first == nodeid) {
        if (pit){
            *pit = &itInFlight->second.second;
        }
        return false;
    }

    // Make sure it's not listed somewhere already.
    MarkBlockAsReceived(hash);

    assert(state);
    std::list<QueuedBlock>::iterator it = state->vBlocksInFlight.insert(
        state->vBlocksInFlight.end(),
        {hash, pindex, true,
         std::unique_ptr<PartiallyDownloadedBlock>(
             pit ? new PartiallyDownloadedBlock(config, &mempool) : nullptr)});
    state->nBlocksInFlight++;
    state->nBlocksInFlightValidHeaders += it->fValidatedHeaders;
    if (state->nBlocksInFlight == 1) {
        // We're starting a block download (batch) from this peer.
        state->nDownloadingSince = GetTimeMicros();
    }

    if (state->nBlocksInFlightValidHeaders == 1) {
        nPeersWithValidatedDownloads++;
    }

    itInFlight = mapBlocksInFlight
                     .insert(std::make_pair(hash, std::make_pair(nodeid, it)))
                     .first;

    if (pit) {
        *pit = &itInFlight->second.second;
    }

    return true;
}

/** Check whether the last unknown block a peer advertised is not yet known. */
void ProcessBlockAvailability(const CNodeStatePtr& state) {

    AssertLockHeld(cs_main);
    assert(state);

    if (!state->hashLastUnknownBlock.IsNull()) {
        BlockMap::iterator itOld =
            mapBlockIndex.find(state->hashLastUnknownBlock);
        if (itOld != mapBlockIndex.end() && itOld->second->nChainWork > 0) {
            if (state->pindexBestKnownBlock == nullptr ||
                itOld->second->nChainWork >=
                    state->pindexBestKnownBlock->nChainWork) {
                state->pindexBestKnownBlock = itOld->second;
            }
            state->hashLastUnknownBlock.SetNull();
        }
    }
}

/** Update tracking information about which blocks a peer is assumed to have. */
void UpdateBlockAvailability(const uint256 &hash, const CNodeStatePtr& state) {

    AssertLockHeld(cs_main);
    assert(state);

    ProcessBlockAvailability(state);
    BlockMap::iterator it = mapBlockIndex.find(hash);
    if (it != mapBlockIndex.end() && it->second->nChainWork > 0) {
        // An actually better block was announced.
        if (state->pindexBestKnownBlock == nullptr ||
            it->second->nChainWork >= state->pindexBestKnownBlock->nChainWork) {
            state->pindexBestKnownBlock = it->second;
        }
    } else {
        // An unknown block was announced; just assume that the latest one is
        // the best one.
        state->hashLastUnknownBlock = hash;
    }
}

void MaybeSetPeerAsAnnouncingHeaderAndIDs(NodeId nodeid, CConnman &connman) {

    AssertLockHeld(cs_main);
    {
        // Try to obtain an access to the node's state data.
        const CNodeStateRef nodestateRef { GetState(nodeid) };
        const CNodeStatePtr& nodestate { nodestateRef.get() };
        if (!nodestate) {
            LogPrint(BCLog::NET, "node state unavailable: peer=%d\n", nodeid);
            return;
        }
        if (!nodestate->fProvidesHeaderAndIDs) {
            return;
        }
    }
    for (std::list<NodeId>::iterator it = lNodesAnnouncingHeaderAndIDs.begin();
         it != lNodesAnnouncingHeaderAndIDs.end(); it++) {
        if (*it == nodeid) {
            lNodesAnnouncingHeaderAndIDs.erase(it);
            lNodesAnnouncingHeaderAndIDs.push_back(nodeid);
            return;
        }
    }
    connman.ForNode(nodeid, [&connman](const CNodePtr& pfrom) {
        bool fAnnounceUsingCMPCTBLOCK = false;
        uint64_t nCMPCTBLOCKVersion = 1;
        if (lNodesAnnouncingHeaderAndIDs.size() >= 3) {
            // As per BIP152, we only get 3 of our peers to announce
            // blocks using compact encodings.
            connman.ForNode(lNodesAnnouncingHeaderAndIDs.front(),
                            [&connman, fAnnounceUsingCMPCTBLOCK,
                             nCMPCTBLOCKVersion](const CNodePtr& pnodeStop) {
                                connman.PushMessage(
                                    pnodeStop,
                                    CNetMsgMaker(pnodeStop->GetSendVersion())
                                        .Make(NetMsgType::SENDCMPCT,
                                              fAnnounceUsingCMPCTBLOCK,
                                              nCMPCTBLOCKVersion));
                                return true;
                            });
            lNodesAnnouncingHeaderAndIDs.pop_front();
        }
        fAnnounceUsingCMPCTBLOCK = true;
        connman.PushMessage(pfrom,
                            CNetMsgMaker(pfrom->GetSendVersion())
                                .Make(NetMsgType::SENDCMPCT,
                                      fAnnounceUsingCMPCTBLOCK,
                                      nCMPCTBLOCKVersion));
        lNodesAnnouncingHeaderAndIDs.push_back(pfrom->GetId());
        return true;
    });
}

// Requires cs_main
bool CanDirectFetch(const Consensus::Params &consensusParams) {
    AssertLockHeld(cs_main);
    return chainActive.Tip()->GetBlockTime() >
           GetAdjustedTime() - consensusParams.nPowTargetSpacing * 20;
}

// Requires cs_main
bool PeerHasHeader(const CNodeStatePtr& state, const CBlockIndex *pindex) {
    AssertLockHeld(cs_main);

    if (!pindex) {
        return false;
    }
    else if (state->pindexBestKnownBlock &&
        pindex == state->pindexBestKnownBlock->GetAncestor(pindex->nHeight)) {
        return true;
    }
    else if (state->pindexBestHeaderSent &&
        pindex == state->pindexBestHeaderSent->GetAncestor(pindex->nHeight)) {
        return true;
    }
    return false;
}

/**
 * Update pindexLastCommonBlock and add not-in-flight missing successors to
 * vBlocks, until it has at most count entries.
 */
void FindNextBlocksToDownload(NodeId nodeid, unsigned int count,
                              std::vector<const CBlockIndex*>& vBlocks,
                              NodeId &nodeStaller,
                              const Consensus::Params& consensusParams,
                              const CNodeStatePtr& state) {
    if (count == 0) {
        return;
    }

    vBlocks.reserve(vBlocks.size() + count);
    assert(state);

    // Make sure pindexBestKnownBlock is up to date, we'll need it.
    ProcessBlockAvailability(state);

    if (state->pindexBestKnownBlock == nullptr ||
        state->pindexBestKnownBlock->nChainWork <
            chainActive.Tip()->nChainWork ||
        state->pindexBestKnownBlock->nChainWork < nMinimumChainWork) {
        // This peer has nothing interesting.
        return;
    }

    if (state->pindexLastCommonBlock == nullptr) {
        // Bootstrap quickly by guessing a parent of our best tip is the forking
        // point. Guessing wrong in either direction is not a problem.
        state->pindexLastCommonBlock = chainActive[std::min(
            state->pindexBestKnownBlock->nHeight, chainActive.Height())];
    }

    // If the peer reorganized, our previous pindexLastCommonBlock may not be an
    // ancestor of its current tip anymore. Go back enough to fix that.
    state->pindexLastCommonBlock = LastCommonAncestor(
        state->pindexLastCommonBlock, state->pindexBestKnownBlock);
    if (state->pindexLastCommonBlock == state->pindexBestKnownBlock) {
        return;
    }

    std::vector<const CBlockIndex *> vToFetch;
    const CBlockIndex *pindexWalk = state->pindexLastCommonBlock;
    // Never fetch further than the best block we know the peer has, or more
    // than BLOCK_DOWNLOAD_WINDOW + 1 beyond the last linked block we have in
    // common with this peer. The +1 is so we can detect stalling, namely if we
    // would be able to download that next block if the window were 1 larger.
    int64_t nWindowSize { gArgs.GetArg("-blockdownloadwindow", DEFAULT_BLOCK_DOWNLOAD_WINDOW) };
    if(nWindowSize <= 0) {
        nWindowSize = DEFAULT_BLOCK_DOWNLOAD_WINDOW;
    }
    int nWindowEnd = state->pindexLastCommonBlock->nHeight + nWindowSize;
    int nMaxHeight = std::min<int>(state->pindexBestKnownBlock->nHeight, nWindowEnd + 1);
    NodeId waitingfor = -1;
    while (pindexWalk->nHeight < nMaxHeight) {
        // Read up to 128 (or more, if more blocks than that are needed)
        // successors of pindexWalk (towards pindexBestKnownBlock) into
        // vToFetch. We fetch 128, because CBlockIndex::GetAncestor may be as
        // expensive as iterating over ~100 CBlockIndex* entries anyway.
        int nToFetch = std::min(nMaxHeight - pindexWalk->nHeight,
                                std::max<int>(count - vBlocks.size(), 128));
        vToFetch.resize(nToFetch);
        pindexWalk = state->pindexBestKnownBlock->GetAncestor(
            pindexWalk->nHeight + nToFetch);
        vToFetch[nToFetch - 1] = pindexWalk;
        for (unsigned int i = nToFetch - 1; i > 0; i--) {
            vToFetch[i - 1] = vToFetch[i]->pprev;
        }

        // Iterate over those blocks in vToFetch (in forward direction), adding
        // the ones that are not yet downloaded and not in flight to vBlocks. In
        // the mean time, update pindexLastCommonBlock as long as all ancestors
        // are already downloaded, or if it's already part of our chain (and
        // therefore don't need it even if pruned).
        for (const CBlockIndex *pindex : vToFetch) {
            if (!pindex->IsValid(BlockValidity::TREE)) {
                // We consider the chain that this peer is on invalid.
                return;
            }
            if (pindex->nStatus.hasData() || chainActive.Contains(pindex)) {
                if (pindex->nChainTx) {
                    state->pindexLastCommonBlock = pindex;
                }
            } else if (mapBlocksInFlight.count(pindex->GetBlockHash()) == 0) {
                // The block is not already downloaded, and not yet in flight.
                if (pindex->nHeight > nWindowEnd) {
                    // We reached the end of the window.
                    if (vBlocks.size() == 0 && waitingfor != nodeid) {
                        // We aren't able to fetch anything, but we would be if
                        // the download window was one larger.
                        nodeStaller = waitingfor;
                    }
                    return;
                }
                vBlocks.push_back(pindex);
                if (vBlocks.size() == count) {
                    return;
                }
            } else if (waitingfor == -1) {
                // This is the first already-in-flight block.
                waitingfor = mapBlocksInFlight[pindex->GetBlockHash()].first;
            }
        }
    }
}

/**
* Utility method to calculate the maximum number of items in an inventory message.
*/
inline unsigned int GetInventoryBroadcastMax(const Config& config)
{
    return INVENTORY_BROADCAST_MAX_PER_MB * (config.GetMaxBlockSize() / ONE_MEGABYTE);
}

} // namespace

// Forward declarion of ProcessMessage
static bool ProcessMessage(const Config& config, const CNodePtr& pfrom, const std::string& strCommand,
    CDataStream& vRecv, int64_t nTimeReceived, const CChainParams& chainparams, CConnman& connman,
    const std::atomic<bool>& interruptMsgProc);

bool GetNodeStateStats(NodeId nodeid, CNodeStateStats &stats) {
    // Try to obtain an access to the node's state data.
    const CNodeStateRef stateRef { GetState(nodeid) };
    const CNodeStatePtr& state { stateRef.get() };
    if (!state) {
        return false;
    }
    stats.nMisbehavior = state->nMisbehavior;
    stats.nSyncHeight =
        state->pindexBestKnownBlock ? state->pindexBestKnownBlock->nHeight : -1;
    stats.nCommonHeight = state->pindexLastCommonBlock
                              ? state->pindexLastCommonBlock->nHeight
                              : -1;
    for (const QueuedBlock &queue : state->vBlocksInFlight) {
        stats.vHeightInFlight.push_back(queue.blockIndex.nHeight);
    }
    return true;
}

void RegisterNodeSignals(CNodeSignals &nodeSignals) {
    nodeSignals.ProcessMessages.connect(&ProcessMessages);
    nodeSignals.SendMessages.connect(&SendMessages);
    nodeSignals.InitializeNode.connect(&InitializeNode);
    nodeSignals.FinalizeNode.connect(&FinalizeNode);
}

void UnregisterNodeSignals(CNodeSignals &nodeSignals) {
    nodeSignals.ProcessMessages.disconnect(&ProcessMessages);
    nodeSignals.SendMessages.disconnect(&SendMessages);
    nodeSignals.InitializeNode.disconnect(&InitializeNode);
    nodeSignals.FinalizeNode.disconnect(&FinalizeNode);
}

static int64_t Fixed_delay_microsecs = DEFAULT_INV_BROADCAST_DELAY * 1000;
bool SetInvBroadcastDelay(const int64_t& nDelayMillisecs) {
    if ( nDelayMillisecs < 0 || nDelayMillisecs > MAX_INV_BROADCAST_DELAY)
        return false;
    Fixed_delay_microsecs=1000*nDelayMillisecs;
    return true;
}

void Misbehaving(NodeId pnode, int howmuch, const std::string &reason) {
    if (howmuch == 0) {
        return;
    }
    // Try to obtain an access to the node's state data.
    const CNodeStateRef stateRef { GetState(pnode) };
    const CNodeStatePtr& state { stateRef.get() };
    if (!state) {
        return;
    }
    state->nMisbehavior += howmuch;
    int banscore = gArgs.GetArg("-banscore", DEFAULT_BANSCORE_THRESHOLD);
    if (state->nMisbehavior >= banscore &&
        state->nMisbehavior - howmuch < banscore) {
        LogPrintf(
            "%s: %s peer=%d (%d -> %d) reason: %s BAN THRESHOLD EXCEEDED\n",
            __func__, state->name, pnode, state->nMisbehavior - howmuch,
            state->nMisbehavior, reason.c_str());
        state->fShouldBan = true;
    } else {
        LogPrintf("%s: %s peer=%d (%d -> %d) reason: %s\n", __func__,
                  state->name, pnode, state->nMisbehavior - howmuch,
                  state->nMisbehavior, reason.c_str());
    }
}

// overloaded variant of above to operate on CNodePtrs
static void Misbehaving(const CNodePtr& node, int howmuch, const std::string &reason) {
    Misbehaving(node->GetId(), howmuch, reason);
}

//////////////////////////////////////////////////////////////////////////////
//
// blockchain -> download logic notification
//

PeerLogicValidation::PeerLogicValidation(CConnman *connmanIn)
    : connman(connmanIn)
{}

void PeerLogicValidation::BlockConnected(
    const std::shared_ptr<const CBlock> &pblock, const CBlockIndex *pindex,
    const std::vector<CTransactionRef> &vtxConflicted) {
    LOCK(cs_main);
    std::vector<uint256> vOrphanErase {};
    for (const CTransactionRef &ptx : pblock->vtx) {
        const CTransaction &tx = *ptx;
        // Which orphan pool entries must we evict?
        for (size_t j = 0; j < tx.vin.size(); j++) {
            auto vOrphanTxns = g_connman->GetOrphanTxnsHash(tx.vin[j].prevout);
            if (vOrphanTxns.empty()) {
                continue;
            } else {
                vOrphanErase.insert(
                        vOrphanErase.end(),
                        std::make_move_iterator(vOrphanTxns.begin()),
                        std::make_move_iterator(vOrphanTxns.end()));
            }
        }
    }
    // Erase orphan transactions include or precluded by this block
    if (vOrphanErase.size()) {
        int nErased = 0;
        for (uint256 &orphanId : vOrphanErase) {
            nErased += g_connman->EraseOrphanTxn(orphanId);
        }
        LogPrint(BCLog::MEMPOOL,
                 "Erased %d orphan txns included or conflicted by block\n",
                 nErased);
    }
}

namespace
{
    class CMostRecentBlockCache
    {
    public:
        struct CCompactBlockMessageData
        {
            CCompactBlockMessageData(
                std::shared_ptr<const std::vector<uint8_t>> inData)
                : data{inData}
                , hash{::Hash(data->data(), data->data() + data->size())}
                , size{data->size()}
            {/**/}

            CCompactBlockMessageData(
                std::shared_ptr<const std::vector<uint8_t>> inData,
                uint256 inHash,
                size_t inSize)
                : data{inData}
                , hash{inHash}
                , size{inSize}
            {/**/}

            CSerializedNetMsg CreateCompactBlockMessage() const
            {
                return
                    {
                        NetMsgType::CMPCTBLOCK,
                        hash,
                        size,
                        std::make_unique<CSharedVectorStream>(data)
                    };
            }

            const std::shared_ptr<const std::vector<uint8_t>> data;
            const uint256 hash;
            const size_t size;
        };

        void SetBlock(
            std::shared_ptr<const CBlock> block,
            const CBlockIndex& index)
        {
            assert(block);

            std::unique_lock lock{mMutex};

            mBlock = std::move(block);
            auto serializedData = std::make_shared<std::vector<uint8_t>>();
            // serialize compact block data
            CVectorWriter{
                SER_NETWORK,
                PROTOCOL_VERSION,
                *serializedData,
                0,
                CBlockHeaderAndShortTxIDs{*mBlock}};

            if(index.nStatus.hasDiskBlockMetaData())
            {
                auto metaData = index.GetDiskBlockMetaData();
                mCompactBlockMessage =
                    std::make_shared<const CCompactBlockMessageData>(
                        std::move(serializedData),
                        metaData.diskDataHash,
                        metaData.diskDataSize);
            }
            else
            {
                mCompactBlockMessage =
                    std::make_shared<const CCompactBlockMessageData>(
                        std::move(serializedData));
            }
        }

        std::shared_ptr<const CBlock> GetBlock() const
        {
            std::shared_lock lock{mMutex};

            return mBlock;
        }

        std::shared_ptr<const CBlock> GetBlockIfMatch(
            const uint256& expectedBlockHash) const
        {
            std::shared_lock lock{mMutex};

            if(mBlock && mBlock->GetHash() == expectedBlockHash)
            {
                return mBlock;
            }

            return {};
        }

        std::shared_ptr<const CCompactBlockMessageData> GetCompactBlockMessage() const
        {
            std::shared_lock lock{mMutex};

            return mCompactBlockMessage;
        }

        std::shared_ptr<const CCompactBlockMessageData> GetCompactBlockMessageIfMatch(
            const uint256& expectedBlockHash) const
        {
            std::shared_lock lock{mMutex};

            if(mBlock && mBlock->GetHash() == expectedBlockHash)
            {
                return mCompactBlockMessage;
            }

            return {};
        }

    private:
        mutable std::shared_mutex mMutex;
        std::shared_ptr<const CBlock> mBlock;
        std::shared_ptr<const CCompactBlockMessageData> mCompactBlockMessage;
    };

    CMostRecentBlockCache mostRecentBlock;
}

void PeerLogicValidation::NewPoWValidBlock(
    const CBlockIndex *pindex, const std::shared_ptr<const CBlock> &pblock) {

    LOCK(cs_main);

    static int nHighestFastAnnounce = 0;
    if (pindex->nHeight <= nHighestFastAnnounce) {
        return;
    }
    nHighestFastAnnounce = pindex->nHeight;

    uint256 hashBlock(pblock->GetHash());

    mostRecentBlock.SetBlock(pblock, *pindex);
    auto msgData = mostRecentBlock.GetCompactBlockMessage();

    connman->ForEachNode([this, &msgData, pindex, &hashBlock](const CNodePtr& pnode) {
        if (pnode->nVersion < INVALID_CB_NO_BAN_VERSION || pnode->fDisconnect) {
            return;
        }
        // Try to obtain an access to the node's state data.
        const CNodeStateRef stateRef { GetState(pnode->GetId()) };
        const CNodeStatePtr& state { stateRef.get() };
        assert(state);
        ProcessBlockAvailability(state);
        // If the peer has, or we announced to them the previous block already,
        // but we don't think they have this one, go ahead and announce it.
        if (state->fPreferHeaderAndIDs && !PeerHasHeader(state, pindex) &&
            PeerHasHeader(state, pindex->pprev)) {

            LogPrint(BCLog::NET, "%s sending header-and-ids %s to peer=%d\n",
                     "PeerLogicValidation::NewPoWValidBlock",
                     hashBlock.ToString(), pnode->id);
            connman->PushMessage(
                pnode,
                msgData->CreateCompactBlockMessage());
            state->pindexBestHeaderSent = pindex;
        }
    });
}

void PeerLogicValidation::UpdatedBlockTip(const CBlockIndex *pindexNew,
                                          const CBlockIndex *pindexFork,
                                          bool fInitialDownload) {
    const int nNewHeight = pindexNew->nHeight;
    connman->SetBestHeight(nNewHeight);

    if (!fInitialDownload) {
        // Find the hashes of all blocks that weren't previously in the best
        // chain.
        std::vector<uint256> vHashes;
        const CBlockIndex *pindexToAnnounce = pindexNew;
        while (pindexToAnnounce != pindexFork) {
            vHashes.push_back(pindexToAnnounce->GetBlockHash());
            pindexToAnnounce = pindexToAnnounce->pprev;
            if (vHashes.size() == MAX_BLOCKS_TO_ANNOUNCE) {
                // Limit announcements in case of a huge reorganization. Rely on
                // the peer's synchronization mechanism in that case.
                break;
            }
        }
        // Relay inventory, but don't relay old inventory during initial block
        // download.
        connman->ForEachNode([nNewHeight, &vHashes](const CNodePtr& pnode) {
            if (nNewHeight > (pnode->nStartingHeight != -1
                                  ? pnode->nStartingHeight - 2000
                                  : 0)) {
                for (const uint256 &hash : boost::adaptors::reverse(vHashes)) {
                    pnode->PushBlockHash(hash);
                }
            }
        });
        connman->WakeMessageHandler();
    }

    nTimeBestReceived = GetTime();
}

void PeerLogicValidation::BlockChecked(const CBlock &block,
                                       const CValidationState &state) {
    LOCK(cs_main);

    const uint256 hash(block.GetHash());
    auto it = mapBlockSource.find(hash);
    
    if(it != mapBlockSource.end())
    {
        int nDoS = 0;
        if (state.IsInvalid(nDoS)) {
            // Try to obtain an access to the node's state data.
            const CNodeStateRef nodestateRef { GetState(it->second.first) };
            const CNodeStatePtr& nodestate { nodestateRef.get() };
            // Don't send reject message with code 0 or an internal reject code.
            if (it != mapBlockSource.end() && nodestate &&
                state.GetRejectCode() > 0 &&
                state.GetRejectCode() < REJECT_INTERNAL) {
                CBlockReject reject = {
                    uint8_t(state.GetRejectCode()),
                    state.GetRejectReason().substr(0, MAX_REJECT_MESSAGE_LENGTH),
                    hash};
                nodestate->rejects.push_back(reject);
                if (nDoS > 0 && it->second.second) {
                    Misbehaving(it->second.first, nDoS, state.GetRejectReason());
                }
            }
        }
        // Check that:
        // 1. The block is valid
        // 2. We're not in initial block download
        // 3. This is currently the best block we're aware of. We haven't updated
        //    the tip yet so we have no way to check this directly here. Instead we
        //    just check that there are currently no other blocks in flight.
        else if (state.IsValid() && !IsInitialBlockDownload() &&
                 mapBlocksInFlight.count(hash) == mapBlocksInFlight.size()) {
            MaybeSetPeerAsAnnouncingHeaderAndIDs(it->second.first, *connman);
        }

        mapBlockSource.erase(it);
    }
    // else block came from for e.g. RPC so we don't have the source node
}

//////////////////////////////////////////////////////////////////////////////
//
// Messages
//
bool AlreadyHave(const CInv &inv) {
    switch (inv.type) {
        case MSG_TX: {
            return IsTxnKnown(inv);
        }
        case MSG_BLOCK: {
            return IsBlockKnown(inv);
        }
    }
    // Don't know what it is, just say we already got one
    return true;
}

bool IsTxnKnown(const CInv &inv) {
    if (MSG_TX == inv.type) {
        const uint256& activeTipBlockHash {
            chainActiveSharedData.GetChainActiveTipBlockHash()
        };
        if (activeTipBlockHash != hashRecentRejectsChainTip) {
            // If the chain tip has changed previously rejected transactions
            // might be now valid, e.g. due to a nLockTime'd tx becoming
            // valid, or a double-spend. Reset the rejects filter and give
            // those txs a second chance.
            hashRecentRejectsChainTip = activeTipBlockHash;
            g_connman->ResetRecentRejects();
        }
        // Use pcoinsTip->HaveCoinInCache as a quick approximation to
        // exclude requesting or processing some txs which have already been
        // included in a block. As this is best effort, we only check for
        // output 0 and 1. This works well enough in practice and we get
        // diminishing returns with 2 onward.
        return g_connman->CheckTxnInRecentRejects(inv.hash) ||
               mempool.Exists(inv.hash) ||
               mempool.getNonFinalPool().exists(inv.hash) ||
               mempool.getNonFinalPool().recentlyRemoved(inv.hash) ||
               g_connman->CheckOrphanTxnExists(inv.hash) ||
               g_connman->CheckTxnExistsInValidatorsQueue(inv.hash) ||
               // It is safe to refer to pcoinsTip (without holding cs_main) as:
               // - pcoinsTip is initialized before CConnman object is created
               // - HaveCoinInCache is protected by an internal mtx
               pcoinsTip->HaveCoinInCache(COutPoint(inv.hash, 0)) ||
               pcoinsTip->HaveCoinInCache(COutPoint(inv.hash, 1));
    }
    // Don't know what it is, just say we already got one
    return true;
}

bool IsBlockKnown(const CInv &inv) {
    if (MSG_BLOCK == inv.type) {
        LOCK(cs_main);
        return mapBlockIndex.count(inv.hash);
    }
    // Don't know what it is, just say we already got one
    return true;
}

void RelayTransaction(const CTransaction &tx, CConnman &connman) {
    CInv inv { MSG_TX, tx.GetId() };
    TxMempoolInfo txinfo {};

    if(mempool.Exists(tx.GetId()))
    {
        txinfo = mempool.Info(tx.GetId());
    }
    else if(mempool.getNonFinalPool().exists(tx.GetId()))
    {
        txinfo = mempool.getNonFinalPool().getInfo(tx.GetId());
    }

    if(txinfo.tx)
    {
        connman.EnqueueTransaction( {inv, txinfo} );
    }
    else
    {
        // Relaying something not in the mempool; must be a forced relay
        connman.EnqueueTransaction( {inv, MakeTransactionRef(tx)} );
    }
}

static void RelayAddress(const CAddress &addr, bool fReachable,
                         CConnman &connman) {
    // Limited relaying of addresses outside our network(s)
    unsigned int nRelayNodes = fReachable ? 2 : 1;

    // Relay to a limited number of other nodes.
    // Use deterministic randomness to send to the same nodes for 24 hours at a
    // time so the addrKnowns of the chosen nodes prevent repeats.
    uint64_t hashAddr = addr.GetHash();
    const CSipHasher hasher =
        connman.GetDeterministicRandomizer(RANDOMIZER_ID_ADDRESS_RELAY)
            .Write(hashAddr << 32)
            .Write((GetTime() + hashAddr) / (24 * 60 * 60));
    FastRandomContext insecure_rand;

    std::array<std::pair<uint64_t, CNodePtr>, 2> best {{{0, nullptr}, {0, nullptr}}};
    assert(nRelayNodes <= best.size());

    bool allowUnsolictedAddr { gArgs.GetBoolArg("-allowunsolicitedaddr", false) };
    auto sortfunc = [&best, &hasher, nRelayNodes, allowUnsolictedAddr](const CNodePtr& pnode) {
        // FIXME: When we get rid of -allowunsolicitedaddr, change to just: pnode->fInbound && ...
        if ((allowUnsolictedAddr || pnode->fInbound) && pnode->nVersion >= CADDR_TIME_VERSION) {
            uint64_t hashKey = CSipHasher(hasher).Write(pnode->id).Finalize();
            for (unsigned int i = 0; i < nRelayNodes; i++) {
                if (hashKey > best[i].first) {
                    std::copy(best.begin() + i, best.begin() + nRelayNodes - 1,
                              best.begin() + i + 1);
                    best[i] = std::make_pair(hashKey, pnode);
                    break;
                }
            }
        }
    };
    connman.ForEachNode(sortfunc);

    for (unsigned int i = 0; i < nRelayNodes && best[i].first != 0; i++) {
        best[i].second->PushAddress(addr, insecure_rand);
    }
}

static bool rejectIfMaxDownloadExceeded(const Config &config, CSerializedNetMsg &msg, bool isMostRecentBlock, const CNodePtr& pfrom, CConnman &connman) {

    uint64_t maxSendQueuesBytes = config.GetMaxSendQueuesBytes();
    size_t totalSize = CSendQueueBytes::getTotalSendQueuesBytes() + msg.Size() + CMessageHeader::HEADER_SIZE;
    if (totalSize > maxSendQueuesBytes) {

        if (!isMostRecentBlock) {
            LogPrint(BCLog::NET, "Size of all msgs currently sending across "
                "all the queues is too large: %s. Maximum size: %s. Request ignored, block will not be sent. "
                "Sending reject.\n", totalSize, maxSendQueuesBytes); 
            connman.PushMessage(pfrom, CNetMsgMaker(INIT_PROTO_VERSION)
                .Make(NetMsgType::REJECT, std::string(NetMsgType::GETDATA), REJECT_TOOBUSY, strprintf("Max blocks' downloading size exceeded.")));
            return true;
        }

        if (!pfrom->fWhitelisted) {
            LogPrint(BCLog::NET, "Size of all msgs currently sending across "
                "all the queues is too large: %s. Maximum size: %s. Last block will not be sent, "
                "because it was requested by non whitelisted peer. \n",
                totalSize, maxSendQueuesBytes);
            connman.PushMessage(pfrom, CNetMsgMaker(INIT_PROTO_VERSION)
                .Make(NetMsgType::REJECT, std::string(NetMsgType::GETDATA), REJECT_TOOBUSY, strprintf("Max blocks' downloading size exceeded.")));
            return true;
        }
        
        LogPrint(BCLog::NET, "Size of all msgs currently sending across "
                "all the queues is too large: %s. Maximum size: %s. Sending last block anyway "
                "because it was requested by whitelisted peer. \n",
                totalSize, maxSendQueuesBytes);
    }

    return false;
}

static bool SendCompactBlock(
    const Config& config,
    bool isMostRecentBlock,
    const CNodePtr& node,
    CConnman& connman,
    const CNetMsgMaker msgMaker,
    const CDiskBlockPos& pos)
{
    auto reader = GetDiskBlockStreamReader(pos);
    if (!reader) {
        assert(!"cannot load block from disk");
    }

    CBlockHeaderAndShortTxIDs cmpctblock{*reader};

    CSerializedNetMsg compactBlockMsg =
        msgMaker.Make(NetMsgType::CMPCTBLOCK, cmpctblock);
    if (rejectIfMaxDownloadExceeded(config, compactBlockMsg, isMostRecentBlock, node, connman)) {
        return false;
    }
    connman.PushMessage(node, std::move(compactBlockMsg));

    return true;
}

static void SendBlock(
    const Config& config,
    bool isMostRecentBlock,
    const CNodePtr& pfrom,
    CConnman& connman,
    CBlockIndex& index)
{
    auto stream = StreamBlockFromDisk(index, pfrom->GetSendVersion());

    if (!stream)
    {
        assert(!"can not load block from disk");
    }

    auto metaData = index.GetDiskBlockMetaData();
    CSerializedNetMsg blockMsg{
            NetMsgType::BLOCK,
            std::move(metaData.diskDataHash),
            metaData.diskDataSize,
            std::move(stream)
        };

    if (rejectIfMaxDownloadExceeded(config, blockMsg, isMostRecentBlock, pfrom, connman)) {
        return;
    }

    connman.PushMessage(pfrom, std::move(blockMsg));
}

static void SendUnseenTransactions(
    // requires: ascending ordered
    const std::vector<std::pair<unsigned int, uint256>>& vOrderedUnseenTransactions,
    CConnman& connman,
    const CNodePtr& pfrom,
    const CNetMsgMaker msgMaker,
    const CDiskBlockPos& pos)
{
    if (vOrderedUnseenTransactions.empty())
    {
        return;
    }

    auto stream = GetDiskBlockStreamReader(pos);
    if (!stream) {
        assert(!"can not load block from disk");
    }

    size_t currentTransactionNumber = 0;
    auto nextMissingIt = vOrderedUnseenTransactions.begin();
    do
    {
        const CTransaction& transaction = stream->ReadTransaction();
        if (nextMissingIt->first == currentTransactionNumber)
        {
            connman.PushMessage(
                pfrom,
                msgMaker.Make(NetMsgType::TX, transaction));
            ++nextMissingIt;

            if (nextMissingIt == vOrderedUnseenTransactions.end())
            {
                return;
            }
        }

        ++currentTransactionNumber;
    } while(!stream->EndOfStream());

    assert(!"vOrderedUnseenTransactions was not ascending ordered or block didn't contain all transactions!");
}

static void ProcessGetData(const Config &config, const CNodePtr& pfrom,
                           const Consensus::Params &consensusParams,
                           CConnman &connman,
                           const std::atomic<bool> &interruptMsgProc) {

    std::deque<CInv>::iterator it = pfrom->vRecvGetData.begin();
    std::vector<CInv> vNotFound;
    const CNetMsgMaker msgMaker(pfrom->GetSendVersion());
    LOCK(cs_main);

    while (it != pfrom->vRecvGetData.end()) {
        // Don't bother if send buffer is too full to respond anyway.
        if (pfrom->fPauseSend) {
            break;
        }

        const CInv &inv = *it;
        {
            if (interruptMsgProc) {
                return;
            }

            it++;

            if (inv.type == MSG_BLOCK || inv.type == MSG_FILTERED_BLOCK ||
                inv.type == MSG_CMPCT_BLOCK) {
                bool send = false;
                BlockMap::iterator mi = mapBlockIndex.find(inv.hash);
                if (mi != mapBlockIndex.end()) {
                    if (mi->second->nChainTx &&
                        !mi->second->IsValid(BlockValidity::SCRIPTS) &&
                        mi->second->IsValid(BlockValidity::TREE)
                        && IsBlockABestChainTipCandidate(*mi->second)) {

                        LogPrint(
                            BCLog::NET,
                            "Block %s is still waiting as a candidate. Deferring getdata reply.\n",
                            inv.hash.ToString());

                        // If we have the block and all of its parents, but have
                        // not yet validated it, we might be in the middle of
                        // connecting it (ie in the unlock of cs_main before
                        // ActivateBestChain but after AcceptBlock). In this
                        // case, we need to wait for a while longer before
                        // deciding whether we should relay it or not so we
                        // break out of the loop and continue once we once again
                        // get our turn.
                        --it;
                        break;
                    }
                    if (chainActive.Contains(mi->second)) {
                        send = true;
                    } else {
                        static const int nOneMonth = 30 * 24 * 60 * 60;
                        // To prevent fingerprinting attacks, only send blocks
                        // outside of the active chain if they are valid, and no
                        // more than a month older (both in time, and in best
                        // equivalent proof of work) than the best header chain
                        // we know about.
                        send = mi->second->IsValid(BlockValidity::SCRIPTS) &&
                               (pindexBestHeader != nullptr) &&
                               (pindexBestHeader->GetBlockTime() -
                                    mi->second->GetBlockTime() <
                                nOneMonth) &&
                               (GetBlockProofEquivalentTime(
                                    *pindexBestHeader, *mi->second,
                                    *pindexBestHeader,
                                    consensusParams) < nOneMonth);
                        if (!send) {
                            LogPrint(BCLog::NET, "%s: ignoring request from peer=%i for "
                                      "old block that isn't in the main chain\n",
                                      __func__, pfrom->GetId());
                        }
                    }
                }

                // Disconnect node in case we have reached the outbound limit
                // for serving historical blocks never disconnect whitelisted
                // nodes.
                // assume > 1 week = historical
                static const int nOneWeek = 7 * 24 * 60 * 60;
                if (send && connman.OutboundTargetReached(true) &&
                    (((pindexBestHeader != nullptr) &&
                      (pindexBestHeader->GetBlockTime() -
                           mi->second->GetBlockTime() >
                       nOneWeek)) ||
                     inv.type == MSG_FILTERED_BLOCK) &&
                    !pfrom->fWhitelisted) {
                    LogPrint(BCLog::NET, "historical block serving limit "
                                         "reached, disconnect peer=%d\n",
                             pfrom->GetId());

                    // disconnect node
                    pfrom->fDisconnect = true;
                    send = false;
                }

                bool isMostRecentBlock = chainActive.Tip() == mi->second;
                // Pruned nodes may have deleted the block, so check whether
                // it's available before trying to send.
                if (send && (mi->second->nStatus.hasData())) {
                    // Send block from disk

                    if (inv.type == MSG_BLOCK)
                    {
                        SendBlock(
                            config,
                            isMostRecentBlock,
                            pfrom,
                            connman,
                            *mi->second);
                    } else if (inv.type == MSG_FILTERED_BLOCK) {
                        auto stream =
                            GetDiskBlockStreamReader(mi->second->GetBlockPos());
                        if (!stream) {
                            assert(!"can not load block from disk");
                        }

                        bool sendMerkleBlock = false;
                        CMerkleBlock merkleBlock;
                        {
                            LOCK(pfrom->cs_filter);
                            sendMerkleBlock = true;
                            merkleBlock =
                                CMerkleBlock(*stream, pfrom->mFilter);
                        }
                        if (sendMerkleBlock) {
                            CSerializedNetMsg merkleBlockMsg = msgMaker.Make(NetMsgType::MERKLEBLOCK, merkleBlock);
                            if (rejectIfMaxDownloadExceeded(config, merkleBlockMsg, isMostRecentBlock, pfrom, connman)) {
                                break;
                            }
                            connman.PushMessage(pfrom, std::move(merkleBlockMsg));
                            // CMerkleBlock just contains hashes, so also push
                            // any transactions in the block the client did not
                            // see. This avoids hurting performance by
                            // pointlessly requiring a round-trip. Note that
                            // there is currently no way for a node to request
                            // any single transactions we didn't send here -
                            // they must either disconnect and retry or request
                            // the full block. Thus, the protocol spec specified
                            // allows for us to provide duplicate txn here,
                            // however we MUST always provide at least what the
                            // remote peer needs.
                            SendUnseenTransactions(
                                merkleBlock.vMatchedTxn,
                                connman,
                                pfrom,
                                msgMaker,
                                mi->second->GetBlockPos());
                        }
                        // else
                        // no response
                    } else if (inv.type == MSG_CMPCT_BLOCK) {
                        // If a peer is asking for old blocks, we're almost
                        // guaranteed they won't have a useful mempool to match
                        // against a compact block, and we don't feel like
                        // constructing the object for them, so instead we
                        // respond with the full, non-compact block.
                        if (CanDirectFetch(consensusParams) &&
                            mi->second->nHeight >=
                                chainActive.Height() - MAX_CMPCTBLOCK_DEPTH)
                        {
                            bool sent = SendCompactBlock(
                                config,
                                isMostRecentBlock,
                                pfrom,
                                connman,
                                msgMaker,
                                mi->second->GetBlockPos());
                            if (!sent)
                            {
                                break;
                            }
                        } else {
                            SendBlock(
                                config,
                                isMostRecentBlock,
                                pfrom,
                                connman,
                                *mi->second);
                        }
                    }

                    // Trigger the peer node to send a getblocks request for the
                    // next batch of inventory.
                    if (inv.hash == pfrom->hashContinue) {
                        // Bypass PushInventory, this must send even if
                        // redundant, and we want it right after the last block
                        // so they don't wait for other stuff first.
                        std::vector<CInv> vInv;
                        vInv.push_back(
                            CInv(MSG_BLOCK, chainActive.Tip()->GetBlockHash()));
                        connman.PushMessage(
                            pfrom, msgMaker.Make(NetMsgType::INV, vInv));
                        pfrom->hashContinue.SetNull();
                    }
                }
            } else if (inv.type == MSG_TX) {
                // Send stream from relay memory
                bool push = false;
                auto mi = mapRelay.find(inv.hash);
                if (mi != mapRelay.end()) {
                    connman.PushMessage(
                        pfrom,
                        msgMaker.Make(NetMsgType::TX, *mi->second));
                    push = true;
                } else if (pfrom->timeLastMempoolReq) {
                    auto txinfo = mempool.Info(inv.hash);
                    // To protect privacy, do not answer getdata using the
                    // mempool when that TX couldn't have been INVed in reply to
                    // a MEMPOOL request.
                    if (txinfo.tx &&
                        txinfo.nTime <= pfrom->timeLastMempoolReq) {
                        connman.PushMessage(pfrom,
                                            msgMaker.Make(NetMsgType::TX,
                                                          *txinfo.tx));
                        push = true;
                    }
                }
                if (!push) {
                    vNotFound.push_back(inv);
                }
            }

            // Track requests for our stuff.
            GetMainSignals().Inventory(inv.hash);

            if (inv.type == MSG_BLOCK || inv.type == MSG_FILTERED_BLOCK ||
                inv.type == MSG_CMPCT_BLOCK) {
                break;
            }
        }
    }

    pfrom->vRecvGetData.erase(pfrom->vRecvGetData.begin(), it);

    if (!vNotFound.empty()) {
        // Let the peer know that we didn't find what it asked for, so it
        // doesn't have to wait around forever. Currently only SPV clients
        // actually care about this message: it's needed when they are
        // recursively walking the dependencies of relevant unconfirmed
        // transactions. SPV clients want to do that because they want to know
        // about (and store and rebroadcast and risk analyze) the dependencies
        // of transactions relevant to them, without having to download the
        // entire memory pool.
        connman.PushMessage(pfrom,
                            msgMaker.Make(NetMsgType::NOTFOUND, vNotFound));
    }
}

inline static void SendBlockTransactions(const CBlock &block,
                                         const BlockTransactionsRequest &req,
                                         const CNodePtr& pfrom, CConnman &connman) {
    BlockTransactions resp(req);
    for (size_t i = 0; i < req.indices.size(); i++) {
        if (req.indices[i] >= block.vtx.size()) {
            Misbehaving(pfrom, 100, "out-of-bound-tx-index");
            LogPrintf("Peer %d sent us a getblocktxn with out-of-bounds tx indices", pfrom->id);
            return;
        }
        resp.txn[i] = block.vtx[req.indices[i]];
    }

    const CNetMsgMaker msgMaker(pfrom->GetSendVersion());
    connman.PushMessage(pfrom,
                        msgMaker.Make(NetMsgType::BLOCKTXN, resp));
}

/**
* Process reject messages.
*/
static void ProcessRejectMessage(CDataStream& vRecv, const CNodePtr& pfrom)
{
    if(LogAcceptCategory(BCLog::NET))
    {
        try
        {
            std::string strMsg;
            uint8_t ccode;
            std::string strReason;
            vRecv >> LIMITED_STRING(strMsg, CMessageHeader::COMMAND_SIZE) >>
                ccode >>
                LIMITED_STRING(strReason, MAX_REJECT_MESSAGE_LENGTH);

            std::ostringstream ss;
            ss << strMsg << " code " << itostr(ccode) << ": " << strReason;

            if(strMsg == NetMsgType::BLOCK || strMsg == NetMsgType::TX)
            {
                uint256 hash;
                vRecv >> hash;
                ss << ": hash " << hash.ToString();
            }
            LogPrint(BCLog::NET, "Reject %s\n", SanitizeString(ss.str()));

            if (ccode == REJECT_TOOBUSY) {
                // Try to obtain an access to the node's state data.
                const CNodeStateRef stateRef { GetState(pfrom->GetId()) };
                const CNodeStatePtr& state { stateRef.get() };
                if (!state) {
                    return;
                }
                // Peer is too busy with sending blocks so we will not ask from a peer for TOOBUSY_RETRY_DELAY.
                state->nextSendThresholdTime = GetTimeMicros() + TOOBUSY_RETRY_DELAY;
                // cs_main needs to be locked due to a reference to mapBlocksInFlight
                {
                    LOCK(cs_main);
                    for (const QueuedBlock &entry : state->vBlocksInFlight) {
                        mapBlocksInFlight.erase(entry.hash);
                    }
                }

            }
        }
        catch (const std::ios_base::failure &)
        {
            // Avoid feedback loops by preventing reject messages from
            // triggering a new reject message.
            LogPrint(BCLog::NET, "Unparseable reject message received\n");
        }
    }
}

/**
* Process version messages.
*/
static bool ProcessVersionMessage(const CNodePtr& pfrom, const std::string& strCommand,
    CDataStream& vRecv, CConnman& connman, const Config& config)
{
    // Each connection can only send one version message
    if(pfrom->nVersion != 0) {
        connman.PushMessage(
            pfrom,
            CNetMsgMaker(INIT_PROTO_VERSION)
                .Make(NetMsgType::REJECT, strCommand, REJECT_DUPLICATE,
                      std::string("Duplicate version message")));
        Misbehaving(pfrom, 1, "multiple-version");
        return false;
    }

    int64_t nTime;
    CAddress addrMe;
    CAddress addrFrom;
    uint64_t nNonce = 1;
    uint64_t nServiceInt;
    ServiceFlags nServices;
    int nVersion;
    int nSendVersion;
    std::string strSubVer;
    std::string cleanSubVer;
    int nStartingHeight = -1;
    bool fRelay = true;

    vRecv >> nVersion >> nServiceInt >> nTime >> addrMe;
    nSendVersion = std::min(nVersion, PROTOCOL_VERSION);
    nServices = ServiceFlags(nServiceInt);
    if(!pfrom->fInbound) {
        connman.SetServices(pfrom->addr, nServices);
    }
    if(pfrom->nServicesExpected & ~nServices) {
        LogPrint(BCLog::NET, "peer=%d does not offer the expected services "
                             "(%08x offered, %08x expected); "
                             "disconnecting\n",
                 pfrom->id, nServices, pfrom->nServicesExpected);
        connman.PushMessage(
            pfrom,
            CNetMsgMaker(INIT_PROTO_VERSION)
                .Make(NetMsgType::REJECT, strCommand, REJECT_NONSTANDARD,
                      strprintf("Expected to offer services %08x",
                                pfrom->nServicesExpected)));
        pfrom->fDisconnect = true;
        return false;
    }

    if(nVersion < MIN_PEER_PROTO_VERSION) {
        // Disconnect from peers older than this proto version
        LogPrint(BCLog::NET, "peer=%d using obsolete version %i; disconnecting\n",
                  pfrom->id, nVersion);
        connman.PushMessage(
            pfrom,
            CNetMsgMaker(INIT_PROTO_VERSION)
                .Make(NetMsgType::REJECT, strCommand, REJECT_OBSOLETE,
                      strprintf("Version must be %d or greater",
                                MIN_PEER_PROTO_VERSION)));
        pfrom->fDisconnect = true;
        return false;
    }

    if(!vRecv.empty()) {
        vRecv >> addrFrom >> nNonce;
    }
    if(!vRecv.empty()) {
        vRecv >> LIMITED_STRING(strSubVer, MAX_SUBVERSION_LENGTH);
        cleanSubVer = SanitizeString(strSubVer);
        
        if (config.IsClientUABanned(cleanSubVer))
        {
            Misbehaving(pfrom, gArgs.GetArg("-banscore", DEFAULT_BANSCORE_THRESHOLD), "invalid-UA");
            return false;
        }
    }
    if(!vRecv.empty()) {
        vRecv >> nStartingHeight;
    }
    if(!vRecv.empty()) {
        vRecv >> fRelay;
    }
    // Disconnect if we connected to ourself
    if(pfrom->fInbound && !connman.CheckIncomingNonce(nNonce)) {
        LogPrintf("connected to self at %s, disconnecting\n",
                  pfrom->addr.ToString());
        pfrom->fDisconnect = true;
        return true;
    }

    if(pfrom->fInbound && addrMe.IsRoutable()) {
        SeenLocal(addrMe);
    }

    // Be shy and don't send version until we hear
    if(pfrom->fInbound) {
        PushNodeVersion(pfrom, connman, GetAdjustedTime());
    }

    connman.PushMessage(pfrom, CNetMsgMaker(INIT_PROTO_VERSION).Make(NetMsgType::VERACK));

    // Announce our protocol configuration immediately after we send VERACK.
    PushProtoconf(pfrom, connman);

    pfrom->nServices = nServices;
    pfrom->SetAddrLocal(addrMe);
    {
        LOCK(pfrom->cs_SubVer);
        pfrom->strSubVer = strSubVer;
        pfrom->cleanSubVer = cleanSubVer;
    }
    pfrom->nStartingHeight = nStartingHeight;
    pfrom->fClient = !(nServices & NODE_NETWORK);
    {
        LOCK(pfrom->cs_filter);
        // Set to true after we get the first filter* message
        pfrom->fRelayTxes = fRelay;
    }

    // Change version
    pfrom->SetSendVersion(nSendVersion);
    pfrom->nVersion = nVersion;

    // Potentially mark this peer as a preferred download peer.
    UpdatePreferredDownload(pfrom);

    if(!pfrom->fInbound) {
        // Advertise our address
        if(fListen && !IsInitialBlockDownload()) {
            CAddress addr =
                GetLocalAddress(&pfrom->addr, pfrom->GetLocalServices());
            FastRandomContext insecure_rand;
            if(addr.IsRoutable()) {
                LogPrint(BCLog::NET,
                         "ProcessMessages: advertising address %s\n",
                         addr.ToString());
                pfrom->PushAddress(addr, insecure_rand);
            }
            else if(IsPeerAddrLocalGood(pfrom)) {
                addr.SetIP(addrMe);
                LogPrint(BCLog::NET,
                         "ProcessMessages: advertising address %s\n",
                         addr.ToString());
                pfrom->PushAddress(addr, insecure_rand);
            }
        }

        // Get recent addresses
        if(pfrom->fOneShot || pfrom->nVersion >= CADDR_TIME_VERSION || connman.GetAddressCount() < 1000) {
            pfrom->fGetAddr = true;
            connman.PushMessage(pfrom, CNetMsgMaker(nSendVersion).Make(NetMsgType::GETADDR));
        }
        connman.MarkAddressGood(pfrom->addr);
    }

    std::string remoteAddr;
    if(fLogIPs) {
        remoteAddr = ", peeraddr=" + pfrom->addr.ToString();
    }

    LogPrint(BCLog::NET, "receive version message: [%s] %s: version %d, blocks=%d, "
              "us=%s, peer=%d%s\n",
              pfrom->addr.ToString().c_str(), cleanSubVer, pfrom->nVersion,
              pfrom->nStartingHeight, addrMe.ToString(), pfrom->id,
              remoteAddr);

    int64_t nTimeOffset = nTime - GetTime();
    pfrom->nTimeOffset = nTimeOffset;
    AddTimeData(pfrom->addr, nTimeOffset);

    // If the peer is old enough to have the old alert system, send it the
    // final alert.
    if(pfrom->nVersion <= 70012) {
        CDataStream finalAlert(
            ParseHex("60010000000000000000000000ffffff7f00000000ffffff7ffef"
                     "fff7f01ffffff7f00000000ffffff7f00ffffff7f002f55524745"
                     "4e543a20416c657274206b657920636f6d70726f6d697365642c2"
                     "075706772616465207265717569726564004630440220653febd6"
                     "410f470f6bae11cad19c48413becb1ac2c17f908fd0fd53bdc3ab"
                     "d5202206d0e9c96fe88d4a0f01ed9dedae2b6f9e00da94cad0fec"
                     "aae66ecf689bf71b50"),
            SER_NETWORK, PROTOCOL_VERSION);
        connman.PushMessage(
            pfrom, CNetMsgMaker(nSendVersion).Make("alert", finalAlert));
    }

    // Feeler connections exist only to verify if address is online.
    if(pfrom->fFeeler) {
        assert(pfrom->fInbound == false);
        pfrom->fDisconnect = true;
    }

    return true;
}

/**
* Prcess version ack message.
*/
static void ProcessVerAckMessage(const CNodePtr& pfrom, const CNetMsgMaker& msgMaker,
    CConnman& connman)
{
    pfrom->SetRecvVersion(std::min(pfrom->nVersion.load(), PROTOCOL_VERSION));

    if(!pfrom->fInbound) {
        // Try to obtain an access to the node's state data.
        const CNodeStateRef stateRef { GetState(pfrom->GetId()) };
        const CNodeStatePtr& state { stateRef.get() };
        assert(state);
        // Mark this node as currently connected, so we update its timestamp later.
        state->fCurrentlyConnected = true;
        LogPrintf("New outbound peer connected: version: %d, blocks=%d, peer=%d%s\n",
                  pfrom->nVersion.load(), pfrom->nStartingHeight, pfrom->GetId(),
                  (fLogIPs ? strprintf(", peeraddr=%s", pfrom->addr.ToString()) : ""));
    }
    else {
        LogPrintf("New inbound peer connected: version: %d, subver: %s, blocks=%d, peer=%d%s\n",
                  pfrom->nVersion.load(), pfrom->cleanSubVer, pfrom->nStartingHeight, pfrom->GetId(),
                  (fLogIPs ? strprintf(", peeraddr=%s", pfrom->addr.ToString()) : ""));
    }

    if(pfrom->nVersion >= SENDHEADERS_VERSION) {
        // Tell our peer we prefer to receive headers rather than inv's
        // We send this to non-NODE NETWORK peers as well, because even
        // non-NODE NETWORK peers can announce blocks (such as pruning
        // nodes)
        connman.PushMessage(pfrom, msgMaker.Make(NetMsgType::SENDHEADERS));
    }

    if(pfrom->nVersion >= SHORT_IDS_BLOCKS_VERSION) {
        // Tell our peer we are willing to provide version 1 or 2
        // cmpctblocks. However, we do not request new block announcements
        // using cmpctblock messages. We send this to non-NODE NETWORK peers
        // as well, because they may wish to request compact blocks from us.
        bool fAnnounceUsingCMPCTBLOCK = false;
        uint64_t nCMPCTBLOCKVersion = 1;
        connman.PushMessage(pfrom,
                            msgMaker.Make(NetMsgType::SENDCMPCT,
                                          fAnnounceUsingCMPCTBLOCK,
                                          nCMPCTBLOCKVersion));
    }
    pfrom->fSuccessfullyConnected = true;
}

/**
* Process peer address message.
*/
static bool ProcessAddrMessage(const CNodePtr& pfrom, const std::atomic<bool>& interruptMsgProc,
    CDataStream& vRecv, CConnman& connman)
{
    std::vector<CAddress> vAddr;
    vRecv >> vAddr;

    // Don't want addr from older versions unless seeding
    if (pfrom->nVersion < CADDR_TIME_VERSION &&
        connman.GetAddressCount() > 1000) {
        return true;
    }
    if (vAddr.size() > 1000) {
        Misbehaving(pfrom, 20, "oversized-addr");
        return error("message addr size() = %u", vAddr.size());
    }

    // The purpose of using exchange here is to atomically set to false and
    // also get whether I asked for an addr.
    bool requestedAddr { pfrom->fGetAddr.exchange(false) };

    // FIXME: For now we make rejecting unsolicited addr messages configurable (on by default).
    // Once we are happy this doesn't have any adverse effects on address propagation we can
    // remove the config option and make it the only behaviour.
    bool rejectUnsolictedAddr { !gArgs.GetBoolArg("-allowunsolicitedaddr", false) };
    if(rejectUnsolictedAddr)
    {
        // To avoid malicious flooding of our address table, only allow unsolicited
        // ADDR messages to insert the connecting IP. We need to allow this IP
        // to be inserted, or there is no way for that node to tell the network
        // about itself if its behind a NAT.

        // Digression about how things work behind a NAT:
        //      Node A periodically ADDRs node B with the address that B reported
        //      to A as A's own address (in the VERSION message).
        if (!requestedAddr && pfrom->fInbound)
        {
            bool reportedOwnAddr {false};
            CAddress ownAddr {};
            for (const CAddress& addr : vAddr)
            {
                // Server listen port will be different. We want to compare IPs and then use provided port
                if (static_cast<CNetAddr>(addr) == static_cast<CNetAddr>(pfrom->addr))
                {
                    ownAddr = addr;
                    reportedOwnAddr = true;
                    break;
                }
            }
            if (reportedOwnAddr)
            {
                // Get rid of every address the remote node tried to inject except itself.
                vAddr.resize(1);
                vAddr[0] = ownAddr;
            }
            else
            {
                // Today unsolicited ADDRs are not illegal, but we should consider
                // misbehaving on this because a few unsolicited ADDRs are ok from
                // a DOS perspective but lots are not.
                LogPrint(BCLog::NET, "Peer %d sent unsolicited ADDR\n", pfrom->id);

                // We don't want to process any other addresses, but giving them is not an error
                return true;
            }
        }
    }

    // Store the new addresses
    std::vector<CAddress> vAddrOk;
    int64_t nNow = GetAdjustedTime();
    int64_t nSince = nNow - 10 * 60;
    for (CAddress& addr : vAddr) {
        if (interruptMsgProc) {
            return true;
        }

        if ((addr.nServices & REQUIRED_SERVICES) != REQUIRED_SERVICES) {
            continue;
        }

        if (addr.nTime <= 100000000 || addr.nTime > nNow + 10 * 60) {
            addr.nTime = nNow - 5 * 24 * 60 * 60;
        }
        pfrom->AddAddressKnown(addr);
        bool fReachable = IsReachable(addr);
        // FIXME: When we remove -allowunsolicitedaddr, remove the whole (rejectUnsolictedAddr || !requestedAddr) check
        if (addr.nTime > nSince && vAddr.size() <= 10 && addr.IsRoutable() && (rejectUnsolictedAddr || !requestedAddr)) {
            // Relay to a limited number of other nodes
            RelayAddress(addr, fReachable, connman);
        }
        // Do not store addresses outside our network
        if (fReachable) {
            vAddrOk.push_back(addr);
        }
    }
    connman.AddNewAddresses(vAddrOk, pfrom->addr, 2 * 60 * 60);
    if (pfrom->fOneShot) {
        pfrom->fDisconnect = true;
    }

    return true;
}

/**
* Process send headers message.
*/
static void ProcessSendHeadersMessage(const CNodePtr& pfrom)
{
    // Try to obtain an access to the node's state data.
    const CNodeStateRef stateRef { GetState(pfrom->GetId()) };
    const CNodeStatePtr& state { stateRef.get() };
    if(state) {
        if(state->fPreferHeaders) {
            // This message should only be received once. If its already set it might
            // indicate a misbehaving node. Increase the banscore
            Misbehaving(pfrom, 1, "Invalid SendHeaders activity");
            LogPrint(BCLog::NET, "Peer %d sent SendHeaders more than once\n", pfrom->id);
        }
        else {
            state->fPreferHeaders = true;
        }
    }
}

/**
* Process send compact message.
*/
static void ProcessSendCompactMessage(const CNodePtr& pfrom, CDataStream& vRecv)
{
    bool fAnnounceUsingCMPCTBLOCK = false;
    uint64_t nCMPCTBLOCKVersion = 0;
    vRecv >> fAnnounceUsingCMPCTBLOCK >> nCMPCTBLOCKVersion;
    if(nCMPCTBLOCKVersion == 1) {
        // Try to obtain an access to the node's state data.
        const CNodeStateRef stateRef { GetState(pfrom->GetId()) };
        const CNodeStatePtr& state { stateRef.get() };
        assert(state);
        // fProvidesHeaderAndIDs is used to "lock in" version of compact
        // blocks we send.
        if(!state->fProvidesHeaderAndIDs) {
            state->fProvidesHeaderAndIDs = true;
        }

        state->fPreferHeaderAndIDs = fAnnounceUsingCMPCTBLOCK;
        if(!state->fSupportsDesiredCmpctVersion) {
            state->fSupportsDesiredCmpctVersion = true;
        }
    }
}
 
/**
* Process inventory message.
*/
static void ProcessInvMessage(const CNodePtr& pfrom,
                              const CNetMsgMaker& msgMaker,
                              const std::atomic<bool>& interruptMsgProc,
                              CDataStream& vRecv,
                              CConnman& connman)
{
    std::vector<CInv> vInv;
    vRecv >> vInv;
    bool fBlocksOnly = !fRelayTxes;

    // Allow whitelisted peers to send data other than blocks in blocks only
    // mode if whitelistrelay is true
    if(pfrom->fWhitelisted && gArgs.GetBoolArg("-whitelistrelay", DEFAULT_WHITELISTRELAY)) {
        fBlocksOnly = false;
    }

    LOCK(cs_main);
    std::vector<CInv> vToFetch;

    for(size_t nInv = 0; nInv < vInv.size(); nInv++) {
        CInv &inv = vInv[nInv];

        if(interruptMsgProc) {
            return;
        }

        bool fAlreadyHave = AlreadyHave(inv);

        if(inv.type == MSG_BLOCK) {
            LogPrint(BCLog::NET, "got block inv: %s %s peer=%d\n", inv.hash.ToString(),
                fAlreadyHave ? "have" : "new", pfrom->id);
            UpdateBlockAvailability(inv.hash, GetState(pfrom->GetId()).get());
            if(!fAlreadyHave && !fImporting && !fReindex && !mapBlocksInFlight.count(inv.hash)) {
                // We used to request the full block here, but since
                // headers-announcements are now the primary method of
                // announcement on the network, and since, in the case that
                // a node fell back to inv we probably have a reorg which we
                // should get the headers for first, we now only provide a
                // getheaders response here. When we receive the headers, we
                // will then ask for the blocks we need.
                connman.PushMessage(
                    pfrom,
                    msgMaker.Make(NetMsgType::GETHEADERS,
                                  chainActive.GetLocator(pindexBestHeader),
                                  inv.hash));
                LogPrint(BCLog::NET, "getheaders (%d) %s to peer=%d\n",
                         pindexBestHeader->nHeight, inv.hash.ToString(),
                         pfrom->id);
            }
        }
        else {
            LogPrint(BCLog::TXNSRC | BCLog::NET, "got txn inv: %s %s txnsrc peer=%d\n",
                inv.hash.ToString(), fAlreadyHave ? "have" : "new", pfrom->id);
            pfrom->AddInventoryKnown(inv);
            if(fBlocksOnly) {
                LogPrint(BCLog::NET, "transaction (%s) inv sent in violation of protocol peer=%d\n",
                         inv.hash.ToString(), pfrom->id);
            }
            else if(!fAlreadyHave && !fImporting && !fReindex && !IsInitialBlockDownload()) {
                pfrom->AskFor(inv);
            }
        }

        // Track requests for our stuff
        GetMainSignals().Inventory(inv.hash);
    }

    if(!vToFetch.empty()) {
        connman.PushMessage(pfrom, msgMaker.Make(NetMsgType::GETDATA, vToFetch));
    }
}

/**
* Process get data message.
*/
static void ProcessGetDataMessage(const Config& config,
                                  const CNodePtr& pfrom,
                                  const CChainParams& chainparams,
                                  const std::atomic<bool>& interruptMsgProc,
                                  CDataStream& vRecv,
                                  CConnman& connman)
{
    std::vector<CInv> vInv;
    vRecv >> vInv;

    LogPrint(BCLog::NET, "received getdata (%u invsz) peer=%d\n", vInv.size(), pfrom->id);

    if(vInv.size() > 0) {
        LogPrint(BCLog::NET, "received getdata for: %s peer=%d\n",
                 vInv[0].ToString(), pfrom->id);
    }

    pfrom->vRecvGetData.insert(pfrom->vRecvGetData.end(), vInv.begin(), vInv.end());
    ProcessGetData(config, pfrom, chainparams.GetConsensus(), connman, interruptMsgProc);
}
 
/**
* Process get blocks message.
*/
static bool ProcessGetBlocks(
    const Config& config,
    const CNodePtr& pfrom,
    const CChainParams& chainparams,
    const CGetBlockMessageRequest& req)
{
    LOCK(cs_main);

    // We might have announced the currently-being-connected tip using a
    // compact block, which resulted in the peer sending a getblocks
    // request, which we would otherwise respond to without the new block.
    // To avoid this situation we simply verify that there are no more block
    // index candidates that were received before getblocks request.
    if(AreOlderOrEqualUnvalidatedBlockIndexCandidates(req.GetRequestTime()))
    {
        return false;
    }

    const CBlockLocator& locator = req.GetLocator();
    const uint256& hashStop = req.GetHashStop();

    // Find the last block the caller has in the main chain
    const CBlockIndex* pindex =
        FindForkInGlobalIndex(chainActive, locator);

    // Send the rest of the chain
    if(pindex) {
        pindex = chainActive.Next(pindex);
    }
    int nLimit = 500;
    LogPrint(BCLog::NET, "getblocks %d to %s limit %d from peer=%d\n",
             (pindex ? pindex->nHeight : -1),
             hashStop.IsNull() ? "end" : hashStop.ToString(), nLimit,
             pfrom->id);
    for(; pindex; pindex = chainActive.Next(pindex)) {
        if(pindex->GetBlockHash() == hashStop) {
            LogPrint(BCLog::NET, "  getblocks stopping at %d %s\n",
                     pindex->nHeight, pindex->GetBlockHash().ToString());
            break;
        }
        // If pruning, don't inv blocks unless we have on disk and are
        // likely to still have for some reasonable time window (1 hour)
        // that block relay might require.
        const int nPrunedBlocksLikelyToHave = MIN_BLOCKS_TO_KEEP - 3600 / chainparams.GetConsensus().nPowTargetSpacing;
        if(fPruneMode &&
            (!pindex->nStatus.hasData() ||
             pindex->nHeight <=
                 chainActive.Tip()->nHeight - nPrunedBlocksLikelyToHave)) {
            LogPrint(
                BCLog::NET,
                " getblocks stopping, pruned or too old block at %d %s\n",
                pindex->nHeight, pindex->GetBlockHash().ToString());
            break;
        }
        pfrom->PushInventory(CInv(MSG_BLOCK, pindex->GetBlockHash()));
        if(--nLimit <= 0) {
            // When this block is requested, we'll send an inv that'll
            // trigger the peer to getblocks the next batch of inventory.
            LogPrint(BCLog::NET, "  getblocks stopping at limit %d %s\n",
                     pindex->nHeight, pindex->GetBlockHash().ToString());
            pfrom->hashContinue = pindex->GetBlockHash();
            break;
        }
    }

    return true;
}

static void ProcessGetBlocksMessage(
    const Config& config,
    const CNodePtr& pfrom,
    const CChainParams& chainparams,
    CDataStream& vRecv)
{
    pfrom->mGetBlockMessageRequest = {vRecv};
    if(ProcessGetBlocks(config, pfrom, chainparams, *pfrom->mGetBlockMessageRequest))
    {
        pfrom->mGetBlockMessageRequest = std::nullopt;
    }
    else
    {
        LogPrint(
            BCLog::NET,
            "Blocks that were received before getblocks message are still"
            " waiting as a candidate. Deferring getblocks reply.\n");
    }
}
 
/**
* Process getblocktxn message.
*/
static void ProcessGetBlockTxnMessage(const Config& config,
                                      const CNodePtr& pfrom,
                                      const CChainParams& chainparams,
                                      const std::atomic<bool>& interruptMsgProc,
                                      CDataStream& vRecv,
                                      CConnman& connman)
{
    BlockTransactionsRequest req;
    vRecv >> req;

    std::shared_ptr<const CBlock> recent_block =
        mostRecentBlock.GetBlockIfMatch(req.blockhash);
    if(recent_block) {
        SendBlockTransactions(*recent_block, req, pfrom, connman);
        return;
    }

    LOCK(cs_main);

    BlockMap::iterator it = mapBlockIndex.find(req.blockhash);
    if(it == mapBlockIndex.end() || !it->second->nStatus.hasData()) {
        LogPrint(BCLog::NET, "Peer %d sent us a getblocktxn for a block we don't have", pfrom->id);
        return;
    }

    if(it->second->nHeight < chainActive.Height() - MAX_BLOCKTXN_DEPTH) {
        // If an older block is requested (should never happen in practice,
        // but can happen in tests) send a block response instead of a
        // blocktxn response. Sending a full block response instead of a
        // small blocktxn response is preferable in the case where a peer
        // might maliciously send lots of getblocktxn requests to trigger
        // expensive disk reads, because it will require the peer to
        // actually receive all the data read from disk over the network.
        LogPrint(BCLog::NET, "Peer %d sent us a getblocktxn for a block > %i deep",
                 pfrom->id, MAX_BLOCKTXN_DEPTH);
        CInv inv;
        inv.type = MSG_BLOCK;
        inv.hash = req.blockhash;
        pfrom->vRecvGetData.push_back(inv);
        ProcessGetData(config, pfrom, chainparams.GetConsensus(), connman, interruptMsgProc);
        return;
    }

    CBlock block;
    bool ret = ReadBlockFromDisk(block, it->second, config);
    assert(ret);

    SendBlockTransactions(block, req, pfrom, connman);
}
 
/**
* Process get headers message.
*/
static void ProcessGetHeadersMessage(const CNodePtr& pfrom,
                                     const CNetMsgMaker& msgMaker,
                                     CDataStream& vRecv,
                                     CConnman& connman)
{
    CBlockLocator locator;
    uint256 hashStop;
    vRecv >> locator >> hashStop;

    LOCK(cs_main);
    if(IsInitialBlockDownload() && !pfrom->fWhitelisted) {
        LogPrint(BCLog::NET, "Ignoring getheaders from peer=%d because "
                             "node is in initial block download\n",
                 pfrom->id);
        return;
    }

    const CBlockIndex* pindex = nullptr;
    if(locator.IsNull()) {
        // If locator is null, return the hashStop block
        BlockMap::iterator mi = mapBlockIndex.find(hashStop);
        if(mi == mapBlockIndex.end()) {
            return;
        }
        pindex = mi->second;
    }
    else {
        // Find the last block the caller has in the main chain
        pindex = FindForkInGlobalIndex(chainActive, locator);
        if(pindex) {
            pindex = chainActive.Next(pindex);
        }
    }

    // We must use CBlocks, as CBlockHeaders won't include the 0x00 nTx
    // count at the end
    std::vector<CBlock> vHeaders;
    int nLimit = MAX_HEADERS_RESULTS;
    LogPrint(BCLog::NET, "getheaders %d to %s from peer=%d\n",
             (pindex ? pindex->nHeight : -1),
             hashStop.IsNull() ? "end" : hashStop.ToString(), pfrom->id);
    for(; pindex; pindex = chainActive.Next(pindex)) {
        vHeaders.push_back(pindex->GetBlockHeader());
        if(--nLimit <= 0 || pindex->GetBlockHash() == hashStop) {
            break;
        }
    }
    // Try to obtain an access to the node's state data.
    const CNodeStateRef stateRef { GetState(pfrom->GetId()) };
    const CNodeStatePtr& state { stateRef.get() };
    assert(state);
    // pindex can be nullptr either if we sent chainActive.Tip() OR
    // if our peer has chainActive.Tip() (and thus we are sending an empty
    // headers message). In both cases it's safe to update
    // pindexBestHeaderSent to be our tip.
    //
    // It is important that we simply reset the BestHeaderSent value here,
    // and not max(BestHeaderSent, newHeaderSent). We might have announced
    // the currently-being-connected tip using a compact block, which
    // resulted in the peer sending a headers request, which we respond to
    // without the new block. By resetting the BestHeaderSent, we ensure we
    // will re-announce the new block via headers (or compact blocks again)
    // in the SendMessages logic.
    state->pindexBestHeaderSent = pindex ? pindex : chainActive.Tip();
    connman.PushMessage(pfrom, msgMaker.Make(NetMsgType::HEADERS, vHeaders));
}
 
/**
* Process tx message.
*/
static void ProcessTxMessage(const Config& config,
                             const CNodePtr& pfrom,
                             const CNetMsgMaker& msgMaker,
                             const std::string& strCommand,
                             CDataStream& vRecv,
                             CConnman& connman)
{
    // Stop processing the transaction early if we are in blocks only mode and
    // peer is either not whitelisted or whitelistrelay is off
    if (!fRelayTxes &&
        (!pfrom->fWhitelisted ||
         !gArgs.GetBoolArg("-whitelistrelay", DEFAULT_WHITELISTRELAY))) {
        LogPrint(BCLog::NET,
                "transaction sent in violation of protocol peer=%d\n",
                 pfrom->id);
        return;
    }

    CTransactionRef ptx;
    vRecv >> ptx;
    const CTransaction &tx = *ptx;

    CInv inv(MSG_TX, tx.GetId());
    pfrom->AddInventoryKnown(inv);
    LogPrint(BCLog::TXNSRC, "got txn: %s txnsrc peer=%d\n", inv.hash.ToString(), pfrom->id);
    // Update 'ask for' inv set
    {
        LOCK(cs_invQueries);
        pfrom->setAskFor.erase(inv.hash);
        mapAlreadyAskedFor.erase(inv.hash);
    }
    // Enqueue txn for validation if it is not known
    if (!IsTxnKnown(inv)) {
        // Forward transaction to the validator thread.
        // By default, treat a received txn as a 'high' priority txn.
        // If the validation timeout occurs the txn is moved to the 'low' priority queue.
        connman.EnqueueTxnForValidator(
					std::make_shared<CTxInputData>(
                                        TxSource::p2p,  // tx source
                                        TxValidationPriority::high,  // tx validation priority
                                        std::move(ptx), // a pointer to the tx
                                        GetTime(),      // nAcceptTime
                                        true,           // fLimitFree
                                        Amount(0),      // nAbsurdFee
                                        pfrom));        // pNode
    } else {
        // Always relay transactions received from whitelisted peers,
        // even if they were already in the mempool or rejected from it
        // due to policy, allowing the node to function as a gateway for
        // nodes hidden behind it.
        bool fWhiteListForceRelay {
                gArgs.GetBoolArg("-whitelistforcerelay",
                                DEFAULT_WHITELISTFORCERELAY)
        };
        if (pfrom->fWhitelisted && fWhiteListForceRelay) {
            RelayTransaction(*ptx, connman);
            LogPrint(BCLog::TXNVAL,
                    "%s: Force relaying tx %s from whitelisted peer=%d\n",
                     enum_cast<std::string>(TxSource::p2p),
                     ptx->GetId().ToString(),
                     pfrom->GetId());
        }
    }
}
 
/**
* Process headers message.
*/
static bool ProcessHeadersMessage(const Config& config, const CNodePtr& pfrom,
    const CNetMsgMaker& msgMaker, const CChainParams& chainparams, CDataStream& vRecv,
    CConnman& connman)
{
    std::vector<CBlockHeader> headers;

    // Bypass the normal CBlock deserialization, as we don't want to risk
    // deserializing 2000 full blocks.
    unsigned int nCount = ReadCompactSize(vRecv);
    if(nCount > MAX_HEADERS_RESULTS) {
        Misbehaving(pfrom, 20, "too-many-headers");
        return error("headers message size = %u", nCount);
    }
    headers.resize(nCount);
    for(unsigned int n = 0; n < nCount; n++) {
        vRecv >> headers[n];
        // Ignore tx count; assume it is 0.
        ReadCompactSize(vRecv);
    }

    if(nCount == 0) {
        // Nothing interesting. Stop asking this peers for more headers.
        return true;
    }

    const CBlockIndex *pindexLast = nullptr;
    {
        LOCK(cs_main);

        // If this looks like it could be a block announcement (nCount <
        // MAX_BLOCKS_TO_ANNOUNCE), use special logic for handling headers
        // that
        // don't connect:
        // - Send a getheaders message in response to try to connect the
        // chain.
        // - The peer can send up to MAX_UNCONNECTING_HEADERS in a row that
        //   don't connect before giving DoS points
        // - Once a headers message is received that is valid and does
        // connect,
        //   nUnconnectingHeaders gets reset back to 0.
        if(mapBlockIndex.find(headers[0].hashPrevBlock) == mapBlockIndex.end() && nCount < MAX_BLOCKS_TO_ANNOUNCE)
        {
            // Try to obtain an access to the node's state data.
            const CNodeStateRef nodestateRef { GetState(pfrom->GetId()) };
            const CNodeStatePtr& nodestate { nodestateRef.get() };
            assert(nodestate);

            nodestate->nUnconnectingHeaders++;
            connman.PushMessage(
                pfrom,
                msgMaker.Make(NetMsgType::GETHEADERS,
                              chainActive.GetLocator(pindexBestHeader),
                              uint256()));
            LogPrint(BCLog::NET, "received header %s: missing prev block "
                                 "%s, sending getheaders (%d) to end "
                                 "(peer=%d, nUnconnectingHeaders=%d)\n",
                     headers[0].GetHash().ToString(),
                     headers[0].hashPrevBlock.ToString(),
                     pindexBestHeader->nHeight, pfrom->id,
                     nodestate->nUnconnectingHeaders);
            // Set hashLastUnknownBlock for this peer, so that if we
            // eventually get the headers - even from a different peer -
            // we can use this peer to download.
            UpdateBlockAvailability(headers.back().GetHash(), nodestate);

            if(nodestate->nUnconnectingHeaders % MAX_UNCONNECTING_HEADERS == 0) {
                // The peer is sending us many headers we can't connect.
                Misbehaving(pfrom, 20, "too-many-unconnected-headers");
            }
            return true;
        }

        uint256 hashLastBlock;
        for(const CBlockHeader &header : headers)
        {
            if(!hashLastBlock.IsNull() && header.hashPrevBlock != hashLastBlock) {
                Misbehaving(pfrom, 20, "disconnected-header");
                return error("non-continuous headers sequence");
            }
            hashLastBlock = header.GetHash();
        }
    }

    CValidationState state;
    if(!ProcessNewBlockHeaders(config, headers, state, &pindexLast))
    {
        int nDoS;
        if(state.IsInvalid(nDoS)) {
            if(nDoS > 0) {
                Misbehaving(pfrom, nDoS, state.GetRejectReason());
            }
            return error("invalid header received");
        }
        // safety net: if the first block header is not accepted but the state is not marked
        // as invalid pindexLast will stay null
        // in that case we have nothing to do...
        if (pindexLast == nullptr)
        {
            return error("first header is not accepted");
        }
    }

    {
        LOCK(cs_main);
        // Try to obtain an access to the node's state data.
        const CNodeStateRef nodestateRef { GetState(pfrom->GetId()) };
        const CNodeStatePtr& nodestate { nodestateRef.get() };
        assert(nodestate);

        if(nodestate->nUnconnectingHeaders > 0) {
            LogPrint(BCLog::NET,
                     "peer=%d: resetting nUnconnectingHeaders (%d -> 0)\n",
                     pfrom->id, nodestate->nUnconnectingHeaders);
        }
        nodestate->nUnconnectingHeaders = 0;

        assert(pindexLast);
        UpdateBlockAvailability(pindexLast->GetBlockHash(), nodestate);

        if(nCount == MAX_HEADERS_RESULTS)
        {
            // Headers message had its maximum size; the peer may have more headers.
            // TODO: optimize: if pindexLast is an ancestor of chainActive.Tip
            // or pindexBestHeader, continue from there instead.
            LogPrint(
                BCLog::NET,
                "more getheaders (%d) to end to peer=%d (startheight:%d)\n",
                pindexLast->nHeight, pfrom->id, pfrom->nStartingHeight);
            connman.PushMessage(
                pfrom,
                msgMaker.Make(NetMsgType::GETHEADERS,
                              chainActive.GetLocator(pindexLast),
                              uint256()));
        }

        bool fCanDirectFetch = CanDirectFetch(chainparams.GetConsensus());
        // If this set of headers is valid and ends in a block with at least
        // as much work as our tip, download as much as possible.
        if(fCanDirectFetch && pindexLast->IsValid(BlockValidity::TREE) &&
            chainActive.Tip()->nChainWork <= pindexLast->nChainWork)
        {
            std::vector<const CBlockIndex*> vToFetch;
            const CBlockIndex *pindexWalk = pindexLast;
            // Calculate all the blocks we'd need to switch to pindexLast,
            // up to a limit.
            while(pindexWalk && !chainActive.Contains(pindexWalk) && vToFetch.size() <= MAX_BLOCKS_IN_TRANSIT_PER_PEER)
            {
                if(!pindexWalk->nStatus.hasData() && !mapBlocksInFlight.count(pindexWalk->GetBlockHash())) {
                    // We don't have this block, and it's not yet in flight.
                    vToFetch.push_back(pindexWalk);
                }
                pindexWalk = pindexWalk->pprev;
            }
            // If pindexWalk still isn't on our main chain, we're looking at
            // a very large reorg at a time we think we're close to caught
            // up to the main chain -- this shouldn't really happen. Bail
            // out on the direct fetch and rely on parallel download
            // instead.
            if(!chainActive.Contains(pindexWalk)) {
                LogPrint(BCLog::NET,
                         "Large reorg, won't direct fetch to %s (%d)\n",
                         pindexLast->GetBlockHash().ToString(),
                         pindexLast->nHeight);
            }
            else {
                std::vector<CInv> vGetData;
                // Download as much as possible, from earliest to latest.
                for(const CBlockIndex *pindex : boost::adaptors::reverse(vToFetch)) {
                    if(nodestate->nBlocksInFlight >= MAX_BLOCKS_IN_TRANSIT_PER_PEER) {
                        // Can't download any more from this peer
                        break;
                    }
                    vGetData.push_back(
                        CInv(MSG_BLOCK, pindex->GetBlockHash()));
                    MarkBlockAsInFlight(
                        config,
                        pfrom->GetId(),
                        pindex->GetBlockHash(),
                        chainparams.GetConsensus(),
                        nodestate,
                        *pindex);
                    LogPrint(BCLog::NET,
                             "Requesting block %s from  peer=%d\n",
                             pindex->GetBlockHash().ToString(), pfrom->id);
                }
                if(vGetData.size() > 1) {
                    LogPrint(BCLog::NET, "Downloading blocks toward %s "
                                         "(%d) via headers direct fetch\n",
                             pindexLast->GetBlockHash().ToString(),
                             pindexLast->nHeight);
                }
                if(vGetData.size() > 0) {
                    if(nodestate->fSupportsDesiredCmpctVersion &&
                        vGetData.size() == 1 &&
                        mapBlocksInFlight.size() == 1 &&
                        pindexLast->pprev->IsValid(BlockValidity::CHAIN)) {
                        // In any case, we want to download using a compact
                        // block, not a regular one.
                        vGetData[0] = CInv(MSG_CMPCT_BLOCK, vGetData[0].hash);
                    }
                    connman.PushMessage(
                        pfrom,
                        msgMaker.Make(NetMsgType::GETDATA, vGetData));
                }
            }
        }
    }

    return true;
}
 
/**
* Process block txn message.
*/
static void ProcessBlockTxnMessage(const Config& config, const CNodePtr& pfrom,
    const CNetMsgMaker& msgMaker, CDataStream& vRecv, CConnman& connman)
{
    BlockTransactions resp;
    vRecv >> resp;

    std::shared_ptr<CBlock> pblock = std::make_shared<CBlock>();
    bool fBlockRead = false;
    {
        LOCK(cs_main);

        std::map<uint256, std::pair<NodeId, std::list<QueuedBlock>::iterator>>::iterator it =
            mapBlocksInFlight.find(resp.blockhash);
        if(it == mapBlocksInFlight.end() || !it->second.second->partialBlock || it->second.first != pfrom->GetId()) {
            LogPrint(BCLog::NET,
                     "Peer %d sent us block transactions for block we weren't expecting\n",
                     pfrom->id);
            return;
        }

        PartiallyDownloadedBlock &partialBlock = *it->second.second->partialBlock;
        ReadStatus status = partialBlock.FillBlock(*pblock, resp.txn, it->second.second->blockIndex.nHeight);
        if(status == READ_STATUS_INVALID) {
            // Reset in-flight state in case of whitelist.
            MarkBlockAsReceived(resp.blockhash);
            Misbehaving(pfrom, 100, "invalid-cmpctblk-txns");
            LogPrintf("Peer %d sent us invalid compact block/non-matching block transactions\n",
                      pfrom->id);
            return;
        }
        else if(status == READ_STATUS_FAILED) {
            // Might have collided, fall back to getdata now :(
            std::vector<CInv> invs;
            invs.push_back(CInv(MSG_BLOCK, resp.blockhash));
            connman.PushMessage(pfrom,
                                msgMaker.Make(NetMsgType::GETDATA, invs));
        }
        else {
            // Block is either okay, or possibly we received
            // READ_STATUS_CHECKBLOCK_FAILED.
            // Note that CheckBlock can only fail for one of a few reasons:
            // 1. bad-proof-of-work (impossible here, because we've already
            //    accepted the header)
            // 2. merkleroot doesn't match the transactions given (already
            //    caught in FillBlock with READ_STATUS_FAILED, so
            //    impossible here)
            // 3. the block is otherwise invalid (eg invalid coinbase,
            //    block is too big, too many legacy sigops, etc).
            // So if CheckBlock failed, #3 is the only possibility.
            // Under BIP 152, we don't DoS-ban unless proof of work is
            // invalid (we don't require all the stateless checks to have
            // been run). This is handled below, so just treat this as
            // though the block was successfully read, and rely on the
            // handling in ProcessNewBlock to ensure the block index is
            // updated, reject messages go out, etc.

            // it is now an empty pointer
            MarkBlockAsReceived(resp.blockhash);
            fBlockRead = true;
            // mapBlockSource is only used for sending reject messages and
            // DoS scores, so the race between here and cs_main in
            // ProcessNewBlock is fine. BIP 152 permits peers to relay
            // compact blocks after validating the header only; we should
            // not punish peers if the block turns out to be invalid.
            mapBlockSource.emplace(resp.blockhash, std::make_pair(pfrom->GetId(), false));
        }
    } // Don't hold cs_main when we call into ProcessNewBlock

    if(fBlockRead) {
        bool fNewBlock = false;
        auto source = task::CCancellationSource::Make();
        // Since we requested this block (it was in mapBlocksInFlight),
        // force it to be processed, even if it would not be a candidate for
        // new tip (missing previous block, chain not long enough, etc)
        auto bestChainActivation =
            ProcessNewBlockWithAsyncBestChainActivation(
                task::CCancellationToken::JoinToken(source->GetToken(), GetShutdownToken()), config, pblock, true, &fNewBlock);
        if(!bestChainActivation)
        {
            // something went wrong before we need to activate best chain
            return;
        }

        pfrom->RunAsyncProcessing(
            [fNewBlock, bestChainActivation]
            (std::weak_ptr<CNode> weakFrom)
            {
                bestChainActivation();

                if(fNewBlock)
                {
                    auto pfrom = weakFrom.lock();
                    if(pfrom)
                    {
                        pfrom->nLastBlockTime = GetTime();
                    }
                }
            },
            source);
    }
}
 
/**
* Process compact block message.
*/
static bool ProcessCompactBlockMessage(const Config& config, const CNodePtr& pfrom,
    const CNetMsgMaker& msgMaker, const std::string& strCommand, const CChainParams& chainparams,
    const std::atomic<bool>& interruptMsgProc, int64_t nTimeReceived, CDataStream& vRecv,
    CConnman& connman)
{
    CBlockHeaderAndShortTxIDs cmpctblock;
    vRecv >> cmpctblock;

    {
        LOCK(cs_main);

        if(mapBlockIndex.find(cmpctblock.header.hashPrevBlock) == mapBlockIndex.end()) {
            // Doesn't connect (or is genesis), instead of DoSing in
            // AcceptBlockHeader, request deeper headers
            if(!IsInitialBlockDownload()) {
                connman.PushMessage(
                    pfrom,
                    msgMaker.Make(NetMsgType::GETHEADERS,
                                  chainActive.GetLocator(pindexBestHeader),
                                  uint256()));
            }
            return true;
        }
    }

    const CBlockIndex *pindex = nullptr;
    CValidationState state;
    if(!ProcessNewBlockHeaders(config, {cmpctblock.header}, state, &pindex)) {
        int nDoS;
        if(state.IsInvalid(nDoS)) {
            if (nDoS > 0) {
                LogPrintf("Peer %d sent us invalid header via cmpctblock\n", pfrom->id);
                Misbehaving(pfrom, nDoS, state.GetRejectReason());
            }
            else {
                LogPrint(BCLog::NET, "Peer %d sent us invalid header via cmpctblock\n", pfrom->id);
            }
            return true;
        }
        
        // safety net: if the first block header is not accepted but the state is not marked
        // as invalid pindexLast will stay null
        // in that case we have nothing to do...
        if (pindex == nullptr)
        {
            return error("header is not accepted");
        }

    }

    // When we succeed in decoding a block's txids from a cmpctblock
    // message we typically jump to the BLOCKTXN handling code, with a
    // dummy (empty) BLOCKTXN message, to re-use the logic there in
    // completing processing of the putative block (without cs_main).
    bool fProcessBLOCKTXN = false;
    CDataStream blockTxnMsg(SER_NETWORK, PROTOCOL_VERSION);

    // If we end up treating this as a plain headers message, call that as
    // well without cs_main.
    bool fRevertToHeaderProcessing = false;
    CDataStream vHeadersMsg(SER_NETWORK, PROTOCOL_VERSION);

    // Keep a CBlock for "optimistic" compactblock reconstructions (see below)
    std::shared_ptr<CBlock> pblock = std::make_shared<CBlock>();
    bool fBlockReconstructed = false;

    {
        LOCK(cs_main);
        // If AcceptBlockHeader returned true, it set pindex
        assert(pindex);
        // Try to obtain an access to the node's state data.
        const CNodeStateRef nodestateRef { GetState(pfrom->GetId()) };
        const CNodeStatePtr& nodestate { nodestateRef.get() };
        assert(nodestate);
        // Update block's availability.
        UpdateBlockAvailability(pindex->GetBlockHash(), nodestate);

        std::map<uint256,
                 std::pair<NodeId, std::list<QueuedBlock>::iterator>>::
            iterator blockInFlightIt =
                mapBlocksInFlight.find(pindex->GetBlockHash());
        bool fAlreadyInFlight = blockInFlightIt != mapBlocksInFlight.end();

        if(pindex->nStatus.hasData()) {
            // Nothing to do here
            return true;
        }

        if(pindex->nChainWork <=
                chainActive.Tip()->nChainWork || // We know something better
            pindex->nTx != 0) {
            // We had this block at some point, but pruned it
            if(fAlreadyInFlight) {
                // We requested this block for some reason, but our mempool
                // will probably be useless so we just grab the block via
                // normal getdata.
                std::vector<CInv> vInv(1);
                vInv[0] = CInv(MSG_BLOCK, cmpctblock.header.GetHash());
                connman.PushMessage(
                    pfrom, msgMaker.Make(NetMsgType::GETDATA, vInv));
            }
            return true;
        }

        // If we're not close to tip yet, give up and let parallel block
        // fetch work its magic.
        if(!fAlreadyInFlight && !CanDirectFetch(chainparams.GetConsensus())) {
            return true;
        }

        // We want to be a bit conservative just to be extra careful about
        // DoS possibilities in compact block processing...
        if(pindex->nHeight <= chainActive.Height() + 2)
        {
            if((!fAlreadyInFlight &&
                 nodestate->nBlocksInFlight < MAX_BLOCKS_IN_TRANSIT_PER_PEER) ||
                (fAlreadyInFlight && blockInFlightIt->second.first == pfrom->GetId()))
            {
                std::list<QueuedBlock>::iterator *queuedBlockIt = nullptr;
                if(!MarkBlockAsInFlight(
                        config,
                        pfrom->GetId(),
                        pindex->GetBlockHash(),
                        chainparams.GetConsensus(),
                        nodestate,
                        *pindex,
                        &queuedBlockIt))
                {
                    if(!(*queuedBlockIt)->partialBlock)
                    {
                        (*queuedBlockIt)->partialBlock.reset(
                                new PartiallyDownloadedBlock(config, &mempool));
                    }
                    else
                    {
                        // The block was already in flight using compact blocks from the same peer.
                        LogPrint(BCLog::NET, "Peer sent us compact block we were already syncing!\n");
                        return true;
                    }
                }

                PartiallyDownloadedBlock& partialBlock = *(*queuedBlockIt)->partialBlock;
                ReadStatus status = partialBlock.InitData(cmpctblock, g_connman->GetCompactExtraTxns());
                if(status == READ_STATUS_INVALID) {
                    // Reset in-flight state in case of whitelist
                    MarkBlockAsReceived(pindex->GetBlockHash());
                    Misbehaving(pfrom, 100, "invalid-cmpctblk");
                    LogPrintf("Peer %d sent us invalid compact block\n", pfrom->id);
                    return true;
                }
                else if(status == READ_STATUS_FAILED) {
                    // Duplicate txindices, the block is now in-flight, so just request it.
                    std::vector<CInv> vInv(1);
                    vInv[0] = CInv(MSG_BLOCK, cmpctblock.header.GetHash());
                    connman.PushMessage(pfrom, msgMaker.Make(NetMsgType::GETDATA, vInv));
                    return true;
                }

                BlockTransactionsRequest req;
                for(size_t i = 0; i < cmpctblock.BlockTxCount(); i++) {
                    if(!partialBlock.IsTxAvailable(i)) {
                        req.indices.push_back(i);
                    }
                }
                if(req.indices.empty()) {
                    // Dirty hack to jump to BLOCKTXN code (TODO: move
                    // message handling into their own functions)
                    BlockTransactions txn;
                    txn.blockhash = cmpctblock.header.GetHash();
                    blockTxnMsg << txn;
                    fProcessBLOCKTXN = true;
                }
                else {
                    req.blockhash = pindex->GetBlockHash();
                    connman.PushMessage(pfrom, msgMaker.Make(NetMsgType::GETBLOCKTXN, req));
                }
            }
            else
            {
                // This block is either already in flight from a different
                // peer, or this peer has too many blocks outstanding to
                // download from. Optimistically try to reconstruct anyway
                // since we might be able to without any round trips.
                PartiallyDownloadedBlock tempBlock(config, &mempool);
                ReadStatus status = tempBlock.InitData(cmpctblock, g_connman->GetCompactExtraTxns());
                if(status != READ_STATUS_OK) {
                    // TODO: don't ignore failures
                    return true;
                }
                std::vector<CTransactionRef> dummy;
                status = tempBlock.FillBlock(*pblock, dummy, pindex->nHeight);
                if(status == READ_STATUS_OK) {
                    fBlockReconstructed = true;
                }
            }
        }
        else
        {
            if(fAlreadyInFlight) {
                // We requested this block, but its far into the future, so
                // our mempool will probably be useless - request the block
                // normally.
                std::vector<CInv> vInv(1);
                vInv[0] = CInv(MSG_BLOCK, cmpctblock.header.GetHash());
                connman.PushMessage(pfrom, msgMaker.Make(NetMsgType::GETDATA, vInv));
                return true;
            }
            else {
                // If this was an announce-cmpctblock, we want the same
                // treatment as a header message. Dirty hack to process as
                // if it were just a headers message (TODO: move message
                // handling into their own functions)
                std::vector<CBlock> headers;
                headers.push_back(cmpctblock.header);
                vHeadersMsg << headers;
                fRevertToHeaderProcessing = true;
            }
        }
    } // cs_main

    if(fProcessBLOCKTXN) {
        ProcessBlockTxnMessage(config, pfrom, msgMaker, blockTxnMsg, connman);
        return true;
    }

    if(fRevertToHeaderProcessing) {
        return ProcessHeadersMessage(config, pfrom, msgMaker, chainparams, vHeadersMsg, connman);
    }

    if(fBlockReconstructed) {
        // If we got here, we were able to optimistically reconstruct a
        // block that is in flight from some other peer.
        {
            LOCK(cs_main);
            mapBlockSource.emplace(pblock->GetHash(),
                                   std::make_pair(pfrom->GetId(), false));
        }

        bool fNewBlock = false;
        auto source = task::CCancellationSource::Make();
        auto bestChainActivation =
            ProcessNewBlockWithAsyncBestChainActivation(
                task::CCancellationToken::JoinToken(source->GetToken(), GetShutdownToken()), config, pblock, true, &fNewBlock);
        if(bestChainActivation)
        {
            pfrom->RunAsyncProcessing(
                [pindex, pblock, fNewBlock, bestChainActivation]
                (std::weak_ptr<CNode> weakFrom)
                {
                    bestChainActivation();

                    if(fNewBlock)
                    {
                        auto pfrom = weakFrom.lock();
                        if(pfrom)
                        {
                            pfrom->nLastBlockTime = GetTime();
                        }
                    }

                    // hold cs_main for CBlockIndex::IsValid()
                    LOCK(cs_main);
                    if(pindex->IsValid(BlockValidity::TRANSACTIONS))
                    {
                        // Clear download state for this block, which is in process from
                        // some other peer. We do this after calling. ProcessNewBlock so
                        // that a malleated cmpctblock announcement can't be used to
                        // interfere with block relay.
                        MarkBlockAsReceived(pblock->GetHash());
                    }
                },
                source);
        }
        // else something went wrong before we need to activate best chain
        // so we just skip it
    }

    return true;
}
 
/**
* Process block message.
*/
static void ProcessBlockMessage(const Config& config, const CNodePtr& pfrom, CDataStream& vRecv)
{
    std::shared_ptr<CBlock> pblock = std::make_shared<CBlock>();
    vRecv >> *pblock;

    LogPrint(BCLog::NET, "received block %s peer=%d\n", pblock->GetHash().ToString(), pfrom->id);

    // Process all blocks from whitelisted peers, even if not requested,
    // unless we're still syncing with the network. Such an unrequested
    // block may still be processed, subject to the conditions in
    // AcceptBlock().
    bool forceProcessing = pfrom->fWhitelisted && !IsInitialBlockDownload();
    const uint256 hash(pblock->GetHash());
    {
        LOCK(cs_main);
        // Also always process if we requested the block explicitly, as we
        // may need it even though it is not a candidate for a new best tip.
        forceProcessing |= MarkBlockAsReceived(hash);
        // mapBlockSource is only used for sending reject messages and DoS
        // scores, so the race between here and cs_main in ProcessNewBlock
        // is fine.
        mapBlockSource.emplace(hash, std::make_pair(pfrom->GetId(), true));
    }

    bool fNewBlock = false;
    auto source = task::CCancellationSource::Make();
    auto bestChainActivation =
        ProcessNewBlockWithAsyncBestChainActivation(
            task::CCancellationToken::JoinToken(source->GetToken(), GetShutdownToken()), config, pblock, forceProcessing, &fNewBlock);
    if(!bestChainActivation)
    {
        // something went wrong before we need to activate best chain
        return;
    }

    pfrom->RunAsyncProcessing(
        [pblock, fNewBlock, bestChainActivation]
        (std::weak_ptr<CNode> weakFrom)
        {
            bestChainActivation();

            if(fNewBlock)
            {
                auto pfrom = weakFrom.lock();
                if(pfrom)
                {
                    pfrom->nLastBlockTime = GetTime();
                }
            }
        },
        source);
}
 
/**
* Process getaddr message.
*/
static void ProcessGetAddrMessage(const CNodePtr& pfrom,
                                  CDataStream& vRecv,
                                  CConnman& connman)
{
    // This asymmetric behavior for inbound and outbound connections was
    // introduced to prevent a fingerprinting attack: an attacker can send
    // specific fake addresses to users' AddrMan and later request them by
    // sending getaddr messages. Making nodes which are behind NAT and can
    // only make outgoing connections ignore the getaddr message mitigates
    // the attack.
    if(!pfrom->fInbound) {
        LogPrint(BCLog::NET,
                 "Ignoring \"getaddr\" from outbound connection. peer=%d\n",
                 pfrom->id);
        return;
    }

    // Only send one GetAddr response per connection to reduce resource
    // waste and discourage addr stamping of INV announcements.
    if(pfrom->fSentAddr) {
        LogPrint(BCLog::NET, "Ignoring repeated \"getaddr\". peer=%d\n",
                 pfrom->id);
        return;
    }
    pfrom->fSentAddr = true;

    pfrom->vAddrToSend.clear();
    std::vector<CAddress> vAddr = connman.GetAddresses();
    FastRandomContext insecure_rand;
    for(const CAddress& addr : vAddr) {
        pfrom->PushAddress(addr, insecure_rand);
    }
}
 
/**
* Process mempool message.
*/
static void ProcessMempoolMessage(const CNodePtr& pfrom,
                                  CDataStream& vRecv,
                                  CConnman& connman)
{

    if (gArgs.GetBoolArg("-rejectmempoolrequest", DEFAULT_REJECTMEMPOOLREQUEST) && !pfrom->fWhitelisted) {
        LogPrint(BCLog::NET, "mempool request from nonwhitelisted peer disabled, disconnect peer=%d\n",
                 pfrom->GetId());
        pfrom->fDisconnect = true;
        return;
    }

    if(!(pfrom->GetLocalServices() & NODE_BLOOM) && !pfrom->fWhitelisted) {
        LogPrint(BCLog::NET, "mempool request with bloom filters disabled, disconnect peer=%d\n",
                 pfrom->GetId());
        pfrom->fDisconnect = true;
        return;
    }

    if(connman.OutboundTargetReached(false) && !pfrom->fWhitelisted) {
        LogPrint(BCLog::NET, "mempool request with bandwidth limit reached, disconnect peer=%d\n",
                 pfrom->GetId());
        pfrom->fDisconnect = true;
        return;
    }

    LOCK(pfrom->cs_inventory);
    pfrom->fSendMempool = true;
}
 
/**
* Process ping message.
*/
static void ProcessPingMessage(const CNodePtr& pfrom, const CNetMsgMaker& msgMaker,
    CDataStream& vRecv, CConnman& connman)
{
    if(pfrom->nVersion > BIP0031_VERSION) {
        uint64_t nonce = 0;
        vRecv >> nonce;
        // Echo the message back with the nonce. This allows for two useful
        // features:
        //
        // 1) A remote node can quickly check if the connection is
        // operational.
        // 2) Remote nodes can measure the latency of the network thread. If
        // this node is overloaded it won't respond to pings quickly and the
        // remote node can avoid sending us more work, like chain download
        // requests.
        //
        // The nonce stops the remote getting confused between different
        // pings: without it, if the remote node sends a ping once per
        // second and this node takes 5 seconds to respond to each, the 5th
        // ping the remote sends would appear to return very quickly.
        connman.PushMessage(pfrom, msgMaker.Make(NetMsgType::PONG, nonce));
    }
}

/**
* Process pong message.
*/
static void ProcessPongMessage(const CNodePtr& pfrom, int64_t nTimeReceived, CDataStream& vRecv)
{
    int64_t pingUsecEnd = nTimeReceived;
    uint64_t nonce = 0;
    size_t nAvail = vRecv.in_avail();
    bool bPingFinished = false;
    std::string sProblem;

    if(nAvail >= sizeof(nonce))
    {
        vRecv >> nonce;

        // Only process pong message if there is an outstanding ping (old
        // ping without nonce should never pong)
        if(pfrom->nPingNonceSent != 0)
        {
            if(nonce == pfrom->nPingNonceSent)
            {
                // Matching pong received, this ping is no longer outstanding
                bPingFinished = true;
                int64_t pingUsecTime = pingUsecEnd - pfrom->nPingUsecStart;
                if (pingUsecTime > 0)
                {
                    // Successful ping time measurement, replace previous
                    pfrom->nPingUsecTime = pingUsecTime;
                    pfrom->nMinPingUsecTime = std::min(pfrom->nMinPingUsecTime.load(), pingUsecTime);
                }
                else
                {
                    // This should never happen
                    sProblem = "Timing mishap";
                }
            }
            else
            {
                // Nonce mismatches are normal when pings are overlapping
                sProblem = "Nonce mismatch";
                if(nonce == 0)
                {
                    // This is most likely a bug in another implementation
                    // somewhere; cancel this ping
                    bPingFinished = true;
                    sProblem = "Nonce zero";
                }
            }
        }
        else
        {
            sProblem = "Unsolicited pong without ping";
        }
    }
    else
    {
        // This is most likely a bug in another implementation somewhere;
        // cancel this ping
        bPingFinished = true;
        sProblem = "Short payload";
    }

    if(!(sProblem.empty()))
    {
        LogPrint(BCLog::NET,
                 "pong peer=%d: %s, %x expected, %x received, %u bytes\n",
                 pfrom->id, sProblem, pfrom->nPingNonceSent, nonce, nAvail);
    }
    if(bPingFinished)
    {
        pfrom->nPingNonceSent = 0;
    }
}

/**
* Process filter load message.
*/
static void ProcessFilterLoadMessage(const CNodePtr& pfrom, CDataStream& vRecv)
{
    CBloomFilter filter;
    vRecv >> filter;

    if(!filter.IsWithinSizeConstraints()) {
        // There is no excuse for sending a too-large filter
        Misbehaving(pfrom, 100, "oversized-bloom-filter");
    }
    else {
        LOCK(pfrom->cs_filter);
        pfrom->mFilter = std::move(filter);
        pfrom->mFilter.UpdateEmptyFull();
        pfrom->fRelayTxes = true;
    }
}
 
/**
* Process filter add message.
*/
static void ProcessFilterAddMessage(const CNodePtr& pfrom, CDataStream& vRecv)
{
    std::vector<uint8_t> vData;
    vRecv >> vData;

    // Nodes must NEVER send a data item > 520 bytes (the max size for a
    // script data object, and thus, the maximum size any matched object can
    // have) in a filteradd message.
    if(vData.size() > MAX_SCRIPT_ELEMENT_SIZE_BEFORE_GENESIS) {
        Misbehaving(pfrom, 100, "invalid-filteradd");
    }
    else {
        LOCK(pfrom->cs_filter);
        pfrom->mFilter.insert(vData);
    }
}

/**
* Process filter clear message.
*/
static void ProcessFilterClearMessage(const CNodePtr& pfrom, CDataStream& vRecv)
{
    LOCK(pfrom->cs_filter);
    if(pfrom->GetLocalServices() & NODE_BLOOM) {
        pfrom->mFilter = {};
    }
    pfrom->fRelayTxes = true;
}
 
/**
* Process fee filter message.
*/
static void ProcessFeeFilterMessage(const CNodePtr& pfrom, CDataStream& vRecv)
{
    Amount newFeeFilter(0);
    vRecv >> newFeeFilter;
    if(MoneyRange(newFeeFilter))
    {
        {
            LOCK(pfrom->cs_feeFilter);
            pfrom->minFeeFilter = newFeeFilter;
        }
        LogPrint(BCLog::NET, "received: feefilter of %s from peer=%d\n",
                 CFeeRate(newFeeFilter).ToString(), pfrom->id);
    }
}

/**
* Process protoconf message.
*/
static bool ProcessProtoconfMessage(const CNodePtr& pfrom, CDataStream& vRecv, const std::string& strCommand)
{
    if (pfrom->protoconfReceived) {
        pfrom->fDisconnect = true;
        return false;
    }

    pfrom->protoconfReceived = true;

    CProtoconf protoconf;

    try {
        vRecv >> protoconf; // exception happens if received protoconf cannot be deserialized or if number of fields is zero      
    } catch (const std::ios_base::failure &e) {
        LogPrint(BCLog::NET, "Invalid protoconf received \"%s\" from peer=%d, exception = %s\n",
            SanitizeString(strCommand), pfrom->id, e.what());
        pfrom->fDisconnect = true;
        return false;
    }

    // Parse known fields:
    if (protoconf.numberOfFields >= 1) {
        // if peer sends maxRecvPayloadLength less than LEGACY_MAX_PROTOCOL_PAYLOAD_LENGTH, it is considered protocol violation
        if (protoconf.maxRecvPayloadLength < LEGACY_MAX_PROTOCOL_PAYLOAD_LENGTH) {
            LogPrint(BCLog::NET, "Invalid protoconf received \"%s\" from peer=%d, peer's proposed maximal message size is too low (%d).\n",
            SanitizeString(strCommand), pfrom->id, protoconf.maxRecvPayloadLength);
            pfrom->fDisconnect = true;
            return false;
        }

        // Limit the amount of data we are willing to send to MAX_PROTOCOL_SEND_PAYLOAD_LENGTH if a peer (or an attacker)
        // that is running a newer version sends us large size, that we are not prepared to handle. 
        pfrom->maxInvElements = CInv::estimateMaxInvElements(std::min(MAX_PROTOCOL_SEND_PAYLOAD_LENGTH, protoconf.maxRecvPayloadLength));

        LogPrint(BCLog::NET, "Protoconf received \"%s\" from peer=%d; peer's proposed max message size: %d," 
            "absolute maximal allowed message size: %d, calculated maximal number of Inv elements in a message = %d\n",
            SanitizeString(strCommand), pfrom->id, protoconf.maxRecvPayloadLength, MAX_PROTOCOL_SEND_PAYLOAD_LENGTH, pfrom->maxInvElements);
    }

    return true;
}
 
/**
* Process next message.
*/
static bool ProcessMessage(const Config& config, const CNodePtr& pfrom,
                           const std::string& strCommand, CDataStream& vRecv,
                           int64_t nTimeReceived,
                           const CChainParams& chainparams, CConnman& connman,
                           const std::atomic<bool>& interruptMsgProc)
{
    LogPrint(BCLog::NET, "received: %s (%u bytes) peer=%d\n",
             SanitizeString(strCommand), vRecv.size(), pfrom->id);
    if (gArgs.IsArgSet("-dropmessagestest") && GetRand(gArgs.GetArg("-dropmessagestest", 0)) == 0) {
        LogPrintf("dropmessagestest DROPPING RECV MESSAGE\n");
        return true;
    }

    if (!(pfrom->GetLocalServices() & NODE_BLOOM) &&
        (strCommand == NetMsgType::FILTERLOAD || strCommand == NetMsgType::FILTERADD)) {
        if (pfrom->nVersion >= NO_BLOOM_VERSION) {
            Misbehaving(pfrom, 100, "no-bloom-version");
            return false;
        } else {
            pfrom->fDisconnect = true;
            return false;
        }
    }

    if (strCommand == NetMsgType::REJECT) {
        ProcessRejectMessage(vRecv, pfrom);
        return true;
    }

    else if (strCommand == NetMsgType::VERSION) {
        return ProcessVersionMessage(pfrom, strCommand, vRecv, connman, config);
    }

    else if (pfrom->nVersion == 0) {
        // Must have a version message before anything else
        Misbehaving(pfrom, 1, "missing-version");
        return false;
    }

    // At this point, the outgoing message serialization version can't change.
    const CNetMsgMaker msgMaker(pfrom->GetSendVersion());

    if (strCommand == NetMsgType::VERACK) {
        ProcessVerAckMessage(pfrom, msgMaker, connman);
    }

    else if (!pfrom->fSuccessfullyConnected) {
        // Must have a verack message before anything else
        Misbehaving(pfrom, 1, "missing-verack");
        return false;
    }

    else if (strCommand == NetMsgType::ADDR) {
        return ProcessAddrMessage(pfrom, interruptMsgProc, vRecv, connman);
    }

    else if (strCommand == NetMsgType::SENDHEADERS) {
        ProcessSendHeadersMessage(pfrom);
    }

    else if (strCommand == NetMsgType::SENDCMPCT) {
        ProcessSendCompactMessage(pfrom, vRecv);
    }

    else if (strCommand == NetMsgType::INV) {
        ProcessInvMessage(pfrom, msgMaker, interruptMsgProc, vRecv, connman);
    }

    else if (strCommand == NetMsgType::GETDATA) {
        ProcessGetDataMessage(config, pfrom, chainparams,
                              interruptMsgProc, vRecv,
                              connman);
    }

    else if (strCommand == NetMsgType::GETBLOCKS) {
        ProcessGetBlocksMessage(config, pfrom, chainparams, vRecv);
    }

    else if (strCommand == NetMsgType::GETBLOCKTXN) {
        ProcessGetBlockTxnMessage(config, pfrom, chainparams, interruptMsgProc, vRecv, connman);
    }

    else if (strCommand == NetMsgType::GETHEADERS) {
        ProcessGetHeadersMessage(pfrom, msgMaker, vRecv, connman);
    }

    else if (strCommand == NetMsgType::TX) {
        ProcessTxMessage(config, pfrom, msgMaker, strCommand, vRecv, connman);
    }

    // Ignore blocks received while importing
    else if (strCommand == NetMsgType::CMPCTBLOCK && !fImporting && !fReindex) {
        return ProcessCompactBlockMessage(config, pfrom, msgMaker, strCommand, chainparams, interruptMsgProc, nTimeReceived, vRecv, connman);
    }

    // Ignore blocks received while importing
    else if (strCommand == NetMsgType::BLOCKTXN && !fImporting && !fReindex) {
        ProcessBlockTxnMessage(config, pfrom, msgMaker, vRecv, connman);
    }

    // Ignore headers received while importing
    else if (strCommand == NetMsgType::HEADERS && !fImporting && !fReindex) {
        return ProcessHeadersMessage(config, pfrom, msgMaker, chainparams,
                                     vRecv, connman);
    }

    // Ignore blocks received while importing
    else if (strCommand == NetMsgType::BLOCK && !fImporting && !fReindex) {
        ProcessBlockMessage(config, pfrom, vRecv);
    }

    else if (strCommand == NetMsgType::GETADDR) {
        ProcessGetAddrMessage(pfrom, vRecv, connman);
    }

    else if (strCommand == NetMsgType::MEMPOOL) {
        ProcessMempoolMessage(pfrom, vRecv, connman);
    }

    else if (strCommand == NetMsgType::PING) {
        ProcessPingMessage(pfrom, msgMaker, vRecv, connman);
    }

    else if (strCommand == NetMsgType::PONG) {
        ProcessPongMessage(pfrom, nTimeReceived, vRecv);
    }

    else if (strCommand == NetMsgType::FILTERLOAD) {
        ProcessFilterLoadMessage(pfrom, vRecv);
    }

    else if (strCommand == NetMsgType::FILTERADD) {
        ProcessFilterAddMessage(pfrom, vRecv);
    }

    else if (strCommand == NetMsgType::FILTERCLEAR) {
        ProcessFilterClearMessage(pfrom, vRecv);
    }

    else if (strCommand == NetMsgType::FEEFILTER) {
        ProcessFeeFilterMessage(pfrom, vRecv);
    }

    else if (strCommand == NetMsgType::PROTOCONF) {
        return ProcessProtoconfMessage(pfrom, vRecv, strCommand);
    }

    else if (strCommand == NetMsgType::NOTFOUND) {
        // We do not care about the NOTFOUND message, but logging an Unknown
        // Command message would be undesirable as we transmit it ourselves.
    }

    else {
        // Ignore unknown commands for extensibility
        LogPrint(BCLog::NET, "Unknown command \"%s\" from peer=%d\n",
                 SanitizeString(strCommand), pfrom->id);
    }

    return true;
}

static bool SendRejectsAndCheckIfBanned(const CNodePtr& pnode, CConnman &connman) {
    // Try to obtain an access to the node's state data.
    const CNodeStateRef stateRef { GetState(pnode->GetId()) };
    const CNodeStatePtr& state { stateRef.get() };
    assert(state);

    for (const CBlockReject &reject : state->rejects) {
        connman.PushMessage(
            pnode,
            CNetMsgMaker(INIT_PROTO_VERSION)
                .Make(NetMsgType::REJECT, std::string(NetMsgType::BLOCK),
                      reject.chRejectCode, reject.strRejectReason,
                      reject.hashBlock));
    }
    state->rejects.clear();

    if (state->fShouldBan) {
        state->fShouldBan = false;
        if (pnode->fWhitelisted) {
            LogPrintf("Warning: not punishing whitelisted peer %s!\n",
                      pnode->addr.ToString());
        } else if (pnode->fAddnode) {
            LogPrintf("Warning: not punishing addnoded peer %s!\n",
                      pnode->addr.ToString());
        } else {
            pnode->fDisconnect = true;
            if (pnode->addr.IsLocal()) {
                LogPrintf("Warning: not banning local peer %s!\n",
                          pnode->addr.ToString());
            } else {
                connman.Ban(pnode->addr, BanReasonNodeMisbehaving);
            }
        }
        return true;
    }
    return false;
}

bool ProcessMessages(const Config &config, const CNodePtr& pfrom, CConnman &connman,
                     const std::atomic<bool> &interruptMsgProc) {
    const CChainParams &chainparams = config.GetChainParams();
    //
    // Message format
    //  (4) message start
    //  (12) command
    //  (4) size
    //  (4) checksum
    //  (x) data
    //
    bool fMoreWork = false;

    if (pfrom->mGetBlockMessageRequest)
    {
        if(!ProcessGetBlocks(config, pfrom, chainparams, *pfrom->mGetBlockMessageRequest))
        {
            // this maintains the order of responses
            return false;
        }

        pfrom->mGetBlockMessageRequest = std::nullopt;
    }

    if (!pfrom->vRecvGetData.empty()) {
        ProcessGetData(config, pfrom, chainparams.GetConsensus(), connman,
                       interruptMsgProc);
    }

    if (pfrom->fDisconnect) {
        return false;
    }

    // this maintains the order of responses
    if (!pfrom->vRecvGetData.empty()) {
        return true;
    }

    // Don't bother if send buffer is too full to respond anyway
    if (pfrom->fPauseSend) {
        return false;
    }

    std::list<CNetMessage> msgs;
    {
        LOCK(pfrom->cs_vProcessMsg);
        if (pfrom->vProcessMsg.empty()) {
            return false;
        }
        // Just take one message
        msgs.splice(msgs.begin(), pfrom->vProcessMsg,
                    pfrom->vProcessMsg.begin());
        pfrom->nProcessQueueSize -=
            msgs.front().vRecv.size() + CMessageHeader::HEADER_SIZE;
        pfrom->fPauseRecv =
            pfrom->nProcessQueueSize > connman.GetReceiveFloodSize();
        fMoreWork = !pfrom->vProcessMsg.empty();
    }
    CNetMessage &msg(msgs.front());

    msg.SetVersion(pfrom->GetRecvVersion());

    // Scan for message start
    if (memcmp(msg.hdr.pchMessageStart.data(),
               chainparams.NetMagic().data(),
               CMessageHeader::MESSAGE_START_SIZE) != 0) {
        LogPrint(BCLog::NET, "PROCESSMESSAGE: INVALID MESSAGESTART %s peer=%d\n",
                  SanitizeString(msg.hdr.GetCommand()), pfrom->id);

        // Make sure we ban where that come from for some time.
        connman.Ban(pfrom->addr, BanReasonNodeMisbehaving);

        pfrom->fDisconnect = true;
        return false;
    }

    // Read header
    CMessageHeader &hdr = msg.hdr;
    if (!hdr.IsValid(config)) {
        LogPrint(BCLog::NET, "PROCESSMESSAGE: ERRORS IN HEADER %s peer=%d\n",
                  SanitizeString(hdr.GetCommand()), pfrom->id);
        return fMoreWork;
    }
    std::string strCommand = hdr.GetCommand();

    // Message size
    unsigned int nPayloadLength = hdr.nPayloadLength;

    // Checksum
    CDataStream &vRecv = msg.vRecv;
    const uint256 &hash = msg.GetMessageHash();
    if (memcmp(hash.begin(), hdr.pchChecksum, CMessageHeader::CHECKSUM_SIZE) !=0) {
        LogPrint(BCLog::NET,
            "%s(%s, %u bytes): CHECKSUM ERROR expected %s was %s\n", __func__,
            SanitizeString(strCommand), nPayloadLength,
            HexStr(hash.begin(), hash.begin() + CMessageHeader::CHECKSUM_SIZE),
            HexStr(hdr.pchChecksum,
                   hdr.pchChecksum + CMessageHeader::CHECKSUM_SIZE));
        {
            // Try to obtain an access to the node's state data.
            const CNodeStateRef stateRef { GetState(pfrom->GetId()) };
            const CNodeStatePtr& state { stateRef.get() };
            if (state){
                auto curTime = std::chrono::system_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(curTime - state->nTimeOfLastInvalidChecksumHeader).count();
                unsigned int interval = gArgs.GetArg("-invalidcsinterval", DEFAULT_MIN_TIME_INTERVAL_CHECKSUM_MS);
                std::chrono::milliseconds checksumInterval(interval); 
                if (duration < std::chrono::milliseconds(checksumInterval).count()){
                    ++ state->dInvalidChecksumFrequency;
                }
                else { 
                    // reset the frequency as this invalid checksum is outside the MIN_INTERVAL
                    state->dInvalidChecksumFrequency = 0;
                }
                unsigned int checkSumFreq = gArgs.GetArg ("-invalidcsfreq", DEFAULT_INVALID_CHECKSUM_FREQUENCY);
                if (state->dInvalidChecksumFrequency > checkSumFreq){
                    // MisbehavingNode if the count goes above some chosen value 
                    // 100 conseqitive invalid checksums received with less than 500ms between them
                    Misbehaving(pfrom, 1, "Invalid Checksum activity");
                    LogPrint(BCLog::NET, "Peer %d showing increased invalid checksum activity\n",pfrom->id);
                }
                state->nTimeOfLastInvalidChecksumHeader = curTime;
            }
        }
        return fMoreWork;
    }

    // Process message
    bool fRet = false;
    try {
        fRet = ProcessMessage(config, pfrom, strCommand, vRecv, msg.nTime,
                              chainparams, connman, interruptMsgProc);
        if (interruptMsgProc) {
            return false;
        }
        if (!pfrom->vRecvGetData.empty()) {
            fMoreWork = true;
        }
    } catch (const std::ios_base::failure &e) {
        connman.PushMessage(pfrom,
                            CNetMsgMaker(INIT_PROTO_VERSION)
                            .Make(NetMsgType::REJECT, strCommand,
                                  REJECT_MALFORMED,
                                  std::string("error parsing message")));
        if (strstr(e.what(), "end of data")) {
            // Allow exceptions from under-length message on vRecv
            LogPrint(BCLog::NET,
                     "%s(%s, %u bytes): Exception '%s' caught, normally caused by a "
                     "message being shorter than its stated length\n",
                     __func__, SanitizeString(strCommand), nPayloadLength, e.what());
        } else if (strstr(e.what(), "size too large")) {
            // Allow exceptions from over-long size
            LogPrint(BCLog::NET, "%s(%s, %u bytes): Exception '%s' caught\n", __func__,
                     SanitizeString(strCommand), nPayloadLength, e.what());
            Misbehaving(pfrom, 1, "Over-long size message protection");
        } else if (strstr(e.what(), "non-canonical ReadCompactSize()")) {
            // Allow exceptions from non-canonical encoding
            LogPrint(BCLog::NET, "%s(%s, %u bytes): Exception '%s' caught\n", __func__,
                     SanitizeString(strCommand), nPayloadLength, e.what());
        } else {
            PrintExceptionContinue(&e, "ProcessMessages()");
        }
    } catch (const std::exception &e) {
        PrintExceptionContinue(&e, "ProcessMessages()");
    } catch (...) {
        PrintExceptionContinue(nullptr, "ProcessMessages()");
    }

    if (!fRet) {
        LogPrint(BCLog::NET, "%s(%s, %u bytes) FAILED peer=%d\n", __func__,
                  SanitizeString(strCommand), nPayloadLength, pfrom->id);
    }

    SendRejectsAndCheckIfBanned(pfrom, connman);

    return fMoreWork;
}

void SendPings(const CNodePtr& pto, CConnman &connman, const CNetMsgMaker& msgMaker)
{
    //
    // Message: ping
    //
    bool pingSend = false;
    if (pto->fPingQueued) {
        // RPC ping request by user
        pingSend = true;
    }
    if (pto->nPingNonceSent == 0 &&
        pto->nPingUsecStart + PING_INTERVAL * 1000000 < GetTimeMicros()) {
        // Ping automatically sent as a latency probe & keepalive.
        pingSend = true;
    }
    if (pingSend) {
        uint64_t nonce = 0;
        while (nonce == 0) {
            GetRandBytes((uint8_t *)&nonce, sizeof(nonce));
        }
        pto->fPingQueued = false;
        pto->nPingUsecStart = GetTimeMicros();
        if (pto->nVersion > BIP0031_VERSION) {
            pto->nPingNonceSent = nonce;
            connman.PushMessage(pto, msgMaker.Make(NetMsgType::PING, nonce));
        } else {
            // Peer is too old to support ping command with nonce, pong will
            // never arrive.
            pto->nPingNonceSent = 0;
            connman.PushMessage(pto, msgMaker.Make(NetMsgType::PING));
        }
    }
}

void SendAddrs(const CNodePtr& pto, CConnman &connman, const CNetMsgMaker& msgMaker)
{
    // Address refresh broadcast
    int64_t nNow = GetTimeMicros();
    if (!IsInitialBlockDownload() && pto->nNextLocalAddrSend < nNow) {
        AdvertiseLocal(pto);
        pto->nNextLocalAddrSend =
            PoissonNextSend(nNow, AVG_LOCAL_ADDRESS_BROADCAST_INTERVAL);
    }

    //
    // Message: addr
    //
    if (pto->nNextAddrSend < nNow) {
        pto->nNextAddrSend =
            PoissonNextSend(nNow, AVG_ADDRESS_BROADCAST_INTERVAL);
        std::vector<CAddress> vAddr;
        vAddr.reserve(pto->vAddrToSend.size());
        for (const CAddress &addr : pto->vAddrToSend) {
            if (!pto->addrKnown.contains(addr.GetKey())) {
                pto->addrKnown.insert(addr.GetKey());
                vAddr.push_back(addr);
                // receiver rejects addr messages larger than 1000
                if (vAddr.size() >= 1000) {
                    connman.PushMessage(pto,
                                        msgMaker.Make(NetMsgType::ADDR, vAddr));
                    vAddr.clear();
                }
            }
        }
        if (!vAddr.empty()) {
            connman.PushMessage(pto, msgMaker.Make(NetMsgType::ADDR, vAddr));
        }
        pto->vAddrToSend.clear();

        // we only send the big addr message once
        if (pto->vAddrToSend.capacity() > 40) {
            pto->vAddrToSend.shrink_to_fit();
        }
    }
}

void SendBlockSync(const CNodePtr& pto, CConnman &connman, const CNetMsgMaker& msgMaker,
    const CNodeStatePtr& state)
{
    // Start block sync
    if (pindexBestHeader == nullptr) {
        pindexBestHeader = chainActive.Tip();
    }
    assert(state);
    // Download if this is a nice peer, or we have no nice peers and this one
    // might do.
    bool fFetch = state->fPreferredDownload ||
                  (nPreferredDownload == 0 && !pto->fClient && !pto->fOneShot);

    if (!state->fSyncStarted && !pto->fClient && !fImporting && !fReindex) {
        // Only actively request headers from a single peer, unless we're close
        // to today.
        if ((nSyncStarted == 0 && fFetch) ||
            pindexBestHeader->GetBlockTime() >
                GetAdjustedTime() - 24 * 60 * 60) {
            state->fSyncStarted = true;
            nSyncStarted++;
            const CBlockIndex *pindexStart = pindexBestHeader;
            /**
             * If possible, start at the block preceding the currently best
             * known header. This ensures that we always get a non-empty list of
             * headers back as long as the peer is up-to-date. With a non-empty
             * response, we can initialise the peer's known best block. This
             * wouldn't be possible if we requested starting at pindexBestHeader
             * and got back an empty response.
             */
            if (pindexStart->pprev) {
                pindexStart = pindexStart->pprev;
            }

            LogPrint(BCLog::NET,
                     "initial getheaders (%d) to peer=%d (startheight:%d)\n",
                     pindexStart->nHeight, pto->id, pto->nStartingHeight);
            connman.PushMessage(
                pto,
                msgMaker.Make(NetMsgType::GETHEADERS,
                              chainActive.GetLocator(pindexStart), uint256()));
        }
    }
}

void SendBlockHeaders(const Config &config, const CNodePtr& pto, CConnman &connman,
    const CNetMsgMaker& msgMaker, const CNodeStatePtr& state)
{
    //
    // Try sending block announcements via headers
    //
    assert(state);
    std::vector<CBlock> vHeaders {};

    // If we have less than MAX_BLOCKS_TO_ANNOUNCE in our list of block
    // hashes we're relaying, and our peer wants headers announcements, then
    // find the first header not yet known to our peer but would connect,
    // and send. If no header would connect, or if we have too many blocks,
    // or if the peer doesn't want headers, just add all to the inv queue.
    LOCK(pto->cs_inventory);
    bool fRevertToInv =
        ((!state->fPreferHeaders &&
          (!state->fPreferHeaderAndIDs ||
           pto->vBlockHashesToAnnounce.size() > 1)) ||
         pto->vBlockHashesToAnnounce.size() > MAX_BLOCKS_TO_ANNOUNCE);
    // last header queued for delivery
    const CBlockIndex *pBestIndex = nullptr;
    // ensure pindexBestKnownBlock is up-to-date
    ProcessBlockAvailability(state);

    if (!fRevertToInv) {
        bool fFoundStartingHeader = false;
        // Try to find first header that our peer doesn't have, and then
        // send all headers past that one. If we come across an headers that
        // aren't on chainActive, give up.
        for (const uint256 &hash : pto->vBlockHashesToAnnounce) {
            BlockMap::iterator mi = mapBlockIndex.find(hash);
            assert(mi != mapBlockIndex.end());
            const CBlockIndex *pindex = mi->second;
            if (pindex && chainActive[pindex->nHeight] != pindex) {
                // Bail out if we reorged away from this block
                fRevertToInv = true;
                break;
            }
            if (pindex && pBestIndex && pindex->pprev != pBestIndex) {
                // This means that the list of blocks to announce don't
                // connect to each other. This shouldn't really be possible
                // to hit during regular operation (because reorgs should
                // take us to a chain that has some block not on the prior
                // chain, which should be caught by the prior check), but
                // one way this could happen is by using invalidateblock /
                // reconsiderblock repeatedly on the tip, causing it to be
                // added multiple times to vBlockHashesToAnnounce. Robustly
                // deal with this rare situation by reverting to an inv.
                fRevertToInv = true;
                break;
            }
            pBestIndex = pindex;
            if (pindex && fFoundStartingHeader) {
                // add this to the headers message
                vHeaders.push_back(pindex->GetBlockHeader());
            } else if (PeerHasHeader(state, pindex)) {
                // Keep looking for the first new block.
                continue;
            } else if (pindex && (pindex->pprev == nullptr || PeerHasHeader(state, pindex->pprev))) {
                // Peer doesn't have this header but they do have the prior
                // one.
                // Start sending headers.
                fFoundStartingHeader = true;
                vHeaders.push_back(pindex->GetBlockHeader());
            } else {
                // Peer doesn't have this header or the prior one --
                // nothing will connect, so bail out.
                fRevertToInv = true;
                break;
            }
        }
    }
    if (!fRevertToInv && !vHeaders.empty()) {
        if (vHeaders.size() == 1 && state->fPreferHeaderAndIDs) {
            // We only send up to 1 block as header-and-ids, as otherwise
            // probably means we're doing an initial-ish-sync or they're
            // slow.
            LogPrint(BCLog::NET,
                     "%s sending header-and-ids %s to peer=%d\n", __func__,
                     vHeaders.front().GetHash().ToString(), pto->id);

            bool fGotBlockFromCache = false;
            if (pBestIndex != nullptr)
            {
                auto msgData =
                    mostRecentBlock.GetCompactBlockMessageIfMatch(
                        pBestIndex->GetBlockHash());

                if(msgData)
                {
                    connman.PushMessage(
                        pto,
                        msgData->CreateCompactBlockMessage());

                    fGotBlockFromCache = true;
                }
            }

            if (!fGotBlockFromCache) {
                // FIXME pBestIndex could be null... what to do in that case?
                SendCompactBlock(
                    config,
                    true,
                    pto,
                    connman,
                    msgMaker,
                    pBestIndex->GetBlockPos());
            }
            state->pindexBestHeaderSent = pBestIndex;
        }
        else if (state->fPreferHeaders) {
            if (vHeaders.size() > 1) {
                LogPrint(BCLog::NET,
                         "%s: %u headers, range (%s, %s), to peer=%d\n",
                         __func__, vHeaders.size(),
                         vHeaders.front().GetHash().ToString(),
                         vHeaders.back().GetHash().ToString(), pto->id);
            }
            else {
                LogPrint(BCLog::NET, "%s: sending header %s to peer=%d\n",
                         __func__, vHeaders.front().GetHash().ToString(),
                         pto->id);
            }
            connman.PushMessage(pto, msgMaker.Make(NetMsgType::HEADERS, vHeaders));
            state->pindexBestHeaderSent = pBestIndex;
        }
        else {
            fRevertToInv = true;
        }
    }
    if (fRevertToInv) {
        // If falling back to using an inv, just try to inv the tip. The
        // last entry in vBlockHashesToAnnounce was our tip at some point in
        // the past.
        if (!pto->vBlockHashesToAnnounce.empty()) {
            const uint256 &hashToAnnounce =
                pto->vBlockHashesToAnnounce.back();
            BlockMap::iterator mi = mapBlockIndex.find(hashToAnnounce);
            assert(mi != mapBlockIndex.end());
            const CBlockIndex *pindex = mi->second;

            // Warn if we're announcing a block that is not on the main
            // chain. This should be very rare and could be optimized out.
            // Just log for now.
            if (chainActive[pindex->nHeight] != pindex) {
                LogPrint(BCLog::NET,
                         "Announcing block %s not on main chain (tip=%s)\n",
                         hashToAnnounce.ToString(),
                         chainActive.Tip()->GetBlockHash().ToString());
            }

            // If the peer's chain has this block, don't inv it back.
            if (!PeerHasHeader(state, pindex)) {
                pto->PushInventory(CInv(MSG_BLOCK, hashToAnnounce));
                LogPrint(BCLog::NET, "%s: sending inv peer=%d hash=%s\n",
                         __func__, pto->id, hashToAnnounce.ToString());
            }
        }
    }
    pto->vBlockHashesToAnnounce.clear();
}

void SendTxnInventory(const Config &config, const CNodePtr& pto, CConnman &connman, const CNetMsgMaker& msgMaker,
    std::vector<CInv>& vInv)
{
    // Get as many TX inventory msgs to send as we can for this peer
    std::vector<CTxnSendingDetails> vInvTx { pto->FetchNInventory(GetInventoryBroadcastMax(config)) };

    int64_t nNow = GetTimeMicros();

    for(const CTxnSendingDetails& txn : vInvTx)
    {
        vInv.emplace_back(txn.getInv());
        // if next element will cause too large message, then we send it now, as message size is still under limit
        // vInv size is actually limited before -- with INVENTORY_BROADCAST_MAX_PER_MB
        if (vInv.size() == pto->maxInvElements) {
            connman.PushMessage(pto, msgMaker.Make(NetMsgType::INV, vInv));
            vInv.clear();
        }

        // Expire old relay messages
        while(!vRelayExpiration.empty() && vRelayExpiration.front().first < nNow)
        {
            mapRelay.erase(vRelayExpiration.front().second);
            vRelayExpiration.pop_front();
        }

        auto ret = mapRelay.insert(std::make_pair(std::move(txn.getInv().hash), std::move(txn.getTxnRef())));
        if(ret.second)
        {
            vRelayExpiration.push_back(std::make_pair(nNow + 15 * 60 * 1000000, ret.first));
        }
    }
}
 
void SendInventory(const Config &config, const CNodePtr& pto, CConnman &connman, const CNetMsgMaker& msgMaker)
{
    //
    // Message: inventory
    //
    int64_t nNow = GetTimeMicros();
    std::vector<CInv> vInv;

    LOCK(pto->cs_inventory);
    vInv.reserve(pto->maxInvElements);

    // Add blocks
    for (const uint256 &hash : pto->vInventoryBlockToSend) {
        vInv.push_back(CInv(MSG_BLOCK, hash));
        // if next element will cause too large message, then we send it now, as message size is still under limit
        // vInv size is actually limited before -- with INVENTORY_BROADCAST_MAX_PER_MB
        if (vInv.size() == pto->maxInvElements) {
            connman.PushMessage(pto, msgMaker.Make(NetMsgType::INV, vInv));
            vInv.clear();
        }
    }
    pto->vInventoryBlockToSend.clear();

    // Check whether periodic sends should happen
    bool fSendTrickle = pto->fWhitelisted;
    if (pto->nNextInvSend < nNow) {
        fSendTrickle = true;
        pto->nNextInvSend = nNow + Fixed_delay_microsecs; 
    }

    // Time to send but the peer has requested we not relay transactions.
    if (fSendTrickle) {
        LOCK(pto->cs_filter);
        if (!pto->fRelayTxes) {
            pto->setInventoryTxToSend.clear();
        }
    }

    // Respond to BIP35 mempool requests
    if (fSendTrickle && pto->fSendMempool) {
        auto vtxinfo = mempool.InfoAll();
        pto->fSendMempool = false;
        Amount filterrate(0);
        {
            LOCK(pto->cs_feeFilter);
            filterrate = pto->minFeeFilter;
        }

        LOCK(pto->cs_filter);

        for (const auto &txinfo : vtxinfo) {
            const uint256 &txid = txinfo.tx->GetId();
            CInv inv(MSG_TX, txid);
            pto->setInventoryTxToSend.erase(txid);
            if (filterrate != Amount(0)) {
                if (txinfo.feeRate.GetFeePerK() < filterrate) {
                    continue;
                }
            }
            if (!pto->mFilter.IsRelevantAndUpdate(*txinfo.tx)) {
                continue;
            }
            pto->filterInventoryKnown.insert(txid);
            vInv.push_back(inv);
            // if next element will cause too large message, then we send it now, as message size is still under limit
            if (vInv.size() == pto->maxInvElements) {
                connman.PushMessage(pto, msgMaker.Make(NetMsgType::INV, vInv));
                vInv.clear();
            }
        }
        pto->timeLastMempoolReq = GetTime();
    }

    // Determine transactions to relay
    if (fSendTrickle) {
        SendTxnInventory(config, pto, connman, msgMaker, vInv);
    }

    if (!vInv.empty()) {
        connman.PushMessage(pto, msgMaker.Make(NetMsgType::INV, vInv));
    }
}

bool DetectStalling(const Config &config, const CNodePtr& pto, const CNodeStatePtr& state)
{
    assert(state);
    const Consensus::Params& consensusParams { config.GetChainParams().GetConsensus() };

    // Detect whether we're stalling
    int64_t nNow = GetTimeMicros();
    if (state->nStallingSince &&
        state->nStallingSince < nNow - MICROS_PER_SECOND * gArgs.GetArg("-blockstallingtimeout", DEFAULT_BLOCK_STALLING_TIMEOUT)) {
        // Stalling only triggers when the block download window cannot move.
        // During normal steady state, the download window should be much larger
        // than the to-be-downloaded set of blocks, so disconnection should only
        // happen during initial block download.
        // 
        // Also, don't abandon this attempt to download all the while we are making
        // sufficient progress, as measured by the current download speed to this
        // peer.
        uint64_t avgbw { pto->GetAverageBandwidth() };
        int64_t minDownloadSpeed { gArgs.GetArg("-blockstallingmindownloadspeed", DEFAULT_MIN_BLOCK_STALLING_RATE) };
        minDownloadSpeed = std::max(static_cast<decltype(minDownloadSpeed)>(0), minDownloadSpeed);
        if(avgbw < static_cast<uint64_t>(minDownloadSpeed) * 1000) {
            LogPrintf("Peer=%d is stalling block download (current speed %d), disconnecting\n", pto->id, avgbw);
            pto->fDisconnect = true;
            return true;
        }
        else {
            LogPrint(BCLog::NET, "Resetting stall (current speed %d) for peer=%d\n", avgbw, pto->id);
            state->nStallingSince = GetTimeMicros();
        }
    }
    // In case there is a block that has been in flight from this peer for 2 +
    // 0.5 * N times the block interval (with N the number of peers from which
    // we're downloading validated blocks), disconnect due to timeout. We
    // compensate for other peers to prevent killing off peers due to our own
    // downstream link being saturated. We only count validated in-flight blocks
    // so peers can't advertise non-existing block hashes to unreasonably
    // increase our timeout.
    if (state->vBlocksInFlight.size() > 0) {
        QueuedBlock &queuedBlock = state->vBlocksInFlight.front();
        int nOtherPeersWithValidatedDownloads =
            nPeersWithValidatedDownloads -
            (state->nBlocksInFlightValidHeaders > 0);
        if (nNow > state->nDownloadingSince +
                       consensusParams.nPowTargetSpacing *
                           (BLOCK_DOWNLOAD_TIMEOUT_BASE +
                            BLOCK_DOWNLOAD_TIMEOUT_PER_PEER *
                                nOtherPeersWithValidatedDownloads)) {
            LogPrintf("Timeout downloading block %s from peer=%d, disconnecting\n",
                      queuedBlock.hash.ToString(), pto->id);
            pto->fDisconnect = true;
            return true;
        }
    }

    return false;
}

void SendGetDataBlocks(const Config &config, const CNodePtr& pto, CConnman& connman,
    const CNetMsgMaker& msgMaker, const CNodeStatePtr& state)
{
    assert(state);
    const Consensus::Params& consensusParams { config.GetChainParams().GetConsensus() };
    //
    // Message: getdata (blocks)
    //
    std::vector<CInv> vGetData {};
    bool fFetch = state->fPreferredDownload ||
                  (nPreferredDownload == 0 && !pto->fClient && !pto->fOneShot);
    if (!pto->fClient && (fFetch || !IsInitialBlockDownload()) &&
        state->nBlocksInFlight < MAX_BLOCKS_IN_TRANSIT_PER_PEER) {
        std::vector<const CBlockIndex *> vToDownload;
        NodeId staller = -1;
        FindNextBlocksToDownload(pto->GetId(),
                                 MAX_BLOCKS_IN_TRANSIT_PER_PEER - state->nBlocksInFlight,
                                 vToDownload, staller, consensusParams, state);
        for (const CBlockIndex *pindex : vToDownload) {
            vGetData.push_back(CInv(MSG_BLOCK, pindex->GetBlockHash()));
            MarkBlockAsInFlight(
                config,
                pto->GetId(),
                pindex->GetBlockHash(),
                consensusParams,
                state,
                *pindex);
            LogPrint(BCLog::NET, "Requesting block %s (%d) peer=%d\n",
                     pindex->GetBlockHash().ToString(), pindex->nHeight,
                     pto->id);
        }
        if (state->nBlocksInFlight == 0 && staller != -1) {
            // Try to obtain an access to the node's state data.
            const CNodeStateRef stallerStateRef { GetState(staller) };
            const CNodeStatePtr& stallerState { stallerStateRef.get() };
            assert(stallerState);
            if (stallerState->nStallingSince == 0) {
                stallerState->nStallingSince = GetTimeMicros();
                uint64_t avgbw { pto->GetAverageBandwidth() };
                LogPrint(BCLog::NET, "Stall started (current speed %d) peer=%d\n", avgbw, staller);
            }
        }
    }
    if (!vGetData.empty()) {
        connman.PushMessage(pto, msgMaker.Make(NetMsgType::GETDATA, vGetData));
    }
}

void SendGetDataNonBlocks(const CNodePtr& pto, CConnman& connman, const CNetMsgMaker& msgMaker)
{
    //
    // Message: getdata (non-blocks)
    //
    int64_t nNow = GetTimeMicros();
    std::vector<CInv> vGetData {};
    {
        LOCK(cs_invQueries);
        while (!pto->mapAskFor.empty()) {
            const auto& firstIt { pto->mapAskFor.begin() };
            const CInv& inv { firstIt->second };
            bool alreadyHave { AlreadyHave(inv) };

            if(firstIt->first <= nNow) {
                // It's time to request (or re-request) this item
                if (!alreadyHave) {
                    LogPrint(BCLog::NET, "Requesting %s peer=%d\n", inv.ToString(), pto->id);
                    vGetData.push_back(inv);
                    // if next element will cause too large message, then we send it now, as message size is still under limit
                    if (vGetData.size() == pto->maxInvElements) {
                        connman.PushMessage(pto, msgMaker.Make(NetMsgType::GETDATA, vGetData));
                        vGetData.clear();
                    }
                }
                else {
                    // If we're not going to ask, don't expect a response.
                    pto->setAskFor.erase(inv.hash);
                }
                pto->mapAskFor.erase(firstIt);
            }
            else {
                // Look ahead to see if we can clear out some items we have already recieved from elsewhere
                if(alreadyHave) {
                    pto->setAskFor.erase(inv.hash);
                    pto->mapAskFor.erase(firstIt);
                }
                else {
                    // Abort clearing out items as soon as we find one that is still required. Bailing out here
                    // is a trade-off between optimally clearing out every item as soon as it's seen from
                    // any of our peers, and wasting a lot of time here iterating over this map every time.
                    break;
                }
            }
        }
    }
    if (!vGetData.empty()) {
        connman.PushMessage(pto, msgMaker.Make(NetMsgType::GETDATA, vGetData));
    }
}

void SendFeeFilter(const Config &config, const CNodePtr& pto, CConnman& connman,
    const CNetMsgMaker& msgMaker)
{
    //
    // Message: feefilter
    //
    // We don't want white listed peers to filter txs to us if we have
    // -whitelistforcerelay
    if (pto->nVersion >= FEEFILTER_VERSION &&
        gArgs.GetBoolArg("-feefilter", DEFAULT_FEEFILTER) &&
        !(pto->fWhitelisted &&
          gArgs.GetBoolArg("-whitelistforcerelay",
                           DEFAULT_WHITELISTFORCERELAY))) {
        Amount currentFilter =
            mempool
                .GetMinFee(
                    gArgs.GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) *
                    1000000)
                .GetFeePerK();
        int64_t timeNow = GetTimeMicros();
        if (timeNow > pto->nextSendTimeFeeFilter) {
            static CFeeRate default_feerate =
                CFeeRate(DEFAULT_MIN_RELAY_TX_FEE);
            static FeeFilterRounder filterRounder(default_feerate);
            Amount filterToSend = filterRounder.round(currentFilter);
            // If we don't allow free transactions, then we always have a fee
            // filter of at least minRelayTxFee
            if (gArgs.GetArg("-limitfreerelay", DEFAULT_LIMITFREERELAY) <= 0) {
                filterToSend = std::max(filterToSend,
                                        config.GetMinFeePerKB().GetFeePerK());
            }

            if (filterToSend != pto->lastSentFeeFilter) {
                connman.PushMessage(
                    pto, msgMaker.Make(NetMsgType::FEEFILTER, filterToSend));
                pto->lastSentFeeFilter = filterToSend;
            }
            pto->nextSendTimeFeeFilter =
                PoissonNextSend(timeNow, AVG_FEEFILTER_BROADCAST_INTERVAL);
        }
        // If the fee filter has changed substantially and it's still more than
        // MAX_FEEFILTER_CHANGE_DELAY until scheduled broadcast, then move the
        // broadcast to within MAX_FEEFILTER_CHANGE_DELAY.
        else if (timeNow + MAX_FEEFILTER_CHANGE_DELAY * 1000000 <
                     pto->nextSendTimeFeeFilter &&
                 (currentFilter < 3 * pto->lastSentFeeFilter / 4 ||
                  currentFilter > 4 * pto->lastSentFeeFilter / 3)) {
            pto->nextSendTimeFeeFilter =
                timeNow + GetRandInt(MAX_FEEFILTER_CHANGE_DELAY) * 1000000;
        }
    }
}

bool SendMessages(const Config &config, const CNodePtr& pto, CConnman &connman,
                  const std::atomic<bool> &interruptMsgProc)
{
    // Don't send anything until the version handshake is complete
    if (!pto->fSuccessfullyConnected || pto->fDisconnect) {
        return true;
    }

    // If we get here, the outgoing message serialization version is set and
    // can't change.
    const CNetMsgMaker msgMaker(pto->GetSendVersion());

    // Message: ping
    SendPings(pto, connman, msgMaker);

    // Acquire cs_main for IsInitialBlockDownload() and CNodeState()
    TRY_LOCK(cs_main, lockMain);
    if (!lockMain) {
        return true;
    }

    if (SendRejectsAndCheckIfBanned(pto, connman)) {
        return true;
    }

    // Message: addr
    SendAddrs(pto, connman, msgMaker);

    // Try to obtain an access to the node's state data.
    const CNodeStateRef stateRef { GetState(pto->GetId()) };
    const CNodeStatePtr& state { stateRef.get() };
    assert(state);

    // Synchronise blockchain
    SendBlockSync(pto, connman, msgMaker, state);

    // Resend wallet transactions that haven't gotten in a block yet
    // Except during reindex, importing and IBD, when old wallet transactions
    // become unconfirmed and spams other nodes.
    if (!fReindex && !fImporting && !IsInitialBlockDownload()) {
        GetMainSignals().Broadcast(nTimeBestReceived, &connman);
    }

    // Try sending block announcements via headers
    SendBlockHeaders(config, pto, connman, msgMaker, state);

    // Message: inventory
    SendInventory(config, pto, connman, msgMaker);

    // Detect stalling peers
    if(DetectStalling(config, pto, state)) {
        return true;
    }

    // Node is not too busy so we can send him GetData requests.
    if (state->CanSend()) {
        // Message: getdata (blocks)
        SendGetDataBlocks(config, pto, connman, msgMaker, state);
    }

    // Message: getdata (non-blocks)
    SendGetDataNonBlocks(pto, connman, msgMaker);

    // Message: feefilter
    SendFeeFilter(config, pto, connman, msgMaker);

    return true;
}

