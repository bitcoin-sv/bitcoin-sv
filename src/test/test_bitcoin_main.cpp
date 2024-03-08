// Copyright (c) 2011-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#define BOOST_TEST_MODULE Bitcoin Test Suite

#include <boost/test/unit_test.hpp>
#include "logging.h"
#include <algorithm>

bool HasCustomOption(std::string option)
{
    const auto& argc = boost::unit_test::framework::master_test_suite().argc;
    const auto& argv = boost::unit_test::framework::master_test_suite().argv;
    return std::any_of(argv, argv+argc, [&option](auto& arg) {return option == arg;});
}

struct EnableLoggingFixture {
    EnableLoggingFixture() {
        std::string option {"--enable-logging"};
        if (HasCustomOption(option)) {
            GetLogger().EnableCategory(BCLog::ALL);
            GetLogger().fPrintToConsole = true;
            GetLogger().fLogTimeMicros = true;
            GetLogger().fLogTimestamps = true;
        } else {
            BOOST_TEST_MESSAGE("To enable logging, run the unit tests with   -- " << option);
        }
    }
};

BOOST_TEST_GLOBAL_FIXTURE(EnableLoggingFixture);
