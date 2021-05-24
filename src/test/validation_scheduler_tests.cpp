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
                      ValidationScheduler::TypeValidationFunc taskFunc,
                      CThreadPool<CDualQueueAdaptor>& threadPool) {
        std::vector<TxInputDataSPtr> txInputDataVec = TxInputDataVec(TxSource::unknown,
                                                                     txsToValidate);
        // Shuffle transaction in the batch. Scheduler is not dependent on the order of transaction in the batch.
        std::random_device rd;
        std::mt19937 rbg(rd());
        std::shuffle(txInputDataVec.begin(), txInputDataVec.end(), rbg);

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

    // Custom test criteria for executed tasks.
    // Purpose is check that executed tasks are one of the expected variants.
    // Most of the code deals with creating meaningful error message, i.e. use positions instead 
    // of meaningless TxId
    void CheckExecutionOrder(const std::vector<CMutableTransaction>& txsToValidate,
                             const std::vector<std::vector<TxId>>& executedTasks, 
                             const std::vector<std::vector<std::vector<size_t>>>& expectedVariants) {
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
        BOOST_TEST_MESSAGE("Executed tasks are: " << executedTasksPos);

        // Flatten task into list of validated transactions.
        std::vector<TxId> executedTxs;
        for (const auto& task : executedTasks) {
            executedTxs.insert(executedTxs.end(), task.cbegin(), task.cend());
        }

        // Check that each input tx is validated exactly once.
        for (const auto& tx : txsToValidate) {
            BOOST_TEST(std::count(executedTxs.cbegin(), executedTxs.cend(), tx.GetId()) == 1,
                       "" << idToPos[tx.GetId()] << " is validated once.");
        }

        // Check that transactions were validated in correct order.
        for (const auto& tx : txsToValidate) {
            const auto& txPos = std::find(executedTxs.cbegin(), executedTxs.cend(), tx.GetId());
            for (const auto& input : tx.vin) {
                const auto& inputPos = std::find(executedTxs.cbegin(), executedTxs.cend(), input.prevout.GetTxId());
                if (inputPos != executedTxs.cend()) {
                    // Input is in the batch.
                    BOOST_TEST(std::distance(inputPos, txPos) > 0,
                               "" << idToPos[tx.GetId()] << " is validated after " << idToPos[input.prevout.GetTxId()]);
                }
            }
        }

        // Check that tasks belong to one of expected variants. 
        BOOST_TEST(std::any_of(expectedVariants.cbegin(), expectedVariants.cend(),
                               [&executedTasksPos](const auto& v){ return(v == executedTasksPos); }));

    }

    struct TestSetup : TestChain100Setup {
        // Remember executed tasks and ids of txns in the task.
        std::vector<std::vector<TxId>> executedTasks{};
        std::mutex executedTasksMtx;

        // Function executed in the tasks.
        // Remembers the txn ids and returns dummy validation result.
        ValidationScheduler::TypeValidationFunc taskFunc = [this](const TxInputDataSPtrRefVec& vTxInputData){
            std::vector<TxId> idsInTask;
            ValidationScheduler::TypeValidationResult results;
            for (auto& tx : vTxInputData) {
                // Return dummy validation result.
                results.emplace_back(CTxnValResult{}, CTask::Status::RanToCompletion);
                // Remember ids of txns in the task.
                idsInTask.push_back(tx.get()->GetTxnPtr()->GetId());
            }
            std::scoped_lock<std::mutex> l(executedTasksMtx);
            executedTasks.push_back(idsInTask);
            return results;
        };

        CThreadPool<CDualQueueAdaptor> threadPool {"TestPool", 8, 8 };
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
    std::vector<CMutableTransaction> txsToValidate;
    auto tx0 = CreateMockTx({COutPoint{coinbaseTxns[0].GetId(), 1}}, 3);
    auto tx1 = CreateMockTx({COutPoint{coinbaseTxns[1].GetId(), 1}}, 3);
    auto tx2 = CreateMockTx({COutPoint{coinbaseTxns[2].GetId(), 1}}, 3);
    auto tx3 = CreateMockTx({COutPoint{coinbaseTxns[3].GetId(), 1}}, 3);
    txsToValidate.push_back(tx0);
    txsToValidate.push_back(tx1);
    txsToValidate.push_back(tx2);
    txsToValidate.push_back(tx3);

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
    std::vector<CMutableTransaction> txsToValidate;
    auto tx0 = CreateMockTx({COutPoint{coinbaseTxns[0].GetId(), 0}}, 1);
    auto tx1 = CreateMockTx({COutPoint{tx0.GetId(), 0}}, 1);
    auto tx2 = CreateMockTx({COutPoint{tx1.GetId(), 0}}, 1);
    auto tx3 = CreateMockTx({COutPoint{tx2.GetId(), 0}}, 1);
    auto tx4 = CreateMockTx({COutPoint{coinbaseTxns[1].GetId(), 0}}, 1);
    auto tx5 = CreateMockTx({COutPoint{tx4.GetId(), 0}}, 1);
    auto tx6 = CreateMockTx({COutPoint{tx5.GetId(), 0}}, 1);
    auto tx7 = CreateMockTx({COutPoint{tx6.GetId(), 0}}, 1);
    txsToValidate.insert(txsToValidate.end(), {tx0, tx1, tx2, tx3, tx4, tx5, tx6, tx7});

    // When
    RunScheduler(txsToValidate, taskFunc, threadPool);
    
    // Then
    // There should be 2 tasks. One for each chain.
    std::vector<size_t> c1 {0, 1, 2, 3};
    std::vector<size_t> c2 {4, 5, 6, 7};
    // Variants
    // Two chains are validated in parallel. Which tasks finishes first is impossible to predict.
    std::vector<std::vector<size_t>> v1{c1, c2};
    std::vector<std::vector<size_t>> v2{c2, c1};

    CheckExecutionOrder(txsToValidate, executedTasks, {v1, v2});
}

// Transactions from the same chain are scheduled in one task.
BOOST_AUTO_TEST_CASE(txs_chains2) {
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
    std::vector<CMutableTransaction> txsToValidate;
    auto tx0 = CreateMockTx({COutPoint{coinbaseTxns[0].GetId(), 1}}, 2);
    auto tx1 = CreateMockTx({COutPoint{tx0.GetId(), 0}}, 1);
    auto tx2 = CreateMockTx({COutPoint{tx0.GetId(), 1}}, 1);
    auto tx3 = CreateMockTx({COutPoint{tx1.GetId(), 0}}, 1);
    auto tx4 = CreateMockTx({COutPoint{tx2.GetId(), 1}}, 1);
    auto tx5 = CreateMockTx({COutPoint{tx3.GetId(), 0}}, 1);
    auto tx6 = CreateMockTx({COutPoint{tx4.GetId(), 1}}, 1);
    auto tx7 = CreateMockTx({COutPoint{tx5.GetId(), 0}, COutPoint{tx6.GetId(), 0}}, 1);
    txsToValidate.insert(txsToValidate.end(), {tx0, tx1, tx2, tx3, tx4, tx5, tx6, tx7});

    // When
    RunScheduler(txsToValidate, taskFunc, threadPool);

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

    CheckExecutionOrder(txsToValidate, executedTasks, {v1, v2});
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
    std::vector<CMutableTransaction> txsToValidate;
    auto tx0 = CreateMockTx({COutPoint{coinbaseTxns[0].GetId(), 1}}, 2);
    auto tx1 = CreateMockTx({COutPoint{tx0.GetId(), 1}}, 2);
    auto tx2 = CreateMockTx({COutPoint{tx0.GetId(), 0}}, 1);
    auto tx3 = CreateMockTx({COutPoint{tx1.GetId(), 0}}, 1);
    auto tx4 = CreateMockTx({COutPoint{tx2.GetId(), 0}, COutPoint{tx3.GetId(), 1}, COutPoint{tx1.GetId(), 1}}, 1);
    txsToValidate.insert(txsToValidate.end(), {tx0, tx1, tx2, tx3, tx4});

    // When
    RunScheduler(txsToValidate, taskFunc, threadPool);

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

    CheckExecutionOrder(txsToValidate, executedTasks, {v1, v2, v3, v4});
}

BOOST_AUTO_TEST_CASE(txs_graph2) {
    /*
     *                  0   1
     *                  |  /|
     *                  | / 2
     *                   \|/
     *                    3
     */
    // Given
    std::vector<CMutableTransaction> txsToValidate;
    auto tx0 = CreateMockTx({COutPoint{coinbaseTxns[0].GetId(), 1}}, 1);
    auto tx1 = CreateMockTx({COutPoint{coinbaseTxns[1].GetId(), 1}}, 2);
    auto tx2 = CreateMockTx({COutPoint{tx1.GetId(), 1}}, 1);
    auto tx3 = CreateMockTx({COutPoint{tx0.GetId(), 0}, COutPoint{tx1.GetId(), 0}, COutPoint{tx2.GetId(), 0}}, 1);
    txsToValidate.insert(txsToValidate.end(), {tx0, tx1, tx2, tx3});

    // When
    RunScheduler(txsToValidate, taskFunc, threadPool);

    // Then
    // There can be 3 or 4 tasks.
    // tx0, tx1, tx3 are always scheduled in own tasks.
    // If tx0 is validated before tx1 or in the same cycle then tx2 and tx3 are scheduled as a chain. 
    std::vector<std::vector<size_t>> v1 {{0}, {1}, {2,3}};
    std::vector<std::vector<size_t>> v2 {{1}, {0}, {2,3}};
    std::vector<std::vector<size_t>> v3 {{1}, {0}, {2}, {3}};

    CheckExecutionOrder(txsToValidate, executedTasks, {v1,v2,v3});
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
    std::vector<CMutableTransaction> txsToValidate;
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
    txsToValidate.insert(txsToValidate.end(), {tx0, tx1, tx2, tx3, tx4, tx5, tx6, tx7, tx8, tx9});

    // When
    RunScheduler(txsToValidate, taskFunc, threadPool);

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

    CheckExecutionOrder(txsToValidate, executedTasks, {v1, v2, v3, v4, v5, v6, v7, v8});
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
    std::vector<CMutableTransaction> txsToValidate;
    auto tx0 = CreateMockTx({COutPoint{coinbaseTxns[0].GetId(), 1}}, 2);
    auto tx1 = CreateMockTx({COutPoint{tx0.GetId(), 0}}, 1);
    auto tx2 = CreateMockTx({COutPoint{tx0.GetId(), 1}}, 1);
    auto tx3 = CreateMockTx({COutPoint{tx1.GetId(), 0}}, 1);
    auto tx4 = CreateMockTx({COutPoint{tx3.GetId(), 0}, COutPoint{tx2.GetId(), 0}}, 1);
    auto tx5 = CreateMockTx({COutPoint{tx4.GetId(), 0}}, 1);
    txsToValidate.insert(txsToValidate.end(), {tx0, tx1, tx2, tx3, tx4, tx5});

    // When
    RunScheduler(txsToValidate, taskFunc, threadPool);

    // Then
    // There should be 4 tasks. One for first tx, one for chain 1-3, one for tx2 and final one for chain 4-5
    // Variants
    // tx0 is always validated first, chain 4-5 is always last.
    // tx2 and chain 1-3 are run in parallel and can finish in any order.
    std::vector<std::vector<size_t>> v1 {{0}, {1,3}, {2}, {4,5}};
    std::vector<std::vector<size_t>> v2 {{0}, {2}, {1,3}, {4,5}};

    CheckExecutionOrder(txsToValidate, executedTasks, {v1, v2});
}

BOOST_AUTO_TEST_SUITE_END()
