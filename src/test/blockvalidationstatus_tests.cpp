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

extern UniValue CallRPC(std::string strMethod);

BOOST_FIXTURE_TEST_SUITE(blockvalidationstatus_tests, TestingSetup)

BOOST_AUTO_TEST_CASE(blockvalidationstatus_rpc) {
    BOOST_CHECK_NO_THROW(CallRPC("getcurrentlyvalidatingblocks"));
    BOOST_CHECK_NO_THROW(CallRPC("getwaitingblocks"));

    BOOST_CHECK_THROW(CallRPC("getcurrentlyvalidatingblocks some_param"), std::runtime_error);
    BOOST_CHECK_THROW(CallRPC("getwaitingblocks some_param"), std::runtime_error);

    BOOST_CHECK_THROW(CallRPC("waitaftervalidatingblock"), std::runtime_error);
    BOOST_CHECK_THROW(CallRPC("waitaftervalidatingblock 000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f add add"),
                      std::runtime_error);

    UniValue r;
    r = CallRPC("waitaftervalidatingblock not_uint add");
    BOOST_CHECK_EQUAL(find_value(r.get_obj(), "message").get_str(), "Wrong hexdecimal string");

    r = CallRPC("waitaftervalidatingblock 000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f diffrentaction");
    BOOST_CHECK_EQUAL(find_value(r.get_obj(),"message").get_str(), "Wrong action");

    BOOST_CHECK_NO_THROW(CallRPC("waitaftervalidatingblock 000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f add"));
    BOOST_CHECK_EQUAL(blockValidationStatus.getWaitingAfterValidationBlocks().at(0).ToString(), "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f");

    BOOST_CHECK_NO_THROW(CallRPC("waitaftervalidatingblock 000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f remove"));
    BOOST_CHECK(blockValidationStatus.getWaitingAfterValidationBlocks().size() == 0);


    uint256 dummyBlockHash = GetRandHash();
    CBlockIndex index;
    index.phashBlock = &dummyBlockHash;
    {
        auto guard = blockValidationStatus.getScopedCurrentlyValidatingBlock(index);
        BOOST_CHECK_EQUAL(
            blockValidationStatus.getCurrentlyValidatingBlocks().at(0).ToString(),
            dummyBlockHash.ToString());
    }
    BOOST_CHECK(blockValidationStatus.getCurrentlyValidatingBlocks().size() == 0);

}

BOOST_AUTO_TEST_SUITE_END()
