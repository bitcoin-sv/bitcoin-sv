// Copyright (c) 2026 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "test/test_bitcoin.h"
#include "config.h"
#include "fs.h"
#include "net/disk_backed_parser.h"
#include "net/net_message.h"
#include "net/msg_buffer.h"
#include "util.h"

#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace
{
    constexpr std::array<uint8_t, 4> net_message_disk_tests_magic_bytes { 0xda, 0xb5, 0xbf, 0xfa };
    constexpr int net_message_disk_tests_netmsg_type { SER_NETWORK };
    constexpr int net_message_disk_tests_netmsg_version { INIT_PROTO_VERSION };
}

BOOST_FIXTURE_TEST_SUITE(net_message_disk_tests, TestingSetup)

// Helper to create a test message header
static std::vector<uint8_t> net_message_disk_tests_create_message_header(
    const std::string& command,
    uint64_t payload_length)
{
    std::vector<uint8_t> header;

    // Magic bytes (4 bytes)
    header.insert(header.end(), net_message_disk_tests_magic_bytes.begin(), net_message_disk_tests_magic_bytes.end());

    // Command (12 bytes, null-padded)
    std::array<uint8_t, 12> cmd_bytes{};
    std::copy(command.begin(), command.end(), cmd_bytes.begin());
    header.insert(header.end(), cmd_bytes.begin(), cmd_bytes.end());

    // Payload length (4 bytes, little-endian)
    for(int i = 0; i < 4; ++i)
    {
        header.push_back(static_cast<uint8_t>((payload_length >> (i * 8)) & 0xFF));
    }

    // Checksum (4 bytes) - use dummy value for testing
    header.insert(header.end(), {0x5d, 0xf6, 0xe0, 0xe2});

    return header;
}

// Helper to generate test payload data
static std::vector<uint8_t> net_message_disk_tests_generate_payload(size_t size)
{
    std::vector<uint8_t> data(size);
    for(size_t i = 0; i < size; ++i)
    {
        data[i] = static_cast<uint8_t>((i * 17 + 23) % 256);
    }
    return data;
}

BOOST_AUTO_TEST_CASE(threshold_detection_small_message)
{
    const uint64_t max_recv_buffer = 1000;
    const size_t payload_size = 500; // Below threshold

    CMessageHeader::MessageMagic mm;
    std::copy(net_message_disk_tests_magic_bytes.begin(), net_message_disk_tests_magic_bytes.end(), mm.begin());

    CNetMessage msg { mm, net_message_disk_tests_netmsg_type, net_message_disk_tests_netmsg_version, max_recv_buffer };

    // Send header
    auto header = net_message_disk_tests_create_message_header("test", payload_size);
    msg.Read(GlobalConfig::GetConfig(), header.data(), header.size());

    // Send payload
    auto payload = net_message_disk_tests_generate_payload(payload_size);
    msg.Read(GlobalConfig::GetConfig(), payload.data(), payload.size());

    BOOST_CHECK(msg.Complete());
    BOOST_CHECK_EQUAL(msg.GetData().size(), payload_size);
}

BOOST_AUTO_TEST_CASE(threshold_detection_large_message)
{
    const uint64_t max_recv_buffer = 1000;
    const size_t payload_size = 2000; // Above threshold

    CMessageHeader::MessageMagic mm;
    std::copy(net_message_disk_tests_magic_bytes.begin(), net_message_disk_tests_magic_bytes.end(), mm.begin());

    CNetMessage msg { mm, net_message_disk_tests_netmsg_type, net_message_disk_tests_netmsg_version, max_recv_buffer };

    // Send header
    auto header = net_message_disk_tests_create_message_header("testlarge", payload_size);
    msg.Read(GlobalConfig::GetConfig(), header.data(), header.size());

    // Send payload in chunks
    auto payload = net_message_disk_tests_generate_payload(payload_size);
    size_t sent = 0;
    size_t chunk_size = 512;
    while(sent < payload_size)
    {
        size_t to_send = std::min(chunk_size, payload_size - sent);
        msg.Read(GlobalConfig::GetConfig(), payload.data() + sent, to_send);
        sent += to_send;
    }

    BOOST_CHECK(msg.Complete());
    BOOST_CHECK_EQUAL(msg.GetData().size(), payload_size);
}

BOOST_AUTO_TEST_CASE(data_integrity_disk_vs_memory)
{
    const uint64_t max_recv_buffer = 1000;
    const size_t small_payload_size = 500;  // Memory
    const size_t large_payload_size = 2000; // Disk

    CMessageHeader::MessageMagic mm;

    std::copy(net_message_disk_tests_magic_bytes.begin(), net_message_disk_tests_magic_bytes.end(), mm.begin());
    // Create two messages with same payload data
    auto small_payload = net_message_disk_tests_generate_payload(small_payload_size);
    auto large_payload = net_message_disk_tests_generate_payload(large_payload_size);

    // Small message (memory-backed)
    CNetMessage msg_memory { mm, net_message_disk_tests_netmsg_type, net_message_disk_tests_netmsg_version, max_recv_buffer };
    auto header_memory = net_message_disk_tests_create_message_header("small", small_payload_size);
    msg_memory.Read(GlobalConfig::GetConfig(), header_memory.data(), header_memory.size());
    msg_memory.Read(GlobalConfig::GetConfig(), small_payload.data(), small_payload_size);

    // Large message (disk-backed) with same pattern
    CNetMessage msg_disk { mm, net_message_disk_tests_netmsg_type, net_message_disk_tests_netmsg_version, max_recv_buffer };
    auto header_disk = net_message_disk_tests_create_message_header("large", large_payload_size);
    msg_disk.Read(GlobalConfig::GetConfig(), header_disk.data(), header_disk.size());
    msg_disk.Read(GlobalConfig::GetConfig(), large_payload.data(), large_payload_size);

    BOOST_CHECK(msg_memory.Complete());
    BOOST_CHECK(msg_disk.Complete());

    // Verify data can be read back correctly from both
    std::vector<uint8_t> read_memory(small_payload_size);
    std::vector<uint8_t> read_disk(large_payload_size);

    msg_memory.GetData().read(std::span<uint8_t>(read_memory));
    msg_disk.GetData().read(std::span<uint8_t>(read_disk));

    // Check first part matches (both use same generation pattern)
    BOOST_CHECK(std::equal(
        small_payload.begin(), small_payload.end(),
        read_memory.begin()
    ));
    BOOST_CHECK(std::equal(
        large_payload.begin(), large_payload.end(),
        read_disk.begin()
    ));
}

BOOST_AUTO_TEST_CASE(sequential_messages)
{
    const uint64_t max_recv_buffer = 1000;

    CMessageHeader::MessageMagic mm;
    std::copy(net_message_disk_tests_magic_bytes.begin(), net_message_disk_tests_magic_bytes.end(), mm.begin());

    // Create and complete first message
    {
        const size_t payload_size_1 = 1500;
        CNetMessage msg1 { mm, net_message_disk_tests_netmsg_type, net_message_disk_tests_netmsg_version, max_recv_buffer };

        auto header1 = net_message_disk_tests_create_message_header("msg1", payload_size_1);
        auto payload1 = net_message_disk_tests_generate_payload(payload_size_1);

        msg1.Read(GlobalConfig::GetConfig(), header1.data(), header1.size());
        msg1.Read(GlobalConfig::GetConfig(), payload1.data(), payload_size_1);

        BOOST_CHECK(msg1.Complete());
        BOOST_CHECK_EQUAL(msg1.GetData().size(), payload_size_1);
    } // msg1 destroyed, file should be cleaned up

    // Create second message
    {
        const size_t payload_size_2 = 1800;
        CNetMessage msg2 { mm, net_message_disk_tests_netmsg_type, net_message_disk_tests_netmsg_version, max_recv_buffer };

        auto header2 = net_message_disk_tests_create_message_header("msg2", payload_size_2);
        auto payload2 = net_message_disk_tests_generate_payload(payload_size_2);

        msg2.Read(GlobalConfig::GetConfig(), header2.data(), header2.size());
        msg2.Read(GlobalConfig::GetConfig(), payload2.data(), payload_size_2);

        BOOST_CHECK(msg2.Complete());
        BOOST_CHECK_EQUAL(msg2.GetData().size(), payload_size_2);
    } // msg2 destroyed, file should be cleaned up
}

BOOST_AUTO_TEST_CASE(partial_read_during_reception)
{
    const uint64_t max_recv_buffer = 1000;
    const size_t payload_size = 2000;

    CMessageHeader::MessageMagic mm;
    std::copy(net_message_disk_tests_magic_bytes.begin(), net_message_disk_tests_magic_bytes.end(), mm.begin());

    CNetMessage msg { mm, net_message_disk_tests_netmsg_type, net_message_disk_tests_netmsg_version, max_recv_buffer };

    auto header = net_message_disk_tests_create_message_header("partial", payload_size);
    auto payload = net_message_disk_tests_generate_payload(payload_size);

    msg.Read(GlobalConfig::GetConfig(), header.data(), header.size());

    // Send only half the payload
    size_t half = payload_size / 2;
    msg.Read(GlobalConfig::GetConfig(), payload.data(), half);

    BOOST_CHECK(!msg.Complete());

    // Send the rest
    msg.Read(GlobalConfig::GetConfig(), payload.data() + half, payload_size - half);

    BOOST_CHECK(msg.Complete());
    BOOST_CHECK_EQUAL(msg.GetData().size(), payload_size);
}

BOOST_AUTO_TEST_SUITE_END()
