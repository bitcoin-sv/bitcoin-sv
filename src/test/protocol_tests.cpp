// Copyright (c) 2019-2021 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "config.h"
#include "netmessagemaker.h"
#include "protocol.h"

#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>

namespace
{
    // For ID only
    class protocol_tests_id;

    // Null header mutating function
    auto NullHdrMutate = [](CMessageHeader&){};
    using HdrMutator = std::function<void(CMessageHeader&)>;

    // Serialise a net message with its header
    CDataStream SerialiseNetMsg(const Config& config, CSerializedNetMsg& msg, const HdrMutator& mutateHdr)
    {
        // Create header (and alter it if required by the test)
        CMessageHeader msgHdr { config, msg };
        mutateHdr(msgHdr);
        CDataStream serialisedMsg { SER_NETWORK, INIT_PROTO_VERSION };
        serialisedMsg << msgHdr;

        auto payloadStream { msg.MoveData() };
        while(!payloadStream->EndOfStream())
        {      
            const CSpan& data { payloadStream->ReadAsync(msg.Size()) };
            serialisedMsg.write(reinterpret_cast<const char*>(data.Begin()), data.Size());
        }

        return serialisedMsg;
    }
}

// Header class inspection
template<>
struct CMessageHeader::UnitTestAccess<protocol_tests_id>
{
    // Access private constructor
    template <typename... Args>
    static CMessageHeader Make(Args&&... args)
    {
        return CMessageHeader { std::forward<Args>(args)... };
    }

    // Get modifiable reference to command
    static char* ModifiableCmd(CMessageHeader& hdr)
    {
        return hdr.pchCommand.data();
    }

    // Get non-modifiable reference to command
    static const char* GetCmd(const CMessageHeader& hdr)
    {
        return hdr.pchCommand.data();
    }

    // Set payload length
    static void SetPayloadLength(CMessageHeader& hdr, uint32_t len)
    {
        hdr.nPayloadLength = len;
    }
};
using HdrUnitTestAccess = CMessageHeader::UnitTestAccess<protocol_tests_id>;

template<>
struct CExtendedMessageHeader::UnitTestAccess<protocol_tests_id>
{
    // Get modifiable reference to command
    static char* ModifiableCmd(CExtendedMessageHeader& hdr)
    {
        return hdr.pchCommand.data();
    }
};
using ExtHdrUnitTestAccess = CExtendedMessageHeader::UnitTestAccess<protocol_tests_id>;

BOOST_FIXTURE_TEST_SUITE(protocol_tests, TestingSetup)

BOOST_AUTO_TEST_CASE(protocol_msghdr_length)
{
    GlobalConfig config;
    config.SetDefaultBlockSizeParams(DefaultBlockSizeParams{0, 10000, 10000, 10000});

    // Test static header sizing methods
    BOOST_CHECK_EQUAL(CMessageHeader::GetHeaderSizeForPayload(0xFFFFFFFFL), CMessageFields::BASIC_HEADER_SIZE);
    BOOST_CHECK_EQUAL(CMessageHeader::GetHeaderSizeForPayload(static_cast<uint64_t>(0xFFFFFFFFL) + 1), CMessageFields::EXTENDED_HEADER_SIZE);
    BOOST_CHECK(! CMessageHeader::IsExtended(0xFFFFFFFFL));
    BOOST_CHECK(CMessageHeader::IsExtended(static_cast<uint64_t>(0xFFFFFFFFL) + 1));
    BOOST_CHECK_EQUAL(CMessageHeader::GetMaxPayloadLength(EXTENDED_PAYLOAD_VERSION - 1), std::numeric_limits<uint32_t>::max());
    BOOST_CHECK_EQUAL(CMessageHeader::GetMaxPayloadLength(EXTENDED_PAYLOAD_VERSION), std::numeric_limits<uint64_t>::max());

    // confirm that an incomplete message is not valid
    CMessageHeader hdr { config.GetChainParams().NetMagic() };
    BOOST_CHECK_EQUAL(hdr.IsValid(config), false);
    BOOST_CHECK_EQUAL(hdr.IsOversized(config), true);

    // zero length ok
    CMessageHeader zero { HdrUnitTestAccess::Make(config.GetChainParams().NetMagic(), NetMsgType::PING, 0UL, uint256{}) };
    BOOST_CHECK_EQUAL(zero.IsValid(config), true);
    BOOST_CHECK_EQUAL(zero.IsOversized(config), false);

    // test with inv message with 10 tx
    CMessageHeader inv10 { HdrUnitTestAccess::Make(config.GetChainParams().NetMagic(), NetMsgType::INV, (1UL+10*(4+32)), uint256{}) };
    BOOST_CHECK_EQUAL(inv10.IsValid(config), true);
    BOOST_CHECK_EQUAL(inv10.IsOversized(config), false);

    // test with max size message
    CMessageHeader sizemax { HdrUnitTestAccess::Make(config.GetChainParams().NetMagic(), NetMsgType::INV, config.GetMaxProtocolRecvPayloadLength(), uint256{}) };
    BOOST_CHECK_EQUAL(sizemax.IsValid(config), true);
    BOOST_CHECK_EQUAL(sizemax.IsOversized(config), false);

    // test with (max size + 1) message
    CMessageHeader sizemaxplus { HdrUnitTestAccess::Make(config.GetChainParams().NetMagic(), NetMsgType::INV, config.GetMaxProtocolRecvPayloadLength()+1, uint256{}) };
    BOOST_CHECK_EQUAL(sizemaxplus.IsValid(config), false);
    BOOST_CHECK_EQUAL(sizemaxplus.IsOversized(config), true);

    // test with max size GETBLOCKTXN message
    uint64_t maxSizeGetBlockTxn { NetMsgType::GetMaxMessageLength(NetMsgType::GETBLOCKTXN, config) };
    CMessageHeader maxSizeGetBlockTxnHdr { HdrUnitTestAccess::Make(config.GetChainParams().NetMagic(), NetMsgType::GETBLOCKTXN, maxSizeGetBlockTxn, uint256{}) };
    BOOST_CHECK_EQUAL(maxSizeGetBlockTxnHdr.IsValid(config), true);
    BOOST_CHECK_EQUAL(maxSizeGetBlockTxnHdr.IsOversized(config), false);

    // test with (max size + 1) GETBLOCKTXN message
    CMessageHeader maxplusSizeGetBlockTxnHdr { HdrUnitTestAccess::Make(config.GetChainParams().NetMagic(), NetMsgType::GETBLOCKTXN, maxSizeGetBlockTxn + 1, uint256{}) };
    BOOST_CHECK_EQUAL(maxplusSizeGetBlockTxnHdr.IsValid(config), false);
    BOOST_CHECK_EQUAL(maxplusSizeGetBlockTxnHdr.IsOversized(config), true);

    // Increase allowable block sizes beyond range of uint32_t
    constexpr uint64_t veryLargeBlockSize { 6UL * ONE_GIBIBYTE };
    config.SetDefaultBlockSizeParams(DefaultBlockSizeParams{0, veryLargeBlockSize, veryLargeBlockSize, veryLargeBlockSize});

    // test with non-extended max size BLOCK message
    CMessageHeader maxNonExtendedBlockHdr { HdrUnitTestAccess::Make(config.GetChainParams().NetMagic(), NetMsgType::BLOCK, std::numeric_limits<uint32_t>::max(), uint256{}) };
    BOOST_CHECK_EQUAL(maxNonExtendedBlockHdr.IsValid(config), true);
    BOOST_CHECK_EQUAL(maxNonExtendedBlockHdr.IsOversized(config), false);
    BOOST_CHECK_EQUAL(maxNonExtendedBlockHdr.IsExtended(), false);
    BOOST_CHECK_EQUAL(maxNonExtendedBlockHdr.GetLength(), CMessageFields::BASIC_HEADER_SIZE);
    BOOST_CHECK_EQUAL(maxNonExtendedBlockHdr.GetPayloadLength(), std::numeric_limits<uint32_t>::max());

    // test with extended large size BLOCK message
    CMessageHeader extendedBlockHdr { HdrUnitTestAccess::Make(config.GetChainParams().NetMagic(), NetMsgType::BLOCK, std::numeric_limits<uint32_t>::max() + static_cast<uint64_t>(1UL), uint256{}) };
    BOOST_CHECK_EQUAL(extendedBlockHdr.IsValid(config), true);
    BOOST_CHECK_EQUAL(extendedBlockHdr.IsOversized(config), false);
    BOOST_CHECK_EQUAL(extendedBlockHdr.IsExtended(), true);
    BOOST_CHECK_EQUAL(extendedBlockHdr.GetLength(), CMessageFields::EXTENDED_HEADER_SIZE);
    BOOST_CHECK_EQUAL(extendedBlockHdr.GetPayloadLength(), std::numeric_limits<uint32_t>::max() + static_cast<uint64_t>(1UL));

    // test with max size extended large BLOCK message
    CMessageHeader maxExtendedBlockHdr { HdrUnitTestAccess::Make(config.GetChainParams().NetMagic(), NetMsgType::BLOCK, veryLargeBlockSize, uint256{}) };
    BOOST_CHECK_EQUAL(maxExtendedBlockHdr.IsValid(config), true);
    BOOST_CHECK_EQUAL(maxExtendedBlockHdr.IsOversized(config), false);

    // test with oversized extended large BLOCK message
    CMessageHeader oversizeExtendedBlockHdr { HdrUnitTestAccess::Make(config.GetChainParams().NetMagic(), NetMsgType::BLOCK, veryLargeBlockSize + 1, uint256{}) };
    BOOST_CHECK_EQUAL(oversizeExtendedBlockHdr.IsValid(config), false);
    BOOST_CHECK_EQUAL(oversizeExtendedBlockHdr.IsOversized(config), true);
}

BOOST_AUTO_TEST_CASE(protocol_estimate_inv_elements)
{
    GlobalConfig config;
    config.SetDefaultBlockSizeParams(Params().GetDefaultBlockSizeParams());
    const CNetMsgMaker msgMaker(INIT_PROTO_VERSION);
    std::vector<CInv> vInv;
    uint32_t maxRecvPayloadLength = CInv::estimateMaxInvElements(config.GetMaxProtocolRecvPayloadLength());

    for (uint32_t i = 0; i < maxRecvPayloadLength - 1; i++) {
        vInv.emplace_back(1, uint256());
    }

    // Send maxInvElements - 1.
    auto serializedInvMsg = msgMaker.Make(NetMsgType::INV, vInv);
    size_t nPayloadLength = serializedInvMsg.Size();
    CMessageHeader hdrLess { HdrUnitTestAccess::Make(config.GetChainParams().NetMagic(), serializedInvMsg.Command().c_str(), nPayloadLength, uint256{}) };
    BOOST_CHECK_EQUAL(hdrLess.IsOversized(config), false);

    // Send maxInvElements.
    vInv.emplace_back(1, uint256());
    serializedInvMsg = msgMaker.Make(NetMsgType::INV, vInv);
    nPayloadLength = serializedInvMsg.Size();
    CMessageHeader hdrEqual { HdrUnitTestAccess::Make(config.GetChainParams().NetMagic(), serializedInvMsg.Command().c_str(), nPayloadLength, uint256{}) };
    BOOST_CHECK_EQUAL(hdrEqual.IsOversized(config), false);

    // Send maxInvElements + 1.
    vInv.emplace_back(1, uint256());
    serializedInvMsg = msgMaker.Make(NetMsgType::INV, vInv);
    nPayloadLength = serializedInvMsg.Size();
    CMessageHeader hdrMore { HdrUnitTestAccess::Make(config.GetChainParams().NetMagic(), serializedInvMsg.Command().c_str(), nPayloadLength, uint256{}) };
    BOOST_CHECK_EQUAL(hdrMore.IsOversized(config), true);
}

BOOST_AUTO_TEST_CASE(protocol_msghdr_magic)
{
    CMessageHeader::MessageMagic wrongMessageMagic { {0x05, 0x70, 0xEA, 0x12} };
    GlobalConfig config;
    config.SetDefaultBlockSizeParams(Params().GetDefaultBlockSizeParams());

    // simple test with invalid magic bytes
    CMessageHeader wrongMagic { HdrUnitTestAccess::Make(wrongMessageMagic, NetMsgType::PING, 4UL, uint256{}) };
    BOOST_CHECK_EQUAL(wrongMagic.IsValid(config), false);
    BOOST_CHECK_EQUAL(wrongMagic.IsOversized(config), false);
}

BOOST_AUTO_TEST_CASE(protocol_msghdr_command)
{
    GlobalConfig config;
    config.SetDefaultBlockSizeParams(Params().GetDefaultBlockSizeParams());

    // zero length command technically ok
    CMessageHeader zerochars { HdrUnitTestAccess::Make(config.GetChainParams().NetMagic(), "", 4UL, uint256{}) };
    BOOST_CHECK_EQUAL(zerochars.IsValid(config), true);
    BOOST_CHECK_EQUAL(zerochars.IsOversized(config), false);
    CExtendedMessageHeader zerocharsext { "", 4 };
    BOOST_CHECK_EQUAL(zerocharsext.IsValid(config), true);

    // command with length 12 ok
    CMessageHeader maxchars { HdrUnitTestAccess::Make(config.GetChainParams().NetMagic(), "123456789012", 4UL, uint256{}) };
    BOOST_CHECK_EQUAL(maxchars.IsValid(config), true);
    BOOST_CHECK_EQUAL(maxchars.IsOversized(config), false);
    CExtendedMessageHeader maxcharsext { "123456789012", 4 };
    BOOST_CHECK_EQUAL(maxcharsext.IsValid(config), true);

    // command with length 13 - constructor will only use the first 12
    CMessageHeader toomanychars { HdrUnitTestAccess::Make(config.GetChainParams().NetMagic(), "1234567890123", 4UL, uint256{}) };
    BOOST_CHECK_EQUAL(toomanychars.IsValid(config), true);
    BOOST_CHECK_EQUAL(toomanychars.GetCommand(), "123456789012");
    CExtendedMessageHeader toomanycharsext { "1234567890123", 4 };
    BOOST_CHECK_EQUAL(toomanycharsext.IsValid(config), true);
    BOOST_CHECK_EQUAL(toomanycharsext.GetCommand(), "123456789012");

    // the command can not have non-zero bytes after the first zero byte
    // the constructor will ignore the extra bytes, so first check is ok
    CMessageHeader extrachars { HdrUnitTestAccess::Make(config.GetChainParams().NetMagic(), "ERROR\0BY", 4UL, uint256{}) };
    BOOST_CHECK_EQUAL(extrachars.IsValid(config), true);
    BOOST_CHECK_EQUAL(extrachars.IsOversized(config), false);
    BOOST_CHECK_EQUAL(extrachars.GetCommand(), "ERROR");
    CExtendedMessageHeader extracharsext { "ERROR\0BY", 4 };
    BOOST_CHECK_EQUAL(extracharsext.IsValid(config), true);
    BOOST_CHECK_EQUAL(extracharsext.GetCommand(), "ERROR");
    // manually set the command to invalid value
    memcpy(HdrUnitTestAccess::ModifiableCmd(extrachars), "ERROR\0BY", 8);
    BOOST_CHECK_EQUAL(extrachars.IsValid(config), false);
    BOOST_CHECK_EQUAL(extrachars.IsOversized(config), false);
    memcpy(ExtHdrUnitTestAccess::ModifiableCmd(extracharsext), "ERROR\0BY", 8);
    BOOST_CHECK_EQUAL(extracharsext.IsValid(config), false);


    // Increase allowable block sizes beyond range of uint32_t
    constexpr uint64_t veryLargeBlockSize { 6UL * ONE_GIBIBYTE };
    config.SetDefaultBlockSizeParams(DefaultBlockSizeParams{0, veryLargeBlockSize, veryLargeBlockSize, veryLargeBlockSize});

    // Check command for extended header
    CMessageHeader extendedHdr { HdrUnitTestAccess::Make(config.GetChainParams().NetMagic(), NetMsgType::BLOCK, veryLargeBlockSize, uint256{}) };
    BOOST_CHECK_EQUAL(extendedHdr.IsExtended(), true);
    BOOST_CHECK_EQUAL(extendedHdr.GetCommand(), NetMsgType::BLOCK);
    BOOST_CHECK_EQUAL(std::string{HdrUnitTestAccess::GetCmd(extendedHdr)}, NetMsgType::EXTMSG);
}

BOOST_AUTO_TEST_CASE(net_messages)
{
    GlobalConfig config {};
    constexpr uint64_t veryLargeBlockSize { 6UL * ONE_GIBIBYTE };
    config.SetDefaultBlockSizeParams(DefaultBlockSizeParams{0, veryLargeBlockSize, veryLargeBlockSize, veryLargeBlockSize});
    const CNetMsgMaker msgMaker { INIT_PROTO_VERSION };

    // Simulate reading a message in chunks
    auto lambda = [&config, &msgMaker](const HdrMutator& hdrMutate, uint64_t bytesToRead, auto&&... msgArgs)
    {
        CSerializedNetMsg msg { msgMaker.Make(std::forward<decltype(msgArgs)>(msgArgs)...) };
        size_t payloadSize { msg.Size() };
        CDataStream serialisedMsg { SerialiseNetMsg(config, msg, hdrMutate) };
        uint64_t serialisedSize { serialisedMsg.size() };

        CNetMessage netMsg { config.GetChainParams().NetMagic(), SER_NETWORK, INIT_PROTO_VERSION };
        BOOST_CHECK(! netMsg.Complete());

        // Read into NetMessage in small chunks (to simulate data arriving over the netowrk in bits)
        uint64_t totRead {0};
        uint64_t maxIterations { (serialisedSize / bytesToRead) + 3 }; // Make sure test will always terminate
        for(auto i = 0UL; i < maxIterations; ++i)
        {
            uint64_t maxToRead { std::min<uint64_t>(bytesToRead, serialisedMsg.size()) };
            uint64_t numRead { netMsg.Read(config, serialisedMsg.data(), maxToRead) };
            serialisedMsg.erase(serialisedMsg.begin(), serialisedMsg.begin() + numRead);
            totRead += numRead;
        }
        BOOST_CHECK(netMsg.Complete());
        BOOST_CHECK_EQUAL(totRead, serialisedSize);
        BOOST_CHECK_EQUAL(netMsg.GetTotalLength(), serialisedSize);
        if(payloadSize > std::numeric_limits<uint32_t>::max())
        {
            BOOST_CHECK_EQUAL(netMsg.GetHeader().IsExtended(), true);
        }
        else
        {
            BOOST_CHECK_EQUAL(netMsg.GetHeader().IsExtended(), false);
        }
    };

    // A non-extended block message, read in 1 byte chunks
    auto oneK { std::make_unique<std::array<uint8_t, 1024>>() };
    lambda(NullHdrMutate, 1, NetMsgType::BLOCK, FLATDATA(*oneK));
    // A non-extended block message, read in 5 byte chunks
    lambda(NullHdrMutate, 5, NetMsgType::BLOCK, FLATDATA(*oneK));
    // A non-extended block message, reading as much as we can 
    lambda(NullHdrMutate, oneK->size() * 2, NetMsgType::BLOCK, FLATDATA(*oneK));

    // Verify a non-extended message with a bad length throws
    auto setBadLength = [&config](CMessageHeader &hdr) { HdrUnitTestAccess::SetPayloadLength(hdr, config.GetMaxProtocolRecvPayloadLength() + 1); };
    BOOST_CHECK_THROW(lambda(setBadLength, oneK->size() * 2, NetMsgType::PING, FLATDATA(*oneK)), BanPeer);

// Windows does not support total size of array exceeding 0x7fffffff bytes
#ifndef WIN32
    // A max size non-extended block message, reading as much as we can.
    // max32bit needs to go out of scope immediately after call to lambda to prevent next allocation failing.
    {
        auto max32bit { std::make_unique<std::array<uint8_t, std::numeric_limits<uint32_t>::max()>>() };
        lambda(NullHdrMutate, max32bit->size() * 2, NetMsgType::BLOCK, FLATDATA(*max32bit));
    }

    // An extended block message, reading as much as we can
    {
        auto extendedPayload { std::make_unique<std::array<uint8_t, std::numeric_limits<uint32_t>::max() + 1UL>>() };
        lambda(NullHdrMutate, extendedPayload->size() * 2, NetMsgType::BLOCK, FLATDATA(*extendedPayload));

        // Verify an extended message with a bad length throws
        BOOST_CHECK_THROW(lambda(setBadLength, extendedPayload->size() * 2, NetMsgType::PING, FLATDATA(*extendedPayload)), BanPeer);
    }
#endif
}

BOOST_AUTO_TEST_SUITE_END()
