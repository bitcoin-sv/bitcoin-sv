// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "test/test_bitcoin.h"
#include "validation.h"

#include <fstream>
#include <sstream>
#include <thread>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(alertnotify_tests, TestingSetup)

BOOST_AUTO_TEST_CASE(alertnotify_test) {
    fs::path tmpfile_name =
        pathTemp / strprintf("alertnotify_test_%lu_%i",
                             (unsigned long)GetTime(),
                             (int)(InsecureRandRange(100000)));
    gArgs.ForceSetArg("-alertnotify",
                      strprintf("echo %%s>> %s", tmpfile_name.string())); // NOTE: Space between '%%s' and '>>' is deliberately omitted because on Windows echo command would also output that space.

    std::string msg = "This is just an alert!";
    AlertNotify(msg);
    std::this_thread::sleep_for( std::chrono::seconds(3) ); // Give some time for AlertNotify spawned thread to finish

    std::ifstream t(tmpfile_name.string());
    std::stringstream buffer;
    buffer << t.rdbuf();
    BOOST_CHECK_EQUAL(SanitizeString(buffer.str()), SanitizeString(msg));
}

BOOST_AUTO_TEST_SUITE_END()
