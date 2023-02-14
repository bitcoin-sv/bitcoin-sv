// Copyright (c) 2012-2015 The Bitcoin Core developers
// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "test/test_bitcoin.h"
#include "util.h"

#include <string>
#include <vector>

#include <boost/algorithm/string.hpp>
#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(getarg_tests, BasicTestingSetup)

static void ResetArgs(const std::string &strArg) {
    std::vector<std::string> vecArg;
    if (strArg.size())
        boost::split(vecArg, strArg, boost::is_space(),
                     boost::token_compress_on);

    // Insert dummy executable name:
    vecArg.insert(vecArg.begin(), "testbitcoin");

    // Convert to char*:
    std::vector<const char *> vecChar;
    for (std::string &s : vecArg) {
        vecChar.push_back(s.c_str());
    }

    gArgs.ParseParameters(vecChar.size(), &vecChar[0]);
}

BOOST_AUTO_TEST_CASE(boolarg) {
    ResetArgs("-foo");
    BOOST_CHECK(gArgs.GetBoolArg("-foo", false));
    BOOST_CHECK(gArgs.GetBoolArg("-foo", true));

    BOOST_CHECK(!gArgs.GetBoolArg("-fo", false));
    BOOST_CHECK(gArgs.GetBoolArg("-fo", true));

    BOOST_CHECK(!gArgs.GetBoolArg("-fooo", false));
    BOOST_CHECK(gArgs.GetBoolArg("-fooo", true));

    ResetArgs("-foo=0");
    BOOST_CHECK(!gArgs.GetBoolArg("-foo", false));
    BOOST_CHECK(!gArgs.GetBoolArg("-foo", true));

    ResetArgs("-foo=1");
    BOOST_CHECK(gArgs.GetBoolArg("-foo", false));
    BOOST_CHECK(gArgs.GetBoolArg("-foo", true));

    // New 0.6 feature: auto-map -nosomething to !-something:
    ResetArgs("-nofoo");
    BOOST_CHECK(!gArgs.GetBoolArg("-foo", false));
    BOOST_CHECK(!gArgs.GetBoolArg("-foo", true));

    ResetArgs("-nofoo=1");
    BOOST_CHECK(!gArgs.GetBoolArg("-foo", false));
    BOOST_CHECK(!gArgs.GetBoolArg("-foo", true));

    // -nofoo should win
    ResetArgs("-foo -nofoo");
    BOOST_CHECK(!gArgs.GetBoolArg("-foo", false));
    BOOST_CHECK(!gArgs.GetBoolArg("-foo", true));

    // -nofoo should win
    ResetArgs("-foo=1 -nofoo=1");
    BOOST_CHECK(!gArgs.GetBoolArg("-foo", false));
    BOOST_CHECK(!gArgs.GetBoolArg("-foo", true));

    // -nofoo=0 should win
    ResetArgs("-foo=0 -nofoo=0");
    BOOST_CHECK(gArgs.GetBoolArg("-foo", false));
    BOOST_CHECK(gArgs.GetBoolArg("-foo", true));

    // New 0.6 feature: treat -- same as -:
    ResetArgs("--foo=1");
    BOOST_CHECK(gArgs.GetBoolArg("-foo", false));
    BOOST_CHECK(gArgs.GetBoolArg("-foo", true));

    ResetArgs("--nofoo=1");
    BOOST_CHECK(!gArgs.GetBoolArg("-foo", false));
    BOOST_CHECK(!gArgs.GetBoolArg("-foo", true));
}

BOOST_AUTO_TEST_CASE(stringarg) {
    ResetArgs("");
    BOOST_CHECK_EQUAL(gArgs.GetArg("-foo", ""), "");
    BOOST_CHECK_EQUAL(gArgs.GetArg("-foo", "eleven"), "eleven");

    ResetArgs("-foo -bar");
    BOOST_CHECK_EQUAL(gArgs.GetArg("-foo", ""), "");
    BOOST_CHECK_EQUAL(gArgs.GetArg("-foo", "eleven"), "");

    ResetArgs("-foo=");
    BOOST_CHECK_EQUAL(gArgs.GetArg("-foo", ""), "");
    BOOST_CHECK_EQUAL(gArgs.GetArg("-foo", "eleven"), "");

    ResetArgs("-foo=11");
    BOOST_CHECK_EQUAL(gArgs.GetArg("-foo", ""), "11");
    BOOST_CHECK_EQUAL(gArgs.GetArg("-foo", "eleven"), "11");

    ResetArgs("-foo=eleven");
    BOOST_CHECK_EQUAL(gArgs.GetArg("-foo", ""), "eleven");
    BOOST_CHECK_EQUAL(gArgs.GetArg("-foo", "eleven"), "eleven");
}

BOOST_AUTO_TEST_CASE(intarg) {
    ResetArgs("");
    BOOST_CHECK_EQUAL(gArgs.GetArg("-foo", 11), 11);
    BOOST_CHECK_EQUAL(gArgs.GetArg("-foo", 0), 0);

    ResetArgs("-foo -bar");
	BOOST_CHECK_EQUAL(gArgs.GetArg("-foo",11),11);
	BOOST_CHECK_EQUAL(gArgs.GetArg("-bar",11),11);
    ResetArgs("-foo=11 -bar=12");
    BOOST_CHECK_EQUAL(gArgs.GetArg("-foo", 0), 11);
    BOOST_CHECK_EQUAL(gArgs.GetArg("-bar", 11), 12);

    ResetArgs("-foo=NaN -bar=NotANumber");
	BOOST_CHECK_EQUAL(gArgs.GetArg("-foo",1),1);
	BOOST_CHECK_EQUAL(gArgs.GetArg("-bar",11),11);
    BOOST_CHECK_EQUAL(gArgs.GetArgAsBytes("-bar", 11, 1000), 11000);

    ResetArgs("-foo=7 -byte=7B -kilo=7kB -mega=7MB -giga=7GB");
    BOOST_CHECK_EQUAL(gArgs.GetArgAsBytes("-foo", 11), 7);
    BOOST_CHECK_EQUAL(gArgs.GetArgAsBytes("-byte", 11), 7);
    BOOST_CHECK_EQUAL(gArgs.GetArgAsBytes("-kilo", 11), 7000);
    BOOST_CHECK_EQUAL(gArgs.GetArgAsBytes("-mega", 11), 7000000);
    BOOST_CHECK_EQUAL(gArgs.GetArgAsBytes("-giga", 11), 7000000000);
    ResetArgs("-kibibyte=7kiB -mebibyte=7MiB -gibibyte=0.5GiB");
    BOOST_CHECK_EQUAL(gArgs.GetArgAsBytes("-kibibyte", 11), 7* static_cast<int64_t>(ONE_KIBIBYTE));
    BOOST_CHECK_EQUAL(gArgs.GetArgAsBytes("-mebibyte", 11), 7* static_cast<int64_t>(ONE_MEBIBYTE));
    BOOST_CHECK_EQUAL(gArgs.GetArgAsBytes("-gibibyte", 11), 0.5* static_cast<int64_t>(ONE_GIBIBYTE));
    ResetArgs("-foo -bar");
    BOOST_CHECK_EQUAL(gArgs.GetArgAsBytes("-foo", 7, 10), 70);
    BOOST_CHECK_EQUAL(gArgs.GetArgAsBytes("-foo", 7, 1000), 7000);
    BOOST_CHECK_EQUAL(gArgs.GetArgAsBytes("-bar", 7, 0), 0);
    ResetArgs("-foo=7kBMB");
    BOOST_CHECK_EQUAL(gArgs.GetArgAsBytes("-foo", 7), 7);
    BOOST_CHECK_EQUAL(gArgs.GetArgAsBytes("-foo", 7, 10), 70);
}

BOOST_AUTO_TEST_CASE(doublearg)
{
    ResetArgs("");
    BOOST_CHECK_EQUAL(gArgs.GetDoubleArg("-foo", 11.5), 11.5);
    BOOST_CHECK_EQUAL(gArgs.GetDoubleArg("-foo", 0.0), 0.0);

    ResetArgs("-foo -bar");
	BOOST_CHECK_EQUAL(gArgs.GetDoubleArg("-foo",11.5),11.5);
	BOOST_CHECK_EQUAL(gArgs.GetDoubleArg("-bar",11.5),11.5);
    ResetArgs("-foo=11.5 -bar=12.5");
    BOOST_CHECK_EQUAL(gArgs.GetDoubleArg("-foo", 0), 11.5);
    BOOST_CHECK_EQUAL(gArgs.GetDoubleArg("-bar", 11.5), 12.5);
}

BOOST_AUTO_TEST_CASE(doubledash) {
    ResetArgs("--foo");
    BOOST_CHECK_EQUAL(gArgs.GetBoolArg("-foo", false), true);

    ResetArgs("--foo=verbose --bar=1");
    BOOST_CHECK_EQUAL(gArgs.GetArg("-foo", ""), "verbose");
    BOOST_CHECK_EQUAL(gArgs.GetArg("-bar", 0), 1);
}

BOOST_AUTO_TEST_CASE(boolargno) {
    ResetArgs("-nofoo");
    BOOST_CHECK(!gArgs.GetBoolArg("-foo", true));
    BOOST_CHECK(!gArgs.GetBoolArg("-foo", false));

    ResetArgs("-nofoo=1");
    BOOST_CHECK(!gArgs.GetBoolArg("-foo", true));
    BOOST_CHECK(!gArgs.GetBoolArg("-foo", false));

    ResetArgs("-nofoo=0");
    BOOST_CHECK(gArgs.GetBoolArg("-foo", true));
    BOOST_CHECK(gArgs.GetBoolArg("-foo", false));

    // --nofoo should win
    ResetArgs("-foo --nofoo");
    BOOST_CHECK(!gArgs.GetBoolArg("-foo", true));
    BOOST_CHECK(!gArgs.GetBoolArg("-foo", false));

    // foo always wins:
    ResetArgs("-nofoo -foo");
    BOOST_CHECK(gArgs.GetBoolArg("-foo", true));
    BOOST_CHECK(gArgs.GetBoolArg("-foo", false));
}

BOOST_AUTO_TEST_SUITE_END()
