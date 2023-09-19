// Copyright (c) 2023 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#include <boost/test/tools/old/interface.hpp>
#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <span>
#include <vector>

#include "mod_n_byte_parser.h"

#include "net/array_parser.h"

using namespace std;

BOOST_AUTO_TEST_SUITE(array_parser_tests)

BOOST_AUTO_TEST_CASE(default_construction)
{
    array_parser<mod_n_byte_parser<10, 10>> parser;
    BOOST_CHECK(parser.empty());
    BOOST_CHECK_EQUAL(0, parser.size());
    BOOST_CHECK_EQUAL(0, parser.segment_count());
}

BOOST_AUTO_TEST_CASE(parse_empty)
{
    array_parser<mod_n_byte_parser<1, 1>> parser;

    const vector<uint8_t> ip;
    std::span s{ip.data(), ip.size()};
    const auto [bytes_read, bytes_reqd] = parser(s);
    BOOST_CHECK_EQUAL(ip.size(), bytes_read);
    BOOST_CHECK_EQUAL(1, bytes_reqd);
    BOOST_CHECK(parser.empty());
    BOOST_CHECK_EQUAL(ip.size(), parser.size());
    BOOST_CHECK_EQUAL(0, parser.segment_count());
}

BOOST_AUTO_TEST_CASE(parse_1_item)
{
    array_parser<mod_n_byte_parser<1, 1>> parser;

    const vector<uint8_t> ip{1, 2};
    std::span s{ip.data(), ip.size()};
    const auto [bytes_read, bytes_reqd] = parser(s);
    BOOST_CHECK_EQUAL(ip.size(), bytes_read);
    BOOST_CHECK_EQUAL(0, bytes_reqd);
    BOOST_CHECK(!parser.empty());
    BOOST_CHECK_EQUAL(ip.size(), parser.size());
    BOOST_CHECK_EQUAL(2, parser.segment_count());
}

BOOST_AUTO_TEST_CASE(parse_2_items)
{
    array_parser<mod_n_byte_parser<1, 1>> parser;

    const vector<uint8_t> ip{2, 3, 4};
    std::span s{ip.data(), ip.size()};
    const auto [bytes_read, bytes_reqd] = parser(s);
    BOOST_CHECK_EQUAL(ip.size(), bytes_read);
    BOOST_CHECK_EQUAL(0, bytes_reqd);
    BOOST_CHECK(!parser.empty());
    BOOST_CHECK_EQUAL(ip.size(), parser.size());
    BOOST_CHECK_EQUAL(3, parser.segment_count());
}

BOOST_AUTO_TEST_CASE(parse_max_items)
{
    array_parser<mod_n_byte_parser<1, 1>> parser;

    const vector<uint8_t> ip{0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                             0x1};
    std::span s{ip.data(), ip.size()};
    const auto [bytes_read, bytes_reqd] = parser(s);
    BOOST_CHECK_EQUAL(ip.size(), bytes_read);
    BOOST_CHECK_EQUAL(0, bytes_reqd);
    BOOST_CHECK(!parser.empty());
    BOOST_CHECK_EQUAL(ip.size(), parser.size());
    BOOST_CHECK_EQUAL(2, parser.segment_count());
}

BOOST_AUTO_TEST_CASE(parse_many_items)
{
    array_parser<mod_n_byte_parser<1, 1>> parser;

    const vector<uint8_t> ip{0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x0f,
                             0x1};
    std::span s{ip.data(), ip.size()};
    const auto [bytes_read, bytes_reqd] = parser(s);
    BOOST_CHECK_EQUAL(ip.size(), bytes_read);
    BOOST_CHECK_EQUAL(0, bytes_reqd);
    BOOST_CHECK(!parser.empty());
    BOOST_CHECK_EQUAL(ip.size(), parser.size());
    BOOST_CHECK_EQUAL(2, parser.segment_count());
}

BOOST_AUTO_TEST_CASE(parse_partial_segment)
{
    array_parser<mod_n_byte_parser<2, 3>> parser;

    const vector<uint8_t> ip{2, 3, 4, 5};
    std::span s{ip.data(), ip.size()};
    const auto [bytes_read, bytes_reqd] = parser(s);
    BOOST_CHECK_EQUAL(3, bytes_read);
    BOOST_CHECK_EQUAL(2, bytes_reqd);
    BOOST_CHECK(!parser.empty());
    BOOST_CHECK_EQUAL(3, parser.size());
    BOOST_CHECK_EQUAL(1, parser.segment_count());
}

BOOST_AUTO_TEST_CASE(seg_offset)
{
    array_parser<mod_n_byte_parser<1, 2>> parser;

    const vector<uint8_t> ip{2, 1, 2, 3, 4};
    std::span s{ip.data(), ip.size()};
    const auto [bytes_read, bytes_reqd] = parser(s);
    BOOST_CHECK_EQUAL(ip.size(), bytes_read);
    BOOST_CHECK_EQUAL(0, bytes_reqd);
    BOOST_CHECK_EQUAL(ip.size(), parser.size());
    BOOST_CHECK_EQUAL(3, parser.segment_count());

    const auto [seg0, byte0] = parser.seg_offset(0);
    BOOST_CHECK_EQUAL(0, seg0);
    BOOST_CHECK_EQUAL(0, byte0);
    
    const auto [seg1, byte1] = parser.seg_offset(1);
    BOOST_CHECK_EQUAL(1, seg1);
    BOOST_CHECK_EQUAL(0, byte1);
    
    const auto [seg2, byte2] = parser.seg_offset(2);
    BOOST_CHECK_EQUAL(1, seg2);
    BOOST_CHECK_EQUAL(1, byte2);
    
    const auto [seg3, byte3] = parser.seg_offset(3);
    BOOST_CHECK_EQUAL(2, seg3);
    BOOST_CHECK_EQUAL(0, byte3);
    
    const auto [seg4, byte4] = parser.seg_offset(4);
    BOOST_CHECK_EQUAL(2, seg4);
    BOOST_CHECK_EQUAL(1, byte4);
}

BOOST_AUTO_TEST_SUITE_END()

