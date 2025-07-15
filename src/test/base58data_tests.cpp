// Copyright (c) 2025 BSV Blockchain Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#include <boost/test/unit_test.hpp>

#include "base58.h"

#include <cstdint>
#include <iostream>

namespace
{
    struct test_base58_data : public CBase58Data
    {
        [[maybe_unused]] // used for BOOST_CHECK_EQUAL etc.
        friend std::ostream& operator<<(std::ostream& os, const test_base58_data& b)
        {
            os << "test_base58_data: " << b.ToString();
            return os;
        }
    };
}

BOOST_AUTO_TEST_SUITE(base58data_tests)

BOOST_AUTO_TEST_CASE(default_construction)
{
    test_base58_data b1;
    BOOST_CHECK_EQUAL(b1, b1);
}

BOOST_AUTO_TEST_CASE(compare)
{
    using namespace std;

    test_base58_data b1;
    vector<uint8_t> ip1{1, 1, 1, 1};
    const auto s1{b1.SetString(EncodeBase58Check(ip1))};
    BOOST_CHECK(s1);
    
    test_base58_data b2;
    vector<uint8_t> ip2{1, 1, 1, 2};
    const auto s2{b2.SetString(EncodeBase58Check(ip2))};
    BOOST_CHECK(s2);
    
    test_base58_data b3;
    vector<uint8_t> ip3{1, 1, 2, 1};
    const auto s3{b3.SetString(EncodeBase58Check(ip3))};
    BOOST_CHECK(s3);
   
    test_base58_data b4;
    vector<uint8_t> ip4{1, 1, 2, 2};
    const auto s4{b4.SetString(EncodeBase58Check(ip4))};
    BOOST_CHECK(s4);
    
    test_base58_data b5;
    vector<uint8_t> ip5{1, 2, 1, 1};
    const auto s5{b5.SetString(EncodeBase58Check(ip5))};
    BOOST_CHECK(s5);
    
    test_base58_data b6;
    vector<uint8_t> ip6{1, 2, 1, 2};
    const auto s6{b6.SetString(EncodeBase58Check(ip6))};
    BOOST_CHECK(s6);
    
    test_base58_data b7;
    vector<uint8_t> ip7{1, 2, 2, 1};
    const auto s7{b7.SetString(EncodeBase58Check(ip7))};
    BOOST_CHECK(s7);
   
    test_base58_data b8;
    vector<uint8_t> ip8{1, 2, 2, 2};
    const auto s8{b8.SetString(EncodeBase58Check(ip8))};
    BOOST_CHECK(s8);
    
    test_base58_data b9;
    vector<uint8_t> ip9{2, 1, 1, 1};
    const auto s9{b9.SetString(EncodeBase58Check(ip9))};
    BOOST_CHECK(s9);
    
    test_base58_data b10;
    vector<uint8_t> ip10{2, 1, 1, 2};
    const auto s10{b10.SetString(EncodeBase58Check(ip10))};
    BOOST_CHECK(s10);
    
    test_base58_data b11;
    vector<uint8_t> ip11{2, 1, 2, 1};
    const auto s11{b11.SetString(EncodeBase58Check(ip11))};
    BOOST_CHECK(s11);
   
    test_base58_data b12;
    vector<uint8_t> ip12{2, 1, 2, 2};
    const auto s12{b12.SetString(EncodeBase58Check(ip12))};
    BOOST_CHECK(s12);
    
    test_base58_data b13;
    vector<uint8_t> ip13{2, 2, 1, 1};
    const auto s13{b13.SetString(EncodeBase58Check(ip13))};
    BOOST_CHECK(s13);
    
    test_base58_data b14;
    vector<uint8_t> ip14{2, 2, 1, 2};
    const auto s14{b14.SetString(EncodeBase58Check(ip14))};
    BOOST_CHECK(s14);
    
    test_base58_data b15;
    vector<uint8_t> ip15{2, 2, 2, 1};
    const auto s15{b15.SetString(EncodeBase58Check(ip15))};
    BOOST_CHECK(s15);
   
    test_base58_data b16;
    vector<uint8_t> ip16{2, 2, 2, 2};
    const auto s16{b16.SetString(EncodeBase58Check(ip16))};
    BOOST_CHECK(s16);

    BOOST_CHECK_EQUAL(-1, b1.CompareTo(b2));
    BOOST_CHECK_EQUAL(-1, b2.CompareTo(b3));
    BOOST_CHECK_EQUAL(-1, b3.CompareTo(b4));
    BOOST_CHECK_EQUAL(-1, b4.CompareTo(b5));
    BOOST_CHECK_EQUAL(-1, b5.CompareTo(b6));
    BOOST_CHECK_EQUAL(-1, b6.CompareTo(b7));
    BOOST_CHECK_EQUAL(-1, b7.CompareTo(b8));
    BOOST_CHECK_EQUAL(-1, b8.CompareTo(b9));
    BOOST_CHECK_EQUAL(-1, b9.CompareTo(b10));
    BOOST_CHECK_EQUAL(-1, b10.CompareTo(b11));
    BOOST_CHECK_EQUAL(-1, b11.CompareTo(b12));
    BOOST_CHECK_EQUAL(-1, b12.CompareTo(b13));
    BOOST_CHECK_EQUAL(-1, b13.CompareTo(b14));
    BOOST_CHECK_EQUAL(-1, b14.CompareTo(b15));
    BOOST_CHECK_EQUAL(-1, b15.CompareTo(b16));
    
    BOOST_CHECK_EQUAL(1, b16.CompareTo(b15));
    BOOST_CHECK_EQUAL(1, b15.CompareTo(b14));
    BOOST_CHECK_EQUAL(1, b14.CompareTo(b13));
    BOOST_CHECK_EQUAL(1, b13.CompareTo(b12));
    BOOST_CHECK_EQUAL(1, b12.CompareTo(b11));
    BOOST_CHECK_EQUAL(1, b11.CompareTo(b10));
    BOOST_CHECK_EQUAL(1, b10.CompareTo(b9));
    BOOST_CHECK_EQUAL(1, b9.CompareTo(b8));
    BOOST_CHECK_EQUAL(1, b8.CompareTo(b7));
    BOOST_CHECK_EQUAL(1, b7.CompareTo(b6));
    BOOST_CHECK_EQUAL(1, b6.CompareTo(b5));
    BOOST_CHECK_EQUAL(1, b5.CompareTo(b4));
    BOOST_CHECK_EQUAL(1, b4.CompareTo(b3));
    BOOST_CHECK_EQUAL(1, b3.CompareTo(b2));
    BOOST_CHECK_EQUAL(1, b2.CompareTo(b1));
    
    BOOST_CHECK_EQUAL(0, b1.CompareTo(b1));
    BOOST_CHECK_EQUAL(0, b2.CompareTo(b2));
    BOOST_CHECK_EQUAL(0, b3.CompareTo(b3));
    BOOST_CHECK_EQUAL(0, b4.CompareTo(b4));
    BOOST_CHECK_EQUAL(0, b5.CompareTo(b5));
    BOOST_CHECK_EQUAL(0, b6.CompareTo(b6));
    BOOST_CHECK_EQUAL(0, b7.CompareTo(b7));
    BOOST_CHECK_EQUAL(0, b8.CompareTo(b8));
    BOOST_CHECK_EQUAL(0, b9.CompareTo(b9));
    BOOST_CHECK_EQUAL(0, b10.CompareTo(b10));
    BOOST_CHECK_EQUAL(0, b11.CompareTo(b11));
    BOOST_CHECK_EQUAL(0, b12.CompareTo(b12));
    BOOST_CHECK_EQUAL(0, b13.CompareTo(b13));
    BOOST_CHECK_EQUAL(0, b14.CompareTo(b14));
    BOOST_CHECK_EQUAL(0, b15.CompareTo(b15));
    BOOST_CHECK_EQUAL(0, b16.CompareTo(b16));
}

BOOST_AUTO_TEST_SUITE_END()
