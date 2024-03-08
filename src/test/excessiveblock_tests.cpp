// Copyright (c) 2017 The Bitcoin developers
// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "consensus/consensus.h"
#include "rpc/server.h"
#include "validation.h"
#include "test/test_bitcoin.h"

#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/test/unit_test.hpp>
#include <limits>
#include <string>
#include <config.h>

extern UniValue CallRPC(std::string strMethod);

BOOST_FIXTURE_TEST_SUITE(excessiveblock_tests, TestingSetup)

BOOST_AUTO_TEST_CASE(excessiveblock_rpc) {
    BOOST_CHECK_NO_THROW(CallRPC("getexcessiveblock"));

    BOOST_CHECK_THROW(CallRPC("setexcessiveblock"), std::runtime_error);
    BOOST_CHECK_THROW(CallRPC("setexcessiveblock not_uint"),
                      boost::bad_lexical_cast);
    BOOST_CHECK_THROW(CallRPC("setexcessiveblock 1000000 not_uint"),
                      std::runtime_error);
    BOOST_CHECK_THROW(CallRPC("setexcessiveblock 1000000 1"),
                      std::runtime_error);
    BOOST_CHECK_THROW(CallRPC("setexcessiveblock -1"), boost::bad_lexical_cast);

    // Check that unlimited value is set properly
    BOOST_CHECK_NO_THROW(CallRPC("setexcessiveblock 0"));
    UniValue result{};
    BOOST_CHECK_NO_THROW(result = CallRPC("getexcessiveblock"););
    BOOST_CHECK_EQUAL(static_cast<uint64_t>(find_value(result.get_obj(), "excessiveBlockSize").get_int64()),
        Params().GetDefaultBlockSizeParams().maxBlockSize);

    BOOST_CHECK_THROW(CallRPC("setexcessiveblock 1"), std::runtime_error);
    BOOST_CHECK_THROW(CallRPC("setexcessiveblock 1000"), std::runtime_error);
    BOOST_CHECK_THROW(CallRPC(std::string("setexcessiveblock ") +
                              std::to_string(ONE_MEGABYTE - 1)),
                      std::runtime_error);
    BOOST_CHECK_THROW(CallRPC(std::string("setexcessiveblock ") +
                              std::to_string(ONE_MEGABYTE)),
                      std::runtime_error);

    BOOST_CHECK_NO_THROW(CallRPC(std::string("setexcessiveblock ") +
                                 std::to_string(ONE_MEGABYTE + 1)));
    BOOST_CHECK_NO_THROW(CallRPC(std::string("setexcessiveblock ") +
                                 std::to_string(ONE_MEGABYTE + 10)));

    // Test that we are alowed to exceed blockfile size
    BOOST_CHECK_NO_THROW(CallRPC(std::string("setexcessiveblock ") +
                         std::to_string(DEFAULT_PREFERRED_BLOCKFILE_SIZE * 100)));

    // Default can be higher than 1MB in future - test it too
    auto nDefaultMaxBlockSize = GlobalConfig::GetConfig().GetMaxBlockSize();
    BOOST_CHECK_NO_THROW(CallRPC(std::string("setexcessiveblock ") +
                                 std::to_string(nDefaultMaxBlockSize)));
}

BOOST_AUTO_TEST_SUITE_END()
