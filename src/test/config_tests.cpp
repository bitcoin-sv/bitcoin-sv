// Copyright (c) 2016 The Bitcoin Core developers
// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "chainparams.h"
#include "config.h"
#include "consensus/consensus.h"
#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>
#include <string>

std::ostream& operator<<(std::ostream& str, DSAttemptHandler::NotificationLevel level)
{
    str << static_cast<int>(level);
    return str;
}

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
    BOOST_CHECK_EXCEPTION(config.GetMaxGeneratedBlockSize(), std::runtime_error, isSetDefaultBlockSizeParamsCalledException);
    BOOST_CHECK_EXCEPTION(config.GetMaxGeneratedBlockSize(0), std::runtime_error, isSetDefaultBlockSizeParamsCalledException);
    BOOST_CHECK_EXCEPTION(config.GetBlockSizeActivationTime(), std::runtime_error, isSetDefaultBlockSizeParamsCalledException);

    config.SetDefaultBlockSizeParams(Params().GetDefaultBlockSizeParams());

    // Too small.
    std::string err = "";
    BOOST_CHECK(!config.SetMaxBlockSize(1, &err));
    BOOST_CHECK(!err.empty());
    err = "";
    BOOST_CHECK(!config.SetMaxBlockSize(12345, &err));
    BOOST_CHECK(!err.empty());
    BOOST_CHECK(!config.SetMaxBlockSize(LEGACY_MAX_BLOCK_SIZE - 1));
    BOOST_CHECK(!config.SetMaxBlockSize(LEGACY_MAX_BLOCK_SIZE));


    // LEGACY_MAX_BLOCK_SIZE + 1
    err = "";
    BOOST_CHECK(config.SetMaxBlockSize(LEGACY_MAX_BLOCK_SIZE + 1, &err));
    BOOST_CHECK(err.empty());
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
        // max block size
        6000,
        // max generated block size before activation
        3000,
        // max generated block size after activation
        4000
    };

    config.SetDefaultBlockSizeParams(defaultParams);

    // Make up genesis activation parameters 
    // - Genesis will be activated at block height 100
    int32_t heightActivateGenesis   = 100;
    config.SetGenesisActivationHeight(heightActivateGenesis);

    // Providing defaults should not override anything
    BOOST_CHECK(!config.MaxGeneratedBlockSizeOverridden());

    BOOST_CHECK_EQUAL(config.GetBlockSizeActivationTime(), 1000);
    BOOST_CHECK_EQUAL(config.GetGenesisActivationHeight(), 100);

    // Functions that do not take time parameter should return future data
    BOOST_CHECK_EQUAL(config.GetMaxBlockSize(), defaultParams.maxBlockSize);
    BOOST_CHECK_EQUAL(config.GetMaxGeneratedBlockSize(), defaultParams.maxGeneratedBlockSizeAfter);


    ///////////////////
    /// Test with default values - they should change based on activation time
    /////////////////
       
    // Functions that do take time parameter should return old values before activation time
    BOOST_CHECK_EQUAL(config.GetMaxGeneratedBlockSize(999), defaultParams.maxGeneratedBlockSizeBefore);

    // Functions that do take time parameter should return new values on activation time
    BOOST_CHECK_EQUAL(config.GetMaxGeneratedBlockSize(1000), defaultParams.maxGeneratedBlockSizeAfter);

    // Functions that do take time parameter should return new value after activation date
    BOOST_CHECK_EQUAL(config.GetMaxGeneratedBlockSize(1001), defaultParams.maxGeneratedBlockSizeAfter);

    // Override one of the values, the overriden value should be used regardless of time.
    // Minimum allowed received block size is 1 MB, so we use 8 MB
    uint64_t overridenMaxBlockSize { 8 * ONE_MEGABYTE };

    BOOST_CHECK(config.SetMaxBlockSize(overridenMaxBlockSize));
    BOOST_CHECK_EQUAL(config.GetMaxBlockSize(), overridenMaxBlockSize);
    BOOST_CHECK_EQUAL(config.GetMaxGeneratedBlockSize(999), defaultParams.maxGeneratedBlockSizeBefore);

    BOOST_CHECK_EQUAL(config.GetMaxGeneratedBlockSize(1000), defaultParams.maxGeneratedBlockSizeAfter);

    BOOST_CHECK_EQUAL(config.GetMaxGeneratedBlockSize(1001), defaultParams.maxGeneratedBlockSizeAfter);


    // Override the generated block size, which must be smaller than received block size
    uint64_t overridenMagGeneratedBlockSize = overridenMaxBlockSize - ONE_MEGABYTE;

    BOOST_CHECK(config.SetMaxGeneratedBlockSize(overridenMagGeneratedBlockSize));
    BOOST_CHECK_EQUAL(config.GetMaxGeneratedBlockSize(999), overridenMagGeneratedBlockSize);

    BOOST_CHECK_EQUAL(config.GetMaxGeneratedBlockSize(1000), overridenMagGeneratedBlockSize);

    BOOST_CHECK_EQUAL(config.GetMaxGeneratedBlockSize(1001), overridenMagGeneratedBlockSize);

}

BOOST_AUTO_TEST_CASE(max_tx_size) {

    GlobalConfig config;
    std::string reason;
    int64_t newMaxTxSizePolicy{ MAX_TX_SIZE_POLICY_BEFORE_GENESIS + 1 };


    // default pre genesis policy tx size
    BOOST_CHECK(config.GetMaxTxSize(false, false) == MAX_TX_SIZE_POLICY_BEFORE_GENESIS);

    // default post genesis policy tx size
    BOOST_CHECK(config.GetMaxTxSize(true, false) == DEFAULT_MAX_TX_SIZE_POLICY_AFTER_GENESIS);

    // default pre genesis consensus tx size
    BOOST_CHECK(config.GetMaxTxSize(false, true) == MAX_TX_SIZE_CONSENSUS_BEFORE_GENESIS);

    // default post genesis consensus tx size
    BOOST_CHECK(config.GetMaxTxSize(true, true) == MAX_TX_SIZE_CONSENSUS_AFTER_GENESIS);


    // can not set policy tx size < pre genesis policy tx size
    BOOST_CHECK(!config.SetMaxTxSizePolicy(MAX_TX_SIZE_POLICY_BEFORE_GENESIS - 1, &reason));

    // can not set policy tx size > post genesis consensus tx size
    BOOST_CHECK(!config.SetMaxTxSizePolicy(MAX_TX_SIZE_CONSENSUS_AFTER_GENESIS + 1, &reason));

    // can not set policy tx size < 0
    BOOST_CHECK(!config.SetMaxTxSizePolicy(- 1, &reason));


    // set new max policy tx size
    BOOST_CHECK(config.SetMaxTxSizePolicy(newMaxTxSizePolicy, &reason));

    // pre genesis policy tx size
    BOOST_CHECK(config.GetMaxTxSize(false, false) == MAX_TX_SIZE_POLICY_BEFORE_GENESIS);

    // post genesis policy tx size
    BOOST_CHECK(config.GetMaxTxSize(true, false) == static_cast<uint64_t>(newMaxTxSizePolicy));


    // set unlimited policy tx size
    BOOST_CHECK(config.SetMaxTxSizePolicy(0, &reason));

    // pre genesis policy tx size
    BOOST_CHECK(config.GetMaxTxSize(false, false) == MAX_TX_SIZE_POLICY_BEFORE_GENESIS);

    // post genesis policy tx size
    BOOST_CHECK(config.GetMaxTxSize(true, false) == MAX_TX_SIZE_CONSENSUS_AFTER_GENESIS);
}

BOOST_AUTO_TEST_CASE(max_bignum_length_policy) {

    GlobalConfig config;
    std::string reason;
    int64_t newMaxScriptNumLengthPolicy{ MAX_SCRIPT_NUM_LENGTH_BEFORE_GENESIS + 1 };

    // default pre genesis policy max length
    BOOST_CHECK(config.GetMaxScriptNumLength(false, false) == MAX_SCRIPT_NUM_LENGTH_BEFORE_GENESIS);

    // default post genesis policy max length
    BOOST_CHECK(config.GetMaxScriptNumLength(true, false) == DEFAULT_SCRIPT_NUM_LENGTH_POLICY_AFTER_GENESIS);

    // default pre genesis consensus max length
    BOOST_CHECK(config.GetMaxScriptNumLength(false, true) == MAX_SCRIPT_NUM_LENGTH_BEFORE_GENESIS);

    // default post genesis consensus max length
    BOOST_CHECK(config.GetMaxScriptNumLength(true, true) == MAX_SCRIPT_NUM_LENGTH_AFTER_GENESIS);

    // can not set script number length policy > post genesis consensus script number length
    BOOST_CHECK(!config.SetMaxScriptNumLengthPolicy(MAX_SCRIPT_NUM_LENGTH_AFTER_GENESIS + 1, &reason));

    // can not set policy script number length < 0
    BOOST_CHECK(!config.SetMaxScriptNumLengthPolicy(-1, &reason));

    // set new max policy script number length
    BOOST_CHECK(config.SetMaxScriptNumLengthPolicy(newMaxScriptNumLengthPolicy, &reason));

    // pre genesis policy script number length
    BOOST_CHECK(config.GetMaxScriptNumLength(false, false) == MAX_SCRIPT_NUM_LENGTH_BEFORE_GENESIS);

    // post genesis policy script number length
    BOOST_CHECK(config.GetMaxScriptNumLength(true, false) == static_cast<uint64_t>(newMaxScriptNumLengthPolicy));

    // set unlimited policy script number length
    BOOST_CHECK(config.SetMaxScriptNumLengthPolicy(0, &reason));

    // pre genesis policy script number length
    BOOST_CHECK(config.GetMaxScriptNumLength(false, false) == MAX_SCRIPT_NUM_LENGTH_BEFORE_GENESIS);

    // post genesis policy script number length
    BOOST_CHECK(config.GetMaxScriptNumLength(true, false) == MAX_SCRIPT_NUM_LENGTH_AFTER_GENESIS);
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

BOOST_AUTO_TEST_CASE(max_stack_size) {

    std::string reason;

    BOOST_CHECK(testConfig.SetMaxStackMemoryUsage(0, 0));
    BOOST_CHECK_EQUAL(testConfig.GetMaxStackMemoryUsage(true, true), static_cast<uint64_t>(INT64_MAX));
    BOOST_CHECK_EQUAL(testConfig.GetMaxStackMemoryUsage(true, false), static_cast<uint64_t>(INT64_MAX));

    BOOST_CHECK(testConfig.SetMaxStackMemoryUsage(0, DEFAULT_STACK_MEMORY_USAGE_POLICY_AFTER_GENESIS));
    BOOST_CHECK_EQUAL(testConfig.GetMaxStackMemoryUsage(true, true), static_cast<uint64_t>(INT64_MAX));
    BOOST_CHECK_EQUAL(testConfig.GetMaxStackMemoryUsage(true, false), DEFAULT_STACK_MEMORY_USAGE_POLICY_AFTER_GENESIS);

    BOOST_CHECK(!testConfig.SetMaxStackMemoryUsage(1000000, 0, &reason));

    BOOST_CHECK(testConfig.SetMaxStackMemoryUsage(200000000, DEFAULT_STACK_MEMORY_USAGE_POLICY_AFTER_GENESIS));
    BOOST_CHECK_EQUAL(testConfig.GetMaxStackMemoryUsage(true, true), 200000000U);
    BOOST_CHECK_EQUAL(testConfig.GetMaxStackMemoryUsage(true, false), DEFAULT_STACK_MEMORY_USAGE_POLICY_AFTER_GENESIS);

    BOOST_CHECK(!testConfig.SetMaxStackMemoryUsage(500, 600, &reason));

    BOOST_CHECK(testConfig.SetMaxStackMemoryUsage(600, 500));
    BOOST_CHECK_EQUAL(testConfig.GetMaxStackMemoryUsage(false, true), static_cast<uint64_t>(INT64_MAX));
    BOOST_CHECK_EQUAL(testConfig.GetMaxStackMemoryUsage(false, false), static_cast<uint64_t>(INT64_MAX));
    BOOST_CHECK_EQUAL(testConfig.GetMaxStackMemoryUsage(true, true), 600U);
    BOOST_CHECK_EQUAL(testConfig.GetMaxStackMemoryUsage(true, false), 500U);

    BOOST_CHECK(!testConfig.SetMaxStackMemoryUsage(-1, -2));
}

BOOST_AUTO_TEST_CASE(max_send_queues_size) {

    std::string reason;

    uint64_t testBlockSize = LEGACY_MAX_BLOCK_SIZE + 1;
    gArgs.ForceSetArg("-excessiveblocksize", to_string(testBlockSize));
    BOOST_CHECK(testConfig.SetMaxBlockSize(testBlockSize, &reason));
    BOOST_CHECK_EQUAL(testConfig.GetMaxSendQueuesBytes(), testBlockSize * DEFAULT_FACTOR_MAX_SEND_QUEUES_BYTES);

    uint64_t testFactor = 3;
    testConfig.SetFactorMaxSendQueuesBytes(testFactor);
    BOOST_CHECK_EQUAL(testConfig.GetMaxSendQueuesBytes(), testBlockSize * testFactor);
}

BOOST_AUTO_TEST_CASE(block_download_config)
{
    GlobalConfig config {};
    std::string err {};

    BOOST_CHECK_EQUAL(config.GetBlockStallingMinDownloadSpeed(), DEFAULT_MIN_BLOCK_STALLING_RATE);
    BOOST_CHECK(config.SetBlockStallingMinDownloadSpeed(2 * DEFAULT_MIN_BLOCK_STALLING_RATE, &err));
    BOOST_CHECK_EQUAL(config.GetBlockStallingMinDownloadSpeed(), 2 * DEFAULT_MIN_BLOCK_STALLING_RATE);
    BOOST_CHECK(config.SetBlockStallingMinDownloadSpeed(0, &err));
    BOOST_CHECK(!config.SetBlockStallingMinDownloadSpeed(-1, &err));

    BOOST_CHECK_EQUAL(config.GetBlockStallingTimeout(), DEFAULT_BLOCK_STALLING_TIMEOUT);
    BOOST_CHECK(config.SetBlockStallingTimeout(2 * DEFAULT_BLOCK_STALLING_TIMEOUT, &err));
    BOOST_CHECK_EQUAL(config.GetBlockStallingTimeout(), 2 * DEFAULT_BLOCK_STALLING_TIMEOUT);
    BOOST_CHECK(!config.SetBlockStallingTimeout(0, &err));
    BOOST_CHECK(!config.SetBlockStallingTimeout(-1, &err));

    BOOST_CHECK_EQUAL(config.GetBlockDownloadWindow(), DEFAULT_BLOCK_DOWNLOAD_WINDOW);
    BOOST_CHECK(config.SetBlockDownloadWindow(2 * DEFAULT_BLOCK_DOWNLOAD_WINDOW, &err));
    BOOST_CHECK_EQUAL(config.GetBlockDownloadWindow(), 2 * DEFAULT_BLOCK_DOWNLOAD_WINDOW);
    BOOST_CHECK(!config.SetBlockDownloadWindow(0, &err));
    BOOST_CHECK(!config.SetBlockDownloadWindow(-1, &err));

    BOOST_CHECK_EQUAL(config.GetBlockDownloadLowerWindow(), DEFAULT_BLOCK_DOWNLOAD_LOWER_WINDOW);
    BOOST_CHECK(config.SetBlockDownloadLowerWindow(2 * DEFAULT_BLOCK_DOWNLOAD_LOWER_WINDOW, &err));
    BOOST_CHECK_EQUAL(config.GetBlockDownloadLowerWindow(), 2 * DEFAULT_BLOCK_DOWNLOAD_LOWER_WINDOW);
    BOOST_CHECK(!config.SetBlockDownloadLowerWindow(0, &err));
    BOOST_CHECK(!config.SetBlockDownloadLowerWindow(-1, &err));
    BOOST_CHECK(config.SetBlockDownloadLowerWindow(config.GetBlockDownloadWindow(), &err));
    BOOST_CHECK_EQUAL(config.GetBlockDownloadLowerWindow(), config.GetBlockDownloadWindow());
    BOOST_CHECK(!config.SetBlockDownloadLowerWindow(config.GetBlockDownloadWindow() + 1, &err));
    BOOST_CHECK(config.SetBlockDownloadWindow(config.GetBlockDownloadLowerWindow() - 1, &err));
    BOOST_CHECK_EQUAL(config.GetBlockDownloadWindow(), config.GetBlockDownloadLowerWindow());

    BOOST_CHECK_EQUAL(config.GetBlockDownloadSlowFetchTimeout(), DEFAULT_BLOCK_DOWNLOAD_SLOW_FETCH_TIMEOUT);
    BOOST_CHECK(config.SetBlockDownloadSlowFetchTimeout(2 * DEFAULT_BLOCK_DOWNLOAD_SLOW_FETCH_TIMEOUT, &err));
    BOOST_CHECK_EQUAL(config.GetBlockDownloadSlowFetchTimeout(), 2 * DEFAULT_BLOCK_DOWNLOAD_SLOW_FETCH_TIMEOUT);
    BOOST_CHECK(!config.SetBlockDownloadSlowFetchTimeout(0, &err));
    BOOST_CHECK(!config.SetBlockDownloadSlowFetchTimeout(-1, &err));

    BOOST_CHECK_EQUAL(config.GetBlockDownloadMaxParallelFetch(), DEFAULT_MAX_BLOCK_PARALLEL_FETCH);
    BOOST_CHECK(config.SetBlockDownloadMaxParallelFetch(2 * DEFAULT_MAX_BLOCK_PARALLEL_FETCH, &err));
    BOOST_CHECK_EQUAL(config.GetBlockDownloadMaxParallelFetch(), 2 * DEFAULT_MAX_BLOCK_PARALLEL_FETCH);
    BOOST_CHECK(!config.SetBlockDownloadMaxParallelFetch(0, &err));
    BOOST_CHECK(!config.SetBlockDownloadMaxParallelFetch(-1, &err));

    BOOST_CHECK_EQUAL(config.GetBlockDownloadTimeoutBase(), DEFAULT_BLOCK_DOWNLOAD_TIMEOUT_BASE);
    BOOST_CHECK(config.SetBlockDownloadTimeoutBase(2 * DEFAULT_BLOCK_DOWNLOAD_TIMEOUT_BASE, &err));
    BOOST_CHECK_EQUAL(config.GetBlockDownloadTimeoutBase(), 2 * DEFAULT_BLOCK_DOWNLOAD_TIMEOUT_BASE);
    BOOST_CHECK(!config.SetBlockDownloadTimeoutBase(0, &err));
    BOOST_CHECK(!config.SetBlockDownloadTimeoutBase(-1, &err));

    BOOST_CHECK_EQUAL(config.GetBlockDownloadTimeoutBaseIBD(), DEFAULT_BLOCK_DOWNLOAD_TIMEOUT_BASE_IBD);
    BOOST_CHECK(config.SetBlockDownloadTimeoutBaseIBD(2 * DEFAULT_BLOCK_DOWNLOAD_TIMEOUT_BASE_IBD, &err));
    BOOST_CHECK_EQUAL(config.GetBlockDownloadTimeoutBaseIBD(), 2 * DEFAULT_BLOCK_DOWNLOAD_TIMEOUT_BASE_IBD);
    BOOST_CHECK(!config.SetBlockDownloadTimeoutBaseIBD(0, &err));
    BOOST_CHECK(!config.SetBlockDownloadTimeoutBaseIBD(-1, &err));

    BOOST_CHECK_EQUAL(config.GetBlockDownloadTimeoutPerPeer(), DEFAULT_BLOCK_DOWNLOAD_TIMEOUT_PER_PEER);
    BOOST_CHECK(config.SetBlockDownloadTimeoutPerPeer(2 * DEFAULT_BLOCK_DOWNLOAD_TIMEOUT_PER_PEER, &err));
    BOOST_CHECK_EQUAL(config.GetBlockDownloadTimeoutPerPeer(), 2 * DEFAULT_BLOCK_DOWNLOAD_TIMEOUT_PER_PEER);
    BOOST_CHECK(!config.SetBlockDownloadTimeoutPerPeer(0, &err));
    BOOST_CHECK(!config.SetBlockDownloadTimeoutPerPeer(-1, &err));
}

BOOST_AUTO_TEST_CASE(p2p_config)
{
    GlobalConfig config {};
    std::string err {};

    BOOST_CHECK_EQUAL(config.GetP2PHandshakeTimeout(), DEFAULT_P2P_HANDSHAKE_TIMEOUT_INTERVAL);
    BOOST_CHECK(config.SetP2PHandshakeTimeout(2 * DEFAULT_P2P_HANDSHAKE_TIMEOUT_INTERVAL, &err));
    BOOST_CHECK_EQUAL(config.GetP2PHandshakeTimeout(), 2 * DEFAULT_P2P_HANDSHAKE_TIMEOUT_INTERVAL);
    BOOST_CHECK(!config.SetP2PHandshakeTimeout(0, &err));
    BOOST_CHECK(!config.SetP2PHandshakeTimeout(-1, &err));

    BOOST_CHECK_EQUAL(config.GetStreamSendRateLimit(), Stream::DEFAULT_SEND_RATE_LIMIT);
    BOOST_CHECK(config.SetStreamSendRateLimit(1000, &err));
    BOOST_CHECK_EQUAL(config.GetStreamSendRateLimit(), 1000);
    BOOST_CHECK(config.SetStreamSendRateLimit(0, &err));
    BOOST_CHECK(config.SetStreamSendRateLimit(-1, &err));
    BOOST_CHECK_EQUAL(config.GetStreamSendRateLimit(), -1);

    BOOST_CHECK_EQUAL(config.GetBanScoreThreshold(), DEFAULT_BANSCORE_THRESHOLD);
    BOOST_CHECK(config.SetBanScoreThreshold(2 * DEFAULT_BANSCORE_THRESHOLD, &err));
    BOOST_CHECK_EQUAL(config.GetBanScoreThreshold(), 2 * DEFAULT_BANSCORE_THRESHOLD);
    BOOST_CHECK(!config.SetBanScoreThreshold(0, &err));
    BOOST_CHECK(!config.SetBanScoreThreshold(-1, &err));

    BOOST_CHECK_EQUAL(config.GetMultistreamsEnabled(), DEFAULT_STREAMS_ENABLED);
    BOOST_CHECK(config.SetMultistreamsEnabled(!DEFAULT_STREAMS_ENABLED));
    BOOST_CHECK_EQUAL(config.GetMultistreamsEnabled(), !DEFAULT_STREAMS_ENABLED);

    BOOST_CHECK_EQUAL(config.GetWhitelistRelay(), DEFAULT_WHITELISTRELAY);
    BOOST_CHECK(config.SetWhitelistRelay(!DEFAULT_WHITELISTRELAY));
    BOOST_CHECK_EQUAL(config.GetWhitelistRelay(), !DEFAULT_WHITELISTRELAY);

    BOOST_CHECK_EQUAL(config.GetWhitelistForceRelay(), DEFAULT_WHITELISTFORCERELAY);
    BOOST_CHECK(config.SetWhitelistForceRelay(!DEFAULT_WHITELISTFORCERELAY));
    BOOST_CHECK_EQUAL(config.GetWhitelistForceRelay(), !DEFAULT_WHITELISTFORCERELAY);

    BOOST_CHECK_EQUAL(config.GetRejectMempoolRequest(), DEFAULT_REJECTMEMPOOLREQUEST);
    BOOST_CHECK(config.SetRejectMempoolRequest(!DEFAULT_REJECTMEMPOOLREQUEST));
    BOOST_CHECK_EQUAL(config.GetRejectMempoolRequest(), !DEFAULT_REJECTMEMPOOLREQUEST);

    BOOST_CHECK(! config.DoDropMessageTest());
    BOOST_CHECK(config.SetDropMessageTest(1));
    BOOST_CHECK(config.DoDropMessageTest());
    BOOST_CHECK_EQUAL(config.GetDropMessageTest(), 1);
    BOOST_CHECK(config.SetDropMessageTest(0));
    BOOST_CHECK(config.DoDropMessageTest());
    BOOST_CHECK_EQUAL(config.GetDropMessageTest(), 0);
    BOOST_CHECK(! config.SetDropMessageTest(-1));
    BOOST_CHECK(! config.DoDropMessageTest());

    BOOST_CHECK_EQUAL(config.GetInvalidChecksumInterval(), DEFAULT_MIN_TIME_INTERVAL_CHECKSUM_MS);
    BOOST_CHECK(config.SetInvalidChecksumInterval(2 * DEFAULT_MIN_TIME_INTERVAL_CHECKSUM_MS, &err));
    BOOST_CHECK_EQUAL(config.GetInvalidChecksumInterval(), 2 * DEFAULT_MIN_TIME_INTERVAL_CHECKSUM_MS);
    BOOST_CHECK(config.SetInvalidChecksumInterval(0, &err));
    BOOST_CHECK(!config.SetInvalidChecksumInterval(-1, &err));

    BOOST_CHECK_EQUAL(config.GetInvalidChecksumFreq(), DEFAULT_INVALID_CHECKSUM_FREQUENCY);
    BOOST_CHECK(config.SetInvalidChecksumFreq(2 * DEFAULT_INVALID_CHECKSUM_FREQUENCY, &err));
    BOOST_CHECK_EQUAL(config.GetInvalidChecksumFreq(), 2 * DEFAULT_INVALID_CHECKSUM_FREQUENCY);
    BOOST_CHECK(config.SetInvalidChecksumFreq(0, &err));
    BOOST_CHECK(!config.SetInvalidChecksumFreq(-1, &err));

    BOOST_CHECK_EQUAL(config.GetFeeFilter(), DEFAULT_FEEFILTER);
    BOOST_CHECK(config.SetFeeFilter(!DEFAULT_FEEFILTER));
    BOOST_CHECK_EQUAL(config.GetFeeFilter(), !DEFAULT_FEEFILTER);

    BOOST_CHECK_EQUAL(config.GetMaxAddNodeConnections(), DEFAULT_MAX_ADDNODE_CONNECTIONS);
    BOOST_CHECK(config.SetMaxAddNodeConnections(2 * DEFAULT_MAX_ADDNODE_CONNECTIONS, &err));
    BOOST_CHECK_EQUAL(config.GetMaxAddNodeConnections(), 2 * DEFAULT_MAX_ADDNODE_CONNECTIONS);
    BOOST_CHECK(config.SetMaxAddNodeConnections(0, &err));
    BOOST_CHECK(! config.SetMaxAddNodeConnections(-1, &err));
}

BOOST_AUTO_TEST_CASE(safe_mode_config)
{
    GlobalConfig config {};
    std::string err {};

    // Initial webhook address and URL are empty
    BOOST_CHECK_EQUAL(config.GetSafeModeWebhookAddress(), "");
    BOOST_CHECK_EQUAL(config.GetSafeModeWebhookPort(), 80);
    BOOST_CHECK_EQUAL(config.GetSafeModeWebhookPath(), "");

    // Try setting some bad URLs
    BOOST_CHECK(!config.SetSafeModeWebhookURL("", &err));
    BOOST_CHECK(!config.SetSafeModeWebhookURL("://path", &err));
    BOOST_CHECK(!config.SetSafeModeWebhookURL("http://", &err));
    BOOST_CHECK(!config.SetSafeModeWebhookURL("https://hostname", &err));

    // Try some good URLS
    BOOST_CHECK(config.SetSafeModeWebhookURL("http://127.0.0.1", &err));
    BOOST_CHECK_EQUAL(config.GetSafeModeWebhookAddress(), "127.0.0.1");
    BOOST_CHECK_EQUAL(config.GetSafeModeWebhookPort(), 80);
    BOOST_CHECK_EQUAL(config.GetSafeModeWebhookPath(), "");

    BOOST_CHECK(config.SetSafeModeWebhookURL("http://[::1]", &err));
    BOOST_CHECK_EQUAL(config.GetSafeModeWebhookAddress(), "::1");
    BOOST_CHECK_EQUAL(config.GetSafeModeWebhookPort(), 80);
    BOOST_CHECK_EQUAL(config.GetSafeModeWebhookPath(), "");

    BOOST_CHECK(config.SetSafeModeWebhookURL("http://www.webhook.com", &err));
    BOOST_CHECK_EQUAL(config.GetSafeModeWebhookAddress(), "www.webhook.com");
    BOOST_CHECK_EQUAL(config.GetSafeModeWebhookPort(), 80);
    BOOST_CHECK_EQUAL(config.GetSafeModeWebhookPath(), "");

    BOOST_CHECK(config.SetSafeModeWebhookURL("http://www.webhook.com/", &err));
    BOOST_CHECK_EQUAL(config.GetSafeModeWebhookAddress(), "www.webhook.com");
    BOOST_CHECK_EQUAL(config.GetSafeModeWebhookPort(), 80);
    BOOST_CHECK_EQUAL(config.GetSafeModeWebhookPath(), "/");

    BOOST_CHECK(config.SetSafeModeWebhookURL("http://127.0.0.1:8080", &err));
    BOOST_CHECK_EQUAL(config.GetSafeModeWebhookAddress(), "127.0.0.1");
    BOOST_CHECK_EQUAL(config.GetSafeModeWebhookPort(), 8080);
    BOOST_CHECK_EQUAL(config.GetSafeModeWebhookPath(), "");

    BOOST_CHECK(config.SetSafeModeWebhookURL("http://[::1]:8080", &err));
    BOOST_CHECK_EQUAL(config.GetSafeModeWebhookAddress(), "::1");
    BOOST_CHECK_EQUAL(config.GetSafeModeWebhookPort(), 8080);
    BOOST_CHECK_EQUAL(config.GetSafeModeWebhookPath(), "");

    BOOST_CHECK(config.SetSafeModeWebhookURL("http://127.0.0.1/path", &err));
    BOOST_CHECK_EQUAL(config.GetSafeModeWebhookAddress(), "127.0.0.1");
    BOOST_CHECK_EQUAL(config.GetSafeModeWebhookPort(), 80);
    BOOST_CHECK_EQUAL(config.GetSafeModeWebhookPath(), "/path");

    BOOST_CHECK(config.SetSafeModeWebhookURL("http://127.0.0.1:8080/path", &err));
    BOOST_CHECK_EQUAL(config.GetSafeModeWebhookAddress(), "127.0.0.1");
    BOOST_CHECK_EQUAL(config.GetSafeModeWebhookPort(), 8080);
    BOOST_CHECK_EQUAL(config.GetSafeModeWebhookPath(), "/path");

    BOOST_CHECK(config.SetSafeModeWebhookURL("http://127.0.0.1:8080/path?arg=val", &err));
    BOOST_CHECK_EQUAL(config.GetSafeModeWebhookAddress(), "127.0.0.1");
    BOOST_CHECK_EQUAL(config.GetSafeModeWebhookPort(), 8080);
    BOOST_CHECK_EQUAL(config.GetSafeModeWebhookPath(), "/path?arg=val");

    BOOST_CHECK_EQUAL(config.GetSafeModeMinForkHeightDifference(), SAFE_MODE_DEFAULT_MIN_POW_DIFFERENCE);
    BOOST_CHECK(config.SetSafeModeMinForkHeightDifference(2 * SAFE_MODE_DEFAULT_MIN_POW_DIFFERENCE, &err));
    BOOST_CHECK_EQUAL(config.GetSafeModeMinForkHeightDifference(), 2 * SAFE_MODE_DEFAULT_MIN_POW_DIFFERENCE);

    BOOST_CHECK_EQUAL(config.GetSafeModeMaxForkDistance(), SAFE_MODE_DEFAULT_MAX_FORK_DISTANCE);
    BOOST_CHECK(config.SetSafeModeMaxForkDistance(2 * SAFE_MODE_DEFAULT_MAX_FORK_DISTANCE, &err));
    BOOST_CHECK_EQUAL(config.GetSafeModeMaxForkDistance(), 2 * SAFE_MODE_DEFAULT_MAX_FORK_DISTANCE);
    BOOST_CHECK(!config.SetSafeModeMaxForkDistance(0, &err));
    
    BOOST_CHECK_EQUAL(config.GetSafeModeMinForkLength(), SAFE_MODE_DEFAULT_MIN_FORK_LENGTH);
    BOOST_CHECK(config.SetSafeModeMinForkLength(2 * SAFE_MODE_DEFAULT_MIN_FORK_LENGTH, &err));
    BOOST_CHECK_EQUAL(config.GetSafeModeMinForkLength(), 2 * SAFE_MODE_DEFAULT_MIN_FORK_LENGTH);
    BOOST_CHECK(!config.SetSafeModeMinForkLength(0, &err));
  
}

BOOST_AUTO_TEST_CASE(rpc_config)
{
    GlobalConfig config {};
    std::string err {};

    BOOST_CHECK_EQUAL(config.GetWebhookClientNumThreads(), rpc::client::WebhookClientDefaults::DEFAULT_NUM_THREADS);
    BOOST_CHECK(config.SetWebhookClientNumThreads(2 * rpc::client::WebhookClientDefaults::DEFAULT_NUM_THREADS, &err));
    BOOST_CHECK_EQUAL(config.GetWebhookClientNumThreads(), 2 * rpc::client::WebhookClientDefaults::DEFAULT_NUM_THREADS);
    BOOST_CHECK(!config.SetWebhookClientNumThreads(0, &err));
    BOOST_CHECK(!config.SetWebhookClientNumThreads(-1, &err));
}

BOOST_AUTO_TEST_CASE(dsattempt_config)
{
    GlobalConfig config {};
    std::string err {};

    BOOST_CHECK_EQUAL(config.GetDoubleSpendNotificationLevel(), DSAttemptHandler::DEFAULT_NOTIFY_LEVEL);
    BOOST_CHECK(config.SetDoubleSpendNotificationLevel(static_cast<int>(DSAttemptHandler::NotificationLevel::NONE), &err));
    BOOST_CHECK_EQUAL(config.GetDoubleSpendNotificationLevel(), DSAttemptHandler::NotificationLevel::NONE);
    BOOST_CHECK(!config.SetDoubleSpendNotificationLevel(-1, &err));
    BOOST_CHECK(!config.SetDoubleSpendNotificationLevel(3, &err));

    BOOST_CHECK_EQUAL(config.GetDoubleSpendEndpointFastTimeout(), rpc::client::RPCClientConfig::DEFAULT_DS_ENDPOINT_FAST_TIMEOUT);
    BOOST_CHECK(config.SetDoubleSpendEndpointFastTimeout(2 * rpc::client::RPCClientConfig::DEFAULT_DS_ENDPOINT_FAST_TIMEOUT, &err));
    BOOST_CHECK_EQUAL(config.GetDoubleSpendEndpointFastTimeout(), 2 * rpc::client::RPCClientConfig::DEFAULT_DS_ENDPOINT_FAST_TIMEOUT);
    BOOST_CHECK(!config.SetDoubleSpendEndpointFastTimeout(0, &err));
    BOOST_CHECK(!config.SetDoubleSpendEndpointFastTimeout(-1, &err));

    BOOST_CHECK_EQUAL(config.GetDoubleSpendEndpointSlowTimeout(), rpc::client::RPCClientConfig::DEFAULT_DS_ENDPOINT_SLOW_TIMEOUT);
    BOOST_CHECK(config.SetDoubleSpendEndpointSlowTimeout(2 * rpc::client::RPCClientConfig::DEFAULT_DS_ENDPOINT_SLOW_TIMEOUT, &err));
    BOOST_CHECK_EQUAL(config.GetDoubleSpendEndpointSlowTimeout(), 2 * rpc::client::RPCClientConfig::DEFAULT_DS_ENDPOINT_SLOW_TIMEOUT);
    BOOST_CHECK(!config.SetDoubleSpendEndpointSlowTimeout(0, &err));
    BOOST_CHECK(!config.SetDoubleSpendEndpointSlowTimeout(-1, &err));

    BOOST_CHECK_EQUAL(config.GetDoubleSpendEndpointSlowRatePerHour(), DSAttemptHandler::DEFAULT_DS_ENDPOINT_SLOW_RATE_PER_HOUR);
    BOOST_CHECK(config.SetDoubleSpendEndpointSlowRatePerHour(2 * DSAttemptHandler::DEFAULT_DS_ENDPOINT_SLOW_RATE_PER_HOUR, &err));
    BOOST_CHECK_EQUAL(config.GetDoubleSpendEndpointSlowRatePerHour(), 2 * DSAttemptHandler::DEFAULT_DS_ENDPOINT_SLOW_RATE_PER_HOUR);
    BOOST_CHECK(!config.SetDoubleSpendEndpointSlowRatePerHour(0, &err));
    BOOST_CHECK(!config.SetDoubleSpendEndpointSlowRatePerHour(-1, &err));

    BOOST_CHECK_EQUAL(config.GetDoubleSpendEndpointPort(), rpc::client::RPCClientConfig::DEFAULT_DS_ENDPOINT_PORT);
    BOOST_CHECK(config.SetDoubleSpendEndpointPort(rpc::client::RPCClientConfig::DEFAULT_DS_ENDPOINT_PORT + 1, &err));
    BOOST_CHECK_EQUAL(config.GetDoubleSpendEndpointPort(), rpc::client::RPCClientConfig::DEFAULT_DS_ENDPOINT_PORT + 1);
    BOOST_CHECK(!config.SetDoubleSpendEndpointPort(0, &err));
    BOOST_CHECK(!config.SetDoubleSpendEndpointPort(65536, &err));

    BOOST_CHECK_EQUAL(config.GetDoubleSpendEndpointBlacklistSize(), DSAttemptHandler::DEFAULT_DS_ENDPOINT_BLACKLIST_SIZE);
    BOOST_CHECK(config.SetDoubleSpendEndpointBlacklistSize(2 * DSAttemptHandler::DEFAULT_DS_ENDPOINT_BLACKLIST_SIZE, &err));
    BOOST_CHECK_EQUAL(config.GetDoubleSpendEndpointBlacklistSize(), 2 * DSAttemptHandler::DEFAULT_DS_ENDPOINT_BLACKLIST_SIZE);
    BOOST_CHECK(!config.SetDoubleSpendEndpointBlacklistSize(-1, &err));

    BOOST_CHECK_EQUAL(config.GetDoubleSpendEndpointSkipList().size(), 0U);
    BOOST_CHECK(config.SetDoubleSpendEndpointSkipList("127.0.0.1", &err));
    BOOST_CHECK_EQUAL(config.GetDoubleSpendEndpointSkipList().size(), 1U);
    BOOST_CHECK(config.SetDoubleSpendEndpointSkipList("127.0.0.1,::1", &err));
    BOOST_CHECK_EQUAL(config.GetDoubleSpendEndpointSkipList().size(), 2U);
    BOOST_CHECK(config.SetDoubleSpendEndpointSkipList("127.0.0.1 , ::1", &err));
    BOOST_REQUIRE_EQUAL(config.GetDoubleSpendEndpointSkipList().size(), 2U);
    BOOST_CHECK_EQUAL(config.GetDoubleSpendEndpointSkipList().count("127.0.0.1"), 1U);
    BOOST_CHECK_EQUAL(config.GetDoubleSpendEndpointSkipList().count("::1"), 1U);

    BOOST_CHECK_EQUAL(config.GetDoubleSpendEndpointMaxCount(), DSAttemptHandler::DEFAULT_DS_ENDPOINT_MAX_COUNT);
    BOOST_CHECK(config.SetDoubleSpendEndpointMaxCount(2 * DSAttemptHandler::DEFAULT_DS_ENDPOINT_MAX_COUNT, &err));
    BOOST_CHECK_EQUAL(config.GetDoubleSpendEndpointMaxCount(), 2 * DSAttemptHandler::DEFAULT_DS_ENDPOINT_MAX_COUNT);
    BOOST_CHECK(!config.SetDoubleSpendEndpointMaxCount(0, &err));
    BOOST_CHECK(!config.SetDoubleSpendEndpointMaxCount(-1, &err));

    BOOST_CHECK_EQUAL(config.GetDoubleSpendTxnRemember(), DSAttemptHandler::DEFAULT_TXN_REMEMBER_COUNT);
    BOOST_CHECK(config.SetDoubleSpendTxnRemember(2 * DSAttemptHandler::DEFAULT_TXN_REMEMBER_COUNT, &err));
    BOOST_CHECK_EQUAL(config.GetDoubleSpendTxnRemember(), 2 * DSAttemptHandler::DEFAULT_TXN_REMEMBER_COUNT);
    BOOST_CHECK(!config.SetDoubleSpendTxnRemember(-1, &err));

    BOOST_CHECK_EQUAL(config.GetDoubleSpendNumFastThreads(), DSAttemptHandler::DEFAULT_NUM_FAST_THREADS);
    BOOST_CHECK(config.SetDoubleSpendNumFastThreads(2 * DSAttemptHandler::DEFAULT_NUM_FAST_THREADS, &err));
    BOOST_CHECK_EQUAL(config.GetDoubleSpendNumFastThreads(), 2 * DSAttemptHandler::DEFAULT_NUM_FAST_THREADS);
    BOOST_CHECK(!config.SetDoubleSpendNumFastThreads(0, &err));
    BOOST_CHECK(!config.SetDoubleSpendNumFastThreads(-1, &err));

    BOOST_CHECK_EQUAL(config.GetDoubleSpendNumSlowThreads(), DSAttemptHandler::DEFAULT_NUM_SLOW_THREADS);
    BOOST_CHECK(config.SetDoubleSpendNumSlowThreads(2 * DSAttemptHandler::DEFAULT_NUM_SLOW_THREADS, &err));
    BOOST_CHECK_EQUAL(config.GetDoubleSpendNumSlowThreads(), 2 * DSAttemptHandler::DEFAULT_NUM_SLOW_THREADS);
    BOOST_CHECK(!config.SetDoubleSpendNumSlowThreads(0, &err));
    BOOST_CHECK(!config.SetDoubleSpendNumSlowThreads(-1, &err));

    BOOST_CHECK_EQUAL(config.GetDoubleSpendQueueMaxMemory(), DSAttemptHandler::DEFAULT_MAX_SUBMIT_MEMORY);
    BOOST_CHECK(config.SetDoubleSpendQueueMaxMemory(2 * DSAttemptHandler::DEFAULT_MAX_SUBMIT_MEMORY, &err));
    BOOST_CHECK_EQUAL(config.GetDoubleSpendQueueMaxMemory(), 2 * DSAttemptHandler::DEFAULT_MAX_SUBMIT_MEMORY);
    BOOST_CHECK(!config.SetDoubleSpendQueueMaxMemory(0, &err));
    BOOST_CHECK(!config.SetDoubleSpendQueueMaxMemory(-1, &err));

    BOOST_CHECK(config.SetDoubleSpendDetectedWebhookURL("http://127.0.0.1:8080/path", &err));
    BOOST_CHECK_EQUAL(config.GetDoubleSpendDetectedWebhookAddress(), "127.0.0.1");
    BOOST_CHECK_EQUAL(config.GetDoubleSpendDetectedWebhookPort(), 8080);
    BOOST_CHECK_EQUAL(config.GetDoubleSpendDetectedWebhookPath(), "/path");

    BOOST_CHECK_EQUAL(config.GetDoubleSpendDetectedWebhookMaxTxnSize(), DSDetectedDefaults::DEFAULT_MAX_WEBHOOK_TXN_SIZE * ONE_MEBIBYTE);
    BOOST_CHECK(config.SetDoubleSpendDetectedWebhookMaxTxnSize(2 * DSDetectedDefaults::DEFAULT_MAX_WEBHOOK_TXN_SIZE * ONE_MEBIBYTE, &err));
    BOOST_CHECK_EQUAL(config.GetDoubleSpendDetectedWebhookMaxTxnSize(), 2 * DSDetectedDefaults::DEFAULT_MAX_WEBHOOK_TXN_SIZE * ONE_MEBIBYTE);
    BOOST_CHECK(!config.SetDoubleSpendDetectedWebhookMaxTxnSize(0, &err));
    BOOST_CHECK(!config.SetDoubleSpendDetectedWebhookMaxTxnSize(-1, &err));
}

BOOST_AUTO_TEST_CASE(minerid_config)
{
    GlobalConfig config {};
    std::string err {};

    BOOST_CHECK_EQUAL(config.GetMinerIdEnabled(), MinerIdDatabaseDefaults::DEFAULT_MINER_ID_ENABLED);
    BOOST_CHECK(config.SetMinerIdEnabled(false, &err));
    BOOST_CHECK_EQUAL(config.GetMinerIdEnabled(), false);
    BOOST_CHECK(config.SetMinerIdEnabled(true, &err));
    BOOST_CHECK_EQUAL(config.GetMinerIdEnabled(), true);

    BOOST_CHECK_EQUAL(config.GetMinerIdCacheSize(), MinerIdDatabaseDefaults::DEFAULT_CACHE_SIZE);
    BOOST_CHECK(config.SetMinerIdCacheSize(2 * MinerIdDatabaseDefaults::DEFAULT_CACHE_SIZE, &err));
    BOOST_CHECK_EQUAL(config.GetMinerIdCacheSize(), 2 * MinerIdDatabaseDefaults::DEFAULT_CACHE_SIZE);
    BOOST_CHECK(config.SetMinerIdCacheSize(0, &err));
    BOOST_CHECK(config.SetMinerIdCacheSize(MinerIdDatabaseDefaults::MAX_CACHE_SIZE, &err));
    BOOST_CHECK(!config.SetMinerIdCacheSize(-1, &err));
    BOOST_CHECK(!config.SetMinerIdCacheSize(MinerIdDatabaseDefaults::MAX_CACHE_SIZE + 1, &err));

    BOOST_CHECK_EQUAL(config.GetMinerIdsNumToKeep(), MinerIdDatabaseDefaults::DEFAULT_MINER_IDS_TO_KEEP);
    BOOST_CHECK(config.SetMinerIdsNumToKeep(2 * MinerIdDatabaseDefaults::DEFAULT_MINER_IDS_TO_KEEP, &err));
    BOOST_CHECK_EQUAL(config.GetMinerIdsNumToKeep(), 2 * MinerIdDatabaseDefaults::DEFAULT_MINER_IDS_TO_KEEP);
    BOOST_CHECK(!config.SetMinerIdsNumToKeep(1, &err));
    BOOST_CHECK(!config.SetMinerIdsNumToKeep(-1, &err));

    BOOST_CHECK_EQUAL(config.GetMinerIdReputationM(), MinerIdDatabaseDefaults::DEFAULT_MINER_REPUTATION_M);
    BOOST_CHECK(config.SetMinerIdReputationM(2 * MinerIdDatabaseDefaults::DEFAULT_MINER_REPUTATION_M, &err));
    BOOST_CHECK_EQUAL(config.GetMinerIdReputationM(), 2 * MinerIdDatabaseDefaults::DEFAULT_MINER_REPUTATION_M);
    BOOST_CHECK(config.SetMinerIdReputationM(config.GetMinerIdReputationN(), &err));
    BOOST_CHECK(!config.SetMinerIdReputationM(config.GetMinerIdReputationN() + 1, &err));
    BOOST_CHECK(config.SetMinerIdReputationM(1, &err));
    BOOST_CHECK(!config.SetMinerIdReputationM(-1, &err));

    BOOST_CHECK_EQUAL(config.GetMinerIdReputationN(), MinerIdDatabaseDefaults::DEFAULT_MINER_REPUTATION_N);
    BOOST_CHECK(config.SetMinerIdReputationN(2 * MinerIdDatabaseDefaults::DEFAULT_MINER_REPUTATION_N, &err));
    BOOST_CHECK_EQUAL(config.GetMinerIdReputationN(), 2 * MinerIdDatabaseDefaults::DEFAULT_MINER_REPUTATION_N);
    BOOST_CHECK(config.SetMinerIdReputationN(1, &err));
    BOOST_CHECK(!config.SetMinerIdReputationN(-1, &err));
    config.SetMinerIdReputationM(MinerIdDatabaseDefaults::DEFAULT_MINER_REPUTATION_M, &err);
    BOOST_CHECK(config.SetMinerIdReputationN(config.GetMinerIdReputationM(), &err));
    BOOST_CHECK(!config.SetMinerIdReputationN(config.GetMinerIdReputationM() - 1, &err));

    BOOST_CHECK_EQUAL(config.GetMinerIdReputationMScale(), MinerIdDatabaseDefaults::DEFAULT_M_SCALE_FACTOR);
    BOOST_CHECK(config.SetMinerIdReputationMScale(2 * MinerIdDatabaseDefaults::DEFAULT_M_SCALE_FACTOR, &err));
    BOOST_CHECK_EQUAL(config.GetMinerIdReputationMScale(), 2 * MinerIdDatabaseDefaults::DEFAULT_M_SCALE_FACTOR);
    BOOST_CHECK(config.SetMinerIdReputationMScale(1, &err));
    BOOST_CHECK(!config.SetMinerIdReputationMScale(-1, &err));

    BOOST_CHECK(config.SetMinerIdGeneratorURL("http://127.0.0.1:8080", &err));
    BOOST_CHECK_EQUAL(config.GetMinerIdGeneratorAddress(), "127.0.0.1");
    BOOST_CHECK_EQUAL(config.GetMinerIdGeneratorPort(), 8080);
    BOOST_CHECK_EQUAL(config.GetMinerIdGeneratorPath(), "");

    BOOST_CHECK(config.SetMinerIdGeneratorAlias("OurAlias", &err));
    BOOST_CHECK_EQUAL(config.GetMinerIdGeneratorAlias(), "OurAlias");
}

BOOST_AUTO_TEST_CASE(disable_BIP30)
{
    GlobalConfig config {};
    std::string err {};

    SelectParams(CBaseChainParams::MAIN);
    BOOST_CHECK(config.SetDisableBIP30Checks(true, &err) == false);
    BOOST_CHECK(err == "Can not change disabling of BIP30 checks on " + config.GetChainParams().NetworkIDString() + " network.");
    BOOST_CHECK(config.GetDisableBIP30Checks() == false);

    for(const auto& networkType: {CBaseChainParams::TESTNET, CBaseChainParams::REGTEST, CBaseChainParams::STN})
    {
        config.Reset();
        SelectParams(networkType);
        BOOST_CHECK(config.GetDisableBIP30Checks() == false);
        BOOST_CHECK(config.SetDisableBIP30Checks(true, &err) == true);
        BOOST_CHECK(config.GetDisableBIP30Checks() == true);
        BOOST_CHECK(config.SetDisableBIP30Checks(false, &err) == true);
        BOOST_CHECK(config.GetDisableBIP30Checks() == false);
    }
}

BOOST_AUTO_TEST_CASE(dust_config_test)
{
    GlobalConfig config {};
    std::string err {};

    BOOST_CHECK(config.SetDustLimitFactor(0, &err));
    BOOST_CHECK_EQUAL(config.GetDustLimitFactor(), 0);
    BOOST_CHECK(config.SetDustLimitFactor(100, &err));
    BOOST_CHECK_EQUAL(config.GetDustLimitFactor(), 100);
    BOOST_CHECK(config.SetDustLimitFactor(200, &err));
    BOOST_CHECK_EQUAL(config.GetDustLimitFactor(), 200);
    BOOST_CHECK(config.SetDustLimitFactor(300, &err));
    BOOST_CHECK_EQUAL(config.GetDustLimitFactor(), 300);

    BOOST_CHECK(!config.SetDustLimitFactor(-1, &err));
    BOOST_CHECK(!config.SetDustLimitFactor(301, &err));
}

BOOST_AUTO_TEST_CASE(banned_clientua_test)
{
    GlobalConfig config {};

    // default patterns are DEFAULT_CLIENTUA_BAN_PATTERNS {"abc","cash","bch"};
    // but we will set allow patterns to "not-abc-client"

    BOOST_CHECK(config.IsClientUABanned("not-abc-client"));
    BOOST_CHECK(config.IsClientUABanned("is-abc-client"));
    BOOST_CHECK(!config.IsClientUABanned("is-bsv-client"));

    config.SetAllowClientUA(std::set<std::string>{"not-abc-client"});
    BOOST_CHECK(!config.IsClientUABanned("not-abc-client"));
    BOOST_CHECK(config.IsClientUABanned("is-abc-client"));
    BOOST_CHECK(!config.IsClientUABanned("is-bsv-client"));

    BOOST_CHECK(!config.IsClientUABanned("not-ABC-client"));
    BOOST_CHECK(config.IsClientUABanned("is-ABC-client"));
    BOOST_CHECK(!config.IsClientUABanned("is-BSV-client"));

    config.SetBanClientUA(std::set<std::string>{"abc","cash","bch","BSV"});
    BOOST_CHECK(!config.IsClientUABanned("not-abc-client"));
    BOOST_CHECK(config.IsClientUABanned("is-abc-client"));
    BOOST_CHECK(config.IsClientUABanned("is-bsv-client"));

    // check that default BitcoinSV client is not banned
    BOOST_CHECK(!config.IsClientUABanned(userAgent()));
}

BOOST_AUTO_TEST_CASE(prune_config_test)
{
    GlobalConfig config {};
    std::string err {};

    BOOST_CHECK_EQUAL(config.GetMinBlocksToKeep(), DEFAULT_MIN_BLOCKS_TO_KEEP);
    BOOST_CHECK(config.SetMinBlocksToKeep(MIN_MIN_BLOCKS_TO_KEEP));
    BOOST_CHECK_EQUAL(config.GetMinBlocksToKeep(), MIN_MIN_BLOCKS_TO_KEEP);
    BOOST_CHECK(! config.SetMinBlocksToKeep(MIN_MIN_BLOCKS_TO_KEEP - 1));
}

BOOST_AUTO_TEST_CASE(tx_validation)
{
    GlobalConfig config {};
    std::string err {};

    BOOST_CHECK_EQUAL(config.GetBlockValidationTxBatchSize(), DEFAULT_BLOCK_VALIDATION_TX_BATCH_SIZE);
    BOOST_CHECK(config.SetBlockValidationTxBatchSize(DEFAULT_BLOCK_VALIDATION_TX_BATCH_SIZE*2, &err));
    BOOST_CHECK_EQUAL(config.GetBlockValidationTxBatchSize(), DEFAULT_BLOCK_VALIDATION_TX_BATCH_SIZE*2);
    BOOST_CHECK(! config.SetBlockValidationTxBatchSize(0, &err));
    BOOST_CHECK(! config.SetBlockValidationTxBatchSize(-1, &err));

    BOOST_CHECK_EQUAL(config.GetPerBlockScriptValidatorThreadsCount(), DEFAULT_SCRIPTCHECK_THREADS);
    BOOST_CHECK_EQUAL(config.GetPerBlockTxnValidatorThreadsCount(), DEFAULT_TXNCHECK_THREADS);
    BOOST_CHECK(config.SetBlockScriptValidatorsParams(1, 2, 0, 1, &err));
    BOOST_CHECK_EQUAL(config.GetPerBlockScriptValidatorThreadsCount(), 2);
    BOOST_CHECK_EQUAL(config.GetPerBlockTxnValidatorThreadsCount(), GetNumCores());
    BOOST_CHECK(config.SetBlockScriptValidatorsParams(1, 0, 2, 1, &err));
    BOOST_CHECK_EQUAL(config.GetPerBlockScriptValidatorThreadsCount(), std::clamp(GetNumCores(), 0, 8));
    BOOST_CHECK_EQUAL(config.GetPerBlockTxnValidatorThreadsCount(), 2);
    BOOST_CHECK(! config.SetBlockScriptValidatorsParams(1, -1, -1, 1, &err));
}

BOOST_AUTO_TEST_SUITE_END()
