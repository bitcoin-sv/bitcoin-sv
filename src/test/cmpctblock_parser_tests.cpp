// Copyright (c) 2023 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE
#include <boost/test/tools/old/interface.hpp>
#include <boost/test/unit_test.hpp>

#include "net/cmpctblock_parser.h"
#include "net/msg_parser.h"
#include "net/msg_parser_buffer.h"
#include "net/p2p_msg_lengths.h"

using namespace std;
using namespace bsv;

constexpr size_t nonce_len{8};

const std::vector<uint8_t> cmpctblock_msg{[]
{
    vector<uint8_t> v;
    v.insert(v.end(), version_len, 1); // version
    v.insert(v.end(), 32, 2);          // hash(prev_block)
    v.insert(v.end(), 32, 3);          // hash(merkle root)
    v.insert(v.end(), 4, 4);           // timestamp
    v.insert(v.end(), 4, 5);           // target
    v.insert(v.end(), 4, 6);           // nonce
    
    v.insert(v.end(), nonce_len, 7);   // nonce 

    // short ids
    v.push_back(200);                   // short id count
    v.insert(v.end(), 600, 8);          // short id
    v.insert(v.end(), 600, 9);          // short id

    // preffilled txs
    v.push_back(1);                     // count

    // prefilled tx
    v.push_back(42);                     // index
    v.insert(v.end(), version_len, 11);  // tx version
    v.push_back(1);                      // 1 input 
    
    v.insert(v.end(), outpoint_len, 12); // tx outpoint 
    v.push_back(1);                      // script length
    v.push_back(0x6a);                   // script (op_return)
    v.insert(v.end(), seq_len, 13);      // sequence

    v.push_back(1);                      // number of outputs
    v.insert(v.end(), value_len, 14);    // value
    v.push_back(1);                      // script length
    v.push_back(0x6a);                   // script (op_return)

    // locktime
    v.insert(v.end(), locktime_len, 15);  // lock time

    return v;
}()};

BOOST_AUTO_TEST_SUITE(cmpctblock_parser_tests)

BOOST_AUTO_TEST_CASE(parse_all)
{
    constexpr size_t block_header_len{80};
    {
        // size(cmpctblock_msg) < block_header_len + nonce_len
        cmpctblock_parser parser;
        std::span s{cmpctblock_msg.data(), block_header_len + nonce_len - 1};
        const auto [bytes_read, bytes_reqd] = parser(s);
        BOOST_CHECK_EQUAL(block_header_len + nonce_len - 1, bytes_read);
        BOOST_CHECK_EQUAL(1, bytes_reqd);
        
        BOOST_CHECK_EQUAL(block_header_len + nonce_len - 1, parser.size());
    }

    {
        // size(cmpctblock_msg) == block_header_len + nonce_len
        cmpctblock_parser parser;
        std::span s{cmpctblock_msg.data(), block_header_len + nonce_len};
        const auto [bytes_read, bytes_reqd] = parser(s);
        BOOST_CHECK_EQUAL(block_header_len + nonce_len, bytes_read);
        BOOST_CHECK_EQUAL(1, bytes_reqd);
        BOOST_CHECK_EQUAL(block_header_len + nonce_len, parser.size());
    }

    {
        // size(cmpctblock_msg) > block_header_len + nonce_len
        cmpctblock_parser parser;
        std::span s{cmpctblock_msg.data(), cmpctblock_msg.size()};
        const auto [bytes_read, bytes_reqd] = parser(s);
        BOOST_CHECK_EQUAL(cmpctblock_msg.size(), bytes_read);
        BOOST_CHECK_EQUAL(0, bytes_reqd);
        BOOST_CHECK_EQUAL(cmpctblock_msg.size(), parser.size());
    }
}

BOOST_AUTO_TEST_CASE(parse_as_reqd)
{
    cmpctblock_parser parser;
    size_t total_bytes_read{};
    size_t offset{};
    size_t n{1};
    size_t passes{};
    while(total_bytes_read < cmpctblock_msg.size())
    {
        span s{cmpctblock_msg.data() + offset, n};
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
    BOOST_CHECK_EQUAL(cmpctblock_msg.size(), total_bytes_read);
    BOOST_CHECK_EQUAL(14, passes);
    BOOST_CHECK_EQUAL(cmpctblock_msg.size(), parser.size());
}

BOOST_AUTO_TEST_CASE(parse_byte_by_byte)
{
    msg_parser_buffer parser{make_unique<msg_parser>(cmpctblock_parser{})};

    for(size_t i{}; i < cmpctblock_msg.size(); ++i)
    {
        std::span s{cmpctblock_msg.data() + i, 1};
        parser(s);
    }

    BOOST_CHECK_EQUAL(cmpctblock_msg.size(), parser.size());
}

BOOST_AUTO_TEST_CASE(read_all)
{
    cmpctblock_parser parser;
    std::span s{cmpctblock_msg.data(), cmpctblock_msg.size()};
    const auto [read, reqd] = parser(s);
    BOOST_CHECK_EQUAL(cmpctblock_msg.size(), read);
    BOOST_CHECK_EQUAL(0, reqd);
    BOOST_CHECK_EQUAL(cmpctblock_msg.size(), parser.size());

    vector<uint8_t> out(cmpctblock_msg.size());
    const auto bytes_read = parser.read(0, std::span{out.data(), out.size()});
    BOOST_CHECK_EQUAL(cmpctblock_msg.size(), bytes_read);
    BOOST_CHECK_EQUAL_COLLECTIONS(cmpctblock_msg.cbegin(), cmpctblock_msg.cend(),
                                  out.cbegin(), out.cend());
    BOOST_CHECK_EQUAL(cmpctblock_msg.size(), parser.size());
}

BOOST_AUTO_TEST_CASE(read_byte_by_byte)
{
    cmpctblock_parser parser;
    std::span s{cmpctblock_msg.data(), cmpctblock_msg.size()};
    const auto [read, reqd] = parser(s);
    BOOST_CHECK_EQUAL(cmpctblock_msg.size(), read);
    BOOST_CHECK_EQUAL(0, reqd);
    BOOST_CHECK_EQUAL(cmpctblock_msg.size(), parser.size());

    size_t total_bytes_read{};
    vector<uint8_t> out(cmpctblock_msg.size());
    for(size_t i{}; i < cmpctblock_msg.size(); ++i)
    {
        total_bytes_read += parser.read(i, std::span{out.data()+i, 1});
    }
    BOOST_CHECK_EQUAL(cmpctblock_msg.size(), total_bytes_read);
    BOOST_CHECK_EQUAL_COLLECTIONS(cmpctblock_msg.cbegin(), cmpctblock_msg.cend(),
                                  out.cbegin(), out.cend());
}

BOOST_AUTO_TEST_CASE(read_beyond_parser_size)
{
    cmpctblock_parser parser;
    std::span s{cmpctblock_msg.data(), cmpctblock_msg.size()};
    const auto [read, reqd] = parser(s);
    BOOST_CHECK_EQUAL(cmpctblock_msg.size(), read);
    BOOST_CHECK_EQUAL(0, reqd);

    vector<uint8_t> out(cmpctblock_msg.size() + 1);
    const auto bytes_read = parser.read(0, std::span{out.data(), out.size()});
    BOOST_CHECK_EQUAL(cmpctblock_msg.size(), bytes_read);
    BOOST_CHECK_EQUAL_COLLECTIONS(cmpctblock_msg.cbegin(), cmpctblock_msg.cend(),
                                  out.cbegin(), out.cend() - 1);
}

BOOST_AUTO_TEST_SUITE_END()
