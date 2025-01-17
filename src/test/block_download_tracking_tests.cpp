// Copyright (c) 2020 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include <consensus/validation.h>
#include <config.h>
#include <net/block_download_tracker.h>
#include <net/net.h>
#include <test/test_bitcoin.h>

#include <boost/test/unit_test.hpp>

namespace
{
    CService ip(uint32_t i)
    {
        struct in_addr s;
        s.s_addr = i;
        return CService(CNetAddr(s), Params().GetDefaultPort());
    }

    CNodePtr MakeDummyNode(const CAddress& dummyAddr, CConnman::CAsyncTaskPool& taskPool)
    {
        static NodeId id {1};

        return CNode::Make(
            id++,
            NODE_NETWORK,
            0,
            INVALID_SOCKET,
            dummyAddr,
            0u,
            0u,
            taskPool,
            "",
            true);
    }

    uint256 RandomBlockID()
    {
        return uint256 { InsecureRand256() };
    }

    bool operator==(const QueuedBlock& qb1, const QueuedBlock& qb2)
    {
        // We don't care about comparing CBlockIndex in these tests
        return qb1.hash == qb2.hash &&
               qb1.fValidatedHeaders == qb2.fValidatedHeaders &&
               qb1.partialBlock == qb2.partialBlock;
    }
}

BOOST_FIXTURE_TEST_SUITE(block_download_tracking_tests, TestingSetup)

BOOST_AUTO_TEST_CASE(TestBlockTracking)
{
    const Config& config { GlobalConfig::GetConfig() };
    const CBlockIndex* pindex { chainActive.Tip() };
    std::vector<uint256> blockIDs { RandomBlockID(), RandomBlockID(), RandomBlockID(), RandomBlockID() };
    std::reference_wrapper<uint256> blockID { blockIDs[0] };
    std::list<QueuedBlock>::iterator* pit {nullptr};
    BlockDownloadTracker::BlockSource blockSource {};

    CValidationState valid {};
    CValidationState invalid {};
    invalid.Invalid(false, 1);

    // Create a few dummy peers
    CConnman::CAsyncTaskPool asyncTaskPool { config };
    CNodePtr pDummyNode1 { MakeDummyNode(CAddress { ip(0xa0b0c001), NODE_NONE }, asyncTaskPool) };
    GetNodeSignals().InitializeNode(pDummyNode1, *connman, nullptr);
    CNodePtr pDummyNode2 { MakeDummyNode(CAddress { ip(0xa0b0c002), NODE_NONE }, asyncTaskPool) };
    GetNodeSignals().InitializeNode(pDummyNode2, *connman, nullptr);
    CNodePtr pDummyNode3 { MakeDummyNode(CAddress { ip(0xa0b0c003), NODE_NONE }, asyncTaskPool) };
    GetNodeSignals().InitializeNode(pDummyNode3, *connman, nullptr);

    // Non-thread-safe pointers to node states (but we're not using threads here so it's fine)
    const CNodeStatePtr nodeState1 { GetState(pDummyNode1->id).get() };
    const CNodeStatePtr nodeState2 { GetState(pDummyNode2->id).get() };
    const CNodeStatePtr nodeState3 { GetState(pDummyNode3->id).get() };
    // Check initial node states
    BOOST_CHECK_EQUAL(nodeState1->nBlocksInFlight, 0);
    BOOST_CHECK_EQUAL(nodeState1->vBlocksInFlight.size(), 0U);
    BOOST_CHECK_EQUAL(nodeState1->rejects.size(), 0U);
    BOOST_CHECK_EQUAL(nodeState2->nBlocksInFlight, 0);
    BOOST_CHECK_EQUAL(nodeState2->vBlocksInFlight.size(), 0U);
    BOOST_CHECK_EQUAL(nodeState2->rejects.size(), 0U);
    BOOST_CHECK_EQUAL(nodeState3->nBlocksInFlight, 0);
    BOOST_CHECK_EQUAL(nodeState3->vBlocksInFlight.size(), 0U);
    BOOST_CHECK_EQUAL(nodeState3->rejects.size(), 0U);

    // Block tracker and tester
    BlockDownloadTracker blockTracker {};
    BlockDownloadTrackerTester tester { blockTracker };

    // Initial block tracking state
    BOOST_CHECK_EQUAL(tester.GetPeersWithValidatedDownloadsCount(), 0);
    BOOST_CHECK_EQUAL(tester.GetTrackedBlockCount(), 0U);
    BOOST_CHECK_EQUAL(tester.GetUniqueBlockCount(), 0U);
    std::vector<BlockDownloadTracker::InFlightBlock> blockDetails { blockTracker.GetBlockDetails(RandomBlockID()) };
    BOOST_CHECK_EQUAL(blockDetails.size(), 0U);
    BOOST_CHECK(! blockTracker.IsOnlyBlockInFlight(blockIDs[0]));

    // Add tracked block1 from node1
    blockID = blockIDs[0];
    blockSource = { blockID, pDummyNode1->id };
    blockTracker.MarkBlockAsInFlight(config, blockSource, nodeState1, *pindex);
    BOOST_CHECK(blockTracker.IsInFlight(blockID));
    BOOST_CHECK(blockTracker.IsInFlight(blockSource));
    BOOST_CHECK(! blockTracker.IsInFlight(blockIDs[1]));
    BOOST_CHECK(! blockTracker.IsInFlight({blockID, pDummyNode2->id}));
    BOOST_CHECK(blockTracker.IsOnlyBlockInFlight(blockID));
    BOOST_CHECK_EQUAL(tester.GetPeersWithValidatedDownloadsCount(), 1);
    BOOST_CHECK_EQUAL(tester.GetTrackedBlockCount(), 1U);
    BOOST_CHECK_EQUAL(tester.GetUniqueBlockCount(), 1U);
    blockDetails = blockTracker.GetBlockDetails(blockID);
    BOOST_CHECK_EQUAL(blockDetails.size(), 1U);
    BOOST_CHECK_EQUAL(blockDetails[0].block.GetNode(), pDummyNode1->id);
    BOOST_CHECK_EQUAL(blockDetails[0].queuedBlockIt->hash, blockID);
    BOOST_CHECK_EQUAL(nodeState1->nBlocksInFlight, 1);
    BOOST_CHECK_EQUAL(nodeState1->vBlocksInFlight.size(), 1U);

    // Add tracked block2 from node2
    blockID = blockIDs[1];
    blockSource = { blockID, pDummyNode2->id };
    blockTracker.MarkBlockAsInFlight(config, blockSource, nodeState2, *pindex);
    BOOST_CHECK(blockTracker.IsInFlight(blockID));
    BOOST_CHECK(blockTracker.IsInFlight(blockSource));
    BOOST_CHECK_EQUAL(tester.GetPeersWithValidatedDownloadsCount(), 2);
    BOOST_CHECK_EQUAL(tester.GetTrackedBlockCount(), 2U);
    BOOST_CHECK_EQUAL(tester.GetUniqueBlockCount(), 2U);
    blockDetails = blockTracker.GetBlockDetails(blockID);
    BOOST_CHECK_EQUAL(blockDetails.size(), 1U);
    BOOST_CHECK_EQUAL(blockDetails[0].block.GetNode(), pDummyNode2->id);
    BOOST_CHECK_EQUAL(blockDetails[0].queuedBlockIt->hash, blockID);
    BOOST_CHECK_EQUAL(nodeState2->nBlocksInFlight, 1);
    BOOST_CHECK_EQUAL(nodeState2->vBlocksInFlight.size(), 1U);
    BOOST_CHECK(! blockTracker.IsOnlyBlockInFlight(blockID));

    // Add tracked block3 from node3
    blockID = blockIDs[2];
    blockSource = { blockID, pDummyNode3->id };
    blockTracker.MarkBlockAsInFlight(config, blockSource, nodeState3, *pindex);
    BOOST_CHECK(blockTracker.IsInFlight(blockID));
    BOOST_CHECK(blockTracker.IsInFlight(blockSource));
    BOOST_CHECK_EQUAL(tester.GetPeersWithValidatedDownloadsCount(), 3);
    BOOST_CHECK_EQUAL(tester.GetTrackedBlockCount(), 3U);
    BOOST_CHECK_EQUAL(tester.GetUniqueBlockCount(), 3U);
    blockDetails = blockTracker.GetBlockDetails(blockID);
    BOOST_CHECK_EQUAL(blockDetails.size(), 1U);
    BOOST_CHECK_EQUAL(blockDetails[0].block.GetNode(), pDummyNode3->id);
    BOOST_CHECK_EQUAL(blockDetails[0].queuedBlockIt->hash, blockID);
    BOOST_CHECK_EQUAL(nodeState3->nBlocksInFlight, 1);
    BOOST_CHECK_EQUAL(nodeState3->vBlocksInFlight.size(), 1U);

    // Also track block1 from node2
    blockID = blockIDs[0];
    pit = nullptr;
    blockTracker.MarkBlockAsInFlight(config, {blockID, pDummyNode2->id}, nodeState2, *pindex, &pit);
    BOOST_CHECK(blockTracker.IsInFlight(blockID));
    BOOST_CHECK(blockTracker.IsInFlight({blockID, pDummyNode1->id}));
    BOOST_CHECK(blockTracker.IsInFlight({blockID, pDummyNode2->id}));
    BOOST_CHECK_EQUAL(tester.GetPeersWithValidatedDownloadsCount(), 3);
    BOOST_CHECK_EQUAL(tester.GetTrackedBlockCount(), 4U);
    BOOST_CHECK_EQUAL(tester.GetUniqueBlockCount(), 3U);
    blockDetails = blockTracker.GetBlockDetails(blockID);
    BOOST_CHECK_EQUAL(blockDetails.size(), 2U);
    BOOST_CHECK_EQUAL(blockDetails[0].block.GetNode(), pDummyNode1->id);
    BOOST_CHECK_EQUAL(blockDetails[0].queuedBlockIt->hash, blockID);
    BOOST_CHECK_EQUAL(blockDetails[1].block.GetNode(), pDummyNode2->id);
    BOOST_CHECK_EQUAL(blockDetails[1].queuedBlockIt->hash, blockID);
    BOOST_CHECK_EQUAL(nodeState1->nBlocksInFlight, 1);
    BOOST_CHECK_EQUAL(nodeState1->vBlocksInFlight.size(), 1U);
    BOOST_CHECK_EQUAL(nodeState2->nBlocksInFlight, 2);
    BOOST_CHECK_EQUAL(nodeState2->vBlocksInFlight.size(), 2U);
    BOOST_CHECK(**pit == *(blockDetails[1].queuedBlockIt));

    {
        // We won't find the partial block from node3
        BOOST_CHECK_THROW(blockTracker.GetBlockDetails({blockID, pDummyNode3->id}), std::runtime_error);
        // But we will from node2
        BOOST_CHECK_NO_THROW(
            auto blockDetails { blockTracker.GetBlockDetails({blockID, pDummyNode2->id}) };
            BOOST_CHECK_EQUAL(blockDetails.block.GetNode(), pDummyNode2->id);
            BOOST_CHECK(blockDetails.queuedBlockIt->partialBlock != nullptr);
        );
        // Will also find it from node1
        BOOST_CHECK_NO_THROW(
            auto blockDetails = blockTracker.GetBlockDetails({blockID, pDummyNode1->id});
            BOOST_CHECK_EQUAL(blockDetails.block.GetNode(), pDummyNode1->id);
            // Node1 has null partial block however
            BOOST_CHECK(blockDetails.queuedBlockIt->partialBlock == nullptr);
        );
    }

    // Track duplicate block(1) from duplicate node(1)
    blockID = blockIDs[0];
    pit = nullptr;
    blockTracker.MarkBlockAsInFlight(config, {blockID, pDummyNode1->id}, nodeState1, *pindex, &pit);
    BOOST_CHECK_EQUAL(tester.GetPeersWithValidatedDownloadsCount(), 3);
    BOOST_CHECK_EQUAL(tester.GetTrackedBlockCount(), 4U);
    BOOST_CHECK_EQUAL(tester.GetUniqueBlockCount(), 3U);
    blockDetails = blockTracker.GetBlockDetails(blockID);
    BOOST_CHECK_EQUAL(blockDetails.size(), 2U);
    BOOST_CHECK_EQUAL(blockDetails[0].block.GetNode(), pDummyNode1->id);
    BOOST_CHECK_EQUAL(blockDetails[0].queuedBlockIt->hash, blockID);
    BOOST_CHECK_EQUAL(blockDetails[1].block.GetNode(), pDummyNode2->id);
    BOOST_CHECK_EQUAL(blockDetails[1].queuedBlockIt->hash, blockID);
    BOOST_CHECK_EQUAL(nodeState1->nBlocksInFlight, 1);
    BOOST_CHECK_EQUAL(nodeState1->vBlocksInFlight.size(), 1U);
    BOOST_CHECK_EQUAL(nodeState2->nBlocksInFlight, 2);
    BOOST_CHECK_EQUAL(nodeState2->vBlocksInFlight.size(), 2U);
    BOOST_CHECK(**pit == *(blockDetails[0].queuedBlockIt));

    // Mark block1 as received from node2 and valid
    blockID = blockIDs[0];
    blockSource = { blockID, pDummyNode2->id };
    blockTracker.MarkBlockAsReceived(blockSource, true, nodeState2);
    BOOST_CHECK(tester.CheckBlockSource(blockSource));
    blockTracker.BlockChecked(blockID, valid);
    BOOST_CHECK(blockTracker.IsInFlight(blockID));
    BOOST_CHECK(blockTracker.IsInFlight({blockID, pDummyNode1->id}));
    BOOST_CHECK(! blockTracker.IsInFlight(blockSource));
    BOOST_CHECK_EQUAL(tester.GetPeersWithValidatedDownloadsCount(), 3);
    BOOST_CHECK_EQUAL(tester.GetTrackedBlockCount(), 3U);
    BOOST_CHECK_EQUAL(tester.GetUniqueBlockCount(), 3U);
    blockDetails = blockTracker.GetBlockDetails(blockID);
    BOOST_CHECK_EQUAL(blockDetails.size(), 1U);
    BOOST_CHECK_EQUAL(blockDetails[0].block.GetNode(), pDummyNode1->id);
    BOOST_CHECK_EQUAL(blockDetails[0].queuedBlockIt->hash, blockID);
    BOOST_CHECK_EQUAL(nodeState1->nBlocksInFlight, 1);
    BOOST_CHECK_EQUAL(nodeState1->vBlocksInFlight.size(), 1U);
    BOOST_CHECK_EQUAL(nodeState2->nBlocksInFlight, 1);
    BOOST_CHECK_EQUAL(nodeState2->vBlocksInFlight.size(), 1U);
    BOOST_CHECK_EQUAL(nodeState2->rejects.size(), 0U);

    // Mark block2 as received from node2 and valid
    blockID = blockIDs[1];
    blockSource = { blockID, pDummyNode2->id };
    blockTracker.MarkBlockAsReceived(blockSource, true, nodeState2);
    BOOST_CHECK(tester.CheckBlockSource(blockSource));
    BOOST_CHECK(! blockTracker.IsInFlight(blockID));
    BOOST_CHECK(! blockTracker.IsInFlight(blockSource));
    BOOST_CHECK_EQUAL(tester.GetPeersWithValidatedDownloadsCount(), 2);
    BOOST_CHECK_EQUAL(tester.GetTrackedBlockCount(), 2U);
    BOOST_CHECK_EQUAL(tester.GetUniqueBlockCount(), 2U);
    blockDetails = blockTracker.GetBlockDetails(blockID);
    BOOST_CHECK_EQUAL(blockDetails.size(), 0U);
    BOOST_CHECK_EQUAL(nodeState2->nBlocksInFlight, 0);
    BOOST_CHECK_EQUAL(nodeState2->vBlocksInFlight.size(), 0U);
    BOOST_CHECK_EQUAL(nodeState2->rejects.size(), 0U);

    // Mark block3 as received from node3 and invalid
    blockID = blockIDs[2];
    blockSource = { blockID, pDummyNode3->id };
    blockTracker.MarkBlockAsReceived(blockSource, true, nodeState3);
    BOOST_CHECK(tester.CheckBlockSource(blockSource));
    blockTracker.BlockChecked(blockID, invalid);
    BOOST_CHECK(! blockTracker.IsInFlight(blockID));
    BOOST_CHECK(! blockTracker.IsInFlight(blockSource));
    BOOST_CHECK_EQUAL(tester.GetPeersWithValidatedDownloadsCount(), 1);
    BOOST_CHECK_EQUAL(tester.GetTrackedBlockCount(), 1U);
    BOOST_CHECK_EQUAL(tester.GetUniqueBlockCount(), 1U);
    blockDetails = blockTracker.GetBlockDetails(blockID);
    BOOST_CHECK_EQUAL(blockDetails.size(), 0U);
    BOOST_CHECK_EQUAL(nodeState3->nBlocksInFlight, 0);
    BOOST_CHECK_EQUAL(nodeState3->vBlocksInFlight.size(), 0U);
    BOOST_CHECK_EQUAL(nodeState3->rejects.size(), 1U);

    // Mark block1 as failed from node1
    blockID = blockIDs[0];
    blockSource = { blockID, pDummyNode1->id };
    blockTracker.MarkBlockAsFailed(blockSource, nodeState1);
    BOOST_CHECK(! blockTracker.IsInFlight(blockID));
    BOOST_CHECK(! blockTracker.IsInFlight(blockSource));
    BOOST_CHECK_EQUAL(tester.GetPeersWithValidatedDownloadsCount(), 0);
    BOOST_CHECK_EQUAL(tester.GetTrackedBlockCount(), 0U);
    BOOST_CHECK_EQUAL(tester.GetUniqueBlockCount(), 0U);
    blockDetails = blockTracker.GetBlockDetails(blockID);
    BOOST_CHECK_EQUAL(blockDetails.size(), 0U);
    BOOST_CHECK_EQUAL(nodeState1->nBlocksInFlight, 0);
    BOOST_CHECK_EQUAL(nodeState1->vBlocksInFlight.size(), 0U);
    BOOST_CHECK_EQUAL(nodeState1->rejects.size(), 0U);

    // Request block3 & block4 from node1, but now they're too busy
    BOOST_CHECK(nodeState1->CanSend());
    blockTracker.MarkBlockAsInFlight(config, {blockIDs[2], pDummyNode1->id}, nodeState1, *pindex);
    blockTracker.MarkBlockAsInFlight(config, {blockIDs[3], pDummyNode1->id}, nodeState1, *pindex);
    BOOST_CHECK(blockTracker.IsInFlight(blockIDs[2]));
    BOOST_CHECK(blockTracker.IsInFlight({blockIDs[2], pDummyNode1->id}));
    BOOST_CHECK(blockTracker.IsInFlight(blockIDs[3]));
    BOOST_CHECK(blockTracker.IsInFlight({blockIDs[3], pDummyNode1->id}));
    BOOST_CHECK_EQUAL(tester.GetPeersWithValidatedDownloadsCount(), 1);
    BOOST_CHECK_EQUAL(tester.GetTrackedBlockCount(), 2U);
    BOOST_CHECK_EQUAL(nodeState1->nBlocksInFlight, 2);
    BOOST_CHECK_EQUAL(nodeState1->vBlocksInFlight.size(), 2U);
    blockTracker.PeerTooBusy(pDummyNode1->id);
    BOOST_CHECK(! nodeState1->CanSend());
    BOOST_CHECK(! blockTracker.IsInFlight(blockIDs[2]));
    BOOST_CHECK(! blockTracker.IsInFlight(blockIDs[3]));
    BOOST_CHECK_EQUAL(tester.GetPeersWithValidatedDownloadsCount(), 0);
    BOOST_CHECK_EQUAL(tester.GetTrackedBlockCount(), 0U);
    BOOST_CHECK_EQUAL(nodeState1->nBlocksInFlight, 0);
    BOOST_CHECK_EQUAL(nodeState1->vBlocksInFlight.size(), 0U);

    // Request block4 from node2 and node3 and mark as received from both
    blockID = blockIDs[3];
    blockTracker.MarkBlockAsInFlight(config, {blockID, pDummyNode2->id}, nodeState2, *pindex);
    blockTracker.MarkBlockAsInFlight(config, {blockID, pDummyNode3->id}, nodeState3, *pindex);
    BOOST_CHECK(blockTracker.IsInFlight(blockID));
    BOOST_CHECK(blockTracker.IsInFlight({blockID, pDummyNode2->id}));
    BOOST_CHECK(blockTracker.IsInFlight({blockID, pDummyNode3->id}));
    BOOST_CHECK_EQUAL(tester.GetPeersWithValidatedDownloadsCount(), 2);
    BOOST_CHECK_EQUAL(tester.GetTrackedBlockCount(), 2U);
    BOOST_CHECK_EQUAL(tester.GetUniqueBlockCount(), 1U);
    blockTracker.MarkBlockAsReceived({blockID, pDummyNode2->id}, true, nodeState2);
    BOOST_CHECK(tester.CheckBlockSource({blockID, pDummyNode2->id}));
    blockTracker.MarkBlockAsReceived({blockID, pDummyNode3->id}, true, nodeState3);
    BOOST_CHECK(tester.CheckBlockSource({blockID, pDummyNode3->id}));
    blockTracker.BlockChecked(blockID, valid);
    BOOST_CHECK(! tester.CheckBlockSource({blockID, pDummyNode2->id}));
    BOOST_CHECK(! tester.CheckBlockSource({blockID, pDummyNode3->id}));
    BOOST_CHECK(! blockTracker.IsInFlight(blockID));
    BOOST_CHECK(! blockTracker.IsInFlight({blockID, pDummyNode2->id}));
    BOOST_CHECK(! blockTracker.IsInFlight({blockID, pDummyNode3->id}));
    BOOST_CHECK_EQUAL(tester.GetPeersWithValidatedDownloadsCount(), 0);
    BOOST_CHECK_EQUAL(tester.GetTrackedBlockCount(), 0U);
    BOOST_CHECK_EQUAL(tester.GetUniqueBlockCount(), 0U);

    // Tidy up nodes (and check asserts)
    {
        bool fUpdateConnectionTime {false};
        LOCK(cs_main);
        GetNodeSignals().FinalizeNode(pDummyNode1->id, fUpdateConnectionTime);
        GetNodeSignals().FinalizeNode(pDummyNode2->id, fUpdateConnectionTime);
        GetNodeSignals().FinalizeNode(pDummyNode3->id, fUpdateConnectionTime);
    }
}

BOOST_AUTO_TEST_SUITE_END();

