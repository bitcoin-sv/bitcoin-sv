// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#include "big_int.h"
#include "consensus/consensus.h"

#include <array>

#include <boost/test/unit_test.hpp>
#include <climits>
#include "compiler_warnings.h"

using namespace std;
using bsv::bint;

constexpr int int_min{numeric_limits<int>::min()};
constexpr int int_max{numeric_limits<int>::max()};

constexpr int64_t int64_min{numeric_limits<int64_t>::min()};
constexpr int64_t int64_max{numeric_limits<int64_t>::max()};

constexpr size_t size_t_min{numeric_limits<size_t>::min()};
constexpr size_t size_t_max{numeric_limits<size_t>::max()};

BOOST_AUTO_TEST_SUITE(bint_tests)

BOOST_AUTO_TEST_CASE(default_construction)
{
    bint assignable;
    assignable = bint{1};
    BOOST_CHECK_EQUAL(bint{1}, assignable);
    bint destructible;
}

BOOST_AUTO_TEST_CASE(int_construction)
{
    BOOST_CHECK_EQUAL(0, bint{0});
    BOOST_CHECK_EQUAL(1, bint{1});
    BOOST_CHECK_EQUAL(-1, bint{-1});
    BOOST_CHECK_EQUAL(int_max, bint{int_max});
    BOOST_CHECK_EQUAL(int_min, bint{int_min});
}

BOOST_AUTO_TEST_CASE(int64_t_construction)
{
    BOOST_CHECK_EQUAL(0, bint{0});
    BOOST_CHECK_EQUAL(1, bint{1});
    BOOST_CHECK_EQUAL(-1, bint{-1});
    BOOST_CHECK_EQUAL(int64_max, bint{int64_max});
    BOOST_CHECK_EQUAL(int64_min, bint{int64_min});
}

BOOST_AUTO_TEST_CASE(size_t_construction)
{
    BOOST_CHECK_EQUAL(size_t_max, bint{size_t_max});
    BOOST_CHECK_EQUAL(size_t_min, bint{size_t_min});
}

BOOST_AUTO_TEST_CASE(is_negative_)
{
    BOOST_CHECK(!is_negative(bint{0}));
    BOOST_CHECK(!is_negative(bint{1}));
    BOOST_CHECK(is_negative(bint{-1}));
}

BOOST_AUTO_TEST_CASE(equality)
{
    array<bint, 3> v = {bint{1}, bint{0}, bint{-1}};
    for(const auto& n : v)
    {
        bint a{n};

        // reflexivity
        BOOST_CHECK_EQUAL(a, a);
        BOOST_CHECK(!(a != a));

        // symmetry
        bint b{n};
        BOOST_CHECK_EQUAL(a, b);
        BOOST_CHECK(!(a != b));

        // transitivity
        bint c{n};
        BOOST_CHECK_EQUAL(a, b);
        BOOST_CHECK_EQUAL(b, c);
        BOOST_CHECK_EQUAL(c, a);
        BOOST_CHECK(!(a != b));
        BOOST_CHECK(!(b != c));
        BOOST_CHECK(!(c != a));
    }
}

BOOST_AUTO_TEST_CASE(inequality)
{
    bint a{1};
    bint b{2};
    BOOST_CHECK(!(a == b));
    BOOST_CHECK_NE(a, b);
}

BOOST_AUTO_TEST_CASE(cmp)
{
    bint a{1};
    BOOST_CHECK(!(a < a));
    BOOST_CHECK_LE(a, a);
    BOOST_CHECK_GE(a, a);
    BOOST_CHECK(!(a > a));

    bint b{2};
    BOOST_CHECK_LT(a, b);
    BOOST_CHECK_LE(a, b);
    BOOST_CHECK(!(a > b));
    BOOST_CHECK(!(a >= b));
}

BOOST_AUTO_TEST_CASE(move_construct)
{
    bint a{1};
    bint b{std::move(a)};
    BOOST_CHECK_EQUAL(bint{1}, b);
}

BOOST_AUTO_TEST_CASE(move_assign)
{
    bint a{1};
    bint b{2};
    b = std::move(a);
    BOOST_CHECK_EQUAL(bint{1}, b);
}

BOOST_AUTO_TEST_CASE(copy_assign)
{
    bint a{1};
    CLANG_WARNINGS_PUSH;
    CLANG_WARNINGS_IGNORE(-Wself-assign-overloaded);
    a = a;
    CLANG_WARNINGS_POP;
    BOOST_CHECK_EQUAL(a, 1);

    bint b{2};
    b = a;
    BOOST_CHECK_EQUAL(a, 1);
    BOOST_CHECK_EQUAL(b, 1);
}

BOOST_AUTO_TEST_CASE(swap)
{
    bint a{1};
    bint b{2};
    std::swap(a, b);
    BOOST_CHECK_EQUAL(a, 2);
    BOOST_CHECK_EQUAL(b, 1);
}

BOOST_AUTO_TEST_CASE(output_streamable)
{
    {
        ostringstream oss;
        oss << bint{};
        BOOST_CHECK_EQUAL("0", oss.str());
    }

    {
        ostringstream oss;
        oss << bint{123};
        BOOST_CHECK_EQUAL("123", oss.str());
    }
}

BOOST_AUTO_TEST_CASE(add)
{
    {
        bint a(1);
        bint b(2);
        bint c = a + b;
        BOOST_CHECK_EQUAL(c, 3);
    }
    {
        bint a(int64_max);
        bint b(int64_max);
        bint c = a + b;
        BOOST_CHECK_EQUAL(c, bint{"18446744073709551614"});
    }
}

BOOST_AUTO_TEST_CASE(sub)
{
    {
        bint a(2);
        bint b(1);
        bint c = a - b;
        BOOST_CHECK_EQUAL(c, 1);
    }
    {
        bint a(int64_max);
        bint b(int64_max);
        bint c = a - b;
        BOOST_CHECK_EQUAL(c, 0);
    }
}

BOOST_AUTO_TEST_CASE(mult)
{
    {
        bint a(1);
        bint b(2);
        bint c = a * b;
        BOOST_CHECK_EQUAL(c, 2);
    }
    {
        bint a(int64_max);
        bint b(int64_max);
        bint c = a * b;
        BOOST_CHECK_EQUAL(c, bint{"85070591730234615847396907784232501249"});
    }
}

BOOST_AUTO_TEST_CASE(div)
{
    {
        bint a(6);
        bint b(2);
        bint c = a / b;
        BOOST_CHECK_EQUAL(c, 3);
    }
    {
        bint a(int64_max);
        bint b(2);
        bint c = a / b;
        BOOST_CHECK_EQUAL(c, bint{"4611686018427387903"});
    }
}

BOOST_AUTO_TEST_CASE(mod)
{
    {
        bint a(7);
        bint b(2);
        bint c = a % b;
        BOOST_CHECK_EQUAL(c, bint{1});
    }
    {
        bint a(int64_max);
        bint b(101);
        bint c = a % b;
        BOOST_CHECK_EQUAL(c, 89);
    }
}

BOOST_AUTO_TEST_CASE(negate)
{
    const vector<int64_t> test_data{0, 1, -1, int64_max, -int64_max, int64_min + 1};
    for(const auto n : test_data)
    {
        bint bn(n);
        BOOST_CHECK_EQUAL(bint{-n}, -bn);
    }
}


BOOST_AUTO_TEST_CASE(lsb)
{
    const bint n{0x1234};
    BOOST_TEST(0x34 == n.lsb(), n.lsb());
}

BOOST_AUTO_TEST_CASE(bitwise_and)
{
    // clang-format off
    array<tuple<bint, bint, bint>, 21> v
    {
        make_tuple(bint{0}, bint{0}, bint{0}),

        make_tuple(bint{1}, bint{0}, bint{0}),
        make_tuple(bint{0}, bint{1}, bint{0}),
        
        make_tuple(bint{0x1234}, bint{0xff}, bint{0x34}),
        make_tuple(bint{0x1234}, bint{0xff00}, bint{0x1200}),
        
        make_tuple(bint{0xff}, bint{0x1234}, bint{0x34}),
        make_tuple(bint{0x1234}, bint{0xff00}, bint{0x1200}),

        make_tuple(bint{0x1010}, bint{0x101}, bint{0x0}),
        make_tuple(bint{0x101}, bint{0x1010}, bint{0x0}),
        
        make_tuple(bint{0x8080}, bint{0x8080}, bint{0x8080}),

        make_tuple(bint{int_max}, bint{0x0}, bint{0x0}),
        make_tuple(bint{0x0}, bint{int_max}, bint{0x0}),
        
        make_tuple(bint{int_max}, 
                   bint{int_max}, 
                   bint{int_max}),
        make_tuple(bint{int_max},
                   bint{int_max},
                   bint{int_max}),
        
        make_tuple(bint{int_min}, bint{0x0}, bint{0x0}),
        make_tuple(bint{0x0}, bint{int_min}, bint{0x0}),
        
        make_tuple(bint{-1}, bint{0}, bint{0}),
        make_tuple(bint{0}, bint{-1}, bint{0}),
        
        make_tuple(bint{1}, bint{-1}, bint{1}),
        make_tuple(bint{-1}, bint{1}, bint{1}),
        
        make_tuple(bint{-1}, bint{-1}, bint{-1}),
    };
    // clang-format on

    for(const auto& e : v)
    {
        bint lhs{get<0>(e)};
        bint rhs{get<1>(e)};
        bint expected{get<2>(e)};
        lhs &= rhs;
        BOOST_CHECK_EQUAL(expected, lhs);
    }
}

BOOST_AUTO_TEST_CASE(bitwise_or)
{
    // clang-format off
    array<tuple<bint, bint, bint>, 16> v
    {
        make_tuple(bint{0}, bint{0}, bint{0}),

        make_tuple(bint{1}, bint{0}, bint{1}),
        make_tuple(bint{0}, bint{1}, bint{1}),
        
        make_tuple(bint{0x1200}, bint{0x34}, bint{0x1234}),
        make_tuple(bint{0x34}, bint{0x1200}, bint{0x1234}),
        
        make_tuple(bint{-1}, bint{0}, bint{-1}),
        make_tuple(bint{0}, bint{-1}, bint{-1}),
        
        make_tuple(bint{1}, bint{-1}, bint{-1}),
        make_tuple(bint{-1}, bint{1}, bint{-1}),
        
        make_tuple(bint{-1}, bint{-1}, bint{1}),

        make_tuple(bint{int_max}, bint{0x0},
                   bint{int_max}),
        make_tuple(bint{0x0}, bint{int_max},
                   bint{int_max}),
        
        make_tuple(bint{int_min}, bint{0x0}, 
                   bint{int_min}),
        make_tuple(bint{0x0}, bint{int_min}, 
                   bint{int_min}),

        make_tuple(bint{0x1010}, bint{0x101}, bint{0x1111}),
        make_tuple(bint{0x101}, bint{0x1010}, bint{0x1111}),
    };
    // clang-format on

    for(const auto& e : v)
    {
        bint lhs{get<0>(e)};
        bint rhs{get<1>(e)};
        bint expected{get<2>(e)};
        lhs |= rhs;
        BOOST_CHECK_EQUAL(expected, lhs);
    }
}

BOOST_AUTO_TEST_CASE(shift_left)
{
    vector<tuple<bint, bint>> test_data
    {
        make_tuple(bint{1}, bint{0}),
        make_tuple(bint{1}, bint{1}),
        make_tuple(bint{1}, bint{7}),
        make_tuple(bint{1}, bint{15}),

        make_tuple(bint{-1}, bint{0}),
        make_tuple(bint{-1}, bint{1}),
        make_tuple(bint{-1}, bint{7}),
        make_tuple(bint{-1}, bint{15}),
    };

    for(const auto& [a, b] : test_data)
    {
        bint actual = a;
        actual <<= b;
        const bint expected = a * bsv::pow(bint{2}, b);
        BOOST_CHECK_EQUAL(expected, actual);
    }
}

BOOST_AUTO_TEST_CASE(shift_left_bint_max_script_len)
{
    bint a{1};
    a <<= MAX_SCRIPT_NUM_LENGTH_AFTER_CHRONICLE * CHAR_BIT;

    std::vector<uint8_t> v(MAX_SCRIPT_NUM_LENGTH_AFTER_CHRONICLE + 1, 0);
    v.back() = 0x1;
    const auto expected{bint::deserialize(v)};
    BOOST_CHECK_EQUAL(expected, a);
}

BOOST_AUTO_TEST_CASE(shift_left_bint_exceeds_int_max)
{
    // Test that shifting by an amount exceeding INT_MAX throws an exception
    bint a{1};
    const bint shift_amount{static_cast<int64_t>(INT_MAX) + 1};
    BOOST_CHECK_THROW(a <<= shift_amount, bsv::big_int_error);
}

BOOST_AUTO_TEST_CASE(shift_right)
{
    const vector<tuple<bint, bint, bint>> test_data
    {
        make_tuple(bint{1}, bint{1}, bint{0}),
        make_tuple(bint{2}, bint{1}, bint{1}),
        make_tuple(bint{0x80}, bint{7}, bint{1}),
        make_tuple(bint{0x8000}, bint{15}, bint{1}),

        make_tuple(bint{-1}, bint{1}, bint{0}),
        make_tuple(bint{-2}, bint{1}, bint{-1}),
        make_tuple(bint{-0x80}, bint{7}, bint{-1}),
        make_tuple(bint{-0x8000}, bint{15}, bint{-1}),
    };

    for(const auto& [a, b, expected] : test_data)
    {
        bint actual{a};
        actual >>= b;
        BOOST_CHECK_EQUAL(expected, actual);
    }
}

BOOST_AUTO_TEST_CASE(shift_right_bint_max_script_len)
{
    constexpr auto byte_len{MAX_SCRIPT_NUM_LENGTH_AFTER_CHRONICLE};
    constexpr auto bit_len{byte_len * CHAR_BIT};
    vector<uint8_t> v(byte_len, 0xc0);
    bint a = bsv::deserialize(v.begin(), v.end());
    a >>= bit_len - 2;
    const bint expected{-1};
    BOOST_CHECK_EQUAL(expected, a);
}

BOOST_AUTO_TEST_CASE(shift_right_bint_exceeds_int_max)
{
    // Test that shifting by an amount exceeding INT_MAX throws an exception
    bint a{1};
    const bint shift_amount{static_cast<int64_t>(INT_MAX) + 1};
    BOOST_CHECK_THROW(a >>= shift_amount, bsv::big_int_error);
}

BOOST_AUTO_TEST_CASE(absolute_value)
{
    using namespace bsv;

    const bint a{int64_max};
    const bint aa{a * a};

    BOOST_TEST(aa == abs(aa));
    BOOST_TEST(bint{"85070591730234615847396907784232501249"} == abs(-aa));
}

BOOST_AUTO_TEST_CASE(to_string)
{
    BOOST_TEST("0" == bsv::to_dec(bint{}));

    constexpr int64_t min64{int64_min};
    constexpr int64_t max64{int64_max};
    vector<int64_t> test_data{0, 1, -1, min64, max64};
    for(const auto n : test_data)
    {
        BOOST_TEST(std::to_string(n) == bsv::to_dec(bint{n}));
    }
}

BOOST_AUTO_TEST_CASE(to_size_t_limited)
{
    BOOST_TEST("0" == bsv::to_dec(bint{}));

    vector<size_t> test_data{ size_t_min, 1, size_t_max };
    for(const auto n : test_data)
    {
        BOOST_TEST(n == bsv::to_size_t_limited(bint{n}));
    }
}

BOOST_AUTO_TEST_CASE(to_int64_t_test)
{
    const vector<int64_t> test_data{0, 1, -1, int64_max, int64_min};
    for(const auto n : test_data)
    {
        BOOST_TEST(n == bsv::to_int64_t(bint{n}));
    }

    const bint too_large{bint{int64_max} + 1};
    BOOST_CHECK_THROW(bsv::to_int64_t(too_large), bsv::big_int_error);

    const bint too_small{bint{int64_min} - 1};
    BOOST_CHECK_THROW(bsv::to_int64_t(too_small), bsv::big_int_error);
}

BOOST_AUTO_TEST_CASE(max_size)
{
    // Check the maximum size (in terms of memory usage) of a big_int
    // is still above the consensus limit for a script num.

    bsv::bint b {1};

    while(static_cast<uint64_t>(b.size_bytes()) <= MAX_SCRIPT_NUM_LENGTH_AFTER_CHRONICLE)
    {
        try
        {
            b <<= ONE_MEBIBYTE * 8;
        }
        catch(const bsv::big_int_error& e)
        {
            BOOST_FAIL("BigInt can't store script num of consensus size");
            break;
        }
    }
}

BOOST_AUTO_TEST_CASE(serialized_size_test)
{
    // Test that serialized_size() returns the same value as serialize().size()
    // without actually performing the full serialization

    vector<bint> test_values {
        bint{0},
        bint{1},
        bint{-1},
        bint{std::numeric_limits<int8_t>::max()},
        bint{std::numeric_limits<int8_t>::max() + 1},
        bint{std::numeric_limits<int8_t>::min()},
        bint{std::numeric_limits<int8_t>::min() - 1},
        bint{std::numeric_limits<int16_t>::max()},
        bint{std::numeric_limits<int16_t>::min()},
        bint{std::numeric_limits<int32_t>::max()},
        bint{std::numeric_limits<int32_t>::min()},
        bint{std::numeric_limits<int64_t>::max()},
        bint{std::numeric_limits<int64_t>::min()},
    };

    // Test basic values
    for(const auto& val : test_values)
    {
        const size_t expected_size = val.serialize().size();
        const size_t actual_size = val.serialized_size();
        BOOST_CHECK_EQUAL(expected_size, actual_size);
    }

    // Test large numbers beyond int64_t range
    const bint bn_max64{std::numeric_limits<int64_t>::max()};
    const bint bn_min64{std::numeric_limits<int64_t>::min()};
    vector<bint> large_values {
        bn_max64 + 1,
        bn_max64 + bn_max64,
        bn_max64 * bn_max64,
        bn_min64 + bn_min64,
    };

    for(const auto& val : large_values)
    {
        const size_t expected_size = val.serialize().size();
        const size_t actual_size = val.serialized_size();
        BOOST_CHECK_EQUAL(expected_size, actual_size);
    }
}

BOOST_AUTO_TEST_SUITE_END()
