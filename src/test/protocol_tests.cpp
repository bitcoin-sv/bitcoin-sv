// Copyright (c) 2019 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "config.h"
#include "netmessagemaker.h"
#include "protocol.h"

#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(protocol_tests, TestingSetup)


BOOST_AUTO_TEST_CASE(protocol_msghdr_length)
{
    GlobalConfig config;
    config.SetDefaultBlockSizeParams(DefaultBlockSizeParams{0, 10000, 10000, 10000});

    // confirm that an incomplete message is not valid
    CMessageHeader hdr(config.GetChainParams().NetMagic());
    BOOST_CHECK_EQUAL(hdr.IsValid(config), false);
    BOOST_CHECK_EQUAL(hdr.IsOversized(config, config.GetMaxBlockSize()), true);

    // zero length ok
    CMessageHeader zero(config.GetChainParams().NetMagic(), NetMsgType::PING, 0);
    BOOST_CHECK_EQUAL(zero.IsValid(config), true);
    BOOST_CHECK_EQUAL(zero.IsOversized(config, config.GetMaxBlockSize()), false);

    // test with inv message with 10 tx
    CMessageHeader inv10(config.GetChainParams().NetMagic(), NetMsgType::INV, (1+10*(4+32)));
    BOOST_CHECK_EQUAL(inv10.IsValid(config), true);
    BOOST_CHECK_EQUAL(inv10.IsOversized(config, config.GetMaxBlockSize()), false);

    // test with max size message
    CMessageHeader sizemax(config.GetChainParams().NetMagic(), NetMsgType::INV, config.GetMaxProtocolRecvPayloadLength());
    BOOST_CHECK_EQUAL(sizemax.IsValid(config), true);
    BOOST_CHECK_EQUAL(sizemax.IsOversized(config, config.GetMaxBlockSize()), false);

    // test with (max size + 1) message
    CMessageHeader sizemaxplus(config.GetChainParams().NetMagic(), NetMsgType::INV, config.GetMaxProtocolRecvPayloadLength()+1);
    BOOST_CHECK_EQUAL(sizemaxplus.IsValid(config), false);
    BOOST_CHECK_EQUAL(sizemaxplus.IsOversized(config, config.GetMaxBlockSize()), true);

    // test with max size GETBLOCKTXN message
    uint64_t maxSizeGetBlockTxn { NetMsgType::GetMaxMessageLength(NetMsgType::GETBLOCKTXN, config, config.GetMaxBlockSize()) };
    CMessageHeader maxSizeGetBlockTxnHdr(config.GetChainParams().NetMagic(), NetMsgType::GETBLOCKTXN, maxSizeGetBlockTxn);
    BOOST_CHECK_EQUAL(maxSizeGetBlockTxnHdr.IsValid(config), true);
    BOOST_CHECK_EQUAL(maxSizeGetBlockTxnHdr.IsOversized(config, config.GetMaxBlockSize()), false);

    // test with (max size + 1) GETBLOCKTXN message
    CMessageHeader maxplusSizeGetBlockTxnHdr(config.GetChainParams().NetMagic(), NetMsgType::GETBLOCKTXN, maxSizeGetBlockTxn + 1);
    BOOST_CHECK_EQUAL(maxplusSizeGetBlockTxnHdr.IsValid(config), false);
    BOOST_CHECK_EQUAL(maxplusSizeGetBlockTxnHdr.IsOversized(config, config.GetMaxBlockSize()), true);
}

BOOST_AUTO_TEST_CASE(protocol_estimate_inv_elements)
{
    GlobalConfig config;
    config.SetDefaultBlockSizeParams(Params().GetDefaultBlockSizeParams());
    const CNetMsgMaker msgMaker(INIT_PROTO_VERSION);
    std::vector<CInv> vInv;
    uint32_t maxRecvPayloadLength = CInv::estimateMaxInvElements(config.GetMaxProtocolRecvPayloadLength());

    auto cnetMsg = CNetMessage(Params().NetMagic(), SER_NETWORK, INIT_PROTO_VERSION);
    for (uint32_t i = 0; i < maxRecvPayloadLength - 1; i++) {
        vInv.emplace_back(1, uint256());
    }

    // Send maxInvElements - 1.
    auto serializedInvMsg = msgMaker.Make(NetMsgType::INV, vInv);
    size_t nPayloadLength = serializedInvMsg.Size();
    CMessageHeader hdrLess(config.GetChainParams().NetMagic(), serializedInvMsg.Command().c_str(),
        nPayloadLength);
    BOOST_CHECK_EQUAL(hdrLess.IsOversized(config, config.GetMaxBlockSize()), false);

    // Send maxInvElements.
    vInv.emplace_back(1, uint256());
    serializedInvMsg = msgMaker.Make(NetMsgType::INV, vInv);
    nPayloadLength = serializedInvMsg.Size();
    CMessageHeader hdrEqual(config.GetChainParams().NetMagic(), serializedInvMsg.Command().c_str(),
        nPayloadLength);
    BOOST_CHECK_EQUAL(hdrEqual.IsOversized(config, config.GetMaxBlockSize()), false);

    // Send maxInvElements + 1.
    vInv.emplace_back(1, uint256());
    serializedInvMsg = msgMaker.Make(NetMsgType::INV, vInv);
    nPayloadLength = serializedInvMsg.Size();
    CMessageHeader hdrMore(config.GetChainParams().NetMagic(), serializedInvMsg.Command().c_str(),
        nPayloadLength);
    BOOST_CHECK_EQUAL(hdrMore.IsOversized(config, config.GetMaxBlockSize()), true);
}

BOOST_AUTO_TEST_CASE(protocol_msghdr_magic)
{
    CMessageHeader::MessageMagic wrongMessageMagic = {
            {0x05, 0x70, 0xEA, 0x12}};
    GlobalConfig config;
    config.SetDefaultBlockSizeParams(Params().GetDefaultBlockSizeParams());

    // simple test with invalid magic bytes
    CMessageHeader wrongMagic(wrongMessageMagic, NetMsgType::PING, 4);
    BOOST_CHECK_EQUAL(wrongMagic.IsValid(config), false);
    BOOST_CHECK_EQUAL(wrongMagic.IsOversized(config, config.GetMaxBlockSize()), false);
}

BOOST_AUTO_TEST_CASE(protocol_msghdr_command)
{
    GlobalConfig config;
    config.SetDefaultBlockSizeParams(Params().GetDefaultBlockSizeParams());

    // zero length command technically ok
    CMessageHeader zerochars(config.GetChainParams().NetMagic(), "", 4);
    BOOST_CHECK_EQUAL(zerochars.IsValid(config), true);
    BOOST_CHECK_EQUAL(zerochars.IsOversized(config, config.GetMaxBlockSize()), false);

    // command with length 12 ok
    CMessageHeader maxchars(config.GetChainParams().NetMagic(), "123456789012", 4);
    BOOST_CHECK_EQUAL(maxchars.IsValid(config), true);
    BOOST_CHECK_EQUAL(maxchars.IsOversized(config, config.GetMaxBlockSize()), false);

    // command with length 13 - constructor will only use the first 12
    CMessageHeader toomanychars(config.GetChainParams().NetMagic(), "1234567890123", 4);
    BOOST_CHECK_EQUAL(toomanychars.GetCommand(), "123456789012");

    // the command can not have non-zero bytes after the first zero byte
    // the constructor will ignore the extra bytes, so first check is ok
    CMessageHeader extrachars(config.GetChainParams().NetMagic(), "ERROR\0BY", 4);
    BOOST_CHECK_EQUAL(extrachars.IsValid(config), true);
    BOOST_CHECK_EQUAL(extrachars.IsOversized(config, config.GetMaxBlockSize()), false);
    // manually set the command to invalid value
    memcpy(extrachars.pchCommand, "ERROR\0BY", 8);
    BOOST_CHECK_EQUAL(extrachars.IsValid(config), false);
    BOOST_CHECK_EQUAL(extrachars.IsOversized(config, config.GetMaxBlockSize()), false);

}

BOOST_AUTO_TEST_SUITE_END()
