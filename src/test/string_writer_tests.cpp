// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "test/test_bitcoin.h"
#include "rpc/text_writer.h"
#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(string_writer_tests, BasicTestingSetup)

CStringWriter strWriter;

BOOST_AUTO_TEST_CASE(CStringWriter_writechar)
{
    strWriter.Write('v');

    BOOST_CHECK_EQUAL(strWriter.MoveOutString(), "v");
}

BOOST_AUTO_TEST_CASE(CStringWriter_writestring)
{
    strWriter.Write("string");

    BOOST_CHECK_EQUAL(strWriter.MoveOutString(), "string");
}

BOOST_AUTO_TEST_CASE(CStringWriter_writestring_withNL)
{
    strWriter.ReserveAdditional(100);
    strWriter.WriteLine("string");

    BOOST_CHECK_EQUAL(strWriter.MoveOutString(), "string\n");
}

BOOST_AUTO_TEST_SUITE_END()
