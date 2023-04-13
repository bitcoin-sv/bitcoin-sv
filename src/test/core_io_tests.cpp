// Copyright (c) 2018 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "core_io.h"
#include "script/script.h"

#include <boost/test/unit_test.hpp>

#include <array>
#include <future>
#include <string>

using namespace std;

BOOST_AUTO_TEST_SUITE(core_io_tests)

BOOST_AUTO_TEST_CASE(mt_parse_script_of_opcodes)
{
    // Create n tasks to call ParseScript at the same time
    // via a promise (go) and shared_future (sf).
    promise<void> go;
    shared_future sf{go.get_future()};

    constexpr size_t n{8};
    array<promise<void>, n> promises;
    array<future<CScript>, n> futures;
    for(size_t i{}; i < n; ++i)
    {
        futures[i] = async(
            std::launch::async,
            [&sf](auto* ready) {
                ready->set_value();
                sf.wait();
                return ParseScript("OP_ADD");
            },
            &promises[i]);
    }

    // wait until all tasks are ready
    for(auto& p : promises)
        p.get_future().wait();

    // All tasks are ready, go...
    go.set_value();

    // Wait until all tasks have finished
    for(auto& f : futures)
        f.get();
}

BOOST_AUTO_TEST_CASE(test_for_exposition)
{
    BOOST_CHECK_NO_THROW(ParseScript("0x00"));
    BOOST_CHECK_NO_THROW(ParseScript("0x0000"));
    BOOST_CHECK_NO_THROW(ParseScript("0x000000"));

    BOOST_CHECK_THROW(ParseScript("0x"), std::runtime_error);
    BOOST_CHECK_THROW(ParseScript("0x0"), std::runtime_error);
    BOOST_CHECK_THROW(ParseScript("0x000"), std::runtime_error);
    BOOST_CHECK_THROW(ParseScript("0x00000"), std::runtime_error);

    BOOST_CHECK_NO_THROW(ParseScript("0"));
    BOOST_CHECK_NO_THROW(ParseScript("1"));
    BOOST_CHECK_NO_THROW(ParseScript("2"));
    BOOST_CHECK_NO_THROW(ParseScript("3"));
    BOOST_CHECK_NO_THROW(ParseScript("4"));
    BOOST_CHECK_NO_THROW(ParseScript("5"));
    BOOST_CHECK_NO_THROW(ParseScript("6"));
    BOOST_CHECK_NO_THROW(ParseScript("7"));
    BOOST_CHECK_NO_THROW(ParseScript("8"));
    BOOST_CHECK_NO_THROW(ParseScript("9"));
    BOOST_CHECK_NO_THROW(ParseScript("10"));
    BOOST_CHECK_NO_THROW(ParseScript("11"));
    BOOST_CHECK_NO_THROW(ParseScript("12"));
    BOOST_CHECK_NO_THROW(ParseScript("13"));
    BOOST_CHECK_NO_THROW(ParseScript("14"));
    BOOST_CHECK_NO_THROW(ParseScript("15"));
    BOOST_CHECK_NO_THROW(ParseScript("16"));

    BOOST_CHECK_THROW(ParseScript("OP_0"), std::runtime_error);
    BOOST_CHECK_THROW(ParseScript("OP_1"), std::runtime_error);
    BOOST_CHECK_THROW(ParseScript("OP_2"), std::runtime_error);
    BOOST_CHECK_THROW(ParseScript("OP_3"), std::runtime_error);
    BOOST_CHECK_THROW(ParseScript("OP_4"), std::runtime_error);
    BOOST_CHECK_THROW(ParseScript("OP_5"), std::runtime_error);
    BOOST_CHECK_THROW(ParseScript("OP_6"), std::runtime_error);
    BOOST_CHECK_THROW(ParseScript("OP_7"), std::runtime_error);
    BOOST_CHECK_THROW(ParseScript("OP_8"), std::runtime_error);
    BOOST_CHECK_THROW(ParseScript("OP_9"), std::runtime_error);
    BOOST_CHECK_THROW(ParseScript("OP_10"), std::runtime_error);
    BOOST_CHECK_THROW(ParseScript("OP_11"), std::runtime_error);
    BOOST_CHECK_THROW(ParseScript("OP_12"), std::runtime_error);
    BOOST_CHECK_THROW(ParseScript("OP_13"), std::runtime_error);
    BOOST_CHECK_THROW(ParseScript("OP_14"), std::runtime_error);
    BOOST_CHECK_THROW(ParseScript("OP_15"), std::runtime_error);
    BOOST_CHECK_THROW(ParseScript("OP_16"), std::runtime_error);
    const CScript delimiters{ParseScript("1 2\t3\n4")};
    BOOST_CHECK_EQUAL(4U, delimiters.size());
    const CScript add{ParseScript("OP_ADD ADD")};
    BOOST_CHECK_EQUAL(2U, add.size());

    BOOST_CHECK_THROW(ParseScript("OP_ADDx"), std::runtime_error);
    BOOST_CHECK_THROW(ParseScript("OP_2OP_ADD"), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(parse_hex_test) {
    std::string s = "0x";
    BOOST_CHECK_THROW(ParseScript(s), std::runtime_error);

    for (int numZeroes = 1; numZeroes <= 32; numZeroes++) {
        s += "0";
        if (numZeroes % 2 == 0) {
            BOOST_CHECK_NO_THROW(ParseScript(s));
        } else {
            BOOST_CHECK_THROW(ParseScript(s), std::runtime_error);
        }
    }
}

static void PrintLE(std::ostringstream &testString, size_t bytes,
                    size_t pushLength) {
    testString << "0x";
    while (bytes != 0) {
        testString << std::setfill('0') << std::setw(2) << std::hex
                   << pushLength % 256;
        pushLength /= 256;
        bytes--;
    }
}

static std::string TestPushOpcode(size_t pushWidth, size_t pushLength,
                                  size_t actualLength) {
    std::ostringstream testString;

    switch (pushWidth) {
        case 1:
            testString << "PUSHDATA1 ";
            break;
        case 2:
            testString << "PUSHDATA2 ";
            break;
        case 4:
            testString << "PUSHDATA4 ";
            break;
        default:
            assert(false);
    }
    PrintLE(testString, pushWidth, pushLength);
    testString << " 0x";

    for (size_t i = 0; i < actualLength; i++) {
        testString << "01";
    }

    return testString.str();
}

BOOST_AUTO_TEST_CASE(printle_tests) {
    // Ensure the test generator is doing what we think it is.
    std::ostringstream testString;
    PrintLE(testString, 04, 0x8001);
    BOOST_CHECK_EQUAL(testString.str(), "0x01800000");
}

BOOST_AUTO_TEST_CASE(testpushopcode_tests) {
    BOOST_CHECK_EQUAL(TestPushOpcode(1, 2, 2), "PUSHDATA1 0x02 0x0101");
    BOOST_CHECK_EQUAL(TestPushOpcode(2, 2, 2), "PUSHDATA2 0x0200 0x0101");
    BOOST_CHECK_EQUAL(TestPushOpcode(4, 2, 2), "PUSHDATA4 0x02000000 0x0101");
}

BOOST_AUTO_TEST_CASE(parse_push_test) {
    BOOST_CHECK_NO_THROW(ParseScript("0x01 0x01"));
    BOOST_CHECK_NO_THROW(ParseScript("0x01 XOR"));
    BOOST_CHECK_NO_THROW(ParseScript("0x01 1"));
    BOOST_CHECK_NO_THROW(ParseScript("0x01 ''"));
    BOOST_CHECK_NO_THROW(ParseScript("0x02 0x0101"));
    BOOST_CHECK_NO_THROW(ParseScript("0x02 42"));
    BOOST_CHECK_NO_THROW(ParseScript("0x02 'a'"));

    BOOST_CHECK_THROW(ParseScript("0x01 0x0101"), std::runtime_error);
    BOOST_CHECK_THROW(ParseScript("0x01 42"), std::runtime_error);
    BOOST_CHECK_THROW(ParseScript("0x02 0x01"), std::runtime_error);
    BOOST_CHECK_THROW(ParseScript("0x02 XOR"), std::runtime_error);
    BOOST_CHECK_THROW(ParseScript("0x02 1"), std::runtime_error);
    BOOST_CHECK_THROW(ParseScript("0x02 ''"), std::runtime_error);
    BOOST_CHECK_THROW(ParseScript("0x02 0x010101"), std::runtime_error);
    BOOST_CHECK_THROW(ParseScript("0x02 'ab'"), std::runtime_error);

    // Note sizes are LE encoded.  Also, some of these values are not
    // minimally encoded intentionally -- nor are they being required to be
    // minimally encoded.
    BOOST_CHECK_NO_THROW(ParseScript("PUSHDATA4 0x02000000 0x0101"));
    BOOST_CHECK_THROW(ParseScript("PUSHDATA4 0x03000000 0x0101"),
                      std::runtime_error);
    BOOST_CHECK_THROW(ParseScript("PUSHDATA4 0x02000000 0x010101"),
                      std::runtime_error);
    BOOST_CHECK_THROW(ParseScript("PUSHDATA4 0x020000 0x0101"),
                      std::runtime_error);
    BOOST_CHECK_THROW(ParseScript("PUSHDATA4 0x0200000000 0x0101"),
                      std::runtime_error);

    BOOST_CHECK_NO_THROW(ParseScript("PUSHDATA2 0x0200 0x0101"));
    BOOST_CHECK_THROW(ParseScript("PUSHDATA2 0x0300 0x0101"),
                      std::runtime_error);
    BOOST_CHECK_THROW(ParseScript("PUSHDATA2 0x030000 0x0101"),
                      std::runtime_error);
    BOOST_CHECK_NO_THROW(ParseScript("PUSHDATA1 0x02 0x0101"));
    BOOST_CHECK_THROW(ParseScript("PUSHDATA1 0x02 0x010101"),
                      std::runtime_error);
    BOOST_CHECK_THROW(ParseScript("PUSHDATA1 0x0200 0x010101"),
                      std::runtime_error);

    // Ensure pushdata handling is not using 1's complement
    BOOST_CHECK_NO_THROW(ParseScript(TestPushOpcode(1, 0xC8, 0xC8)));
    BOOST_CHECK_THROW(ParseScript(TestPushOpcode(1, 0xC8, 0xC9)),
                      std::runtime_error);

    BOOST_CHECK_NO_THROW(ParseScript(TestPushOpcode(2, 0x8000, 0x8000)));
    BOOST_CHECK_THROW(ParseScript(TestPushOpcode(2, 0x8000, 0x8001)),
                      std::runtime_error);
    BOOST_CHECK_THROW(ParseScript(TestPushOpcode(2, 0x8001, 0x8000)),
                      std::runtime_error);
    BOOST_CHECK_THROW(ParseScript(TestPushOpcode(2, 0x80, 0x81)),
                      std::runtime_error);
    BOOST_CHECK_THROW(ParseScript(TestPushOpcode(2, 0x80, 0x7F)),
                      std::runtime_error);

    // Can't build something too long.
    BOOST_CHECK_NO_THROW(ParseScript(TestPushOpcode(4, 0x8000, 0x8000)));
    BOOST_CHECK_THROW(ParseScript(TestPushOpcode(4, 0x8000, 0x8001)),
                      std::runtime_error);
    BOOST_CHECK_THROW(ParseScript(TestPushOpcode(4, 0x8001, 0x8000)),
                      std::runtime_error);
    BOOST_CHECK_THROW(ParseScript(TestPushOpcode(4, 0x80, 0x81)),
                      std::runtime_error);
    BOOST_CHECK_THROW(ParseScript(TestPushOpcode(4, 0x80, 0x7F)),
                      std::runtime_error);
}

BOOST_AUTO_TEST_SUITE_END()
