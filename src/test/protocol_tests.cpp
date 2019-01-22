// Copyright (c) 2019 The Bitcoin SV developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "config.h"
#include "netmessagemaker.h"
#include "protocol.h"

#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(protocol_tests, TestingSetup)

class OurConfig : public DummyConfig {
    uint64_t GetMaxBlockSize() const override { return DEFAULT_MAX_BLOCK_SIZE; }
};


BOOST_AUTO_TEST_CASE(protocol_msghdr_length)
{
    OurConfig config;

    // confirm that an incomplete message is not valid
    CMessageHeader hdr(config.GetChainParams().NetMagic());
    BOOST_CHECK_EQUAL(hdr.IsValidWithoutConfig(config.GetChainParams().NetMagic()),false);
    BOOST_CHECK_EQUAL(hdr.IsValid(config), false);
    BOOST_CHECK_EQUAL(hdr.IsOversized(config), true);

    // simple test CMessageHeader::IsValidWithoutConfig()
    CMessageHeader ping(config.GetChainParams().NetMagic(), NetMsgType::PING, 4);
    BOOST_CHECK_EQUAL(ping.IsValidWithoutConfig(config.GetChainParams().NetMagic()), true);
    BOOST_CHECK_EQUAL(ping.IsValid(config), true);
    BOOST_CHECK_EQUAL(ping.IsOversized(config), false);

    // zero length ok
    CMessageHeader zero(config.GetChainParams().NetMagic(), NetMsgType::PING, 0);
    BOOST_CHECK_EQUAL(zero.IsValidWithoutConfig(config.GetChainParams().NetMagic()), true);
    BOOST_CHECK_EQUAL(zero.IsValid(config), true);
    BOOST_CHECK_EQUAL(zero.IsOversized(config), false);

    // test with inv message with 10 tx
    CMessageHeader inv10(config.GetChainParams().NetMagic(), NetMsgType::INV, (1+10*(4+32)));
    BOOST_CHECK_EQUAL(inv10.IsValidWithoutConfig(config.GetChainParams().NetMagic()), true);
    BOOST_CHECK_EQUAL(inv10.IsValid(config), true);
    BOOST_CHECK_EQUAL(inv10.IsOversized(config), false);

    // test with max size message
    CMessageHeader sizemax(config.GetChainParams().NetMagic(), NetMsgType::INV, MAX_PROTOCOL_MESSAGE_LENGTH);
    BOOST_CHECK_EQUAL(sizemax.IsValidWithoutConfig(config.GetChainParams().NetMagic()), true);
    BOOST_CHECK_EQUAL(sizemax.IsValid(config), true);
    BOOST_CHECK_EQUAL(sizemax.IsOversized(config), false);

    // test with (max size + 1) message
    CMessageHeader sizemaxplus(config.GetChainParams().NetMagic(), NetMsgType::INV, MAX_PROTOCOL_MESSAGE_LENGTH+1);
    BOOST_CHECK_EQUAL(sizemaxplus.IsValidWithoutConfig(config.GetChainParams().NetMagic()), false);
    BOOST_CHECK_EQUAL(sizemaxplus.IsValid(config), false);
    BOOST_CHECK_EQUAL(sizemaxplus.IsOversized(config), true);

    // test with max inv size message
    CMessageHeader invmax(config.GetChainParams().NetMagic(), NetMsgType::INV, (3+MAX_INV_SZ*(4+32)));
    BOOST_CHECK_EQUAL(invmax.IsValidWithoutConfig(config.GetChainParams().NetMagic()), true);
    BOOST_CHECK_EQUAL(invmax.IsValid(config), true);
    BOOST_CHECK_EQUAL(invmax.IsOversized(config), false);
}

BOOST_AUTO_TEST_CASE(protocol_msghdr_magic)
{
    CMessageHeader::MessageMagic wrongMessageMagic = {
            {0x05, 0x70, 0xEA, 0x12}};
    OurConfig config;

    // simple test with invalid magic bytes
    CMessageHeader wrongMagic(wrongMessageMagic, NetMsgType::PING, 4);
    BOOST_CHECK_EQUAL(wrongMagic.IsValidWithoutConfig(config.GetChainParams().NetMagic()), false);
    BOOST_CHECK_EQUAL(wrongMagic.IsValid(config), false);
    BOOST_CHECK_EQUAL(wrongMagic.IsOversized(config), false);
}

BOOST_AUTO_TEST_CASE(protocol_msghdr_command)
{
    OurConfig config;

    // zero length command technically ok
    CMessageHeader zerochars(config.GetChainParams().NetMagic(), "", 4);
    BOOST_CHECK_EQUAL(zerochars.IsValidWithoutConfig(config.GetChainParams().NetMagic()), true);
    BOOST_CHECK_EQUAL(zerochars.IsValid(config), true);
    BOOST_CHECK_EQUAL(zerochars.IsOversized(config), false);

    // command with length 12 ok
    CMessageHeader maxchars(config.GetChainParams().NetMagic(), "123456789012", 4);
    BOOST_CHECK_EQUAL(maxchars.IsValidWithoutConfig(config.GetChainParams().NetMagic()), true);
    BOOST_CHECK_EQUAL(maxchars.IsValid(config), true);
    BOOST_CHECK_EQUAL(maxchars.IsOversized(config), false);

    // command with length 13 - constructor will only use the first 12
    CMessageHeader toomanychars(config.GetChainParams().NetMagic(), "1234567890123", 4);
    BOOST_CHECK_EQUAL(toomanychars.GetCommand(), "123456789012");

    // the command can not have non-zero bytes after the first zero byte
    // the constructor will ignore the extra bytes, so first check is ok
    CMessageHeader extrachars(config.GetChainParams().NetMagic(), "ERROR\0BY", 4);
    BOOST_CHECK_EQUAL(extrachars.IsValidWithoutConfig(config.GetChainParams().NetMagic()), true);
    BOOST_CHECK_EQUAL(extrachars.IsValid(config), true);
    BOOST_CHECK_EQUAL(extrachars.IsOversized(config), false);
    // manually set the command to invalid value
    memcpy(extrachars.pchCommand, "ERROR\0BY", 8);
    BOOST_CHECK_EQUAL(extrachars.IsValidWithoutConfig(config.GetChainParams().NetMagic()), false);
    BOOST_CHECK_EQUAL(extrachars.IsValid(config), false);
    BOOST_CHECK_EQUAL(extrachars.IsOversized(config), false);

}

BOOST_AUTO_TEST_SUITE_END()
