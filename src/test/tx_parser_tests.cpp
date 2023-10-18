// Copyright (c) 2023 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <vector>

#include <boost/test/unit_test_suite.hpp>
#include <boost/test/unit_test.hpp>

#include "net/tx_parser.h"
#include "net/p2p_msg_lengths.h"

using namespace std;
using namespace bsv;

static const std::vector<uint8_t> tx = []
{
    std::vector<uint8_t> tx;

    tx.insert(tx.end(), version_len, 3);    // tx version
    tx.push_back(4);                        // n inputs 
    
    // ip 1
    tx.insert(tx.end(), outpoint_len, 4);   // tx outpoint 
    tx.push_back(1);                        // script length
    tx.push_back(0x6a);                     // script (op_return)
    tx.insert(tx.end(), seq_len, 5);        // sequence
    // ip 2
    tx.insert(tx.end(), outpoint_len, 6);   // tx outpoint 
    tx.push_back(0xfd);                     // script length encoded 2 bytes
    tx.push_back(2);                        // script length little endian 
    tx.push_back(0);                        // 
    tx.push_back(0x6a);                     // script (op_return)
    tx.push_back(0x6a);                     // script (op_return)
    tx.insert(tx.end(), seq_len, 7);        // sequence
    // ip 3
    tx.insert(tx.end(), outpoint_len, 12);  // tx outpoint 
    tx.push_back(0xfe);                     // script length encoded 4 bytes
    tx.push_back(3);                        // script length
    tx.push_back(0);                        // script length
    tx.push_back(0);                        // script length
    tx.push_back(0);                        // script length
    tx.push_back(0x6a);                     // script (op_return)
    tx.push_back(0x6a);                     // script (op_return)
    tx.push_back(0x6a);                     // script (op_return)
    tx.insert(tx.end(), seq_len, 13);       // sequence
    // ip 4
    tx.insert(tx.end(), outpoint_len, 14);  // tx outpoint 
    tx.push_back(0xff);                     // script length encoded 4 bytes
    tx.push_back(4);                        // script length
    tx.push_back(0);                        // script length
    tx.push_back(0);                        // script length
    tx.push_back(0);                        // script length
    tx.push_back(0);                        // script length
    tx.push_back(0);                        // script length
    tx.push_back(0);                        // script length
    tx.push_back(0);                        // script length
    tx.push_back(0x6a);                     // script (op_return)
    tx.push_back(0x6a);                     // script (op_return)
    tx.push_back(0x6a);                     // script (op_return)
    tx.push_back(0x6a);                     // script (op_return)
    tx.insert(tx.end(), seq_len, 15);       // sequence

    tx.push_back(4);                        // number of outputs
    // op 1
    tx.insert(tx.end(), value_len, 8);      // value
    tx.push_back(1);                        // script length
    tx.push_back(0x6a);                     // script (op_return)
    // op 2
    tx.insert(tx.end(), value_len, 9);      // value
    tx.push_back(0xfd);                     // script length encoded 2 bytes
    tx.push_back(2);                        // script length
    tx.push_back(0);                        // script length
    tx.push_back(0x6a);                     // script (op_return)
    tx.push_back(0x6a);                     // script (op_return)
    // op 3
    tx.insert(tx.end(), value_len, 16);     // value
    tx.push_back(0xfe);                     // script length
    tx.push_back(3);                        // script length
    tx.push_back(0);                        // script length
    tx.push_back(0);                        // script length
    tx.push_back(0);                        // script length
    tx.push_back(0x6a);                     // script (op_return)
    tx.push_back(0x6a);                     // script (op_return)
    tx.push_back(0x6a);                     // script (op_return)
    
    // op 4
    tx.insert(tx.end(), value_len, 17);     // value
    tx.push_back(0xff);                     // script length
    tx.push_back(4);                        // script length
    tx.push_back(0);                        // script length
    tx.push_back(0);                        // script length
    tx.push_back(0);                        // script length
    tx.push_back(0);                        // script length
    tx.push_back(0);                        // script length
    tx.push_back(0);                        // script length
    tx.push_back(0);                        // script length
    tx.push_back(0x6a);                     // script (op_return)
    tx.push_back(0x6a);                     // script (op_return)
    tx.push_back(0x6a);                     // script (op_return)
    tx.push_back(0x6a);                     // script (op_return)

    // locktime
    tx.insert(tx.end(), locktime_len, 10);  // lock time

    return tx;
}();

constexpr size_t script_len_1{1};
constexpr size_t script_len_2{2};
constexpr size_t script_len_3{3};
constexpr size_t script_len_4{4};

BOOST_AUTO_TEST_SUITE(tx_parser_tests)

BOOST_AUTO_TEST_CASE(tx_parser_by_parts)
{
    tx_parser parser;
    size_t offset{};
    size_t exp_buffer_len{};
    
    {
        // empty range
        const size_t n{};
        std::span s{tx.data(), n};
        const auto [bytes_read, bytes_reqd] = parser(s);
        BOOST_CHECK_EQUAL(0, bytes_read);
        BOOST_CHECK_EQUAL(version_len, bytes_reqd);
        BOOST_CHECK_EQUAL(exp_buffer_len, parser.buffer_size());
        offset += bytes_read;
    }

    {
        // version
        const size_t n{bsv::version_len};
        std::span s{tx.data() + offset, n};
        const auto [bytes_read, bytes_reqd] = parser(s);
        BOOST_CHECK_EQUAL(bsv::version_len, bytes_read);
        BOOST_CHECK_EQUAL(var_int_len_1, bytes_reqd);

        exp_buffer_len += bsv::version_len;
        BOOST_CHECK_EQUAL(exp_buffer_len, parser.buffer_size());
        offset += bytes_read;
    }

    {
        // ip count 
        const size_t n{var_int_len_1};
        std::span s{tx.data() + offset, n};
        const auto [bytes_read, bytes_reqd] = parser(s);
        BOOST_CHECK_EQUAL(var_int_len_1, bytes_read);
        BOOST_CHECK_EQUAL(bsv::outpoint_len + var_int_len_1, bytes_reqd);
        exp_buffer_len += var_int_len_1;
        BOOST_CHECK_EQUAL(exp_buffer_len, parser.buffer_size());
        offset += bytes_read;
    }
    
    // input 1, var_int len=1, val=1
    {
        // upto script len  
        const size_t n{bsv::outpoint_len + var_int_len_1};
        std::span s{tx.data() + offset, n};
        const auto [bytes_read, bytes_reqd] = parser(s);
        BOOST_CHECK_EQUAL(n, bytes_read);
        BOOST_CHECK_EQUAL(script_len_1 + bsv::seq_len, bytes_reqd);
        exp_buffer_len += bsv::outpoint_len + var_int_len_1;
        BOOST_CHECK_EQUAL(exp_buffer_len, parser.buffer_size());
        offset += bytes_read;
    }
    
    {
        // post script len
        const size_t n{script_len_1 + bsv::seq_len};
        std::span s{tx.data() + offset, n};
        const auto [bytes_read, bytes_reqd] = parser(s);
        BOOST_CHECK_EQUAL(n, bytes_read);
        BOOST_CHECK_EQUAL(bsv::outpoint_len + var_int_len_1,
                          bytes_reqd); // <- expect another tx
        exp_buffer_len += bytes_read; 
        BOOST_CHECK_EQUAL(exp_buffer_len, parser.buffer_size());
        offset += bytes_read;
    }
        
    // input 2 (var_int len= 3, val=2)
    {
        // tx, input 2 upto script len 1 
        const size_t n{outpoint_len + var_int_len_1};
        std::span s{tx.data() + offset, n};
        const auto [bytes_read, bytes_reqd] = parser(s);
        BOOST_CHECK_EQUAL(0, bytes_read);
        BOOST_CHECK_EQUAL(outpoint_len + var_int_len_3, bytes_reqd);
        BOOST_CHECK_EQUAL(exp_buffer_len, parser.buffer_size());
        offset += bytes_read;
    }
    
    {
        // script len 
        const size_t n{bsv::outpoint_len + var_int_len_3};
        std::span s{tx.data() + offset, n};
        const auto [bytes_read, bytes_reqd] = parser(s);
        BOOST_CHECK_EQUAL(n, bytes_read);
        BOOST_CHECK_EQUAL(script_len_2 + bsv::seq_len, bytes_reqd);

        exp_buffer_len += bsv::outpoint_len + var_int_len_3;
        BOOST_CHECK_EQUAL(exp_buffer_len, parser.buffer_size());

        offset += bytes_read;
    }
    
    {
        // post script len
        const size_t n{script_len_2 + bsv::seq_len};
        std::span s{tx.data() + offset, n};
        const auto [bytes_read, bytes_reqd] = parser(s);
        BOOST_CHECK_EQUAL(n, bytes_read);
        BOOST_CHECK_EQUAL(bsv::outpoint_len + var_int_len_1,
                          bytes_reqd); // <- expect another tx
        exp_buffer_len += bytes_read; 
        BOOST_CHECK_EQUAL(exp_buffer_len, parser.buffer_size());
        offset += bytes_read;
    }
    
    // input 3 (var_int len= 5, val=3)
    {
        // upto script len  
        const size_t n{bsv::outpoint_len + var_int_len_5};
        std::span s{tx.data() + offset, n};
        const auto [bytes_read, bytes_reqd] = parser(s);
        BOOST_CHECK_EQUAL(n, bytes_read);
        BOOST_CHECK_EQUAL(script_len_3 + bsv::seq_len, bytes_reqd);
        exp_buffer_len += bsv::outpoint_len + var_int_len_5;
        BOOST_CHECK_EQUAL(exp_buffer_len, parser.buffer_size());
        offset += bytes_read;
    }
    
    {
        // post script len
        const size_t n{script_len_3 + bsv::seq_len};
        std::span s{tx.data() + offset, n};
        const auto [bytes_read, bytes_reqd] = parser(s);
        BOOST_CHECK_EQUAL(n, bytes_read);
        BOOST_CHECK_EQUAL(bsv::outpoint_len + var_int_len_1,
                          bytes_reqd); // <- expect another tx
        exp_buffer_len += bytes_read; 
        BOOST_CHECK_EQUAL(exp_buffer_len, parser.buffer_size());
        offset += bytes_read;
    }
    
    // input 4 (var_int len= 9, val=4)
    {
        // upto script len  
        const size_t n{bsv::outpoint_len + var_int_len_9};
        std::span s{tx.data() + offset, n};
        const auto [bytes_read, bytes_reqd] = parser(s);
        BOOST_CHECK_EQUAL(n, bytes_read);
        BOOST_CHECK_EQUAL(script_len_4 + bsv::seq_len, bytes_reqd);
        exp_buffer_len += bsv::outpoint_len + var_int_len_9;
        BOOST_CHECK_EQUAL(exp_buffer_len, parser.buffer_size());
        offset += bytes_read;
    }

    {
        // post script len
        const size_t n{script_len_4 + bsv::seq_len};
        std::span s{tx.data() + offset, n};
        const auto [bytes_read, bytes_reqd] = parser(s);
        BOOST_CHECK_EQUAL(n, bytes_read);
        BOOST_CHECK_EQUAL(var_int_len_1, bytes_reqd); // <- output count
        exp_buffer_len += bytes_read; 
        BOOST_CHECK_EQUAL(exp_buffer_len, parser.buffer_size());
        offset += bytes_read;
    }

    // op_count
    {
        const size_t n{var_int_len_1};
        std::span s{tx.data() + offset, n};
        const auto [bytes_read, bytes_reqd] = parser(s);
        BOOST_CHECK_EQUAL(n, bytes_read);
        BOOST_CHECK_EQUAL(bsv::value_len + var_int_len_1, bytes_reqd);
        exp_buffer_len += bytes_read; 
        BOOST_CHECK_EQUAL(exp_buffer_len, parser.buffer_size());
        offset += bytes_read;
    }

    // output 1 (var_int len= 1, val=1)
    {
        // upto script len  
        const size_t n{bsv::value_len + var_int_len_1};
        std::span s{tx.data() + offset, n};
        const auto [bytes_read, bytes_reqd] = parser(s);
        BOOST_CHECK_EQUAL(n, bytes_read);
        BOOST_CHECK_EQUAL(script_len_1, bytes_reqd);
        exp_buffer_len += bytes_read; 
        BOOST_CHECK_EQUAL(exp_buffer_len, parser.buffer_size());
        offset += bytes_read;
    }
    
    {
        // post script len
        const size_t n{script_len_1};
        std::span s{tx.data() + offset, n};
        const auto [bytes_read, bytes_reqd] = parser(s);
        BOOST_CHECK_EQUAL(n, bytes_read);
        BOOST_CHECK_EQUAL(bsv::value_len + var_int_len_1, bytes_reqd);
        exp_buffer_len += bytes_read; 
        BOOST_CHECK_EQUAL(exp_buffer_len, parser.buffer_size());
        offset += bytes_read;
    }

    // output 2 (var_int len=3, val=2)
    {
        // upto script len 1 
        const size_t n{bsv::value_len + var_int_len_1};
        std::span s{tx.data() + offset, n};
        const auto [bytes_read, bytes_reqd] = parser(s);
        BOOST_CHECK_EQUAL(0, bytes_read);
        BOOST_CHECK_EQUAL(bsv::value_len + var_int_len_3, bytes_reqd);
        exp_buffer_len += bytes_read; 
        BOOST_CHECK_EQUAL(exp_buffer_len, parser.buffer_size());
        offset += bytes_read;
    }
    
    {
        // post script len 
        const size_t n{bsv::value_len + var_int_len_3 + script_len_2};
        std::span s{tx.data() + offset, n};
        const auto [bytes_read, bytes_reqd] = parser(s);
        BOOST_CHECK_EQUAL(n, bytes_read);
        BOOST_CHECK_EQUAL(bsv::value_len + var_int_len_1, bytes_reqd);
        exp_buffer_len += bytes_read; 
        BOOST_CHECK_EQUAL(exp_buffer_len, parser.buffer_size());
        offset += bytes_read;
    }

    // output 3 (var_int len=5, val=3)
    {
        // upto script len 
        const size_t n{bsv::value_len + var_int_len_5 + script_len_3};
        std::span s{tx.data() + offset, n};
        const auto [bytes_read, bytes_reqd] = parser(s);
        BOOST_CHECK_EQUAL(n, bytes_read);
        BOOST_CHECK_EQUAL(bsv::value_len + var_int_len_1, bytes_reqd);
        exp_buffer_len += bytes_read; 
        BOOST_CHECK_EQUAL(exp_buffer_len, parser.buffer_size());
        offset += bytes_read;
    }
    
    // output 4 (var_int len=9, val=4)
    {
        // upto script len 
        const size_t n{bsv::value_len + var_int_len_9 + script_len_4};
        std::span s{tx.data() + offset, n};
        const auto [bytes_read, bytes_reqd] = parser(s);
        BOOST_CHECK_EQUAL(n, bytes_read);
        BOOST_CHECK_EQUAL(bsv::locktime_len, bytes_reqd);
        exp_buffer_len += bytes_read; 
        BOOST_CHECK_EQUAL(exp_buffer_len, parser.buffer_size());
        offset += bytes_read;
    }

    {
        // locktime
        const size_t n{bsv::locktime_len};
        std::span s{tx.data() + offset, n};
        const auto [bytes_read, bytes_reqd] = parser(s);
        BOOST_CHECK_EQUAL(n, bytes_read);
        BOOST_CHECK_EQUAL(0, bytes_reqd);
        BOOST_CHECK_EQUAL(0, parser.buffer_size());
        offset += bytes_read;
    }
    
    {
        // Check reports 0, 0 once complete 
        std::vector<uint8_t> tx{42};
        const auto [bytes_read, bytes_reqd] = parser(tx);
        BOOST_CHECK_EQUAL(0, bytes_read);
        BOOST_CHECK_EQUAL(0, bytes_reqd);
    }
}

BOOST_AUTO_TEST_CASE(tx_parser_1_pass)
{
    tx_parser parser;
    std::span s{tx.data(), tx.size()};
    const auto [bytes_read, bytes_reqd] = parser(s);
    BOOST_CHECK_EQUAL(tx.size(), bytes_read);
    BOOST_CHECK_EQUAL(0, bytes_reqd);
    BOOST_CHECK_EQUAL(0, parser.buffer_size());
    BOOST_CHECK_EQUAL(tx.size(), parser.size());
}

BOOST_AUTO_TEST_CASE(tx_parser_2_pass)
{
    tx_parser parser;

    constexpr size_t split_pos{20};
    const auto [bytes_read, bytes_reqd] = parser(std::span{tx.data(), split_pos});
    parser(std::span{tx.data() + bytes_read, tx.size() - bytes_read});
    BOOST_CHECK_EQUAL(0, parser.buffer_size());
    BOOST_CHECK_EQUAL(tx.size(), parser.size());
}

BOOST_AUTO_TEST_CASE(tx_parser_as_reqd)
{
    tx_parser parser;
    size_t total_bytes_read{};
    size_t offset{};
    size_t n{1};
    size_t passes{};
    while(total_bytes_read < tx.size())
    {
        std::span s{tx.data() + offset, n};
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
    BOOST_CHECK_EQUAL(tx.size(), total_bytes_read);
    BOOST_CHECK_EQUAL(0, parser.buffer_size());
    BOOST_CHECK_EQUAL(27, passes);
    BOOST_CHECK_EQUAL(tx.size(), parser.size());
}

BOOST_AUTO_TEST_CASE(parse_large_input_count_and_script_len)
{
    tx_parser parser;
    
    std::vector<uint8_t> tx;
    tx.insert(tx.cend(), version_len, 1);       // tx version
    tx.insert(tx.cend(), 9, 0xff);              // <- large n inputs 
    tx.insert(tx.cend(), bsv::outpoint_len, 2); // outpoint
    tx.insert(tx.cend(), 9, 0xff);              // <- large script len
    std::span s{tx.data(), tx.size()};
    const auto [bytes_read, bytes_reqd] = parser(s);
    BOOST_CHECK_EQUAL(58, bytes_read);
    BOOST_CHECK_EQUAL(0xffff'ffff'ffff'ffff, bytes_reqd);
}

BOOST_AUTO_TEST_CASE(parse_large_output_count_and_script_len)
{
    tx_parser parser;
    
    std::vector<uint8_t> tx;
    tx.insert(tx.cend(), version_len, 1);       // tx version
    tx.push_back(1);                            // n inputs
    tx.insert(tx.cend(), bsv::outpoint_len, 2); // outpoint
    tx.push_back(1);                            // script len
    tx.push_back(0x6a);                         // script
    tx.insert(tx.cend(), bsv::seq_len, 3);      // sequence
    tx.insert(tx.cend(), 9, 0xff);              // <- large n outputs
    tx.insert(tx.cend(), bsv::value_len, 4);    // value 
    tx.insert(tx.cend(), 9, 0xff);              // <- large script len

    std::span s{tx.data(), tx.size()};
    const auto [bytes_read, bytes_reqd] = parser(s);
    BOOST_CHECK_EQUAL(73, bytes_read);
    BOOST_CHECK_EQUAL(0xffff'ffff'ffff'ffff, bytes_reqd);
}

BOOST_AUTO_TEST_SUITE_END()

