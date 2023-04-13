// Copyright (c) 2023 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file

#include "net/fixed_len_parser.h"

#include <cstddef>
#include <cstdint>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "net/p2p_msg_lengths.h"

using namespace std;
using namespace bsv;

BOOST_AUTO_TEST_SUITE(fixed_len_parser_tests)

BOOST_AUTO_TEST_CASE(construction)
{
    constexpr size_t arbitary_len{42};
    fixed_len_parser parser{arbitary_len};
    BOOST_CHECK(parser.empty());
    BOOST_CHECK_EQUAL(0, parser.size());
}

BOOST_AUTO_TEST_CASE(fixed)
{
    constexpr size_t arbitary_len{42};
    constexpr size_t arbitary_value{101};
    vector<uint8_t> msg(arbitary_len + 1, arbitary_value);

    {
        // msg < arb_len
        fixed_len_parser parser{arbitary_len};
        constexpr size_t n{1};
        const std::span s{msg.data(), arbitary_len - n};
        const auto [bytes_read, bytes_reqd] = parser(s);
        BOOST_CHECK_EQUAL(arbitary_len - n, bytes_read);
        BOOST_CHECK_EQUAL(n, bytes_reqd);
        BOOST_CHECK(!parser.empty());
        BOOST_CHECK_EQUAL(bytes_read, parser.size());
    }
    
    {
        // msg == arb_len
        fixed_len_parser parser{arbitary_len};
        const std::span s{msg.data(), arbitary_len};
        const auto [bytes_read, bytes_reqd] = parser(s);
        BOOST_CHECK_EQUAL(arbitary_len, bytes_read);
        BOOST_CHECK_EQUAL(0, bytes_reqd);
        BOOST_CHECK(!parser.empty());
        BOOST_CHECK_EQUAL(bytes_read, parser.size());
    }
    
    {
        // msg > arb_len
        fixed_len_parser parser{arbitary_len};
        const std::span s{msg.data(), arbitary_len + 1};
        const auto [bytes_read, bytes_reqd] = parser(s);
        BOOST_CHECK_EQUAL(arbitary_len, bytes_read);
        BOOST_CHECK_EQUAL(0, bytes_reqd);
        BOOST_CHECK(!parser.empty());
        BOOST_CHECK_EQUAL(bytes_read, parser.size());
    }
}

BOOST_AUTO_TEST_CASE(byte_by_byte)
{
    constexpr size_t arbitary_len{10};
    constexpr size_t arbitary_value{42};
    vector<uint8_t> msg(arbitary_len, arbitary_value);

    fixed_len_parser parser{arbitary_len};
    for(size_t i{}; i < arbitary_len; ++i)
    {
        const std::span s{msg.data() + i, 1};
        const auto [bytes_read, bytes_reqd] = parser(s);
        BOOST_CHECK_EQUAL(1, bytes_read);
        BOOST_CHECK_EQUAL(arbitary_len - i - 1, bytes_reqd);
        BOOST_CHECK(!parser.empty());
        BOOST_CHECK_EQUAL(i + 1, parser.size());
    }
}

BOOST_AUTO_TEST_SUITE_END()

