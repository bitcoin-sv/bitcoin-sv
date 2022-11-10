// Copyright (c) 2021 The Bitcoin SV developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "net/validation_scheduler.h"
#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>
#include <random>

namespace{
    // Create a vector with input data for a given txn and source
    std::vector<TxInputDataSPtr> TxInputDataVec(TxSource source,
                                                const std::vector<CMutableTransaction>& spends,
                                                std::shared_ptr<CNode> pNode = nullptr,
                                                TxValidationPriority priority = TxValidationPriority::normal) {
        std::vector<TxInputDataSPtr> vTxInputData {};
        for (auto& elem : spends) {
            vTxInputData.
                    emplace_back(
                    std::make_shared<CTxInputData>(
                            TxIdTrackerWPtr{}, // a pointer to the TxIdTracker
                            MakeTransactionRef(elem),  // a pointer to the tx
                            source,   // tx source
                            priority, // tx validation priority
                            TxStorage::memory, // tx storage
                            GetTime(),// nAcceptTime
                            Amount(0), // nAbsurdFee
                            pNode));   // pNode
        }
        return vTxInputData;
    }

    // Create a mock transaction. 
    // As we don't really validate txs in this test most of attributes e.g. signature are not needed.
    CMutableTransaction CreateMockTx(const std::vector<COutPoint>& inputs, const int nOutputs) {
        static uint32_t dummyLockTime = 0;
        CMutableTransaction mock_txn;
        mock_txn.nVersion = 1;
        mock_txn.nLockTime = ++dummyLockTime;
        mock_txn.vin.resize(inputs.size());
        auto funds = Amount { 10 * CENT }; // just some fake amount
        for (int input = 0; input < (int)inputs.size(); ++input) {
            mock_txn.vin[input].prevout = inputs[input];
        }
        mock_txn.vout.resize(nOutputs);
        for (int output = 0; output < nOutputs; ++output) {
            mock_txn.vout[output].nValue = funds / nOutputs;
        }
        return mock_txn;
    }

    // Runs the scheduler and waits for all tasks to complete.
    void RunScheduler(const std::vector<CMutableTransaction>& txsToValidate,
                      const ValidationScheduler::TypeValidationFunc& taskFunc,
                      CThreadPool<CDualQueueAdaptor>& threadPool) {
        std::vector<TxInputDataSPtr> txInputDataVec = TxInputDataVec(TxSource::unknown,
                                                                     txsToValidate);
        auto scheduler = std::make_shared<ValidationScheduler>(threadPool, txInputDataVec, taskFunc);
        while (!scheduler->IsSpendersGraphReady()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        auto taskResults = scheduler->Schedule();
        for (auto& result : taskResults) {
            // Wait for all tasks to complete.
            result.get();
        }
    }

    bool CompareTxById(const CMutableTransaction& a, const CMutableTransaction& b) {
        return a.GetId() < b.GetId();
    }

    // Custom test criteria for executed tasks.
    // Purpose is check that executed tasks are one of the expected variants.
    // Most of the code deals with creating meaningful error message, i.e. use positions instead 
    // of meaningless TxId
    void CheckExecutionOrder(const std::vector<CMutableTransaction>& txsToValidate,
                             const std::vector<std::vector<TxId>>& executedTasks, 
                             const std::vector<std::vector<std::vector<size_t>>>& expectedVariants) {
        // Prepare mapping from tx ids to human-readable positions.
        std::unordered_map<TxId, size_t> idToPos;
        for (size_t i = 0; i < txsToValidate.size(); ++i) {
            idToPos.emplace(txsToValidate[i].GetId(), i);
        }
        std::vector<std::vector<size_t>> executedTasksPos;
        for (const auto& task : executedTasks) {
            std::vector<size_t> taskPos;
            std::transform(task.cbegin(), task.cend(), std::back_inserter(taskPos),
                           [&idToPos](const TxId& id){ return idToPos.at(id); });
            executedTasksPos.push_back(std::move(taskPos));
        }
        // BOOST_TEST_MESSAGE("Executed tasks are: " << executedTasksPos);

        // Flatten tasks into list of validated transactions.
        std::vector<TxId> executedTxs;
        for (const auto& task : executedTasks) {
            executedTxs.insert(executedTxs.end(), task.cbegin(), task.cend());
        }

        // Check that each input tx is validated exactly once.
        for (const auto& tx : txsToValidate) {
            if (std::count(executedTxs.cbegin(), executedTxs.cend(), tx.GetId()) != 1) {
                BOOST_TEST_MESSAGE("Executed tasks are: " << executedTasksPos);
                BOOST_ERROR("" << idToPos[tx.GetId()] << " is validated more than once.");
            }
        }

        // Check that transactions were validated in correct order.
        for (const auto& tx : txsToValidate) {
            const auto& txPos = std::find(executedTxs.cbegin(), executedTxs.cend(), tx.GetId());
            for (const auto& input : tx.vin) {
                const auto& inputPos = std::find(executedTxs.cbegin(), executedTxs.cend(), input.prevout.GetTxId());
                if (inputPos != executedTxs.cend()) {
                    // Input is in the batch.
                    if (std::distance(inputPos, txPos) <= 0) {
                        BOOST_TEST_MESSAGE("Executed tasks are: " << executedTasksPos);
                        BOOST_ERROR("" << idToPos[tx.GetId()] << "should be validated after " << idToPos[input.prevout.GetTxId()]);
                    }
                }
            }
        }

        // Check that tasks belong to one of expected variants.
        if (!(std::any_of(expectedVariants.cbegin(), expectedVariants.cend(),
                               [&executedTasksPos](const auto& v){ return(v == executedTasksPos); }))) {
            BOOST_TEST_MESSAGE("Executed tasks are: " << executedTasksPos);
            BOOST_ERROR("Executed tasks do not belong to any of the expected variants.");
        }

    }

    struct TestSetup : TestChain100Setup {
        // Remember executed tasks and ids of txns in the task.
        std::vector<std::vector<TxId>> executedTasks{};
        std::mutex executedTasksMtx;
        // List of txns for which exception is thrown in the task.
        std::set<TxId> failList;

        // Function executed in the tasks.
        ValidationScheduler::TypeValidationFunc taskFunc = [this](const TxInputDataSPtrRefVec& vTxInputData){
            // Remembers the txn ids in the task. So that we can check that validation was scheduled for each tx.
            std::vector<TxId> idsInTask;
            for (auto& tx : vTxInputData) {
                // Remember ids of txns in the task.
                idsInTask.push_back(tx.get()->GetTxnPtr()->GetId());
            }
            {
                std::lock_guard<std::mutex> l(executedTasksMtx);
                executedTasks.push_back(idsInTask);
            }
            
            // Prepare results. If tx is on the fail list we throw, otherwise we return dummy results.
            ValidationScheduler::TypeValidationResult results;
            for (auto& tx : vTxInputData) {
                if (failList.find(tx.get()->GetTxnPtr()->GetId()) != failList.end()) {
                    throw std::runtime_error("Testing validation throwing exception.");
                } else {
                    // Return dummy validation result.
                    results.emplace_back(CTxnValResult{}, CTask::Status::RanToCompletion);
                }
            }
            return results;
        };

        CThreadPool<CDualQueueAdaptor> threadPool { false, "TestPool", 8, 8 };

        void RunTest(const std::vector<CMutableTransaction>& txsToValidate,
                             const std::vector<std::vector<std::vector<size_t>>>& expectedVariants) {
            executedTasks.clear();

            RunScheduler(txsToValidate, taskFunc, threadPool);

            BOOST_TEST(!expectedVariants.empty());
            CheckExecutionOrder(txsToValidate, executedTasks, expectedVariants);
        }

        void RunPermutedTest(const std::vector<CMutableTransaction>& txsToValidate,
                             const std::vector<std::vector<std::vector<size_t>>>& expectedVariants) {
            std::vector<CMutableTransaction> permutedTxsToValidate{txsToValidate};
            std::sort(permutedTxsToValidate.begin(), permutedTxsToValidate.end(), CompareTxById);
            BOOST_TEST(!expectedVariants.empty());
            do {
                executedTasks.clear();

                // Run the scheduler for any permutation of input txs.
                RunScheduler(permutedTxsToValidate, taskFunc, threadPool);

                CheckExecutionOrder(txsToValidate, executedTasks, expectedVariants);
            } while(std::next_permutation(permutedTxsToValidate.begin(), permutedTxsToValidate.end(), CompareTxById));
        }
    };
}

namespace std {
    // Needed by Boost to print vector
    template<typename T>
    inline std::ostream &
    operator<<(std::ostream &wrapped, std::vector<T> const &item) {
        wrapped << '[';
        bool first = true;
        for (auto const &element : item) {
            wrapped << (!first ? "," : "") << element;
            first = false;
        }
        wrapped << ']';
        return wrapped;
    }
    // Needed by Boost to print TxId
    inline std::ostream &
    operator<<(std::ostream &wrapped, const TxId& item) {
        wrapped << item.ToString();
        return wrapped;
    }
};

BOOST_FIXTURE_TEST_SUITE(validation_scheduler_tests, TestSetup)

// Isolated transactions are scheduled in parallel. Task completion is random.
BOOST_AUTO_TEST_CASE(txs_isolated) {
    /*      0  1  2  3     */
    // Given
    auto tx0 = CreateMockTx({COutPoint{coinbaseTxns[0].GetId(), 1}}, 3);
    auto tx1 = CreateMockTx({COutPoint{coinbaseTxns[1].GetId(), 1}}, 3);
    auto tx2 = CreateMockTx({COutPoint{coinbaseTxns[2].GetId(), 1}}, 3);
    auto tx3 = CreateMockTx({COutPoint{coinbaseTxns[3].GetId(), 1}}, 3);
    std::vector<CMutableTransaction> txsToValidate{tx0, tx1, tx2, tx3};

    // When
    RunScheduler(txsToValidate, taskFunc, threadPool);

    // Then
    // Expect each transaction to be validated in one task. Order of execution can be random.
    std::set<std::vector<TxId>> expectedTasks {{tx0.GetId()}, {tx1.GetId()}, {tx2.GetId()}, {tx3.GetId()}};
    BOOST_CHECK_EQUAL(executedTasks.size(), txsToValidate.size());
    BOOST_CHECK_EQUAL(executedTasks.size(), expectedTasks.size());
    std::set<std::vector<TxId>> executedTasksSet(executedTasks.cbegin(), executedTasks.cend());
    BOOST_TEST(executedTasksSet == expectedTasks, boost::test_tools::per_element());
}

// Transactions from the same chain are scheduled in one task.
BOOST_AUTO_TEST_CASE(txs_chains) {
    /*       0   4
     *       |   |
     *       1   5
     *       |   |
     *       2   6
     *       |   |
     *       3   7
     */
    // Given
    auto tx0 = CreateMockTx({COutPoint{coinbaseTxns[0].GetId(), 0}}, 1);
    auto tx1 = CreateMockTx({COutPoint{tx0.GetId(), 0}}, 1);
    auto tx2 = CreateMockTx({COutPoint{tx1.GetId(), 0}}, 1);
    auto tx3 = CreateMockTx({COutPoint{tx2.GetId(), 0}}, 1);
    auto tx4 = CreateMockTx({COutPoint{coinbaseTxns[1].GetId(), 0}}, 1);
    auto tx5 = CreateMockTx({COutPoint{tx4.GetId(), 0}}, 1);
    auto tx6 = CreateMockTx({COutPoint{tx5.GetId(), 0}}, 1);
    auto tx7 = CreateMockTx({COutPoint{tx6.GetId(), 0}}, 1);
    std::vector<CMutableTransaction> txsToValidate{tx0, tx1, tx2, tx3, tx4, tx5, tx6, tx7};

    // Then
    // There should be 2 tasks. One for each chain.
    std::vector<size_t> c1 {0, 1, 2, 3};
    std::vector<size_t> c2 {4, 5, 6, 7};
    // Variants
    // Two chains are validated in parallel. Which tasks finishes first is impossible to predict.
    std::vector<std::vector<size_t>> v1{c1, c2};
    std::vector<std::vector<size_t>> v2{c2, c1};

    RunTest(txsToValidate, {v1, v2});
}

// Transactions from the same chain are scheduled in one task.
BOOST_AUTO_TEST_CASE(txs_twoParallelChains) {
    /*         0
     *        / \
     *       1   2
     *       |   |
     *       3   4
     *       |   |
     *       5   6
     *        \ /
     *         7
     */
    // Given
    auto tx0 = CreateMockTx({COutPoint{coinbaseTxns[0].GetId(), 1}}, 2);
    auto tx1 = CreateMockTx({COutPoint{tx0.GetId(), 0}}, 1);
    auto tx2 = CreateMockTx({COutPoint{tx0.GetId(), 1}}, 1);
    auto tx3 = CreateMockTx({COutPoint{tx1.GetId(), 0}}, 1);
    auto tx4 = CreateMockTx({COutPoint{tx2.GetId(), 1}}, 1);
    auto tx5 = CreateMockTx({COutPoint{tx3.GetId(), 0}}, 1);
    auto tx6 = CreateMockTx({COutPoint{tx4.GetId(), 1}}, 1);
    auto tx7 = CreateMockTx({COutPoint{tx5.GetId(), 0}, COutPoint{tx6.GetId(), 0}}, 1);
    std::vector<CMutableTransaction> txsToValidate{tx0, tx1, tx2, tx3, tx4, tx5, tx6, tx7};

    // Then
    // There should be 4 tasks. One for first tx, one for last tx. And two tasks for each chain.
    std::vector<size_t> t0 {0};
    std::vector<size_t> c1 {1,3,5};
    std::vector<size_t> c2 {2,4,6};
    std::vector<size_t> t7 {7};
    // Variants
    // tx0 is always validated first, tx7 is always last.
    // Two chains are validated in parallel. Which tasks finishes first is impossible to predict.
    std::vector<std::vector<size_t>> v1 {{0}, {1,3,5}, {2,4,6}, {7}}; 
    std::vector<std::vector<size_t>> v2 {{0}, {2,4,6}, {1,3,5}, {7}};

    RunTest(txsToValidate, {v1, v2});
}

BOOST_AUTO_TEST_CASE(txs_graph) {
    /*
     *          0
     *         /|
     *        / 1
     *       2  |\
     *        \ 3 |
     *         \|/
     *          4
     */
    // Given
    auto tx0 = CreateMockTx({COutPoint{coinbaseTxns[0].GetId(), 1}}, 2);
    auto tx1 = CreateMockTx({COutPoint{tx0.GetId(), 1}}, 2);
    auto tx2 = CreateMockTx({COutPoint{tx0.GetId(), 0}}, 1);
    auto tx3 = CreateMockTx({COutPoint{tx1.GetId(), 0}}, 1);
    auto tx4 = CreateMockTx({COutPoint{tx2.GetId(), 0}, COutPoint{tx3.GetId(), 0}, COutPoint{tx1.GetId(), 1}}, 1);
    std::vector<CMutableTransaction> txsToValidate{tx0, tx1, tx2, tx3, tx4};

    // Then
    // There can be 4 or 5 tasks.
    // tx0, tx1, tx2 are always scheduled in own tasks.
    // If tx2 is validated before tx1 or together with tx1, then tx3 and tx4 are validated as a chain in one task.
    // If tx2 is not yet validated when tx1 is finished, then tx3 is scheduled next. tx4 is only 
    // scheduled after tx2 and tx3 are finished.
    std::vector<std::vector<size_t>> v1 {{0}, {1}, {2}, {3}, {4}};
    std::vector<std::vector<size_t>> v2 {{0}, {2}, {1}, {3}, {4}};
    std::vector<std::vector<size_t>> v3 {{0}, {1}, {2}, {3, 4}};
    std::vector<std::vector<size_t>> v4 {{0}, {2}, {1}, {3, 4}};
    std::vector<std::vector<size_t>> v5 {{0}, {1}, {3}, {2}, {4}};

    RunPermutedTest(txsToValidate, {v1, v2, v3, v4, v5});
}

// Same as previous test except that transactions have many outputs and children spend many inputs.
BOOST_AUTO_TEST_CASE(txs_graphManyLinks) {
    /*
     *          0
     *        //||
     *       // 1
     *       2  ||\\
     *       \\ 3 ||
     *        \\||//
     *          4
     */
    // Given
    auto tx0 = CreateMockTx({COutPoint{coinbaseTxns[0].GetId(), 1}}, 4);
    auto tx1 = CreateMockTx({COutPoint{tx0.GetId(), 2}, COutPoint{tx0.GetId(), 3}}, 4);
    auto tx2 = CreateMockTx({COutPoint{tx0.GetId(), 0}, COutPoint{tx0.GetId(), 1}}, 2);
    auto tx3 = CreateMockTx({COutPoint{tx1.GetId(), 0}, COutPoint{tx1.GetId(), 1}}, 2);
    auto tx4 = CreateMockTx({COutPoint{tx2.GetId(), 0}, COutPoint{tx2.GetId(), 1}, 
                             COutPoint{tx3.GetId(), 0}, COutPoint{tx3.GetId(), 1}, 
                             COutPoint{tx1.GetId(), 2}, COutPoint{tx1.GetId(), 3}}, 1);
    std::vector<CMutableTransaction> txsToValidate{tx0, tx1, tx2, tx3, tx4};

    // Then
    // There can be 4 or 5 tasks.
    // tx0, tx1, tx2 are always scheduled in own tasks.
    // If tx2 is validated before tx1 or together with tx1, then tx3 and tx4 are validated as a chain in one task.
    // If tx2 is not yet validated when tx1 is finished, then tx3 is scheduled next. tx4 is only 
    // scheduled after tx2 and tx3 are finished.
    std::vector<std::vector<size_t>> v1 {{0}, {1}, {2}, {3}, {4}};
    std::vector<std::vector<size_t>> v2 {{0}, {2}, {1}, {3}, {4}};
    std::vector<std::vector<size_t>> v3 {{0}, {1}, {2}, {3, 4}};
    std::vector<std::vector<size_t>> v4 {{0}, {2}, {1}, {3, 4}};
    std::vector<std::vector<size_t>> v5 {{0}, {1}, {3}, {2}, {4}};

    RunPermutedTest(txsToValidate, {v1, v2, v3, v4, v5});
}

BOOST_AUTO_TEST_CASE(txs_detectChainInGraph) {
    /*
     *                  0   1
     *                  |  /|
     *                  | / 2
     *                   \|/
     *                    3
     */
    // Given
    auto tx0 = CreateMockTx({COutPoint{coinbaseTxns[0].GetId(), 1}}, 1);
    auto tx1 = CreateMockTx({COutPoint{coinbaseTxns[1].GetId(), 1}}, 2);
    auto tx2 = CreateMockTx({COutPoint{tx1.GetId(), 1}}, 1);
    auto tx3 = CreateMockTx({COutPoint{tx0.GetId(), 0}, COutPoint{tx1.GetId(), 0}, COutPoint{tx2.GetId(), 0}}, 1);
    std::vector<CMutableTransaction> txsToValidate{tx0, tx1, tx2, tx3};

    // Then
    // There can be 3 or 4 tasks.
    // tx0, tx1 are always scheduled in own tasks.
    // If tx0 is validated before tx1 or in the same cycle then tx2 and tx3 are scheduled as a chain. 
    std::vector<std::vector<size_t>> v1 {{0}, {1}, {2,3}};
    std::vector<std::vector<size_t>> v2 {{1}, {0}, {2,3}};
    std::vector<std::vector<size_t>> v3 {{1}, {0}, {2}, {3}};
    std::vector<std::vector<size_t>> v4 {{0}, {1}, {2}, {3}};
    std::vector<std::vector<size_t>> v5 {{1}, {2}, {0}, {3}};

    RunPermutedTest(txsToValidate, {v1,v2,v3,v4,v5});
}

// Transactions from the same chain are scheduled in one task.
BOOST_AUTO_TEST_CASE(txs_graphAndChains) {
    /*           0
     *          / \
     *         1   \
     *         |    2
     *         3   / \
     *         |  4   5
     *         6  |   |
     *         |  7   8
     *         \  |  /
     *          \ | /
     *           \|/
     *            9
     */
    // Given
    auto tx0 = CreateMockTx({COutPoint{coinbaseTxns[0].GetId(), 1}}, 2);
    auto tx1 = CreateMockTx({COutPoint{tx0.GetId(), 0}}, 1);
    auto tx2 = CreateMockTx({COutPoint{tx0.GetId(), 1}}, 2);
    auto tx3 = CreateMockTx({COutPoint{tx1.GetId(), 0}}, 1);
    auto tx4 = CreateMockTx({COutPoint{tx2.GetId(), 0}}, 1);
    auto tx5 = CreateMockTx({COutPoint{tx2.GetId(), 1}}, 1);
    auto tx6 = CreateMockTx({COutPoint{tx3.GetId(), 0}}, 1);
    auto tx7 = CreateMockTx({COutPoint{tx4.GetId(), 0}}, 1);
    auto tx8 = CreateMockTx({COutPoint{tx5.GetId(), 0}}, 1);
    auto tx9 = CreateMockTx({COutPoint{tx6.GetId(), 0}, COutPoint{tx7.GetId(), 0}, COutPoint{tx8.GetId(), 0}}, 1);
    std::vector<CMutableTransaction> txsToValidate{tx0, tx1, tx2, tx3, tx4, tx5, tx6, tx7, tx8, tx9};

    // Then
    // There should be 6 tasks. One for first tx, one for last tx, one for tx2.
    // And 3 tasks for 3 chains.
    std::vector<size_t> t0 {0};
    std::vector<size_t> c1 {1,3,6};
    std::vector<size_t> t2 {2};
    std::vector<size_t> c2 {4,7};
    std::vector<size_t> c3 {5,8};
    std::vector<size_t> t9 {9};
    // Variants
    // tx0 is always validated first, tx9 is always last.
    // Three chains are validated in parallel. Which tasks finishes first is impossible to predict.
    std::vector<std::vector<size_t>> v1 {t0, c1, t2, c2, c3, t9};
    std::vector<std::vector<size_t>> v2 {t0, c1, t2, c3, c2, t9};
    std::vector<std::vector<size_t>> v3 {t0, t2, c1, c2, c3, t9};
    std::vector<std::vector<size_t>> v4 {t0, t2, c1, c3, c2, t9};
    std::vector<std::vector<size_t>> v5 {t0, t2, c2, c1, c3, t9};
    std::vector<std::vector<size_t>> v6 {t0, t2, c2, c3, c1, t9};
    std::vector<std::vector<size_t>> v7 {t0, t2, c3, c1, c2, t9};
    std::vector<std::vector<size_t>> v8 {t0, t2, c3, c2, c1, t9};

    RunTest(txsToValidate, {v1, v2, v3, v4, v5, v6, v7, v8});
}

// Chain in two tasks due to dependency.
BOOST_AUTO_TEST_CASE(txs_chainInTwoParts) {
    /*           0
     *          / \
     *         1   2
     *         |  /
     *         3 /
     *         |/
     *         4
     *         |
     *         5
     */
    // Given
    auto tx0 = CreateMockTx({COutPoint{coinbaseTxns[0].GetId(), 1}}, 2);
    auto tx1 = CreateMockTx({COutPoint{tx0.GetId(), 0}}, 1);
    auto tx2 = CreateMockTx({COutPoint{tx0.GetId(), 1}}, 1);
    auto tx3 = CreateMockTx({COutPoint{tx1.GetId(), 0}}, 1);
    auto tx4 = CreateMockTx({COutPoint{tx3.GetId(), 0}, COutPoint{tx2.GetId(), 0}}, 1);
    auto tx5 = CreateMockTx({COutPoint{tx4.GetId(), 0}}, 1);
    std::vector<CMutableTransaction> txsToValidate{tx0, tx1, tx2, tx3, tx4, tx5};

    // Then
    // There should be 4 tasks. One for first tx, one for chain 1-3, one for tx2 and final one for chain 4-5
    // Variants
    // tx0 is always validated first, chain 4-5 is always last.
    // tx2 and chain 1-3 are run in parallel and can finish in any order.
    std::vector<std::vector<size_t>> v1 {{0}, {1,3}, {2}, {4,5}};
    std::vector<std::vector<size_t>> v2 {{0}, {2}, {1,3}, {4,5}};

    RunPermutedTest(txsToValidate, {v1, v2});
}

// Test that even if validation throws then spending txns are still scheduled.
// I.e. all txs in the batch are scheduled even if there are exceptions thrown.
BOOST_AUTO_TEST_CASE(txs_validationThrows) {
    /*
     *          0
     *         / \
     *        1   2
     *         \ /
     *          3
     */
    // Given
    auto tx0 = CreateMockTx({COutPoint{coinbaseTxns[0].GetId(), 1}}, 2);
    auto tx1 = CreateMockTx({COutPoint{tx0.GetId(), 0}}, 1);
    auto tx2 = CreateMockTx({COutPoint{tx0.GetId(), 1}}, 1);
    auto tx3 = CreateMockTx({COutPoint{tx1.GetId(), 0}, COutPoint{tx2.GetId(), 0}}, 1);
    std::vector<CMutableTransaction> txsToValidate{tx0, tx1, tx2, tx3};

    // Then
    // tx3 is still scheduled even if 1 and 2 fails.
    std::vector<std::vector<size_t>> v1 {{0}, {1}, {2}, {3}};
    std::vector<std::vector<size_t>> v2 {{0}, {2}, {1}, {3}};

    // When validations for 2 and 3 throws
    failList = {tx1.GetId(), tx2.GetId()};
    RunTest(txsToValidate, {v1, v2});
    failList.clear();
}

BOOST_AUTO_TEST_SUITE_END()
