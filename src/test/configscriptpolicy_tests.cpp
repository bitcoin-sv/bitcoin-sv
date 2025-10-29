// Copyright (c) 2016 The Bitcoin Core developers
// Copyright (c) 2019 Bitcoin Association
// Copyright (c) 2025 BSV Blockchain Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include <boost/test/unit_test.hpp>

#include "configscriptpolicy.h"
#include "protocol_era.h"
#include "test/test_bitcoin.h"

BOOST_FIXTURE_TEST_SUITE(configscriptpolicy_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(validation_helpers)
{
    std::string err;

    err.clear();
    BOOST_CHECK(LessThan(-5, &err, "too small", 0));
    BOOST_CHECK_EQUAL(err, "too small");

    err = "unchanged";
    BOOST_CHECK(!LessThan(0, &err, "ignored", 0));
    BOOST_CHECK_EQUAL(err, "unchanged");

    err.clear();
    BOOST_CHECK(LessThanZero(-1, &err, "negative"));
    BOOST_CHECK_EQUAL(err, "negative");

    err = "ok";
    BOOST_CHECK(!LessThanZero(0, &err, "ignored"));
    BOOST_CHECK_EQUAL(err, "ok");
}

BOOST_AUTO_TEST_CASE(max_ops_per_script_policy_behavior)
{
    ConfigScriptPolicy policy;
    std::string err;

    BOOST_CHECK(policy.SetMaxOpsPerScriptPolicy(static_cast<int64_t>(MAX_OPS_PER_SCRIPT_AFTER_GENESIS), &err));
    BOOST_CHECK(err.empty());
    BOOST_CHECK_EQUAL(policy.GetMaxOpsPerScript(true, false), MAX_OPS_PER_SCRIPT_AFTER_GENESIS);

    BOOST_CHECK(policy.SetMaxOpsPerScriptPolicy(400, &err));
    BOOST_CHECK_EQUAL(policy.GetMaxOpsPerScript(true, false), 400);
}

BOOST_AUTO_TEST_CASE(max_script_num_length_behavior)
{
    ConfigScriptPolicy policy;
    std::string err;

    BOOST_CHECK(!policy.SetMaxScriptNumLengthPolicy(MAX_SCRIPT_NUM_LENGTH_BEFORE_GENESIS - 1, &err));
    BOOST_CHECK_NE(err.find("must not be less than"), std::string::npos);

    BOOST_CHECK(policy.SetMaxScriptNumLengthPolicy(0, &err));

    BOOST_CHECK_EQUAL(policy.GetMaxScriptNumLength(ProtocolEra::PreGenesis, false), MAX_SCRIPT_NUM_LENGTH_BEFORE_GENESIS);
    BOOST_CHECK_EQUAL(policy.GetMaxScriptNumLength(ProtocolEra::PreGenesis, true), MAX_SCRIPT_NUM_LENGTH_BEFORE_GENESIS);

    BOOST_CHECK_EQUAL(policy.GetMaxScriptNumLength(ProtocolEra::PostGenesis, true), MAX_SCRIPT_NUM_LENGTH_AFTER_GENESIS);
    BOOST_CHECK_EQUAL(policy.GetMaxScriptNumLength(ProtocolEra::PostChronicle, true), MAX_SCRIPT_NUM_LENGTH_AFTER_CHRONICLE);

    BOOST_CHECK_EQUAL(policy.GetMaxScriptNumLength(ProtocolEra::PostChronicle, false), MAX_SCRIPT_NUM_LENGTH_AFTER_CHRONICLE);

    policy.SetMaxScriptNumLengthPolicy(300, &err);
    BOOST_CHECK_EQUAL(policy.GetMaxScriptNumLength(ProtocolEra::PostGenesis, false), 300);
}

BOOST_AUTO_TEST_CASE(max_script_size_policy_behavior)
{
    ConfigScriptPolicy policy;
    std::string err;

    const uint64_t testValue = MAX_SCRIPT_SIZE_AFTER_GENESIS / 2;
    BOOST_CHECK(policy.SetMaxScriptSizePolicy(0, &err));
    BOOST_CHECK_EQUAL(policy.GetMaxScriptSize(true, false), MAX_SCRIPT_SIZE_AFTER_GENESIS);

    BOOST_CHECK(policy.SetMaxScriptSizePolicy(static_cast<int64_t>(testValue), &err));
    BOOST_CHECK_EQUAL(policy.GetMaxScriptSize(true, false), testValue);

    BOOST_CHECK(policy.GetMaxScriptSize(false, true) == MAX_SCRIPT_SIZE_BEFORE_GENESIS);
}

BOOST_AUTO_TEST_CASE(pubkeys_per_multisig_policy_behavior)
{
    ConfigScriptPolicy policy;
    std::string err;

    BOOST_CHECK(policy.SetMaxPubKeysPerMultiSigPolicy(0, &err));
    BOOST_CHECK_EQUAL(policy.GetMaxPubKeysPerMultiSig(true, false), MAX_PUBKEYS_PER_MULTISIG_AFTER_GENESIS);

    BOOST_CHECK(policy.SetMaxPubKeysPerMultiSigPolicy(15, &err));
    BOOST_CHECK_EQUAL(policy.GetMaxPubKeysPerMultiSig(true, false), 15);

    BOOST_CHECK(policy.GetMaxPubKeysPerMultiSig(false, false) == MAX_PUBKEYS_PER_MULTISIG_BEFORE_GENESIS);
}

BOOST_AUTO_TEST_CASE(stack_memory_usage_behavior)
{
    ConfigScriptPolicy policy;
    std::string err;

    BOOST_CHECK(policy.SetMaxStackMemoryUsage(0, 0, &err));
    BOOST_CHECK_EQUAL(policy.GetMaxStackMemoryUsage(true, true), DEFAULT_STACK_MEMORY_USAGE_CONSENSUS_AFTER_GENESIS);
    BOOST_CHECK_EQUAL(policy.GetMaxStackMemoryUsage(true, false), DEFAULT_STACK_MEMORY_USAGE_CONSENSUS_AFTER_GENESIS);

    BOOST_CHECK(!policy.SetMaxStackMemoryUsage(200, 300, &err));
    BOOST_CHECK_NE(err.find("must not exceed consensus limit"), std::string::npos);

    BOOST_CHECK(policy.SetMaxStackMemoryUsage(600, 400, &err));
    BOOST_CHECK_EQUAL(policy.GetMaxStackMemoryUsage(true, true), 600);
    BOOST_CHECK_EQUAL(policy.GetMaxStackMemoryUsage(true, false), 400);

    BOOST_CHECK_EQUAL(policy.GetMaxStackMemoryUsage(false, false), INT64_MAX);
    BOOST_CHECK_EQUAL(policy.GetMaxStackMemoryUsage(false, true), INT64_MAX);
}

BOOST_AUTO_TEST_CASE(protocol_activation_and_graceful_periods)
{
    ConfigScriptPolicy policy;
    std::string err;

    BOOST_CHECK(!policy.SetGenesisActivationHeight(0, &err));
    BOOST_CHECK_NE(err.find("cannot be configured with a zero"), std::string::npos);
    BOOST_CHECK(policy.SetGenesisActivationHeight(123, &err));
    BOOST_CHECK_EQUAL(policy.GetGenesisActivationHeight(), 123);

    BOOST_CHECK(!policy.SetChronicleActivationHeight(0, &err));
    BOOST_CHECK_NE(err.find("cannot be configured with a zero"), std::string::npos);
    BOOST_CHECK(policy.SetChronicleActivationHeight(456, &err));
    BOOST_CHECK_EQUAL(policy.GetChronicleActivationHeight(), 456);

    BOOST_CHECK(!policy.SetGenesisGracefulPeriod(MAX_SCRIPT_NUM_LENGTH_AFTER_GENESIS, &err));
    BOOST_CHECK(policy.SetGenesisGracefulPeriod(10, &err));
    BOOST_CHECK_EQUAL(policy.GetGenesisGracefulPeriod(), 10);

    BOOST_CHECK(!policy.SetChronicleGracefulPeriod(MAX_SCRIPT_NUM_LENGTH_AFTER_GENESIS, &err));
    BOOST_CHECK(policy.SetChronicleGracefulPeriod(12, &err));
    BOOST_CHECK_EQUAL(policy.GetChronicleGracefulPeriod(), 12);
}

BOOST_AUTO_TEST_CASE(reset_default_behavior)
{
    ConfigScriptPolicy policy;
    std::string err;

    policy.SetMaxOpsPerScriptPolicy(42, &err);
    policy.SetMaxScriptSizePolicy(1000, &err);
    policy.SetMaxScriptNumLengthPolicy(300, &err);
    policy.SetMaxPubKeysPerMultiSigPolicy(7, &err);
    policy.SetMaxStackMemoryUsage(200, 150, &err);
    policy.SetGenesisActivationHeight(1234, &err);
    policy.SetChronicleActivationHeight(4321, &err);
    policy.SetGenesisGracefulPeriod(5, &err);
    policy.SetChronicleGracefulPeriod(6, &err);

    policy.ResetDefault();

    BOOST_CHECK_EQUAL(policy.GetMaxOpsPerScript(true, false), DEFAULT_OPS_PER_SCRIPT_POLICY_AFTER_GENESIS);
    BOOST_CHECK_EQUAL(policy.GetMaxScriptNumLength(ProtocolEra::PostGenesis, false), MAX_SCRIPT_NUM_LENGTH_AFTER_GENESIS);
    BOOST_CHECK_EQUAL(policy.GetMaxScriptSize(true, false), DEFAULT_MAX_SCRIPT_SIZE_POLICY_AFTER_GENESIS);
    BOOST_CHECK_EQUAL(policy.GetMaxPubKeysPerMultiSig(true, false), DEFAULT_PUBKEYS_PER_MULTISIG_POLICY_AFTER_GENESIS);
    BOOST_CHECK_EQUAL(policy.GetMaxStackMemoryUsage(true, false), DEFAULT_STACK_MEMORY_USAGE_POLICY_AFTER_GENESIS);
    BOOST_CHECK_EQUAL(policy.GetMaxStackMemoryUsage(true, true), DEFAULT_STACK_MEMORY_USAGE_CONSENSUS_AFTER_GENESIS);
    BOOST_CHECK_EQUAL(policy.GetGenesisActivationHeight(), 0);
    BOOST_CHECK_EQUAL(policy.GetChronicleActivationHeight(), 0);
    BOOST_CHECK_EQUAL(policy.GetGenesisGracefulPeriod(), DEFAULT_GENESIS_GRACEFUL_ACTIVATION_PERIOD);
    BOOST_CHECK_EQUAL(policy.GetChronicleGracefulPeriod(), DEFAULT_CHRONICLE_GRACEFUL_ACTIVATION_PERIOD);
}

BOOST_AUTO_TEST_SUITE_END()
