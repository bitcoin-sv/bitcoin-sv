// Copyright (c) 2019 The Bitcoin SV developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "config.h"
#include "key.h"
#include "pubkey.h"
#include "test/test_bitcoin.h"
#include "txmempool.h"
#include "txn_validator.h"

#include <boost/test/unit_test.hpp>

namespace {
    // Create scriptPubKey from a given key
    CScript GetScriptPubKey(CKey& key) {
        CScript scriptPubKey = CScript() << ToByteVector(key.GetPubKey())
                                         << OP_CHECKSIG;
        return scriptPubKey;
    }
    // Create a spend txn
    CMutableTransaction CreateSpendTxn(CTransaction& foundTxn,
                                       CKey& key,
                                       CScript& scriptPubKey) {
        CMutableTransaction spend_txn;
        spend_txn.nVersion = 1;
        spend_txn.vin.resize(1);
        spend_txn.vin[0].prevout = COutPoint(foundTxn.GetId(), 0);
        spend_txn.vout.resize(1);
        spend_txn.vout[0].nValue = 11 * CENT;
        spend_txn.vout[0].scriptPubKey = scriptPubKey;
        // Sign:
        std::vector<uint8_t> vchSig {};
        uint256 hash = SignatureHash(scriptPubKey, CTransaction(spend_txn), 0,
                                     SigHashType().withForkId(),
                                     foundTxn.vout[0].nValue);
        BOOST_CHECK(key.Sign(hash, vchSig));
        vchSig.push_back(uint8_t(SIGHASH_ALL | SIGHASH_FORKID));
        spend_txn.vin[0].scriptSig << vchSig;
        return spend_txn;
    }
    // Create N spend txns from a given found txn
    std::vector<CMutableTransaction> CreateNSpendTxns(size_t nSpendTxns,
                                                      CTransaction& foundTxn,
                                                      CKey& key,
                                                      CScript& scriptPubKey) {
        // Create txns spending the same coinbase txn.
        std::vector<CMutableTransaction> spends {};
        for (size_t i=0; i<nSpendTxns; i++) {
            spends.emplace_back(CreateSpendTxn(foundTxn, key, scriptPubKey));
        };
        return spends;
    }
    // Create txInputData for a given txn and source
    TxInputDataSPtr TxInputData(TxSource source,
                                CMutableTransaction& spend,
                                std::shared_ptr<CNode> pNode = nullptr) {
        // Return txn's input data
        return std::make_shared<CTxInputData>(
                                   source,   // tx source
                                   MakeTransactionRef(spend),// a pointer to the tx
                                   GetTime(),// nAcceptTime
                                   false,    // mfLimitFree
                                   Amount(0),// nAbsurdFee
                                   pNode);   // pNode
    }
    // Validate txn using synchronous validation interface
    void ProcessTxnsSynchApi(std::vector<CMutableTransaction>& spends,
                                      TxSource source,
                                      std::shared_ptr<CNode> pNode = nullptr) {
        // Create txn validator
        std::shared_ptr<CTxnValidator> txnValidator {
            std::make_shared<CTxnValidator>(
                    GlobalConfig::GetConfig(),
                    mempool,
                    std::make_shared<CTxnDoubleSpendDetector>())
        };
        // Clear mempool before validation
        mempool.clear();
        // Mempool Journal ChangeSet
        mining::CJournalChangeSetPtr changeSet {nullptr};
        // Validate the first txn
        CValidationState result {
            txnValidator->processValidation(TxInputData(source, spends[0], pNode), changeSet)
        };
        BOOST_CHECK(result.IsValid());
        // Validate the second txn
        // spends1 should be rejected if spend0 is in the mempool
        result = txnValidator->processValidation(TxInputData(source, spends[1], pNode), changeSet);
        BOOST_CHECK(!result.IsValid());
    }
    struct TestChain100Setup2 : TestChain100Setup {
        CScript scriptPubKey {
            GetScriptPubKey(coinbaseKey)
        };
        // spends contains two txns spending the same coinbase txn
        std::vector<CMutableTransaction> spends2 {
            CreateSpendTxn(coinbaseTxns[0], coinbaseKey, scriptPubKey),
            CreateSpendTxn(coinbaseTxns[0], coinbaseKey, scriptPubKey)
        };
        // spends contains N txns spending the same coinbase txn
        std::vector<CMutableTransaction> spendsN {
            CreateNSpendTxns(10, coinbaseTxns[0], coinbaseKey, scriptPubKey)
        };    
    };
}

BOOST_FIXTURE_TEST_SUITE(test_txnvalidator, TestChain100Setup2)

BOOST_AUTO_TEST_CASE(txn_validator_creation) {
	// Create txn validator
    std::shared_ptr<CTxnValidator> txnValidator {
        std::make_shared<CTxnValidator>(
                GlobalConfig::GetConfig(),
                mempool,
                std::make_shared<CTxnDoubleSpendDetector>())
    };
	BOOST_REQUIRE(txnValidator);
}

/**
 * TxnValidator: Test synch interface.
 */
BOOST_AUTO_TEST_CASE(txnvalidator_doublespend_synch_api) {
    // Test: Txns from wallet.
    ProcessTxnsSynchApi(spends2, TxSource::wallet);
    BOOST_CHECK_EQUAL(mempool.size(), 1);
    // Test: Txns from rpc.
    ProcessTxnsSynchApi(spends2, TxSource::rpc);
    BOOST_CHECK_EQUAL(mempool.size(), 1);
    // Test: Txns from file.
    ProcessTxnsSynchApi(spends2, TxSource::file);
    BOOST_CHECK_EQUAL(mempool.size(), 1);
    // Test: Txns from reorg.
    ProcessTxnsSynchApi(spends2, TxSource::reorg);
    BOOST_CHECK_EQUAL(mempool.size(), 1);
    // Process txn if it is valid.
    ProcessTxnsSynchApi(spends2, TxSource::unknown);
    BOOST_CHECK_EQUAL(mempool.size(), 1);
}

BOOST_AUTO_TEST_SUITE_END()
