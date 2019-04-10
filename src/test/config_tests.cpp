// Copyright (c) 2016 The Bitcoin Core developers
// Copyright (c) 2019 The Bitcoin SV developers
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

BOOST_AUTO_TEST_CASE(max_block_size_related_defaults) {

    GlobalConfig config;

    // Make up some dummy parameters taking into account the following rules
    // - Block size should be at least 1000 
    // - generated block size can not be larger than received block size - 1000
    DefaultBlockSizeParams defaultParams {
        // activation time 
        1000,
        // max block size before activation
        5000,
        // max block size after activation
        6000,
        // max generated block size before activation
        3000,
        // max generated block size after activation
        4000
    };

    config.SetDefaultBlockSizeParams(defaultParams);

    // Providing defaults should not override anything
    BOOST_CHECK(!config.MaxBlockSizeOverridden());
    BOOST_CHECK(!config.MaxGeneratedBlockSizeOverridden());

    BOOST_CHECK_EQUAL(config.GetBlockSizeActivationTime(), 1000);

    // Functions that do not take time parameter should return future data
    BOOST_CHECK_EQUAL(config.GetMaxBlockSize(), defaultParams.maxBlockSizeAfter);
    BOOST_CHECK_EQUAL(config.GetMaxGeneratedBlockSize(), defaultParams.maxGeneratedBlockSizeAfter);


    ///////////////////
    /// Test with default values - they should change based on activation time
    /////////////////
        
    // Functions that do take time parameter should return old values before activation time
    BOOST_CHECK_EQUAL(config.GetMaxBlockSize(999), defaultParams.maxBlockSizeBefore);
    BOOST_CHECK_EQUAL(config.GetMaxGeneratedBlockSize(999), defaultParams.maxGeneratedBlockSizeBefore);

    // Functions that do take time parameter should return new values on activation time
    BOOST_CHECK_EQUAL(config.GetMaxBlockSize(1000), defaultParams.maxBlockSizeAfter);
    BOOST_CHECK_EQUAL(config.GetMaxGeneratedBlockSize(1000), defaultParams.maxGeneratedBlockSizeAfter);

    // Functions that do take time parameter should return new value after activation date
    BOOST_CHECK_EQUAL(config.GetMaxBlockSize(1001), defaultParams.maxBlockSizeAfter);
    BOOST_CHECK_EQUAL(config.GetMaxGeneratedBlockSize(1001), defaultParams.maxGeneratedBlockSizeAfter);

    // Override one of the values, the overriden value should be used regardless of time.
    // Minimum allowed received block size is 1 MB, so we use 8 MB
    uint64_t overridenMaxBlockSize { 8 * ONE_MEGABYTE };

    BOOST_CHECK(config.SetMaxBlockSize(overridenMaxBlockSize));
    BOOST_CHECK_EQUAL(config.GetMaxBlockSize(999), overridenMaxBlockSize);
    BOOST_CHECK_EQUAL(config.GetMaxGeneratedBlockSize(999), defaultParams.maxGeneratedBlockSizeBefore);

    BOOST_CHECK_EQUAL(config.GetMaxBlockSize(1000), overridenMaxBlockSize);
    BOOST_CHECK_EQUAL(config.GetMaxGeneratedBlockSize(1000), defaultParams.maxGeneratedBlockSizeAfter);

    BOOST_CHECK_EQUAL(config.GetMaxBlockSize(1001), overridenMaxBlockSize);
    BOOST_CHECK_EQUAL(config.GetMaxGeneratedBlockSize(1001), defaultParams.maxGeneratedBlockSizeAfter);


    // Override the generated block size, which must be smaller than received block size
    uint64_t overridenMagGeneratedBlockSize = overridenMaxBlockSize - ONE_MEGABYTE;

    BOOST_CHECK(config.SetMaxGeneratedBlockSize(overridenMagGeneratedBlockSize));
    BOOST_CHECK_EQUAL(config.GetMaxBlockSize(999), overridenMaxBlockSize);
    BOOST_CHECK_EQUAL(config.GetMaxGeneratedBlockSize(999), overridenMagGeneratedBlockSize);

    BOOST_CHECK_EQUAL(config.GetMaxBlockSize(1000), overridenMaxBlockSize);
    BOOST_CHECK_EQUAL(config.GetMaxGeneratedBlockSize(1000), overridenMagGeneratedBlockSize);

    BOOST_CHECK_EQUAL(config.GetMaxBlockSize(1001), overridenMaxBlockSize);
    BOOST_CHECK_EQUAL(config.GetMaxGeneratedBlockSize(1001), overridenMagGeneratedBlockSize);

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
