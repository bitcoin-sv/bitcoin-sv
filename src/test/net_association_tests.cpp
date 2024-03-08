// Copyright (c) 2020 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include <config.h>
#include <net/association.h>
#include <net/stream.h>
#include <net/stream_policy_factory.h>
#include <test/test_bitcoin.h>

#include <boost/algorithm/string.hpp>
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
        BOOST_CHECK_EQUAL(stats.streamType, enum_cast<std::string>(StreamType::GENERAL));
        BOOST_CHECK_EQUAL(stats.nLastSend, 0);
        BOOST_CHECK_EQUAL(stats.nLastRecv, 0);
        BOOST_CHECK_EQUAL(stats.nSendBytes, 0U);
        BOOST_CHECK_EQUAL(stats.nSendSize, 0U);
        BOOST_CHECK_EQUAL(stats.nRecvBytes, 0U);
        BOOST_CHECK_EQUAL(stats.nMinuteBytesPerSec, 0U);
        BOOST_CHECK_EQUAL(stats.nSpotBytesPerSec, 0U);
        BOOST_CHECK_EQUAL(stats.fPauseRecv, false);
    }

    CNodePtr MakeDummyNode(const CAddress& dummyAddr, CConnman::CAsyncTaskPool& taskPool)
    {
        return CNode::Make(
            0,
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
}

BOOST_FIXTURE_TEST_SUITE(TestNetAssociation, TestingSetup)

// Basic stream testing only without a real network connection
BOOST_AUTO_TEST_CASE(TestBasicStream)
{
    // Create dummy CNode just to be able to pass it to the CStream
    CAddress dummyAddr{ ip(0xa0b0c001), NODE_NONE };
    CConnman::CAsyncTaskPool asyncTaskPool { GlobalConfig::GetConfig() };
    CNodePtr pDummyNode { MakeDummyNode(dummyAddr, asyncTaskPool) };

    // Create a stream
    Stream stream { pDummyNode.get(), StreamType::GENERAL, INVALID_SOCKET, 1000 };

    // Check initial state
    StreamStats stats {};
    stream.CopyStats(stats);
    CheckInitialStreamStats(stats);

    BOOST_CHECK_EQUAL(stream.GetSendQueueSize(), 0U);
    BOOST_CHECK_EQUAL(stream.GetSendQeueMemoryUsage(), 0U);
    AverageBandwidth abw { stream.GetAverageBandwidth() };
    BOOST_CHECK_EQUAL(abw.first, 0U);
    BOOST_CHECK_EQUAL(abw.second, 0U);

    // Update avg bandwidth calcs
    std::this_thread::sleep_for(std::chrono::seconds(1));
    stream.AvgBandwithCalc();
    abw = stream.GetAverageBandwidth();
    BOOST_CHECK_EQUAL(abw.first, 0U);
    BOOST_CHECK_EQUAL(abw.second, 1U);
}

// Basic association testing only without a real network connection
BOOST_AUTO_TEST_CASE(TestBasicAssociation)
{
    // Create dummy CNode just to be able to pass it to the CStream
    CAddress dummyAddr{ ip(0xa0b0c001), NODE_NONE };
    CConnman::CAsyncTaskPool asyncTaskPool { GlobalConfig::GetConfig() };
    CNodePtr pDummyNode { MakeDummyNode(dummyAddr, asyncTaskPool) };

    // Create an association
    Association association { pDummyNode.get(), INVALID_SOCKET, dummyAddr };

    // Check initial state
    CAddress peerAddr { association.GetPeerAddr() };
    BOOST_CHECK_EQUAL(peerAddr.ToString(), "1.192.176.160:8333");

    AssociationStats stats {};
    association.CopyStats(stats);
    BOOST_CHECK_EQUAL(stats.streamStats.size(), 1U);
    CheckInitialStreamStats(stats.streamStats[0]);
    BOOST_CHECK_EQUAL(stats.nLastSend, 0);
    BOOST_CHECK_EQUAL(stats.nLastRecv, 0);
    BOOST_CHECK_EQUAL(stats.addr.ToString(), "1.192.176.160:8333");
    BOOST_CHECK_EQUAL(stats.nAvgBandwidth, 0U);
    BOOST_CHECK_EQUAL(stats.nSendBytes, 0U);
    BOOST_CHECK_EQUAL(stats.nRecvBytes, 0U);
    BOOST_CHECK_EQUAL(stats.nSendSize, 0U);
    BOOST_CHECK_EQUAL(stats.assocID, AssociationID::NULL_ID_STR);
    for(const auto& cmdTot : stats.mapSendBytesPerMsgCmd)
    {
        BOOST_CHECK_EQUAL(cmdTot.second, 0U);
    }
    for(const auto& cmdTot : stats.mapRecvBytesPerMsgCmd)
    {
        BOOST_CHECK_EQUAL(cmdTot.second, 0U);
    }

    BOOST_CHECK_EQUAL(association.GetTotalSendQueueSize(), 0U);
    BOOST_CHECK_EQUAL(association.GetTotalSendQueueMemoryUsage(), 0U);
    BOOST_CHECK_EQUAL(association.GetAverageBandwidth(), 0U);
    AverageBandwidth abw { association.GetAverageBandwidth(StreamType::GENERAL) };
    BOOST_CHECK_EQUAL(abw.first, 0U);
    BOOST_CHECK_EQUAL(abw.second, 0U);

    // Update avg bandwidth calcs
    std::this_thread::sleep_for(std::chrono::seconds(1));
    association.AvgBandwithCalc();
    BOOST_CHECK_EQUAL(association.GetAverageBandwidth(), 0U);
    abw = association.GetAverageBandwidth(StreamType::GENERAL);
    BOOST_CHECK_EQUAL(abw.first, 0U);
    BOOST_CHECK_EQUAL(abw.second, 1U);
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
    BOOST_CHECK_EQUAL(uuidAIDBytes.size(), 17U);
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

// Test stream policy factory
BOOST_AUTO_TEST_CASE(TestStreamPolicyFactory)
{
    StreamPolicyFactory factory {};

    // Fetch a known policy
    BOOST_CHECK_NO_THROW(
        StreamPolicyPtr policy { factory.Make(DefaultStreamPolicy::POLICY_NAME) };
        BOOST_CHECK(policy != nullptr);
    );

    // Fetch a non-existant policy
    BOOST_CHECK_THROW(
        StreamPolicyPtr policy { factory.Make("Unknown policy name") };
    , std::runtime_error);
}

// Test configuring available stream policies
BOOST_AUTO_TEST_CASE(TestStreamPolicyConfig)
{
    // Check unchanged supported stream policies
    std::set<std::string> defaultStreamPolicyList {};
    boost::split(defaultStreamPolicyList, DEFAULT_STREAM_POLICY_LIST, boost::is_any_of(","));
    BOOST_CHECK(StreamPolicyFactory{}.GetSupportedPolicyNames() == defaultStreamPolicyList);

    // Set the supported policy list as just Default
    gArgs.ForceSetArg("-multistreampolicies", DefaultStreamPolicy::POLICY_NAME);
    std::set<std::string> expected { DefaultStreamPolicy::POLICY_NAME };
    BOOST_CHECK(StreamPolicyFactory{}.GetSupportedPolicyNames() == expected);

    // Set the supported policy list as just BlockPriority, but we will always have Default available as well
    gArgs.ForceSetArg("-multistreampolicies", BlockPriorityStreamPolicy::POLICY_NAME);
    expected = { BlockPriorityStreamPolicy::POLICY_NAME, DefaultStreamPolicy::POLICY_NAME };
    BOOST_CHECK(StreamPolicyFactory{}.GetSupportedPolicyNames() == expected);

    // Try to configure an empty policy list, but we will still have Default
    gArgs.ForceSetArg("-multistreampolicies", "");
    expected = { DefaultStreamPolicy::POLICY_NAME };
    BOOST_CHECK(StreamPolicyFactory{}.GetSupportedPolicyNames() == expected);

    // Try to configure a non-existant policy name
    gArgs.ForceSetArg("-multistreampolicies", "Wibble");
    expected = { DefaultStreamPolicy::POLICY_NAME };
    BOOST_CHECK(StreamPolicyFactory{}.GetSupportedPolicyNames() == expected);

    // Configure the same policy name several times
    std::stringstream str {};
    str << BlockPriorityStreamPolicy::POLICY_NAME << "," << DefaultStreamPolicy::POLICY_NAME << "," <<
           BlockPriorityStreamPolicy::POLICY_NAME << "," << DefaultStreamPolicy::POLICY_NAME;
    gArgs.ForceSetArg("-multistreampolicies", str.str());
    expected = { BlockPriorityStreamPolicy::POLICY_NAME, DefaultStreamPolicy::POLICY_NAME };
    BOOST_CHECK(StreamPolicyFactory{}.GetSupportedPolicyNames() == expected);

    // Check prioritisation of configured policy names
    str.str("");
    str << BlockPriorityStreamPolicy::POLICY_NAME << "," << DefaultStreamPolicy::POLICY_NAME;
    gArgs.ForceSetArg("-multistreampolicies", str.str());
    std::vector<std::string> expectedPri { BlockPriorityStreamPolicy::POLICY_NAME, DefaultStreamPolicy::POLICY_NAME };
    BOOST_CHECK(StreamPolicyFactory{}.GetPrioritisedPolicyNames() == expectedPri);
    str.str("");
    str << DefaultStreamPolicy::POLICY_NAME << "," << BlockPriorityStreamPolicy::POLICY_NAME;
    gArgs.ForceSetArg("-multistreampolicies", str.str());
    expectedPri = { DefaultStreamPolicy::POLICY_NAME, BlockPriorityStreamPolicy::POLICY_NAME };
    BOOST_CHECK(StreamPolicyFactory{}.GetPrioritisedPolicyNames() == expectedPri);
}

BOOST_AUTO_TEST_SUITE_END()

