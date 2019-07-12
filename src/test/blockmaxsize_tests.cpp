// Copyright (c) 2017 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "consensus/consensus.h"
#include "chainparams.h"
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

BOOST_FIXTURE_TEST_SUITE(blockmaxsize_tests, TestingSetup)

BOOST_AUTO_TEST_CASE(blockmaxsize_rpc) {

    BOOST_CHECK_THROW(CallRPC("setblockmaxsize"), std::runtime_error);
    BOOST_CHECK_THROW(CallRPC("setblockmaxsize not_uint"),
                      boost::bad_lexical_cast);
    BOOST_CHECK_THROW(CallRPC("setblockmaxsize 1000000 not_uint"),
                      std::runtime_error);
    BOOST_CHECK_THROW(CallRPC("setblockmaxsize 1000000 1"),
                      std::runtime_error);
    BOOST_CHECK_THROW(CallRPC("setblockmaxsize -1"),
                      boost::bad_lexical_cast);


    BOOST_CHECK_NO_THROW(CallRPC(std::string("setblockmaxsize ") +
                                     std::to_string(ONE_MEGABYTE + 1)));
    BOOST_CHECK_NO_THROW(CallRPC(std::string("setblockmaxsize ") +
                                     std::to_string(ONE_MEGABYTE + 10)));
}

BOOST_AUTO_TEST_SUITE_END()
