// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "test/test_bitcoin.h"
#include "taskcancellation.h"

#include <boost/test/unit_test.hpp>
#include <future>


BOOST_FIXTURE_TEST_SUITE(taskcancellation_tests, BasicTestingSetup)

namespace
{
    void TestToken(
        const task::CCancellationToken& token,
        std::shared_ptr<task::CCancellationSource>& source)
    {
        std::atomic<bool> oneLoopLocked{false};

        auto future =
            std::async(
                std::launch::async,
                [&token, &oneLoopLocked]
                {
                    while(!token.IsCanceled()) {oneLoopLocked = true;}
                });

        // make sure that we are really executing the task
        while(!oneLoopLocked);

        using namespace std::chrono_literals;
        BOOST_CHECK(future.wait_for(1s) != std::future_status::ready);
        source->Cancel();
        BOOST_CHECK(future.wait_for(5s) == std::future_status::ready);
        future.get();
    }
}

BOOST_AUTO_TEST_CASE(cancellation)
{
    auto source = task::CCancellationSource::Make();

    TestToken(source->GetToken(), source);
}

BOOST_AUTO_TEST_CASE(token_joining)
{
    {
        auto source = task::CCancellationSource::Make();
        auto source2 = task::CCancellationSource::Make();

        auto token2 = source2->GetToken();
        auto token =
            task::CCancellationToken::JoinToken(source->GetToken(), token2);

        TestToken(std::move(token), source);

        BOOST_CHECK_EQUAL(token2.IsCanceled(), false);
        BOOST_CHECK_EQUAL(source->GetToken().IsCanceled(), true);
        BOOST_CHECK_EQUAL(source2->GetToken().IsCanceled(), false);
    }

    {
        auto source = task::CCancellationSource::Make();
        auto source2 = task::CCancellationSource::Make();

        auto token2 = source2->GetToken();
        auto token =
            task::CCancellationToken::JoinToken(source->GetToken(), token2);

        TestToken(std::move(token), source2);

        BOOST_CHECK_EQUAL(token2.IsCanceled(), true);
        BOOST_CHECK_EQUAL(source->GetToken().IsCanceled(), false);
        BOOST_CHECK_EQUAL(source2->GetToken().IsCanceled(), true);
    }
}

// sleep for at least the specified duration
void sleep_wait(std::chrono::milliseconds delay) {
    using clock = std::chrono::steady_clock;
    using namespace std::chrono_literals;
    auto end = clock::now() + delay;
    std::this_thread::sleep_for(delay);
    while (clock::now() < end) {
        std::this_thread::yield();
    }
}

BOOST_AUTO_TEST_CASE(cancellation_after_500ms)
{
    using namespace std::chrono_literals;

    auto source = task::CTimedCancellationSource::Make(50ms);
    auto token = source->GetToken();

    BOOST_CHECK_EQUAL(token.IsCanceled(), false);
    sleep_wait(51ms);
    BOOST_CHECK_EQUAL(token.IsCanceled(), true);
}


BOOST_AUTO_TEST_CASE(cancellation_after_500ms_budget)
{
    using namespace std::chrono_literals;

    auto budget = task::CTimedCancellationBudget {50ms};
    budget.FillBudget(-100ms);
    {
        {
            auto source = task::CTimedCancellationSource::Make(55ms, budget);
            auto token = source->GetToken();

            BOOST_CHECK_EQUAL(token.IsCanceled(), false);
            sleep_wait(5ms);
            BOOST_CHECK_EQUAL(token.IsCanceled(), false);
        }
        auto remains = budget.DrainBudget(0ms);
        BOOST_CHECK_GE(remains, 20ms);
   }

    budget.FillBudget(30ms);
    {
        {
            auto source = task::CTimedCancellationSource::Make(20ms, budget);
            auto token = source->GetToken();

            BOOST_CHECK_EQUAL(token.IsCanceled(), false);
            sleep_wait(40ms);
            BOOST_CHECK_EQUAL(token.IsCanceled(), false);
            sleep_wait(11ms);
            BOOST_CHECK_EQUAL(token.IsCanceled(), true);
        }
        auto remains = budget.DrainBudget(0ms);
        BOOST_CHECK_GE(remains, 0us);
    }

}


#ifdef BOOST_CHRONO_HAS_THREAD_CLOCK

void busy_wait(std::chrono::milliseconds delay) {
    using clock = task::thread_clock;
    std::atomic_size_t jiffies = 0;
    auto end = clock::now() + delay;
    while (clock::now() < end) {
        ++jiffies;
    }
}

BOOST_AUTO_TEST_CASE(thread_cancellation_after_500ms_cpu)
{
    using namespace std::chrono_literals;

    auto source = task::CThreadTimedCancellationSource::Make(50ms);
    auto token = source->GetToken();

    BOOST_CHECK_EQUAL(token.IsCanceled(), false);
    busy_wait(51ms);
    BOOST_CHECK_EQUAL(token.IsCanceled(), true);
}


BOOST_AUTO_TEST_CASE(thread_cancellation_after_500ms_cpu_budget)
{
    using namespace std::chrono_literals;

    auto budget = task::CTimedCancellationBudget {50ms};
    budget.FillBudget(-100ms);
    {
        {
            auto source = task::CThreadTimedCancellationSource::Make(55ms, budget);
            auto token = source->GetToken();

            BOOST_CHECK_EQUAL(token.IsCanceled(), false);
            busy_wait(5ms);
            BOOST_CHECK_EQUAL(token.IsCanceled(), false);
        }
        auto remains = budget.DrainBudget(0ms);
        BOOST_CHECK_GE(remains, 20ms);
    }

    budget.FillBudget(30ms);
    {
        {
            auto source = task::CThreadTimedCancellationSource::Make(20ms, budget);
            auto token = source->GetToken();

            BOOST_CHECK_EQUAL(token.IsCanceled(), false);
            busy_wait(40ms);
            BOOST_CHECK_EQUAL(token.IsCanceled(), false);
            busy_wait(11ms);
            BOOST_CHECK_EQUAL(token.IsCanceled(), true);
        }
        auto remains = budget.DrainBudget(0ms);
        BOOST_CHECK_GE(remains, 0us);
    }
}
#endif

BOOST_AUTO_TEST_SUITE_END()
