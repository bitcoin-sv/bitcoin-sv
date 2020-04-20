// Copyright (c) 2020 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include <config.h>
#include <net/association.h>
#include <net/stream.h>
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

    void CheckInitialStreamStats(const StreamStats& stats)
    {
        BOOST_CHECK_EQUAL(stats.streamType, "GENERAL");
        BOOST_CHECK_EQUAL(stats.nLastSend, 0);
        BOOST_CHECK_EQUAL(stats.nLastRecv, 0);
        BOOST_CHECK_EQUAL(stats.nSendBytes, 0);
        BOOST_CHECK_EQUAL(stats.nSendSize, 0);
        BOOST_CHECK_EQUAL(stats.nRecvBytes, 0);
        BOOST_CHECK_EQUAL(stats.nMinuteBytesPerSec, 0);
        BOOST_CHECK_EQUAL(stats.nSpotBytesPerSec, 0);
    }
}

BOOST_FIXTURE_TEST_SUITE(TestNetAssociation, BasicTestingSetup)

// Basic stream testing only without a real network connection
BOOST_AUTO_TEST_CASE(TestBasicStream)
{
    // Create dummy CNode just to be abke to pass it to the CStream
    CAddress dummy_addr{ ip(0xa0b0c001), NODE_NONE };
    CConnman::CAsyncTaskPool asyncTaskPool { GlobalConfig::GetConfig() };
    CNodePtr pDummyNode =
        CNode::Make(
            0,
            NODE_NETWORK,
            0,
            INVALID_SOCKET,
            dummy_addr,
            0u,
            0u,
            asyncTaskPool,
            "",
            true);

    // Create a stream
    Stream stream { *pDummyNode, StreamType::GENERAL, INVALID_SOCKET };

    // Check initial state
    StreamStats stats {};
    stream.CopyStats(stats);
    CheckInitialStreamStats(stats);

    BOOST_CHECK_EQUAL(stream.GetSendQueueSize(), 0);
    AverageBandwidth abw { stream.GetAverageBandwidth() };
    BOOST_CHECK_EQUAL(abw.first, 0);
    BOOST_CHECK_EQUAL(abw.second, 0);

    // Update avg bandwidth calcs
    std::this_thread::sleep_for(std::chrono::seconds(1));
    stream.AvgBandwithCalc();
    abw = stream.GetAverageBandwidth();
    BOOST_CHECK_EQUAL(abw.first, 0);
    BOOST_CHECK_EQUAL(abw.second, 1);
}

// Basic association testing only without a real network connection
BOOST_AUTO_TEST_CASE(TestBasicAssociation)
{
    // Create dummy CNode just to be abke to pass it to the CStream
    CAddress dummy_addr{ ip(0xa0b0c001), NODE_NONE };
    CConnman::CAsyncTaskPool asyncTaskPool { GlobalConfig::GetConfig() };
    CNodePtr pDummyNode =
        CNode::Make(
            0,
            NODE_NETWORK,
            0,
            INVALID_SOCKET,
            dummy_addr,
            0u,
            0u,
            asyncTaskPool,
            "",
            true);

    // Create an association
    Association association { *pDummyNode, INVALID_SOCKET, dummy_addr };

    // Check initial state
    CAddress peerAddr { association.GetPeerAddr() };
    BOOST_CHECK_EQUAL(peerAddr.ToString(), "1.192.176.160:8333");

    AssociationStats stats {};
    association.CopyStats(stats);
    BOOST_CHECK_EQUAL(stats.streamStats.size(), 1);
    CheckInitialStreamStats(stats.streamStats[0]);
    BOOST_CHECK_EQUAL(stats.nLastSend, 0);
    BOOST_CHECK_EQUAL(stats.nLastRecv, 0);
    BOOST_CHECK_EQUAL(stats.addr.ToString(), "1.192.176.160:8333");
    BOOST_CHECK_EQUAL(stats.nAvgBandwidth, 0);
    BOOST_CHECK_EQUAL(stats.nSendBytes, 0);
    BOOST_CHECK_EQUAL(stats.nRecvBytes, 0);
    BOOST_CHECK_EQUAL(stats.nSendSize, 0);
    BOOST_CHECK_EQUAL(stats.assocID, "Null");
    for(const auto& cmdTot : stats.mapSendBytesPerMsgCmd)
    {
        BOOST_CHECK_EQUAL(cmdTot.second, 0);
    }
    for(const auto& cmdTot : stats.mapRecvBytesPerMsgCmd)
    {
        BOOST_CHECK_EQUAL(cmdTot.second, 0);
    }

    BOOST_CHECK_EQUAL(association.GetTotalSendQueueSize(), 0);
    BOOST_CHECK_EQUAL(association.GetAverageBandwidth(), 0);
    AverageBandwidth abw { association.GetAverageBandwidth(StreamType::GENERAL) };
    BOOST_CHECK_EQUAL(abw.first, 0);
    BOOST_CHECK_EQUAL(abw.second, 0);

    // Update avg bandwidth calcs
    std::this_thread::sleep_for(std::chrono::seconds(1));
    association.AvgBandwithCalc();
    BOOST_CHECK_EQUAL(association.GetAverageBandwidth(), 0);
    abw = association.GetAverageBandwidth(StreamType::GENERAL);
    BOOST_CHECK_EQUAL(abw.first, 0);
    BOOST_CHECK_EQUAL(abw.second, 1);
}

// Test AssociationID
BOOST_AUTO_TEST_CASE(TestAssociationID)
{
    // Generate new random UUIDs
    UUIDAssociationID uuidAID {};
    UUIDAssociationID uuidAID2 {};
    BOOST_CHECK(uuidAID.ToString() != uuidAID2.ToString());
    BOOST_CHECK(!(uuidAID == uuidAID2));
    std::vector<uint8_t> uuidAIDBytes { uuidAID.GetBytes() };
    BOOST_CHECK_EQUAL(uuidAIDBytes.size(), 17);
    BOOST_CHECK_EQUAL(uuidAIDBytes[0], static_cast<uint8_t>(AssociationID::IDType::UUID));

    // Regenerate an ID from the raw bytes
    BOOST_CHECK_NO_THROW(
        std::unique_ptr<AssociationID> reconstructed { AssociationID::Make(uuidAIDBytes) };
        BOOST_CHECK(reconstructed != nullptr);
        BOOST_CHECK_EQUAL(reconstructed->ToString(), uuidAID.ToString());
        BOOST_CHECK(*reconstructed == uuidAID);
        AssociationID& uuidAIDRef { uuidAID };
        BOOST_CHECK(*reconstructed == uuidAIDRef);
        BOOST_CHECK(uuidAIDRef == *reconstructed);
    );

    // Test factory method errors
    std::vector<uint8_t> uuidAIDBytesBadType { uuidAIDBytes };
    uuidAIDBytesBadType[0] = 0xff;
    BOOST_CHECK_THROW(
        std::unique_ptr<AssociationID> reconstructed { AssociationID::Make(uuidAIDBytesBadType) };
    , std::runtime_error);

    std::vector<uint8_t> uuidAIDBytesBadLength { static_cast<uint8_t>(AssociationID::IDType::UUID), 0x00 };
    BOOST_CHECK_THROW(
        std::unique_ptr<AssociationID> reconstructed { AssociationID::Make(uuidAIDBytesBadLength) };
    , std::runtime_error);

    BOOST_CHECK_NO_THROW(
        std::vector<uint8_t> uuidAIDBytesNull {};
        std::unique_ptr<AssociationID> reconstructed { AssociationID::Make(uuidAIDBytesNull)};
        BOOST_CHECK(reconstructed == nullptr);
    );
}

BOOST_AUTO_TEST_SUITE_END();

