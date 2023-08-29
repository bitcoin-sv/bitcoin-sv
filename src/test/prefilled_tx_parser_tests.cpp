// Copyright (c) 2023 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file

//#include <algorithm>
//#include <cstddef>
//#include <cstdint>
//#include <iterator>
//#include <vector>

#include <boost/test/tools/old/interface.hpp>
#include <boost/test/unit_test.hpp>

#include "net/prefilled_tx_parser.h"
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

BOOST_AUTO_TEST_SUITE(prefilled_tx_parser_tests)

BOOST_AUTO_TEST_CASE(parse_empty_input)
{
    vector<uint8_t> ip;
    std::span s{ip.data(), 0};
    prefilled_tx_parser parser;
    const auto [bytes_read, bytes_reqd] = parser(s);
    BOOST_CHECK_EQUAL(0, bytes_read);
    BOOST_CHECK_EQUAL(var_int_len_1, bytes_reqd);
    BOOST_CHECK_EQUAL(0, parser.size());
}

BOOST_AUTO_TEST_CASE(parse_index_var_int_len_3)
{
    vector<uint8_t> ip{0xfd};
    std::span s{ip.data(), ip.size()};
    prefilled_tx_parser parser;
    const auto [bytes_read, bytes_reqd] = parser(s);
    BOOST_CHECK_EQUAL(0, bytes_read);
    BOOST_CHECK_EQUAL(var_int_len_3, bytes_reqd);
    BOOST_CHECK_EQUAL(0, parser.size());
}

BOOST_AUTO_TEST_CASE(parse_index_var_int_len_5)
{
    vector<uint8_t> ip{0xfe};
    std::span s{ip.data(), ip.size()};
    prefilled_tx_parser parser;
    const auto [bytes_read, bytes_reqd] = parser(s);
    BOOST_CHECK_EQUAL(0, bytes_read);
    BOOST_CHECK_EQUAL(var_int_len_5, bytes_reqd);
    BOOST_CHECK_EQUAL(0, parser.size());
}

BOOST_AUTO_TEST_CASE(parse_index_var_int_len_9)
{
    vector<uint8_t> ip{0xff};
    std::span s{ip.data(), ip.size()};
    prefilled_tx_parser parser;
    const auto [bytes_read, bytes_reqd] = parser(s);
    BOOST_CHECK_EQUAL(0, bytes_read);
    BOOST_CHECK_EQUAL(var_int_len_9, bytes_reqd);
    BOOST_CHECK_EQUAL(0, parser.size());
}

BOOST_AUTO_TEST_CASE(parse_index_var_int_len_1)
{
    vector<uint8_t> ip{42};
    std::span s{ip.data(), ip.size()};
    prefilled_tx_parser parser;
    const auto [bytes_read, bytes_reqd] = parser(s);
    BOOST_CHECK_EQUAL(1, bytes_read);
    BOOST_CHECK_EQUAL(bsv::version_len, bytes_reqd);
    BOOST_CHECK_EQUAL(1, parser.size());
}

BOOST_AUTO_TEST_CASE(parse_in_one_pass)
{
    prefilled_tx_parser parser;
    vector<uint8_t> ip;
    const size_t index{42};
    ip.push_back(index);
    ip.insert(ip.end(), tx.cbegin(), tx.cend());
    std::span s{ip.data(), ip.size()};
    const auto [bytes_read, bytes_reqd] = parser(s);
    BOOST_CHECK_EQUAL(ip.size(), bytes_read);
    BOOST_CHECK_EQUAL(0, bytes_reqd); 
    BOOST_CHECK_EQUAL(ip.size(), parser.size());

    unique_array a{std::move(parser).buffer()};
    BOOST_CHECK_EQUAL(ip.size(), a.size());
    BOOST_CHECK_EQUAL_COLLECTIONS(ip.cbegin(), ip.cend(),
                                  a.cbegin(), a.cend());
}

BOOST_AUTO_TEST_CASE(parse_as_reqd)
{
    prefilled_tx_parser parser;
    vector<uint8_t> ip;
    ip.push_back(42); // index
    ip.insert(ip.end(), tx.cbegin(), tx.cend());
    size_t total_bytes_read{};
    size_t offset{};
    size_t n{1};
    size_t passes{};
    while(total_bytes_read < ip.size())
    {
        std::span s{ip.data() + offset, n};
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
    BOOST_CHECK_EQUAL(ip.size(), total_bytes_read);
    BOOST_CHECK_EQUAL(ip.size(), parser.size());
    BOOST_CHECK_EQUAL(27, passes);
}

BOOST_AUTO_TEST_SUITE_END()

