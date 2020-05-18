// Copyright (c) 2019 The Bitcoin SV developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "config.h"
#include "net_processing.h"
#include "orphan_txns.h"
#include "test/test_bitcoin.h"

#include <random>
#include <boost/test/unit_test.hpp>

namespace {
    CService ip(uint32_t i) {
        struct in_addr s;
        s.s_addr = i;
        return CService(CNetAddr(s), Params().GetDefaultPort());
    }
    // Use a default configuration
    size_t maxCollectedOutpoints = COrphanTxns::DEFAULT_MAX_COLLECTED_OUTPOINTS;
    size_t maxExtraTxnsForCompactBlock = COrphanTxns::DEFAULT_BLOCK_RECONSTRUCTION_EXTRA_TXN;
    size_t maxTxSizePolicy = DEFAULT_MAX_TX_SIZE_POLICY_AFTER_GENESIS;
    // Create txn inputs
    std::vector<CTxIn> CreateTxnInputs(size_t nNumOfInputs, uint256 txid=InsecureRand256()) {
        std::vector<CTxIn> vin;
        vin.resize(nNumOfInputs);
        for (size_t idx=0; idx<nNumOfInputs; ++idx) {
            vin[idx].prevout = COutPoint(txid, idx);
            vin[idx].scriptSig << OP_1;
        }
        return vin;
    }
    // Create txn input from a given outpoints.
    std::vector<CTxIn> CreateTxnInputs(std::vector<COutPoint> vOutpoints) {
        std::vector<CTxIn> vin;
        vin.resize(vOutpoints.size());
        for (size_t idx=0; idx<vOutpoints.size(); ++idx) {
            vin[idx].prevout = vOutpoints[idx];
            vin[idx].scriptSig << OP_1;
        }
        return vin;
    }
    // Create txn outputs
    std::vector<CTxOut> CreateTxnOutputs(size_t nNumOfOutputs) {
        // Creat private keys.
        std::vector<CKey> vKey(nNumOfOutputs);
        for (auto& key: vKey) {
            key.MakeNewKey(true);
        }
        // Create outpoints
        std::vector<CTxOut> vout;
        vout.resize(nNumOfOutputs);
        for (size_t idx=0; idx<nNumOfOutputs; ++idx) {
            // A fixed value
            vout[idx].nValue = 1 * CENT;
            vout[idx].scriptPubKey = GetScriptForDestination(vKey[idx].GetPubKey().GetID());
        }
        return vout;
    }

    // Create an orphan txn
    TxInputDataSPtr CreateOrphanTxn(TxSource source,
                                    std::vector<CTxIn> vTxnInputs = CreateTxnInputs(1),
                                    std::vector<CTxOut> vTxnOutputs = CreateTxnOutputs(1),
                                    std::shared_ptr<CNode> pNode=nullptr) {
        CMutableTransaction tx;
        tx.vin = vTxnInputs;
        tx.vout = vTxnOutputs;
        // Return txn's input data
        return std::make_shared<CTxInputData>(
                                   source,   // tx source
                                   TxValidationPriority::normal, // tx validation priority
                                   MakeTransactionRef(tx),// a pointer to the tx
                                   0,        // nAcceptTime
                                   false,    // mfLimitFree
                                   Amount(0),// nAbsurdFee
                                   pNode);   // pNode
    }
    // Populate orphan txn's object with a given number of txns.
    void OrphanTxnsObjectCreateNOrphanTxns(
        std::shared_ptr<COrphanTxns>& orphanTxns,
        TxSource source,
        int32_t nOrphanTxnsCount,
        CConnman::CAsyncTaskPool& asyncTaskPool,
        std::vector<CNodePtr>& nodes)
    {
        nodes.clear();
        for (NodeId i = 0; i < nOrphanTxnsCount; i++) {
            CAddress dummy_addr(ip(0xa0b0c001), NODE_NONE);
            CNodePtr pNode =
                CNode::Make(
                    i,
                    NODE_NETWORK,
                    0,
                    INVALID_SOCKET,
                    dummy_addr,
                    0u,
                    0u,
                    asyncTaskPool,
                    "",
                    true);
            nodes.push_back(pNode);
            // Create txn and add it to the queue
            orphanTxns->addTxn(CreateOrphanTxn(source, CreateTxnInputs(1), CreateTxnOutputs(1), pNode));
        }
    }
}

BOOST_FIXTURE_TEST_SUITE(test_orphantxns, TestingSetup)

BOOST_AUTO_TEST_CASE(test_orphantxns_creation) {
    // Create orphan txn's object.
    std::shared_ptr<COrphanTxns> orphanTxns {
        std::make_shared<COrphanTxns>(
                maxCollectedOutpoints,
                maxExtraTxnsForCompactBlock,
                maxTxSizePolicy)
    };
    BOOST_REQUIRE(orphanTxns);
}

BOOST_AUTO_TEST_CASE(test_orphantxns_addtxn_erasetxns) {
    CConnman::CAsyncTaskPool asyncTaskPool{GlobalConfig::GetConfig()};
    // Create orphan txn's object.
    std::shared_ptr<COrphanTxns> orphanTxns {
        std::make_shared<COrphanTxns>(
                maxCollectedOutpoints,
                maxExtraTxnsForCompactBlock,
                maxTxSizePolicy)
    };
    size_t nTxnsNumber=10;
    // Create orphan transactions:
    std::vector<CNodePtr> nodes {};
    OrphanTxnsObjectCreateNOrphanTxns(orphanTxns, TxSource::p2p, nTxnsNumber, asyncTaskPool, nodes);
    // Check txns count
    BOOST_CHECK(orphanTxns->getTxnsNumber() == nTxnsNumber);
    // Erase all txns
    orphanTxns->eraseTxns();
    BOOST_CHECK(orphanTxns->getTxnsNumber() == 0);
}

BOOST_AUTO_TEST_CASE(test_orphantxns_limit_txns_size) {
    CConnman::CAsyncTaskPool asyncTaskPool{GlobalConfig::GetConfig()};
    // Create orphan txn's object.
    std::shared_ptr<COrphanTxns> orphanTxns {
        std::make_shared<COrphanTxns>(
                maxCollectedOutpoints,
                maxExtraTxnsForCompactBlock,
                maxTxSizePolicy)
    };
    size_t nTxnsNumber=1000;
    CAddress dummy_addr(ip(0xa0b0c001), NODE_NONE);
    // Create orphan transactions:
    std::vector<CNodePtr> nodes {};
    OrphanTxnsObjectCreateNOrphanTxns(orphanTxns, TxSource::p2p, nTxnsNumber, asyncTaskPool, nodes);
    // Check txns count
    BOOST_CHECK(orphanTxns->getTxnsNumber() == nTxnsNumber);
    // Test limit function: (each generated transaction is 86 bytes long)
    orphanTxns->limitTxnsSize(86000);
    BOOST_CHECK(orphanTxns->getTxnsNumber() == 1000);
    orphanTxns->limitTxnsSize(860);
    BOOST_CHECK(orphanTxns->getTxnsNumber() == 10);
     orphanTxns->limitTxnsSize(859);
    BOOST_CHECK(orphanTxns->getTxnsNumber() == 9);
    orphanTxns->limitTxnsSize(86);
    BOOST_CHECK(orphanTxns->getTxnsNumber() == 1);
    orphanTxns->limitTxnsSize(85);
    BOOST_CHECK(orphanTxns->getTxnsNumber() == 0);
    orphanTxns->limitTxnsSize(0);
    BOOST_CHECK(orphanTxns->getTxnsNumber() == 0);
}

BOOST_AUTO_TEST_CASE(test_orphantxns_checktxnexists) {
    CConnman::CAsyncTaskPool asyncTaskPool{GlobalConfig::GetConfig()};
    // Create orphan txn's object.
    std::shared_ptr<COrphanTxns> orphanTxns {
        std::make_shared<COrphanTxns>(
                maxCollectedOutpoints,
                maxExtraTxnsForCompactBlock,
                maxTxSizePolicy)
    };
    size_t nTxnsNumber=10;
    // Create orphan transactions:
    std::vector<CNodePtr> nodes {};
    OrphanTxnsObjectCreateNOrphanTxns(orphanTxns, TxSource::p2p, nTxnsNumber, asyncTaskPool, nodes);
    // Check txns count
    BOOST_CHECK(orphanTxns->getTxnsNumber() == nTxnsNumber);
    // Create a txns which is not present in queue
    auto txn = CreateOrphanTxn(TxSource::p2p);
    BOOST_CHECK(!orphanTxns->checkTxnExists(txn->mpTx->GetId()));
    orphanTxns->addTxn(txn);
    BOOST_CHECK(orphanTxns->checkTxnExists(txn->mpTx->GetId()));
    BOOST_CHECK(orphanTxns->getTxnsNumber() == nTxnsNumber+1);
}

BOOST_AUTO_TEST_CASE(test_orphantxns_erasetxn) {
    CConnman::CAsyncTaskPool asyncTaskPool{GlobalConfig::GetConfig()};
    // Create orphan txn's object.
    std::shared_ptr<COrphanTxns> orphanTxns {
        std::make_shared<COrphanTxns>(
                maxCollectedOutpoints,
                maxExtraTxnsForCompactBlock,
                maxTxSizePolicy)
    };
    size_t nTxnsNumber=10;
    // Create orphan transactions:
    std::vector<CNodePtr> nodes {};
    OrphanTxnsObjectCreateNOrphanTxns(orphanTxns, TxSource::p2p, nTxnsNumber, asyncTaskPool, nodes);
    // Check txns count
    BOOST_CHECK(orphanTxns->getTxnsNumber() == nTxnsNumber);
    // Create a txns which is not present in queue
    auto txn = CreateOrphanTxn(TxSource::p2p);
    orphanTxns->addTxn(txn);
    BOOST_CHECK(orphanTxns->getTxnsNumber() == nTxnsNumber+1);
    auto txnToErase = txn->mpTx->GetId();
    // Erase a given txn
    orphanTxns->eraseTxn(txnToErase);
    // Check if txn was erased
    BOOST_CHECK(!orphanTxns->checkTxnExists(txnToErase));
    // Check if a total number of txns is changed
    BOOST_CHECK(orphanTxns->getTxnsNumber() == nTxnsNumber);
}

BOOST_AUTO_TEST_CASE(test_orphantxns_erasetxnfrompeer) {
    CConnman::CAsyncTaskPool asyncTaskPool{GlobalConfig::GetConfig()};
    // Create orphan txn's object.
    std::shared_ptr<COrphanTxns> orphanTxns {
        std::make_shared<COrphanTxns>(
                maxCollectedOutpoints,
                maxExtraTxnsForCompactBlock,
                maxTxSizePolicy)
    };
    size_t nTxnsNumber=10;
    size_t nNodesNumber=10;
    // Create orphan transactions:
    std::vector<CNodePtr> nodes {};
    OrphanTxnsObjectCreateNOrphanTxns(orphanTxns, TxSource::p2p, nTxnsNumber, asyncTaskPool, nodes);
    // Check txns count
    BOOST_CHECK(orphanTxns->getTxnsNumber() == nTxnsNumber);
    // Erase txns from a node which is not connected (there are no orphan txns from this node)
    orphanTxns->eraseTxnsFromPeer((NodeId)(nNodesNumber+1));
    BOOST_CHECK(orphanTxns->getTxnsNumber() == nTxnsNumber);
    // Erase all txns from Node0
    orphanTxns->eraseTxnsFromPeer((NodeId)(0));
    BOOST_CHECK(orphanTxns->getTxnsNumber() == nTxnsNumber-1);
    // Delete txns from all other nodes
    for (NodeId nodeId=1; nodeId < (NodeId)nNodesNumber; nodeId++) {
        orphanTxns->eraseTxnsFromPeer((NodeId)nodeId);
    }
    BOOST_CHECK(orphanTxns->getTxnsNumber() == 0);
}

BOOST_AUTO_TEST_CASE(test_orphantxns_maxcollectedoutpoints) {
    size_t nMaxCollectedOutpoints = 100;
    // Create orphan txn's object.
    std::shared_ptr<COrphanTxns> orphanTxns {
        std::make_shared<COrphanTxns>(
                nMaxCollectedOutpoints,
                maxExtraTxnsForCompactBlock,
                maxTxSizePolicy)
    };
    // Create txn with a max number of outpoints the OrphanTxn can collect
    auto txn1 = CreateOrphanTxn(
                    TxSource::p2p,
                    CreateTxnInputs(1),
                    CreateTxnOutputs(nMaxCollectedOutpoints));
    // Create a vector with expected outpoints from txn1
    std::vector<COutPoint> vExpectedOutpoints {};
    auto txn1id = txn1->mpTx->GetId();
    for (size_t i=0; i<nMaxCollectedOutpoints; ++i) {
        vExpectedOutpoints.emplace_back(COutPoint{txn1id, (uint32_t)i});
    }
    // Collect outpoints from txn1
    orphanTxns->collectTxnOutpoints(*(txn1->mpTx));
    // Get collected outpoints
    auto vReturnedOutpoints = orphanTxns->getCollectedOutpoints();
    BOOST_CHECK(
        std::equal(vExpectedOutpoints.begin(),
                   vExpectedOutpoints.end(),
                   vReturnedOutpoints.begin()));
    BOOST_CHECK(vReturnedOutpoints.size() == nMaxCollectedOutpoints);

    // Collect outpoints from a txn which creates nTxnNumOfOutpoints number of outpoints,
    // where nTxnNumOfOutpoints is a random number from the range [1, nMaxCollectedOutpoints+1].
    // The following loop helps to check if outpoints are collected properly.
    std::mt19937 random_engine;
    std::uniform_int_distribution<int> distribution(1, nMaxCollectedOutpoints+1);
    for (int j=0; j<100; ++j) {
        size_t nTxnNumOfOutpoints = distribution(random_engine);
        auto txn = CreateOrphanTxn(
                       TxSource::p2p,
                       CreateTxnInputs(1),
                       CreateTxnOutputs(nTxnNumOfOutpoints));
        orphanTxns->collectTxnOutpoints(*(txn->mpTx));
        // Check if rotate can be applied to remove the oldest outpoints
        if (nTxnNumOfOutpoints < nMaxCollectedOutpoints) {
                std::rotate(
                    vExpectedOutpoints.begin(),
                    vExpectedOutpoints.begin() + nTxnNumOfOutpoints,
                    vExpectedOutpoints.end());
            vExpectedOutpoints.resize(vExpectedOutpoints.size() - nTxnNumOfOutpoints);
        } else {
            vExpectedOutpoints.clear();
        }
        auto txnid = txn->mpTx->GetId();
        for (size_t i=0; i<nTxnNumOfOutpoints; ++i) {
            vExpectedOutpoints.emplace_back(COutPoint{txnid, (uint32_t)i});
        }
        auto vReturnedOutpoints2 = orphanTxns->getCollectedOutpoints();
        BOOST_CHECK(
            std::equal(
                vExpectedOutpoints.begin(),
                vExpectedOutpoints.end(),
                vReturnedOutpoints2.begin()));
    }
}

BOOST_AUTO_TEST_CASE(test_orphantxns_erasecollectedoutpointsfromtxns) {
    size_t nMaxCollectedOutpoints = 100;
    size_t nTxn1NumOfOutpoints = 10;
    size_t nTxn2NumOfOutpoints = 2;
    size_t nTxn3NumOfOutpoints = 3;
    // Create orphan txn's object.
    std::shared_ptr<COrphanTxns> orphanTxns {
        std::make_shared<COrphanTxns>(
                nMaxCollectedOutpoints,
                maxExtraTxnsForCompactBlock,
                maxTxSizePolicy)
    };
    // Create txn1
    auto txn1 = CreateOrphanTxn(
                    TxSource::p2p,
                    CreateTxnInputs(1),
                    CreateTxnOutputs(nTxn1NumOfOutpoints));
    auto txn1id = txn1->mpTx->GetId();
    // Create txn2
    auto txn2 = CreateOrphanTxn(
                    TxSource::p2p,
                    CreateTxnInputs(1),
                    CreateTxnOutputs(nTxn2NumOfOutpoints));
    auto txn2id = txn2->mpTx->GetId();
    // Create txn3
    auto txn3 = CreateOrphanTxn(
                    TxSource::p2p,
                    CreateTxnInputs(1),
                    CreateTxnOutputs(nTxn3NumOfOutpoints));
    auto txn3id = txn3->mpTx->GetId();
    // Create a vector with expected outpoints from txn1
    std::vector<COutPoint> vTxn1ExpectedOutpoints {};
    for (size_t i=0; i<nTxn1NumOfOutpoints; ++i) {
        vTxn1ExpectedOutpoints.emplace_back(COutPoint{txn1id, (uint32_t)i});
    }
    // Create a vector with expected outpoints from txn3
    std::vector<COutPoint> vTxn3ExpectedOutpoints {};
    for (size_t i=0; i<nTxn3NumOfOutpoints; ++i) {
        vTxn3ExpectedOutpoints.emplace_back(COutPoint{txn3id, (uint32_t)i});
    }
    // Collect outpoints from txn1. Then, remove all outpoints from txn1
    {
        orphanTxns->collectTxnOutpoints(*(txn1->mpTx));
        orphanTxns->eraseCollectedOutpointsFromTxns(std::vector<TxId>{txn1id});
        auto vReturnedOutpoints = orphanTxns->getCollectedOutpoints();
        BOOST_CHECK(vReturnedOutpoints.empty());
    }
    // Collect outpoints from txn1 & txn2. Then, remove outpoints from txn2
    {
        orphanTxns->collectTxnOutpoints(*(txn1->mpTx));
        orphanTxns->collectTxnOutpoints(*(txn2->mpTx));
        orphanTxns->eraseCollectedOutpointsFromTxns(std::vector<TxId>{txn2id});
        auto vReturnedOutpoints = orphanTxns->getCollectedOutpoints();
        BOOST_CHECK(vReturnedOutpoints.size() == nTxn1NumOfOutpoints);
        BOOST_CHECK(
            std::equal(
                vTxn1ExpectedOutpoints.begin(),
                vTxn1ExpectedOutpoints.end(),
                vReturnedOutpoints.begin()));
    }
    // Erase previously collected outpoints
    orphanTxns->eraseCollectedOutpoints();
    // Collect outpoints from txn1, txn2 & txn3. Then, remove outpoints from txn2
    {
        orphanTxns->collectTxnOutpoints(*(txn1->mpTx));
        orphanTxns->collectTxnOutpoints(*(txn2->mpTx));
        orphanTxns->collectTxnOutpoints(*(txn3->mpTx));
        orphanTxns->eraseCollectedOutpointsFromTxns(std::vector<TxId>{txn2id});
        auto vReturnedOutpoints = orphanTxns->getCollectedOutpoints();
        BOOST_CHECK(vReturnedOutpoints.size() == nTxn1NumOfOutpoints + nTxn3NumOfOutpoints);
        // Get outpoints from txn1 & txn3
        auto vTxn1AndTxn3ExpectedOutpoints = vTxn1ExpectedOutpoints;
        vTxn1AndTxn3ExpectedOutpoints.insert(
                vTxn1AndTxn3ExpectedOutpoints.end(),
                vTxn3ExpectedOutpoints.begin(),
                vTxn3ExpectedOutpoints.end());
        BOOST_CHECK(
            std::equal(
                vTxn1AndTxn3ExpectedOutpoints.begin(),
                vTxn1AndTxn3ExpectedOutpoints.end(),
                vReturnedOutpoints.begin()));
    }
}

BOOST_AUTO_TEST_CASE(test_orphantxns_collectdependenttxnsforretry) {
    // Create orphan txn's object.
    std::shared_ptr<COrphanTxns> orphanTxns {
        std::make_shared<COrphanTxns>(
                maxCollectedOutpoints,
                maxExtraTxnsForCompactBlock,
                maxTxSizePolicy)
    };
    size_t nTxnsNumber=5;
    // Create orphan transactions:
    auto txn1 = CreateOrphanTxn(TxSource::p2p);
    orphanTxns->addTxn(txn1);
    auto txn2 = CreateOrphanTxn(TxSource::p2p);
    orphanTxns->addTxn(txn2);
    auto txn3 = CreateOrphanTxn(TxSource::p2p);
    orphanTxns->addTxn(txn3);
    auto txn4 = CreateOrphanTxn(TxSource::p2p);
    orphanTxns->addTxn(txn4);
    auto txn5 = CreateOrphanTxn(TxSource::p2p);
    orphanTxns->addTxn(txn5);
    BOOST_CHECK(orphanTxns->getTxnsNumber() == nTxnsNumber);
    // Create dependent orphan txns:
    // dependent_txn1 takes txn1 as an input.
    auto dependent_txn1 {
        CreateOrphanTxn(TxSource::p2p, CreateTxnInputs({COutPoint(txn1->mpTx->GetId(), 0)}))
    };
    orphanTxns->addTxn(dependent_txn1);
    BOOST_CHECK(orphanTxns->getTxnsNumber() == nTxnsNumber+1);
    // Test case 1: Collect dependent txns for retry
    orphanTxns->collectTxnOutpoints(*(txn1->mpTx));
    auto vOrphanTxnsForRetry = orphanTxns->collectDependentTxnsForRetry();
    // Check if txn1 was taken from the orphan set.
    size_t nDependentTxn1VoutSize = dependent_txn1->mpTx->vout.size();
    BOOST_CHECK(vOrphanTxnsForRetry.size() == nDependentTxn1VoutSize);
    auto dependent_txn1_id = dependent_txn1->mpTx->GetId();
    auto fdependenttxn1 {
        std::find_if(vOrphanTxnsForRetry.begin(),
                     vOrphanTxnsForRetry.end(),
                     [&dependent_txn1_id](const TxInputDataSPtr& txn) {
                            return dependent_txn1_id == txn->mpTx->GetId();
                        })
    };
    BOOST_CHECK(fdependenttxn1 != vOrphanTxnsForRetry.end());
    // Test case 2:
    // Add one new not-dependent orphan txn.
    auto txn6 = CreateOrphanTxn(TxSource::p2p);
    orphanTxns->addTxn(txn6);
    BOOST_CHECK(orphanTxns->getTxnsNumber() == nTxnsNumber+2);
    orphanTxns->collectTxnOutpoints(*(txn6->mpTx));
    BOOST_CHECK(orphanTxns->collectDependentTxnsForRetry().empty());
    // Test case 3:
    // There is no newly added dependent orphan txn. There should be nothing for re-try.
    auto txn7 = CreateOrphanTxn(TxSource::p2p);
    orphanTxns->collectTxnOutpoints(*(txn7->mpTx));
    BOOST_CHECK(orphanTxns->collectDependentTxnsForRetry().empty());
}

// In this test we have got txn1 which creates two outputs and then two child transactions txn2 and txn3
// each of them spending one of the outputs of txn1.
// A single collectDependentTxnsForRetry invocation should return one related orphan at a time.
BOOST_AUTO_TEST_CASE(test_orphantxns_collectdependenttxnsforretry2) {
    // Create orphan txn's object.
    std::shared_ptr<COrphanTxns> orphanTxns {
        std::make_shared<COrphanTxns>(
                maxCollectedOutpoints,
                maxExtraTxnsForCompactBlock,
                maxTxSizePolicy)
    };
    // Create orphan transaction of type 1-2 (one input - two outputs):
    auto txn1 = CreateOrphanTxn(TxSource::p2p, CreateTxnInputs(1), CreateTxnOutputs(2));
    // txn2 takes the first output from txn1 
    auto txn2 = CreateOrphanTxn(TxSource::p2p, CreateTxnInputs({COutPoint(txn1->mpTx->GetId(), 0)}));
    // txn3 takes the second output from txn1 
    auto txn3 = CreateOrphanTxn(TxSource::p2p, CreateTxnInputs({COutPoint(txn1->mpTx->GetId(), 1)}));
    // Add txn2 to orphans
    orphanTxns->addTxn(txn2);
    // Add txn3 to orphans
    orphanTxns->addTxn(txn3);
    // We presume that txn1 is submitted to the mempool so collect it's outpoints
    orphanTxns->collectTxnOutpoints(*(txn1->mpTx));
    // For the same parent we can not simply return both orphans.
    // It is due to txn descendant count & size calculations.
    //
    // Test case1:
    // Call collectDependentTxnsForRetry to get the first related orphan txn 
    {
        auto vRetryTxns = orphanTxns->collectDependentTxnsForRetry();
        BOOST_CHECK(vRetryTxns.size() == 1);
        BOOST_CHECK(*(vRetryTxns[0]->mpTx) == *(txn2->mpTx));
        // Remove txn2 from orphans. It simulates a possible use cases:
        // - txn2 was accepted to the mempool
        // - txn2 was rejected as being invalid orphan txn.
        orphanTxns->eraseTxn(txn2->mpTx->GetId());
    }
    // Test case2:
    // Call collectDependentTxnsForRetry to get the second related orphan txn 
    {
        auto vRetryTxns = orphanTxns->collectDependentTxnsForRetry();
        BOOST_CHECK(vRetryTxns.size() == 1);
        BOOST_CHECK(*(vRetryTxns[0]->mpTx) == *(txn3->mpTx));
        // Remove txn3 from orphans.
        orphanTxns->eraseTxn(txn3->mpTx->GetId());
    }

    // At this stage there is no orphans and collected outpoints in the queue.
    BOOST_CHECK(orphanTxns->collectDependentTxnsForRetry().empty());
    BOOST_CHECK(!orphanTxns->getTxnsNumber());

    // Test case3:
    // The collect outpoint from txn1 persists in the queue after the call collectDependentTxnsForRetry.
    // The outpoints are randomly shuffled every invocation.
    {
        orphanTxns->addTxn(txn2);
        orphanTxns->addTxn(txn3);
        orphanTxns->collectTxnOutpoints(*(txn1->mpTx));
        {
            auto vRetryTxns = orphanTxns->collectDependentTxnsForRetry();
            BOOST_CHECK(vRetryTxns.size() == 1);
            BOOST_CHECK(*(vRetryTxns[0]->mpTx) == *(txn2->mpTx));
        }
        {
            auto vRetryTxns = orphanTxns->collectDependentTxnsForRetry();
            BOOST_CHECK(vRetryTxns.size() == 1);
            BOOST_CHECK(*(vRetryTxns[0]->mpTx) == *(txn2->mpTx) || *(vRetryTxns[0]->mpTx) == *(txn3->mpTx));
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()
