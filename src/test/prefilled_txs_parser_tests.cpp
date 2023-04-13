// Copyright (c) 2023 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file

#include <boost/test/unit_test.hpp>

#include "net/array_parser.h"
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

using prefilled_txs_parser = array_parser<prefilled_tx_parser>;

BOOST_AUTO_TEST_SUITE(prefilled_txs_parser_tests)

BOOST_AUTO_TEST_CASE(parse_empty_input)
{
    prefilled_txs_parser parser;

    vector<uint8_t> ip;
    std::span s{ip.data(), 0};
    const auto [bytes_read, bytes_reqd] = parser(s);
    BOOST_CHECK_EQUAL(0, bytes_read);
    BOOST_CHECK_EQUAL(var_int_len_1, bytes_reqd);
    BOOST_CHECK_EQUAL(0, parser.size());
}

BOOST_AUTO_TEST_CASE(parse_count_0)
{
    prefilled_txs_parser parser;
    vector<uint8_t> ip;
    ip.push_back(0);
    std::span s{ip.data(), ip.size()};
    const auto [bytes_read, bytes_reqd] = parser(s);
    BOOST_CHECK_EQUAL(ip.size(), bytes_read);
    BOOST_CHECK_EQUAL(0, bytes_reqd);
    BOOST_CHECK_EQUAL(ip.size(), parser.size());
}

BOOST_AUTO_TEST_CASE(parse_count_1)
{
    prefilled_txs_parser parser;
    vector<uint8_t> ip;
    ip.push_back(1);
    const size_t index{42};
    ip.push_back(index);
    ip.insert(ip.cend(), tx.cbegin(), tx.cend());
    std::span s{ip.data(), ip.size()};
    const auto [bytes_read, bytes_reqd] = parser(s);
    BOOST_CHECK_EQUAL(ip.size(), bytes_read);
    BOOST_CHECK_EQUAL(0, bytes_reqd);
    BOOST_CHECK_EQUAL(ip.size(), parser.size());
}

BOOST_AUTO_TEST_SUITE_END()

