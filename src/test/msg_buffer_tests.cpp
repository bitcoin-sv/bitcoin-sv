// Copyright (c) 2022 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#include <boost/test/tools/old/interface.hpp>
#include <boost/test/unit_test.hpp>

#include "net/msg_buffer.h"

#include <array>
#include <boost/test/unit_test_suite.hpp>
#include <cstdint>
#include <mutex>
#include <numeric>
#include <string>
#include <type_traits>
#include <vector>

#include "protocol.h"
#include "net/net_message.h"
#include "net/p2p_msg_lengths.h"

using namespace std;
using namespace bsv;
    
constexpr auto type{1};
constexpr auto version{2};

static auto make_msg_header{[](const string& cmd)
{
    assert(cmd.size() <= cmd_len);

    vector<uint8_t> v{0xda, 0xb5, 0xbf, 0xfa};
    array<uint8_t, 12> a{};
    copy(cmd.cbegin(), cmd.cend(), a.begin());
    v.insert(v.end(), a.begin(), a.end());
    array<uint8_t, 4> len{0x0, 0x0, 0x0, 0x0};
    v.insert(v.end(), len.cbegin(), len.cend());
    array<uint8_t, 4> checksum{0x1, 0x2, 0x3, 0x4};
    v.insert(v.end(), checksum.cbegin(), checksum.cend());
    return v;
}};

static std::vector<uint8_t> block_msg_payload{[]
{
    std::vector<uint8_t> v;

    // block_header
    v.insert(v.end(), version_len, 1);  // version
    v.insert(v.end(), 32, 2);           // hash(prev_block)
    v.insert(v.end(), 32, 3);           // hash(merkle root)
    v.insert(v.end(), 4, 4);            // timestamp
    v.insert(v.end(), 4, 5);            // target
    v.insert(v.end(), 4, 6);            // nonce

    // tx count
    v.insert(v.end(), 1, 2);
    
    // tx 1
    v.insert(v.end(), version_len, 8);   // tx version
    v.push_back(1);                       // 1 input 
    v.insert(v.end(), outpoint_len, 9); // tx outpoint 
    v.push_back(1);                       // script length
    v.push_back(0x6a);                    // script (op_return)
    v.insert(v.end(), seq_len, 10);       // sequence
    v.push_back(1);                       // number of outputs
    v.insert(v.end(), value_len, 11);     // value
    v.push_back(1);                       // script length
    v.push_back(0x6a);                    // script (op_return)
    v.insert(v.end(), locktime_len, 12); // lock time

    // tx 2
    v.insert(v.end(), version_len, 13);   // tx version
    v.push_back(1);                       // 1 input 
    v.insert(v.end(), outpoint_len, 14); // tx outpoint 
    v.push_back(1);                       // script length
    v.push_back(0x6a);                    // script (op_return)
    v.insert(v.end(), seq_len, 15);       // sequence
    v.push_back(1);                       // number of outputs
    v.insert(v.end(), value_len, 16);     // value
    v.push_back(1);                       // script length
    v.push_back(0x6a);                    // script (op_return)
    v.insert(v.end(), locktime_len, 17); // lock time

    return v;
}()};

BOOST_AUTO_TEST_SUITE(msg_buffer_tests)

BOOST_AUTO_TEST_CASE(write_read_happy_case)
{
    msg_buffer buff{type, version};
   
    constexpr auto n{10};
    vector<uint8_t> ip1(n);
    std::iota(ip1.begin(), ip1.end(), 0);
    buff.write(span{ip1.data(), ip1.size()});
    BOOST_CHECK_EQUAL(n, buff.size());

    vector<uint8_t> op1(ip1.size());
    buff.read(span{op1.data(), op1.size()});
    BOOST_CHECK_EQUAL_COLLECTIONS(ip1.cbegin(),
                                  ip1.cend(),
                                  op1.cbegin(),
                                  op1.cend());
    BOOST_CHECK_EQUAL(0, buff.size());
   
    buff.command("default");
    constexpr auto payload_len{42};
    buff.payload_len(payload_len);

    vector<uint8_t> ip2(n);
    std::iota(ip2.begin(), ip2.end(), 100);
    buff.write(span{ip2.data(), ip2.size()});
    BOOST_CHECK_EQUAL(n, buff.size());
    
    vector<uint8_t> op2(ip2.size());
    buff.read(span{op2.data(), op2.size()});
    BOOST_CHECK_EQUAL_COLLECTIONS(ip2.cbegin(),
                                  ip2.cend(),
                                  op2.cbegin(),
                                  op2.cend());
}

BOOST_AUTO_TEST_CASE(write_read_past_the_end_of_header)
{
    msg_buffer buff{type, version};
   
    constexpr auto n{10};
    vector<uint8_t> ip(n);
    std::iota(ip.begin(), ip.end(), 0);
    buff.write(span{ip.data(), ip.size()});
    BOOST_CHECK_EQUAL(n, buff.size());

    vector<uint8_t> op(ip.size() + 1);
    try
    {
        buff.read(span{op.data(), op.size()});
    }
    catch(const std::ios_base::failure& e)
    {
        const string expected{"msg_buffer::read(): end of data: iostream error"};
        BOOST_CHECK_EQUAL(expected, e.what());
    }
}

BOOST_AUTO_TEST_CASE(write_read_past_the_end_of_payload)
{
    msg_buffer buff{type, version};
   
    constexpr auto n{10};
    vector<uint8_t> ip1(n);
    std::iota(ip1.begin(), ip1.end(), 0);
    buff.write(span{ip1.data(), ip1.size()});
    BOOST_CHECK_EQUAL(n, buff.size());

    vector<uint8_t> op1(ip1.size());
    buff.read(span{op1.data(), op1.size()});
    BOOST_CHECK_EQUAL_COLLECTIONS(ip1.cbegin(),
                                  ip1.cend(),
                                  op1.cbegin(),
                                  op1.cend());
    BOOST_CHECK_EQUAL(0, buff.size());
    
    buff.command("default");
    constexpr auto payload_len{42};
    buff.payload_len(payload_len);

    vector<uint8_t> ip2(n);
    std::iota(ip2.begin(), ip2.end(), 100);
    buff.write(span{ip2.data(), ip2.size()});
    BOOST_CHECK_EQUAL(n, buff.size());
    
    vector<uint8_t> op2(ip2.size() + 1);
    try
    {
        buff.read(span{op1.data(), op1.size()});
    }
    catch(const std::ios_base::failure& e)
    {
        const string expected{"msg_buffer::read(): end of data: iostream error"};
        BOOST_CHECK_EQUAL(expected, e.what());
    }
}

BOOST_AUTO_TEST_CASE(write_read_std_header)
{
    msg_buffer buff{type, version};
    
    vector<uint8_t> ip{0xda, 0xb5, 0xbf, 0xfa};
    const string cmd{"verack"};
    array<uint8_t, 12> a{};
    copy(cmd.cbegin(), cmd.cend(), a.begin());
    ip.insert(ip.end(), a.begin(), a.end());
    array<uint8_t, 4> len{0x8, 0x0, 0x0, 0x0};
    ip.insert(ip.end(), len.cbegin(), len.cend());
    array<uint8_t, 4> checksum{0x1, 0x2, 0x3, 0x4};
    ip.insert(ip.end(), checksum.cbegin(), checksum.cend());

    buff.write(span{ip.data(), ip.size()});
    BOOST_CHECK_EQUAL(bsv::msg_header_len, buff.size());

    vector<uint8_t> op(ip.size());
    buff.read(span{op.data(), ip.size()});
    BOOST_CHECK_EQUAL_COLLECTIONS(ip.cbegin(),
                                  ip.cend(),
                                  op.cbegin(),
                                  op.cend());
}

BOOST_AUTO_TEST_CASE(write_read_block_msg)
{
    msg_buffer buff{type, version};
    const auto msg_header{make_msg_header("block")};
    buff.write(std::span{msg_header.data(), msg_header.size()});

    vector<uint8_t> header(msg_header_len);
    buff.read(span{header.data(), header.size()});
    BOOST_CHECK_EQUAL(0, buff.size());

    buff.command("default");
    buff.payload_len(block_msg_payload.size());

    buff.write(span(block_msg_payload.data(),
               block_msg_payload.size()));
    BOOST_CHECK_EQUAL(block_msg_payload.size(), buff.size());
}

BOOST_AUTO_TEST_CASE(read_null_payload)
{
    msg_buffer buff{type, version};
    const auto msg_header{make_msg_header("version")};

    buff.write(std::span{msg_header.data(), msg_header.size()});
    buff.payload_len(0);

    vector<uint8_t> v(msg_header_len + 1);
    buff.read(span{v.data(), msg_header_len});
    BOOST_CHECK_EQUAL(msg_header_len, buff.size());
}

BOOST_AUTO_TEST_CASE(read_too_much)
{
    msg_buffer buff{type, version};
    
    vector<uint8_t> header{0xda, 0xb5, 0xbf, 0xfa};
    array<uint8_t, 12> a{};
    const string cmd{"ping"};
    copy(cmd.cbegin(), cmd.cend(), a.begin());
    header.insert(header.end(), a.begin(), a.end());
    constexpr auto payload_len{1};
    array<uint8_t, 4> len{payload_len, 0x0, 0x0, 0x0};
    header.insert(header.end(), len.cbegin(), len.cend());
    array<uint8_t, 4> checksum{0x1, 0x2, 0x3, 0x4};
    header.insert(header.end(), checksum.cbegin(), checksum.cend());
    buff.write(std::span{header.data(), header.size()});
    buff.payload_len(payload_len); // <-too short ping payload should be 8 bytes

    vector<uint8_t> payload(payload_len);
    buff.write(std::span{payload.data(), payload.size()});

    vector<uint8_t> out(header.size() + payload.size() + 1);
    try 
    {
        buff.read(span{out.data(), out.size()});
        BOOST_FAIL("Expected runtime_error");
    }
    catch(const std::ios_base::failure& e)
    {   
        const string actual{e.what()};
        const string expected{"msg_buffer::read(): end of data: iostream error"};
        BOOST_CHECK_EQUAL(expected, actual);
    }
}

BOOST_AUTO_TEST_SUITE_END()

