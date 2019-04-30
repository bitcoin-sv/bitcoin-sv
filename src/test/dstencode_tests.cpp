// Copyright (c) 2017 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chainparams.h"
#include "config.h"
#include "dstencode.h"
#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(dstencode_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(test_addresses) {

    DummyConfig config(CBaseChainParams::MAIN);
    
    std::vector<uint8_t> hash = {118, 160, 64,  83,  189, 160, 168,
                                 139, 218, 81,  119, 184, 106, 21,
                                 195, 178, 159, 85,  152, 115};

    const CTxDestination dstKey = CKeyID(uint160(hash));
    const CTxDestination dstScript = CScriptID(uint160(hash));

    std::string base58_pubkey = "1BpEi6DfDAUFd7GtittLSdBeYJvcoaVggu";
    std::string base58_script = "3CWFddi6m4ndiGyKqzYvsFYagqDLPVMTzC";

    BOOST_CHECK_EQUAL(base58_pubkey, EncodeDestination(dstKey, config));
    BOOST_CHECK_EQUAL(base58_script, EncodeDestination(dstScript, config));

    // Check decoding
    const CChainParams &params = config.GetChainParams();
    BOOST_CHECK(dstKey == DecodeDestination(base58_pubkey, params));
    BOOST_CHECK(dstScript == DecodeDestination(base58_script, params));

    // Validation
    BOOST_CHECK(IsValidDestinationString(base58_pubkey, params));
    BOOST_CHECK(IsValidDestinationString(base58_script, params));
    BOOST_CHECK(!IsValidDestinationString("notvalid", params));
}

BOOST_AUTO_TEST_SUITE_END()
