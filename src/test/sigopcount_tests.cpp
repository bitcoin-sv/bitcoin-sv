// Copyright (c) 2012-2016 The Bitcoin Core developers
// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "config.h"
#include "consensus/consensus.h"
#include "consensus/validation.h"
#include "key.h"
#include "pubkey.h"
#include "script/script.h"
#include "script/standard.h"
#include "taskcancellation.h"
#include "script/script_num.h"
#include "test/test_bitcoin.h"
#include "uint256.h"
#include "validation.h"
#include "chainparams.h"
#include "config.h"
#include "script/sign.h"

#include <limits>
#include <tuple>
#include <vector>

#include <boost/test/unit_test.hpp>

namespace
{
    // Helpers:
    std::vector<uint8_t> Serialize(const CScript &s)
    {
        std::vector<uint8_t> sSerialized(s.begin(), s.end());
        return sSerialized;
    }
}

BOOST_FIXTURE_TEST_SUITE(sigopcount_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(GetSigOpCount_WithReturn)
{
    // Tests for GitHub #296 & SVN-2388
    CScriptNum BigNum { std::vector<uint8_t>{1,2,3,4,5}, false, 6, true };
    using TestParams = std::tuple<CScript, bool, uint64_t>; // <Script, triggers error, sig op count>
    std::vector<TestParams> tests {
        {
            // Check we can reproduce the sigops error in a non OP_RETURN script
            CScript{} << BigNum << OP_CHECKMULTISIG,
                true, 0
        },
        {
            // OP_RETURN allows us to skip unexecutable opcodes that follow it at the top level scope
            CScript{} << OP_RETURN << BigNum << OP_CHECKMULTISIG,
                false, 0
        },
        {
            // Script with nested OP_IFs skips unexecutable opcodes that follow OP_RETURN at top level scope
            CScript{} << OP_TRUE << OP_IF << OP_TRUE << OP_IF << OP_RETURN << OP_ELSE << OP_3 << OP_CHECKMULTISIG << OP_ENDIF << OP_ENDIF
                      << OP_RETURN << BigNum << OP_CHECKMULTISIG,
                false, 3
        },
        {
            // Script with nested OP_IFs detects error if not short-circuited by OP_RETURN at top level scope
            CScript{} << OP_TRUE << OP_IF << OP_TRUE << OP_IF << OP_RETURN << OP_ELSE << OP_3 << OP_CHECKMULTISIG << OP_ENDIF << OP_ENDIF
                      << BigNum << OP_CHECKMULTISIG,
                true, 0
        },
        {
            // Invalid script with unbalanced IF/ENDIF
            CScript{} << OP_TRUE << OP_IF << OP_ENDIF << OP_ENDIF,
                true, 0
        }
    };

    for(const auto& [script, errorExpected, sigOpsExpected] : tests)
    {
        bool sigOpCountError {false};
        uint64_t sigOps { script.GetSigOpCount(true, true, sigOpCountError) };
        BOOST_CHECK_EQUAL(sigOpCountError, errorExpected);
        BOOST_CHECK_EQUAL(sigOps, sigOpsExpected);
    }
}

BOOST_AUTO_TEST_CASE(GetSigOpCount) {
    // Test CScript::GetSigOpCount()

    bool sigOpCountError;
    CScript s1;
    BOOST_CHECK_EQUAL(s1.GetSigOpCount(false, false, sigOpCountError), 0U);
    BOOST_CHECK_EQUAL(s1.GetSigOpCount(true, false, sigOpCountError), 0U);

    uint160 dummy;
    s1 << OP_1 << ToByteVector(dummy) << ToByteVector(dummy) << OP_2
       << OP_CHECKMULTISIG;
    BOOST_CHECK_EQUAL(s1.GetSigOpCount(true, false, sigOpCountError), 2U);
    s1 << OP_IF << OP_CHECKSIG << OP_ENDIF;
    BOOST_CHECK_EQUAL(s1.GetSigOpCount(true, false, sigOpCountError), 3U);
    BOOST_CHECK_EQUAL(s1.GetSigOpCount(false, false, sigOpCountError), 21U);

    CScript p2sh = GetScriptForDestination(CScriptID(s1));
    CScript scriptSig;
    scriptSig << OP_0 << Serialize(s1);
    BOOST_CHECK_EQUAL(p2sh.GetSigOpCount(scriptSig, false, sigOpCountError), 3U);

    std::vector<CPubKey> keys;
    for (int i = 0; i < 3; i++) {
        CKey k;
        k.MakeNewKey(true);
        keys.push_back(k.GetPubKey());
    }
    CScript s2 = GetScriptForMultisig(1, keys);
    BOOST_CHECK_EQUAL(s2.GetSigOpCount(true, false, sigOpCountError), 3U);
    BOOST_CHECK_EQUAL(s2.GetSigOpCount(false, false, sigOpCountError), 20U);

    p2sh = GetScriptForDestination(CScriptID(s2));
    BOOST_CHECK_EQUAL(p2sh.GetSigOpCount(true, false, sigOpCountError), 0U);
    BOOST_CHECK_EQUAL(p2sh.GetSigOpCount(false, false, sigOpCountError), 0U);
    CScript scriptSig2;
    scriptSig2 << OP_1 << ToByteVector(dummy) << ToByteVector(dummy)
               << Serialize(s2);
    BOOST_CHECK_EQUAL(p2sh.GetSigOpCount(scriptSig2, false, sigOpCountError), 3U);

    uint64_t maxPubKeysPerMultiSig = 100;   // larger than before genesis limit
    std::vector<CPubKey> keysAfterGenesis;
    for (uint64_t i = 0; i < maxPubKeysPerMultiSig; i++) {
        CKey k;
        k.MakeNewKey(true);
        keysAfterGenesis.push_back(k.GetPubKey());
    }

    CScript s3 = GetScriptForMultisig(1, keysAfterGenesis);
    BOOST_CHECK_EQUAL(s3.GetSigOpCount(false, true, sigOpCountError), maxPubKeysPerMultiSig);

    // Test policy after Genesis
    testConfig.Reset();
    CScript s4;
    s4 << OP_1 << ToByteVector(dummy) << maxPubKeysPerMultiSig - 1 << OP_CHECKMULTISIG;
    BOOST_CHECK_EQUAL(s4.GetSigOpCount(false, true, sigOpCountError), maxPubKeysPerMultiSig - 1);
    CScript s5;
    s5 << OP_1 << ToByteVector(dummy) << maxPubKeysPerMultiSig + 1 << OP_CHECKMULTISIG;
    BOOST_CHECK_EQUAL(s5.GetSigOpCount(false, true, sigOpCountError), maxPubKeysPerMultiSig + 1);

    // Test default policy before Genesis with fAccurate == true
    BOOST_CHECK_EQUAL(s4.GetSigOpCount(true, false, sigOpCountError), MAX_PUBKEYS_PER_MULTISIG_BEFORE_GENESIS);
    BOOST_CHECK_EQUAL(s5.GetSigOpCount(true, false, sigOpCountError), MAX_PUBKEYS_PER_MULTISIG_BEFORE_GENESIS);

    // Test default policy before Genesis with fAccurate == false
    BOOST_CHECK_EQUAL(s4.GetSigOpCount(false, false, sigOpCountError), MAX_PUBKEYS_PER_MULTISIG_BEFORE_GENESIS);
    BOOST_CHECK_EQUAL(s5.GetSigOpCount(false, false, sigOpCountError), MAX_PUBKEYS_PER_MULTISIG_BEFORE_GENESIS);


    CScript scriptMinus1 = CScript() << OP_1 << ToByteVector(dummy) << ToByteVector(dummy) << CScriptNum(-1) << OP_CHECKMULTISIG;
    BOOST_CHECK(scriptMinus1.GetSigOpCount(false, false, sigOpCountError) == MAX_PUBKEYS_PER_MULTISIG_BEFORE_GENESIS);
    BOOST_CHECK(!sigOpCountError);

    BOOST_CHECK(scriptMinus1.GetSigOpCount(true, false, sigOpCountError) == MAX_PUBKEYS_PER_MULTISIG_BEFORE_GENESIS);
    BOOST_CHECK(!sigOpCountError);

    BOOST_CHECK(scriptMinus1.GetSigOpCount(false, true, sigOpCountError) == 0); // treated as error after Genesis
    BOOST_CHECK(sigOpCountError);

    BOOST_CHECK(scriptMinus1.GetSigOpCount(true, true, sigOpCountError) == 0); // treated as error after Genesis
    BOOST_CHECK(sigOpCountError);


    CScript script_OP_9 = CScript() << OP_1 << ToByteVector(dummy) << ToByteVector(dummy) << OP_9 << OP_CHECKMULTISIG;
    BOOST_CHECK(script_OP_9.GetSigOpCount(false, false, sigOpCountError) == MAX_PUBKEYS_PER_MULTISIG_BEFORE_GENESIS);
    BOOST_CHECK(!sigOpCountError);

    BOOST_CHECK(script_OP_9.GetSigOpCount(true, false, sigOpCountError) == 9);
    BOOST_CHECK(!sigOpCountError);

    BOOST_CHECK(script_OP_9.GetSigOpCount(false, true, sigOpCountError) == 9);
    BOOST_CHECK(!sigOpCountError);

    BOOST_CHECK(script_OP_9.GetSigOpCount(true, true, sigOpCountError) == 9);
    BOOST_CHECK(!sigOpCountError);

    CScript script_OP_19 = CScript() << OP_1 << ToByteVector(dummy) << ToByteVector(dummy) << CScriptNum(19) << OP_CHECKMULTISIG;
    BOOST_CHECK(script_OP_19.GetSigOpCount(false, false, sigOpCountError) == MAX_PUBKEYS_PER_MULTISIG_BEFORE_GENESIS);
    BOOST_CHECK(!sigOpCountError);

    BOOST_CHECK(script_OP_19.GetSigOpCount(true, false, sigOpCountError) == MAX_PUBKEYS_PER_MULTISIG_BEFORE_GENESIS); // more than OP_16 is not recognized before geneis
    BOOST_CHECK(!sigOpCountError);

    BOOST_CHECK(script_OP_19.GetSigOpCount(false, true, sigOpCountError) == 19);
    BOOST_CHECK(!sigOpCountError);

    BOOST_CHECK(script_OP_19.GetSigOpCount(true, true, sigOpCountError) == 19);
    BOOST_CHECK(!sigOpCountError);

    std::vector<uint8_t> bignum = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    CScript script_bigNum = CScript() << OP_1 << ToByteVector(dummy) << ToByteVector(dummy) << bignum << OP_CHECKMULTISIG;
    BOOST_CHECK(script_bigNum.GetSigOpCount(false, false, sigOpCountError) == MAX_PUBKEYS_PER_MULTISIG_BEFORE_GENESIS);
    BOOST_CHECK(!sigOpCountError);

    BOOST_CHECK(script_bigNum.GetSigOpCount(true, false, sigOpCountError) == MAX_PUBKEYS_PER_MULTISIG_BEFORE_GENESIS); 
    BOOST_CHECK(!sigOpCountError);

    BOOST_CHECK(script_bigNum.GetSigOpCount(false, true, sigOpCountError) == 0);
    BOOST_CHECK(sigOpCountError); // treated as error after Genesis

    BOOST_CHECK(script_bigNum.GetSigOpCount(true, true, sigOpCountError) == 0);
    BOOST_CHECK(sigOpCountError); // treated as error after Genesis




}

/**
 * Verifies script execution of the zeroth scriptPubKey of tx output and zeroth
 * scriptSig and witness of tx input.
 */
ScriptError VerifyWithFlag(const CTransaction &output,
                           const CMutableTransaction &input, int flags) {
    ScriptError error;
    const Config& config = GlobalConfig::GetConfig();
    CTransaction inputi(input);
    auto ret =
        VerifyScript(
            config, true,
            task::CCancellationSource::Make(),
            inputi.vin[0].scriptSig,
            output.vout[0].scriptPubKey,
            flags,
            TransactionSignatureChecker(&inputi, 0, output.vout[0].nValue),
            &error);
    BOOST_CHECK((ret.value() == true) == (error == SCRIPT_ERR_OK));

    return error;
}

/**
 * Builds a creationTx from scriptPubKey and a spendingTx from scriptSig and
 * witness such that spendingTx spends output zero of creationTx. Also inserts
 * creationTx's output into the coins view.
 */
void BuildTxs(CMutableTransaction &spendingTx, CCoinsViewCache &coins,
              CMutableTransaction &creationTx, const CScript &scriptPubKey,
              const CScript &scriptSig, int nHeight) {
    creationTx.nVersion = 1;
    creationTx.vin.resize(1);
    creationTx.vin[0].prevout = COutPoint();
    creationTx.vin[0].scriptSig = CScript();
    creationTx.vout.resize(1);
    creationTx.vout[0].nValue = Amount(1);
    creationTx.vout[0].scriptPubKey = scriptPubKey;

    spendingTx.nVersion = 1;
    spendingTx.vin.resize(1);
    spendingTx.vin[0].prevout = COutPoint(creationTx.GetId(), 0);
    spendingTx.vin[0].scriptSig = scriptSig;
    spendingTx.vout.resize(1);
    spendingTx.vout[0].nValue = Amount(1);
    spendingTx.vout[0].scriptPubKey = CScript();

    AddCoins(coins, CTransaction(creationTx), false, nHeight, 10 /* scriptPubKey is not a data about, so genesisActivationHEight does not matter */);
}

BOOST_AUTO_TEST_CASE(GetTxSigOpCost) {
    // Transaction creates outputs
    CMutableTransaction creationTx;
    // Transaction that spends outputs and whose sig op cost is going to be
    // tested
    CMutableTransaction spendingTx;

    // Create utxo set
    CCoinsViewEmpty coinsDummy;
    CCoinsViewCache coins(coinsDummy);
    // Create key
    CKey key;
    key.MakeNewKey(true);
    CPubKey pubkey = key.GetPubKey();
    // Default flags
    int flags = SCRIPT_VERIFY_P2SH;
    
    uint64_t genesisHeight = 10;
    testConfig.SetGenesisActivationHeight(genesisHeight);
    

    // Multisig script (legacy counting)
    {
        CScript scriptPubKey = CScript() << 1 << ToByteVector(pubkey)
                                         << ToByteVector(pubkey) << 2
                                         << OP_CHECKMULTISIGVERIFY;
        // Do not use a valid signature to avoid using wallet operations.
        CScript scriptSig = CScript() << OP_0 << OP_0;

        for(int nHeight : {genesisHeight-1, genesisHeight}){ // test before and after genesis (should be the same)
            BuildTxs(spendingTx, coins, creationTx, scriptPubKey, scriptSig, nHeight);
            
            bool genesisEnabled =  IsGenesisEnabled(testConfig, nHeight);
            bool sigOpCountError;

            // Legacy counting only includes signature operations in scriptSigs and
            // scriptPubKeys of a transaction and does not take the actual executed
            // sig operations into account. spendingTx in itself does not contain a
            // signature operation.
            BOOST_REQUIRE(GetTransactionSigOpCount(testConfig, CTransaction(spendingTx), coins,
                                            true, genesisEnabled, sigOpCountError) == 0);
            // creationTx contains two signature operations in its scriptPubKey, but
            // legacy counting is not accurate.
            if (!genesisEnabled)
            {
                BOOST_REQUIRE(GetTransactionSigOpCount(testConfig, CTransaction(creationTx), coins,
                                                true, genesisEnabled, sigOpCountError) == MAX_PUBKEYS_PER_MULTISIG_BEFORE_GENESIS);
            }
            else
            {
                BOOST_REQUIRE(GetTransactionSigOpCount(testConfig, CTransaction(creationTx), coins,
                                                true, genesisEnabled, sigOpCountError) == 2);
            }
            // Sanity check: script verification fails because of an invalid
            // signature.
            BOOST_REQUIRE(VerifyWithFlag(CTransaction(creationTx), spendingTx, flags) ==
                   SCRIPT_ERR_CHECKMULTISIGVERIFY);
        }

    }

    // Multisig nested in P2SH
    {
        CScript redeemScript = CScript() << 1 << ToByteVector(pubkey)
                                         << ToByteVector(pubkey) << 2
                                         << OP_CHECKMULTISIGVERIFY;
        CScript scriptPubKey = GetScriptForDestination(CScriptID(redeemScript));
        CScript scriptSig = CScript()
                            << OP_0 << OP_0 << ToByteVector(redeemScript);
        { // testing before genesis
            bool sigOpCountError;
            BuildTxs(spendingTx, coins, creationTx, scriptPubKey, scriptSig, genesisHeight-1);
            BOOST_REQUIRE(GetTransactionSigOpCount(testConfig, CTransaction(spendingTx), coins,
                                            true, false, sigOpCountError) == 2);
            BOOST_REQUIRE(VerifyWithFlag(CTransaction(creationTx), spendingTx, flags) ==
                    SCRIPT_ERR_CHECKMULTISIGVERIFY);
        }
        { // testing after genesis
            bool sigOpCountError;
            BuildTxs(spendingTx, coins, creationTx, scriptPubKey, scriptSig, genesisHeight);
            BOOST_REQUIRE(GetTransactionSigOpCount(testConfig, CTransaction(spendingTx), coins,
                                            true, true, sigOpCountError) == 0);
            BOOST_REQUIRE(VerifyWithFlag(CTransaction(creationTx), spendingTx, flags) ==
                    SCRIPT_ERR_CHECKMULTISIGVERIFY);
        }
    }

    // Test 100 pub keys after genesis (testing policy rule)
    {
        ///signature was taken from a random transaction on whatsonchain.com
        std::vector<uint8_t> signature = ParseHex("3045022100b96e65395c5f2e4dbcef1480ac692ba7b35d74e4b35c95f3d83c3734dc66fe0202205e756a979c3f67089a1ecf22cd72bd7a43f8eed532d5be94c72120848e5b12b001");
        testConfig.SetMaxPubKeysPerMultiSigPolicy(100);

        CScript scriptPubKey = CScript() << OP_1;
        for (int i = 0; i < 100; i++)
          scriptPubKey << ToByteVector(pubkey);
        scriptPubKey << CScriptNum(100) << OP_CHECKMULTISIGVERIFY;
        // Do not use a valid signature to avoid using wallet operations.
        CScript scriptSig = CScript() << OP_0 << signature;

        BuildTxs(spendingTx, coins, creationTx, scriptPubKey, scriptSig, genesisHeight);

        bool sigOpCountError;

        // Legacy counting only includes signature operations in scriptSigs and
        // scriptPubKeys of a transaction and does not take the actual executed
        // sig operations into account. spendingTx in itself does not contain a
        // signature operation.
        BOOST_CHECK(GetTransactionSigOpCount(testConfig, CTransaction(spendingTx), coins, false, true, sigOpCountError) == 0);
        // creationTx contains two signature operations in its scriptPubKey, but
        // legacy counting is not accurate.
        BOOST_CHECK(GetTransactionSigOpCount(testConfig, CTransaction(creationTx), coins, false, true, sigOpCountError) == 100);
        // Sanity check: script verification fails because of an invalid
        // signature.
        flags = SCRIPT_UTXO_AFTER_GENESIS;
        BOOST_CHECK(VerifyWithFlag(CTransaction(creationTx), spendingTx, flags) == SCRIPT_ERR_CHECKMULTISIGVERIFY);
    }

    // Test overflow error with too big number
    {
        std::vector< uint8_t> bignum = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
        CScript scriptPubKey = CScript() << 1 << ToByteVector(pubkey)
            << ToByteVector(pubkey) << bignum
            << OP_CHECKMULTISIGVERIFY;
        CScript scriptSig = CScript() << OP_0 << OP_0;

        bool sigOpCountError;
        BuildTxs(spendingTx, coins, creationTx, scriptPubKey, scriptSig, genesisHeight);
        BOOST_CHECK(GetTransactionSigOpCount(testConfig, CTransaction(creationTx), coins, true, true, sigOpCountError) == 0);
        BOOST_CHECK(sigOpCountError == true);
    }

    // Test with negative number of pubkeys
    {
        CScript scriptPubKey = CScript() << 1 << ToByteVector(pubkey)
            << ToByteVector(pubkey) << CScriptNum(-1)
            << OP_CHECKMULTISIGVERIFY;
        CScript scriptSig = CScript() << OP_0 << OP_0;

        bool sigOpCountError;
        BuildTxs(spendingTx, coins, creationTx, scriptPubKey, scriptSig, genesisHeight);
        BOOST_CHECK(GetTransactionSigOpCount(testConfig, CTransaction(creationTx), coins, true, true, sigOpCountError) == 0);
        BOOST_CHECK(sigOpCountError == true);

        BOOST_CHECK(VerifyWithFlag(CTransaction(creationTx), spendingTx, flags) == SCRIPT_ERR_PUBKEY_COUNT);
    }
    
}

BOOST_AUTO_TEST_CASE(test_sigops_limits) {
    Config& config = GlobalConfig::GetConfig();
    std::string error;
    uint64_t expected_res = MAX_BLOCK_SIGOPS_PER_MB_BEFORE_GENESIS;

    BOOST_CHECK_EQUAL(config.GetMaxBlockSigOpsConsensusBeforeGenesis(1), expected_res);
    BOOST_CHECK_EQUAL(config.GetMaxBlockSigOpsConsensusBeforeGenesis(123456), expected_res);
    BOOST_CHECK_EQUAL(config.GetMaxBlockSigOpsConsensusBeforeGenesis(1000000), expected_res);
    BOOST_CHECK_EQUAL(config.GetMaxBlockSigOpsConsensusBeforeGenesis(1000001), 2 * expected_res);
    BOOST_CHECK_EQUAL(config.GetMaxBlockSigOpsConsensusBeforeGenesis(1348592), 2 * expected_res);
    BOOST_CHECK_EQUAL(config.GetMaxBlockSigOpsConsensusBeforeGenesis(2000000), 2 * expected_res);
    BOOST_CHECK_EQUAL(config.GetMaxBlockSigOpsConsensusBeforeGenesis(2000001), 3 * expected_res);
    BOOST_CHECK_EQUAL(config.GetMaxBlockSigOpsConsensusBeforeGenesis(2654321), 3 * expected_res);
    BOOST_CHECK_EQUAL(config.GetMaxBlockSigOpsConsensusBeforeGenesis(std::numeric_limits<uint32_t>::max()), 4295 * expected_res);
}

void TestMaxSigOps(const Config& globalConfig, uint64_t maxTxSigOpsCount, uint64_t maxTxSize)
{
    CMutableTransaction tx;
    tx.nVersion = 1;
    tx.vin.resize(1);
    tx.vin[0].prevout = COutPoint(InsecureRand256(), 0);
    tx.vin[0].scriptSig = CScript();
    tx.vout.resize(1);
    tx.vout[0].nValue = Amount(1);
    tx.vout[0].scriptPubKey = CScript();

    {
        CValidationState state;
        BOOST_CHECK(CheckRegularTransaction(CTransaction(tx), state, maxTxSigOpsCount, maxTxSize, false));
        BOOST_CHECK(CheckRegularTransaction(CTransaction(tx), state, maxTxSigOpsCount, maxTxSize, true));
    }

    // Get just before the limit.
    for (size_t i = 0; i < maxTxSigOpsCount; i++) {
        tx.vout[0].scriptPubKey << OP_CHECKSIG;
    }

    {
        CValidationState state;
        BOOST_CHECK(CheckRegularTransaction(CTransaction(tx), state, maxTxSigOpsCount, maxTxSize, false));
        BOOST_CHECK(CheckRegularTransaction(CTransaction(tx), state, maxTxSigOpsCount, maxTxSize, true));
    }

    // And go over.
    tx.vout[0].scriptPubKey << OP_CHECKSIG;

    {
        CValidationState state;
        BOOST_CHECK(!CheckRegularTransaction(CTransaction(tx), state, maxTxSigOpsCount, maxTxSize, false));
        BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-txn-sigops");
        BOOST_CHECK(CheckRegularTransaction(CTransaction(tx), state, maxTxSigOpsCount, maxTxSize, true));
    }
}

BOOST_AUTO_TEST_CASE(test_max_sigops_per_tx)
{


    /* Case 1: Genesis is not enabled, consensus - MAX_TX_SIGOPS_COUNT_BEFORE_GENESIS */
    uint64_t maxTxSigOpsCountConsensusBeforeGenesis = testConfig.GetMaxTxSigOpsCountConsensusBeforeGenesis();
    BOOST_CHECK_EQUAL(maxTxSigOpsCountConsensusBeforeGenesis, MAX_TX_SIGOPS_COUNT_BEFORE_GENESIS);
    TestMaxSigOps(testConfig, maxTxSigOpsCountConsensusBeforeGenesis, MAX_TX_SIZE_CONSENSUS_BEFORE_GENESIS);

    /* Case 2: Genesis is not enabled, policy - MAX_TX_SIGOPS_COUNT_POLICY_BEFORE_GENESIS */
    uint64_t maxTxSigOpsCountPolicy = testConfig.GetMaxTxSigOpsCountPolicy(false);
    BOOST_CHECK_EQUAL(maxTxSigOpsCountPolicy, MAX_TX_SIGOPS_COUNT_POLICY_BEFORE_GENESIS);

    /* Case 3: Genesis is enabled, default policy - MAX_TX_SIGOPS_COUNT_POLICY_AFTER_GENESIS */
    maxTxSigOpsCountPolicy = testConfig.GetMaxTxSigOpsCountPolicy(true);
    BOOST_CHECK_EQUAL(maxTxSigOpsCountPolicy, MAX_TX_SIGOPS_COUNT_POLICY_AFTER_GENESIS);

    /* Case 4: policy is applied with value 0 - returns MAX_TX_SIGOPS_COUNT_POLICY_AFTER_GENESIS */
    std::string error("");
    BOOST_CHECK(testConfig.SetMaxTxSigOpsCountPolicy(0, &error));
    BOOST_CHECK_EQUAL(error, "");
    maxTxSigOpsCountPolicy = testConfig.GetMaxTxSigOpsCountPolicy(true);
    BOOST_CHECK_EQUAL(maxTxSigOpsCountPolicy, MAX_TX_SIGOPS_COUNT_POLICY_AFTER_GENESIS);

    /* Case 5: policy is applied - returns set value */
    BOOST_CHECK(testConfig.SetMaxTxSigOpsCountPolicy(20500, &error));
    BOOST_CHECK_EQUAL(error,"");
    maxTxSigOpsCountPolicy = testConfig.GetMaxTxSigOpsCountPolicy(true);
    BOOST_CHECK_EQUAL(maxTxSigOpsCountPolicy, 20500U);
    
    /* Case 6: Policy is applied with too big value - previous value must not be changed */
    BOOST_CHECK(!testConfig.SetMaxTxSigOpsCountPolicy(static_cast<int64_t>(MAX_TX_SIGOPS_COUNT_POLICY_AFTER_GENESIS) + 1, &error));
    BOOST_CHECK(error.find("Policy value for maximum allowed number of signature operations per transaction must not exceed limit of") != std::string::npos);
    maxTxSigOpsCountPolicy = testConfig.GetMaxTxSigOpsCountPolicy(true);
    BOOST_CHECK_EQUAL(maxTxSigOpsCountPolicy, 20500U);

    /* Case 7: Policy is applied with negative value - previous value must not be changed */
    BOOST_CHECK(!testConfig.SetMaxTxSigOpsCountPolicy(-123, &error));
    BOOST_CHECK_EQUAL(error, "Policy value for maximum allowed number of signature operations per transaction cannot be less than 0");
    maxTxSigOpsCountPolicy = testConfig.GetMaxTxSigOpsCountPolicy(true);
    BOOST_CHECK_EQUAL(maxTxSigOpsCountPolicy, 20500U);
}

BOOST_AUTO_TEST_SUITE_END()
