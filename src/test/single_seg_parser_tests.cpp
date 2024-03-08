// Copyright (c) 2023 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE

#include <boost/test/unit_test.hpp>
#include <boost/test/unit_test_suite.hpp>

#include <numeric>

#include "net/single_seg_parser.h"
#include "net/msg_parser.h"
#include "net/p2p_msg_lengths.h"

using namespace std;
using namespace bsv;

BOOST_AUTO_TEST_SUITE(single_seg_parser_tests)

BOOST_AUTO_TEST_CASE(default_construction)
{
    single_seg_parser parser;
    BOOST_CHECK_EQUAL(0, parser.size());
}

BOOST_AUTO_TEST_CASE(default_move)
{
    single_seg_parser a;
    BOOST_CHECK_EQUAL(0, a.size());

    single_seg_parser b{std::move(a)};
    BOOST_CHECK_EQUAL(0, a.size());
    BOOST_CHECK_EQUAL(0, b.size());
}

BOOST_AUTO_TEST_CASE(single_seg_parser_lvalue)
{
    single_seg_parser dp;
    msg_parser parser{dp};
    BOOST_CHECK_EQUAL(0, parser.size());

    vector<uint8_t> v(42);
    iota(v.begin(), v.end(), 0);
    parser(std::span{v.data(), v.size()});
    BOOST_CHECK_EQUAL(42, parser.size());
}

BOOST_AUTO_TEST_CASE(single_seg_parser_xvalue)
{
    single_seg_parser dp;
    msg_parser parser{std::move(dp)};
    BOOST_CHECK_EQUAL(0, parser.size());

    vector<uint8_t> v(42);
    iota(v.begin(), v.end(), 0);
    parser(std::span{v.data(), v.size()});
    BOOST_CHECK_EQUAL(42, parser.size());
}

BOOST_AUTO_TEST_CASE(single_seg_parser_prvalue)
{
    msg_parser parser{single_seg_parser{}};
    BOOST_CHECK_EQUAL(0, parser.size());

    vector<uint8_t> v(42);
    iota(v.begin(), v.end(), 0);
    parser(std::span{v.data(), v.size()});
    BOOST_CHECK_EQUAL(42, parser.size());
}
            
BOOST_AUTO_TEST_CASE(single_seg_parser_ptr)
{
    single_seg_parser x;
    auto parser = make_unique<msg_parser>(x);
    BOOST_CHECK_EQUAL(0, parser->size());
    
    vector<uint8_t> v(42);
    iota(v.begin(), v.end(), 0);
    (*parser)(std::span{v.data(), v.size()});
    BOOST_CHECK_EQUAL(42, parser->size());
}

static const std::vector<uint8_t> large_txs = []
{
    std::vector<uint8_t> txs;

    txs.insert(txs.end(), 1, 1);              // tx count
    
    // tx 1
    txs.insert(txs.end(), version_len, 3);    // tx version
    
    constexpr size_t n_ips{0x3e8};
    txs.push_back(0xfd);
    txs.push_back(0xe8);
    txs.push_back(0x3);
    for(size_t i{}; i < n_ips; ++i)
    {
        // ip 1
        txs.insert(txs.end(), outpoint_len, 4);   // tx outpoint 
        txs.push_back(0xfd);                      // script length
        txs.push_back(0xff);                    // script length
        txs.push_back(0xff);                    // script length
        txs.insert(txs.cend(), 0xffff, 0x6a);     // script (op_return)
        txs.insert(txs.end(), seq_len, 5);        // sequence
    }

    constexpr size_t n_ops{0x3e8};
    txs.push_back(0xfd);
    txs.push_back(0xe8);
    txs.push_back(0x3);
    for(size_t i{}; i < n_ops; ++i)
    {
        txs.insert(txs.end(), value_len, 8);      // value
        txs.push_back(0xfd);                      // script length
        txs.push_back(0xff);                    // script length
        txs.push_back(0xff);                    // script length
        txs.insert(txs.cend(), 0xffff, 0x6a);     // script (op_return)
    }

    txs.insert(txs.end(), locktime_len, 10);  // lock time

    return txs;
}();

BOOST_AUTO_TEST_CASE(parse_large_outputs)
{
    single_seg_parser parser;
    std::span s{large_txs.data(), large_txs.size()};
    const auto [bytes_read, bytes_reqd] = parser(s);
    //BOOST_CHECK_EQUAL(large_txs.size(), bytes_read);
    BOOST_CHECK_EQUAL(0, bytes_reqd);

    //BOOST_CHECK_EQUAL(0, parser.size());
    //BOOST_CHECK_EQUAL(large_txs.size(), parser.size());
}

BOOST_AUTO_TEST_SUITE_END()
