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
    std::vector<uint8_t> txn;

    txn.insert(txn.end(), version_len, 3);    // txn version
    txn.push_back(4);                         // n inputs 
    
    // ip 1
    txn.insert(txn.end(), outpoint_len, 4);   // txn outpoint 
    txn.push_back(1);                         // script length
    txn.push_back(0x6a);                      // script (op_return)
    txn.insert(txn.end(), seq_len, 5);        // sequence
    // ip 2
    txn.insert(txn.end(), outpoint_len, 6);   // txn outpoint 
    txn.push_back(0xfd);                      // script length encoded 2 bytes
    txn.push_back(2);                         // script length little endian 
    txn.push_back(0);                         // 
    txn.push_back(0x6a);                      // script (op_return)
    txn.push_back(0x6a);                      // script (op_return)
    txn.insert(txn.end(), seq_len, 7);        // sequence
    // ip 3
    txn.insert(txn.end(), outpoint_len, 12);  // txn outpoint 
    txn.push_back(0xfe);                      // script length encoded 4 bytes
    txn.push_back(3);                         // script length
    txn.push_back(0);                         // script length
    txn.push_back(0);                         // script length
    txn.push_back(0);                         // script length
    txn.push_back(0x6a);                      // script (op_return)
    txn.push_back(0x6a);                      // script (op_return)
    txn.push_back(0x6a);                      // script (op_return)
    txn.insert(txn.end(), seq_len, 13);       // sequence
    // ip 4
    txn.insert(txn.end(), outpoint_len, 14);  // txn outpoint 
    txn.push_back(0xff);                      // script length encoded 4 bytes
    txn.push_back(4);                         // script length
    txn.push_back(0);                         // script length
    txn.push_back(0);                         // script length
    txn.push_back(0);                         // script length
    txn.push_back(0);                         // script length
    txn.push_back(0);                         // script length
    txn.push_back(0);                         // script length
    txn.push_back(0);                         // script length
    txn.push_back(0x6a);                      // script (op_return)
    txn.push_back(0x6a);                      // script (op_return)
    txn.push_back(0x6a);                      // script (op_return)
    txn.push_back(0x6a);                      // script (op_return)
    txn.insert(txn.end(), seq_len, 15);       // sequence

    txn.push_back(4);                         // number of outputs
    // op 1
    txn.insert(txn.end(), value_len, 8);      // value
    txn.push_back(1);                         // script length
    txn.push_back(0x6a);                      // script (op_return)
    // op 2
    txn.insert(txn.end(), value_len, 9);      // value
    txn.push_back(0xfd);                      // script length encoded 2 bytes
    txn.push_back(2);                         // script length
    txn.push_back(0);                         // script length
    txn.push_back(0x6a);                      // script (op_return)
    txn.push_back(0x6a);                      // script (op_return)
    // op 3
    txn.insert(txn.end(), value_len, 16);     // value
    txn.push_back(0xfe);                      // script length
    txn.push_back(3);                         // script length
    txn.push_back(0);                         // script length
    txn.push_back(0);                         // script length
    txn.push_back(0);                         // script length
    txn.push_back(0x6a);                      // script (op_return)
    txn.push_back(0x6a);                      // script (op_return)
    txn.push_back(0x6a);                      // script (op_return)
    
    // op 4
    txn.insert(txn.end(), value_len, 17);     // value
    txn.push_back(0xff);                      // script length
    txn.push_back(4);                         // script length
    txn.push_back(0);                         // script length
    txn.push_back(0);                         // script length
    txn.push_back(0);                         // script length
    txn.push_back(0);                         // script length
    txn.push_back(0);                         // script length
    txn.push_back(0);                         // script length
    txn.push_back(0);                         // script length
    txn.push_back(0x6a);                      // script (op_return)
    txn.push_back(0x6a);                      // script (op_return)
    txn.push_back(0x6a);                      // script (op_return)
    txn.push_back(0x6a);                      // script (op_return)

    // locktime
    txn.insert(txn.end(), locktime_len, 10);  // lock time

    return txn;
}();

using prefilled_txs_parser = array_parser<prefilled_tx_parser>;

BOOST_AUTO_TEST_SUITE(prefilled_txs_parser_tests)

BOOST_AUTO_TEST_CASE(parse_empty_input)
{
    prefilled_txs_parser parser;

    vector<uint8_t> ip;
    std::span s{ip.data(), 0};
    const auto [bytes_read, bytes_reqd] = parser(s);
    BOOST_CHECK_EQUAL(0U, bytes_read);
    BOOST_CHECK_EQUAL(var_int_len_1, bytes_reqd);
    BOOST_CHECK_EQUAL(0U, parser.size());
}

BOOST_AUTO_TEST_CASE(parse_count_0)
{
    prefilled_txs_parser parser;
    vector<uint8_t> ip;
    ip.push_back(0);
    std::span s{ip.data(), ip.size()};
    const auto [bytes_read, bytes_reqd] = parser(s);
    BOOST_CHECK_EQUAL(ip.size(), bytes_read);
    BOOST_CHECK_EQUAL(0U, bytes_reqd);
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
    BOOST_CHECK_EQUAL(0U, bytes_reqd);
    BOOST_CHECK_EQUAL(ip.size(), parser.size());
}

BOOST_AUTO_TEST_SUITE_END()

