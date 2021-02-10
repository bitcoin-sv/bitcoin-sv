// Copyright (c) 2020 The Bitcoin SV developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>
#include <netmessagemaker.h>
#include <protocol.h>

#include <vector>

BOOST_AUTO_TEST_SUITE(NetMessageMaker);

BOOST_AUTO_TEST_CASE(Make)
{
    CNetMsgMaker msgMaker {0};
    std::vector<CInv> vToFetch {};

    // Unspecified payload type
    CSerializedNetMsg msg1 { msgMaker.Make(NetMsgType::GETDATA, vToFetch) };
    BOOST_CHECK(msg1.Command() == NetMsgType::GETDATA);
    BOOST_CHECK(msg1.GetPayloadType() == CSerializedNetMsg::PayloadType::UNKNOWN);

    // Specified payload type
    CSerializedNetMsg msg2 { msgMaker.Make(CSerializedNetMsg::PayloadType::BLOCK, NetMsgType::GETDATA, vToFetch) };
    BOOST_CHECK(msg2.Command() == NetMsgType::GETDATA);
    BOOST_CHECK(msg2.GetPayloadType() == CSerializedNetMsg::PayloadType::BLOCK);
}

BOOST_AUTO_TEST_SUITE_END();
