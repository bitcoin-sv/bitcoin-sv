// Copyright (c) 2023 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file

#include <cstddef>
#include <cstdint>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "net/tx_parser.h"
#include "net/array_parser.h"
#include "net/parser_utils.h"
#include "net/p2p_msg_lengths.h"

using namespace std;
using namespace bsv;

static const std::vector<uint8_t> txs = []
{
    std::vector<uint8_t> txs;

    txs.insert(txs.end(), 1, 2);              // tx count
    
    // tx 1
    txs.insert(txs.end(), version_len, 3);    // tx version
    txs.push_back(2);                         // 1 input 
    
    // ip 1
    txs.insert(txs.end(), outpoint_len, 4);   // tx outpoint 
    txs.push_back(1);                       // script length
    txs.push_back(0x6a);                    // script (op_return)
    txs.insert(txs.end(), seq_len, 5);        // sequence
    // ip 2
    txs.insert(txs.end(), outpoint_len, 6);   // tx outpoint 
    txs.push_back(0xfd);                    // script length encoded 2 bytes
    txs.push_back(2);                       // script length little endian 
    txs.push_back(0);                       // 
    txs.push_back(0x6a);                    // script (op_return)
    txs.push_back(0x6a);                    // script (op_return)
    txs.insert(txs.end(), seq_len, 7);        // sequence

    txs.push_back(2);                       // number of outputs
    // op 1
    txs.insert(txs.end(), value_len, 8);      // value
    txs.push_back(1);                       // script length
    txs.push_back(0x6a);                    // script (op_return)
    // op 2
    txs.insert(txs.end(), value_len, 9);      // value
    txs.push_back(0xfd);                    // script length encoded 2 bytes
    txs.push_back(2);                       // script length
    txs.push_back(0);                       // script length
    txs.push_back(0x6a);                    // script (op_return)
    txs.push_back(0x6a);                    // script (op_return)

    // locktime
    txs.insert(txs.end(), locktime_len, 10);  // lock time

    // tx 2
    txs.insert(txs.end(), version_len, 11);   // tx version
    txs.push_back(2);                       // 2 inputs
    // ip 1
    txs.insert(txs.end(), outpoint_len, 12);  // tx outpoint 
    txs.push_back(0xfe);                    // script length encoded 4 bytes
    txs.push_back(1);                       // script length
    txs.push_back(0);                       // script length
    txs.push_back(0);                       // script length
    txs.push_back(0);                       // script length
    txs.push_back(0x6a);                    // script (op_return)
    txs.insert(txs.end(), seq_len, 13);       // sequence
    // ip 2
    txs.insert(txs.end(), outpoint_len, 14);  // tx outpoint 
    txs.push_back(0xff);                    // script length encoded 4 bytes
    txs.push_back(1);                       // script length
    txs.push_back(0);                       // script length
    txs.push_back(0);                       // script length
    txs.push_back(0);                       // script length
    txs.push_back(0);                       // script length
    txs.push_back(0);                       // script length
    txs.push_back(0);                       // script length
    txs.push_back(0);                       // script length
    txs.push_back(0x6a);                    // script (op_return)
    txs.insert(txs.end(), seq_len, 15);       // sequence

    txs.push_back(2);                       // number of outputs
    // op 1
    txs.insert(txs.end(), value_len, 16);     // value
    txs.push_back(0xfe);                       // script length
    txs.push_back(1);                       // script length
    txs.push_back(0);                       // script length
    txs.push_back(0);                       // script length
    txs.push_back(0);                       // script length
    txs.push_back(0x6a);                    // script (op_return)
    
    // op 2
    txs.insert(txs.end(), value_len, 17);     // value
    txs.push_back(0xff);                       // script length
    txs.push_back(1);                       // script length
    txs.push_back(0);                       // script length
    txs.push_back(0);                       // script length
    txs.push_back(0);                       // script length
    txs.push_back(0);                       // script length
    txs.push_back(0);                       // script length
    txs.push_back(0);                       // script length
    txs.push_back(0);                       // script length
    txs.push_back(0x6a);                    // script (op_return)
        
    txs.insert(txs.end(), locktime_len, 18 );  // lock time

    return txs;
}();

constexpr size_t script_len_1{1};
constexpr size_t script_len_2{2};

constexpr auto tx_n_len{1};
constexpr auto tx1_len{120};
constexpr auto tx2_len{138};

using txs_parser = array_parser<tx_parser>;
    
BOOST_AUTO_TEST_SUITE(txs_parser_tests)

BOOST_AUTO_TEST_CASE(txs_parser_by_parts)
{
    txs_parser parser;
    size_t offset{};
    size_t exp_parser_size{};
    
    {
        // empty range
        const size_t n{};
        std::span s{txs.data(), n};
        const auto [bytes_read, bytes_reqd] = parser(s);
        BOOST_CHECK_EQUAL(0, bytes_read);
        BOOST_CHECK_EQUAL(var_int_len_1, bytes_reqd);
        BOOST_CHECK_EQUAL(0, parser.segment_count());
        BOOST_CHECK_EQUAL(exp_parser_size, parser.size());
        offset += bytes_read;
    }

    {
        // tx_count
        const size_t n{var_int_len_1};
        std::span s{txs.data(), n};
        const auto [bytes_read, bytes_reqd] = parser(s);
        BOOST_CHECK_EQUAL(var_int_len_1, bytes_read);
        BOOST_CHECK_EQUAL(bsv::version_len, bytes_reqd);
        BOOST_CHECK_EQUAL(1, parser.segment_count());
        exp_parser_size += 1;
        BOOST_CHECK_EQUAL(exp_parser_size, parser.size());
        offset += bytes_read;
    }

    {
        // version
        const size_t n{bsv::version_len};
        std::span s{txs.data() + offset, n};
        const auto [bytes_read, bytes_reqd] = parser(s);
        BOOST_CHECK_EQUAL(bsv::version_len, bytes_read);
        BOOST_CHECK_EQUAL(var_int_len_1, bytes_reqd);
        BOOST_CHECK_EQUAL(1, parser.segment_count());
        exp_parser_size += bsv::version_len;
        BOOST_CHECK_EQUAL(exp_parser_size, parser.size());
        offset += bytes_read;
    }

    {
        // ip count 
        const size_t n{var_int_len_1};
        std::span s{txs.data() + offset, n};
        const auto [bytes_read, bytes_reqd] = parser(s);
        BOOST_CHECK_EQUAL(var_int_len_1, bytes_read);
        BOOST_CHECK_EQUAL(bsv::outpoint_len + var_int_len_1, bytes_reqd);
        BOOST_CHECK_EQUAL(1, parser.segment_count());
        exp_parser_size += var_int_len_1;
        BOOST_CHECK_EQUAL(exp_parser_size, parser.size());
        offset += bytes_read;
    }
    
    {
        // tx1, input 1 upto script len  
        const size_t n{bsv::outpoint_len + var_int_len_1};
        std::span s{txs.data() + offset, n};
        const auto [bytes_read, bytes_reqd] = parser(s);
        BOOST_CHECK_EQUAL(n, bytes_read);
        BOOST_CHECK_EQUAL(script_len_1 + bsv::seq_len, bytes_reqd);
        BOOST_CHECK_EQUAL(1, parser.segment_count());
        exp_parser_size += bsv::outpoint_len + var_int_len_1;
        BOOST_CHECK_EQUAL(exp_parser_size, parser.size());
        offset += bytes_read;
    }
    
    {
        // tx1, input 1 post script len
        const size_t n{script_len_1 + bsv::seq_len};
        std::span s{txs.data() + offset, n};
        const auto [bytes_read, bytes_reqd] = parser(s);
        BOOST_CHECK_EQUAL(n, bytes_read);
        BOOST_CHECK_EQUAL(bsv::outpoint_len + var_int_len_1,
                          bytes_reqd); // <- expect another tx
        BOOST_CHECK_EQUAL(1, parser.segment_count());
        exp_parser_size += bytes_read; 
        BOOST_CHECK_EQUAL(exp_parser_size, parser.size());
        offset += bytes_read;
    }
        
    {
        // tx1, input 2 upto script len  
        const size_t n{bsv::outpoint_len + var_int_len_3};
        std::span s{txs.data() + offset, n};
        const auto [bytes_read, bytes_reqd] = parser(s);
        BOOST_CHECK_EQUAL(n, bytes_read);
        BOOST_CHECK_EQUAL(script_len_2 + bsv::seq_len, bytes_reqd);
        BOOST_CHECK_EQUAL(1, parser.segment_count());
        exp_parser_size += bsv::outpoint_len + var_int_len_3;
        BOOST_CHECK_EQUAL(exp_parser_size, parser.size());
        offset += bytes_read;
    }

    {
        // tx1, input 2 post script len
        const size_t n{script_len_2 + bsv::seq_len};
        std::span s{txs.data() + offset, n};
        const auto [bytes_read, bytes_reqd] = parser(s);
        BOOST_CHECK_EQUAL(n, bytes_read);
        BOOST_CHECK_EQUAL(var_int_len_1, bytes_reqd); // <- output count
        BOOST_CHECK_EQUAL(1, parser.segment_count());
        exp_parser_size += bytes_read; 
        BOOST_CHECK_EQUAL(exp_parser_size, parser.size());
        offset += bytes_read;
    }

    {
        // tx1, op_count
        const size_t n{var_int_len_1};
        std::span s{txs.data() + offset, n};
        const auto [bytes_read, bytes_reqd] = parser(s);
        BOOST_CHECK_EQUAL(n, bytes_read);
        BOOST_CHECK_EQUAL(bsv::value_len + var_int_len_1, bytes_reqd);
        BOOST_CHECK_EQUAL(1, parser.segment_count());
        exp_parser_size += bytes_read; 
        BOOST_CHECK_EQUAL(exp_parser_size, parser.size());
        offset += bytes_read;
    }

    {
        // tx1, output 1 upto script len  
        const size_t n{bsv::value_len + var_int_len_1};
        std::span s{txs.data() + offset, n};
        const auto [bytes_read, bytes_reqd] = parser(s);
        BOOST_CHECK_EQUAL(n, bytes_read);
        BOOST_CHECK_EQUAL(script_len_1, bytes_reqd);
        BOOST_CHECK_EQUAL(1, parser.segment_count());
        exp_parser_size += bytes_read; 
        BOOST_CHECK_EQUAL(exp_parser_size, parser.size());
        offset += bytes_read;
    }
    
    {
        // tx1, output 1 post script len
        const size_t n{script_len_1};
        std::span s{txs.data() + offset, n};
        const auto [bytes_read, bytes_reqd] = parser(s);
        BOOST_CHECK_EQUAL(n, bytes_read);
        BOOST_CHECK_EQUAL(bsv::value_len + var_int_len_1, bytes_reqd);
        BOOST_CHECK_EQUAL(1, parser.segment_count());
        exp_parser_size += bytes_read; 
        BOOST_CHECK_EQUAL(exp_parser_size, parser.size());
        offset += bytes_read;
    }

    {
        // tx1, output 2 all of it
        const size_t n{bsv::value_len + var_int_len_3 + script_len_2};
        std::span s{txs.data() + offset, n};
        const auto [bytes_read, bytes_reqd] = parser(s);
        BOOST_CHECK_EQUAL(n, bytes_read);
        BOOST_CHECK_EQUAL(bsv::locktime_len, bytes_reqd);
        BOOST_CHECK_EQUAL(1, parser.segment_count());
        exp_parser_size += bytes_read; 
        BOOST_CHECK_EQUAL(exp_parser_size, parser.size());
        offset += bytes_read;
    }

    {
        // tx1, locktime
        const size_t n{bsv::locktime_len};
        std::span s{txs.data() + offset, n};
        const auto [bytes_read, bytes_reqd] = parser(s);
        BOOST_CHECK_EQUAL(n, bytes_read);
        BOOST_CHECK_EQUAL(bsv::version_len, bytes_reqd);
        BOOST_CHECK_EQUAL(2, parser.segment_count());
        exp_parser_size += bytes_read; 
        BOOST_CHECK_EQUAL(exp_parser_size, parser.size());
        offset += bytes_read;
    }

    {
        // tx2 
        const size_t n{tx2_len};
        std::span s{txs.data() + offset, n};
        const auto [bytes_read, bytes_reqd] = parser(s);
        BOOST_CHECK_EQUAL(n, bytes_read);
        BOOST_CHECK_EQUAL(0, bytes_reqd);

        exp_parser_size += bytes_read; 
        BOOST_CHECK_EQUAL(exp_parser_size, parser.size());
        BOOST_CHECK_EQUAL(3, parser.segment_count());

        BOOST_CHECK_EQUAL(tx1_len, parser[1].size());
        const auto tx1_begin{txs.cbegin() + tx_n_len};
        BOOST_CHECK_EQUAL_COLLECTIONS(parser[1].cbegin(), parser[1].cend(),
                                    tx1_begin, tx1_begin + tx1_len);
        
        BOOST_CHECK_EQUAL(tx2_len, parser[2].size());
        BOOST_CHECK_EQUAL(tx2_len, parser[2].size());
        const auto tx2_begin{txs.cbegin() + tx_n_len + tx1_len};
        BOOST_CHECK_EQUAL_COLLECTIONS(parser[2].cbegin(), parser[2].cend(),
                                    tx2_begin, tx2_begin + tx2_len);
    }

    {
        // Check reports 0, 0 once complete 
        std::vector<uint8_t> txs{42};
        const auto [bytes_read, bytes_reqd] = parser(txs);
        BOOST_CHECK_EQUAL(0, bytes_read);
        BOOST_CHECK_EQUAL(0, bytes_reqd);
    }
}

BOOST_AUTO_TEST_CASE(txs_parser_1_pass)
{
    txs_parser parser;
    std::span s{txs.data(), txs.size()};
    const auto [bytes_read, bytes_reqd] = parser(s);
    BOOST_CHECK_EQUAL(txs.size(), bytes_read);
    BOOST_CHECK_EQUAL(0, bytes_reqd);

    constexpr size_t exp_n_tx{3};
    BOOST_CHECK_EQUAL(exp_n_tx, parser.segment_count());
    BOOST_CHECK_EQUAL(txs.size(), parser.size());
}

BOOST_AUTO_TEST_CASE(txs_parser_2_pass)
{
    txs_parser parser;

    constexpr size_t split_pos{20};
    const auto [bytes_read, bytes_reqd] = parser(std::span{txs.data(), split_pos});
    parser(std::span{txs.data() + bytes_read, txs.size() - bytes_read});

    constexpr size_t exp_n_tx{3};
    BOOST_CHECK_EQUAL(exp_n_tx, parser.segment_count());
    BOOST_CHECK_EQUAL(txs.size(), parser.size());
}

BOOST_AUTO_TEST_CASE(txs_parser_as_reqd)
{
    txs_parser parser;
    size_t total_bytes_read{};
    size_t offset{};
    size_t n{1};
    size_t passes{};
    while(total_bytes_read < txs.size())
    {
        std::span s{txs.data() + offset, n};
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
    BOOST_CHECK_EQUAL(txs.size(), total_bytes_read);
    BOOST_CHECK_EQUAL(total_bytes_read, parser.size());
    BOOST_CHECK_EQUAL(3, parser.segment_count());
    BOOST_CHECK_EQUAL(tx1_len, parser[1].size());
    const auto tx1_begin{txs.cbegin() + tx_n_len};
    BOOST_CHECK_EQUAL_COLLECTIONS(parser[1].cbegin(), parser[1].cend(),
                                  tx1_begin, tx1_begin + tx1_len);
    BOOST_CHECK_EQUAL(tx2_len, parser[2].size());
    const auto tx2_begin{txs.cbegin() + tx_n_len + tx1_len};
    BOOST_CHECK_EQUAL_COLLECTIONS(parser[2].cbegin(), parser[2].cend(),
                                  tx2_begin, tx2_begin + tx2_len);

    BOOST_CHECK_EQUAL(31, passes);
    BOOST_CHECK_EQUAL(txs.size(), parser.size());
}

BOOST_AUTO_TEST_CASE(parse_0_tx_count)
{
    txs_parser parser;
    vector<uint8_t> v(2, 0);
    auto p = parser(span{v.data(), v.size()});
    BOOST_CHECK_EQUAL(1, p.first);
    BOOST_CHECK_EQUAL(0, p.second);
    
    p = parser(span{v.data() + p.first, v.size() - p.first});
    BOOST_CHECK_EQUAL(0, p.first);
    BOOST_CHECK_EQUAL(0, p.second);
}

BOOST_AUTO_TEST_CASE(read_txs)
{
    txs_parser parser;
    std::span s{txs.data(), txs.size()};
    parser(s);
    BOOST_CHECK_EQUAL(3, parser.segment_count());
    BOOST_CHECK_EQUAL(txs.size(), parser.size());

    size_t total_bytes_read{};

    vector<uint8_t> out_0(parser[0].size());
    size_t read_pos{};
    total_bytes_read += read(parser, read_pos, std::span{out_0.data(), out_0.size()});
    BOOST_CHECK_EQUAL(2, out_0[0]);
    BOOST_CHECK_EQUAL(0, parser[0].size());
    BOOST_CHECK_EQUAL(txs.size(), parser.size());
    
    vector<uint8_t> out_1(parser[1].size());
    read_pos += parser[0].size();
    total_bytes_read += read(parser, read_pos, std::span{out_1.data(), out_1.size()});
    BOOST_CHECK_EQUAL(3, out_1[0]);
    BOOST_CHECK_EQUAL(0, parser[1].size());
    
    vector<uint8_t> out_2(parser[2].size());
    read_pos += parser[1].size();
    total_bytes_read += read(parser, read_pos, std::span{out_2.data(), out_2.size()});
    BOOST_CHECK_EQUAL(11, out_2[0]);
    BOOST_CHECK_EQUAL(0, parser[2].size());

    BOOST_CHECK_EQUAL(txs.size(), total_bytes_read);
    BOOST_CHECK_EQUAL(txs.size(), parser.size());
}

BOOST_AUTO_TEST_CASE(read_all)
{
    txs_parser parser;
    std::span s{txs.data(), txs.size()};
    parser(s);
    BOOST_CHECK_EQUAL(txs.size(), size(parser));

    vector<uint8_t> out(txs.size());
    const auto bytes_read = read(parser, 0, std::span{out.data(), out.size()});
    BOOST_CHECK_EQUAL(out.size(), bytes_read);
    BOOST_CHECK_EQUAL_COLLECTIONS(txs.cbegin(), txs.cend(),
                                  out.cbegin(), out.cend());
    BOOST_CHECK_EQUAL(txs.size(), size(parser));
}

BOOST_AUTO_TEST_CASE(read_byte_by_byte)
{
    txs_parser parser;
    std::span s{txs.data(), txs.size()};
    parser(s);

    const size_t out_size{txs.size()};
    vector<uint8_t> out(out_size);
    size_t total_bytes_read{};
    for(size_t i{}; i < out.size(); ++i)
    {
        total_bytes_read += read(parser, i, std::span{out.data() + i, 1});
    }
    BOOST_CHECK_EQUAL(out.size(), total_bytes_read);
    BOOST_CHECK_EQUAL_COLLECTIONS(txs.cbegin(), txs.cend(),
                                  out.cbegin(), out.cend());
}

BOOST_AUTO_TEST_CASE(read_beyond_parser_size)
{
    txs_parser parser;
    std::span s{txs.data(), txs.size()};
    parser(s);

    vector<uint8_t> out(txs.size() + 1);
    const size_t bytes_read = read(parser, 0, std::span{out.data(), out.size()});
    BOOST_CHECK_EQUAL(txs.size(), bytes_read);
    BOOST_CHECK_EQUAL_COLLECTIONS(txs.cbegin(), txs.cend(),
                                  out.cbegin(), out.cend() - 1);
}

BOOST_AUTO_TEST_SUITE_END()
   
