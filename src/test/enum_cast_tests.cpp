// Copyright (c) 2019 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "enum_cast.h"
#include <boost/test/unit_test.hpp>

namespace
{
    // Table for casting to/from string
    enum class MyTypesCorrect { UNKNOWN, Type1, Type2 };
    const enumTableT<MyTypesCorrect>& enumTable(MyTypesCorrect)
    {
        static enumTableT<MyTypesCorrect> table
        {
            {MyTypesCorrect::UNKNOWN, "Unknown"}, {MyTypesCorrect::Type1, "Type 1"}, {MyTypesCorrect::Type2, "Type 2"}
        };
        return table;
    }

    // Output operator
    std::ostream& operator<<(std::ostream& str, MyTypesCorrect mytype)
    {
        str << enum_cast<std::string>(mytype);
        return str;
    }
}

BOOST_AUTO_TEST_SUITE(TestEnumCast);

// Test normal (correct) operation of enum_cast
BOOST_AUTO_TEST_CASE(TestCorrectEnumCast)
{
    // Cast from existing string and back (non-fundamental type)
    std::string str { "Type 1" };
    MyTypesCorrect myType { enum_cast<MyTypesCorrect>(str) };
    BOOST_CHECK_EQUAL(myType, MyTypesCorrect::Type1);
    str = enum_cast<std::string>(myType);
    BOOST_CHECK_EQUAL(str, "Type 1");

    // Cast from convertable to string
    myType = enum_cast<MyTypesCorrect>("Type 1");
    BOOST_CHECK_EQUAL(myType, MyTypesCorrect::Type1);
}

// Test UNKNOWN casting
BOOST_AUTO_TEST_CASE(TestUnknownEnumCast)
{
    // Cast to MyTypes from unknown string
    std::string str { "Wibble" };
    MyTypesCorrect myType { enum_cast<MyTypesCorrect>(str) };
    BOOST_CHECK_EQUAL(myType, MyTypesCorrect::UNKNOWN);
}

BOOST_AUTO_TEST_SUITE_END();

