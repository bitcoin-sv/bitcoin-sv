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

constexpr size_t header_len{80};
constexpr size_t tx_count_len{0x3};
constexpr size_t tx_count{0xfd};
constexpr size_t tx_len{62};

const std::vector<uint8_t> block_msg{[]
{
    vector<uint8_t> v;
    v.insert(v.end(), version_len, 1); // version
    v.insert(v.end(), 32, 2);          // hash(prev_block)
    v.insert(v.end(), 32, 3);          // hash(merkle root)
    v.insert(v.end(), 4, 4);           // timestamp
    v.insert(v.end(), 4, 5);           // target
    v.insert(v.end(), 4, 6);           // nonce

    v.push_back(0xfd);
    v.push_back(tx_count);
    v.push_back(0);

    for(size_t i{}; i < tx_count; ++i)
    {
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
    }

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
        BOOST_CHECK_EQUAL(1U, bytes_reqd);
        BOOST_CHECK_EQUAL(block_header_len - 1, parser.size());
    }

    {
        // size(block_msg) == block_header_len
        block_parser parser;
        std::span s{block_msg.data(), block_header_len};
        const auto [bytes_read, bytes_reqd] = parser(s);
        BOOST_CHECK_EQUAL(block_header_len, bytes_read);
        BOOST_CHECK_EQUAL(1U, bytes_reqd);
        BOOST_CHECK_EQUAL(80U, parser.size());
    }

    {
        // size(block_msg) > block_header_len
        block_parser parser;
        std::span s{block_msg.data(), block_msg.size()};
        const auto [bytes_read, bytes_reqd] = parser(s);
        BOOST_CHECK_EQUAL(block_msg.size(), bytes_read);
        BOOST_CHECK_EQUAL(0U, bytes_reqd);
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
        span s{block_msg.data() + offset, n}; // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
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
    BOOST_CHECK_EQUAL(2'028U, passes);
    BOOST_CHECK_EQUAL(block_msg.size(), parser.size());
}

BOOST_AUTO_TEST_CASE(parse_byte_by_byte)
{
    msg_parser_buffer parser{make_unique<msg_parser>(block_parser{})};

    for(size_t i{}; i < block_msg.size(); ++i)
    {
        std::span s{block_msg.data() + i, 1}; // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
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
    BOOST_CHECK_EQUAL(0U, reqd);

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
    BOOST_CHECK_EQUAL(0U, reqd);

    size_t total_bytes_read{};
    vector<uint8_t> out(block_msg.size());
    for(size_t i{}; i < block_msg.size(); ++i)
    {
        total_bytes_read += parser.read(i, std::span{out.data()+i, 1}); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
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
    BOOST_CHECK_EQUAL(0U, reqd);
    BOOST_CHECK_EQUAL(block_msg.size(), parser.size());

    vector<uint8_t> out(block_msg.size() + 1);
    const auto bytes_read = parser.read(0, std::span{out.data(), out.size()});
    BOOST_CHECK_EQUAL(out.size()-1, bytes_read);
    BOOST_CHECK_EQUAL_COLLECTIONS(block_msg.cbegin(), block_msg.cend(),
                                  out.cbegin(), out.cend() - 1);
    BOOST_CHECK_EQUAL(block_msg.size(), parser.size());
}

BOOST_AUTO_TEST_CASE(read_partial_tx_count)
{
    block_parser parser;
    constexpr auto n_bytes{header_len + 1}; // <- partial tx count
    std::ignore = parser(span{block_msg.data(), n_bytes});

    vector<uint8_t> out(block_msg.size());
    const auto exp_bytes{n_bytes - 1};
    const size_t bytes_read = parser.read(0, span{out.data(), out.size()});
    BOOST_CHECK_EQUAL(exp_bytes, bytes_read);
    BOOST_CHECK_EQUAL_COLLECTIONS(block_msg.cbegin(), 
                                  block_msg.cbegin() + exp_bytes, 
                                  out.cbegin(), 
                                  out.cbegin() + exp_bytes);
}

BOOST_AUTO_TEST_CASE(read_partial_tx_1)
{
    block_parser parser;
    constexpr auto n_bytes{header_len + 
                           tx_count_len +
                           version_len }; // <- partial tx 
    std::ignore = parser(span{block_msg.data(), n_bytes});

    vector<uint8_t> out(block_msg.size());
    const auto exp_bytes{n_bytes - version_len};
    const size_t bytes_read = parser.read(0, span{out.data(), out.size()});
    BOOST_CHECK_EQUAL(exp_bytes, bytes_read);
    BOOST_CHECK_EQUAL_COLLECTIONS(block_msg.cbegin(), 
                                  block_msg.cbegin() + exp_bytes, 
                                  out.cbegin(), 
                                  out.cbegin() + exp_bytes);
}

BOOST_AUTO_TEST_CASE(read_partial_tx_2)
{
    block_parser parser;
    constexpr auto n_bytes{header_len + 
                           tx_count_len + tx_len +
                           version_len }; // <- partial tx
    std::ignore = parser(span{block_msg.data(), n_bytes});

    vector<uint8_t> out(block_msg.size());
    const auto exp_bytes{n_bytes - version_len};
    const size_t bytes_read = parser.read(0, span{out.data(), out.size()});
    BOOST_CHECK_EQUAL(exp_bytes, bytes_read);
    BOOST_CHECK_EQUAL_COLLECTIONS(block_msg.cbegin(), 
                                  block_msg.cbegin() + exp_bytes, 
                                  out.cbegin(), 
                                  out.cbegin() + exp_bytes);
}

BOOST_AUTO_TEST_SUITE_END()
