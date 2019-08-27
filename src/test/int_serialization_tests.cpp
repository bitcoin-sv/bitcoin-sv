// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#include "script/int_serialization.h"

#include <list>
#include <vector>

#include "big_int.hpp"

#include "bn_helpers.h"

#include <boost/test/unit_test.hpp>

using namespace std;
using namespace bsv;

namespace
{
    using int64_t_test_data_type = std::vector<std::pair<int64_t, std::vector<uint8_t>>>;
    // clang-format off
    int64_t_test_data_type int64_t_test_data = 
    {
        {1, {1}},
        {std::numeric_limits<int8_t>::max()-1, {0x7e}}, // 126
        {std::numeric_limits<int8_t>::max(), {0x7f}}, // 127
        {255, {0xff, 0x0}},
        {256, {0x0, 0x1}},
        {257, {0x1,0x1}},
        {std::numeric_limits<int8_t>::max()+1, {0x80, 0x0}}, // 128
        {std::numeric_limits<int16_t>::max()-1, {0xfe, 0x7f}},
        {std::numeric_limits<int16_t>::max(), {0xff, 0x7f}},
        {std::numeric_limits<int16_t>::max()+1, {0x0, 0x80, 0x0}},
        {std::numeric_limits<int32_t>::max()-1, {0xfe, 0xff, 0xff, 0x7f}},
        {std::numeric_limits<int32_t>::max(), {0xff, 0xff, 0xff, 0x7f}},
        {std::numeric_limits<int32_t>::max()+1L, {0x0, 0x0, 0x0, 0x80, 0x0}},
        {std::numeric_limits<int64_t>::max()-1, {0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f}},
        {std::numeric_limits<int64_t>::max(), {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f}},

        {-1, {0x81}},
        {std::numeric_limits<int8_t>::min()+1, {0xff}}, // -127
        {std::numeric_limits<int8_t>::min(), {0x80, 0x80}}, // -128
        {std::numeric_limits<int8_t>::min()-1, {0x81, 0x80}}, // -129
        {-255, {0xff, 0x80}},
        {-256, {0x0 , 0x81}},
        {-257, {0x1 , 0x81}},
        {std::numeric_limits<int16_t>::min()+1, {0xff, 0xff}},
        {std::numeric_limits<int16_t>::min(), {0x0, 0x80, 0x80}},
        {std::numeric_limits<int16_t>::min()-1, {0x01, 0x80, 0x80}},
        
        {std::numeric_limits<int32_t>::min()+1, {0xff, 0xff, 0xff, 0xff}},
        {std::numeric_limits<int32_t>::min(), {0x0, 0x0, 0x0, 0x80, 0x80}},
        {std::numeric_limits<int32_t>::min()-1L, {0x1, 0x0, 0x0, 0x80, 0x80}},
        
        {std::numeric_limits<int64_t>::min()+1, {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}},
        {std::numeric_limits<int64_t>::min(), {0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x80, 0x80}},
        
        {486'604'799, {0xff , 0xff, 0x0, 0x1D}},
        {2'150'637'584, {0x10, 0x20, 0x30, 0x80, 0x0}},
    };
    
    const bint bn_min64{numeric_limits<int64_t>::min()};
    const bint bn_max64{numeric_limits<int64_t>::max()};
    using bint_test_data_type = std::vector<std::pair<bint, std::vector<uint8_t>>>;
    bint_test_data_type bint_test_data = 
    {
        {bn_max64, {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f}},
        {bn_max64 + 1, {0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x80, 0x0}},
        {bn_max64 + bn_max64, {0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x0}},
        {bn_max64 * bn_max64, {0x1, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 
                               0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x3f}},
        
        {bn_min64, {0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x80, 0x80}},
        {bn_min64 + bn_min64, {0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x81}},
    };
    // clang-format on
}

BOOST_AUTO_TEST_SUITE(int_serialization_tests)

BOOST_AUTO_TEST_CASE(serialize_int64_t)
{
    for(const auto& [n, s] : int64_t_test_data)
    {
        std::vector<uint8_t> op;
        op.reserve(sizeof(n));
        serialize(n, back_inserter(op));
        BOOST_CHECK_EQUAL_COLLECTIONS(begin(s), end(s), begin(op), end(op));

        const auto ip{deserialize<int64_t>(op.begin(), op.end())};
        BOOST_CHECK_EQUAL(n, ip);
    }
}

BOOST_AUTO_TEST_CASE(serialize_bint)
{
    using namespace bsv;

    for(const auto& [n, s] : int64_t_test_data)
    {
        std::vector<uint8_t> op;
        op.reserve(sizeof(n));
        serialize(bint{n}, back_inserter(op));
        BOOST_CHECK_EQUAL_COLLECTIONS(begin(s), end(s), begin(op), end(op));

        const auto ip{deserialize<bint>(op.begin(), op.end())};
        BOOST_CHECK_EQUAL(bint{n}, ip);
    }

    for(const auto& [n, s] : int64_t_test_data)
    {
        std::list<uint8_t> op;
        serialize(bint{n}, back_inserter(op));
        BOOST_CHECK_EQUAL_COLLECTIONS(begin(s), end(s), begin(op), end(op));

        const auto ip{deserialize<bint>(op.begin(), op.end())};
        BOOST_CHECK_EQUAL(bint{n}, ip);
    }

    for(const auto& [n, s] : bint_test_data)
    {
        std::vector<uint8_t> op;
        op.reserve(sizeof(n));
        serialize(n, back_inserter(op));
        BOOST_CHECK_EQUAL_COLLECTIONS(begin(s), end(s), begin(op), end(op));

        const auto ip{deserialize<bint>(op.begin(), op.end())};
        BOOST_CHECK_EQUAL(n, ip);
    }
}

BOOST_AUTO_TEST_CASE(very_big_number)
{
    const auto n = power_binary(bint{2}, std::multiplies<bint>(),
                                15); // bn = 2^(2^15) == 2^32,768
    std::vector<uint8_t> op;
    op.reserve(n.size_bytes());
    serialize(n, back_inserter(op));
    BOOST_CHECK_EQUAL(4097, op.size());

    const auto ip{deserialize<bint>(op.begin(), op.end())};
    BOOST_CHECK_EQUAL(n, ip);
}

BOOST_AUTO_TEST_SUITE_END()

