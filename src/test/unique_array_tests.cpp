// Copyright (c) 2023 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#include <boost/test/tools/old/interface.hpp>
#include <cstdint>
#include <numeric>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "unique_array.h"

using namespace std;

BOOST_AUTO_TEST_SUITE(unique_array_tests)

BOOST_AUTO_TEST_CASE(default_construction)
{
    unique_array a;
    BOOST_CHECK(a.empty());
    BOOST_CHECK_EQUAL(0, a.size());
    BOOST_CHECK_EQUAL(0, a.capacity());
}

BOOST_AUTO_TEST_CASE(span_construction)
{
    std::vector<uint8_t> v{1, 2, 3};
    unique_array a{std::span<const uint8_t>{v.data(), v.size()}};
    BOOST_CHECK(!a.empty());
    BOOST_CHECK_EQUAL(3, a.capacity());
    BOOST_CHECK_EQUAL(3, a.size());
    BOOST_CHECK_EQUAL_COLLECTIONS(v.cbegin(), v.cend(),
                                  a.cbegin(), a.cend());
}

BOOST_AUTO_TEST_CASE(move_construction)
{
    unique_array a;
    a.push_back(42);
    unique_array b{std::move(a)};
    BOOST_CHECK_EQUAL(1, b.size());
    BOOST_CHECK_EQUAL(1, b.capacity());
    BOOST_CHECK_EQUAL(42, b[0]);
    
    // moved from object should still be useable
    BOOST_CHECK(a.empty());
    a.push_back(101);
    BOOST_CHECK_EQUAL(101, a[0]);
}

BOOST_AUTO_TEST_CASE(move_assignment)
{
    unique_array a;
    a.push_back(1);
    unique_array b;
    b.push_back(2);
    b.push_back(3);
    b = std::move(a);
    BOOST_CHECK_EQUAL(1, b.size());
    BOOST_CHECK_EQUAL(1, b.capacity());
    BOOST_CHECK_EQUAL(1, b[0]);
    
    // moved from object should still be useable
    BOOST_CHECK(a.empty());
    a.push_back(101);
    BOOST_CHECK_EQUAL(101, a[0]);
}

BOOST_AUTO_TEST_CASE(reserve_capacity)
{
    unique_array a;
    BOOST_CHECK_EQUAL(0, a.capacity());
    BOOST_CHECK_EQUAL(0, a.size());

    a.reserve(0);
    BOOST_CHECK_EQUAL(0, a.capacity());
    BOOST_CHECK_EQUAL(0, a.size());

    constexpr auto cap{42};
    a.reserve(cap);
    BOOST_CHECK_EQUAL(cap, a.capacity());
    BOOST_CHECK_EQUAL(0, a.size());

    // reserve doesn't shrink
    a.reserve(cap/2);
    BOOST_CHECK_EQUAL(cap, a.capacity());
    BOOST_CHECK_EQUAL(0, a.size());
}

BOOST_AUTO_TEST_CASE(push_back)
{
    unique_array a;

    a.push_back(42);
    BOOST_CHECK_EQUAL(1, a.capacity());
    BOOST_CHECK_EQUAL(1, a.size());
    
    a.push_back(42);
    BOOST_CHECK_EQUAL(2, a.capacity());
    BOOST_CHECK_EQUAL(2, a.size());
    
    a.push_back(42);
    BOOST_CHECK_EQUAL(4, a.capacity());
    BOOST_CHECK_EQUAL(3, a.size());
}
    
BOOST_AUTO_TEST_CASE(insert_into_default_constructed)
{
    constexpr auto n{10};
    constexpr auto half{n/2};
    std::vector<uint8_t> v(n);
    iota(v.begin(), v.end(), 0);

    unique_array a;
    a.insert(a.end(), v.cbegin(), v.cbegin() + half);
    BOOST_CHECK(!a.empty());
    BOOST_CHECK_EQUAL(half, a.size());
    BOOST_CHECK_EQUAL(half, a.capacity());
    BOOST_CHECK_EQUAL_COLLECTIONS(v.cbegin(), v.cbegin() + half, 
                                  a.cbegin(), a.cbegin() + half);

    a.insert(a.end(), v.cbegin() + half, v.cend());
    BOOST_CHECK(!a.empty());
    BOOST_CHECK_EQUAL(n, a.size());
    BOOST_CHECK_EQUAL(n, a.capacity());
    BOOST_CHECK_EQUAL_COLLECTIONS(v.cbegin(), v.cend(), 
                                  a.cbegin(), a.cend());
}

BOOST_AUTO_TEST_CASE(insert_into_non_empty)
{
    constexpr auto n{10};
    constexpr auto half{n/2};
    std::vector<uint8_t> v(n);
    iota(v.begin(), v.end(), 0);

    unique_array a;
    a.push_back(42);
    BOOST_CHECK_EQUAL(1, a.size());
    a.insert(a.end(), v.cbegin(), v.cbegin() + half);
    BOOST_CHECK(!a.empty());
    BOOST_CHECK_EQUAL(half + 1, a.size());
    BOOST_CHECK_EQUAL(half + 1, a.capacity());
    BOOST_CHECK_EQUAL_COLLECTIONS(v.cbegin(), v.cbegin() + half, 
                                  a.cbegin() + 1, a.cbegin() + 1 + half);

    a.insert(a.end(), v.cbegin() + half, v.cend());
    BOOST_CHECK(!a.empty());
    BOOST_CHECK_EQUAL(n + 1, a.size());
    BOOST_CHECK_EQUAL(n + 2, a.capacity());
    BOOST_CHECK_EQUAL_COLLECTIONS(v.cbegin(), v.cend(), 
                                  a.cbegin() + 1, a.cend());
}

BOOST_AUTO_TEST_CASE(data)
{
    unique_array a;
    BOOST_CHECK_NE(nullptr, a.data());

    a.push_back(42);
    BOOST_CHECK_EQUAL(42, *a.data());
}

BOOST_AUTO_TEST_CASE(reset)
{
    unique_array a;
    a.push_back(42);
    a.reset();
    BOOST_CHECK(a.empty());
    
    // check still useable
    a.push_back(101);
    BOOST_CHECK_EQUAL(101, a[0]);
}

BOOST_AUTO_TEST_CASE(shrink_to_fit_size_equal_cap)
{
    unique_array a;
    constexpr auto n{10};
    std::vector<uint8_t> v(n);
    iota(v.begin(), v.end(), 0);
    
    a.insert(a.cend(), v.cbegin(), v.cend());
    BOOST_CHECK_EQUAL(a.size(), a.capacity());
    a.shrink_to_fit();
    BOOST_CHECK_EQUAL(v.size(), a.size());
    BOOST_CHECK_EQUAL(a.size(), a.capacity());

    a.push_back(42);
    BOOST_CHECK_NE(a.size(), a.capacity());
    a.shrink_to_fit();
    BOOST_CHECK_EQUAL(v.size() + 1, a.size());
    BOOST_CHECK_EQUAL(a.size(), a.capacity());
    BOOST_CHECK_EQUAL_COLLECTIONS(v.cbegin(), v.cend(), 
                                  a.cbegin(), a.cbegin() + v.size());
    BOOST_CHECK_EQUAL(42, a[10]);
}

BOOST_AUTO_TEST_CASE(read_empty_input)
{
    unique_array a;
    vector<uint8_t> v(1);
    const auto bytes_read = read(a, 0, span{v.data(), v.size()});
    BOOST_CHECK_EQUAL(0, bytes_read);
}

BOOST_AUTO_TEST_CASE(read_empty_output)
{
    unique_array a;
    a.push_back(42);
    vector<uint8_t> v;
    const auto bytes_read = read(a, 0, span{v.data(), v.size()});
    BOOST_CHECK_EQUAL(0, bytes_read);
}

BOOST_AUTO_TEST_CASE(read_1)
{
    unique_array a;
    a.push_back(42);
    vector<uint8_t> v(1);
    const auto bytes_read = read(a, 0, span{v.data(), v.size()});
    BOOST_CHECK_EQUAL(1, bytes_read);
    BOOST_CHECK_EQUAL(42, v[0]);
}

BOOST_AUTO_TEST_CASE(read_many)
{
    unique_array a;
    a.push_back(42);
    a.push_back(69);

    vector<uint8_t> v(2);
    const auto bytes_read = read(a, 0, span{v.data(), v.size()});
    BOOST_CHECK_EQUAL(2, bytes_read);
    BOOST_CHECK_EQUAL_COLLECTIONS(a.cbegin(), a.cend(),
                                  v.cbegin(), v.cend());
}

BOOST_AUTO_TEST_CASE(read_too_many)
{
    unique_array a;
    a.push_back(42);
    a.push_back(69);

    vector<uint8_t> v(2);
    const auto bytes_read = read(a, 1, span{v.data(), v.size()});
    BOOST_CHECK_EQUAL(1, bytes_read);
    BOOST_CHECK_EQUAL(69, v[0]);
}

BOOST_AUTO_TEST_SUITE_END()

