// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#include "big_int.hpp"

#include <array>

#include <boost/test/unit_test.hpp>

using namespace std;
using bsv::bint;

BOOST_AUTO_TEST_SUITE(bint_tests)

BOOST_AUTO_TEST_CASE(default_construction)
{
    bint assignable;
    assignable = bint{1};
    BOOST_CHECK_EQUAL(bint{1}, assignable);
    bint destructible;
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
    BOOST_CHECK_EQUAL(bint(1), b);
}

BOOST_AUTO_TEST_CASE(move_assign)
{
    bint a{1};
    bint b{2};
    b = std::move(a);
    BOOST_CHECK_EQUAL(bint(1), b);
}

BOOST_AUTO_TEST_CASE(copy_assign)
{
    bint a{1};
    a = a;
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
    ostringstream oss;
    oss << bint{};
    BOOST_CHECK_EQUAL("", oss.str());

    bint a{123};
    oss << a;
    BOOST_CHECK_EQUAL("123", oss.str());
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
        bint a(std::numeric_limits<int64_t>::max());
        bint b(std::numeric_limits<int64_t>::max());
        bint c = a + b;
        BOOST_CHECK_EQUAL(c, bint("18446744073709551614"));
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
        bint a(std::numeric_limits<int64_t>::max());
        bint b(std::numeric_limits<int64_t>::max());
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
        bint a(std::numeric_limits<int64_t>::max());
        bint b(std::numeric_limits<int64_t>::max());
        bint c = a * b;
        BOOST_CHECK_EQUAL(c, bint("85070591730234615847396907784232501249"));
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
        bint a(std::numeric_limits<int64_t>::max());
        bint b(2);
        bint c = a / b;
        BOOST_CHECK_EQUAL(c, bint("4611686018427387903"));
    }
}

BOOST_AUTO_TEST_CASE(mod)
{
    {
        bint a(7);
        bint b(2);
        bint c = a % b;
        BOOST_CHECK_EQUAL(c, bint(1));
    }
    {
        bint a(std::numeric_limits<int64_t>::max());
        bint b(101);
        bint c = a % b;
        BOOST_CHECK_EQUAL(c, 89);
    }
}

BOOST_AUTO_TEST_CASE(negate)
{
    {
        bint a(1);
        bint b{-a};
        BOOST_CHECK_EQUAL(b, -1);
        bint c{-b};
        BOOST_CHECK_EQUAL(c, 1);
    }
    {
        const auto max{std::numeric_limits<int64_t>::max()};
        bint a(std::numeric_limits<int64_t>::max());
        bint b{-a};
        BOOST_CHECK_EQUAL(a, max);
        BOOST_CHECK_EQUAL(b, -max);
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
        make_tuple(bint(0), bint(0), bint(0)),

        make_tuple(bint(1), bint(0), bint(0)),
        make_tuple(bint(0), bint(1), bint(0)),
        
        make_tuple(bint{0x1234}, bint{0xff}, bint{0x34}),
        make_tuple(bint{0x1234}, bint{0xff00}, bint{0x1200}),
        
        make_tuple(bint{0xff}, bint{0x1234}, bint{0x34}),
        make_tuple(bint{0x1234}, bint{0xff00}, bint{0x1200}),

        make_tuple(bint(0x1010), bint(0x101), bint(0x0)),
        make_tuple(bint(0x101), bint(0x1010), bint(0x0)),
        
        make_tuple(bint(0x8080), bint(0x8080), bint(0x8080)),

        make_tuple(bint(numeric_limits<int>::max()), bint(0x0), bint{0x0}),
        make_tuple(bint(0x0), bint(numeric_limits<int>::max()), bint{0x0}),
        
        make_tuple(bint(numeric_limits<int>::max()), 
                   bint(numeric_limits<int>::max()), 
                   bint{numeric_limits<int>::max()}),
        make_tuple(bint(numeric_limits<int>::max()),
                   bint(numeric_limits<int>::max()),
                   bint(numeric_limits<int>::max())),
        
        make_tuple(bint(numeric_limits<int>::min()), bint(0x0), bint(0x0)),
        make_tuple(bint(0x0), bint(numeric_limits<int>::min()), bint(0x0)),
        
        make_tuple(bint(-1), bint(0), bint(0)),
        make_tuple(bint(0), bint(-1), bint(0)),
        
        make_tuple(bint(1), bint(-1), bint(1)),
        make_tuple(bint(-1), bint(1), bint(1)),
        
        make_tuple(bint(-1), bint(-1), bint(-1)),
    };
    // clang-format on

    for(const auto e : v)
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
        make_tuple(bint(0), bint(0), bint(0)),

        make_tuple(bint(1), bint(0), bint(1)),
        make_tuple(bint(0), bint(1), bint(1)),
        
        make_tuple(bint{0x1200}, bint{0x34}, bint{0x1234}),
        make_tuple(bint{0x34}, bint{0x1200}, bint{0x1234}),
        
        make_tuple(bint(-1), bint(0), bint(-1)),
        make_tuple(bint(0), bint(-1), bint(-1)),
        
        make_tuple(bint(1), bint(-1), bint(-1)),
        make_tuple(bint(-1), bint(1), bint(-1)),
        
        make_tuple(bint(-1), bint(-1), bint(1)),

        make_tuple(bint(numeric_limits<int>::max()), bint(0x0),
                   bint(numeric_limits<int>::max())),
        make_tuple(bint(0x0), bint(numeric_limits<int>::max()),
                   bint(numeric_limits<int>::max())),
        
        make_tuple(bint(numeric_limits<int>::min()), bint(0x0), 
                   bint(numeric_limits<int>::min())),
        make_tuple(bint(0x0), bint(numeric_limits<int>::min()), 
                   bint(numeric_limits<int>::min())),

        make_tuple(bint(0x1010), bint(0x101), bint(0x1111)),
        make_tuple(bint(0x101), bint(0x1010), bint(0x1111)),
    };
    // clang-format on

    for(const auto e : v)
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
    // clang-format off
    array<tuple<bint, int, bint>, 5> v
    {
        make_tuple(bint{0x1}, 0, bint{0x1}),
        make_tuple(bint{0x1}, 1, bint{0x2}),
        make_tuple(bint{0x1}, 2, bint{0x4}),
        make_tuple(bint{0x1}, 3, bint{0x8}),
        make_tuple(bint{0x0f}, 4, bint{0xf0}),
    };
    // clang-format on

    for(const auto& e : v)
    {
        bint lhs{get<0>(e)};
        int n{get<1>(e)};
        lhs <<= n;
        bint expected{get<2>(e)};
        BOOST_CHECK_EQUAL(lhs, expected);
    }
}

BOOST_AUTO_TEST_CASE(shift_right)
{
    // clang-format off
    array<tuple<bint, int, bint>, 6> v
    {
        make_tuple(bint{0x1}, 0, bint{0x1}),
        make_tuple(bint{0x1}, 1, bint{0x0}),
        make_tuple(bint{0x2}, 1, bint{0x1}),
        make_tuple(bint{0x4}, 2, bint{0x1}),
        make_tuple(bint{0x8}, 3, bint{0x1}),
        make_tuple(bint{0xf0}, 4, bint{0xf}),
    };
    // clang-format on

    for(const auto& e : v)
    {
        bint lhs{get<0>(e)};
        int n{get<1>(e)};
        lhs >>= n;
        bint expected{get<2>(e)};
        BOOST_CHECK_EQUAL(lhs, expected);
    }
}

BOOST_AUTO_TEST_CASE(absolute_value)
{
    using namespace bsv;

    const bint a{std::numeric_limits<int64_t>::max()};
    const bint aa{a * a};

    BOOST_TEST(aa == abs(aa));
    BOOST_TEST(bint("85070591730234615847396907784232501249") == abs(-aa));
}

BOOST_AUTO_TEST_CASE(to_string)
{
    BOOST_TEST("" == bsv::to_string(bint()));

    constexpr int64_t min64{numeric_limits<int64_t>::min()};
    constexpr int64_t max64{numeric_limits<int64_t>::max()};
    vector<int64_t> test_data{0, 1, -1, min64, max64};
    for(const auto n : test_data)
    {
        BOOST_TEST(std::to_string(n) == bsv::to_string(bint{n}));
    }
}

BOOST_AUTO_TEST_SUITE_END()
