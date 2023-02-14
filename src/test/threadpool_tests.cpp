// Copyright (c) 2018-2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include <boost/test/unit_test.hpp>
#include <threadpool.h>
#include <atomic>
#include <iostream>

#include <task.h>
#include <task_helpers.h>

namespace
{
    // Each task increments a counter by this much
    constexpr unsigned Increment { 1000000 };

    // A shared counter
    std::atomic<unsigned> Counter {0};

    // A function task
    void Function(unsigned inc)
    {
        // Some pointless work
        for(unsigned i = 0; i < inc; ++i)
        {
            Counter++;
        }
    }

    // A member function task
    struct TaskClass
    {
        void MemberFunction(unsigned inc)
        {
            Function(inc);
        }
    };
    TaskClass taskClass {};

    // A lambda task
    auto lambdaTask { [](unsigned inc){ Function(inc); } };
}

BOOST_AUTO_TEST_SUITE(TestThreadPool)

// Test basic non-prioritised thread pool handling
BOOST_AUTO_TEST_CASE(NonPrioritised)
{
    CThreadPool<CQueueAdaptor> pool { false, "TestPool", 4 };
    BOOST_CHECK_EQUAL(pool.getPoolSize(), 4U);
    BOOST_CHECK_EQUAL(Counter, 0U);

    // Submit some tasks to the queue
    std::vector<std::future<void>> results {};
    results.push_back(make_task(pool, Function, Increment));
    results.push_back(make_task(pool, Function, Increment));
    results.push_back(make_task(pool, Function, Increment));
    results.push_back(make_task(pool, Function, Increment));
    results.push_back(make_task(pool, Function, Increment));
    results.push_back(make_task(pool, &TaskClass::MemberFunction, &taskClass, Increment));
    results.push_back(make_task(pool, &TaskClass::MemberFunction, &taskClass, Increment));
    results.push_back(make_task(pool, &TaskClass::MemberFunction, &taskClass, Increment));
    results.push_back(make_task(pool, &TaskClass::MemberFunction, &taskClass, Increment));
    results.push_back(make_task(pool, &TaskClass::MemberFunction, &taskClass, Increment));
    results.push_back(make_task(pool, lambdaTask, Increment));
    results.push_back(make_task(pool, lambdaTask, Increment));
    results.push_back(make_task(pool, lambdaTask, Increment));
    results.push_back(make_task(pool, lambdaTask, Increment));
    results.push_back(make_task(pool, lambdaTask, Increment));

    // Wait for all tasks to complete
    for(auto& res : results)
    {
        res.get();
    }

    // Should have run 15 tasks
    BOOST_CHECK_EQUAL(Counter, Increment * results.size());
}

// Test prioritised thread pool handling
BOOST_AUTO_TEST_CASE(Prioritised)
{
    // Scenario 1: test basic, specified priorities
    // Single threaded pool for reproducable task execution ordering
    CThreadPool<CPriorityQueueAdaptor> pool { false, "TestPool", 1 };
    // Make sure nothing starts executing until we have queued everything
    pool.pause();
    BOOST_CHECK(pool.paused());
    BOOST_CHECK_EQUAL(pool.getTaskDepth(), 0U);

    // Each task will add a result to this vector
    std::vector<std::string> taskResults {};
    // Expected final contents
    std::vector<std::string> expectedResults { "VeryHigh", "High", "Medium", "Low" };

    // Some tasks to run
    std::vector<std::future<void>> results {};
    results.push_back(make_task(pool, CTask::Priority::Low, [&taskResults](){ taskResults.push_back("Low"); }));
    results.push_back(make_task(pool, CTask::Priority::Medium, [&taskResults](){ taskResults.push_back("Medium"); }));
    results.push_back(make_task(pool, CTask::Priority::High, [&taskResults](){ taskResults.push_back("High"); }));
    results.push_back(make_task(pool, 10, [&taskResults](){ taskResults.push_back("VeryHigh"); }));
    BOOST_CHECK_EQUAL(pool.getTaskDepth(), 4U);

    // Wait for all tasks to complete
    pool.run();
    BOOST_CHECK(!pool.paused());
    for(auto& res : results)
    {
        res.get();
    }

    BOOST_CHECK(taskResults == expectedResults);
    BOOST_CHECK_EQUAL(pool.getTaskDepth(), 0U);

    taskResults.clear();
    expectedResults.clear();
    results.clear();
    pool.pause();

    // Scenario 2: test that unspecified priority is the same as medium priority
    expectedResults  = { "High", "Unspec", "Low" };
    results.push_back(make_task(pool, CTask::Priority::Low, [&taskResults](){ taskResults.push_back("Low"); }));
    results.push_back(make_task(pool, [&taskResults](){ taskResults.push_back("Unspec"); }));
    results.push_back(make_task(pool, CTask::Priority::High, [&taskResults](){ taskResults.push_back("High"); }));

    // Wait for all tasks to complete
    pool.run();
    BOOST_CHECK(!pool.paused());
    for(auto& res : results)
    {
        res.get();
    }

    BOOST_CHECK(taskResults == expectedResults);

}

// Test dual queue processed by prioritised thread pool
BOOST_AUTO_TEST_CASE(DualQueueProcessedByPriotisedThreadsCase1)
{
    Counter = 0;
    CThreadPool<CDualQueueAdaptor> pool { false, "TestPool", 4, 1 };
    BOOST_CHECK_EQUAL(pool.getPoolSize(), 5U);
    BOOST_CHECK_EQUAL(Counter, 0U);

    // Submit some tasks to the queue
    std::vector<std::future<void>> results {};
    results.push_back(make_task(pool, CTask::Priority::Low, Function, Increment));
    results.push_back(make_task(pool, CTask::Priority::Low, Function, Increment));
    results.push_back(make_task(pool, CTask::Priority::Low, Function, Increment));
    results.push_back(make_task(pool, CTask::Priority::Low, Function, Increment));
    results.push_back(make_task(pool, CTask::Priority::Low, Function, Increment));
    results.push_back(make_task(pool, CTask::Priority::High, Function, Increment));
    results.push_back(make_task(pool, CTask::Priority::Low, &TaskClass::MemberFunction, &taskClass, Increment));
    results.push_back(make_task(pool, CTask::Priority::Low, &TaskClass::MemberFunction, &taskClass, Increment));
    results.push_back(make_task(pool, CTask::Priority::Low, &TaskClass::MemberFunction, &taskClass, Increment));
    results.push_back(make_task(pool, CTask::Priority::Low, &TaskClass::MemberFunction, &taskClass, Increment));
    results.push_back(make_task(pool, CTask::Priority::High, &TaskClass::MemberFunction, &taskClass, Increment));
    results.push_back(make_task(pool, CTask::Priority::Low, lambdaTask, Increment));
    results.push_back(make_task(pool, CTask::Priority::Low, lambdaTask, Increment));
    results.push_back(make_task(pool, CTask::Priority::Low, lambdaTask, Increment));
    results.push_back(make_task(pool, CTask::Priority::Low, lambdaTask, Increment));
    results.push_back(make_task(pool, CTask::Priority::High, lambdaTask, Increment));

    // Wait for all tasks to complete
    for(auto& res : results)
    {
        res.get();
    }

    // Should have run 16 tasks
    BOOST_CHECK_EQUAL(Counter, Increment * results.size());
}

// Test dual queue processed by prioritised thread pool
BOOST_AUTO_TEST_CASE(DualQueueProcessedByPriotisedThreadsCase2)
{
    Counter = 0;
    CThreadPool<CDualQueueAdaptor> pool { false, "TestPool", 1, 4 };
    BOOST_CHECK_EQUAL(pool.getPoolSize(), 5U);
    BOOST_CHECK_EQUAL(Counter, 0U);

    // Submit some tasks to the queue
    std::vector<std::future<void>> results {};
    results.push_back(make_task(pool, CTask::Priority::High, Function, Increment));
    results.push_back(make_task(pool, CTask::Priority::High, Function, Increment));
    results.push_back(make_task(pool, CTask::Priority::High, Function, Increment));
    results.push_back(make_task(pool, CTask::Priority::High, Function, Increment));
    results.push_back(make_task(pool, CTask::Priority::High, Function, Increment));
    results.push_back(make_task(pool, CTask::Priority::Low, Function, Increment));
    results.push_back(make_task(pool, CTask::Priority::High, &TaskClass::MemberFunction, &taskClass, Increment));
    results.push_back(make_task(pool, CTask::Priority::High, &TaskClass::MemberFunction, &taskClass, Increment));
    results.push_back(make_task(pool, CTask::Priority::High, &TaskClass::MemberFunction, &taskClass, Increment));
    results.push_back(make_task(pool, CTask::Priority::High, &TaskClass::MemberFunction, &taskClass, Increment));
    results.push_back(make_task(pool, CTask::Priority::Low, &TaskClass::MemberFunction, &taskClass, Increment));
    results.push_back(make_task(pool, CTask::Priority::High, lambdaTask, Increment));
    results.push_back(make_task(pool, CTask::Priority::High, lambdaTask, Increment));
    results.push_back(make_task(pool, CTask::Priority::High, lambdaTask, Increment));
    results.push_back(make_task(pool, CTask::Priority::High, lambdaTask, Increment));
    results.push_back(make_task(pool, CTask::Priority::Low, lambdaTask, Increment));

    // Wait for all tasks to complete
    for(auto& res : results)
    {
        res.get();
    }

    // Should have run 16 tasks
    BOOST_CHECK_EQUAL(Counter, Increment * results.size());
}

// Test dual queue processed by prioritised thread pool
BOOST_AUTO_TEST_CASE(DualQueueProcessedByPriotisedThreadsCase3)
{
    Counter = 0;
    CThreadPool<CDualQueueAdaptor> pool { false, "TestPool", 0, 4 };
    BOOST_CHECK_EQUAL(pool.getPoolSize(), 4U);
    BOOST_CHECK_EQUAL(Counter, 0U);

    // Submit some tasks to the queue
    std::vector<std::future<void>> results {};
    results.push_back(make_task(pool, CTask::Priority::High, Function, Increment));
    results.push_back(make_task(pool, CTask::Priority::Low, Function, Increment));
    results.push_back(make_task(pool, CTask::Priority::High, &TaskClass::MemberFunction, &taskClass, Increment));
    results.push_back(make_task(pool, CTask::Priority::Low, &TaskClass::MemberFunction, &taskClass, Increment));
    results.push_back(make_task(pool, CTask::Priority::High, lambdaTask, Increment));
    results.push_back(make_task(pool, CTask::Priority::Low, lambdaTask, Increment));

    // Wait for all tasks to complete
    for(auto& res : results)
    {
        res.get();
    }

    // Should have run 6 tasks
    BOOST_CHECK_EQUAL(Counter, Increment * results.size());
}

// Test dual queue processed by prioritised thread pool
BOOST_AUTO_TEST_CASE(DualQueueProcessedByPriotisedThreadsCase4)
{
    Counter = 0;
    CThreadPool<CDualQueueAdaptor> pool { false, "TestPool", 4, 0 };
    BOOST_CHECK_EQUAL(pool.getPoolSize(), 4U);
    BOOST_CHECK_EQUAL(Counter, 0U);

    // Submit some tasks to the queue
    std::vector<std::future<void>> results {};
    results.push_back(make_task(pool, CTask::Priority::Low, Function, Increment));
    results.push_back(make_task(pool, CTask::Priority::High, Function, Increment));
    results.push_back(make_task(pool, CTask::Priority::Low, &TaskClass::MemberFunction, &taskClass, Increment));
    results.push_back(make_task(pool, CTask::Priority::High, &TaskClass::MemberFunction, &taskClass, Increment));
    results.push_back(make_task(pool, CTask::Priority::Low, lambdaTask, Increment));
    results.push_back(make_task(pool, CTask::Priority::High, lambdaTask, Increment));

    // Wait for all tasks to complete
    for(auto& res : results)
    {
        res.get();
    }

    // Should have run 6 tasks
    BOOST_CHECK_EQUAL(Counter, Increment * results.size());
}

BOOST_AUTO_TEST_SUITE_END()

