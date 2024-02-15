// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2019-2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "net/net_processing.h"
#include "addrman.h"
#include "arith_uint256.h"
#include "block_file_access.h"
#include "block_index.h"
#include "block_index_store.h"
#include "blockencodings.h"
#include "blockstreams.h"
#include "chainparams.h"
#include "clientversion.h"
#include "config.h"
#include "consensus/validation.h"
#include "double_spend/dsdetected_message.h"
#include "hash.h"
#include "init.h"
#include "invalid_txn_publisher.h"
#include "limited_cache.h"
#include "locked_ref.h"
#include "merkleblock.h"
#include "merkleproof.h"
#include "merkletreestore.h"
#include "miner_id/dataref_index.h"
#include "miner_id/datareftx.h"
#include "miner_id/miner_id_db.h"
#include "miner_id/miner_info_ref.h"
#include "miner_id/miner_info_tracker.h"
#include "miner_id/revokemid.h"
#include "net/authconn.h"
#include "net/block_download_tracker.h"
#include "net/net.h"
#include "net/netbase.h"
#include "net/node_state.h"
#include "netmessagemaker.h"
#include "policy/fees.h"
#include "primitives/block.h"
#include "primitives/transaction.h"
#include "protocol.h"
#include "random.h"
#include "rpc/webhook_client.h"
#include "taskcancellation.h"
#include "tinyformat.h"
#include "txdb.h"
#include "txmempool.h"
#include "ui_interface.h"
#include "util.h"
#include "utilmoneystr.h"
#include "utilstrencodings.h"
#include "validation.h"
#include "validationinterface.h"
#include <algorithm>
#include <boost/range/adaptor/reversed.hpp>
#include <boost/thread.hpp>
#include <chrono>
#include <exception>
#include <memory>
#include <optional>
#include <shared_mutex>

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

uint256 hashRecentRejectsChainTip;

/** Track blocks in flight and where they're coming from */
BlockDownloadTracker blockDownloadTracker {};

/** Number of preferable block download peers. */
std::atomic<int> nPreferredDownload = 0;

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

bool IsBlockDownloadStallingFromPeer(const Config& config, const CNodePtr& node, uint64_t& avgbw)
{
    avgbw = node->GetAssociation().GetAverageBandwidth(StreamPolicy::MessageType::BLOCK).first;
    return (avgbw < config.GetBlockStallingMinDownloadSpeed() * 1000);
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
    int32_t nNodeStartingHeight = pnode->GetMyStartingHeight();
    NodeId nodeid = pnode->GetId();
    CAddress addr = pnode->GetAssociation().GetPeerAddr();

    CAddress addrYou = (addr.IsRoutable() && !IsProxy(addr)
                            ? addr
                            : CAddress(CService(), addr.nServices));
    CAddress addrMe = CAddress(CService(), nLocalNodeServices);

    // Include association ID if we have one and supported stream policies
    std::vector<uint8_t> assocIDBytes {};
    std::string assocIDStr { AssociationID::NULL_ID_STR };
    AssociationIDPtr assocID { pnode->GetAssociation().GetAssociationID() };
    if(assocID) {
        assocIDBytes = assocID->GetBytes();
        assocIDStr = assocID->ToString();
    }

    connman.PushMessage(pnode,
                        CNetMsgMaker(INIT_PROTO_VERSION)
                            .Make(NetMsgType::VERSION, PROTOCOL_VERSION,
                                  (uint64_t)nLocalNodeServices, nTime, addrYou,
                                  addrMe, nonce, userAgent(),
                                  nNodeStartingHeight, ::fRelayTxes, assocIDBytes));

    if (fLogIPs) {
        LogPrint(BCLog::NETMSG, "send version message: version %d, blocks=%d, "
                             "us=%s, them=%s, assocID=%s, peer=%d\n", PROTOCOL_VERSION, nNodeStartingHeight, addrMe.ToString(),
                 addrYou.ToString(), assocIDStr, nodeid);
    } else {
        LogPrint(
            BCLog::NETMSG,
            "send version message: version %d, blocks=%d, us=%s, assocID=%s, peer=%d\n",
            PROTOCOL_VERSION, nNodeStartingHeight, addrMe.ToString(), assocIDStr, nodeid);
    }
}

void PushProtoconf(const CNodePtr& pnode, CConnman& connman, const Config &config)
{
    std::string streamPolicies { connman.GetStreamPolicyFactory().GetSupportedPolicyNamesStr() };
    connman.PushMessage(
        pnode, CNetMsgMaker(INIT_PROTO_VERSION).Make(NetMsgType::PROTOCONF,
            CProtoconf(config.GetMaxProtocolRecvPayloadLength(), streamPolicies)
    ));

    LogPrint(BCLog::NETMSG, "send protoconf message: max size %d, stream policies %s, number of fields %d\n",
        config.GetMaxProtocolRecvPayloadLength(), streamPolicies, 2);
}

static void PushCreateStream(const CNodePtr& pnode, CConnman& connman, StreamType streamType,
    const std::string& streamPolicyName, const AssociationIDPtr& assocID)
{

    connman.PushMessage(pnode,
        CNetMsgMaker(INIT_PROTO_VERSION).Make(NetMsgType::CREATESTREAM, assocID->GetBytes(),
            static_cast<uint8_t>(streamType), streamPolicyName
    ));

    LogPrint(BCLog::NETMSG, "send createstream message: type %s, assoc %s, peer=%d\n", enum_cast<std::string>(streamType),
        assocID->ToString(), pnode->id);
}

void InitializeNode(const CNodePtr& pnode, CConnman& connman, const NodeConnectInfo* connectInfo)
{
    CAddress addr = pnode->GetAssociation().GetPeerAddr();
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
        if(connectInfo && connectInfo->fNewStream) {
            PushCreateStream(pnode, connman, connectInfo->streamType, connectInfo->streamPolicy, connectInfo->assocID);
        }
        else {
            if(GlobalConfig::GetConfig().GetMultistreamsEnabled()) {
                pnode->GetAssociation().CreateAssociationID<UUIDAssociationID>();
            }
            PushNodeVersion(pnode, connman, GetTime());
        }
    }
}

void FinalizeNode(NodeId nodeid, bool &fUpdateConnectionTime)
{
    // For mapBlocksInFlight and mapBlockSource
    AssertLockHeld(cs_main);

    fUpdateConnectionTime = false;

    // Erase orphan txns received from the given nodeId
    g_connman->EraseOrphanTxnsFromPeer(nodeid);

    // Read & modify mapNodeState in an exclusive mode.
    std::unique_lock<std::shared_mutex> lock { mapNodeStateMtx };
    auto it { mapNodeState.find(nodeid) };
    assert(it != mapNodeState.end());

    const CNodeStateRef stateRef { it->second, it->second->mMtx };
    const CNodeStatePtr state { stateRef.get() };

    if (state->fSyncStarted) {
        nSyncStarted--;
    }
    if (state->nMisbehavior == 0 && state->fCurrentlyConnected) {
        fUpdateConnectionTime = true;
    }
    nPreferredDownload -= state->fPreferredDownload;

    // Finished with node entry
    mapNodeState.erase(it);
    bool lastPeer { mapNodeState.empty() };

    if (lastPeer) {
        // Do a consistency check after the last peer is removed.
        assert(nPreferredDownload == 0);
    }

    // Clear out node details from block download tracker
    blockDownloadTracker.ClearPeer(nodeid, state, lastPeer);
}

/** Check whether the last unknown block a peer advertised is not yet known. */
void ProcessBlockAvailability(const CNodeStatePtr& state) {

    AssertLockHeld(cs_main);
    assert(state);

    if (!state->hashLastUnknownBlock.IsNull()) {
        if (auto index = mapBlockIndex.Get(state->hashLastUnknownBlock); index)
        {
            if (auto chainWork = index->GetChainWork(); chainWork > 0)
            {
                if (state->pindexBestKnownBlock == nullptr ||
                    chainWork >= state->pindexBestKnownBlock->GetChainWork())
                {
                    state->pindexBestKnownBlock = index;
                }
                state->hashLastUnknownBlock.SetNull();
            }
        }
    }
}

/** Update tracking information about which blocks a peer is assumed to have. */
void UpdateBlockAvailability(const uint256 &hash, const CNodeStatePtr& state) {

    AssertLockHeld(cs_main);
    assert(state);

    ProcessBlockAvailability(state);

    if (auto index = mapBlockIndex.Get(hash); index)
    {
        if (auto chainWork = index->GetChainWork(); chainWork > 0)
        {
            // An actually better block was announced.
            if (state->pindexBestKnownBlock == nullptr ||
                chainWork >= state->pindexBestKnownBlock->GetChainWork())
            {
                state->pindexBestKnownBlock = index;
            }

            return;
        }
    }

    // An unknown block was announced; just assume that the latest one is
    // the best one.
    state->hashLastUnknownBlock = hash;
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
        pindex == state->pindexBestKnownBlock->GetAncestor(pindex->GetHeight())) {
        return true;
    }
    else if (state->pindexBestHeaderSent &&
        pindex == state->pindexBestHeaderSent->GetAncestor(pindex->GetHeight())) {
        return true;
    }
    return false;
}

/**
 * Update pindexLastCommonBlock and add not-in-flight missing successors to
 * vBlocks, until it has at most count entries.
 */
static void FindNextBlocksToDownload(
    const Config& config,
    NodeId nodeid,
    unsigned int count,
    std::vector<const CBlockIndex*>& vBlocks,
    NodeId& nodeStaller,
    const Consensus::Params& consensusParams,
    const CNodeStatePtr& state,
    CConnman& connman)
{
    if (count == 0) {
        return;
    }

    vBlocks.reserve(vBlocks.size() + count);
    assert(state);

    // Make sure pindexBestKnownBlock is up to date, we'll need it.
    ProcessBlockAvailability(state);

    if (state->pindexBestKnownBlock == nullptr)
    {
        // This peer has nothing interesting.
        return;
    }
    else if (auto chainWork = state->pindexBestKnownBlock->GetChainWork();
        chainWork < nMinimumChainWork ||
        chainWork < chainActive.Tip()->GetChainWork())
    {
        // This peer has nothing interesting.
        return;
    }

    if (state->pindexLastCommonBlock == nullptr) {
        // Bootstrap quickly by guessing a parent of our best tip is the forking
        // point. Guessing wrong in either direction is not a problem.
        state->pindexLastCommonBlock = chainActive[std::min(
            state->pindexBestKnownBlock->GetHeight(), chainActive.Height())];
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
    int32_t nWindowEnd = state->pindexLastCommonBlock->GetHeight() + config.GetBlockDownloadWindow();
    int32_t nMaxHeight = std::min<int>(state->pindexBestKnownBlock->GetHeight(), nWindowEnd + 1);
    NodeId waitingfor = -1;

    const int32_t nDownloadHeightThreshold = chainActive.Height() + config.GetBlockDownloadLowerWindow();

    // Lambda to record a block we should fetch
    auto FetchBlock = [nodeid, count, nWindowEnd, &vBlocks, &nodeStaller, &waitingfor, nDownloadHeightThreshold](const CBlockIndex* pindex)
    {
        // The block is not already downloaded, and not yet in flight.
        if (pindex->GetHeight() > nWindowEnd) {
            // We reached the end of the window.
            if (vBlocks.size() == 0 && waitingfor != nodeid) {
                // We aren't able to fetch anything, but we would be if
                // the download window was one larger.
                nodeStaller = waitingfor;
            }
            return false;
        }

        // A further limit on how far ahead we download blocks to reduce disk usage
        if (pindex->GetHeight() > nDownloadHeightThreshold) {
            return false;
        }

        vBlocks.push_back(pindex);
        if (vBlocks.size() == count) {
            return false;
        }

        return true;
    };

    while (pindexWalk->GetHeight() < nMaxHeight) {
        // Read up to 128 (or more, if more blocks than that are needed)
        // successors of pindexWalk (towards pindexBestKnownBlock) into
        // vToFetch. We fetch 128, because CBlockIndex::GetAncestor may be as
        // expensive as iterating over ~100 CBlockIndex* entries anyway.
        int nToFetch = std::min(nMaxHeight - pindexWalk->GetHeight(),
                                std::max<int>(count - vBlocks.size(), 128));
        vToFetch.resize(nToFetch);
        pindexWalk = state->pindexBestKnownBlock->GetAncestor(
            pindexWalk->GetHeight() + nToFetch);
        vToFetch[nToFetch - 1] = pindexWalk;
        for (unsigned int i = nToFetch - 1; i > 0; i--) {
            vToFetch[i - 1] = vToFetch[i]->GetPrev();
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
            if (pindex->getStatus().hasData() || chainActive.Contains(pindex)) {
                if (pindex->GetChainTx())
                {
                    state->pindexLastCommonBlock = pindex;
                }
            }
            else if (! blockDownloadTracker.IsInFlight(pindex->GetBlockHash())) {
                if(! FetchBlock(pindex)) {
                    // Can't fetch anymore
                    return;
                }
            }
            else if (waitingfor == -1) {
                // This is the first already-in-flight block.
                const uint256& hash { pindex->GetBlockHash() };
                waitingfor = blockDownloadTracker.GetPeerForBlock(hash);

                size_t stallerCount {0};
                const std::vector<BlockDownloadTracker::InFlightBlock> allInFlightDetails { blockDownloadTracker.GetBlockDetails(hash) };
                bool stalling { ! allInFlightDetails.empty() };
                for(const auto& inFlightDetails : allInFlightDetails) {
                    // In flight for a while?
                    int64_t inFlightSecs { (GetTimeMicros() - inFlightDetails.inFlightSince) / MICROS_PER_SECOND };
                    if(inFlightSecs >= config.GetBlockDownloadSlowFetchTimeout()) {
                        // Are we getting (any) data from this peer?
                        const CNodePtr& nodePtr { connman.FindNodeById(inFlightDetails.block.GetNode()) };
                        if(nodePtr) {
                            uint64_t avgbw {0};
                            if(IsBlockDownloadStallingFromPeer(config, nodePtr, avgbw)) {
                                // This peer is stalling
                                ++stallerCount;
                            }
                            else {
                                // This peer seems active currently
                                stalling = false;
                                break;
                            }
                        }
                    }
                    else {
                        // Give this peer more time
                        stalling = false;
                        break;
                    }
                }

                if(stalling) {
                    // Should we ask someone else for this block?
                    size_t maxParallelFetch { config.GetBlockDownloadMaxParallelFetch() };
                    if(stallerCount < maxParallelFetch && ! blockDownloadTracker.IsInFlight( {hash, nodeid} )) {
                        LogPrint(BCLog::NETMSG, "Triggering parallel block download for %s to peer=%d\n", hash.ToString(), nodeid);
                        if(! FetchBlock(pindex)) {
                            // Can't fetch anymore
                            return;
                        }
                    }
                }
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

/**
 * Helper class for logging the duration of ProcessMessages request
 * processing. It writes to log all the requests that take more time to
 * process than the provided threshold.
 */
class CLogP2PStallDuration
{
public:
    CLogP2PStallDuration(CLogP2PStallDuration const &) = delete;
    CLogP2PStallDuration & operator= (CLogP2PStallDuration const &) = delete;
    CLogP2PStallDuration(CLogP2PStallDuration &&) = default;
    CLogP2PStallDuration & operator= (CLogP2PStallDuration &&) = default;

    CLogP2PStallDuration(
        std::string command,
        std::chrono::milliseconds debugP2PTheadStallsThreshold)
        : mDebugP2PTheadStallsThreshold{debugP2PTheadStallsThreshold}
        , mProcessingStart{std::chrono::steady_clock::now()}
        , mCommand{std::move(command)}
    {/**/}


    ~CLogP2PStallDuration()
    {   
        if(!mCommand.empty())
        {   
            auto processingDuration =
                    std::chrono::steady_clock::now() - mProcessingStart;

            if(processingDuration > mDebugP2PTheadStallsThreshold)
            {   
                LogPrintf(
                    "ProcessMessages request processing took %s ms to complete "
                    "processing '%s' request!\n",
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        processingDuration).count(),
                    mCommand);
            }
        }
    }

private:
    std::chrono::milliseconds mDebugP2PTheadStallsThreshold;
    std::chrono::time_point<std::chrono::steady_clock> mProcessingStart;
    std::string mCommand;
};

} // namespace

static bool ProcessMessage(const Config& config,
                           const CNodePtr& pfrom,
                           const std::string& strCommand,
                           msg_buffer& vRecv,
                           int64_t nTimeReceived,
                           const CChainParams& chainparams,
                           CConnman& connman,
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
        state->pindexBestKnownBlock ? state->pindexBestKnownBlock->GetHeight() : -1;
    stats.nCommonHeight = state->pindexLastCommonBlock
                              ? state->pindexLastCommonBlock->GetHeight()
                              : -1;
    for (const QueuedBlock &queue : state->vBlocksInFlight) {
        stats.vHeightInFlight.push_back(queue.blockIndex.GetHeight());
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
    int64_t banscore = GlobalConfig::GetConfig().GetBanScoreThreshold();
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

void PeerLogicValidation::RegisterValidationInterface()
{
    CMainSignals& sigs { GetMainSignals() };

    using namespace boost::placeholders;
    slotConnections.push_back(sigs.BlockConnected.connect(boost::bind(&PeerLogicValidation::BlockConnected, this, _1, _2, _3)));
    slotConnections.push_back(sigs.UpdatedBlockTip.connect(boost::bind(&PeerLogicValidation::UpdatedBlockTip, this, _1, _2, _3)));
    slotConnections.push_back(sigs.BlockChecked.connect(boost::bind(&PeerLogicValidation::BlockChecked, this, _1, _2)));
    slotConnections.push_back(sigs.NewPoWValidBlock.connect(boost::bind(&PeerLogicValidation::NewPoWValidBlock, this, _1, _2)));
}

void PeerLogicValidation::UnregisterValidationInterface()
{
    slotConnections.clear();
}

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

            if(auto metaData = index.GetDiskBlockMetaData(); !metaData.diskDataHash.IsNull())
            {
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
    if (pindex->GetHeight() <= nHighestFastAnnounce) {
        return;
    }
    nHighestFastAnnounce = pindex->GetHeight();

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
            PeerHasHeader(state, pindex->GetPrev())) {

            LogPrint(BCLog::NETMSG, "%s sending header-and-ids %s to peer=%d\n",
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
    const int32_t nNewHeight = pindexNew->GetHeight();
    connman->SetBestHeight(nNewHeight);

    if (!fInitialDownload) {
        // Find the hashes of all blocks that weren't previously in the best
        // chain.
        std::vector<uint256> vHashes;
        const CBlockIndex *pindexToAnnounce = pindexNew;
        while (pindexToAnnounce != pindexFork) {
            vHashes.push_back(pindexToAnnounce->GetBlockHash());
            pindexToAnnounce = pindexToAnnounce->GetPrev();
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

void PeerLogicValidation::BlockChecked(const CBlock& block,
                                       const CValidationState& state)
{
    blockDownloadTracker.BlockChecked(block.GetHash(), state);
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
            chainActive.Tip()->GetBlockHash()
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
               // A call to the TxIdTracker is sufficient to verify, if currently:
               // - txn is already received from the network and then moved into ptv queues
               // - txn is already detected as an orphan and it is still being kept
               //   (until evicted or accepted)
               g_connman->GetTxIdTracker()->Contains(TxId(inv.hash)) ||
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
        return mapBlockIndex.Get(inv.hash);
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

    if (!txinfo.IsNull())
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

    auto sortfunc = [&best, &hasher, nRelayNodes](const CNodePtr& pnode) {
        if (pnode->fInbound && pnode->nVersion >= CADDR_TIME_VERSION) {
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

static bool rejectIfMaxDownloadExceeded(
    const Config& config,
    const CSerializedNetMsg& msg,
    bool isMostRecentBlock,
    const CNodePtr& pfrom,
    CConnman& connman)
{
    uint64_t maxSendQueuesBytes { config.GetMaxSendQueuesBytes() };
    size_t totalSize = CSendQueueBytes::getTotalSendQueuesMemory() + msg.GetEstimatedMemoryUsage() + CMessageHeader::GetHeaderSizeForPayload(msg.Size());
    if (totalSize > maxSendQueuesBytes) {

        if (!isMostRecentBlock) {
            LogPrint(BCLog::NETMSG, "Size of all msgs currently sending across "
                "all the queues is too large: %s. Maximum size: %s. Request ignored, block will not be sent. "
                "Sending reject.\n", totalSize, maxSendQueuesBytes); 
            connman.PushMessage(pfrom, CNetMsgMaker(INIT_PROTO_VERSION)
                .Make(NetMsgType::REJECT, std::string(NetMsgType::GETDATA), REJECT_TOOBUSY, strprintf("Max blocks' downloading size exceeded.")));
            return true;
        }

        if (!pfrom->fWhitelisted) {
            LogPrint(BCLog::NETMSG, "Size of all msgs currently sending across "
                "all the queues is too large: %s. Maximum size: %s. Last block will not be sent, "
                "because it was requested by non whitelisted peer. \n",
                totalSize, maxSendQueuesBytes);
            connman.PushMessage(pfrom, CNetMsgMaker(INIT_PROTO_VERSION)
                .Make(NetMsgType::REJECT, std::string(NetMsgType::GETDATA), REJECT_TOOBUSY, strprintf("Max blocks' downloading size exceeded.")));
            return true;
        }
        
        LogPrint(BCLog::NETMSG, "Size of all msgs currently sending across "
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
    const CBlockHeaderAndShortTxIDs& cmpctblock)
{
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
    CBlockIndex::BlockStreamAndMetaData data,
    CConnman& connman)
{
    CSerializedNetMsg blockMsg{
            NetMsgType::BLOCK,
            std::move(data.metaData.diskDataHash),
            data.metaData.diskDataSize,
            std::move(data.stream)
        };

    if (rejectIfMaxDownloadExceeded(config, blockMsg, isMostRecentBlock, pfrom, connman)) {
        return;
    }

    connman.PushMessage(pfrom, std::move(blockMsg));
}

static void SendUnseenTransactions(
    // requires: ascending ordered
    const std::vector<std::pair<size_t, uint256>>& vOrderedUnseenTransactions,
    CConnman& connman,
    const CNodePtr& pfrom,
    const CNetMsgMaker msgMaker,
    const CBlockIndex& index)
{
    if (vOrderedUnseenTransactions.empty())
    {
        return;
    }

    auto stream = index.GetDiskBlockStreamReader();
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
        if (pfrom->GetPausedForSending()) {
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
                auto index = mapBlockIndex.Get(inv.hash);
                const auto& bestHeader = mapBlockIndex.GetBestHeader();
                if (index)
                {
                    if (index->GetChainTx() &&
                        !index->IsValid(BlockValidity::SCRIPTS) &&
                        index->IsValid(BlockValidity::TREE)
                        && IsBlockABestChainTipCandidate(*index)) {

                        LogPrint(
                            BCLog::NETMSG,
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
                    if (chainActive.Contains(index)) {
                        send = true;
                    } else {
                        static const int nOneMonth = 30 * 24 * 60 * 60;
                        // To prevent fingerprinting attacks, only send blocks
                        // outside of the active chain if they are valid, and no
                        // more than a month older (both in time, and in best
                        // equivalent proof of work) than the best header chain
                        // we know about.
                        send = index->IsValid(BlockValidity::SCRIPTS) &&
                               (bestHeader.GetBlockTime() -
                                    index->GetBlockTime() <
                                nOneMonth) &&
                               (GetBlockProofEquivalentTime(
                                    bestHeader, *index,
                                    bestHeader,
                                    consensusParams) < nOneMonth);
                        if (!send) {
                            LogPrint(BCLog::NETMSG, "%s: ignoring request from peer=%i for "
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
                    (((bestHeader.GetBlockTime() -
                           index->GetBlockTime() >
                       nOneWeek)) ||
                     inv.type == MSG_FILTERED_BLOCK) &&
                    !pfrom->fWhitelisted) {
                    LogPrint(BCLog::NETMSG, "historical block serving limit reached, disconnect peer=%d\n",
                             pfrom->GetId());

                    // disconnect node
                    pfrom->fDisconnect = true;
                    send = false;
                }

                // Pruned nodes may have deleted the block, so check whether
                // it's available before trying to send.
                if (send) {
                    bool isMostRecentBlock = chainActive.Tip() == index;
                    bool wasSent = false;
                    // Send block from disk
                    if (inv.type == MSG_BLOCK)
                    {
                        auto data = index->StreamBlockFromDisk(pfrom->GetSendVersion(), mapBlockIndex);

                        if (data.stream)
                        {
                            SendBlock(
                                config,
                                isMostRecentBlock,
                                pfrom,
                                std::move(data),
                                connman);
                            wasSent = true;
                        }
                    } else if (inv.type == MSG_FILTERED_BLOCK) {
                        auto stream =
                            index->GetDiskBlockStreamReader();
                        if (stream) {
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
                                    *index);
                            }
                            // else
                            // no response

                            wasSent = true;
                        }
                    } else if (inv.type == MSG_CMPCT_BLOCK) {
                        // If a peer is asking for old blocks, we're almost
                        // guaranteed they won't have a useful mempool to match
                        // against a compact block, and we don't feel like
                        // constructing the object for them, so instead we
                        // respond with the full, non-compact block.
                        if (CanDirectFetch(consensusParams) &&
                            index->GetHeight() >=
                                chainActive.Height() - MAX_CMPCTBLOCK_DEPTH)
                        {
                            auto reader =
                                index->GetDiskBlockStreamReader( config );
                            if (reader)
                            {
                                bool sent = SendCompactBlock(
                                    config,
                                    isMostRecentBlock,
                                    pfrom,
                                    connman,
                                    msgMaker,
                                    *reader);
                                if (!sent)
                                {
                                    break;
                                }
                                wasSent = true;
                            }
                        } else {
                            auto data = index->StreamBlockFromDisk(pfrom->GetSendVersion(), mapBlockIndex);

                            if (data.stream)
                            {
                                SendBlock(
                                    config,
                                    isMostRecentBlock,
                                    pfrom,
                                    std::move(data),
                                    connman);
                                wasSent = true;
                            }
                        }
                    }

                    if (wasSent)
                    {
                        // Trigger the peer node to send a getblocks request for the
                        // next batch of inventory.
                        if (inv.hash == pfrom->hashContinue) {
                            // Bypass PushBlockInventory, this must send even if
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
                    if (!txinfo.IsNull() &&
                        txinfo.nTime <= pfrom->timeLastMempoolReq) {

                        if(const auto& pTx{txinfo.GetTx()}; pTx)
                        {
                            connman.PushMessage(pfrom,
                                                msgMaker.Make(NetMsgType::TX,
                                                              *pTx));
                            push = true;
                        }
                    }
                }
                if (!push) {
                    vNotFound.push_back(inv);
                }
            }
            else if(inv.type == MSG_DATAREF_TX) {
                bool found {false};
                if(g_dataRefIndex) {
                    // Lookup up inv.hash in the dataref index
                    try {
                        const auto& dataref { g_dataRefIndex->CreateLockingAccess().GetDataRefEntry(inv.hash) };
                        if(dataref) {
                            // Push datareftx msg back to requester
                            DataRefTx datareftx { dataref->txn, dataref->proof };
                            CSerializedNetMsg msg { msgMaker.Make(NetMsgType::DATAREFTX, datareftx) };
                            connman.PushMessage(pfrom, std::move(msg));
                            found = true;
                        }
                    }
                    catch(const std::exception& e) {
                        LogPrint(BCLog::NETMSG, "Couldn't fetch dataref from index: %s\n", e.what());
                    }
                }

                if(!found) {
                    // Return not found
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

/**
* Process reject messages.
*/
static void ProcessRejectMessage(msg_buffer& vRecv, const CNodePtr& pfrom)
{
    if(LogAcceptCategory(BCLog::NETMSG))
    {
        try
        {
            std::string strMsg;
            uint8_t ccode;
            std::string strReason;
            vRecv >> LIMITED_STRING(strMsg, CMessageFields::COMMAND_SIZE) >>
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
            LogPrint(BCLog::NETMSG, "Reject %s\n", SanitizeString(ss.str()));

            if (ccode == REJECT_TOOBUSY) {
                // Peer is too busy with sending blocks so we will not ask again for a while
                blockDownloadTracker.PeerTooBusy(pfrom->GetId());
            }
        }
        catch (const std::ios_base::failure &)
        {
            // Avoid feedback loops by preventing reject messages from
            // triggering a new reject message.
            LogPrint(BCLog::NETMSG, "Unparseable reject message received\n");
        }
    }
}

/**
* Process createstream messages.
*/
static bool ProcessCreateStreamMessage(const CNodePtr& pfrom,
                                       const std::string& strCommand,
                                       msg_buffer& vRecv,
                                       CConnman& connman)
{
    // Check we haven't already received either a createstream or a version message
    if(pfrom->nVersion != 0)
    {
        connman.PushMessage(pfrom, CNetMsgMaker(INIT_PROTO_VERSION)
            .Make(NetMsgType::REJECT, strCommand, REJECT_NONSTANDARD,
                std::string("Invalid createstream scenario")));
        pfrom->fDisconnect = true;
        return false;
    }

    std::vector<uint8_t> associationID {};
    uint8_t streamTypeRaw {0};
    std::string streamPolicyName {};

    // Which association is this for?
    try
    {
        try
        {
            vRecv >> LIMITED_BYTE_VEC(associationID, AssociationID::MAX_ASSOCIATION_ID_LENGTH);
            vRecv >> streamTypeRaw;
            vRecv >> LIMITED_STRING(streamPolicyName, MAX_STREAM_POLICY_NAME_LENGTH);
        }
        catch(std::exception& e)
        {
            throw std::runtime_error("Badly formatted message");
        }

        // Parse stream type
        if(streamTypeRaw >= static_cast<uint8_t>(StreamType::MAX_STREAM_TYPE))
        {
            throw std::runtime_error("StreamType out of range");
        }
        StreamType streamType { static_cast<StreamType>(streamTypeRaw) };

        // Parse association ID
        AssociationIDPtr idptr { AssociationID::Make(associationID) };
        if(idptr == nullptr)
        {
            throw std::runtime_error("NULL association ID");
        }
        LogPrint(BCLog::NETCONN, "Got request for new %s stream within association %s, peer=%d\n",
            enum_cast<std::string>(streamType), idptr->ToString(), pfrom->id);

        // Move stream to owning association
        CNodePtr newOwner { connman.MoveStream(pfrom->id, idptr, streamType, streamPolicyName) };

        // Send stream ack
        connman.PushMessage(newOwner, CNetMsgMaker(INIT_PROTO_VERSION)
            .Make(NetMsgType::STREAMACK, associationID, streamTypeRaw),
            streamType);

        // Once a node has had its stream moved out it's just an empty husk
        // and should be flagged for shutdown/removal. The actual stream and
        // socket connection will live on however under the new owner.
        pfrom->fDisconnect = true;
    }
    catch(std::exception& e)
    {
        LogPrint(BCLog::NETCONN, "peer=%d Failed to setup new stream (%s); disconnecting\n", pfrom->id, e.what());
        connman.PushMessage(pfrom, CNetMsgMaker(INIT_PROTO_VERSION)
            .Make(NetMsgType::REJECT, strCommand, REJECT_STREAM_SETUP, std::string(e.what())));
        pfrom->fDisconnect = true;
        return false;
    }

    return true;
}

/**
* Process streamack messages.
*/
static bool ProcessStreamAckMessage(const CNodePtr& pfrom,
                                    const std::string& strCommand,
                                    msg_buffer& vRecv,
                                    CConnman& connman)
{
    // Can't receive streamacks over an established connection
    if(pfrom->nVersion != 0)
    {
        connman.PushMessage(pfrom, CNetMsgMaker(INIT_PROTO_VERSION)
            .Make(NetMsgType::REJECT, strCommand, REJECT_NONSTANDARD,
                std::string("Invalid streamack")));
        pfrom->fDisconnect = true;
        return false;
    }

    std::vector<uint8_t> associationID {};
    uint8_t streamTypeRaw {0};

    try
    {
        try
        {
            vRecv >> LIMITED_BYTE_VEC(associationID, AssociationID::MAX_ASSOCIATION_ID_LENGTH);
            vRecv >> streamTypeRaw;
        }
        catch(std::exception& e)
        {
            throw std::runtime_error("Badly formatted message");
        }

        // Parse stream type
        if(streamTypeRaw >= static_cast<uint8_t>(StreamType::MAX_STREAM_TYPE))
        {
            throw std::runtime_error("StreamType out of range");
        }
        StreamType streamType { static_cast<StreamType>(streamTypeRaw) };

        // Parse association ID
        AssociationIDPtr idptr { AssociationID::Make(associationID) };
        if(idptr == nullptr)
        {
            throw std::runtime_error("NULL association ID");
        }
        LogPrint(BCLog::NETCONN, "Got stream ack for new %s stream within association %s, peer=%d\n",
            enum_cast<std::string>(streamType), idptr->ToString(), pfrom->id);

        // Move newly established stream to owning association
        connman.MoveStream(pfrom->id, idptr, streamType);

        // Once a node has had its stream moved out it's just an empty husk
        // and should be flagged for shutdown/removal. The actual stream and
        // socket connection will live on however under the new owner.
        pfrom->fDisconnect = true;
    }
    catch(std::exception& e)
    {
        LogPrint(BCLog::NETCONN, "peer=%d Failed to process stream ack (%s); disconnecting\n", pfrom->id, e.what());
        pfrom->fDisconnect = true;
        return false;
    }

    return true;
}

/**
* Process version messages.
*/
static bool ProcessVersionMessage(const CNodePtr& pfrom,
                                  const std::string& strCommand,
                                  msg_buffer& vRecv,
                                  CConnman& connman,
                                  const Config& config)
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
    uint64_t nNonce = 1;
    uint64_t nServiceInt;
    ServiceFlags nServices;
    int nVersion;
    int nSendVersion;
    std::string strSubVer;
    std::string cleanSubVer;
    int32_t nStartingHeight = -1;
    bool fRelay = true;
    std::vector<uint8_t> associationID {};
    std::string assocIDStr { AssociationID::NULL_ID_STR };

    try {
        vRecv >> nVersion >> nServiceInt >> nTime >> addrMe;

        // Set protocol version
        nSendVersion = std::min(nVersion, PROTOCOL_VERSION);
        pfrom->SetSendVersion(nSendVersion);
        pfrom->nVersion = nVersion;

        nServices = ServiceFlags(nServiceInt);
        if(!pfrom->fInbound) {
            connman.SetServices(pfrom->GetAssociation().GetPeerAddr(), nServices);
        }
        if(pfrom->nServicesExpected & ~nServices) {
            LogPrint(BCLog::NETCONN, "peer=%d does not offer the expected services "
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
            LogPrint(BCLog::NETCONN, "peer=%d using obsolete version %i; disconnecting\n",
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

        if(!vRecv.empty()) 
        {
            CAddress addrFrom;
            vRecv >> addrFrom >> nNonce;
        }
        if(!vRecv.empty()) {
            vRecv >> LIMITED_STRING(strSubVer, MAX_SUBVERSION_LENGTH);
            cleanSubVer = SanitizeString(strSubVer);
            
            if (config.IsClientUABanned(cleanSubVer))
            {
                LogPrint(BCLog::NETCONN, "Client UA is banned (%s) peer=%d\n", cleanSubVer, pfrom->id);
                Misbehaving(pfrom, config.GetBanScoreThreshold(), "invalid-UA");
                return false;
            }
        }
        if(!vRecv.empty()) {
            vRecv >> nStartingHeight;
        }
        if(!vRecv.empty()) {
            vRecv >> fRelay;
        }

        if(!vRecv.empty()) {
            try {
                vRecv >> LIMITED_BYTE_VEC(associationID, AssociationID::MAX_ASSOCIATION_ID_LENGTH);
                if(config.GetMultistreamsEnabled()) {
                    // Decode received association ID
                    AssociationIDPtr recvdAssocID { AssociationID::Make(associationID) };
                    if(recvdAssocID) {
                        assocIDStr = recvdAssocID->ToString();

                        // If we sent them an assoc ID, make sure they echoed back the same one
                        const AssociationIDPtr& currAssocID { pfrom->GetAssociation().GetAssociationID() };
                        if(currAssocID != nullptr) {
                            if(!(*recvdAssocID == *currAssocID)) {
                                throw std::runtime_error("Mismatched association IDs");
                            }
                        }
                        else {
                            // Set association ID for node
                            pfrom->GetAssociation().SetAssociationID(std::move(recvdAssocID));
                        }
                    }
                    else {
                        // Peer sent us a null ID, so they support streams but have disabled them
                        pfrom->GetAssociation().ClearAssociationID();
                    }
                }
            }
            catch(std::exception& e) {
                // Re-throw
                std::stringstream err {};
                err << "Badly formatted association ID: " << e.what();
                throw std::runtime_error(err.str());
            }
        }
        else if(!pfrom->fInbound) {
            // Remote didn't echo back the association ID, so they don't support streams
            pfrom->GetAssociation().ClearAssociationID();
        }
    }
    catch(std::exception& e) {
        LogPrint(BCLog::NETCONN, "peer=%d Failed to process version: (%s); disconnecting\n", pfrom->id, e.what());
        connman.PushMessage(pfrom, CNetMsgMaker(INIT_PROTO_VERSION)
            .Make(NetMsgType::REJECT, strCommand, REJECT_STREAM_SETUP, std::string(e.what())));
        pfrom->fDisconnect = true;
        return false;
    }

    // Disconnect if we connected to ourself
    if(pfrom->fInbound && !connman.CheckIncomingNonce(nNonce)) {
        LogPrint(BCLog::NETCONN, "connected to self at %s, disconnecting\n",
                 pfrom->GetAssociation().GetPeerAddr().ToString());
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
    PushProtoconf(pfrom, connman, config);

    pfrom->nServices = nServices;
    pfrom->GetAssociation().SetPeerAddrLocal(addrMe);
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

    // Potentially mark this peer as a preferred download peer.
    UpdatePreferredDownload(pfrom);

    CAddress peerAddr { pfrom->GetAssociation().GetPeerAddr() };

    if(!pfrom->fInbound) {
        // Advertise our address
        if(fListen && !IsInitialBlockDownload()) {
            CAddress addr =
                GetLocalAddress(&peerAddr, pfrom->GetLocalServices());
            FastRandomContext insecure_rand;
            if(addr.IsRoutable()) {
                LogPrint(BCLog::NETCONN,
                         "ProcessMessages: advertising address %s\n",
                         addr.ToString());
                pfrom->PushAddress(addr, insecure_rand);
            }
            else if(IsPeerAddrLocalGood(pfrom)) {
                addr.SetIP(addrMe);
                LogPrint(BCLog::NETCONN,
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
        connman.MarkAddressGood(peerAddr);
    }

    std::string remoteAddr;
    if(fLogIPs) {
        remoteAddr = ", peeraddr=" + peerAddr.ToString();
    }

    LogPrint(BCLog::NETMSG, "receive version message: [%s] %s: version %d, blocks=%d, "
              "us=%s, assocID=%s, peer=%d%s\n",
              peerAddr.ToString().c_str(), cleanSubVer, pfrom->nVersion,
              pfrom->nStartingHeight, addrMe.ToString(), assocIDStr,
              pfrom->id, remoteAddr);

    int64_t nTimeOffset = nTime - GetTime();
    pfrom->nTimeOffset = nTimeOffset;
    AddTimeData(peerAddr, nTimeOffset);

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
* Process version ack message.
*/
static void ProcessVerAckMessage(const CNodePtr& pfrom, const CNetMsgMaker& msgMaker,
    CConnman& connman)
{
    pfrom->SetRecvVersion(std::min(pfrom->nVersion.load(), PROTOCOL_VERSION));

    const CAddress& peerAddr { pfrom->GetAssociation().GetPeerAddr() };
    if(!pfrom->fInbound) {
        // Try to obtain an access to the node's state data.
        const CNodeStateRef stateRef { GetState(pfrom->GetId()) };
        const CNodeStatePtr& state { stateRef.get() };
        assert(state);
        // Mark this node as currently connected, so we update its timestamp later.
        state->fCurrentlyConnected = true;
        LogPrintf("New outbound peer connected: version: %d, blocks=%d, peer=%d%s\n",
                  pfrom->nVersion.load(), pfrom->nStartingHeight, pfrom->GetId(),
                  (fLogIPs ? strprintf(", peeraddr=%s", peerAddr.ToString()) : ""));
    }
    else {
        LogPrintf("New inbound peer connected: version: %d, subver: %s, blocks=%d, peer=%d%s\n",
                  pfrom->nVersion.load(), pfrom->cleanSubVer, pfrom->nStartingHeight, pfrom->GetId(),
                  (fLogIPs ? strprintf(", peeraddr=%s", peerAddr.ToString()) : ""));
    }
    // Create and send the authch network message.
    uint256 rndMsgHash { GetRandHash() };
    {
            LOCK(pfrom->cs_authconn);
            pfrom->authConnData.msgHash = rndMsgHash;
    }
    using namespace authconn;
    connman.
        PushMessage(pfrom, msgMaker.Make(NetMsgType::AUTHCH, AUTHCH_V1, AUTHCH_MSG_SIZE_IN_BYTES_V1, rndMsgHash));
    // Add a log message.
    LogPrint(BCLog::NETCONN, "Sent authch message (version: %d, nMsgLen: %d, msg: %s), to peer=%d\n",
       AUTHCH_V1, AUTHCH_MSG_SIZE_IN_BYTES_V1, rndMsgHash.ToString(), pfrom->id);

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
* Process authch message.
*/
static bool ProcessAuthChMessage(const Config& config,
                                 const CNodePtr& pfrom,
                                 const CNetMsgMaker& msgMaker,
                                 const std::string& strCommand,
                                 msg_buffer& vRecv,
                                 CConnman& connman)
{
    // Skip the message if the AuthConn has already been established.
    if (pfrom->fAuthConnEstablished) {
        return true;
    }
    using namespace authconn;
    uint32_t nVersion {0};
    uint32_t nMsgLen {0};
    uint256 msg {};

    try {
        // Read data from the message.
        vRecv >> nVersion;
        vRecv >> nMsgLen;
        vRecv >> msg;
        // Add a log message.
        LogPrint(BCLog::NETCONN, "Got authch message (version: %d, nMsgLen: %d, msg: %s), from peer=%d\n",
            nVersion, nMsgLen, msg.ToString(), pfrom->id);
        if (AUTHCH_V1 != nVersion) {
            throw std::runtime_error("Unsupported authch message version= "+ std::to_string(AUTHCH_V1));
        }

        /**
         * Create signature.
         *
         * Make a hash of the following data and sign it:
         * (a) the received auth challenge message, (b) the nonce we've generated.
         */
        // Generate our nonce.
        uint64_t nClientNonce {0};
        while (nClientNonce == 0) {
            GetRandBytes((uint8_t *)&nClientNonce, sizeof(nClientNonce));
        }
        // Create the message to be signed.
        uint256 hash {};
        CHash256()
            .Write(msg.begin(), msg.size()) // (a)
            .Write(reinterpret_cast<uint8_t*>(&nClientNonce), 8) // (b)
            .Finalize(hash.begin());

        // Get the current MinerID from this node
        std::optional<CPubKey> pubKeyOpt = g_BlockDatarefTracker->get_current_minerid();

        try {
            using namespace rpc::client;
            if (g_pWebhookClient) {
                RPCClientConfig rpcConfig { RPCClientConfig::CreateForMinerIdGenerator(config, 5) };
                using HTTPRequest = rpc::client::HTTPRequest;
                auto request { std::make_shared<HTTPRequest>(HTTPRequest::CreateGetMinerIdRequest(
                                        rpcConfig,
                                        config.GetMinerIdGeneratorAlias())) };
                auto response { std::make_shared<StringHTTPResponse>() };
                auto fut = g_pWebhookClient->SubmitRequest(rpcConfig, std::move(request), std::move(response));
                fut.wait();
                auto futResponse = fut.get();
                const StringHTTPResponse* r = dynamic_cast<StringHTTPResponse*> (futResponse.get());
                if (!r)
                    throw std::runtime_error("Could not get the miner-id from the MinerID Generator.");

                pubKeyOpt = CPubKey(ParseHex(r->GetBody()));
                if (!pubKeyOpt)
                    throw std::runtime_error("Could not get the miner-id from the MinerID Generator.");

                auto docinfo = GetMinerCoinbaseDocInfo(*g_minerIDs, *pubKeyOpt);
                if (!docinfo)
                    throw std::runtime_error("Miner-id from MinerID Generator is not in the minerid database.");

                g_BlockDatarefTracker->set_current_minerid(*pubKeyOpt);
            }
            if (!pubKeyOpt) {
                throw std::runtime_error ("Ignoring authch messages until this node has mined a block containing a miner_info document\n");
            }
        } catch (const std::exception& e) {
            LogPrint(BCLog::MINERID, strprintf("Ignoring authch messages until this node has mined a block containing a miner_info document. %s\n", e.what()));
            return true;
        }

        CPubKey pubKey = pubKeyOpt.value();

        // Create the DER-encoded signature of the expected minimal size.
        // For this purpose send a request to the MinerID Generator which knows the private keys.
        std::vector<uint8_t> vSign {};
        {
            vSign.clear();
            using namespace rpc::client;
            if (!g_pWebhookClient) {
                // we log unconditionally because we already checked that we do have a minerid.
                LogPrintf("No authentication client for minerid authentication instantiated\n");
                // we return true because we still want to connect, but unauthenticated instead.
                return true;
            } else {
                LogPrint(BCLog::MINERID, "sending signature request to MinerID Generator\n");
                RPCClientConfig rpcConfig { RPCClientConfig::CreateForMinerIdGenerator(config, 5) };
                using HTTPRequest = rpc::client::HTTPRequest;
                auto request { std::make_shared<HTTPRequest>(HTTPRequest::CreateMinerIdGeneratorSigningRequest(
                        rpcConfig,
                        config.GetMinerIdGeneratorAlias(),
                        HexStr(hash))) };
                auto response { std::make_shared<JSONHTTPResponse>() };
                auto fut = g_pWebhookClient->SubmitRequest(rpcConfig, std::move(request), std::move(response));
                fut.wait();
                auto futResponse = fut.get();
                const JSONHTTPResponse* r = dynamic_cast<JSONHTTPResponse*> (futResponse.get());
                if (!r)
                    throw std::runtime_error("Signature creation has not returned from the MinerID Generator.");
                const UniValue& uv = r->GetBody();
                if (!uv.isObject() || !uv.exists("signature"))
                    throw std::runtime_error("JSON error, object containing a string with key name \"signature\" expected");
                std::string signedHex = uv["signature"].get_str();
                vSign = ParseHex(signedHex);
            }
        }
        // Check if the signature is correct before sending it.
        if (!pubKey.Verify(hash, vSign)) {
            throw std::runtime_error("Could not create authresp message as the MinerID Generator created signature failed to verify.");
        }
        // Get the public key as a byte's vector.
        std::vector<uint8_t> vPubKey { ToByteVector(pubKey) };
        // Send the authresp message.
        connman.
            PushMessage(pfrom,
                msgMaker.Make(NetMsgType::AUTHRESP, vPubKey, nClientNonce, vSign));
        // Add a log message.
        LogPrint(BCLog::MINERID | BCLog::NETCONN, "Sent authresp message (nPubKeyLen: %d, vPubKey: %s, nClientNonce: %d, nSignLen: %d, vSign: %s), to peer=%d\n",
            vPubKey.size(), HexStr(vPubKey).c_str(), nClientNonce, vSign.size(), HexStr(vSign).c_str(), pfrom->id);
    } catch(std::exception& e) {
        LogPrint(BCLog::MINERID | BCLog::NETCONN, "peer=%d Failed to process authch: (%s)\n", pfrom->id, e.what());
        connman.PushMessage(pfrom, msgMaker
            .Make(NetMsgType::REJECT, strCommand, REJECT_AUTH_CONN_SETUP, std::string(e.what())));
	    // We still connect if authentication failed.
        return false;
    }
    return true;
}

/**
* Process authresp messages.
*/
static bool ProcessAuthRespMessage(const CNodePtr& pfrom,
                                   const std::string& strCommand,
                                   msg_buffer& vRecv,
                                   CConnman& connman)
{
    // Skip the message if the AuthConn has already been established.
    if (pfrom->fAuthConnEstablished) {
        return true;
    }
    using namespace authconn;
    std::vector<uint8_t> vPubKey {};
    uint64_t nClientNonce {0};
    std::vector<uint8_t> vSign {};

    try {
        // Read data from the message.
        vRecv >> LIMITED_BYTE_VEC(vPubKey, SECP256K1_COMP_PUB_KEY_SIZE_IN_BYTES);
        if(SECP256K1_COMP_PUB_KEY_SIZE_IN_BYTES != vPubKey.size())
            throw std::runtime_error("Incorrect nPubKeyLen="+std::to_string(vPubKey.size()));

        vRecv >> nClientNonce;

        vRecv >> LIMITED_BYTE_VEC(vSign, SECP256K1_DER_SIGN_MAX_SIZE_IN_BYTES);
        const size_t vSignSize {vSign.size()};
        if (!(SECP256K1_DER_SIGN_MIN_SIZE_IN_BYTES <= vSignSize &&
              vSignSize <= SECP256K1_DER_SIGN_MAX_SIZE_IN_BYTES))
            throw std::runtime_error("Incorrect vSign.size()=" +
                                     std::to_string(vSignSize));

        // Add a log message.
        LogPrint(BCLog::NETCONN,
                 "Got authresp message (nPubKeyLen: %d, vPubKey: %s, "
                 "nClientNonce: %d, nSignLen: %d, vSign: %s), from peer=%d\n",
                 vPubKey.size(), HexStr(vPubKey).c_str(), nClientNonce,
                 vSignSize, HexStr(vSign).c_str(), pfrom->id);

        /**
         * Verify signature.
         *
         * Recreate the original message and verify the received signature using sender's public key.
         * The message contains: (a) the authch challenge message, (b) the client's nonce.
         */
        uint256 msgHash {};
        {
            LOCK(pfrom->cs_authconn);
            msgHash = pfrom->authConnData.msgHash;
        }
        // Check if the public key has been correctly recreated.
        CPubKey recvPubKey(vPubKey);
        if (!recvPubKey.IsValid()) {
            throw std::runtime_error("Invalid public key data");
        }

        // check if the address is the one advertised in the minerid document
        if (g_minerIDs) {
            auto ipMatchesMineridDocument = [](const CPubKey& minerid, const std::string& socket_addr) {
                auto docinfo = GetMinerCoinbaseDocInfo(*g_minerIDs, minerid);
                if (docinfo) {
                    UniValue uv;
                    uv.read(docinfo->first.GetRawJSON());
                    if (uv.exists("extensions")) {
                        UniValue ex = uv["extensions"];
                        if (ex.isObject() && ex.exists("PublicIP")) {
                            UniValue pk = ex["PublicIP"];
                            if (pk.isStr() && pk.get_str() == socket_addr) {
                                return true;
                            }
                        }
                    }
                }
                return false;
            };

            const Association& assoc = pfrom->GetAssociation();
            const std::string addr = assoc.GetPeerAddr().ToStringIP();
            if (!ipMatchesMineridDocument(recvPubKey, addr))
                throw std::runtime_error("Public ip address does not match the one advertised in the miner info document.");
        }

        // Does the miner identified with the given miner ID have a good reputation?
        // 1. true (if all listed conditions are met):
        //    - the public key is present and marked as a valid key in the MinerID DB
        //    - the miner identified by the public key passes further criterion, such as 'the M of the last N' check
        // 2. false (if any of the listed conditions is met):
        //    - the public key is not present or it is marked as an invalid key in the MinerID DB
        //    - the miner identified by the public key doesn't pass further criterion, such as 'the M of the last N' check
        if (g_minerIDs && !MinerHasGoodReputation(*g_minerIDs, recvPubKey)) {
            LogPrint(BCLog::NETCONN, "Authentication has failed. The miner identified with the minerId= %s doesn't have a good reputation, peer= %d\n",
                HexStr(ToByteVector(recvPubKey)).c_str(), pfrom->id);
            return true;
        }
        // Recreate the message.
        uint256 hash;
        CHash256()
            .Write(msgHash.begin(), msgHash.size()) // (a)
            .Write(reinterpret_cast<uint8_t*>(&nClientNonce), 8) // (b)
            .Finalize(hash.begin());
        // Execute verification.
        if (!recvPubKey.Verify(hash, vSign)) {
            throw std::runtime_error("authresp message signature failed to verify.");
        }
    } catch(std::exception& e) {
        LogPrint(BCLog::NETCONN, "peer=%d Failed to process authresp: (%s); disconnecting\n", pfrom->id, e.what());
        connman.PushMessage(pfrom, CNetMsgMaker(INIT_PROTO_VERSION)
            .Make(NetMsgType::REJECT, strCommand, REJECT_AUTH_CONN_SETUP, std::string(e.what())));
	    // We still connect if authentication failed.
        return false;
    }

    // Mark the connection as successfully established.
    pfrom->fAuthConnEstablished = true;
    // Add a log message.
    LogPrint(BCLog::NETCONN, "Authenticated connection has been established with the remote peer=%d\n", pfrom->id);

    return true;
}

/**
* Process peer address message.
*/
static bool ProcessAddrMessage(const CNodePtr& pfrom,
                               const std::atomic<bool>& interruptMsgProc,
                               msg_buffer& vRecv,
                               CConnman& connman)
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

    const CAddress& peerAddr { pfrom->GetAssociation().GetPeerAddr() };

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
            if (static_cast<CNetAddr>(addr) == static_cast<CNetAddr>(peerAddr))
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
            LogPrint(BCLog::NETMSG, "Peer %d sent unsolicited ADDR\n", pfrom->id);

            // We don't want to process any other addresses, but giving them is not an error
            return true;
        }
    }

    // Store the new addresses
    std::vector<CAddress> vAddrOk;
    int64_t nNow = GetAdjustedTime();
    int64_t nSince = nNow - 10 * 60;
    for (CAddress& addr : vAddr)
    {
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
        if (addr.nTime > nSince && vAddr.size() <= 10 && addr.IsRoutable()) {
            // Relay to a limited number of other nodes
            RelayAddress(addr, fReachable, connman);
        }
        // Do not store addresses outside our network
        if (fReachable) {
            vAddrOk.push_back(addr);
        }
    }
    connman.AddNewAddresses(vAddrOk, peerAddr, 2 * 60 * 60);
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
            LogPrint(BCLog::NETMSG, "Peer %d sent SendHeaders more than once\n", pfrom->id);
        }
        else {
            state->fPreferHeaders = true;
        }
    }
}

/**
* Process sendhdrsen message.
*/
static void ProcessSendHdrsEnMessage(const CNodePtr& pfrom)
{
    // Try to obtain an access to the node's state data.
    const CNodeStateRef stateRef { GetState(pfrom->GetId()) };
    const CNodeStatePtr& state { stateRef.get() };
    if(state)
    {
        if(state->fPreferHeadersEnriched)
        {
            // This message should only be received once. If its already set it might
            // indicate a misbehaving node. Increase the banscore
            Misbehaving(pfrom, 1, "Invalid SendHdrsEn activity");
            LogPrint(BCLog::NETMSG, "Peer %d sent SendHdrsEn more than once\n", pfrom->id);
        }
        else
        {
            state->fPreferHeadersEnriched = true;
        }
    }
}

/**
* Process send compact message.
*/
static void ProcessSendCompactMessage(const CNodePtr& pfrom, msg_buffer& vRecv)
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
                              msg_buffer& vRecv,
                              CConnman& connman,
                              const Config &config)
{
    std::vector<CInv> vInv;
    vRecv >> vInv;
    bool fBlocksOnly = !fRelayTxes;

    // Allow whitelisted peers to send data other than blocks in blocks only
    // mode if whitelistrelay is true
    if(pfrom->fWhitelisted && config.GetWhitelistRelay()) {
        fBlocksOnly = false;
    }

    LOCK(cs_main);
    for(size_t nInv = 0; nInv < vInv.size(); nInv++) {
        CInv &inv = vInv[nInv];

        if(interruptMsgProc) {
            return;
        }

        bool fAlreadyHave = AlreadyHave(inv);

        if(inv.type == MSG_BLOCK) {
            LogPrint(BCLog::NETMSG, "got block inv: %s %s peer=%d\n", inv.hash.ToString(),
                fAlreadyHave ? "have" : "new", pfrom->id);
            UpdateBlockAvailability(inv.hash, GetState(pfrom->GetId()).get());
            if(!fAlreadyHave && !fImporting && !fReindex && !blockDownloadTracker.IsInFlight(inv.hash)) {
                const auto& bestHeader = mapBlockIndex.GetBestHeader();
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
                                  chainActive.GetLocator(&bestHeader),
                                  inv.hash));
                LogPrint(BCLog::NETMSG, "getheaders (%d) %s to peer=%d\n",
                         bestHeader.GetHeight(), inv.hash.ToString(),
                         pfrom->id);
            }
        }
        else {
            LogPrint(BCLog::TXNSRC | BCLog::NETMSGVERB, "got txn inv: %s %s txnsrc peer=%d\n",
                inv.hash.ToString(), fAlreadyHave ? "have" : "new", pfrom->id);
            pfrom->AddInventoryKnown(inv);
            if(fBlocksOnly) {
                LogPrint(BCLog::NETMSGVERB, "transaction (%s) inv sent in violation of protocol peer=%d\n",
                         inv.hash.ToString(), pfrom->id);
            }
            else if(!fAlreadyHave && !fImporting && !fReindex && !IsInitialBlockDownload()) {
                pfrom->AskFor(inv, config);
            }
        }

        // Track requests for our stuff
        GetMainSignals().Inventory(inv.hash);
    }
}

/**
* Process get data message.
*/
static void ProcessGetDataMessage(const Config& config,
                                  const CNodePtr& pfrom,
                                  const CChainParams& chainparams,
                                  const std::atomic<bool>& interruptMsgProc,
                                  msg_buffer& vRecv,
                                  CConnman& connman)
{
    std::vector<CInv> vInv;
    vRecv >> vInv;

    if(vInv.size() == 1) {
        LogPrint(BCLog::NETMSG, "received getdata for: %s peer=%d\n", vInv[0].ToString(), pfrom->id);
    }
    else {
        LogPrint(BCLog::NETMSG, "received getdata (%u invsz) peer=%d\n", vInv.size(), pfrom->id);
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
    LogPrint(BCLog::NETMSG, "getblocks %d to %s limit %d from peer=%d\n",
             (pindex ? pindex->GetHeight() : -1),
             hashStop.IsNull() ? "end" : hashStop.ToString(), nLimit,
             pfrom->id);
    for(; pindex; pindex = chainActive.Next(pindex)) {
        if(pindex->GetBlockHash() == hashStop) {
            LogPrint(BCLog::NETMSG, "  getblocks stopping at %d %s\n",
                     pindex->GetHeight(), pindex->GetBlockHash().ToString());
            break;
        }
        // If pruning, don't inv blocks unless we have on disk and are
        // likely to still have for some reasonable time window (1 hour)
        // that block relay might require.
        const int nPrunedBlocksLikelyToHave = config.GetMinBlocksToKeep() - 3600 / chainparams.GetConsensus().nPowTargetSpacing;
        if(fPruneMode &&
            (!pindex->getStatus().hasData() ||
             pindex->GetHeight() <=
                 chainActive.Tip()->GetHeight() - nPrunedBlocksLikelyToHave)) {
            LogPrint(
                BCLog::NETMSG,
                " getblocks stopping, pruned or too old block at %d %s\n",
                pindex->GetHeight(), pindex->GetBlockHash().ToString());
            break;
        }
        pfrom->PushBlockInventory(CInv(MSG_BLOCK, pindex->GetBlockHash()));
        if(--nLimit <= 0) {
            // When this block is requested, we'll send an inv that'll
            // trigger the peer to getblocks the next batch of inventory.
            LogPrint(BCLog::NETMSG, "  getblocks stopping at limit %d %s\n",
                     pindex->GetHeight(), pindex->GetBlockHash().ToString());
            pfrom->hashContinue = pindex->GetBlockHash();
            break;
        }
    }

    return true;
}

static void ProcessGetBlocksMessage(const Config& config,
                                    const CNodePtr& pfrom,
                                    const CChainParams& chainparams,
                                    msg_buffer& vRecv)
{
    pfrom->mGetBlockMessageRequest = {vRecv};
    if(ProcessGetBlocks(config, pfrom, chainparams, *pfrom->mGetBlockMessageRequest))
    {
        pfrom->mGetBlockMessageRequest = std::nullopt;
    }
    else
    {
        LogPrint(
            BCLog::NETMSG,
            "Blocks that were received before getblocks message are still"
            " waiting as a candidate. Deferring getblocks reply.\n");
    }
}

namespace
{

// Interface for classes to fetch BlockTxn transactions
class BlockTransactionReader
{
  public:
    virtual ~BlockTransactionReader() = default;

    virtual size_t GetNumTxnsInBlock() const = 0;

    // Return transaction at index from block. Subsequent calls
    // must be for indexes higher than the previous call.
    virtual CTransactionRef GetTransactionIndex(size_t index) = 0;
};

// Fetch BlockTxn transactions from a block file
class DiskBlockTransactionReader : public BlockTransactionReader
{
  public:
    DiskBlockTransactionReader(std::unique_ptr<CBlockStreamReader<CFileReader>> reader)
        : BlockTransactionReader{}, mReader{std::move(reader)},
          mNumTxnsInBlock{mReader->GetRemainingTransactionsCount()}
    {}

    size_t GetNumTxnsInBlock() const override { return mNumTxnsInBlock; }

    CTransactionRef GetTransactionIndex(size_t index) override
    {
        if(index >= mNumTxnsInBlock)
        {
            throw std::runtime_error("Index out-of-bounds");
        }

        // Transaction indexes in request are assumed to be sorted in ascending order without duplicates.
        // This must always be true since indexes are differentially encoded in getblocktxn P2P message.
        assert(index >= mNumTxnsRead);

        // Read from block stream until we get to the transaction with requested index.
        for(; mNumTxnsRead <= index; ++mNumTxnsRead)
        {
            assert(! mReader->EndOfStream()); // We should never get pass the end of stream since we checked transaction index above.
            auto* tx_ptr = mReader->ReadTransaction_NoThrow();
            (void)tx_ptr; // not used except for assert
            assert(tx_ptr); // Reading block should not fail
        }

        // CBlockStreamReader object now holds transaction with requested index.
        // Take ownership of transaction object and store transaction reference in the response.
        return mReader->GetLastTransactionRef();
    }

  private:

    std::unique_ptr<CBlockStreamReader<CFileReader>> mReader {nullptr};
    size_t mNumTxnsInBlock {0};
    size_t mNumTxnsRead {0};
};

// Fetch BlockTxn transactions from the cached latest block
class CachedBlockTransactionReader : public BlockTransactionReader
{
  public:
    CachedBlockTransactionReader(const CBlock& block)
        : BlockTransactionReader{}, mBlock{block}
    {}

    size_t GetNumTxnsInBlock() const override { return mBlock.vtx.size(); }

    CTransactionRef GetTransactionIndex(size_t index) override
    {
        if(index >= mBlock.vtx.size())
        {
            throw std::runtime_error("Index out-of-bounds");
        }

        return mBlock.vtx[index];
    }

  private:

    const CBlock& mBlock;
};

void SendBlockTransactions(const Config& config,
                           const CNodePtr& pfrom,
                           const CChainParams& chainparams,
                           const std::atomic<bool>& interruptMsgProc,
                           const BlockTransactionsRequest& req,
                           BlockTransactionReader& reader,
                           bool mostRecentBlock,
                           CConnman& connman)
{
    // If the peer wants more than the configured % of txns in the original block, just stream them the whole thing
    size_t numTxnsRequested { req.indices.size() };
    size_t numTxnsInBlock { reader.GetNumTxnsInBlock() };
    if(numTxnsInBlock > 0)
    {
        double percentRequested { (static_cast<double>(numTxnsRequested) / numTxnsInBlock) * 100 };
        unsigned maxPercent { config.GetBlockTxnMaxPercent() };
        if(percentRequested > maxPercent)
        {
            LogPrint(BCLog::NETMSG, "Peer %d sent us a getblocktxn wanting %f%% of txns "
                "which is more than the configured max of %d%%. Responding with full block\n",
                pfrom->id, percentRequested, maxPercent);
            CInv inv { MSG_BLOCK, req.blockhash };
            pfrom->vRecvGetData.push_back(inv);
            ProcessGetData(config, pfrom, chainparams.GetConsensus(), connman, interruptMsgProc);
            return;
        }
    }

    BlockTransactions resp {req};
    for(size_t i = 0; i < req.indices.size(); i++)
    {
        try
        {
            resp.txn[i] = reader.GetTransactionIndex(req.indices[i]);
        }
        catch(const std::exception&)
        {
            Misbehaving(pfrom, 100, "out-of-bound-tx-index");
            LogPrint(BCLog::NETMSG, "Peer %d sent us a getblocktxn with out-of-bounds tx indices\n", pfrom->id);
            return;
        }
    }

    const CNetMsgMaker msgMaker { pfrom->GetSendVersion() };
    CSerializedNetMsg msg { msgMaker.Make(NetMsgType::BLOCKTXN, resp) };
    if(! rejectIfMaxDownloadExceeded(config, msg, mostRecentBlock, pfrom, connman))
    {
        connman.PushMessage(pfrom, std::move(msg));
    }
}

}

/**
* Process getblocktxn message.
*/
static void ProcessGetBlockTxnMessage(const Config& config,
                                      const CNodePtr& pfrom,
                                      const CChainParams& chainparams,
                                      const std::atomic<bool>& interruptMsgProc,
                                      msg_buffer& vRecv,
                                      CConnman& connman)
{
    BlockTransactionsRequest req {};
    vRecv >> req;

    // See if we can serve this request from the last received cached block
    std::shared_ptr<const CBlock> recent_block { mostRecentBlock.GetBlockIfMatch(req.blockhash) };
    if(recent_block)
    {
        std::unique_ptr<BlockTransactionReader> blockReader { std::make_unique<CachedBlockTransactionReader>(*recent_block) };
        SendBlockTransactions(config, pfrom, chainparams, interruptMsgProc, req, *blockReader, true, connman);
        return;
    }

    LOCK(cs_main);

    auto index = mapBlockIndex.Get(req.blockhash);
    if(!index)
    {
        LogPrint(BCLog::NETMSG, "Peer %d sent us a getblocktxn for a block we don't have\n", pfrom->id);
        return;
    }

    if(index->GetHeight() < chainActive.Height() - MAX_BLOCKTXN_DEPTH)
    {
        // If an older block is requested (should never happen in practice,
        // but can happen in tests) send a block response instead of a
        // blocktxn response. Sending a full block response instead of a
        // small blocktxn response is preferable in the case where a peer
        // might maliciously send lots of getblocktxn requests to trigger
        // expensive disk reads, because it will require the peer to
        // actually receive all the data read from disk over the network.
        LogPrint(BCLog::NETMSG, "Peer %d sent us a getblocktxn for a block > %i deep\n", pfrom->id, MAX_BLOCKTXN_DEPTH);
        CInv inv { MSG_BLOCK, req.blockhash };
        pfrom->vRecvGetData.push_back(inv);
        ProcessGetData(config, pfrom, chainparams.GetConsensus(), connman, interruptMsgProc);
        return;
    }

    // Create stream reader object that will be used to read block from disk.
    auto blockStreamReader = index->GetDiskBlockStreamReader(config, false); // Disk block meta-data is not needed and does not need to be calculated.
    if(!blockStreamReader)
    {
        LogPrint(BCLog::NET, "Peer %d sent us a getblocktxn for a block we don't have\n", pfrom->id);
        return;
    }

    std::unique_ptr<BlockTransactionReader> blockReader { std::make_unique<DiskBlockTransactionReader>(std::move(blockStreamReader)) };
    bool isTip { req.blockhash == chainActive.Tip()->GetBlockHash() };
    SendBlockTransactions(config, pfrom, chainparams, interruptMsgProc, req, *blockReader, isTip, connman);
}


namespace {

/**
 * Returns pointer to the first block specified by locator or nullptr.
 *
 * Returns nullopt if locator is not specified and block in hashStop is not found.
 */
std::optional<const CBlockIndex*> GetFirstBlockIndexFromLocatorNL(const CBlockLocator& locator, const uint256& hashStop)
{
    AssertLockHeld(cs_main);

    const CBlockIndex* pindex = nullptr;
    if(locator.IsNull())
    {
        // If locator is null, return the hashStop block
        pindex = mapBlockIndex.Get(hashStop);
        if (!pindex)
        {
            return {};

        }
    }
    else 
    {
        // Find the last block the caller has in the main chain
        pindex = FindForkInGlobalIndex(chainActive, locator);
        if(pindex) 
        {
            pindex = chainActive.Next(pindex);
        }
    }

    return pindex;
}

/**
 * Helper class template used to create CVectorStream objects that will track number of pending responses.
 *
 * It is used in ProcessGetHeadersMessage() and ProcessGetHeadersEnrichedMessage().
 *
 * @tparam PMemPendingResponses Pointer to data member in CNode::MonitoredPendingResponses that keeps count of pending
 *                              responses for the specific request type (e.g. &CNode::MonitoredPendingResponses::getheaders
 *                              for getheaders P2P requests).
 */
template<CNode::MonitoredPendingResponses::PendingResponses CNode::MonitoredPendingResponses::* PMemPendingResponses>
class CreateHeaderStreamWithPendingResponsesCounting
{
public:
    explicit CreateHeaderStreamWithPendingResponsesCounting(const CNodePtr& pfrom)
    : pfrom_weak( pfrom )
    {}

    std::unique_ptr<CVectorStream> operator()(std::vector<uint8_t>&& serialisedHeader)
    {
        struct CVectorStream_WithPendingResponsesCounting : CVectorStream
        {
            /**
             * Constructor forwards the data to constructor of CVectorStream and increments the pending responses count.
             *
             * This object is intended to be constructed when the response is added to the sending queue.
             *
             * @param data
             * @param pfrom_weak0
             */
            CVectorStream_WithPendingResponsesCounting(std::vector<uint8_t>&& data, CNodePtr::weak_type&& pfrom_weak0)
            : CVectorStream(std::move(data))
            , pfrom_weak(std::move(pfrom_weak0))
            {
                // When the header stream object is created, the response is considered to be pending.
                // Note that this will happen during the call to connman.PushMessage() that adds the message to the sending P2P queue.
                if(auto pfrom = this->pfrom_weak.lock())
                {
                    (pfrom->pendingResponses.*PMemPendingResponses).Increment();
                }
            }

            CVectorStream_WithPendingResponsesCounting(CVectorStream_WithPendingResponsesCounting&&) = delete; // no copying or moving

            /**
             * Destructor decrements the pending responses count.
             *
             * This object is intended to be destroyed when the response is taken out from the sending queue.
             */
            ~CVectorStream_WithPendingResponsesCounting()
            {
                // When the header stream object is destroyed, the response is considered to be sent.
                // If connection is still alive, number of pending responses is decremented. If it is not,
                // nothing is done since the counter does not exist anymore.
                // Note that the sender of the request did not yet receive the response at this time and it
                // is (probably) still in the TCP send buffer (which may contain several responses that
                // are not yet received by the sender of the request). Actual response payload may also be
                // stored in a different message (immediately after this one) that is still in the sending
                // queue. In addition, this object may also be destroyed even without sending the response
                // (e.g. if connection is closed).
                // All this means that the value in member PMemPendingResponses may be a bit lower than
                // the number of responses the sender still has to receive. But this doesn't matter because
                // we're only interested in detecting too large number of responses still waiting in the
                // sending queue.
                if(auto pfrom = pfrom_weak.lock())
                {
                    (pfrom->pendingResponses.*PMemPendingResponses).Decrement();
                }
            }

            /**
             * Return the memory used by this object
             */
            size_t GetEstimatedMaxMemoryUsage() const override
            {
                // Used memory reported by the base class must be adjusted to account for additional
                // data member in this class.
                return this->CVectorStream::GetEstimatedMaxMemoryUsage() + (sizeof(*this) - sizeof(CVectorStream));
            }

            CNodePtr::weak_type pfrom_weak;
        };

        return std::make_unique<CVectorStream_WithPendingResponsesCounting>( std::move(serialisedHeader), std::move(pfrom_weak) );
    }

private:
    CNodePtr::weak_type pfrom_weak;
};

} // anonymous namespace

/**
* Process get headers message.
*/
static void ProcessGetHeadersMessage(const CNodePtr& pfrom,
                                     const CNetMsgMaker& msgMaker,
                                     msg_buffer& vRecv,
                                     CConnman& connman)
{
    if(unsigned int n; !pfrom->fWhitelisted && !pfrom->pendingResponses.getheaders.IsBelowLimit(n))
    {
        // If number of pending responses is too large, the request is ignored and the peer is disconnected.
        // NOTE: Because we check if the number of pending responses is below limit before adding a new response,
        //       it is possible that the number becomes a bit higher than specified maximum. E.g.: If two threads
        //       running at the same time both see max-1 pending responses, they will both proceed to add a new
        //       response, which will result in max+1 pending responses. But this is OK, since we don't need to
        //       enforce the exact maximum. If, on the other hand, an exact maximum would need to be enforced
        //       (e.g. no more than one pending response guarantee), this would need to be addressed properly and
        //       would probably introduce some performance overhead.
        LogPrint(BCLog::NETMSG, "Ignoring getheaders and disconnecting the peer because there are too many (%d, max=%d) pending responses to previously received getheaders from peer=%d.\n",
            n, pfrom->pendingResponses.getheaders.GetMaxAllowed(), pfrom->id);
        pfrom->fDisconnect = true;
        return;
    }

    CBlockLocator locator;
    uint256 hashStop;
    vRecv >> locator >> hashStop;

    LOCK(cs_main);
    if(IsInitialBlockDownload() && !pfrom->fWhitelisted) {
        LogPrint(BCLog::NETMSG, "Ignoring getheaders from peer=%d because "
                                "node is in initial block download\n",
                 pfrom->id);
        return;
    }

    const CBlockIndex* pindex = nullptr;
    if(auto opt_block_index = GetFirstBlockIndexFromLocatorNL(locator, hashStop))
    {
        pindex = *opt_block_index;
    }
    else
    {
        LogPrint(BCLog::NETMSG, "Ignoring getheaders from peer=%d because "
                                "it requested unknown block (hashstop=%s) without locator\n",
                 pfrom->id, hashStop.ToString());
        return;
    }

    // We must use CBlocks, as CBlockHeaders won't include the 0x00 nTx
    // count at the end
    std::vector<CBlock> vHeaders;
    int nLimit = MAX_HEADERS_RESULTS;
    LogPrint(BCLog::NETMSG, "getheaders %d to %s from peer=%d\n",
             (pindex ? pindex->GetHeight() : -1),
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

    auto msg = msgMaker.Make(NetMsgType::HEADERS, vHeaders);
    if(!pfrom->fWhitelisted)
    {
        msg.headerStreamCreator = CreateHeaderStreamWithPendingResponsesCounting<&CNode::MonitoredPendingResponses::getheaders>(pfrom);
    }
    connman.PushMessage(pfrom, std::move(msg));
}

namespace {

/**
 * Defines the structure of hdrsen message and holds data needed to create it
 */
class CBlockHeaderEnriched
{
    struct TxnAndProof
    {
        // Contains Merkle proof in binary TSC format (https://tsc.bitcoinassociation.net/standards/merkle-proof-standardised-format)
        using TSCMerkleProof = ::MerkleProof;

        // Serialisation
        ADD_SERIALIZE_METHODS
        template <typename Stream, typename Operation>
        inline void SerializationOp(Stream& s, Operation ser_action)
        {
            READWRITE(txn);
            READWRITE(proof);
        }

        CTransactionRef txn {nullptr};
        TSCMerkleProof proof {};
    };

public:
    CBlockHeader blockHeader {};
    uint64_t nTx{ 0 };
    bool noMoreHeaders{ false };
    std::optional<TxnAndProof> coinbaseAndProof { std::nullopt };
    std::optional<TxnAndProof> minerInfoAndProof { std::nullopt };

    // Pointer to block index object is only included in object so that it can be used
    // when setting (non-const) data members after construction.
    const CBlockIndex* blockIndex{nullptr};

    /**
     * Return size (in bytes) of serialized message when transmitted over the network
     */
    std::size_t GetSerializedSize() const
    {
        return GetSerializeSize(*this, SER_NETWORK, PROTOCOL_VERSION);
    }

    // Default constructor is only needed by serialization framework
    CBlockHeaderEnriched() = default;

    CBlockHeaderEnriched(const CBlockIndex* blockIndex)
    : blockHeader { blockIndex->GetBlockHeader() }
    , nTx { blockIndex->GetBlockTxCount() }
    , blockIndex { blockIndex }
    {
    }

    // Serialisation
    ADD_SERIALIZE_METHODS
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(blockHeader);
        READWRITECOMPACTSIZE(nTx);
        READWRITE(noMoreHeaders);
        READWRITE(coinbaseAndProof);
        READWRITE(minerInfoAndProof);
    }

    /**
     * Set values of members that provide information about coinbase transaction
     * by reading the data from block file and Merkle tree factory.
     *
     * If data is not available, values of these members are cleared and hasCoinbaseData
     * is set to false.
     *
     * @param version Serialization version (including any flags) used to serialize coinbase transaction.
     * @param config Needed by CMerkleTreeFactory.
     * @param chainActiveHeight Current height of active chain. Needed by CMerkleTreeFactory.
     */
    void SetCoinBaseInfo(int serializationVersion, const Config& config, int32_t chainActiveHeight)
    {
        coinbaseAndProof.reset();
        try
        {
            auto blockReader = blockIndex->GetDiskBlockStreamReader();
            if(blockReader)
            {
                // Read CB txn from disk
                CTransaction cbTx { blockReader->ReadTransaction() };
                coinbaseAndProof = std::make_optional<TxnAndProof>(TxnAndProof{});

                coinbaseAndProof->proof.TxnId(cbTx.GetId());
                coinbaseAndProof->proof.Target(blockIndex->GetBlockHash());

                coinbaseAndProof->txn = MakeTransactionRef(std::move(cbTx));

                // Default constructor should set these to values we expect here
                assert( coinbaseAndProof->proof.Flags() == 0 ); // Always set to 0 because we're providing transaction ID in txOrId, block hash in target, Merkle branch as proof in nodes and there is a single proof
                assert( coinbaseAndProof->proof.Index() == 0 ); // Always set to 0, because we're providing proof for coinbase transaction

                // See if this coinbase contains a miner-info reference
                if(g_dataRefIndex && coinbaseAndProof->txn->vout.size() > 1)
                {
                    const span<const uint8_t> script { coinbaseAndProof->txn->vout[1].scriptPubKey };
                    if(IsMinerInfo(script))
                    {
                        const auto& mir { ParseMinerInfoRef(script) };
                        if(std::holds_alternative<miner_info_ref>(mir))
                        {
                            const auto& ref { std::get<miner_info_ref>(mir) };

                            // Lookup miner-info txn in the dataref index
                            const auto& locking { g_dataRefIndex->CreateLockingAccess() };
                            const auto& minerInfo { locking.GetMinerInfoEntry(ref.txid()) };
                            if(minerInfo)
                            {
                                // Return txn and proof details for the miner-info txn as well
                                minerInfoAndProof = std::make_optional<TxnAndProof>(TxnAndProof{});
                                minerInfoAndProof->txn = minerInfo->txn;
                                minerInfoAndProof->proof = minerInfo->proof;
                            }
                        }
                    }
                }
            }
        }
        catch(...)
        {
            LogPrint(BCLog::NETMSG, "hdrsen: Reading of coinbase/miner-info txns failed.\n");
        }

        if(coinbaseAndProof)
        {
            // Get Merkle proof for CB txn from Merkle tree cache.
            // Note that this can be done without holding cs_main lock.
            if(CMerkleTreeRef merkleTree=pMerkleTreeFactory->GetMerkleTree(config, *blockIndex, chainActiveHeight))
            {
                auto merkleTreeHashes = merkleTree->GetMerkleProof(0, false).merkleTreeHashes;

                TxnAndProof::TSCMerkleProof::nodes_type nodes;
                for(const auto& h: merkleTreeHashes)
                {
                    nodes.emplace_back(h);
                    assert( nodes.back().mType ==0 ); // Type of node in Merkle proof is always 0 because we're providing hash in value field
                }
                coinbaseAndProof->proof.Nodes( std::move(nodes) );
            }
            else
            {
                // Delete CB txn if we were unable to get its Merkle proof, since it is not needed.
                coinbaseAndProof.reset();
            }
        }
    }
};

} // anonymous namespace

/**
 * Process enriched get headers message.
 */
static void ProcessGetHeadersEnrichedMessage(const CNodePtr& pfrom,
                                             const CNetMsgMaker& msgMaker,
                                             msg_buffer& vRecv,
                                             CConnman& connman,
                                             const Config& config)
{
    if(unsigned int n; !pfrom->fWhitelisted && !pfrom->pendingResponses.gethdrsen.IsBelowLimit(n))
    {
        // Same comment applies as in ProcessGetHeadersMessage().
        LogPrint(BCLog::NETMSG, "Ignoring gethdrsen and disconnecting the peer because there are too many (%d, max=%d) pending responses to previously received gethdrsen from peer=%d.\n",
            n, pfrom->pendingResponses.gethdrsen.GetMaxAllowed(), pfrom->id);
        pfrom->fDisconnect = true;
        return;
    }

    CBlockLocator locator;
    uint256 hashStop;
    vRecv >> locator >> hashStop;

    // Get block data that must be obtained under lock
    const CBlockIndex* lastBlockIndex;
    std::vector<CBlockHeaderEnriched> vHeadersEnriched;
    int32_t chainActiveHeight;
    {
        if(IsInitialBlockDownload() && !pfrom->fWhitelisted) {
            LogPrint(BCLog::NETMSG, "Ignoring gethdrsen from peer=%d because "
                                    "node is in initial block download\n",
                     pfrom->id);
            return;
        }

        LOCK(cs_main);

        // Get index of the first requested block
        const CBlockIndex* pindex = nullptr;
        if(auto opt_block_index = GetFirstBlockIndexFromLocatorNL(locator, hashStop))
        {
            pindex = *opt_block_index;
        }
        else
        {
            LogPrint(BCLog::NET, "Ignoring gethdrsen from peer=%d because "
                                 "it requested unknown block (hashstop=%s) without locator\n",
                     pfrom->id, hashStop.ToString());
            return;
        }

        LogPrint(BCLog::NET, "gethdrsen %d to %s from peer=%d\n",
                 (pindex ? pindex->GetHeight() : -1),
                 hashStop.IsNull() ? "end" : hashStop.ToString(), pfrom->id);

        int nLimit = MAX_HEADERS_RESULTS;
        for (; pindex; pindex = chainActive.Next(pindex))
        {
            auto& hdr = vHeadersEnriched.emplace_back( pindex );

            if (chainActive.Tip() == pindex)
            {
                // This is the last header that we currently have.
                hdr.noMoreHeaders = true;
            }

            if (--nLimit <= 0 || pindex->GetBlockHash() == hashStop)
            {
                break;
            }
        }

        // Remember index of the last block that will be returned to node so that
        // we can later update BestHeaderSent for that node.
        // Null values are handled in the same way as in ProcessGetHeadersMessage.
        lastBlockIndex = pindex ? pindex : chainActive.Tip();

        chainActiveHeight = chainActive.Height();
    }

    // Get data that is slow to obtain but can be obtained without holding cs_main lock
    size_t combinedMsgSize = GetSizeOfCompactSize(vHeadersEnriched.size()); // number of bytes needed to store number of enriched headers
    for (auto it=vHeadersEnriched.begin(); it!=vHeadersEnriched.end(); ++it)
    {
        auto& enrichedHeader = *it;

        // Store coinbase info to enriched header object.
        // CB txn should be serialized using the same protocol version as in msgMaker.
        enrichedHeader.SetCoinBaseInfo(msgMaker.GetVersion(), config, chainActiveHeight);

        // Check if total message size is still within limits.
        // Note that we start checking only after we have already created one header,
        // so that a single header is always returned even if the message is too big.
        combinedMsgSize += enrichedHeader.GetSerializedSize();
        if (combinedMsgSize > static_cast<size_t>(pfrom->maxRecvPayloadLength))
        {
            if (it==vHeadersEnriched.begin())
            {
                // Do not return subsequent headers if message has gotten too big after the first header.
                vHeadersEnriched.erase(it+1, vHeadersEnriched.end());
            }
            else
            {
                // Do not return this and subsequent headers if message has gotten too big.
                vHeadersEnriched.erase(it, vHeadersEnriched.end());
            }
            lastBlockIndex = vHeadersEnriched.back().blockIndex;
            break;
        }
    }

    // Update node's BestHeaderSent like it is done in ProcessGetHeadersMessage so that
    // 'gethdrsen' is functionally equivalent to 'getheaders' except it provides more data.
    const CNodeStateRef stateRef { GetState(pfrom->GetId()) };
    const CNodeStatePtr& state { stateRef.get() };
    assert(state);
    state->pindexBestHeaderSent = lastBlockIndex;

    auto msg = msgMaker.Make(NetMsgType::HDRSEN, vHeadersEnriched);
    if(!pfrom->fWhitelisted)
    {
        msg.headerStreamCreator = CreateHeaderStreamWithPendingResponsesCounting<&CNode::MonitoredPendingResponses::gethdrsen>(pfrom);
    }
    connman.PushMessage(pfrom, std::move(msg));
}

/**
* Process tx message.
*/
static void ProcessTxMessage(const Config& config,
                             const CNodePtr& pfrom,
                             const CNetMsgMaker& msgMaker,
                             const std::string& strCommand,
                             msg_buffer& vRecv,
                             CConnman& connman)
{
    // Stop processing the transaction early if we are in blocks only mode and
    // peer is either not whitelisted or whitelistrelay is off
    if (!fRelayTxes &&
        (!pfrom->fWhitelisted || !config.GetWhitelistRelay())) {
        LogPrint(BCLog::NETMSGVERB,
                "transaction sent in violation of protocol peer=%d\n",
                 pfrom->id);
        return;
    }

    CTransactionRef ptx;
    vRecv >> ptx;
    const CTransaction &tx = *ptx;

    CInv inv(MSG_TX, tx.GetId());
    pfrom->AddInventoryKnown(inv);
    LogPrint(BCLog::TXNSRC | BCLog::NETMSGVERB, "got txn: %s txnsrc peer=%d\n", inv.hash.ToString(), pfrom->id);
    // Update 'ask for' inv set
    {
        LOCK(cs_invQueries);
        pfrom->indexAskFor.get<CNode::TagTxnID>().erase(inv.hash);
        mapAlreadyAskedFor->erase(inv.hash);
    }
    // Enqueue txn for validation if it is not known
    if (!IsTxnKnown(inv)) {
        // Forward transaction to the validator thread.
        // By default, treat a received txn as a 'high' priority txn.
        // If the validation timeout occurs the txn is moved to the 'low' priority queue.
        connman.EnqueueTxnForValidator(
            std::make_shared<CTxInputData>(
                connman.GetTxIdTracker(),
                std::move(ptx), // a pointer to the tx
                TxSource::p2p,  // tx source
                TxValidationPriority::high,  // tx validation priority
                TxStorage::memory, // tx storage
                GetTime(),      // nAcceptTime
                Amount(0),      // nAbsurdFee
                pfrom));        // pNode
    } else {
        // Always relay transactions received from whitelisted peers,
        // even if they were already in the mempool or rejected from it
        // due to policy, allowing the node to function as a gateway for
        // nodes hidden behind it.
        if (pfrom->fWhitelisted && config.GetWhitelistForceRelay()) {
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
static bool ProcessHeadersMessage(const Config& config,
                                  const CNodePtr& pfrom,
                                  const CNetMsgMaker& msgMaker,
                                  const CChainParams& chainparams,
                                  msg_buffer& vRecv,
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
        if(mapBlockIndex.Get(headers[0].hashPrevBlock) == nullptr && nCount < MAX_BLOCKS_TO_ANNOUNCE)
        {
            const auto& bestHeader = mapBlockIndex.GetBestHeader();
            // Try to obtain an access to the node's state data.
            const CNodeStateRef nodestateRef { GetState(pfrom->GetId()) };
            const CNodeStatePtr& nodestate { nodestateRef.get() };
            assert(nodestate);

            nodestate->nUnconnectingHeaders++;
            connman.PushMessage(
                pfrom,
                msgMaker.Make(NetMsgType::GETHEADERS,
                              chainActive.GetLocator(&bestHeader),
                              uint256()));
            LogPrint(BCLog::NETMSG, "received header %s: missing prev block "
                                 "%s, sending getheaders (%d) to end "
                                 "(peer=%d, nUnconnectingHeaders=%d)\n",
                     headers[0].GetHash().ToString(),
                     headers[0].hashPrevBlock.ToString(),
                     bestHeader.GetHeight(), pfrom->id,
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
            LogPrint(BCLog::NETMSG,
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
            // or mapBlockIndex.GetBestHeader(), continue from there instead.
            LogPrint(
                BCLog::NETMSG,
                "more getheaders (%d) to end to peer=%d (startheight:%d)\n",
                pindexLast->GetHeight(), pfrom->id, pfrom->nStartingHeight);
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
            chainActive.Tip()->GetChainWork() <= pindexLast->GetChainWork())
        {
            std::vector<const CBlockIndex*> vToFetch;
            const CBlockIndex *pindexWalk = pindexLast;
            // Calculate all the blocks we'd need to switch to pindexLast,
            // up to a limit.
            while(pindexWalk && !chainActive.Contains(pindexWalk) && vToFetch.size() <= MAX_BLOCKS_IN_TRANSIT_PER_PEER)
            {
                if(!pindexWalk->getStatus().hasData() && !blockDownloadTracker.IsInFlight(pindexWalk->GetBlockHash())) {
                    // We don't have this block, and it's not yet in flight.
                    vToFetch.push_back(pindexWalk);
                }
                pindexWalk = pindexWalk->GetPrev();
            }
            // If pindexWalk still isn't on our main chain, we're looking at
            // a very large reorg at a time we think we're close to caught
            // up to the main chain -- this shouldn't really happen. Bail
            // out on the direct fetch and rely on parallel download
            // instead.
            if(!chainActive.Contains(pindexWalk)) {
                LogPrint(BCLog::NETMSG,
                         "Large reorg, won't direct fetch to %s (%d)\n",
                         pindexLast->GetBlockHash().ToString(),
                         pindexLast->GetHeight());
            }
            else {
                std::vector<CInv> vGetData;
                // Download as much as possible, from earliest to latest.
                for(const CBlockIndex *pindex : boost::adaptors::reverse(vToFetch)) {
                    if(nodestate->nBlocksInFlight >= MAX_BLOCKS_IN_TRANSIT_PER_PEER) {
                        // Can't download any more from this peer
                        break;
                    }
                    vGetData.push_back(CInv(MSG_BLOCK, pindex->GetBlockHash()));
                    blockDownloadTracker.MarkBlockAsInFlight(config, { pindex->GetBlockHash(), pfrom->id }, nodestate, *pindex);
                    LogPrint(BCLog::NETMSG, "Requesting block %s from peer=%d\n",
                             pindex->GetBlockHash().ToString(), pfrom->id);
                }
                if(vGetData.size() > 1) {
                    LogPrint(BCLog::NETMSG, "Downloading blocks toward %s "
                                         "(%d) via headers direct fetch\n",
                             pindexLast->GetBlockHash().ToString(),
                             pindexLast->GetHeight());
                }
                if(vGetData.size() > 0) {
                    if(nodestate->fSupportsDesiredCmpctVersion &&
                        vGetData.size() == 1 &&
                        blockDownloadTracker.IsOnlyBlockInFlight(vGetData[0].hash) &&
                        pindexLast->GetPrev()->IsValid(BlockValidity::CHAIN)) {
                        // In any case, we want to download using a compact
                        // block, not a regular one.
                        vGetData[0] = CInv(MSG_CMPCT_BLOCK, vGetData[0].hash);
                    }
                    connman.PushMessage(
                        pfrom,
                        msgMaker.Make(CSerializedNetMsg::PayloadType::BLOCK, NetMsgType::GETDATA, vGetData));
                }
            }
        }
    }

    return true;
}

/**
* Process block txn message.
*/
static void ProcessBlockTxnMessage(const Config& config,
                                   const CNodePtr& pfrom,
                                   const CNetMsgMaker& msgMaker,
                                   msg_buffer& vRecv,
                                   CConnman& connman)
{
    BlockTransactions resp;
    vRecv >> resp;

    std::shared_ptr<CBlock> pblock = std::make_shared<CBlock>();
    BlockDownloadTracker::BlockSource blockSource { resp.blockhash, pfrom->id };
    bool fBlockRead = false;
    {
        // Get node state details. Holding the lock on the node state ensures this nodes
        // vBlocksInFlight can't change underneath us.
        const CNodeStateRef stateRef { GetState(pfrom->id) };
        const CNodeStatePtr& state { stateRef.get() };
        assert(state);

        BlockDownloadTracker::InFlightBlock inFlightBlock {};
        try {
            inFlightBlock = blockDownloadTracker.GetBlockDetails(blockSource);
            if(! inFlightBlock.queuedBlockIt->partialBlock) {
                throw std::runtime_error("Partial block not set");
            }
        }
        catch(std::exception& e) {
            LogPrint(BCLog::NETMSG, "Peer %d sent us block transactions for block we weren't expecting (%s)\n",
                pfrom->id, e.what());
            return;
        }

        PartiallyDownloadedBlock& partialBlock { *(inFlightBlock.queuedBlockIt->partialBlock) };
        ReadStatus status = partialBlock.FillBlock(*pblock, resp.txn, inFlightBlock.queuedBlockIt->blockIndex.GetHeight());
        if(status == READ_STATUS_INVALID) {
            // Reset in-flight state in case of whitelist.
            blockDownloadTracker.MarkBlockAsFailed(blockSource, state);
            Misbehaving(pfrom, 100, "invalid-cmpctblk-txns");
            LogPrint(BCLog::NETMSG, "Peer %d sent us invalid compact block/non-matching block transactions\n",
                     pfrom->id);
            return;
        }
        else if(status == READ_STATUS_FAILED) {
            // Might have collided, fall back to getdata now :(
            std::vector<CInv> invs;
            invs.push_back(CInv(MSG_BLOCK, resp.blockhash));
            connman.PushMessage(pfrom, msgMaker.Make(CSerializedNetMsg::PayloadType::BLOCK, NetMsgType::GETDATA, invs));
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

            // BIP 152 permits peers to relay compact blocks after validating
            // the header only; we should not punish peers if the block turns
            // out to be invalid.
            blockDownloadTracker.MarkBlockAsReceived(blockSource, false, state);
            fBlockRead = true;
        }
    }

    if(fBlockRead) {
        bool fNewBlock = false;
        auto source = task::CCancellationSource::Make();
        auto scopedBlockOriginReg = std::make_shared<CScopedBlockOriginRegistry>(
            pblock->GetHash(),
            "ProcessBlockTxnMessage",
            pfrom->GetAddrName(),
            pfrom->GetId());
        // Since we requested this block (it was in mapBlocksInFlight),
        // force it to be processed, even if it would not be a candidate for
        // new tip (missing previous block, chain not long enough, etc)
        auto bestChainActivation =
            ProcessNewBlockWithAsyncBestChainActivation(
                task::CCancellationToken::JoinToken(source->GetToken(), GetShutdownToken()), config, pblock, true, &fNewBlock, CBlockSource::MakeP2P(pfrom->GetAssociation().GetPeerAddr().ToString()));
        if(!bestChainActivation)
        {
            // something went wrong before we need to activate best chain
            return;
        }

        pfrom->RunAsyncProcessing(
            [fNewBlock, bestChainActivation, pblock, scopedBlockOriginReg]
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
static bool ProcessCompactBlockMessage(
    const Config& config,
    const CNodePtr& pfrom,
    const CNetMsgMaker& msgMaker,
    const std::string& strCommand,
    const CChainParams& chainparams,
    const std::atomic<bool>& interruptMsgProc,
    int64_t nTimeReceived,
    msg_buffer& vRecv,
    CConnman& connman)
{
    CBlockHeaderAndShortTxIDs cmpctblock;
    vRecv >> cmpctblock;

    LogPrint(BCLog::NETMSG, "Got compact block for %s from peer=%d\n", cmpctblock.header.GetHash().ToString(), pfrom->id);

    {
        LOCK(cs_main);

        if(mapBlockIndex.Get(cmpctblock.header.hashPrevBlock) == nullptr) {
            const auto& bestHeader = mapBlockIndex.GetBestHeader();
            // Doesn't connect (or is genesis), instead of DoSing in
            // AcceptBlockHeader, request deeper headers
            if(!IsInitialBlockDownload()) {
                connman.PushMessage(
                    pfrom,
                    msgMaker.Make(NetMsgType::GETHEADERS,
                                  chainActive.GetLocator(&bestHeader),
                                  uint256()));
            }
            return true;
        }
    }

    BlockDownloadTracker::BlockSource blockSource { cmpctblock.header.GetHash(), pfrom->id };

    const CBlockIndex *pindex = nullptr;
    CValidationState state;
    if(!ProcessNewBlockHeaders(config, {cmpctblock.header}, state, &pindex))
    {
        int nDoS;
        if(state.IsInvalid(nDoS))
        {
            LogPrint(BCLog::NETMSG, "Peer %d sent us invalid header via cmpctblock\n", pfrom->id);
            blockDownloadTracker.MarkBlockAsFailed(blockSource, GetState(pfrom->id).get());
            if(nDoS > 0)
            {
                Misbehaving(pfrom, nDoS, state.GetRejectReason());
            }
            return true;
        }
        
        // safety net: if the first block header is not accepted but the state is not marked
        // as invalid pindexLast will stay null
        // in that case we have nothing to do...
        if(pindex == nullptr)
        {
            blockDownloadTracker.MarkBlockAsFailed(blockSource, GetState(pfrom->id).get());
            return error("header is not accepted");
        }
    }

    // When we succeed in decoding a block's txids from a cmpctblock
    // message we typically jump to the BLOCKTXN handling code, with a
    // dummy (empty) BLOCKTXN message, to re-use the logic there in
    // completing processing of the putative block (without cs_main).
    bool fProcessBLOCKTXN = false;
    msg_buffer blockTxnMsg(SER_NETWORK, PROTOCOL_VERSION);

    // If we end up treating this as a plain headers message, call that as
    // well without cs_main.
    bool fRevertToHeaderProcessing = false;
    msg_buffer vHeadersMsg(SER_NETWORK, PROTOCOL_VERSION);

    // Keep a CBlock for "optimistic" compactblock reconstructions (see below)
    std::shared_ptr<CBlock> pblock = std::make_shared<CBlock>();
    bool fBlockReconstructed = false;

    // If AcceptBlockHeader returned true, it set pindex
    assert(pindex);

    {
        LOCK(cs_main);
        // Try to obtain an access to the node's state data.
        const CNodeStateRef nodestateRef { GetState(pfrom->GetId()) };
        const CNodeStatePtr& nodestate { nodestateRef.get() };
        assert(nodestate);
        // Update block's availability.
        UpdateBlockAvailability(pindex->GetBlockHash(), nodestate);

        bool fAlreadyInFlight { blockDownloadTracker.IsInFlight(pindex->GetBlockHash()) };
        bool fAlreadyInFlightFromThisPeer { blockDownloadTracker.IsInFlight(blockSource) };

        if(pindex->getStatus().hasData()) {
            // Nothing to do here
            blockDownloadTracker.MarkBlockAsFailed(blockSource, nodestate);
            return true;
        }

        if(pindex->GetChainWork() <=
                chainActive.Tip()->GetChainWork() || // We know something better
            pindex->GetBlockTxCount() != 0) {
            // We had this block at some point, but pruned it
            if(fAlreadyInFlight) {
                // We requested this block for some reason, but our mempool
                // will probably be useless so we just grab the block via
                // normal getdata.
                std::vector<CInv> vInv(1);
                vInv[0] = CInv(MSG_BLOCK, cmpctblock.header.GetHash());
                connman.PushMessage(
                    pfrom, msgMaker.Make(CSerializedNetMsg::PayloadType::BLOCK, NetMsgType::GETDATA, vInv));
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
        if(pindex->GetHeight() <= chainActive.Height() + 2)
        {
            if((!fAlreadyInFlight &&
                 nodestate->nBlocksInFlight < MAX_BLOCKS_IN_TRANSIT_PER_PEER) ||
                fAlreadyInFlightFromThisPeer)
            {
                std::list<QueuedBlock>::iterator *queuedBlockIt = nullptr;
                if(!blockDownloadTracker.MarkBlockAsInFlight(config, blockSource, nodestate, *pindex, &queuedBlockIt))
                {
                    if(!(*queuedBlockIt)->partialBlock)
                    {
                        (*queuedBlockIt)->partialBlock.reset(
                                new PartiallyDownloadedBlock(config, &mempool));
                    }
                    else
                    {
                        // The block was already in flight using compact blocks from the same peer.
                        LogPrint(BCLog::NETMSG, "Peer sent us compact block we were already syncing!\n");
                        return true;
                    }
                }

                PartiallyDownloadedBlock& partialBlock = *(*queuedBlockIt)->partialBlock;
                ReadStatus status = partialBlock.InitData(cmpctblock, g_connman->GetCompactExtraTxns());
                if(status == READ_STATUS_INVALID) {
                    // Reset in-flight state in case of whitelist
                    blockDownloadTracker.MarkBlockAsFailed(blockSource, nodestate);
                    Misbehaving(pfrom, 100, "invalid-cmpctblk");
                    LogPrint(BCLog::NETMSG, "Peer %d sent us invalid compact block\n", pfrom->id);
                    return true;
                }
                else if(status == READ_STATUS_FAILED) {
                    // Duplicate txindices, the block is now in-flight, so just request it.
                    std::vector<CInv> vInv(1);
                    vInv[0] = CInv(MSG_BLOCK, cmpctblock.header.GetHash());
                    connman.PushMessage(pfrom, msgMaker.Make(CSerializedNetMsg::PayloadType::BLOCK, NetMsgType::GETDATA, vInv));
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
                status = tempBlock.FillBlock(*pblock, dummy, pindex->GetHeight());
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
                connman.PushMessage(pfrom, msgMaker.Make(CSerializedNetMsg::PayloadType::BLOCK, NetMsgType::GETDATA, vInv));
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
        {
            // If we got here, we were able to optimistically reconstruct a
            // block that is in flight from some other peer.
            const CNodeStateRef nodestateRef { GetState(pfrom->GetId()) };
            const CNodeStatePtr& nodestate { nodestateRef.get() };
            assert(nodestate);
            blockDownloadTracker.MarkBlockAsReceived(blockSource, false, nodestate);
        }

        bool fNewBlock = false;
        auto source = task::CCancellationSource::Make();
        auto scopedBlockOriginReg = std::make_shared<CScopedBlockOriginRegistry>(
            pblock->GetHash(),
            "ProcessCompactBlock",
            pfrom->GetAddrName(),
            pfrom->GetId());
        auto bestChainActivation =
            ProcessNewBlockWithAsyncBestChainActivation(
                task::CCancellationToken::JoinToken(source->GetToken(), GetShutdownToken()), config, pblock, true, &fNewBlock, CBlockSource::MakeP2P(pfrom->GetAssociation().GetPeerAddr().ToString()));
        if(bestChainActivation)
        {
            pfrom->RunAsyncProcessing(
                [pblock, fNewBlock, bestChainActivation, scopedBlockOriginReg]
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
        // else something went wrong before we need to activate best chain
        // so we just skip it
    }

    return true;
}
 
/**
* Process block message.
*/
static void ProcessBlockMessage(const Config& config,
                                const CNodePtr& pfrom,
                                msg_buffer& vRecv,
                                CConnman& connman)
{
    std::shared_ptr<CBlock> pblock = std::make_shared<CBlock>();
    vRecv >> *pblock;
    
    LogPrint(BCLog::NETMSG, "received block %s peer=%d\n", pblock->GetHash().ToString(), pfrom->id);

    // Process all blocks from whitelisted peers, even if not requested,
    // unless we're still syncing with the network. Such an unrequested
    // block may still be processed, subject to the conditions in
    // AcceptBlock().
    bool forceProcessing = pfrom->fWhitelisted && !IsInitialBlockDownload();
    const uint256 hash(pblock->GetHash());
    {
        const CNodeStateRef nodestateRef { GetState(pfrom->GetId()) };
        const CNodeStatePtr& nodestate { nodestateRef.get() };
        assert(nodestate);

        // Also always process if we requested the block explicitly, as we
        // may need it even though it is not a candidate for a new best tip.
        forceProcessing |= blockDownloadTracker.MarkBlockAsReceived({ hash, pfrom->id }, true, nodestate);
    }

    bool fNewBlock = false;
    auto source = task::CCancellationSource::Make();
    auto scopedBlockOriginReg = std::make_shared<CScopedBlockOriginRegistry>(
            pblock->GetHash(),
            "ProcessBlockMessage",
            pfrom->GetAddrName(),
            pfrom->GetId());
    auto bestChainActivation =
        ProcessNewBlockWithAsyncBestChainActivation(
            task::CCancellationToken::JoinToken(source->GetToken(), GetShutdownToken()), config, pblock, forceProcessing, &fNewBlock, CBlockSource::MakeP2P(pfrom->GetAssociation().GetPeerAddr().ToString()));
    if(!bestChainActivation)
    {
        // something went wrong before we need to activate best chain
        return;
    }

    pfrom->RunAsyncProcessing(
        [pblock, fNewBlock, bestChainActivation, scopedBlockOriginReg]
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
                                  msg_buffer& vRecv,
                                  CConnman& connman)
{
    // This asymmetric behavior for inbound and outbound connections was
    // introduced to prevent a fingerprinting attack: an attacker can send
    // specific fake addresses to users' AddrMan and later request them by
    // sending getaddr messages. Making nodes which are behind NAT and can
    // only make outgoing connections ignore the getaddr message mitigates
    // the attack.
    if(!pfrom->fInbound) {
        LogPrint(BCLog::NETMSG,
                 "Ignoring \"getaddr\" from outbound connection. peer=%d\n",
                 pfrom->id);
        return;
    }

    // Only send one GetAddr response per connection to reduce resource
    // waste and discourage addr stamping of INV announcements.
    if(pfrom->fSentAddr) {
        LogPrint(BCLog::NETMSG, "Ignoring repeated \"getaddr\". peer=%d\n",
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
static void ProcessMempoolMessage(const Config& config,
                                  const CNodePtr& pfrom,
                                  msg_buffer& vRecv,
                                  CConnman& connman)
{
    if (config.GetRejectMempoolRequest() && !pfrom->fWhitelisted) {
        LogPrint(BCLog::NETMSG, "mempool request from nonwhitelisted peer disabled, disconnect peer=%d\n",
                 pfrom->GetId());
        pfrom->fDisconnect = true;
        return;
    }

    if(!(pfrom->GetLocalServices() & NODE_BLOOM) && !pfrom->fWhitelisted) {
        LogPrint(BCLog::NETMSG, "mempool request with bloom filters disabled, disconnect peer=%d\n",
                 pfrom->GetId());
        pfrom->fDisconnect = true;
        return;
    }

    if(connman.OutboundTargetReached(false) && !pfrom->fWhitelisted) {
        LogPrint(BCLog::NETMSG, "mempool request with bandwidth limit reached, disconnect peer=%d\n",
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
static void ProcessPingMessage(const CNodePtr& pfrom,
                               const CNetMsgMaker& msgMaker,
                               msg_buffer& vRecv,
                               CConnman& connman)
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
static void ProcessPongMessage(const CNodePtr& pfrom,
                               int64_t nTimeReceived,
                               msg_buffer& vRecv)
{
    int64_t pingUsecEnd = nTimeReceived;
    uint64_t nonce = 0;
    size_t nAvail = vRecv.size();
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
        LogPrint(BCLog::NETMSG,
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
static void ProcessFilterLoadMessage(const CNodePtr& pfrom, msg_buffer& vRecv)
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
        pfrom->fRelayTxes = true;
    }
}
 
/**
* Process filter add message.
*/
static void ProcessFilterAddMessage(const CNodePtr& pfrom, msg_buffer& vRecv)
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
static void ProcessFilterClearMessage(const CNodePtr& pfrom, msg_buffer& vRecv)
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
static void ProcessFeeFilterMessage(const CNodePtr& pfrom, msg_buffer& vRecv)
{
    Amount newFeeFilter(0);
    vRecv >> newFeeFilter;
    if(MoneyRange(newFeeFilter))
    {
        {
            LOCK(pfrom->cs_feeFilter);
            pfrom->minFeeFilter = newFeeFilter;
        }
        LogPrint(BCLog::NETMSG, "received: feefilter of %s from peer=%d\n",
                 CFeeRate(newFeeFilter).ToString(), pfrom->id);
    }
}

/**
* Process protoconf message.
*/
static bool ProcessProtoconfMessage(const CNodePtr& pfrom,
                                    msg_buffer& vRecv,
                                    CConnman& connman,
                                    const std::string& strCommand,
                                    const Config& config)
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
        LogPrint(BCLog::NETMSG, "Invalid protoconf received \"%s\" from peer=%d, exception = %s\n",
            SanitizeString(strCommand), pfrom->id, e.what());
        pfrom->fDisconnect = true;
        return false;
    }

    // Parse known fields:
    if (protoconf.numberOfFields >= 1) {
        // if peer sends maxRecvPayloadLength less than LEGACY_MAX_PROTOCOL_PAYLOAD_LENGTH, it is considered protocol violation
        if (protoconf.maxRecvPayloadLength < LEGACY_MAX_PROTOCOL_PAYLOAD_LENGTH) {
            LogPrint(BCLog::NETMSG, "Invalid protoconf received \"%s\" from peer=%d, peer's proposed maximal message size is too low (%d).\n",
            SanitizeString(strCommand), pfrom->id, protoconf.maxRecvPayloadLength);
            pfrom->fDisconnect = true;
            return false;
        }

        // Limit the amount of data we are willing to send if a peer (or an attacker)
        // that is running a newer version sends us large size, that we are not prepared to handle. 
        pfrom->maxInvElements = CInv::estimateMaxInvElements(std::min(config.GetMaxProtocolSendPayloadLength(), protoconf.maxRecvPayloadLength));
        pfrom->maxRecvPayloadLength = protoconf.maxRecvPayloadLength;

        // Parse supported stream policies if we have them
        if(protoconf.numberOfFields >= 2) {
            pfrom->SetSupportedStreamPolicies(protoconf.streamPolicies);
        }

        LogPrint(BCLog::NETMSG, "Protoconf received \"%s\" from peer=%d; peer's proposed max message size: %d," 
            "absolute maximal allowed message size: %d, calculated maximal number of Inv elements in a message = %d, "
            "their stream policies: %s, common stream policies: %s\n",
            SanitizeString(strCommand), pfrom->id, protoconf.maxRecvPayloadLength, config.GetMaxProtocolSendPayloadLength(), pfrom->maxInvElements,
            protoconf.streamPolicies, pfrom->GetCommonStreamPoliciesStr());
    }

    if(!pfrom->fInbound) {
        try {
            // For outbound connections, now we can create any required further streams to this peer
            pfrom->GetAssociation().OpenRequiredStreams(connman);
        }
        catch(std::exception& e) {
            LogPrint(BCLog::NETCONN, "Error opening required streams (%s) to peer=%d\n", e.what(), pfrom->id);
            pfrom->fDisconnect = true;
            return false;
        }
    }

    return true;
}
    
static bool AcceptBlockHeaders(const DSDetected& msg, const Config& config)
{
    LOCK(cs_main);
    return all_of(msg.begin(), msg.end(), [&config](const auto& fork) {
        return all_of(fork.mBlockHeaders.crbegin(),
                      fork.mBlockHeaders.crend(),
                      [&config](const CBlockHeader& bh) {
                          CValidationState state;
                          CBlockIndex* pbIndex{};
                          bool accepted = AcceptBlockHeader(config, bh, state, &pbIndex);
                          return accepted && state.IsValid();
                      });
    });
}

static bool UpdateBlockStatus(const DSDetected& msg)
{
    // Update CBlockIndex::BlockStatus to show the double-spend
    LOCK(cs_main);
    for(const auto& fork : msg)
    {
        assert(!fork.mBlockHeaders.empty());
        const auto& header{fork.mBlockHeaders.back()};
        const auto hash{header.GetHash()};

        CBlockIndex* pIndex{mapBlockIndex.Get(hash)};
        if(pIndex)
            pIndex->ModifyStatusWithDoubleSpend(mapBlockIndex); 
        else    
            return false;
    }
    return true; 
}

static bool IsSamePeer(const CNode& peer1, const CNode& peer2)
{
    const auto& assocID1{peer1.GetAssociation().GetAssociationID()};
    const auto& assocID2{peer2.GetAssociation().GetAssociationID()};
    // If either peer doesn't have an association ID, best we can do is compare
    // node ptrs
    if(!assocID1 || !assocID2)
    {
        return &peer1 == &peer2;
    }
    return *assocID1 == *assocID2;
}

static bool ValidateForkHeight(const DSDetected& msg, const int64_t max_fork_distance)
{
    const auto& fork{MaxForkLength(msg)};
    if(fork.mBlockHeaders.empty())
        return false;

    const auto fork_len{fork.mBlockHeaders.size()};
    const auto& common_ancestor_hash{fork.mBlockHeaders[fork_len-1].hashPrevBlock};
    
    LOCK(cs_main);
    const auto* pIndex{mapBlockIndex.Get(common_ancestor_hash)};
    if(!pIndex)
        return false;

    const auto ca_height{pIndex->GetHeight()};

    const CBlockIndex& bestIndex{mapBlockIndex.GetBestHeader()};
    const size_t best_height = bestIndex.GetHeight();
    return (ca_height + fork_len + max_fork_distance) > best_height;
}

/**
* Proces revokemid message.
*/
static void ProcessRevokeMidMessage(const CNodePtr& pfrom,
                                    msg_buffer& vRecv,
                                    CConnman& connman,
                                    const CNetMsgMaker& msgMaker)
{
    if(g_minerIDs)
    {
        try
        {
            // Deserialise and check signatures
            RevokeMid msg {};
            vRecv >> msg;

            // Check for duplicate message
            constexpr size_t cacheSize { 1000 };
            static limited_cache msgCache { cacheSize }; 
            const std::hash<RevokeMid> hasher {};
            const auto hash { hasher(msg) };
            if(msgCache.contains(hash))
            {
                LogPrint(BCLog::NETMSG, "Ignoring duplicate revokemid message from peer=%d\n", pfrom->id);
                return;
            }
            msgCache.insert(hash);

            // Pass to miner ID database for processing
            g_minerIDs->ProcessRevokemidMessage(msg);

            // Relay to our peers
            connman.ForEachNode([&pfrom, &msg, &connman, &msgMaker](const CNodePtr& to)
            {
                // No point echoing back to the sender
                if(!IsSamePeer(*pfrom, *to))
                {
                    connman.PushMessage(to, msgMaker.Make(NetMsgType::REVOKEMID, msg));
                }
            });
        }
        catch(const std::exception& e)
        {
            LogPrint(BCLog::NETMSG | BCLog::MINERID,
                "Error processing revokemid message from peer=%d: %s\n", pfrom->id, e.what());
            Misbehaving(pfrom, 10, "Invalid revokemid message");
        }
    }
}

/**
* Process double-spend detected message.
*/
static void ProcessDoubleSpendMessage(const Config& config,
                                      const std::shared_ptr<CNode>& pfrom,
                                      msg_buffer& vRecv,
                                      CConnman& connman,
                                      const CNetMsgMaker& msgMaker)
{
    // Deserialise message
    constexpr int misbehaviour_penalty{10};
    DSDetected msg{};
    try
    {
        vRecv >> msg;
    }
    catch(const exception& e)
    {
        LogPrint(BCLog::NETMSG,
                 "Error processing double-spend detected message from "
                 "peer=%d: %s\n",
                 pfrom->id,
                 e.what());
        Misbehaving(pfrom,
                    misbehaviour_penalty,
                    "Invalid double-spend Detected message received");
        return;
    }

    try
    {
        // Check if we've already handled this message
        constexpr size_t cache_size{1000};
        static limited_cache msg_cache{cache_size}; 
       
        const auto hash = sort_hasher(msg);

        if(msg_cache.contains(hash))
        {
            LogPrint(BCLog::NETMSG,
                     "Ignoring duplicate double-spend detected message from "
                     "peer=%d\n",
                     pfrom->id);
            return;     // ignore messages we've already seen
        }

        msg_cache.insert(hash); 
        
        if(!IsValid(msg))
        {
            Misbehaving(pfrom, misbehaviour_penalty,
                        "Invalid double-spend detected message received");
            return;
        }
        LogPrint(BCLog::NETMSG,
                 "Valid double-spend detected message from peer=%d\n",
                 pfrom->id);

        if(!ValidateForkHeight(msg, config.GetSafeModeMaxForkDistance()))
        {
            Misbehaving(pfrom, misbehaviour_penalty,
                "Block height too low in double-spend detected message");
            LogPrint(BCLog::NETMSG,
                     "Block height too low in double-spend "
                     "detected message from peer=%d\n",
                     pfrom->id);
            return;
        }

        if(!AcceptBlockHeaders(msg, config))
        {
            LogPrint(BCLog::NETMSG,
                     "Failed to accept block headers from double-spend detected "
                     "message from peer=%d\n",
                     pfrom->id);
            return;
        }

        if(!UpdateBlockStatus(msg))
        {
            LogPrint(BCLog::NETMSG,
                     "Failed to update block statuses from double-spend detected "
                     "message from peer=%d\n",
                     pfrom->id);
            return;
        }

        // Relay to our peers
        connman.ForEachNode([&pfrom, &msg, &connman, &msgMaker](const CNodePtr& to)
            {
                // No point echoing back to the sender
                if(!IsSamePeer(*pfrom, *to))
                {
                    connman.PushMessage(to, msgMaker.Make(NetMsgType::DSDETECTED, msg));
                }
            }
        );

        // Send webhook notification if configured to do so
        using namespace rpc::client;
        if(!config.GetDoubleSpendDetectedWebhookAddress().empty() && g_pWebhookClient)
        {
            RPCClientConfig rpcConfig { RPCClientConfig::CreateForDoubleSpendDetectedWebhook(config) };
            using HTTPRequest = rpc::client::HTTPRequest;
            auto request { std::make_shared<HTTPRequest>(HTTPRequest::CreateJSONPostRequest(rpcConfig, msg.ToJSON(config))) };
            auto response { std::make_shared<StringHTTPResponse>() };
            g_pWebhookClient->SubmitRequest(rpcConfig, std::move(request), std::move(response));
        }
    }
    catch(const std::exception& e)
    {
        LogPrint(BCLog::NETMSG, "Error processing double-spend detected message from peer=%d: %s\n",
            pfrom->id, e.what());
    }
} 

/**
* Process next message.
*/
static bool ProcessMessage(const Config& config,
                           const CNodePtr& pfrom,
                           const std::string& strCommand,
                           msg_buffer& vRecv,
                           int64_t nTimeReceived,
                           const CChainParams& chainparams,
                           CConnman& connman,
                           const std::atomic<bool>& interruptMsgProc)
{
    LogPrint(BCLog::NETMSGVERB, "received: %s (%u bytes) peer=%d\n",
             SanitizeString(strCommand), vRecv.size(), pfrom->id);
    if (config.DoDropMessageTest() && GetRand(config.GetDropMessageTest()) == 0) {
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
    else if(strCommand == NetMsgType::CREATESTREAM) {
        return ProcessCreateStreamMessage(pfrom, strCommand, vRecv, connman);
    }
    else if(strCommand == NetMsgType::STREAMACK) {
        return ProcessStreamAckMessage(pfrom, strCommand, vRecv, connman);
    }

    else if (pfrom->nVersion == 0) {
        // Must have a version or createstream message before anything else
        Misbehaving(pfrom, 1, "missing-version");
        return false;
    }

    // At this point, the outgoing message serialization version can't change.
    const CNetMsgMaker msgMaker(pfrom->GetSendVersion());

    if (strCommand == NetMsgType::VERACK) {
        ProcessVerAckMessage(pfrom, msgMaker, connman);
    }

    else if (strCommand == NetMsgType::AUTHCH) {
        return ProcessAuthChMessage(config, pfrom, msgMaker, strCommand, vRecv, connman);
    }

    else if (strCommand == NetMsgType::AUTHRESP) {
        return ProcessAuthRespMessage(pfrom, strCommand, vRecv, connman);
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

    else if (strCommand == NetMsgType::SENDHDRSEN) {
        ProcessSendHdrsEnMessage(pfrom);
    }

    else if (strCommand == NetMsgType::SENDCMPCT) {
        ProcessSendCompactMessage(pfrom, vRecv);
    }

    else if (strCommand == NetMsgType::INV) {
        ProcessInvMessage(pfrom, msgMaker, interruptMsgProc, vRecv, connman, config);
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

    else if (strCommand == NetMsgType::GETHDRSEN) {
        ProcessGetHeadersEnrichedMessage(pfrom, msgMaker, vRecv, connman, config);
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
        ProcessBlockMessage(config, pfrom, vRecv, connman);
    }

    // Ignore double-spend detected notifications while importing
    else if (strCommand == NetMsgType::DSDETECTED && !fImporting && !fReindex) {
        ProcessDoubleSpendMessage(config, pfrom, vRecv, connman, msgMaker);
    }

    else if (strCommand == NetMsgType::GETADDR) {
        ProcessGetAddrMessage(pfrom, vRecv, connman);
    }

    else if (strCommand == NetMsgType::MEMPOOL) {
        ProcessMempoolMessage(config, pfrom, vRecv, connman);
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
        return ProcessProtoconfMessage(pfrom, vRecv, connman, strCommand, config);
    }

    else if (strCommand == NetMsgType::REVOKEMID) {
        ProcessRevokeMidMessage(pfrom, vRecv, connman, msgMaker);
    }

    else if (strCommand == NetMsgType::NOTFOUND) {
        // We do not care about the NOTFOUND message, but logging an Unknown
        // Command message would be undesirable as we transmit it ourselves.
    }

    else {
        // Ignore unknown commands for extensibility
        LogPrint(BCLog::NETMSG, "Unknown command \"%s\" from peer=%d\n",
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
        const CAddress& peerAddr { pnode->GetAssociation().GetPeerAddr() };
        if (pnode->fWhitelisted) {
            LogPrintf("Warning: not punishing whitelisted peer %s!\n",
                      peerAddr.ToString());
        } else if (pnode->fAddnode) {
            LogPrintf("Warning: not punishing addnoded peer %s!\n",
                      peerAddr.ToString());
        } else {
            pnode->fDisconnect = true;
            if (peerAddr.IsLocal()) {
                LogPrintf("Warning: not banning local peer %s!\n",
                          peerAddr.ToString());
            } else {
                connman.Ban(peerAddr, BanReasonNodeMisbehaving);
            }
        }
        return true;
    }
    return false;
}

bool ProcessMessages(const Config &config, const CNodePtr& pfrom, CConnman &connman,
                     const std::atomic<bool> &interruptMsgProc,
                     std::chrono::milliseconds debugP2PTheadStallsThreshold)
{
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
    if (pfrom->GetPausedForSending(true)) {
        return false;
    }

    // Get next message for processing
    auto [ nextMsg, moreMsgs ] { pfrom->GetAssociation().GetNextMessage() };
    if(!nextMsg) {
        return false;
    }
    fMoreWork = moreMsgs;
    CNetMessage& msg { *nextMsg };
    msg.SetVersion(pfrom->GetRecvVersion());

    const CMessageHeader& hdr = msg.GetHeader();
    std::optional<CLogP2PStallDuration> durationLog;
    using namespace std::literals::chrono_literals;
    if(debugP2PTheadStallsThreshold > 0ms)
    {
        durationLog = { hdr.GetCommand(), debugP2PTheadStallsThreshold };
    }

    // Scan for message start
    if (memcmp(hdr.GetMsgStart().data(),
               chainparams.NetMagic().data(),
               CMessageFields::MESSAGE_START_SIZE) != 0) {
        LogPrint(BCLog::NETMSG, "PROCESSMESSAGE: INVALID MESSAGESTART %s peer=%d\n",
                  SanitizeString(hdr.GetCommand()), pfrom->id);

        // Make sure we ban where that come from for some time.
        connman.Ban(pfrom->GetAssociation().GetPeerAddr(), BanReasonNodeMisbehaving);

        pfrom->fDisconnect = true;
        return false;
    }

    // Read header
    if (!hdr.IsValid(config)) {
        LogPrint(BCLog::NETMSG, "PROCESSMESSAGE: ERRORS IN HEADER %s peer=%d\n",
                  SanitizeString(hdr.GetCommand()), pfrom->id);
        return fMoreWork;
    }
    std::string strCommand = hdr.GetCommand();

    // Message size
    uint64_t nPayloadLength = hdr.GetPayloadLength();

    // Checksum (skipped for extended messages)
    if(! hdr.IsExtended()) {
        const uint256 &hash = msg.GetMessageHash();
        if (memcmp(hash.begin(), hdr.GetChecksum().data(), CMessageFields::CHECKSUM_SIZE) !=0) {
            LogPrint(BCLog::NETMSG,
                "%s(%s, %lu bytes): CHECKSUM ERROR expected %s was %s\n", __func__,
                SanitizeString(strCommand), nPayloadLength,
                HexStr(hash.begin(), hash.begin() + CMessageFields::CHECKSUM_SIZE),
                HexStr(hdr.GetChecksum()));
            {
                // Try to obtain an access to the node's state data.
                const CNodeStateRef stateRef { GetState(pfrom->GetId()) };
                const CNodeStatePtr& state { stateRef.get() };
                if (state){
                    auto curTime = std::chrono::system_clock::now();
                    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(curTime - state->nTimeOfLastInvalidChecksumHeader).count();
                    std::chrono::milliseconds checksumInterval(config.GetInvalidChecksumInterval()); 
                    if (duration < std::chrono::milliseconds(checksumInterval).count()){
                        ++ state->dInvalidChecksumFrequency;
                    }
                    else { 
                        // reset the frequency as this invalid checksum is outside the MIN_INTERVAL
                        state->dInvalidChecksumFrequency = 0;
                    }
                    if (state->dInvalidChecksumFrequency > config.GetInvalidChecksumFreq()) {
                        // MisbehavingNode if the count goes above some chosen value 
                        // 100 consecutive invalid checksums received with less than 500ms between them
                        Misbehaving(pfrom, 1, "Invalid Checksum activity");
                        LogPrint(BCLog::NETMSG, "Peer %d showing increased invalid checksum activity\n",pfrom->id);
                    }
                    state->nTimeOfLastInvalidChecksumHeader = curTime;
                }
            }
            return fMoreWork;
        }
    }

    // Process message
    bool fRet = false;
    try {
        fRet = ProcessMessage(config, pfrom, strCommand, msg.GetData(), msg.GetTime(),
                              chainparams, connman, interruptMsgProc);
        if (interruptMsgProc) {
            return false;
        }
        if(!pfrom->vRecvGetData.empty()) {
            fMoreWork = true;
        }
    }
    catch (const std::ios_base::failure &e) {
        connman.PushMessage(pfrom,
                            CNetMsgMaker(INIT_PROTO_VERSION)
                            .Make(NetMsgType::REJECT, strCommand,
                                  REJECT_MALFORMED,
                                  std::string("error parsing message")));
        if (strstr(e.what(), "end of data")) {
            // Allow exceptions from under-length message on vRecv
            LogPrint(BCLog::NETMSG,
                     "%s(%s, %lu bytes): Exception '%s' caught, normally caused by a "
                     "message being shorter than its stated length\n",
                     __func__, SanitizeString(strCommand), nPayloadLength, e.what());
        } else if (strstr(e.what(), "size too large")) {
            // Allow exceptions from over-long size
            LogPrint(BCLog::NETMSG, "%s(%s, %lu bytes): Exception '%s' caught\n", __func__,
                     SanitizeString(strCommand), nPayloadLength, e.what());
            Misbehaving(pfrom, 1, "Over-long size message protection");
        } else if (strstr(e.what(), "non-canonical ReadCompactSize()")) {
            // Allow exceptions from non-canonical encoding
            LogPrint(BCLog::NETMSG, "%s(%s, %lu bytes): Exception '%s' caught\n", __func__,
                     SanitizeString(strCommand), nPayloadLength, e.what());
        } else if (strstr(e.what(), "parsing error")) {
            // Allow generic parsing errors
            LogPrint(BCLog::NETMSG, "%s(%s, %lu bytes): Exception '%s' caught\n", __func__,
                     SanitizeString(strCommand), nPayloadLength, e.what());
        } else {
            PrintExceptionContinue(&e, "ProcessMessages()");
        }
    }
    catch (const std::exception &e) {
        PrintExceptionContinue(&e, "ProcessMessages()");
    }
    catch (...) {
        PrintExceptionContinue(nullptr, "ProcessMessages()");
    }

    if (!fRet) {
        LogPrint(BCLog::NETMSG, "%s(%s, %lu bytes) FAILED peer=%d\n", __func__,
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
    const auto& bestHeader = mapBlockIndex.GetBestHeader();
    assert(state);
    // Download if this is a nice peer, or we have no nice peers and this one
    // might do.
    bool fFetch = state->fPreferredDownload ||
                  (nPreferredDownload == 0 && !pto->fClient && !pto->fOneShot);

    if (!state->fSyncStarted && !pto->fClient && !fImporting && !fReindex) {
        // Only actively request headers from a single peer, unless we're close
        // to today.
        if ((nSyncStarted == 0 && fFetch) ||
            bestHeader.GetBlockTime() >
                GetAdjustedTime() - 24 * 60 * 60) {
            state->fSyncStarted = true;
            nSyncStarted++;
            const CBlockIndex *pindexStart = &bestHeader;
            /**
             * If possible, start at the block preceding the currently best
             * known header. This ensures that we always get a non-empty list of
             * headers back as long as the peer is up-to-date. With a non-empty
             * response, we can initialise the peer's known best block. This
             * wouldn't be possible if we requested starting at bestHeader
             * and got back an empty response.
             */
            if (!pindexStart->IsGenesis())
            {
                pindexStart = pindexStart->GetPrev();
            }

            LogPrint(BCLog::NETMSG,
                     "initial getheaders (%d) to peer=%d (startheight:%d)\n",
                     pindexStart->GetHeight(), pto->id, pto->nStartingHeight);
            connman.PushMessage(
                pto,
                msgMaker.Make(NetMsgType::GETHEADERS,
                              chainActive.GetLocator(pindexStart), uint256()));
        }
    }
}

void SendBlockHeaders(const Config& config, const CNodePtr& pto, CConnman &connman,
    const CNetMsgMaker& msgMaker, const CNodeStatePtr& state)
{
    //
    // Try sending block announcements via headers
    //
    assert(state);
    std::vector<CBlock> vHeaders {};

    LOCK(pto->cs_inventory);

    // Array vBlockHashesToAnnounce must be sorted in ascending order according to block height since this
    // is assumed by algorithms below. E.g.: If hash of block with the largest height is not the last in array,
    // that block may never be announced.
    // Note that even if blocks are always processed according to height, hashes are added to this array
    // asynchronously making the ordering arbitrary.
    // This sort should not affect performance much since array vBlockHashesToAnnounce contains only small number
    // of hashes (often just one). This is because each time a new hash is added after tip was updated, network
    // thread is also woken up so that we immediately try to send them. As a result, this function is also called
    // shortly after and array vBlockHashesToAnnounce is always cleared before it completes.
    std::sort(pto->vBlockHashesToAnnounce.begin(), pto->vBlockHashesToAnnounce.end(), [](const auto& h1, const auto& h2){
        auto it_blk_idx1 = mapBlockIndex.Get(h1);
        auto it_blk_idx2 = mapBlockIndex.Get(h2);
        assert(it_blk_idx1);
        assert(it_blk_idx2);

        return it_blk_idx1->GetHeight() < it_blk_idx2->GetHeight();
    });

    // If we have less than MAX_BLOCKS_TO_ANNOUNCE in our list of block
    // hashes we're relaying, and our peer wants headers announcements, then
    // find the first header not yet known to our peer but would connect,
    // and send. If no header would connect, or if we have too many blocks,
    // or if the peer doesn't want headers, just add all to the inv queue.
    bool fRevertToInv =
        ((!state->fPreferHeaders && !state->fPreferHeadersEnriched &&
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
            auto pindex = mapBlockIndex.Get(hash);
            assert(pindex);
            if (pindex && chainActive[pindex->GetHeight()] != pindex) {
                // Bail out if we reorged away from this block
                fRevertToInv = true;
                break;
            }
            if (pindex && pBestIndex && pindex->GetPrev() != pBestIndex) {
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
            } else if (pindex && (pindex->IsGenesis() || PeerHasHeader(state, pindex->GetPrev())))
            {
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
            LogPrint(BCLog::NETMSG,
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
                auto reader =
                    pBestIndex->GetDiskBlockStreamReader( config );
                if (!reader) {
                    assert(!"cannot load block from disk");
                }
                SendCompactBlock(
                    config,
                    true,
                    pto,
                    connman,
                    msgMaker,
                    *reader);
            }
            state->pindexBestHeaderSent = pBestIndex;
        }
        else if (state->fPreferHeaders) {
            if (vHeaders.size() > 1) {
                LogPrint(BCLog::NETMSG,
                         "%s: %u headers, range (%s, %s), to peer=%d\n",
                         __func__, vHeaders.size(),
                         vHeaders.front().GetHash().ToString(),
                         vHeaders.back().GetHash().ToString(), pto->id);
            }
            else {
                LogPrint(BCLog::NETMSG, "%s: sending header %s to peer=%d\n",
                         __func__, vHeaders.front().GetHash().ToString(),
                         pto->id);
            }
            connman.PushMessage(pto, msgMaker.Make(NetMsgType::HEADERS, vHeaders));
            state->pindexBestHeaderSent = pBestIndex;
        }
        else if (state->fPreferHeadersEnriched) {
            const CBlockIndex* tip = chainActive.Tip();
            const std::int32_t chainActiveHeight = tip->GetHeight();

            // Convert vHeaders to array of enriched headers.
            std::vector<CBlockHeaderEnriched> vHeadersEnriched;
            vHeadersEnriched.reserve(vHeaders.size());
            size_t combinedMsgSize = 0;
            for(auto& h: vHeaders)
            {
                auto pindex = mapBlockIndex.Get(h.GetHash());
                assert(pindex);

                // Create enriched header
                auto& enrichedHeader = vHeadersEnriched.emplace_back(pindex);

                if (tip == pindex)
                {
                    // This is the last header that we currently have.
                    enrichedHeader.noMoreHeaders = true;
                }

                // Set coinbase information in enriched header
                // Note that contents of CB tx and its Merkle proof should be available here
                // since we only recently processed this block.
                enrichedHeader.SetCoinBaseInfo(msgMaker.GetVersion(), config, chainActiveHeight);

                combinedMsgSize += enrichedHeader.GetSerializedSize();
                if ( (combinedMsgSize + GetSizeOfCompactSize(vHeadersEnriched.size())) > static_cast<std::size_t>(pto->maxRecvPayloadLength) )
                {
                    // Size of hdrsen message would exceed maximum message size the peer can accept.
                    // Revert to sending inv.
                    fRevertToInv=true;
                    break;
                }
            }

            if(!fRevertToInv) {
                if (vHeadersEnriched.size() > 1) {
                    LogPrint(BCLog::NETMSG,
                             "%s: %u hdrsen, range (%s, %s), to peer=%d\n",
                             __func__, vHeadersEnriched.size(),
                             vHeadersEnriched.front().blockHeader.GetHash().ToString(),
                             vHeadersEnriched.back().blockHeader.GetHash().ToString(), pto->id);
                }
                else {
                    LogPrint(BCLog::NETMSG, "%s: sending hdrsen %s to peer=%d\n",
                             __func__, vHeadersEnriched.front().blockHeader.GetHash().ToString(),
                             pto->id);
                }
                connman.PushMessage(pto, msgMaker.Make(NetMsgType::HDRSEN, vHeadersEnriched));
                state->pindexBestHeaderSent = pBestIndex;
            }
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
            auto pindex = mapBlockIndex.Get(hashToAnnounce);
            assert(pindex);

            // Warn if we're announcing a block that is not on the main
            // chain. This should be very rare and could be optimized out.
            // Just log for now.
            if (chainActive[pindex->GetHeight()] != pindex) {
                LogPrint(BCLog::NETMSG,
                         "Announcing block %s not on main chain (tip=%s)\n",
                         hashToAnnounce.ToString(),
                         chainActive.Tip()->GetBlockHash().ToString());
            }

            // If the peer's chain has this block, don't inv it back.
            if (!PeerHasHeader(state, pindex)) {
                pto->PushBlockInventory(CInv(MSG_BLOCK, hashToAnnounce));
                LogPrint(BCLog::NETMSG, "%s: sending block inv peer=%d hash=%s\n",
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

        auto ret = mapRelay.insert(std::make_pair(std::move(txn.getInv().hash), txn.getTxnRef()));
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
            connman.PushMessage(pto, msgMaker.Make(CSerializedNetMsg::PayloadType::BLOCK, NetMsgType::INV, vInv));
            vInv.clear();
        }
    }
    pto->vInventoryBlockToSend.clear();

    // Send blocks inventory seperately over a higher priority stream (if available)
    if (!vInv.empty()) {
        connman.PushMessage(pto, msgMaker.Make(CSerializedNetMsg::PayloadType::BLOCK, NetMsgType::INV, vInv));
        vInv.clear();
    }

    // Check whether periodic sends should happen
    bool fSendTrickle = pto->fWhitelisted;
    if (pto->nNextInvSend < nNow) {
        fSendTrickle = true;
        pto->nNextInvSend = nNow + Fixed_delay_microsecs; 
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
            const uint256 &txid = txinfo.GetTxId();
            CInv inv(MSG_TX, txid);
            if (filterrate != Amount(0)) {
                if (txinfo.feeRate.GetFeePerK() < filterrate) {
                    continue;
                }
            }
            if (!pto->mFilter.IsRelevantAndUpdate(*txinfo.GetTx())) {
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

bool DetectStalling(const Config& config, const CNodePtr& pto, const CNodeStatePtr& state)
{
    assert(state);
    const Consensus::Params& consensusParams { config.GetChainParams().GetConsensus() };

    // Detect whether we're stalling
    int64_t nNow = GetTimeMicros();
    if (state->nStallingSince &&
        state->nStallingSince < nNow - MICROS_PER_SECOND * config.GetBlockStallingTimeout()) {
        // Stalling only triggers when the block download window cannot move.
        // During normal steady state, the download window should be much larger
        // than the to-be-downloaded set of blocks, so disconnection should only
        // happen during initial block download.
        // 
        // Also, don't abandon this attempt to download all the while we are making
        // sufficient progress, as measured by the current download speed to this
        // peer.
        uint64_t avgbw {0};
        if(IsBlockDownloadStallingFromPeer(config, pto, avgbw)) {
            LogPrintf("Peer=%d is stalling block download (current speed %d), disconnecting\n", pto->id, avgbw);
            pto->fDisconnect = true;
            return true;
        }
        else {
            LogPrint(BCLog::NETMSG, "Resetting stall (current speed %d) for peer=%d\n", avgbw, pto->id);
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
            blockDownloadTracker.GetPeersWithValidatedDownloadsCount() - 
            ((state->nBlocksInFlightValidHeaders > 0) ? 1 : 0);
        assert(nOtherPeersWithValidatedDownloads >= 0);

        auto timeoutBase = IsInitialBlockDownload() 
                                ? config.GetBlockDownloadTimeoutBaseIBD() 
                                : config.GetBlockDownloadTimeoutBase();
        auto timeoutPeers = config.GetBlockDownloadTimeoutPerPeer() * nOtherPeersWithValidatedDownloads;
        // nPowTargetSpacing is in seconds, timeout is percentage, while maxDownloadTime should be in microseconds
        // to get seconds we must multiply by 1000000 and divide by 100 which is equivalent to multipy by 10000
        auto maxDownloadTime = consensusParams.nPowTargetSpacing * (timeoutBase + timeoutPeers) * 10000;

        if (nNow > state->nDownloadingSince + maxDownloadTime) {
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
        FindNextBlocksToDownload(config, pto->GetId(),
                                 MAX_BLOCKS_IN_TRANSIT_PER_PEER - state->nBlocksInFlight,
                                 vToDownload, staller, consensusParams, state, connman);
        for (const CBlockIndex *pindex : vToDownload) {
            vGetData.push_back(CInv(MSG_BLOCK, pindex->GetBlockHash()));
            blockDownloadTracker.MarkBlockAsInFlight(config, {pindex->GetBlockHash(), pto->id}, state, *pindex);
            LogPrint(BCLog::NETMSG, "Requesting block %s (%d) peer=%d\n",
                     pindex->GetBlockHash().ToString(), pindex->GetHeight(),
                     pto->id);
        }
        if (state->nBlocksInFlight == 0 && staller != -1) {
            // Try to obtain an access to the node's state data.
            const CNodeStateRef stallerStateRef { GetState(staller) };
            const CNodeStatePtr& stallerState { stallerStateRef.get() };
            assert(stallerState);
            if (stallerState->nStallingSince == 0) {
                stallerState->nStallingSince = GetTimeMicros();
                uint64_t avgbw { pto->GetAssociation().GetAverageBandwidth(StreamPolicy::MessageType::BLOCK).first };
                LogPrint(BCLog::NETMSG, "Stall started (current speed %d) peer=%d\n", avgbw, staller);
            }
        }
    }
    if (!vGetData.empty()) {
        connman.PushMessage(pto, msgMaker.Make(CSerializedNetMsg::PayloadType::BLOCK, NetMsgType::GETDATA, vGetData));
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
                    LogPrint((inv.type == MSG_TX)? BCLog::NETMSGVERB : BCLog::NETMSG,
                             "Requesting %s peer=%d\n", inv.ToString(), pto->id);
                    vGetData.push_back(inv);
                    // if next element will cause too large message, then we send it now, as message size is still under limit
                    if (vGetData.size() == pto->maxInvElements) {
                        connman.PushMessage(pto, msgMaker.Make(NetMsgType::GETDATA, vGetData));
                        vGetData.clear();
                    }
                }
                else {
                    // If we're not going to ask, don't expect a response.
                    pto->indexAskFor.get<CNode::TagTxnID>().erase(inv.hash);
                }
                pto->mapAskFor.erase(firstIt);
            }
            else {
                // Look ahead to see if we can clear out some items we have already recieved from elsewhere
                if(alreadyHave) {
                    pto->indexAskFor.get<CNode::TagTxnID>().erase(inv.hash);
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

        // Check and expire entries from indexAskFor
        auto& timeIndex { pto->indexAskFor.get<CNode::TagInsertionTime>() };
        for(auto it = timeIndex.begin(); it != timeIndex.end(); ) {
            if(it->expiryTime > nNow) {
                break;
            }

            // Remove expired entry
            it = timeIndex.erase(it);
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
    if (pto->nVersion >= FEEFILTER_VERSION && config.GetFeeFilter() &&
        !(pto->fWhitelisted && config.GetWhitelistForceRelay()))
    {
        MempoolSizeLimits limits = MempoolSizeLimits::FromConfig();
        Amount currentFilter =
            mempool
                .GetMinFee(limits.Total())
                .GetFeePerK();
        int64_t timeNow = GetTimeMicros();
        if (timeNow > pto->nextSendTimeFeeFilter) {
            static CFeeRate default_feerate =
                CFeeRate(DEFAULT_MIN_RELAY_TX_FEE);
            static FeeFilterRounder filterRounder(default_feerate);
            Amount filterToSend = filterRounder.round(currentFilter);
            // We don't allow free transactions, we always have a fee
            // filter of at least minRelayTxFee
            filterToSend = std::max(filterToSend, config.GetMinFeePerKB().GetFeePerK());

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

