// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include <double_spend/dscallback_msg.h>
#include <net/netaddress.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <streams.h>

#include <boost/test/unit_test.hpp>

namespace
{
    CScript MakeCallbackScript(const DSCallbackMsg& callback_msg)
    {
        std::vector<uint8_t> msgBytes {};
        CVectorWriter stream { SER_NETWORK, 0, msgBytes, 0 };
        stream << callback_msg;
        // Note we have to reverse the protocol id here to get it pushed big-endian
        return CScript() << OP_FALSE << OP_RETURN << 0x746e7364 << msgBytes;
    }

    bool operator==(const DSCallbackMsg& msg1, const DSCallbackMsg& msg2)
    {
        return msg1.GetVersionByte() == msg2.GetVersionByte() &&
               msg1.GetAddresses() == msg2.GetAddresses() &&
               msg1.GetInputs() == msg2.GetInputs();
    }
}

BOOST_AUTO_TEST_SUITE(dsattempt)

BOOST_AUTO_TEST_CASE(CallbackMsg)
{
    // Test creation of callback message from IPv4 address
    BOOST_CHECK_NO_THROW(
        std::string ip {"127.0.0.1"};
        DSCallbackMsg ipv4_callback(0x01, {ip}, {0,3});
        BOOST_CHECK_EQUAL(ipv4_callback.GetVersionByte(), 0x01);
        BOOST_CHECK_EQUAL(ipv4_callback.GetProtocolVersion(), 1U);

        auto addrs { ipv4_callback.GetAddresses() };
        BOOST_REQUIRE_EQUAL(addrs.size(), 1U);
        CNetAddr addr {};
        addr.SetRaw(NET_IPV4, addrs[0].data());
        BOOST_CHECK_EQUAL(addr.ToStringIP(), ip);
        BOOST_CHECK_EQUAL(DSCallbackMsg::IPAddrToString(addrs[0]), ip);

        auto inputs { ipv4_callback.GetInputs() };
        BOOST_REQUIRE_EQUAL(inputs.size(), 2U);
        BOOST_CHECK_EQUAL(inputs[0], 0U);
        BOOST_CHECK_EQUAL(inputs[1], 3U);
    );

    // Test creation of callback message from IPv6 address
    BOOST_CHECK_NO_THROW(
        std::string ip {"::1"};
        DSCallbackMsg ipv6_callback(0x81, {ip}, {0});
        BOOST_CHECK_EQUAL(ipv6_callback.GetVersionByte(), 0x81);
        BOOST_CHECK_EQUAL(ipv6_callback.GetProtocolVersion(), 1U);

        auto addrs { ipv6_callback.GetAddresses() };
        BOOST_REQUIRE_EQUAL(addrs.size(), 1U);
        CNetAddr addr {};
        addr.SetRaw(NET_IPV6, addrs[0].data());
        BOOST_CHECK_EQUAL(addr.ToStringIP(), ip);
        BOOST_CHECK_EQUAL(DSCallbackMsg::IPAddrToString(addrs[0]), ip);
    );

    // Test creation of callback message from mixed IP addresses
    BOOST_CHECK_THROW(DSCallbackMsg ips_callback(0x80, {"127.0.0.1", "::1"}, {0}), std::runtime_error);

    // Check callback message serialisation/deserialisation
    {
        DSCallbackMsg ipv4_callback { 0x01, {"127.0.0.1"}, {0} };
        CDataStream ss { SER_NETWORK, 0 };
        ss << ipv4_callback;
        DSCallbackMsg callback_msg_deserialised {};
        ss >> callback_msg_deserialised;
        BOOST_CHECK(callback_msg_deserialised == ipv4_callback);
    }
    {
        DSCallbackMsg ipv6_callback { 0x81, {"::1"}, {0,1} };
        CDataStream ss { SER_NETWORK, 0 };
        ss << ipv6_callback;
        DSCallbackMsg callback_msg_deserialised {};
        ss >> callback_msg_deserialised;
        BOOST_CHECK(callback_msg_deserialised == ipv6_callback);
    }
}

BOOST_AUTO_TEST_CASE(CallbackEnabledTransaction)
{
    DSCallbackMsg callback_msg { 0x01, {"127.0.0.1"}, {0} };

    // Create a txn with a callback output
    CMutableTransaction mtx {};
    mtx.vout.push_back(CTxOut{Amount{1}, CScript() << OP_TRUE});
    mtx.vout.push_back(CTxOut{Amount{0}, MakeCallbackScript(callback_msg)});
    CTransaction tx {mtx};

    // Check recognition of callback enabled transaction
    auto [ enabled, output ] = TxnHasDSNotificationOutput(tx);
    BOOST_CHECK(enabled);
    BOOST_CHECK_EQUAL(output, 1U);

    // Check extraction and parsing of callback message from output
    BOOST_CHECK_NO_THROW(
        const CScript& script { tx.vout[output].scriptPubKey };
        DSCallbackMsg callback_msg_from_script {script};
        BOOST_CHECK(callback_msg_from_script == callback_msg);
    );
}

BOOST_AUTO_TEST_CASE(CallbackMallformed)
{
    std::array<uint8_t, 4> ipbytes { 0x7F, 0x00, 0x00, 0x01 };

    // Check for missing version (empty message)
    BOOST_CHECK_THROW(
        DSCallbackMsg callback_msg_deserialised;
        CDataStream ss(SER_NETWORK, 0);
        ss >> callback_msg_deserialised;
    , std::runtime_error);

    // Missing IP address count
    BOOST_CHECK_THROW(
        DSCallbackMsg callback_msg_deserialised;
        CDataStream ss(SER_NETWORK, 0);
        ss << 0x01;
        ss >> callback_msg_deserialised;
    , std::runtime_error);

    // IP address count is 0
    BOOST_CHECK_THROW(
        CDataStream ss(SER_NETWORK, 0);
        ss << uint8_t(0x01) << VARINT(0) << VARINT(1) << VARINT(0);
        DSCallbackMsg callback_msg_deserialised;
        ss >> callback_msg_deserialised;
    , std::runtime_error);

    // Missing IP address
    BOOST_CHECK_THROW(
        CDataStream ss(SER_NETWORK, 0);
        ss << uint8_t(0x01) << VARINT(1) << VARINT(1) << VARINT(0);
        DSCallbackMsg callback_msg_deserialised;
        ss >> callback_msg_deserialised;
    , std::runtime_error);

    // Extra IP address
    BOOST_CHECK_THROW(
        CDataStream ss(SER_NETWORK, 0);
        ss << uint8_t(0x01) << VARINT(1) << FLATDATA(ipbytes) << FLATDATA(ipbytes) << VARINT(1) << VARINT(0);
        DSCallbackMsg callback_msg_deserialised;
        ss >> callback_msg_deserialised;
    , std::runtime_error);

    // Missing input from list
    BOOST_CHECK_THROW(
        CDataStream ss(SER_NETWORK, 0);
        ss << uint8_t(0x01) << VARINT(1) << FLATDATA(ipbytes) << VARINT(2) << VARINT(0);
        DSCallbackMsg callback_msg_deserialised;
        ss >> callback_msg_deserialised;
    , std::runtime_error);

    // Check for trailing data beyond the end of the message
    BOOST_CHECK_THROW(
        DSCallbackMsg callback_msg(0x01, {"127.0.0.1"}, {});
        CDataStream ss(SER_NETWORK, 0);
        ss << callback_msg;
        // Tack on an extra redundant byte to stream
        ss << 0x00;
        DSCallbackMsg callback_msg_deserialised;
        ss >> callback_msg_deserialised;
    , std::runtime_error);
}

BOOST_AUTO_TEST_SUITE_END()

