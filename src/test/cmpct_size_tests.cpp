// Copyright (c) 2023 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file

#include <cstddef>
#include <cstdint>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "net/cmpct_size.h"

using namespace std;

BOOST_AUTO_TEST_SUITE(parser_compact_size_tests)

BOOST_AUTO_TEST_CASE(parse_compact_size_test)
{
    // clang-format off
    vector<tuple<vector<uint8_t>, size_t, size_t>> v
    {
        {{}, 0, 1},
        {{0xfd}, 0, 3},
        {{0xfe}, 0, 5},
        {{0xff}, 0, 9},

        {{0x0}, 1, 0},
        {{0x1}, 1, 1},
        {{0xfc}, 1, 0xfc},
            
        {{0xfd, 0xfd, 0x0}, 3, 0xfd},
        {{0xfd, 0x12, 0x34}, 3, 0x3412},
        {{0xfd, 0xff, 0xff}, 3, 0xffff},

        {{0xfe, 0x0, 0x0, 0x1, 0x0}, 5, 0x1'0000},
        {{0xfe, 0x12, 0x34, 0x56, 0x78}, 5, 0x7856'3412},
        {{0xfe, 0xff, 0xff, 0xff, 0xff}, 5, 0xffff'ffff},

        {{0xff, 0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x1,  0x0}, 9, 0x1'0000'0000'0000},
        {{0xff, 0x12, 0x34, 0x56, 0x78, 0x90, 0xab, 0xcd, 0xef}, 9, 0xefcd'ab90'7856'3412},
        {{0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}, 9, 0xffff'ffff'ffff'ffff},
    };
    // clang-format on

    for(const auto& [ ip, exp_bytes_read, exp_value ] : v)
    {
        const auto [bytes_read, value] = parse_compact_size(ip); 
        BOOST_CHECK_EQUAL(exp_bytes_read, bytes_read);
        BOOST_CHECK_EQUAL(exp_value, value);
    }
}

BOOST_AUTO_TEST_SUITE_END()

