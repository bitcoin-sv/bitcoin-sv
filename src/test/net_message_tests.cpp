// Copyright (c) 2023 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE
#include "net/net_message.h"

#include <boost/test/tools/old/interface.hpp>
#include <boost/test/unit_test.hpp>

#include <array>
#include <cstdint>

#include "config.h"
#include "net/p2p_msg_lengths.h"

using namespace std;
using namespace bsv;
    
static constexpr array<uint8_t, 4> magic_bytes{0xda, 0xb5, 0xbf, 0xfa};
constexpr int type{1};
constexpr int version{2};

BOOST_AUTO_TEST_SUITE(net_msg_tests)

BOOST_AUTO_TEST_CASE(read_0_bytes)
{
    CMessageHeader::MessageMagic mm;
    CNetMessage msg{mm, type, version};

    std::vector<uint8_t> ip;
    const auto bytes_read = msg.Read(GlobalConfig::GetConfig(), ip.data(), ip.size());
    BOOST_CHECK_EQUAL(0, bytes_read);
    BOOST_CHECK_EQUAL(false, msg.Complete());
}

BOOST_AUTO_TEST_CASE(read_header_only_msg)
{
    CMessageHeader::MessageMagic mm;
    CNetMessage msg{mm, type, version};

    const string com{"verack"};
    array<uint8_t, 12> command{};
    copy(com.cbegin(), com.cend(), command.begin());

    const array<uint8_t, 4> length{0x0, 0x0, 0x0, 0x0};
    const array<uint8_t, 4> checksum{0x1, 0x2, 0x3, 0x4};

    std::vector<uint8_t> ip{magic_bytes.cbegin(), magic_bytes.cend()};
    ip.insert(ip.end(), command.cbegin(), command.cend());
    ip.insert(ip.end(), length.cbegin(), length.cend());
    ip.insert(ip.end(), checksum.cbegin(), checksum.cend());

    // start of next msg
    ip.insert(ip.end(), magic_bytes.cbegin(), magic_bytes.cend());

    // Write the header
    auto bytes_read = msg.Read(GlobalConfig::GetConfig(), ip.data(), ip.size());
    BOOST_CHECK_EQUAL(24, bytes_read);
    BOOST_CHECK_EQUAL(true, msg.Complete());

    // Check the header has been read into CMessageHeader
    const CMessageHeader& hdr = msg.GetHeader();
    BOOST_CHECK_EQUAL("verack", hdr.GetCommand());
    BOOST_CHECK_EQUAL(0, hdr.GetPayloadLength());

    // Write the payload
    bytes_read = msg.Read(GlobalConfig::GetConfig(), 
                          ip.data() + bytes_read,
                          ip.size() - bytes_read);
    BOOST_CHECK_EQUAL(0, bytes_read);
    BOOST_CHECK_EQUAL(true, msg.Complete());
}

BOOST_AUTO_TEST_CASE(read_ping_msg)
{
    CMessageHeader::MessageMagic mm;
    CNetMessage msg{mm, type, version};

    const string com{"ping"};
    array<uint8_t, 12> command{};
    copy(com.cbegin(), com.cend(), command.begin());

    const array<uint8_t, 4> length{0x8, 0x0, 0x0, 0x0};
    const array<uint8_t, 4> checksum{0x1, 0x2, 0x3, 0x4};
    const array<uint8_t, 8> random_nonce{0x2a, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0};

    std::vector<uint8_t> ip{magic_bytes.cbegin(), magic_bytes.cend()};
    ip.insert(ip.end(), command.cbegin(), command.cend());
    ip.insert(ip.end(), length.cbegin(), length.cend());
    ip.insert(ip.end(), checksum.cbegin(), checksum.cend());
    ip.insert(ip.end(), random_nonce.cbegin(), random_nonce.cend());

    // start of next msg
    ip.insert(ip.end(), magic_bytes.cbegin(), magic_bytes.cend());

    // Write the header
    auto bytes_read = msg.Read(GlobalConfig::GetConfig(), ip.data(), ip.size());
    BOOST_CHECK_EQUAL(24, bytes_read);
    BOOST_CHECK_EQUAL(false, msg.Complete());

    // Check the header has been read into CMessageHeader
    const CMessageHeader& hdr = msg.GetHeader();
    BOOST_CHECK_EQUAL("ping", hdr.GetCommand());
    BOOST_CHECK_EQUAL(8, hdr.GetPayloadLength());

    // Write the payload
    bytes_read = msg.Read(GlobalConfig::GetConfig(), ip.data() + bytes_read,
                          ip.size() - bytes_read);
    BOOST_CHECK_EQUAL(8, bytes_read);
    BOOST_CHECK_EQUAL(true, msg.Complete());

    // Read payload
    msg_buffer& msg_buff = msg.GetData();
    uint64_t nonce;
    msg_buff >> nonce; 
    BOOST_CHECK_EQUAL(42, nonce);
}

BOOST_AUTO_TEST_CASE(read_extmsg_msg)
{
    CMessageHeader::MessageMagic mm;
    CNetMessage msg{mm, type, version};

    const string com{"extmsg"};
    array<uint8_t, 12> command{};
    copy(com.cbegin(), com.cend(), command.begin());

    const array<uint8_t, 4> length{0xff, 0xff, 0xff, 0xff};
    const array<uint8_t, 4> checksum{0x1, 0x2, 0x3, 0x4};
    
    const string ext_com{"ping"};
    array<uint8_t, 12> ext_command{};
    copy(ext_com.cbegin(), ext_com.cend(), ext_command.begin());
    const array<uint8_t, 8> ext_length{0x8, 0, 0, 0, 0, 0, 0, 0};
    const array<uint8_t, 8> random_nonce{0x2a, 0, 0, 0, 0, 0, 0, 0};

    std::vector<uint8_t> ip{magic_bytes.cbegin(), magic_bytes.cend()};
    ip.insert(ip.end(), command.cbegin(), command.cend());
    ip.insert(ip.end(), length.cbegin(), length.cend());
    ip.insert(ip.end(), checksum.cbegin(), checksum.cend());
    ip.insert(ip.end(), ext_command.cbegin(), ext_command.cend());
    ip.insert(ip.end(), ext_length.cbegin(), ext_length.cend());
    ip.insert(ip.end(), random_nonce.cbegin(), random_nonce.cend());

    // start of next msg
    ip.insert(ip.end(), magic_bytes.cbegin(), magic_bytes.cend());

    const auto& config{GlobalConfig::GetConfig()};
    // Read the standard header
    auto bytes_read = msg.Read(config, ip.data(), ip.size());
    BOOST_CHECK_EQUAL(bsv::msg_header_len, bytes_read);
    BOOST_CHECK_EQUAL(false, msg.Complete());
   
    // Read the extended header
    bytes_read += msg.Read(config, ip.data() + bytes_read, ip.size() - bytes_read);
    BOOST_CHECK_EQUAL(bsv::ext_msg_header_len, bytes_read);
    BOOST_CHECK_EQUAL(false, msg.Complete());

    // Check the header has been read into CMessageHeader
    const CMessageHeader& hdr = msg.GetHeader();
    BOOST_CHECK_EQUAL("ping", hdr.GetCommand());
    BOOST_CHECK_EQUAL(0x8, hdr.GetPayloadLength());

    // Read the payload
    bytes_read = msg.Read(config, 
                          ip.data() + bytes_read,
                          ip.size() - bytes_read);
    BOOST_CHECK_EQUAL(8, bytes_read);
    BOOST_CHECK_EQUAL(true, msg.Complete());

    // Read payload
    msg_buffer& msg_buff = msg.GetData();
    uint64_t nonce;
    msg_buff >> nonce; 
    BOOST_CHECK_EQUAL(42, nonce);
}

BOOST_AUTO_TEST_CASE(read_block_msg)
{
    CMessageHeader::MessageMagic mm;
    CNetMessage msg{mm, type, version};

    const string com{"block"};
    array<uint8_t, 12> command{};
    copy(com.cbegin(), com.cend(), command.begin());

    const array<uint8_t, 4> checksum{0x1, 0x2, 0x3, 0x4};

    constexpr auto block_header_len{80};
    constexpr int nTx_len{1};
    
    array<uint8_t, nTx_len + block_header_len> block_header{};
    const int64_t nTx{1};
    block_header[0] = 1;
    block_header[68] = 2;
    block_header[72] = 3;
    block_header[76] = 4;
    block_header[block_header.size() - 1] = nTx;

    constexpr int n_ips_len{1};
    constexpr int n_ops_len{1};
    const array<uint8_t, version_len + n_ips_len + n_ops_len + locktime_len> tx{};
    const auto payload_len{block_header.size() + tx.size()};
    const array<uint8_t, 4> length{payload_len, 0x0, 0x0, 0x0};

    std::vector<uint8_t> ip{magic_bytes.cbegin(), magic_bytes.cend()};
    ip.insert(ip.end(), command.cbegin(), command.cend());
    ip.insert(ip.end(), length.cbegin(), length.cend());
    ip.insert(ip.end(), checksum.cbegin(), checksum.cend());
    ip.insert(ip.end(), block_header.cbegin(), block_header.cend());
    ip.insert(ip.end(), tx.cbegin(), tx.cend());

    // start of next msg
    ip.insert(ip.end(), magic_bytes.cbegin(), magic_bytes.cend());

    SelectParams(CBaseChainParams::MAIN);
    ConfigInit& config = GlobalConfig::GetModifiableGlobalConfig();
    config.SetDefaultBlockSizeParams(Params().GetDefaultBlockSizeParams());

    // Write header
    auto bytes_read = msg.Read(config, ip.data(), ip.size());
    BOOST_CHECK_EQUAL(24, bytes_read);
    BOOST_CHECK_EQUAL(false, msg.Complete()); // header is read but not the payload
    
    // Check the header has been read into CMessageHeader
    const CMessageHeader& hdr = msg.GetHeader();
    BOOST_CHECK_EQUAL("block", hdr.GetCommand());
    BOOST_CHECK_EQUAL(payload_len, hdr.GetPayloadLength());
    
    // Write payload
    bytes_read = msg.Read(config, ip.data() + bytes_read, ip.size() - bytes_read);
    BOOST_CHECK_EQUAL(payload_len, bytes_read);
    BOOST_CHECK_EQUAL(true, msg.Complete());
   
    // Read the payload
    msg_buffer& msg_buff = msg.GetData();
    CBlock block;
    msg_buff >> block; 
    const CBlockHeader actual_block_header = block.GetBlockHeader();
    CBlockHeader expected_block_header;
    expected_block_header.nVersion = 1;
    expected_block_header.nTime = 2;
    expected_block_header.nBits = 3;
    expected_block_header.nNonce = 4;
    BOOST_CHECK_EQUAL(expected_block_header, actual_block_header);
}

BOOST_AUTO_TEST_CASE(block_msg_with_superflous_data)
{
    SelectParams(CBaseChainParams::MAIN);
    GlobalConfig config;
    config.SetDefaultBlockSizeParams(Params().GetDefaultBlockSizeParams());

    CMessageHeader::MessageMagic mm;
    CNetMessage msg{mm, type, version};

    const string com{"block"};
    array<uint8_t, 12> command{};
    copy(com.cbegin(), com.cend(), command.begin());

    const array<uint8_t, 4> length{82, 0, 0, 0}; // <- length mismatch
    const array<uint8_t, 4> checksum{1, 2, 3, 4};
    const array<uint8_t, 80> block_header{};

    std::vector<uint8_t> ip{magic_bytes.cbegin(), magic_bytes.cend()};
    ip.insert(ip.end(), command.cbegin(), command.cend());
    ip.insert(ip.end(), length.cbegin(), length.cend());
    ip.insert(ip.end(), checksum.cbegin(), checksum.cend());
    ip.insert(ip.end(), block_header.cbegin(), block_header.cend());
    ip.push_back(0); // n txs
    ip.push_back(2); // extra byte to make msg len 82 

    // Write the header
    auto bytes_read = msg.Read(config, ip.data(), ip.size());
    BOOST_CHECK_EQUAL(24, bytes_read);
    BOOST_CHECK_EQUAL(false, msg.Complete());

    // Check the header has been read into CMessageHeader
    const CMessageHeader& hdr = msg.GetHeader();
    BOOST_CHECK_EQUAL("block", hdr.GetCommand());
    BOOST_CHECK_EQUAL(82, hdr.GetPayloadLength());

    // Write the payload
    bytes_read = msg.Read(config, ip.data() + bytes_read,
                          ip.size() - bytes_read);
    BOOST_CHECK_EQUAL(82, bytes_read);
    BOOST_CHECK_EQUAL(true, msg.Complete());
}

BOOST_AUTO_TEST_SUITE_END()


