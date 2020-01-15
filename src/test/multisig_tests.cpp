// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "key.h"
#include "keystore.h"
#include "policy/policy.h"
#include "script/interpreter.h"
#include "script/ismine.h"
#include "script/script.h"
#include "script/script_error.h"
#include "script/sighashtype.h"
#include "script/sign.h"
#include "taskcancellation.h"
#include "test/test_bitcoin.h"
#include "uint256.h"
#include "chainparams.h"
#include "config.h"

#include <boost/test/unit_test.hpp>

typedef std::vector<uint8_t> valtype;

BOOST_FIXTURE_TEST_SUITE(multisig_tests, BasicTestingSetup)

CScript sign_multisig(CScript scriptPubKey, std::vector<CKey> keys,
                      CMutableTransaction mutableTransaction, int whichIn) {
    uint256 hash = SignatureHash(scriptPubKey, CTransaction(mutableTransaction),
                                 whichIn, SigHashType(), Amount(0));

    CScript result;
    // CHECKMULTISIG bug workaround
    result << OP_0;
    for (const CKey &key : keys) {
        std::vector<uint8_t> vchSig;
        BOOST_CHECK(key.Sign(hash, vchSig));
        vchSig.push_back(uint8_t(SIGHASH_ALL));
        result << vchSig;
    }
    return result;
}

BOOST_AUTO_TEST_CASE(multisig_verify) {
    uint32_t flags = SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_STRICTENC;

    ScriptError err;
    CKey key[4];
    Amount amount(0);
    for (int i = 0; i < 4; i++) {
        key[i].MakeNewKey(true);
    }

    CScript a_and_b;
    a_and_b << OP_2 << ToByteVector(key[0].GetPubKey())
            << ToByteVector(key[1].GetPubKey()) << OP_2 << OP_CHECKMULTISIG;

    CScript a_or_b;
    a_or_b << OP_1 << ToByteVector(key[0].GetPubKey())
           << ToByteVector(key[1].GetPubKey()) << OP_2 << OP_CHECKMULTISIG;

    CScript escrow;
    escrow << OP_2 << ToByteVector(key[0].GetPubKey())
           << ToByteVector(key[1].GetPubKey())
           << ToByteVector(key[2].GetPubKey()) << OP_3 << OP_CHECKMULTISIG;

    // Funding transaction
    CMutableTransaction txFrom;
    txFrom.vout.resize(3);
    txFrom.vout[0].scriptPubKey = a_and_b;
    txFrom.vout[1].scriptPubKey = a_or_b;
    txFrom.vout[2].scriptPubKey = escrow;

    // Spending transaction
    CMutableTransaction txTo[3];
    for (int i = 0; i < 3; i++) {
        txTo[i].vin.resize(1);
        txTo[i].vout.resize(1);
        txTo[i].vin[0].prevout = COutPoint(txFrom.GetId(), i);
        txTo[i].vout[0].nValue = Amount(1);
    }

    std::vector<CKey> keys;
    CScript s;

    // Test a AND b:
    keys.assign(1, key[0]);
    keys.push_back(key[1]);
    s = sign_multisig(a_and_b, keys, txTo[0], 0);
    auto source = task::CCancellationSource::Make();
    auto res =
        VerifyScript(
            testConfig, true,
            source->GetToken(),
            s,
            a_and_b,
            flags,
            MutableTransactionSignatureChecker(&txTo[0], 0, amount),
            &err);
    BOOST_CHECK(res.value());
    BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_OK, ScriptErrorString(err));

    for (int i = 0; i < 4; i++) {
        keys.assign(1, key[i]);
        s = sign_multisig(a_and_b, keys, txTo[0], 0);
        res =
            VerifyScript(
                testConfig, true,
                source->GetToken(),
                s,
                a_and_b,
                flags,
                MutableTransactionSignatureChecker(&txTo[0], 0, amount),
                &err);
        BOOST_CHECK_MESSAGE(!res.value(), strprintf("a&b 1: %d", i));
        BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_INVALID_STACK_OPERATION,
                            ScriptErrorString(err));

        keys.assign(1, key[1]);
        keys.push_back(key[i]);
        s = sign_multisig(a_and_b, keys, txTo[0], 0);
        res =
            VerifyScript(
                testConfig, true,
                source->GetToken(),
                s,
                a_and_b,
                flags,
                MutableTransactionSignatureChecker(&txTo[0], 0, amount),
                &err);
        BOOST_CHECK_MESSAGE(!res.value(), strprintf("a&b 2: %d", i));
        BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_EVAL_FALSE,
                            ScriptErrorString(err));
    }

    // Test a OR b:
    for (int i = 0; i < 4; i++) {
        keys.assign(1, key[i]);
        s = sign_multisig(a_or_b, keys, txTo[1], 0);
        if (i == 0 || i == 1) {
            res =
                VerifyScript(
                    testConfig, true,
                    source->GetToken(),
                    s,
                    a_or_b,
                    flags,
                    MutableTransactionSignatureChecker(&txTo[1], 0, amount),
                    &err);
            BOOST_CHECK_MESSAGE(res.value(), strprintf("a|b: %d", i));
            BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_OK, ScriptErrorString(err));
        } else {
            res =
                VerifyScript(
                    testConfig, true,
                    source->GetToken(),
                    s,
                    a_or_b,
                    flags,
                    MutableTransactionSignatureChecker(&txTo[1], 0, amount),
                    &err);
            BOOST_CHECK_MESSAGE(!res.value(), strprintf("a|b: %d", i));
            BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_EVAL_FALSE,
                                ScriptErrorString(err));
        }
    }
    s.clear();
    s << OP_0 << OP_1;
    res =
        VerifyScript(
            testConfig, true,
            source->GetToken(),
            s,
            a_or_b,
            flags,
            MutableTransactionSignatureChecker(&txTo[1], 0, amount),
            &err);
    BOOST_CHECK(!res.value());
    BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_SIG_DER, ScriptErrorString(err));

    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            keys.assign(1, key[i]);
            keys.push_back(key[j]);
            s = sign_multisig(escrow, keys, txTo[2], 0);
            if (i < j && i < 3 && j < 3) {
                res =
                    VerifyScript(
                        testConfig, true,
                        source->GetToken(),
                        s,
                        escrow,
                        flags,
                        MutableTransactionSignatureChecker(&txTo[2], 0, amount),
                        &err);
                BOOST_CHECK_MESSAGE(res.value(), strprintf("escrow 1: %d %d", i, j));
                BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_OK,
                                    ScriptErrorString(err));
            } else {
                res =
                    VerifyScript(
                        testConfig, true,
                        source->GetToken(),
                        s,
                        escrow,
                        flags,
                        MutableTransactionSignatureChecker(&txTo[2], 0, amount),
                        &err);
                BOOST_CHECK_MESSAGE(
                    !res.value(),
                    strprintf("escrow 2: %d %d", i, j));
                BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_EVAL_FALSE,
                                    ScriptErrorString(err));
            }
        }
}

BOOST_AUTO_TEST_CASE(multisig_IsStandard) {

    DummyConfig config(CBaseChainParams::MAIN);

    CKey key[4];
    for (int i = 0; i < 4; i++)
        key[i].MakeNewKey(true);

    txnouttype whichType;

    CScript a_and_b;
    a_and_b << OP_2 << ToByteVector(key[0].GetPubKey())
            << ToByteVector(key[1].GetPubKey()) << OP_2 << OP_CHECKMULTISIG;
    BOOST_CHECK(::IsStandard(config, a_and_b, 1, whichType));

    CScript a_or_b;
    a_or_b << OP_1 << ToByteVector(key[0].GetPubKey())
           << ToByteVector(key[1].GetPubKey()) << OP_2 << OP_CHECKMULTISIG;
    BOOST_CHECK(::IsStandard(config, a_or_b, 1, whichType));

    CScript escrow;
    escrow << OP_2 << ToByteVector(key[0].GetPubKey())
           << ToByteVector(key[1].GetPubKey())
           << ToByteVector(key[2].GetPubKey()) << OP_3 << OP_CHECKMULTISIG;
    BOOST_CHECK(::IsStandard(config, escrow, 1, whichType));

    CScript one_of_four;
    one_of_four << OP_1 << ToByteVector(key[0].GetPubKey())
                << ToByteVector(key[1].GetPubKey())
                << ToByteVector(key[2].GetPubKey())
                << ToByteVector(key[3].GetPubKey()) << OP_4 << OP_CHECKMULTISIG;
    BOOST_CHECK(!::IsStandard(config, one_of_four, 1, whichType));

    CScript malformed[6];
    malformed[0] << OP_3 << ToByteVector(key[0].GetPubKey())
                 << ToByteVector(key[1].GetPubKey()) << OP_2
                 << OP_CHECKMULTISIG;
    malformed[1] << OP_2 << ToByteVector(key[0].GetPubKey())
                 << ToByteVector(key[1].GetPubKey()) << OP_3
                 << OP_CHECKMULTISIG;
    malformed[2] << OP_0 << ToByteVector(key[0].GetPubKey())
                 << ToByteVector(key[1].GetPubKey()) << OP_2
                 << OP_CHECKMULTISIG;
    malformed[3] << OP_1 << ToByteVector(key[0].GetPubKey())
                 << ToByteVector(key[1].GetPubKey()) << OP_0
                 << OP_CHECKMULTISIG;
    malformed[4] << OP_1 << ToByteVector(key[0].GetPubKey())
                 << ToByteVector(key[1].GetPubKey()) << OP_CHECKMULTISIG;
    malformed[5] << OP_1 << ToByteVector(key[0].GetPubKey())
                 << ToByteVector(key[1].GetPubKey());

    for (int i = 0; i < 6; i++)
        BOOST_CHECK(!::IsStandard(config, malformed[i], 1, whichType));
}

BOOST_AUTO_TEST_CASE(multisig_Solver1) {
    // Tests Solver() that returns lists of keys that are required to satisfy a
    // ScriptPubKey
    //
    // Also tests IsMine() and ExtractDestination()
    //
    // Note: ExtractDestination for the multisignature transactions always
    // returns false for this release, even if you have one key that would
    // satisfy an (a|b) or 2-of-3 keys needed to spend an escrow transaction.
    //
    CBasicKeyStore keystore, emptykeystore, partialkeystore;
    CKey key[3];
    CTxDestination keyaddr[3];
    for (int i = 0; i < 3; i++) {
        key[i].MakeNewKey(true);
        keystore.AddKey(key[i]);
        keyaddr[i] = key[i].GetPubKey().GetID();
    }
    partialkeystore.AddKey(key[0]);

    { // P2PK
        std::vector<valtype> solutions;
        txnouttype whichType;
        CScript s;
        s << ToByteVector(key[0].GetPubKey()) << OP_CHECKSIG;
        for(bool genesisEnabled : {true, false}){
            BOOST_CHECK(Solver(s, genesisEnabled, whichType, solutions));
            BOOST_CHECK(solutions.size() == 1);
            CTxDestination addr;
            BOOST_CHECK(ExtractDestination(s, genesisEnabled, addr));
            BOOST_CHECK(addr == keyaddr[0]);
        }
        BOOST_CHECK(IsMine(keystore, s));
        BOOST_CHECK(!IsMine(emptykeystore, s));
    }
    { // P2PKH
        std::vector<valtype> solutions;
        txnouttype whichType;
        CScript s;
        s << OP_DUP << OP_HASH160 << ToByteVector(key[0].GetPubKey().GetID())
          << OP_EQUALVERIFY << OP_CHECKSIG;
        for(bool genesisEnabled : {true, false}){
            BOOST_CHECK(Solver(s, genesisEnabled, whichType, solutions));
            BOOST_CHECK(solutions.size() == 1);
            CTxDestination addr;        
            BOOST_CHECK(ExtractDestination(s, genesisEnabled, addr));
            BOOST_CHECK(addr == keyaddr[0]);
        }
        BOOST_CHECK(!IsMine(emptykeystore, s));
        BOOST_CHECK(IsMine(keystore, s));
    }
    { // Data - has no address  - just test ExtractDestination() and IsMine() since we have Solver() dedicated tests in script_tests.cpp (script_Solver)
        std::vector<uint8_t> data(200, 1);
        std::vector<valtype> solutions;
        CScript opReturn = CScript() << OP_RETURN << data;
        CTxDestination addr;
        for(bool genesisEnabled : {true, false}){
            BOOST_CHECK(!ExtractDestination(opReturn, genesisEnabled, addr));
        }
        BOOST_CHECK(!IsMine(keystore, opReturn));
        CScript opFalseOpReturn = CScript() << OP_FALSE << OP_RETURN;
        for(bool genesisEnabled : {true, false}){
            BOOST_CHECK(!ExtractDestination(opReturn, genesisEnabled, addr));
        }
        BOOST_CHECK(!IsMine(keystore, opReturn));

    }
    { // Multisig
        std::vector<valtype> solutions;
        txnouttype whichType;
        CScript s;
        s << OP_2 << ToByteVector(key[0].GetPubKey())
          << ToByteVector(key[1].GetPubKey()) << OP_2 << OP_CHECKMULTISIG;
        for(bool genesisEnabled : {true, false}){
            BOOST_CHECK(Solver(s, genesisEnabled, whichType, solutions));
            BOOST_CHECK_EQUAL(solutions.size(), 4U);
            CTxDestination addr;
            BOOST_CHECK(!ExtractDestination(s, genesisEnabled, addr));
        }
        BOOST_CHECK(IsMine(keystore, s));
        BOOST_CHECK(!IsMine(emptykeystore, s));
        BOOST_CHECK(!IsMine(partialkeystore, s));
    }
    { // Multisig
        std::vector<valtype> solutions;
        txnouttype whichType;
        CScript s;
        s << OP_1 << ToByteVector(key[0].GetPubKey())
          << ToByteVector(key[1].GetPubKey()) << OP_2 << OP_CHECKMULTISIG;

        for(bool genesisEnabled : {true, false}){
            BOOST_CHECK(Solver(s, genesisEnabled, whichType, solutions));
            BOOST_CHECK_EQUAL(solutions.size(), 4U);
            
            std::vector<CTxDestination> addrs;
            int nRequired;        
            BOOST_CHECK(ExtractDestinations(s, genesisEnabled, whichType, addrs, nRequired));
            BOOST_CHECK(addrs[0] == keyaddr[0]);
            BOOST_CHECK(addrs[1] == keyaddr[1]);
            BOOST_CHECK(nRequired == 1);

        }

        BOOST_CHECK(IsMine(keystore, s));
        BOOST_CHECK(!IsMine(emptykeystore, s));
        BOOST_CHECK(!IsMine(partialkeystore, s));
    }
    { // Multisig
        std::vector<valtype> solutions;
        txnouttype whichType;
        CScript s;
        s << OP_2 << ToByteVector(key[0].GetPubKey())
          << ToByteVector(key[1].GetPubKey())
          << ToByteVector(key[2].GetPubKey()) << OP_3 << OP_CHECKMULTISIG;
        BOOST_CHECK(Solver(s, true, whichType, solutions));
        BOOST_CHECK(solutions.size() == 5);
        BOOST_CHECK(Solver(s, false, whichType, solutions));
        BOOST_CHECK(solutions.size() == 5);

    }
}

BOOST_AUTO_TEST_CASE(multisig_Sign) {
    // Test SignSignature() (and therefore the version of Solver() that signs
    // transactions)
    CBasicKeyStore keystore;
    CKey key[4];
    for (int i = 0; i < 4; i++) {
        key[i].MakeNewKey(true);
        keystore.AddKey(key[i]);
    }

    CScript a_and_b;
    a_and_b << OP_2 << ToByteVector(key[0].GetPubKey())
            << ToByteVector(key[1].GetPubKey()) << OP_2 << OP_CHECKMULTISIG;

    CScript a_or_b;
    a_or_b << OP_1 << ToByteVector(key[0].GetPubKey())
           << ToByteVector(key[1].GetPubKey()) << OP_2 << OP_CHECKMULTISIG;

    CScript escrow;
    escrow << OP_2 << ToByteVector(key[0].GetPubKey())
           << ToByteVector(key[1].GetPubKey())
           << ToByteVector(key[2].GetPubKey()) << OP_3 << OP_CHECKMULTISIG;

    // Funding transaction
    CMutableTransaction txFrom;
    txFrom.vout.resize(3);
    txFrom.vout[0].scriptPubKey = a_and_b;
    txFrom.vout[1].scriptPubKey = a_or_b;
    txFrom.vout[2].scriptPubKey = escrow;

    // Spending transaction
    CMutableTransaction txTo[3];
    for (int i = 0; i < 3; i++) {
        txTo[i].vin.resize(1);
        txTo[i].vout.resize(1);
        txTo[i].vin[0].prevout = COutPoint(txFrom.GetId(), i);
        txTo[i].vout[0].nValue = Amount(1);
    }

    for (int i = 0; i < 3; i++) {
        BOOST_CHECK_MESSAGE(SignSignature(testConfig, keystore, true, true, CTransaction(txFrom),
                                          txTo[i], 0,
                                          SigHashType().withForkId()),
                            strprintf("SignSignature %d", i));
        BOOST_CHECK_MESSAGE(SignSignature(testConfig, keystore, true, false, CTransaction(txFrom),
                                          txTo[i], 0,
                                          SigHashType().withForkId()),
                            strprintf("SignSignature %d", i));
    }
}

BOOST_AUTO_TEST_SUITE_END()
