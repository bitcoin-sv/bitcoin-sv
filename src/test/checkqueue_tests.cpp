// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "test/test_bitcoin.h"
#include "checkqueuepool.h"
#include "taskcancellation.h"

#include <boost/test/unit_test.hpp>
#include <boost/thread/thread.hpp>

#include <array>
#include <atomic>
#include <future>
#include <mutex>
#include <thread>

namespace
{
    /**
     * Validator that simulates long running validation and exits only after
     * it is unblocked by setting an external blocking variable to false
     */
    struct CBlockingValidator
    {
        CBlockingValidator() = default;
        CBlockingValidator(std::atomic<bool>& blocking)
            : mBlocking{&blocking}
        {/**/}

        std::optional<bool> operator()(const task::CCancellationToken&)
        {
            while(mBlocking->load())
            {
                using namespace std::chrono_literals;
                std::this_thread::sleep_for(100ms);
            }

            return true;
        }

        void swap(CBlockingValidator& check)
        {
            std::atomic<bool>* tmp = mBlocking;
            mBlocking = check.mBlocking;
            check.mBlocking = tmp;
        }

        std::atomic<bool>* mBlocking;
    };

    struct CDummyValidator
    {
        std::optional<bool> operator()(const task::CCancellationToken&)
        {
            return true;
        }
        void swap(CDummyValidator& check) {/**/}
    };

    struct CCancellingValidator
    {
        std::optional<bool> operator()(const task::CCancellationToken& token)
        {
            while(!token.IsCanceled());

            return {};
        }

        void swap(CCancellingValidator& check) {/**/}
    };
}

BOOST_FIXTURE_TEST_SUITE(checkqueue_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(check_queue_termination)
{
    std::atomic<bool> running = false;
    auto future =
        std::async(
            std::launch::async,
            [&running]
            {
                running = true;
                boost::thread_group threadGroup;
                CCheckQueue<CDummyValidator> check{4, threadGroup, 1, ""};

                // worker threads expect to be terminated by the interrupt signal
                threadGroup.interrupt_all();
                threadGroup.join_all();
            });

    using namespace std::chrono_literals;

    while(!running.load())
    {
        std::this_thread::sleep_for(100ms);
    }

    BOOST_CHECK(future.wait_for(5s) == std::future_status::ready);
}

BOOST_AUTO_TEST_CASE(removal_of_threads_during_processing)
{
    boost::thread_group threadGroup;
    CCheckQueue<CBlockingValidator> check{4, threadGroup, 1, ""};

    constexpr size_t checksNumber = 20;

    std::array<std::atomic<bool>, checksNumber> blocking;
    std::vector<CBlockingValidator> checks;

    for(size_t i=0; i<checksNumber; ++i)
    {
        blocking[i] = true;
        checks.emplace_back(blocking[i]);
    }
    
    auto source = task::CCancellationSource::Make();
    check.StartCheckingSession(source->GetToken());
    check.Add(checks);

    threadGroup.interrupt_all();

    for(auto& b : blocking)
    {
        b = false;
    }

    threadGroup.join_all();

    // we expect that everything will be validated even though thread
    // termination request was issued during execution
    auto result = check.Wait();

    BOOST_CHECK(result.has_value() && result.value());
}

BOOST_AUTO_TEST_CASE(premature_validation_cancellation)
{
    boost::thread_group threadGroup;
    CCheckQueue<CCancellingValidator> check{4, threadGroup, 1, ""};
    std::vector<CCancellingValidator> checks(20);

    auto source = task::CCancellationSource::Make();
    check.StartCheckingSession(source->GetToken());

    check.Add(checks);
    source->Cancel();

    // we expect that validation will be terminated without result as we quit
    // before we tried to get to result
    auto result = check.Wait();

    threadGroup.interrupt_all();
    threadGroup.join_all();

    BOOST_CHECK(!result.has_value());
}

BOOST_AUTO_TEST_CASE(check_queue_pool_termination)
{
    boost::thread_group threadGroup;
    checkqueue::CCheckQueuePool<CDummyValidator, int> scriptCheckQueuePool{
        4, threadGroup, 1, 4};

    // worker threads expect to be terminated by the interrupt signal
    threadGroup.interrupt_all();
    threadGroup.join_all();
}

BOOST_AUTO_TEST_CASE(premature_implicit_cancellation_and_reusing_the_worst_checker)
{
    boost::thread_group threadGroup;
    checkqueue::CCheckQueuePool<CDummyValidator, int> scriptCheckQueuePool{
        4, threadGroup, 1, 4};

    auto source = task::CCancellationSource::Make();

    std::optional<task::CCancellationToken> worstCancellationToken;
    auto checkerWorst =
        scriptCheckQueuePool.GetChecker(
            1,
            source->GetToken(),
            &worstCancellationToken);

    auto checker2 = scriptCheckQueuePool.GetChecker(2, source->GetToken());
    auto checker3 = scriptCheckQueuePool.GetChecker(3, source->GetToken());
    auto checker4 = scriptCheckQueuePool.GetChecker(4, source->GetToken());

    // we need a lock since we access checkerWorst from two threads and checker
    // is not thread safe
    std::mutex worstWaitSyncLock;

    // queue is returned to the pool only after checker goes out of scope or
    // Wait() is called on it so we need to run it on a different thread
    auto future =
        std::async(
            std::launch::async,
            [
                &worstWaitSyncLock,
                &checkerWorst,
                token = std::move(worstCancellationToken.value())]
            {
                // wait until pool requests the cancellation
                while(!token.IsCanceled());
                std::lock_guard lock{worstWaitSyncLock};
                BOOST_CHECK(!checkerWorst.Wait().has_value());
            });

    // since we do not have any idle checkers left in the pool checkerWorst
    // should be terminated by the pool without blocking
    auto checkerBest = scriptCheckQueuePool.GetChecker(5, source->GetToken());

    {
        std::lock_guard lock{worstWaitSyncLock};
        BOOST_CHECK(!checkerWorst.Wait().has_value());
    }
    BOOST_CHECK(checker2.Wait().value());
    BOOST_CHECK(checker3.Wait().value());
    BOOST_CHECK(checker4.Wait().value());
    BOOST_CHECK(checkerBest.Wait().value());

    threadGroup.interrupt_all();
    threadGroup.join_all();
}

BOOST_AUTO_TEST_CASE(checkqueue_invalid_use__call_wait_before_session)
{
    CCheckQueue<CDummyValidator> scriptCheckQueue{128};

    BOOST_CHECK_THROW(scriptCheckQueue.Wait(), std::runtime_error);
    scriptCheckQueue.StartCheckingSession(
        task::CCancellationSource::Make()->GetToken());
    scriptCheckQueue.Wait();
}

BOOST_AUTO_TEST_CASE(checkqueue_invalid_use__call_add_before_session)
{
    CCheckQueue<CDummyValidator> scriptCheckQueue{128};

    std::vector check{CDummyValidator{}};

    BOOST_CHECK_THROW(scriptCheckQueue.Add(check), std::runtime_error);
    scriptCheckQueue.StartCheckingSession(
        task::CCancellationSource::Make()->GetToken());
    scriptCheckQueue.Add(check);
    scriptCheckQueue.Wait();
}

BOOST_AUTO_TEST_CASE(checkqueue_invalid_use__call_add_after_wait)
{
    CCheckQueue<CDummyValidator> scriptCheckQueue{128};

    std::vector check{CDummyValidator{}};

    scriptCheckQueue.StartCheckingSession(
        task::CCancellationSource::Make()->GetToken());
    scriptCheckQueue.Add(check);
    check = {CDummyValidator{}};
    scriptCheckQueue.Wait();
    BOOST_CHECK_THROW(scriptCheckQueue.Add(check), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(checkqueue_invalid_use__call_second_session_before_wait)
{
    CCheckQueue<CDummyValidator> scriptCheckQueue{128};

    scriptCheckQueue.StartCheckingSession(
        task::CCancellationSource::Make()->GetToken());
    BOOST_CHECK_THROW(
        scriptCheckQueue.StartCheckingSession(
            task::CCancellationSource::Make()->GetToken()),
        std::runtime_error);
    scriptCheckQueue.Wait();
    scriptCheckQueue.StartCheckingSession(
        task::CCancellationSource::Make()->GetToken());
}

BOOST_AUTO_TEST_SUITE_END()
