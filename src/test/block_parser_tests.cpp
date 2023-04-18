// Copyright (c) 2023 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE
#include <boost/test/unit_test.hpp>

#include "net/block_parser.h"
#include "net/msg_parser.h"
#include "net/msg_parser_buffer.h"
#include "net/p2p_msg_lengths.h"

using namespace std;
using namespace bsv;

const std::vector<uint8_t> block_msg{[]
{
    vector<uint8_t> v;
    v.insert(v.end(), version_len, 1); // version
    v.insert(v.end(), 32, 2);          // hash(prev_block)
    v.insert(v.end(), 32, 3);          // hash(merkle root)
    v.insert(v.end(), 4, 4);           // timestamp
    v.insert(v.end(), 4, 5);           // target
    v.insert(v.end(), 4, 6);           // nonce

    v.insert(v.end(), 1, 1);              // tx count

    v.insert(v.end(), version_len, 7);    // tx version
    v.push_back(1);                       // 1 input 
    
    v.insert(v.end(), outpoint_len, 8);   // tx outpoint 
    v.push_back(1);                       // script length
    v.push_back(0x6a);                    // script (op_return)
    v.insert(v.end(), seq_len, 9);        // sequence

    v.push_back(1);                       // number of outputs
    v.insert(v.end(), value_len, 10);      // value
    v.push_back(1);                       // script length
    v.push_back(0x6a);                    // script (op_return)

    // locktime
    v.insert(v.end(), locktime_len, 11);  // lock time

    return v;
}()};

BOOST_AUTO_TEST_SUITE(block_parser_tests)

BOOST_AUTO_TEST_CASE(parse_all)
{
    constexpr size_t block_header_len{80};
    {
        // size(block_msg) < block_header_len
        block_parser parser;
        std::span s{block_msg.data(), block_header_len - 1};
        const auto [bytes_read, bytes_reqd] = parser(s);
        BOOST_CHECK_EQUAL(block_header_len - 1, bytes_read);
        BOOST_CHECK_EQUAL(1, bytes_reqd);
        
        BOOST_CHECK_EQUAL(block_header_len - 1, parser.size());
    }

    {
        // size(block_msg) == block_header_len
        block_parser parser;
        std::span s{block_msg.data(), block_header_len};
        const auto [bytes_read, bytes_reqd] = parser(s);
        BOOST_CHECK_EQUAL(block_header_len, bytes_read);
        BOOST_CHECK_EQUAL(1, bytes_reqd);
        BOOST_CHECK_EQUAL(80, parser.size());
    }

    {
        // size(block_msg) > block_header_len
        block_parser parser;
        std::span s{block_msg.data(), block_msg.size()};
        const auto [bytes_read, bytes_reqd] = parser(s);
        BOOST_CHECK_EQUAL(block_msg.size(), bytes_read);
        BOOST_CHECK_EQUAL(0, bytes_reqd);
        BOOST_CHECK_EQUAL(block_msg.size(), parser.size());
    }
}

BOOST_AUTO_TEST_CASE(parse_as_reqd)
{
    block_parser parser;
    size_t total_bytes_read{};
    size_t offset{};
    size_t n{1};
    size_t passes{};
    while(total_bytes_read < block_msg.size())
    {
        span s{block_msg.data() + offset, n};
        const auto [bytes_read, bytes_reqd] = parser(s);
        ++passes;
        if(bytes_read)
        {
            total_bytes_read += bytes_read;
            offset += bytes_read;
            if(bytes_reqd)
                n += bytes_reqd - bytes_read;
        }
        else
        {
            n = bytes_reqd; 
        }
    }
    BOOST_CHECK_EQUAL(block_msg.size(), total_bytes_read);
    BOOST_CHECK_EQUAL(11, passes);
    BOOST_CHECK_EQUAL(block_msg.size(), parser.size());
}

BOOST_AUTO_TEST_CASE(parse_byte_by_byte)
{
    msg_parser_buffer parser{make_unique<msg_parser>(block_parser{})};

    for(size_t i{}; i < block_msg.size(); ++i)
    {
        std::span s{block_msg.data() + i, 1};
        parser(s);
    }

    BOOST_CHECK_EQUAL(block_msg.size(), parser.size());
}

BOOST_AUTO_TEST_CASE(read_all)
{
    block_parser parser;
    std::span s{block_msg.data(), block_msg.size()};
    const auto [read, reqd] = parser(s);
    BOOST_CHECK_EQUAL(block_msg.size(), read);
    BOOST_CHECK_EQUAL(0, reqd);

    BOOST_CHECK_EQUAL(block_msg.size(), parser.size());

    vector<uint8_t> out(block_msg.size());
    const auto bytes_read = parser.read(0, std::span{out.data(), out.size()});
    BOOST_CHECK_EQUAL(out.size(), bytes_read);
    BOOST_CHECK_EQUAL_COLLECTIONS(block_msg.cbegin(), block_msg.cend(),
                                  out.cbegin(), out.cend());
    BOOST_CHECK_EQUAL(block_msg.size(), parser.size());
}

BOOST_AUTO_TEST_CASE(read_byte_by_byte)
{
    block_parser parser;
    std::span s{block_msg.data(), block_msg.size()};
    const auto [read, reqd] = parser(s);
    BOOST_CHECK_EQUAL(block_msg.size(), read);
    BOOST_CHECK_EQUAL(0, reqd);

    size_t total_bytes_read{};
    vector<uint8_t> out(block_msg.size());
    for(size_t i{}; i < block_msg.size(); ++i)
    {
        total_bytes_read += parser.read(i, std::span{out.data()+i, 1});
    }
    BOOST_CHECK_EQUAL(out.size(), total_bytes_read);
    BOOST_CHECK_EQUAL_COLLECTIONS(block_msg.cbegin(), block_msg.cend(),
                                  out.cbegin(), out.cend());
}

BOOST_AUTO_TEST_CASE(read_beyond_parser_size)
{
    block_parser parser;
    std::span s{block_msg.data(), block_msg.size()};
    const auto [read, reqd] = parser(s);
    BOOST_CHECK_EQUAL(block_msg.size(), read);
    BOOST_CHECK_EQUAL(0, reqd);
    BOOST_CHECK_EQUAL(block_msg.size(), parser.size());

    vector<uint8_t> out(block_msg.size() + 1);
    const auto bytes_read = parser.read(0, std::span{out.data(), out.size()});
    BOOST_CHECK_EQUAL(out.size()-1, bytes_read);
    BOOST_CHECK_EQUAL_COLLECTIONS(block_msg.cbegin(), block_msg.cend(),
                                  out.cbegin(), out.cend() - 1);
    BOOST_CHECK_EQUAL(block_msg.size(), parser.size());
}

BOOST_AUTO_TEST_SUITE_END()
