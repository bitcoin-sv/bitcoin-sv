// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include <blockencodings.h>
#include <net/netaddress.h>
#include <net/net_types.h>
#include <locked_ref.h>
#include <protocol.h>
#include <uint256.h>
#include <utiltime.h>

#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>

class CBlockIndex;

// Blocks for which we need to send a reject
struct CBlockReject
{
    CBlockReject(uint8_t code, const std::string& reason, const uint256& hash)
    : chRejectCode{code}, strRejectReason{reason}, hashBlock{hash}
    {}

    uint8_t chRejectCode;
    std::string strRejectReason;
    uint256 hashBlock;
};

// Blocks that are in flight, and that are in the queue to be downloaded.
struct QueuedBlock
{
    uint256 hash;
    const CBlockIndex& blockIndex;
    // Whether this block has validated headers at the time of request.
    bool fValidatedHeaders;
    // Optional, used for CMPCTBLOCK downloads
    std::unique_ptr<PartiallyDownloadedBlock> partialBlock;
};

/**
 * Maintain validation-specific state about nodes.
 */
struct CNodeState
{
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
    //! Whether this peer wants invs or hdrsen (when possible) for block
    //! announcements.
    bool fPreferHeadersEnriched {false};
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

    CNodeState(const CService& addrIn, const std::string& addrNameIn)
        : address{addrIn}, name{addrNameIn}
    {
        hashLastUnknownBlock.SetNull();
    }

    bool CanSend() const {
        return nextSendThresholdTime < GetTimeMicros();
    }
};

using CNodeStatePtr = std::shared_ptr<CNodeState>;
using CNodeStateRef = CLockedRef<CNodeStatePtr, std::unique_lock<std::recursive_mutex>>;

/** Map maintaining per-node state. */
extern std::map<NodeId, CNodeStatePtr> mapNodeState;
extern std::shared_mutex mapNodeStateMtx;

// Fetch node state
CNodeStateRef GetState(NodeId pnode);
