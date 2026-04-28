// Copyright (c) 2026 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "test/test_bitcoin.h"
#include "net/disk_backed_parser.h"
#include "fs.h"
#include "util.h"

#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <cstdint>
#include <span>
#include <vector>

BOOST_FIXTURE_TEST_SUITE(disk_backed_parser_tests, TestingSetup)

// Helper to generate test data
static std::vector<uint8_t> generate_disk_backed_parser_test_data(size_t size)
{
    std::vector<uint8_t> data(size);
    for(size_t i = 0; i < size; ++i)
    {
        data[i] = static_cast<uint8_t>(i % 256);
    }
    return data;
}

BOOST_AUTO_TEST_CASE(basic_write_read_cycle)
{
    const size_t payload_size = 1024;
    auto test_data = generate_disk_backed_parser_test_data(payload_size);

    disk_backed_parser parser { payload_size };

    // Write data incrementally in chunks
    size_t written = 0;
    size_t chunk_size = 256;
    while(written < payload_size)
    {
        size_t to_write = std::min(chunk_size, payload_size - written);
        std::span<const uint8_t> chunk { test_data.data() + written, to_write };
        auto [bytes_written, bytes_remaining] = parser(chunk);

        BOOST_CHECK_EQUAL(bytes_written, to_write);
        written += bytes_written;
        BOOST_CHECK_EQUAL(bytes_remaining, payload_size - written);
    }

    BOOST_CHECK_EQUAL(parser.size(), payload_size);
    BOOST_CHECK_EQUAL(parser.readable_size(), payload_size);

    // Read data back and verify
    std::vector<uint8_t> read_buffer(payload_size);
    size_t bytes_read = parser.read(0, read_buffer);
    BOOST_CHECK_EQUAL(bytes_read, payload_size);
    BOOST_CHECK(std::equal(test_data.begin(), test_data.end(), read_buffer.begin()));
}

BOOST_AUTO_TEST_CASE(large_message)
{
    const size_t payload_size = 10 * 1024 * 1024; // 10MB
    auto test_data = generate_disk_backed_parser_test_data(payload_size);

    disk_backed_parser parser { payload_size };

    // Write data in larger chunks
    size_t written = 0;
    size_t chunk_size = 64 * 1024;
    while(written < payload_size)
    {
        size_t to_write = std::min(chunk_size, payload_size - written);
        std::span<const uint8_t> chunk { test_data.data() + written, to_write };
        auto [bytes_written, bytes_remaining] = parser(chunk);

        BOOST_CHECK_EQUAL(bytes_written, to_write);
        written += bytes_written;
        BOOST_CHECK_EQUAL(bytes_remaining, payload_size - written);
    }

    BOOST_CHECK_EQUAL(parser.size(), payload_size);
    BOOST_CHECK_EQUAL(parser.readable_size(), payload_size);

    // Read data back and verify
    std::vector<uint8_t> read_buffer(payload_size);
    size_t bytes_read = parser.read(0, read_buffer);
    BOOST_CHECK_EQUAL(bytes_read, payload_size);
    BOOST_CHECK(std::equal(test_data.begin(), test_data.end(), read_buffer.begin()));
}

BOOST_AUTO_TEST_CASE(random_access_reads)
{
    const size_t payload_size = 8192;
    auto test_data = generate_disk_backed_parser_test_data(payload_size);

    disk_backed_parser parser { payload_size };

    // Write all data at once
    auto [bytes_written, bytes_remaining] = parser(test_data);
    BOOST_CHECK_EQUAL(bytes_written, payload_size);
    BOOST_CHECK_EQUAL(bytes_remaining, 0);

    // Read at various offsets
    std::vector<size_t> offsets = {0, 100, 1000, 4096, 7000};
    std::vector<size_t> sizes = {100, 200, 512, 1024, 500};

    for(size_t i = 0; i < offsets.size(); ++i)
    {
        std::vector<uint8_t> read_buffer(sizes[i]);
        size_t bytes_read = parser.read(offsets[i], read_buffer);

        BOOST_CHECK_EQUAL(bytes_read, sizes[i]);
        BOOST_CHECK(std::equal(
            test_data.begin() + offsets[i],
            test_data.begin() + offsets[i] + sizes[i],
            read_buffer.begin()
        ));
    }
}

BOOST_AUTO_TEST_CASE(filename_uniqueness)
{
    const size_t payload_size = 1024;

    // Create multiple parsers
    disk_backed_parser parser1 { payload_size };
    disk_backed_parser parser2 { payload_size };
    disk_backed_parser parser3 { payload_size };

    // Write some data to ensure files are created
    auto test_data = generate_disk_backed_parser_test_data(512);
    (void)parser1(test_data);
    (void)parser2(test_data);
    (void)parser3(test_data);

    // Files should exist (we can't directly check filenames from here,
    // but if they weren't unique, the writes would have conflicted)
    BOOST_CHECK_EQUAL(parser1.size(), 512);
    BOOST_CHECK_EQUAL(parser2.size(), 512);
    BOOST_CHECK_EQUAL(parser3.size(), 512);
}

BOOST_AUTO_TEST_CASE(file_deletion_on_destruction)
{
    const size_t payload_size = 1024;
    auto test_data = generate_disk_backed_parser_test_data(payload_size);

    auto CountFiles = [&]() -> size_t
    {
        size_t count = 0;
        if(fs::exists(g_p2p_recv_tmp_dir))
        {
            for(fs::directory_iterator it {g_p2p_recv_tmp_dir}; it != fs::directory_iterator(); ++it)
            {
                ++count;
            }
        }
        return count;
    };

    // Count files before
    size_t files_before = CountFiles();

    {
        disk_backed_parser parser { payload_size };
        (void)parser(test_data);
        BOOST_CHECK_EQUAL(parser.size(), payload_size);

        // Should have created a file for this parser
        size_t files_during = CountFiles();
        BOOST_CHECK_EQUAL(files_during, files_before + 1);
        // Parser goes out of scope here
    }

    // Count files after - should be the same as before
    size_t files_after = CountFiles();
    BOOST_CHECK_EQUAL(files_before, files_after);
}

BOOST_AUTO_TEST_CASE(write_after_complete)
{
    const size_t payload_size = 1024;
    auto test_data = generate_disk_backed_parser_test_data(payload_size);

    disk_backed_parser parser { payload_size };

    // Write all data
    auto [bytes_written, bytes_remaining] = parser(std::span<const uint8_t>(test_data));
    BOOST_CHECK_EQUAL(bytes_written, payload_size);
    BOOST_CHECK_EQUAL(bytes_remaining, 0);

    // Try to write more data - should return (0, 0)
    std::vector<uint8_t> extra_data(100, 0xFF);
    auto [extra_written, extra_remaining] = parser(std::span<const uint8_t>(extra_data));
    BOOST_CHECK_EQUAL(extra_written, 0);
    BOOST_CHECK_EQUAL(extra_remaining, 0);

    // Size should remain unchanged
    BOOST_CHECK_EQUAL(parser.size(), payload_size);
}

BOOST_AUTO_TEST_CASE(move_assignment_transfers_state)
{
    const size_t payload_size = 1024;
    auto test_data = generate_disk_backed_parser_test_data(payload_size);

    // Create two parsers, one will be destroyed later
    disk_backed_parser parser2 { payload_size };
    {
        disk_backed_parser parser1 { payload_size };

        // Fully populate parser1
        (void)parser1(std::span<const uint8_t>(test_data));
        BOOST_CHECK_EQUAL(parser1.size(), payload_size);

        // Move-assign parser1 into parser2
        parser2 = std::move(parser1);

        // The moved-to parser should own the data
        BOOST_CHECK_EQUAL(parser2.size(), payload_size);
        BOOST_CHECK_EQUAL(parser2.readable_size(), payload_size);

        // Destroy parser1 (destructor must not delete the file)
    }

    // Data should be readable from the moved-to parser
    std::vector<uint8_t> buf(payload_size);
    BOOST_CHECK_EQUAL(parser2.read(0, buf), payload_size);
    BOOST_CHECK(std::equal(test_data.begin(), test_data.end(), buf.begin()));
}

BOOST_AUTO_TEST_CASE(test_error_paths)
{
    // Constructor: payload length exceeding streamsize::max() should throw
    {
        const uint64_t overflow_size =
            static_cast<uint64_t>(std::numeric_limits<std::streamsize>::max()) + 1;
        BOOST_CHECK_THROW(disk_backed_parser { overflow_size }, std::runtime_error);
    }

    // read(): empty span should return 0 without touching the file
    {
        const size_t payload_size = 1024;
        auto test_data = generate_disk_backed_parser_test_data(payload_size);
        disk_backed_parser parser { payload_size };
        (void)parser(std::span<const uint8_t>(test_data));

        std::vector<uint8_t> empty_buf;
        BOOST_CHECK_EQUAL(parser.read(0, std::span<uint8_t>(empty_buf)), 0);
    }

    // read(): read_pos at or beyond the written boundary should throw
    {
        const size_t payload_size = 1024;
        auto test_data = generate_disk_backed_parser_test_data(payload_size);
        disk_backed_parser parser { payload_size };
        (void)parser(std::span<const uint8_t>(test_data));

        std::vector<uint8_t> buf(100);
        BOOST_CHECK_THROW((void)parser.read(payload_size, buf), std::runtime_error);
        BOOST_CHECK_THROW((void)parser.read(payload_size + 100, buf), std::runtime_error);
    }

    // read(): should throw when the backing file has been deleted before the first read
    {
        const size_t payload_size = 1024;
        auto test_data = generate_disk_backed_parser_test_data(payload_size);
        disk_backed_parser parser { payload_size };
        // Completing the write closes the write stream; read() will need to reopen it
        (void)parser(std::span<const uint8_t>(test_data));

        // Remove the backing file to simulate unexpected loss
        fs::remove_all(g_p2p_recv_tmp_dir);
        fs::create_directories(g_p2p_recv_tmp_dir);

        // read() attempts to reopen the (now missing) file and should throw
        std::vector<uint8_t> buf(payload_size);
        BOOST_CHECK_THROW((void)parser.read(0, buf), std::runtime_error);
    }
}

BOOST_AUTO_TEST_SUITE_END()
