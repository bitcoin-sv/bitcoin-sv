// Copyright (c) 2026 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#include "config.h"

#if defined(HAVE_CONSENSUS_LIB)

#include "amount.h"
#include "key.h"
#include "primitives/transaction.h"
#include "pubkey.h"
#include "script/bitcoinconsensus.h"
#include "script/interpreter.h"
#include "script/script.h"
#include "script/script_num.h"
#include "script/sighashtype.h"
#include "streams.h"
#include "test/test_bitcoin.h"
#include "version.h"

#include <cstdint>
#include <vector>

#include <boost/test/unit_test.hpp>

namespace {

std::vector<uint8_t> Serialize(const CMutableTransaction& tx)
{
    CDataStream stream { SER_NETWORK, PROTOCOL_VERSION };
    stream << tx;
    return {stream.begin(), stream.end()};
}

CMutableTransaction BuildCredit(const CScript& scriptPubKey, const Amount& value)
{
    CMutableTransaction tx {};
    tx.nVersion = 1;
    tx.nLockTime = 0;
    tx.vin.resize(1);
    tx.vout.resize(1);
    tx.vin[0].prevout = COutPoint();
    tx.vin[0].scriptSig = CScript() << CScriptNum(0) << CScriptNum(0);
    tx.vin[0].nSequence = CTxIn::SEQUENCE_FINAL;
    tx.vout[0].scriptPubKey = scriptPubKey;
    tx.vout[0].nValue = value;
    return tx;
}

CMutableTransaction BuildSpend(const CScript& scriptSig, const CMutableTransaction& credit)
{
    CMutableTransaction tx {};
    tx.nVersion = 1;
    tx.nLockTime = 0;
    tx.vin.resize(1);
    tx.vout.resize(1);
    tx.vin[0].prevout = COutPoint(credit.GetId(), 0);
    tx.vin[0].scriptSig = scriptSig;
    tx.vin[0].nSequence = CTxIn::SEQUENCE_FINAL;
    tx.vout[0].scriptPubKey = CScript();
    tx.vout[0].nValue = credit.vout[0].nValue;
    return tx;
}

} // namespace

BOOST_FIXTURE_TEST_SUITE(bitcoinconsensus_tests, BasicTestingSetup)

// Version 2 of the API exposes the Genesis/Chronicle era flags and FORKID.
BOOST_AUTO_TEST_CASE(version)
{
    BOOST_CHECK_EQUAL(bitcoinconsensus_version(), 2U);
}

// Regression guard for the reported bug: the era flags and FORKID must be
// accepted by the public API rather than rejected as invalid flags. Before the
// fix every flag outside P2SH|DERSIG|NULLDUMMY|CLTV|CSV produced
// bitcoinconsensus_ERR_INVALID_FLAGS, so post-Genesis/Chronicle outputs could
// not be expressed at all.
BOOST_AUTO_TEST_CASE(era_flags_accepted)
{
    const Config& config = GlobalConfig::GetConfig();

    // Trivial always-true script so the call exercises flag handling, not script
    // logic: empty scriptSig followed by OP_1 leaves a true value on the stack.
    const CScript scriptPubKey = CScript() << OP_1;
    const CMutableTransaction credit = BuildCredit(scriptPubKey, Amount(0));
    const CMutableTransaction spend = BuildSpend(CScript(), credit);
    const std::vector<uint8_t> txBytes = Serialize(spend);

    const unsigned int eraFlagSets[] =
    {
        bitcoinconsensus_SCRIPT_GENESIS,
        bitcoinconsensus_SCRIPT_GENESIS | bitcoinconsensus_SCRIPT_UTXO_AFTER_GENESIS,
        bitcoinconsensus_SCRIPT_GENESIS | bitcoinconsensus_SCRIPT_UTXO_AFTER_GENESIS |
            bitcoinconsensus_SCRIPT_CHRONICLE | bitcoinconsensus_SCRIPT_UTXO_AFTER_CHRONICLE,
    };

    for(const unsigned int flags : eraFlagSets)
    {
        bitcoinconsensus_error err = bitcoinconsensus_ERR_INVALID_FLAGS;
        const int ok = bitcoinconsensus_verify_script_with_amount(
            config, scriptPubKey.data(), scriptPubKey.size(), 0,
            txBytes.data(), txBytes.size(), 0, flags, &err);
        BOOST_CHECK_EQUAL(err, bitcoinconsensus_ERR_OK);
        BOOST_CHECK_EQUAL(ok, 1);
    }

    // FORKID likewise must be accepted (it used to be masked out of VERIFY_ALL,
    // making _with_amount unusable for any real BSV input).
    bitcoinconsensus_error err = bitcoinconsensus_ERR_INVALID_FLAGS;
    const int ok = bitcoinconsensus_verify_script_with_amount(
        config, scriptPubKey.data(), scriptPubKey.size(), 0,
        txBytes.data(), txBytes.size(), 0,
        bitcoinconsensus_SCRIPT_ENABLE_SIGHASH_FORKID, &err);
    BOOST_CHECK_EQUAL(err, bitcoinconsensus_ERR_OK);
    BOOST_CHECK_EQUAL(ok, 1);
}

// The era flag must actually change the verdict. A script number wider than 4
// bytes exceeds the pre-Genesis limit but is permitted once the UTXO is flagged
// as post-Genesis, so the same script verifies differently depending on the
// SCRIPT_UTXO_AFTER_GENESIS bit.
BOOST_AUTO_TEST_CASE(era_flag_changes_verdict)
{
    const Config& config = GlobalConfig::GetConfig();

    // Push a 5-byte number then OP_1ADD. CScriptNum rejects operands longer than
    // 4 bytes pre-Genesis (SCRIPTNUM overflow), but allows them post-Genesis,
    // where the result (non-zero) leaves the script true.
    const std::vector<uint8_t> fiveByteNum{0x01, 0x00, 0x00, 0x00, 0x00};
    const CScript scriptPubKey = CScript() << fiveByteNum << OP_1ADD;
    const CMutableTransaction credit = BuildCredit(scriptPubKey, Amount(0));
    const CMutableTransaction spend = BuildSpend(CScript(), credit);
    const std::vector<uint8_t> txBytes = Serialize(spend);

    // Pre-Genesis: rejected.
    {
        bitcoinconsensus_error err = bitcoinconsensus_ERR_INVALID_FLAGS;
        const int ok = bitcoinconsensus_verify_script_with_amount(
            config, scriptPubKey.data(), scriptPubKey.size(), 0,
            txBytes.data(), txBytes.size(), 0,
            bitcoinconsensus_SCRIPT_FLAGS_VERIFY_NONE, &err);
        BOOST_CHECK_EQUAL(err, bitcoinconsensus_ERR_OK);
        BOOST_CHECK_EQUAL(ok, 0);
    }

    // Post-Genesis: accepted.
    {
        bitcoinconsensus_error err = bitcoinconsensus_ERR_INVALID_FLAGS;
        const int ok = bitcoinconsensus_verify_script_with_amount(
            config, scriptPubKey.data(), scriptPubKey.size(), 0,
            txBytes.data(), txBytes.size(), 0,
            bitcoinconsensus_SCRIPT_UTXO_AFTER_GENESIS, &err);
        BOOST_CHECK_EQUAL(err, bitcoinconsensus_ERR_OK);
        BOOST_CHECK_EQUAL(ok, 1);
    }
}

// A real input signed with SIGHASH_FORKID must verify through _with_amount when
// FORKID is enabled. This is the first live exercise of the formerly-dead FORKID
// path. The matching no-amount entry point must still report ERR_AMOUNT_REQUIRED.
BOOST_AUTO_TEST_CASE(forkid_signature_roundtrip)
{
    const Config& config = GlobalConfig::GetConfig();

    CKey key;
    key.MakeNewKey(true);
    const CPubKey pubkey = key.GetPubKey();

    const CScript scriptPubKey = CScript() << ToByteVector(pubkey) << OP_CHECKSIG;
    const Amount value(100000);
    const CMutableTransaction credit = BuildCredit(scriptPubKey, value);
    CMutableTransaction spend = BuildSpend(CScript(), credit);

    const SigHashType sigHashType = SigHashType().withForkId();
    const uint256 sighash = SignatureHash(scriptPubKey, CTransaction(spend), 0,
                                          sigHashType, value, nullptr, true);
    std::vector<uint8_t> vchSig;
    BOOST_REQUIRE(key.Sign(sighash, vchSig));
    vchSig.push_back(static_cast<uint8_t>(sigHashType.getRawSigHashType()));

    spend.vin[0].scriptSig = CScript() << vchSig;
    const std::vector<uint8_t> txBytes = Serialize(spend);

    bitcoinconsensus_error err = bitcoinconsensus_ERR_INVALID_FLAGS;
    const int ok = bitcoinconsensus_verify_script_with_amount(
        config, scriptPubKey.data(), scriptPubKey.size(), value.GetSatoshis(),
        txBytes.data(), txBytes.size(), 0,
        bitcoinconsensus_SCRIPT_ENABLE_SIGHASH_FORKID, &err);
    BOOST_CHECK_EQUAL(err, bitcoinconsensus_ERR_OK);
    BOOST_CHECK_EQUAL(ok, 1);

    // The no-amount entry point cannot honour FORKID (it needs the amount for the
    // BIP143 sighash) and must say so rather than silently mis-verify.
    bitcoinconsensus_error noAmountErr = bitcoinconsensus_ERR_OK;
    const int noAmountOk = bitcoinconsensus_verify_script(
        config, scriptPubKey.data(), scriptPubKey.size(),
        txBytes.data(), txBytes.size(), 0,
        bitcoinconsensus_SCRIPT_ENABLE_SIGHASH_FORKID, &noAmountErr);
    BOOST_CHECK_EQUAL(noAmountErr, bitcoinconsensus_ERR_AMOUNT_REQUIRED);
    BOOST_CHECK_EQUAL(noAmountOk, 0);
}

// UTXO_AFTER_CHRONICLE without UTXO_AFTER_GENESIS is an invalid combination
// (mirrors VerifyScript's valid_flags precondition) and must be reported as
// ERR_INVALID_FLAGS, not produce a bare verdict.
BOOST_AUTO_TEST_CASE(invalid_era_combination)
{
    const Config& config = GlobalConfig::GetConfig();

    const CScript scriptPubKey = CScript() << OP_1;
    const CMutableTransaction credit = BuildCredit(scriptPubKey, Amount(0));
    const CMutableTransaction spend = BuildSpend(CScript(), credit);
    const std::vector<uint8_t> txBytes = Serialize(spend);

    // Invalid flags are signalled through the return value
    bitcoinconsensus_error err = bitcoinconsensus_ERR_OK;
    const int rc = bitcoinconsensus_verify_script_with_amount(
        config, scriptPubKey.data(), scriptPubKey.size(), 0,
        txBytes.data(), txBytes.size(), 0,
        bitcoinconsensus_SCRIPT_UTXO_AFTER_CHRONICLE, &err);
    BOOST_CHECK_EQUAL(rc, bitcoinconsensus_ERR_INVALID_FLAGS);

    // The valid pairing is accepted.
    bitcoinconsensus_error err2 = bitcoinconsensus_ERR_INVALID_FLAGS;
    const int ok2 = bitcoinconsensus_verify_script_with_amount(
        config, scriptPubKey.data(), scriptPubKey.size(), 0,
        txBytes.data(), txBytes.size(), 0,
        bitcoinconsensus_SCRIPT_UTXO_AFTER_GENESIS |
            bitcoinconsensus_SCRIPT_UTXO_AFTER_CHRONICLE, &err2);
    BOOST_CHECK_EQUAL(err2, bitcoinconsensus_ERR_OK);
    BOOST_CHECK_EQUAL(ok2, 1);
}

BOOST_AUTO_TEST_SUITE_END()

#endif // HAVE_CONSENSUS_LIB
