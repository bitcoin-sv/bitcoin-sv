// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#include "config.h"
#include "consensus/validation.h"
#include "frozentxo.h"
#include "key.h"
#include "keystore.h"
#include "mining/assembler.h"
#include "pubkey.h"
#include "random.h"
#include "script/scriptcache.h"
#include "script/sighashtype.h"
#include "script/sign.h"
#include "script/standard.h"
#include "test/sigutil.h"
#include "test/test_bitcoin.h"
#include "txmempool.h"
#include "txn_validator.h"
#include "utiltime.h"
#include "validation.h"

#include <boost/test/unit_test.hpp>

namespace {
    struct TestChain100Setup2 : TestChain100Setup {
        /**
         * Check if txn is valid and accepted by the mempool.
         *
         */
        // TxnValidator
        bool ToMemPool(CMutableTransaction &tx) {
            // Mock rpc txn
            auto pTxInputData {
                std::make_shared<CTxInputData>(
                    pTxIdTracker,             // a pointer to the TxIdTracker
                    MakeTransactionRef(tx),   // a pointer to the tx
                    TxSource::rpc,            // tx source
                    TxValidationPriority::normal,   // tx validation priority
                    TxStorage::memory,        // tx storage
                    GetTime())                // nAcceptTime
            };
            // Mempool Journal ChangeSet
            mining::CJournalChangeSetPtr changeSet {nullptr};
            // Execute validation via synchronous interface
            const auto& status {
                txnValidator->processValidation(pTxInputData, changeSet)
            };
            return status.IsValid();
        }
        // Get a pointer to the TxIdTracker.
        TxIdTrackerSPtr pTxIdTracker = connman->GetTxIdTracker();
        // A default double spend detector
        TxnDoubleSpendDetectorSPtr dsDetector {
            std::make_shared<CTxnDoubleSpendDetector>()
        };
        // Create txn validator
        std::shared_ptr<CTxnValidator> txnValidator {
            std::make_shared<CTxnValidator>(
                    GlobalConfig::GetConfig(),
                    mempool,
                    dsDetector,
                    pTxIdTracker)
        };
    };

    // Run CheckInputs (using pcoinsTip) on the given transaction, for all script
    // flags. Test that CheckInputs passes for all flags that don't overlap with the
    // failing_flags argument, but otherwise fails.
    // CHECKLOCKTIMEVERIFY and CHECKSEQUENCEVERIFY (and future NOP codes that may
    // get reassigned) have an interaction with DISCOURAGE_UPGRADABLE_NOPS: if the
    // script flags used contain DISCOURAGE_UPGRADABLE_NOPS but don't contain
    // CHECKLOCKTIMEVERIFY (or CHECKSEQUENCEVERIFY), but the script does contain
    // OP_CHECKLOCKTIMEVERIFY (or OP_CHECKSEQUENCEVERIFY), then script execution
    // should fail.
    // Capture this interaction with the upgraded_nop argument: set it when
    // evaluating any script flag that is implemented as an upgraded NOP code.
    void ValidateCheckInputsForAllFlags(const CMutableTransaction &mutableTx,
                                        std::function<bool(uint32_t)> expectedResultBasedOnFlags,
                                        bool add_to_cache,
                                        bool upgraded_nop,
                                        CFrozenTXOCheck& frozenTXOCheckTransaction,
                                        const CCoinsViewCache& tipView)
    {
        //DummyConfig config(CBaseChainParams::MAIN);
        auto& config = GlobalConfig::GetModifiableGlobalConfig();
        auto genesisActivationHeight = config.GetGenesisActivationHeight();
        const CTransaction tx(mutableTx);
        PrecomputedTransactionData txdata(tx);
        auto source = task::CCancellationSource::Make();
        // If we add many more flags, this loop can get too expensive, but we can
        // rewrite in the future to randomly pick a set of flags to evaluate.
        for (size_t test_flags = 0; test_flags < SCRIPT_FLAG_LAST; test_flags += 1) {

            // skipping impossible combination
            if ((test_flags & SCRIPT_UTXO_AFTER_GENESIS) && !(test_flags & SCRIPT_GENESIS)){
                continue; 
            }

            // If all mandatory flags are not set no point to test.
            if((test_flags & MANDATORY_SCRIPT_VERIFY_FLAGS) != MANDATORY_SCRIPT_VERIFY_FLAGS) {
                continue;
            }

            if (test_flags & SCRIPT_UTXO_AFTER_GENESIS) {
                config.SetGenesisActivationHeight(1); // put genesis activation low to be sure that every utxo is before genesis
            } else {
                config.SetGenesisActivationHeight(chainActive.Height() + 2); // put genesis activation one block above mempool height
            }

            CValidationState state;

            bool ret =
                CheckInputs(
                    source->GetToken(),
                    config,
                    true,
                    tx,
                    state,
                    tipView,
                    true,
                    test_flags,
                    true,
                    add_to_cache,
                    txdata,
                    frozenTXOCheckTransaction,
                    nullptr).value();

            // find out if we should pass or fail based on flags.
            bool expected_return_value = expectedResultBasedOnFlags(test_flags);
            if (expected_return_value && upgraded_nop) {
                // If the script flag being tested corresponds to an upgraded NOP,
                // then script execution should fail if DISCOURAGE_UPGRADABLE_NOPS
                // is set.
                expected_return_value =
                    !(test_flags & SCRIPT_VERIFY_DISCOURAGE_UPGRADABLE_NOPS);
            }

            BOOST_CHECK_EQUAL(ret, expected_return_value);

            // Test the caching
            if (ret && add_to_cache) {
                // Check that we get a cache hit if the tx was valid
                std::vector<CScriptCheck> scriptchecks;
                BOOST_CHECK(
                    CheckInputs(
                        source->GetToken(),
                        config,
                        true,
                        tx,
                        state,
                        tipView,
                        true,
                        test_flags,
                        true,
                        add_to_cache,
                        txdata,
                        frozenTXOCheckTransaction,
                        &scriptchecks).value());
                BOOST_CHECK(scriptchecks.empty());
            } else {
                // Check that we get script executions to check, if the transaction
                // was invalid, or we didn't add to cache.
                std::vector<CScriptCheck> scriptchecks;
                BOOST_CHECK(
                    CheckInputs(
                        source->GetToken(),
                        config,
                        true,
                        tx,
                        state,
                        tipView,
                        true,
                        test_flags,
                        true,
                        add_to_cache,
                        txdata,
                        frozenTXOCheckTransaction,
                        &scriptchecks).value());
                BOOST_CHECK_EQUAL(scriptchecks.size(), tx.vin.size());
            }
        }
        config.SetGenesisActivationHeight(genesisActivationHeight);
    }
}

BOOST_FIXTURE_TEST_SUITE(txvalidationcache_tests, TestChain100Setup2)

BOOST_AUTO_TEST_CASE(tx_mempool_block_doublespend) {
    // Make sure skipping validation of transctions that were validated going
    // into the memory pool does not allow double-spends in blocks to pass
    // validation when they should not.
    CScript scriptPubKey = CScript() << ToByteVector(coinbaseKey.GetPubKey())
                                     << OP_CHECKSIG;

    // Create a double-spend of mature coinbase txn:
    std::vector<CMutableTransaction> spends;
    spends.resize(2);
    for (int i = 0; i < 2; i++) {
        spends[i].nVersion = 1;
        spends[i].vin.resize(1);
        spends[i].vin[0].prevout = COutPoint(coinbaseTxns[0].GetId(), 0);
        spends[i].vout.resize(1);
        spends[i].vout[0].nValue = 11 * CENT;
        spends[i].vout[0].scriptPubKey = scriptPubKey;

        // Sign:
        std::vector<uint8_t> vchSig;
        uint256 hash = SignatureHash(scriptPubKey, CTransaction(spends[i]), 0,
                                     SigHashType().withForkId(),
                                     coinbaseTxns[0].vout[0].nValue);
        BOOST_CHECK(coinbaseKey.Sign(hash, vchSig));
        vchSig.push_back(uint8_t(SIGHASH_ALL | SIGHASH_FORKID));
        spends[i].vin[0].scriptSig << vchSig;
    }

    CBlock block;

    // Test 1: block with both of those transactions should be rejected.
    block = CreateAndProcessBlock(spends, scriptPubKey);
    BOOST_CHECK(chainActive.Tip()->GetBlockHash() != block.GetHash());

    // Test 2: ... and should be rejected if spend1 is in the memory pool
    BOOST_CHECK(ToMemPool(spends[0]));
    block = CreateAndProcessBlock(spends, scriptPubKey);
    BOOST_CHECK(chainActive.Tip()->GetBlockHash() != block.GetHash());
    mempool.Clear();

    // Test 3: ... and should be rejected if spend2 is in the memory pool
    BOOST_CHECK(ToMemPool(spends[1]));
    block = CreateAndProcessBlock(spends, scriptPubKey);
    BOOST_CHECK(chainActive.Tip()->GetBlockHash() != block.GetHash());
    mempool.Clear();

    // Final sanity test: first spend in mempool, second in block, that's OK:
    std::vector<CMutableTransaction> oneSpend;
    oneSpend.push_back(spends[0]);
    BOOST_CHECK(ToMemPool(spends[1]));
    block = CreateAndProcessBlock(oneSpend, scriptPubKey);
    BOOST_CHECK(chainActive.Tip()->GetBlockHash() == block.GetHash());
    // spends[1] should have been removed from the mempool when the block with
    // spends[0] is accepted:
    BOOST_CHECK_EQUAL(mempool.Size(), 0U);
}


BOOST_AUTO_TEST_CASE(checkinputs_test) {
    // Test that passing CheckInputs with one set of script flags doesn't imply
    // that we would pass again with a different set of flags.
    InitScriptExecutionCache();

    auto parentHash = InsecureRand256();
    CFrozenTXOCheck frozenTXOCheckTransaction{
        0, // NOTE: Since no TXO is frozen in this test, dummy block height can be used for checking.
        "test transaction",
        parentHash};

    CScript p2pk_scriptPubKey =
        CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;
    CScript p2sh_scriptPubKey =
        GetScriptForDestination(CScriptID(p2pk_scriptPubKey));
    CScript p2pkh_scriptPubKey =
        GetScriptForDestination(coinbaseKey.GetPubKey().GetID());

    CBasicKeyStore keystore;
    keystore.AddKey(coinbaseKey);
    keystore.AddCScript(p2pk_scriptPubKey);

    // flags to test: SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY,
    // SCRIPT_VERIFY_CHECKSEQUENCE_VERIFY, SCRIPT_VERIFY_NULLDUMMY, uncompressed
    // pubkey thing

    // Create 2 outputs that match the three scripts above, spending the first
    // coinbase tx.
    CMutableTransaction mutableSpend_tx;

    mutableSpend_tx.nVersion = 1;
    mutableSpend_tx.vin.resize(1);
    mutableSpend_tx.vin[0].prevout = COutPoint(coinbaseTxns[0].GetId(), 0);
    mutableSpend_tx.vout.resize(4);
    mutableSpend_tx.vout[0].nValue = 11 * CENT;
    mutableSpend_tx.vout[0].scriptPubKey = p2sh_scriptPubKey;
    mutableSpend_tx.vout[1].nValue = 11 * CENT;
    mutableSpend_tx.vout[1].scriptPubKey =
        CScript() << OP_CHECKLOCKTIMEVERIFY << OP_DROP
                  << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;
    mutableSpend_tx.vout[2].nValue = 11 * CENT;
    mutableSpend_tx.vout[2].scriptPubKey =
        CScript() << OP_CHECKSEQUENCEVERIFY << OP_DROP
                  << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;
    mutableSpend_tx.vout[3].nValue = 11 * CENT;
    mutableSpend_tx.vout[3].scriptPubKey = p2sh_scriptPubKey;

    // Sign, and push an extra element on the stack.
    {
        std::vector<uint8_t> vchSig;
        uint256 hash = SignatureHash(
            p2pk_scriptPubKey, CTransaction(mutableSpend_tx), 0,
            SigHashType().withForkId(), coinbaseTxns[0].vout[0].nValue);
        BOOST_CHECK(coinbaseKey.Sign(hash, vchSig));
        vchSig.push_back(uint8_t(SIGHASH_ALL | SIGHASH_FORKID));
        mutableSpend_tx.vin[0].scriptSig << OP_TRUE << vchSig;
    }

    const CTransaction spend_tx(mutableSpend_tx);

    LOCK(cs_main);
    auto& config = GlobalConfig::GetModifiableGlobalConfig();
    config.SetGenesisActivationHeight(102);


    // Test that invalidity under a set of flags doesn't preclude validity under
    // other (eg consensus) flags.
    // spend_tx is invalid according to DERSIG
    CValidationState state;
    auto source = task::CCancellationSource::Make();
    {
        PrecomputedTransactionData ptd_spend_tx(spend_tx);

        {
            CoinsDBSpan cache{*pcoinsTip};
            BOOST_CHECK(
                !CheckInputs(
                    source->GetToken(),
                    config,
                    true,
                    spend_tx,
                    state,
                    cache,
                    true,
                    MANDATORY_SCRIPT_VERIFY_FLAGS |
                        SCRIPT_VERIFY_CLEANSTACK | SCRIPT_GENESIS,
                    true,
                    true,
                    ptd_spend_tx,
                    frozenTXOCheckTransaction,
                    nullptr).value());

            // If we call again asking for scriptchecks (as happens in
            // ConnectBlock), we should add a script check object for this -- we're
            // not caching invalidity (if that changes, delete this test case).
            std::vector<CScriptCheck> scriptchecks;
            BOOST_CHECK(
                CheckInputs(
                    source->GetToken(),
                    config,
                    true,
                    spend_tx,
                    state,
                    cache,
                    true,
                    MANDATORY_SCRIPT_VERIFY_FLAGS |
                        SCRIPT_VERIFY_CLEANSTACK | SCRIPT_GENESIS,
                    true,
                    true,
                    ptd_spend_tx,
                    frozenTXOCheckTransaction,
                    &scriptchecks).value());

            BOOST_CHECK_EQUAL(scriptchecks.size(), 1U);

            // Test that CheckInputs returns true iff cleanstack-enforcing flags are
            // not present. Don't add these checks to the cache, so that we can test
            // later that block validation works fine in the absence of cached
            // successes.
            ValidateCheckInputsForAllFlags(spend_tx,
                                           [](uint32_t flags) -> bool { return !(flags & SCRIPT_VERIFY_CLEANSTACK); },
                                           false, false, frozenTXOCheckTransaction, cache);
        }
        
        // And if we produce a block with this tx, it should be valid (LOW_S not
        // enabled yet), even though there's no cache entry.
        CBlock block;

        block = CreateAndProcessBlock({spend_tx}, p2pk_scriptPubKey);
        BOOST_CHECK(chainActive.Tip()->GetBlockHash() == block.GetHash());
        BOOST_CHECK(CoinsDBView{*pcoinsTip}.GetBestBlock() == block.GetHash());
    }

    // Test P2SH: construct a transaction that is valid without P2SH, redeem script hash is correct but redeem script is invalid. 
    // Redeem script is not executed after genesis so it passes.
    {
        CMutableTransaction invalid_under_p2sh_tx;
        invalid_under_p2sh_tx.nVersion = 1;
        invalid_under_p2sh_tx.vin.resize(1);
        invalid_under_p2sh_tx.vin[0].prevout = COutPoint(spend_tx.GetId(), 0);
        invalid_under_p2sh_tx.vout.resize(1);
        invalid_under_p2sh_tx.vout[0].nValue = 11 * CENT;
        invalid_under_p2sh_tx.vout[0].scriptPubKey = p2pk_scriptPubKey;
        std::vector<uint8_t> vchSig2(p2pk_scriptPubKey.begin(),
                                     p2pk_scriptPubKey.end());
        invalid_under_p2sh_tx.vin[0].scriptSig << vchSig2;

        CoinsDBView view{ *pcoinsTip };

        ValidateCheckInputsForAllFlags(invalid_under_p2sh_tx,
                                       [](uint32_t flags) -> bool { return (flags & SCRIPT_UTXO_AFTER_GENESIS); },
                                       true, false, frozenTXOCheckTransaction, CCoinsViewCache{ view });
    }

    // Test CHECKLOCKTIMEVERIFY
    {
        CMutableTransaction invalid_with_cltv_tx;
        invalid_with_cltv_tx.nVersion = 1;
        invalid_with_cltv_tx.nLockTime = 100;
        invalid_with_cltv_tx.vin.resize(1);
        invalid_with_cltv_tx.vin[0].prevout = COutPoint(spend_tx.GetId(), 1);
        invalid_with_cltv_tx.vin[0].nSequence = 0;
        invalid_with_cltv_tx.vout.resize(1);
        invalid_with_cltv_tx.vout[0].nValue = 11 * CENT;
        invalid_with_cltv_tx.vout[0].scriptPubKey = p2pk_scriptPubKey;

        // Sign
        std::vector<uint8_t> vchSig;
        uint256 hash = SignatureHash(
            spend_tx.vout[1].scriptPubKey, CTransaction(invalid_with_cltv_tx),
            0, SigHashType().withForkId(), spend_tx.vout[1].nValue);
        BOOST_CHECK(coinbaseKey.Sign(hash, vchSig));
        vchSig.push_back(uint8_t(SIGHASH_ALL | SIGHASH_FORKID));
        invalid_with_cltv_tx.vin[0].scriptSig = CScript() << vchSig << 101;

        CoinsDBSpan cache{*pcoinsTip};

        // Since Genesis CLV operator is treated as NOP.
        ValidateCheckInputsForAllFlags(invalid_with_cltv_tx,
                                       [](uint32_t flags) -> bool { return !(flags & SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY)
                                                                            || (flags & SCRIPT_UTXO_AFTER_GENESIS); },
                                       true, true, frozenTXOCheckTransaction, cache);
        
        // Make it valid, and check again
        invalid_with_cltv_tx.vin[0].scriptSig = CScript() << vchSig << 100;
        CValidationState state;

        CTransaction transaction(invalid_with_cltv_tx);
        PrecomputedTransactionData txdata(transaction);

        BOOST_CHECK(
            CheckInputs(
                source->GetToken(),
                config,
                true,
                transaction,
                state,
                cache,
                true,
                MANDATORY_SCRIPT_VERIFY_FLAGS |
                    SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY | SCRIPT_GENESIS,
                true,
                true,
                txdata,
                frozenTXOCheckTransaction,
                nullptr).value());
    }

    // TEST CHECKSEQUENCEVERIFY
    {
        CMutableTransaction invalid_with_csv_tx;
        invalid_with_csv_tx.nVersion = 2;
        invalid_with_csv_tx.vin.resize(1);
        invalid_with_csv_tx.vin[0].prevout = COutPoint(spend_tx.GetId(), 2);
        invalid_with_csv_tx.vin[0].nSequence = 100;
        invalid_with_csv_tx.vout.resize(1);
        invalid_with_csv_tx.vout[0].nValue = 11 * CENT;
        invalid_with_csv_tx.vout[0].scriptPubKey = p2pk_scriptPubKey;

        // Sign
        std::vector<uint8_t> vchSig;
        uint256 hash = SignatureHash(
            spend_tx.vout[2].scriptPubKey, CTransaction(invalid_with_csv_tx), 0,
            SigHashType().withForkId(), spend_tx.vout[2].nValue);
        BOOST_CHECK(coinbaseKey.Sign(hash, vchSig));
        vchSig.push_back(uint8_t(SIGHASH_ALL | SIGHASH_FORKID));
        invalid_with_csv_tx.vin[0].scriptSig = CScript() << vchSig << 101;

        CoinsDBSpan cache{*pcoinsTip};

        // Since Genesis CSV operator is treated as NOP.
        ValidateCheckInputsForAllFlags(invalid_with_csv_tx,
                                       [](uint32_t flags) -> bool { return !(flags & SCRIPT_VERIFY_CHECKSEQUENCEVERIFY)
                                                                            || (flags & SCRIPT_UTXO_AFTER_GENESIS); },
                                       true, true, frozenTXOCheckTransaction, cache);
        // Make it valid, and check again
        invalid_with_csv_tx.vin[0].scriptSig = CScript() << vchSig << 100;
        CValidationState state;

        CTransaction transaction(invalid_with_csv_tx);
        PrecomputedTransactionData txdata(transaction);

        BOOST_CHECK(
            CheckInputs(
                source->GetToken(),
                config,
                true,
                transaction,
                state,
                cache,
                true,
                MANDATORY_SCRIPT_VERIFY_FLAGS |
                    SCRIPT_VERIFY_CHECKSEQUENCEVERIFY | SCRIPT_GENESIS,
                true,
                true,
                txdata,
                frozenTXOCheckTransaction,
                nullptr).value());
    }

    // TODO: add tests for remaining script flags

    {
        // Test a transaction with multiple inputs.
        CMutableTransaction tx;

        tx.nVersion = 1;
        tx.vin.resize(2);
        tx.vin[0].prevout = COutPoint(spend_tx.GetId(), 0);
        tx.vin[1].prevout = COutPoint(spend_tx.GetId(), 3);
        tx.vout.resize(1);
        tx.vout[0].nValue = 22 * CENT;
        tx.vout[0].scriptPubKey = p2pk_scriptPubKey;

        // Sign
        SignatureData sigdata;
        ProduceSignature(config, true,
            MutableTransactionSignatureCreator(&keystore, &tx, 0, 11 * CENT,
                                               SigHashType().withForkId()),
            true, false, spend_tx.vout[0].scriptPubKey, sigdata);

        UpdateTransaction(tx, 0, sigdata);
        ProduceSignature(config, true,
            MutableTransactionSignatureCreator(&keystore, &tx, 1, 11 * CENT,
                                               SigHashType().withForkId()),
            true, false, spend_tx.vout[3].scriptPubKey, sigdata);
        UpdateTransaction(tx, 1, sigdata);

        auto shouldPass = [](uint32_t flags) -> bool {
            bool isUtxoAfterGenesis = flags & SCRIPT_UTXO_AFTER_GENESIS;
            bool isCleanStackEnforced = flags & SCRIPT_VERIFY_CLEANSTACK;
            return !(isUtxoAfterGenesis && isCleanStackEnforced); 
        };

        CoinsDBSpan cache{*pcoinsTip};

        // This spends p2sh so after genesis it should fail if cleans stack rule is enforced
        ValidateCheckInputsForAllFlags(tx, shouldPass,  true, false, frozenTXOCheckTransaction, cache);

        // Check that if the second input is invalid, but the first input is
        // valid, the transaction is not cached.
        // Invalidate vin[1]
        tx.vin[1].scriptSig = CScript();

        CValidationState state;
        CTransaction transaction(tx);
        PrecomputedTransactionData txdata(transaction);

        // This transaction is now invalid because the second signature is
        // missing.
        BOOST_CHECK(
            !CheckInputs(
                source->GetToken(),
                config,
                true,
                transaction,
                state,
                cache,
                true,
                MANDATORY_SCRIPT_VERIFY_FLAGS | SCRIPT_GENESIS,
                true,
                true,
                txdata,
                frozenTXOCheckTransaction,
                nullptr).value());

        // Make sure this transaction was not cached (ie becausethe first input
        // was valid)
        std::vector<CScriptCheck> scriptchecks;
        BOOST_CHECK(
            CheckInputs(
                source->GetToken(),
                config,
                true,
                transaction,
                state,
                cache,
                true,
                MANDATORY_SCRIPT_VERIFY_FLAGS | SCRIPT_GENESIS,
                true,
                true,
                txdata,
                frozenTXOCheckTransaction,
                &scriptchecks).value());
        // Should get 2 script checks back -- caching is on a whole-transaction
        // basis.
        BOOST_CHECK_EQUAL(scriptchecks.size(), 2U);
    }
}

BOOST_AUTO_TEST_SUITE_END()
