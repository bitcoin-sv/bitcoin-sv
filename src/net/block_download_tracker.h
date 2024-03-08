// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include <net/node_state.h>
#include <uint256.h>

#include <list>
#include <map>
#include <mutex>
#include <vector>

class CValidationState;

/*
 * Track which blocks are in flight from which peers.
 */
class BlockDownloadTracker
{
    // Make the tester our friend so it can inspect us properly
    friend class BlockDownloadTrackerTester;

  public:

    // Details for where a block came from
    class BlockSource
    {
      public:
        BlockSource() = default;
        BlockSource(const uint256& hash, NodeId node) : mHash{hash}, mNode{node} {}

        // Accessors
        const uint256& GetHash() const { return mHash; }
        NodeId GetNode() const { return mNode; }

      private:
        uint256 mHash {};
        NodeId  mNode {-1};
    };

    // Details for in flight blocks
    struct InFlightBlock
    {
        BlockSource block {};
        std::list<QueuedBlock>::iterator queuedBlockIt {};
        int64_t inFlightSince {0};
    };

    // Default constructable, shouldn't ever need to be copied or moved
    BlockDownloadTracker() = default;
    ~BlockDownloadTracker() = default;
    BlockDownloadTracker(const BlockDownloadTracker&) = delete;
    BlockDownloadTracker(BlockDownloadTracker&&) = delete;
    BlockDownloadTracker& operator=(const BlockDownloadTracker&) = delete;
    BlockDownloadTracker& operator=(BlockDownloadTracker&&) = delete;

    // Notification that a block is now in flight from some peer
    bool MarkBlockAsInFlight(const Config& config, const BlockSource& block, const CNodeStatePtr& state,
        const CBlockIndex& pindex, std::list<QueuedBlock>::iterator** pit = nullptr);

    // Notification that a block download was received
    bool MarkBlockAsReceived(const BlockSource& block, bool punish, const CNodeStatePtr& state);

    // Notification that a block download was cancelled, timed out or otherwise failed
    bool MarkBlockAsFailed(const BlockSource& block, const CNodeStatePtr& state);

    // Notification that a downloaded block has been checked
    void BlockChecked(const uint256& hash, const CValidationState& state);

    // Notification that a peer is too busy to send us blocks
    void PeerTooBusy(NodeId node);

    // Clear out details for the given peer
    void ClearPeer(NodeId node, const CNodeStatePtr& state, bool lastPeer);

    // Get whether the given block is in flight from anyone
    bool IsInFlight(const uint256& hash) const;
    // Get whether the given block is in flight from the given peer
    bool IsInFlight(const BlockSource& block) const;

    // Get first peer specified block is in flight from (if any)
    NodeId GetPeerForBlock(const uint256& hash) const;

    // Get whether the given block is the only one currently in flight
    bool IsOnlyBlockInFlight(const uint256& hash) const;

    // Fetch details for the specified in flight block
    InFlightBlock GetBlockDetails(const BlockSource& block) const;
    std::vector<InFlightBlock> GetBlockDetails(const uint256& hash) const;

    // Get number of peers from which we are downloading blocks
    int GetPeersWithValidatedDownloadsCount() const;

  private:

    // Record whether a node should be punished for a block that fails validation
    struct BlockPunish
    {
        BlockSource block {};
        bool punish {false};
    };

    // Remove a block from our in flight details
    bool removeFromBlockMapNL(const BlockSource& block, const CNodeStatePtr& state);

    // Find block from node
    using InFlightMap = std::multimap<uint256, InFlightBlock>;
    InFlightMap::const_iterator getBlockFromNodeNL(const BlockSource& block) const;

    // Get count of unique blocks (blocks from multiple peers just counted once)
    size_t getUniqueBlockCountNL() const;

    // Lookup sender NodeIds for the given block
    std::vector<NodeId> getAllSourcesForBlockNL(const uint256& hash) const;

    // Select peers to announce new blocks via compact blocks
    void maybeSetPeerAsAnnouncingHeaderAndIDsNL(NodeId nodeid, const CNodeStatePtr& nodestate);


    // Blocks currently in flight and who they are in flight from
    InFlightMap mMapBlocksInFlight {};

    // Number of peers from which we're downloading blocks
    int mPeersWithValidatedDownloadsCount {0};

    // Where downloaded blocks came from and whether to punish
    std::multimap<uint256, BlockPunish> mMapBlockSender {};

    // Stack of nodes which we have set to announce using compact blocks
    std::list<NodeId> mNodesAnnouncingHeaderAndIDs {};

    // Mutex
    mutable std::mutex mMtx {};

};


/**
 * A class to aid testing of the BlockDownloadTracker, so that we don't have
 * to expose lots of testing methods on the main class itself.
 */
class BlockDownloadTrackerTester final
{
  public:

    BlockDownloadTrackerTester(const BlockDownloadTracker& tracker)
    : mBlockTracker{tracker}
    {}

    // Member accessors
    int GetPeersWithValidatedDownloadsCount() const { return mBlockTracker.mPeersWithValidatedDownloadsCount; }

    // Get count of tracked blocks
    size_t GetTrackedBlockCount() const { return mBlockTracker.mMapBlocksInFlight.size(); }
    // Get count of unique blocks (blocks from multiple peers just counted once)
    size_t GetUniqueBlockCount() const { return mBlockTracker.getUniqueBlockCountNL(); }

    // Check block source is recorded as the given node
    bool CheckBlockSource(const BlockDownloadTracker::BlockSource& block) const;

  private:

    const BlockDownloadTracker& mBlockTracker;
};

