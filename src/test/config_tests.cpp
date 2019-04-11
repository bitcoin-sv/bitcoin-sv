// Copyright (c) 2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chainparams.h"
#include "config.h"
#include "consensus/consensus.h"
#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(config_tests, BasicTestingSetup)

static bool isSetDefaultBlockSizeParamsCalledException(const std::runtime_error &ex) {
    static std::string expectedException("GlobalConfig::SetDefaultBlockSizeParams must be called before accessing block size related parameters");
    return expectedException == ex.what();
}

BOOST_AUTO_TEST_CASE(max_block_size) {
    GlobalConfig config;

    // SetDefaultBlockSizeParams must be called before using config block size parameters
    // otherwise getters rise exceptions
    BOOST_CHECK_EXCEPTION(config.GetMaxBlockSize(), std::runtime_error, isSetDefaultBlockSizeParamsCalledException);
    BOOST_CHECK_EXCEPTION(config.GetMaxBlockSize(0), std::runtime_error, isSetDefaultBlockSizeParamsCalledException);
    BOOST_CHECK_EXCEPTION(config.GetMaxGeneratedBlockSize(), std::runtime_error, isSetDefaultBlockSizeParamsCalledException);
    BOOST_CHECK_EXCEPTION(config.GetMaxGeneratedBlockSize(0), std::runtime_error, isSetDefaultBlockSizeParamsCalledException);
    BOOST_CHECK_EXCEPTION(config.GetBlockSizeActivationTime(), std::runtime_error, isSetDefaultBlockSizeParamsCalledException);

    config.SetDefaultBlockSizeParams(Params().GetDefaultBlockSizeParams());

    // Too small.
    BOOST_CHECK(!config.SetMaxBlockSize(0));
    BOOST_CHECK(!config.SetMaxBlockSize(12345));
    BOOST_CHECK(!config.SetMaxBlockSize(LEGACY_MAX_BLOCK_SIZE - 1));
    BOOST_CHECK(!config.SetMaxBlockSize(LEGACY_MAX_BLOCK_SIZE));

    // LEGACY_MAX_BLOCK_SIZE + 1
    BOOST_CHECK(config.SetMaxBlockSize(LEGACY_MAX_BLOCK_SIZE + 1));
    BOOST_CHECK_EQUAL(config.GetMaxBlockSize(), LEGACY_MAX_BLOCK_SIZE + 1);

    // 2MB
    BOOST_CHECK(config.SetMaxBlockSize(2 * ONE_MEGABYTE));
    BOOST_CHECK_EQUAL(config.GetMaxBlockSize(), 2 * ONE_MEGABYTE);

    // 8MB
    BOOST_CHECK(config.SetMaxBlockSize(8 * ONE_MEGABYTE));
    BOOST_CHECK_EQUAL(config.GetMaxBlockSize(), 8 * ONE_MEGABYTE);

    // Invalid size keep config.
    BOOST_CHECK(!config.SetMaxBlockSize(54321));
    BOOST_CHECK_EQUAL(config.GetMaxBlockSize(), 8 * ONE_MEGABYTE);

    // Setting it back down
    BOOST_CHECK(config.SetMaxBlockSize(7 * ONE_MEGABYTE));
    BOOST_CHECK_EQUAL(config.GetMaxBlockSize(), 7 * ONE_MEGABYTE);
    BOOST_CHECK(config.SetMaxBlockSize(ONE_MEGABYTE + 1));
    BOOST_CHECK_EQUAL(config.GetMaxBlockSize(), ONE_MEGABYTE + 1);
}

BOOST_AUTO_TEST_CASE(hex_to_array) {
    const std::string hexstr = "0a0b0C0D";//Lower and Upper char should both work
    CMessageHeader::MessageMagic array;
    BOOST_CHECK(HexToArray(hexstr, array)); 
    BOOST_CHECK_EQUAL(array[0],10);
    BOOST_CHECK_EQUAL(array[1],11);
    BOOST_CHECK_EQUAL(array[2],12);
    BOOST_CHECK_EQUAL(array[3],13);
}

BOOST_AUTO_TEST_CASE(chain_params) {
    GlobalConfig config;

    // Global config is consistent with params.
    SelectParams(CBaseChainParams::MAIN);
    BOOST_CHECK_EQUAL(&Params(), &config.GetChainParams());

    SelectParams(CBaseChainParams::TESTNET);
    BOOST_CHECK_EQUAL(&Params(), &config.GetChainParams());

    SelectParams(CBaseChainParams::REGTEST);
    BOOST_CHECK_EQUAL(&Params(), &config.GetChainParams());
}

BOOST_AUTO_TEST_SUITE_END()
