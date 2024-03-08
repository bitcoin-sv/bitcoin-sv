// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include <netmessagemaker.h>
#include <net/block_download_tracker.h>
#include <net/net_processing.h>
#include <validation.h>

#include <stdexcept>

// Notification that a block is now in flight
bool BlockDownloadTracker::MarkBlockAsInFlight(
    const Config& config, const BlockSource& block, const CNodeStatePtr& state,
    const CBlockIndex& pindex, std::list<QueuedBlock>::iterator** pit)
{
    std::lock_guard lock { mMtx };

    // Short-circuit most stuff in case it's the same block from the same node
    const auto [rangeBegin, rangeEnd] = mMapBlocksInFlight.equal_range(block.GetHash());
    for(auto itInFlight = rangeBegin; itInFlight != rangeEnd; ++itInFlight)
    {
        if(itInFlight->second.block.GetNode() == block.GetNode())
        {
            if(pit)
            {
                *pit = &itInFlight->second.queuedBlockIt;
            }
            return false;
        }
    }

    // Make partially downloaded block if we need one
    std::unique_ptr<PartiallyDownloadedBlock> partialBlock {nullptr};
    if(pit)
    {
        partialBlock = std::make_unique<PartiallyDownloadedBlock>(config, &mempool);
    }

    // Update node state
    std::list<QueuedBlock>::iterator it { state->vBlocksInFlight.insert( 
            state->vBlocksInFlight.end(), { block.GetHash(), pindex, true, std::move(partialBlock) }
        )
    };
    state->nBlocksInFlight++;
    state->nBlocksInFlightValidHeaders++;
    if(state->nBlocksInFlight == 1)
    {
        // We're starting a block download (batch) from this peer.
        state->nDownloadingSince = GetTimeMicros();
    }
    if(state->nBlocksInFlightValidHeaders == 1)
    {
        mPeersWithValidatedDownloadsCount++;
    }

    // Track block in flight
    auto itInFlight = mMapBlocksInFlight.insert(std::make_pair(block.GetHash(), InFlightBlock { block, it, GetTimeMicros() }));

    if(pit)
    {
        *pit = &itInFlight->second.queuedBlockIt;
    }

    return true;
}

// Notification that a block download was received, and we are about to validate it
bool BlockDownloadTracker::MarkBlockAsReceived(const BlockSource& block, bool punish, const CNodeStatePtr& state)
{
    std::lock_guard lock { mMtx };

    // Record block sender and whether to punish
    mMapBlockSender.insert(std::make_pair(block.GetHash(), BlockPunish{block, punish}));

    // Remove from in-flight details
    return removeFromBlockMapNL(block, state);
}

// Notification that a block download was cancelled, timed out or otherwise failed
bool BlockDownloadTracker::MarkBlockAsFailed(const BlockSource& block, const CNodeStatePtr& state)
{
    std::lock_guard lock { mMtx };
    return removeFromBlockMapNL(block, state);
}

// Notification that a downloaded block has been checked
void BlockDownloadTracker::BlockChecked(const uint256& hash, const CValidationState& state)
{
    std::vector<NodeId> sourceNodes {};
    {
        // Get all nodes block is downloading from. We have to do this here as
        // a separate step to later maintain the standard locking order of
        // node state first followed by our mutex.
        std::lock_guard lock { mMtx };
        sourceNodes = getAllSourcesForBlockNL(hash);
    }

    bool isIBD { IsInitialBlockDownload() };

    for(NodeId node : sourceNodes)
    {
        // Get access to the node's state data.
        const CNodeStateRef nodestateRef { GetState(node) };
        const CNodeStatePtr& nodestate { nodestateRef.get() };

        std::lock_guard lock { mMtx };

        const auto [rangeBegin, rangeEnd] = mMapBlockSender.equal_range(hash);
        for(auto it = rangeBegin; it != rangeEnd; ++it)
        {
            if(it->second.block.GetNode() == node)
            {
                int nDoS {0};
                if(state.IsInvalid(nDoS))
                {
                    // Don't send reject message with code 0 or an internal reject code.
                    if(nodestate && state.GetRejectCode() > 0 && state.GetRejectCode() < REJECT_INTERNAL)
                    {
                        nodestate->rejects.emplace_back(
                            static_cast<uint8_t>(state.GetRejectCode()),
                            state.GetRejectReason().substr(0, MAX_REJECT_MESSAGE_LENGTH),
                            it->second.block.GetHash());

                        if(nDoS > 0 && it->second.punish)
                        {
                            Misbehaving(node, nDoS, state.GetRejectReason());
                        }
                    }
                }
                // Check that:
                // 1. The block is valid
                // 2. We're not in initial block download
                // 3. This is currently the best block we're aware of. We haven't updated
                //    the tip yet so we have no way to check this directly here. Instead we
                //    just check that there are currently no other blocks in flight.
                else if(state.IsValid() && !isIBD &&
                        mMapBlocksInFlight.count(it->second.block.GetHash()) > 0 &&
                        getUniqueBlockCountNL() == 1)
                {
                    maybeSetPeerAsAnnouncingHeaderAndIDsNL(node, nodestate);
                }

                mMapBlockSender.erase(it);
                break;
            }
        }
    }
}

// Notification that a peer is too busy to send us blocks
void BlockDownloadTracker::PeerTooBusy(NodeId node)
{
    // Try to obtain an access to the node's state data.
    const CNodeStateRef stateRef { GetState(node) };
    const CNodeStatePtr& state { stateRef.get() };
    if(state)
    {
        // Peer is too busy with sending blocks so we will not ask again for TOOBUSY_RETRY_DELAY.
        state->nextSendThresholdTime = GetTimeMicros() + TOOBUSY_RETRY_DELAY;

        // Clear out all details for all blocks requested from this peer
        std::lock_guard lock { mMtx };
        while(!state->vBlocksInFlight.empty())
        {
            const QueuedBlock& entry { state->vBlocksInFlight.front() };
            removeFromBlockMapNL({entry.hash, node}, state);
        }
    }
}

// Clear out details for the given peer
void BlockDownloadTracker::ClearPeer(NodeId node, const CNodeStatePtr& state, bool lastPeer)
{
    std::lock_guard lock { mMtx };

    // Clear out entries for blocks in flight from this peer
    for(const QueuedBlock& entry : state->vBlocksInFlight)
    {
        const auto [rangeBegin, rangeEnd] = mMapBlocksInFlight.equal_range(entry.hash);
        for(auto itInFlight = rangeBegin; itInFlight != rangeEnd; ++itInFlight)
        {
            // Found node block was in flight from?
            if(itInFlight->second.block.GetNode() == node)
            {
                mMapBlocksInFlight.erase(itInFlight);
                break;
            }
        }
    }
    state->vBlocksInFlight.clear();

    mPeersWithValidatedDownloadsCount -= (state->nBlocksInFlightValidHeaders != 0);
    assert(mPeersWithValidatedDownloadsCount >= 0);

    // Clear out entries for block source
    for(auto it = mMapBlockSender.begin(); it != mMapBlockSender.end(); /*NA*/)
    {
        if(it->second.block.GetNode() == node)
        {
            it = mMapBlockSender.erase(it);
        }
        else
        {
            ++it;
        }
    }

    // Final consistency checks if this was our last peer
    if(lastPeer)
    {
        assert(mMapBlocksInFlight.empty());
        assert(mMapBlockSender.empty());
        assert(mPeersWithValidatedDownloadsCount == 0);
    }
}

// Get whether the given block is in flight from anyone
bool BlockDownloadTracker::IsInFlight(const uint256& hash) const
{
    std::lock_guard lock { mMtx };
    return mMapBlocksInFlight.count(hash) > 0;
}

// Get whether the given block is in flight from the given peer
bool BlockDownloadTracker::IsInFlight(const BlockSource& block) const
{
    std::lock_guard lock { mMtx };
    return getBlockFromNodeNL(block) != mMapBlocksInFlight.end();
}

// Get first peer specified block is in flight from (if any)
NodeId BlockDownloadTracker::GetPeerForBlock(const uint256& hash) const
{
    std::lock_guard lock { mMtx };

    const auto rangeBegin = mMapBlocksInFlight.equal_range(hash).first;
    if(rangeBegin != mMapBlocksInFlight.end())
    {
        return rangeBegin->second.block.GetNode();
    }

    // No peer found for that block
    return -1;
}

// Get whether the given block is the only one currently in flight
bool BlockDownloadTracker::IsOnlyBlockInFlight(const uint256& hash) const
{
    std::lock_guard lock { mMtx };

    if(getUniqueBlockCountNL() == 1)
    {
        return (mMapBlocksInFlight.begin()->first == hash);
    }

    return false;
}

// Fetch details for the specified in flight block
BlockDownloadTracker::InFlightBlock BlockDownloadTracker::GetBlockDetails(const BlockSource& block) const
{
    std::lock_guard lock { mMtx };

    const auto itInFlight { getBlockFromNodeNL(block) };
    if(itInFlight == mMapBlocksInFlight.end())
    {
        // Not found
        throw std::runtime_error("Queued block not found");
    }

    return itInFlight->second;
}

// Fetch all details for the specified in flight block
std::vector<BlockDownloadTracker::InFlightBlock> BlockDownloadTracker::GetBlockDetails(const uint256& hash) const
{
    std::vector<InFlightBlock> res {};

    std::lock_guard lock { mMtx };

    const auto [rangeBegin, rangeEnd] = mMapBlocksInFlight.equal_range(hash);
    for(auto it = rangeBegin; it != rangeEnd; ++it)
    {
        res.push_back(it->second);
    }

    return res;
}

// Get number of peers from which we are downloading blocks
int BlockDownloadTracker::GetPeersWithValidatedDownloadsCount() const 
{
    std::lock_guard lock{mMtx};
    return mPeersWithValidatedDownloadsCount;
}

// Remove a block from our in flight details
bool BlockDownloadTracker::removeFromBlockMapNL(const BlockSource& block, const CNodeStatePtr& state)
{
    const auto itInFlight { getBlockFromNodeNL(block) };
    if(itInFlight != mMapBlocksInFlight.end())
    {
        // Update nodes state
        const auto& queuedBlockIt { itInFlight->second.queuedBlockIt };
        state->nBlocksInFlightValidHeaders -= queuedBlockIt->fValidatedHeaders;
        if(state->nBlocksInFlightValidHeaders == 0 && queuedBlockIt->fValidatedHeaders)
        {
            // Last validated block on the queue was received.
            mPeersWithValidatedDownloadsCount--;
        }
        if(state->vBlocksInFlight.begin() == queuedBlockIt)
        {
            // First block on the queue was received, update the start download time for the next one
            state->nDownloadingSince = std::max(state->nDownloadingSince, GetTimeMicros());
        }
        state->vBlocksInFlight.erase(queuedBlockIt);
        state->nBlocksInFlight--;
        state->nStallingSince = 0;

        // Remove block details from in flight map
        mMapBlocksInFlight.erase(itInFlight);
        return true;
    }

    // Details not found
    return false;
}

// Find block from node
BlockDownloadTracker::InFlightMap::const_iterator BlockDownloadTracker::getBlockFromNodeNL(const BlockSource& block) const
{
    InFlightMap::const_iterator resIt { mMapBlocksInFlight.end() };

    const auto [rangeBegin, rangeEnd] = mMapBlocksInFlight.equal_range(block.GetHash());
    for(auto itInFlight = rangeBegin; itInFlight != rangeEnd; ++itInFlight)
    {
        // Found node block was in flight from?
        if(itInFlight->second.block.GetNode() == block.GetNode())
        {
            resIt = itInFlight;
            break;
        }
    }

    return resIt;
}

// Get count of unique blocks (blocks from multiple peers just counted once)
size_t BlockDownloadTracker::getUniqueBlockCountNL() const
{
    size_t count {0};

    for(auto it = mMapBlocksInFlight.begin(); it != mMapBlocksInFlight.end(); it = mMapBlocksInFlight.upper_bound(it->first))
    {
        ++count;
    }

    return count;
}

// Lookup sender NodeIds for the given block
std::vector<NodeId> BlockDownloadTracker::getAllSourcesForBlockNL(const uint256& hash) const
{
    std::vector<NodeId> sources {};

    const auto [rangeBegin, rangeEnd] = mMapBlockSender.equal_range(hash);
    for(auto it = rangeBegin; it != rangeEnd; ++it)
    {
        sources.push_back(it->second.block.GetNode());
    }

    return sources;
}

// Select peers to announce new blocks via compact blocks
void BlockDownloadTracker::maybeSetPeerAsAnnouncingHeaderAndIDsNL(NodeId nodeid, const CNodeStatePtr& nodestate)
{
    // Check whether node will provide compact blocks
    if(!nodestate || !nodestate->fProvidesHeaderAndIDs)
    {
        return;
    }

    // If we already know about this node, move it to the end of the list
    for(auto it = mNodesAnnouncingHeaderAndIDs.begin(); it != mNodesAnnouncingHeaderAndIDs.end(); ++it)
    {
        if(*it == nodeid)
        {
            mNodesAnnouncingHeaderAndIDs.erase(it);
            mNodesAnnouncingHeaderAndIDs.push_back(nodeid);
            return;
        }
    }

    // If we have more than 3 peers in the list, switch one we haven't received a block from for
    // a while to high bandwidth relaying mode (sendcmpct(1)). Set this node to low bandwidth
    // relaying mode (sendcmpct(0))
    CConnman& connman { *g_connman };
    connman.ForNode(nodeid, [&connman, this](const CNodePtr& pfrom)
    {
        bool fAnnounceUsingCMPCTBLOCK {false};
        uint64_t nCMPCTBLOCKVersion {1};
        if(mNodesAnnouncingHeaderAndIDs.size() >= 3)
        {
            // As per BIP152, we only get 3 of our peers to announce
            // blocks using compact encodings.
            connman.ForNode(mNodesAnnouncingHeaderAndIDs.front(),
                [&connman, fAnnounceUsingCMPCTBLOCK, nCMPCTBLOCKVersion](const CNodePtr& pnodeStop)
                {
                    connman.PushMessage(pnodeStop,
                                        CNetMsgMaker(pnodeStop->GetSendVersion())
                                            .Make(NetMsgType::SENDCMPCT, fAnnounceUsingCMPCTBLOCK, nCMPCTBLOCKVersion));
                    return true;
                }
            );
            mNodesAnnouncingHeaderAndIDs.pop_front();
        }

        // Add this node using low bandwidth relaying
        fAnnounceUsingCMPCTBLOCK = true;
        connman.PushMessage(pfrom,
                            CNetMsgMaker(pfrom->GetSendVersion())
                                .Make(NetMsgType::SENDCMPCT, fAnnounceUsingCMPCTBLOCK, nCMPCTBLOCKVersion));
        mNodesAnnouncingHeaderAndIDs.push_back(pfrom->GetId());
        return true;
    });
}


/**
 * Testing methods
 */

// Check block source is recorded as the given node
bool BlockDownloadTrackerTester::CheckBlockSource(const BlockDownloadTracker::BlockSource& block) const
{
    const auto [rangeBegin, rangeEnd] = mBlockTracker.mMapBlockSender.equal_range(block.GetHash());
    for(auto it = rangeBegin; it != rangeEnd; ++it)
    {
        if(it->second.block.GetNode() == block.GetNode())
        {
            return true;
        }
    }

    return false;
}

