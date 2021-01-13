// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include <boost/test/unit_test.hpp>
#include <atomic>
#include <future>
#include "write_preferring_upgradable_mutex.h"
#include "testutil.h"

BOOST_AUTO_TEST_SUITE(write_preferring_upgradable_mutex_tests)

BOOST_AUTO_TEST_CASE(write_lock_request_waits_for_read_locks)
{
    using namespace std::literals::chrono_literals;
    WPUSMutex mutex;

    for(bool tryWriteLock : {true, false})
    {
        {
            // make write lock without being blocked by other locks
            WPUSMutex::Lock lock;

            if( tryWriteLock )
            {
                mutex.ReadLock( lock );
                bool locked = mutex.TryWriteLock( lock );
                BOOST_TEST( locked == true );
            }
            else
            {
                lock = mutex.WriteLock();
            }
            BOOST_TEST((lock.GetLockType() == WPUSMutex::Lock::Type::write));
        }

        auto test_read_lock =
            [&mutex](std::atomic<int>& step)
            {
                WPUSMutex::Lock read_lock;
                mutex.ReadLock( read_lock );
                BOOST_TEST((read_lock.GetLockType() == WPUSMutex::Lock::Type::read));
                step = 1;

                // wait for others to finish initialization
                while(step.load() == 1);
            };

        std::atomic<int> one_step{0};
        auto one = std::async(std::launch::async, test_read_lock, std::reference_wrapper{one_step});
        std::atomic<int> two_step{0};
        auto two = std::async(std::launch::async, test_read_lock, std::reference_wrapper{two_step});

        // wait for all read locks to initialize
        BOOST_TEST(wait_for([&]{ return one_step.load() == 1 && two_step.load() == 1; }, 200ms));

        // getting here indicates that we can have multiple read locks at the same time

        auto write =
            std::async(
                std::launch::async,
                [&mutex, &tryWriteLock]
                {
                    WPUSMutex::Lock lock;

                    if( tryWriteLock )
                    {
                        mutex.ReadLock( lock );
                        bool locked = mutex.TryWriteLock( lock );
                        BOOST_TEST( locked == true );
                    }
                    else
                    {
                        lock = mutex.WriteLock();
                    }

                    BOOST_TEST((lock.GetLockType() == WPUSMutex::Lock::Type::write));
                });

        // make sure that write lock can't be obtained as we are holding read locks
        BOOST_TEST((write.wait_for(200ms) == std::future_status::timeout));

        one_step = 2;
        BOOST_TEST((one.wait_for(200ms) == std::future_status::ready));
        // make sure that write lock can't be obtained as there is still one read lock pending
        BOOST_TEST((write.wait_for(200ms) == std::future_status::timeout));

        two_step = 2;
        BOOST_TEST((two.wait_for(200ms) == std::future_status::ready));
        // make sure that write lock can now be obtained as no read locks are present
        BOOST_TEST((write.wait_for(500ms) == std::future_status::ready));
    }
}

BOOST_AUTO_TEST_CASE(prefering_write_to_read_request_lock)
{
    using namespace std::literals::chrono_literals;
    WPUSMutex mutex;

    for(bool tryWriteLock : {true, false})
    {
        WPUSMutex::Lock read_lock;
        BOOST_TEST((read_lock.GetLockType() == WPUSMutex::Lock::Type::unlocked));
        mutex.ReadLock( read_lock );
        BOOST_TEST((read_lock.GetLockType() == WPUSMutex::Lock::Type::read));
        WPUSMutex::Lock read_lock_2;
        mutex.ReadLock( read_lock_2 );
        BOOST_TEST((read_lock_2.GetLockType() == WPUSMutex::Lock::Type::read));

        std::atomic<int> write_step{0};
        auto write =
            std::async(
                std::launch::async,
                [&mutex, &tryWriteLock](std::atomic<int>& step)
                {
                    WPUSMutex::Lock lock;
                    BOOST_TEST((lock.GetLockType() == WPUSMutex::Lock::Type::unlocked));

                    if( tryWriteLock )
                    {
                        mutex.ReadLock( lock );
                        bool locked = mutex.TryWriteLock( lock );
                        BOOST_TEST( locked == true );
                    }
                    else
                    {
                        lock = mutex.WriteLock();
                    }

                    BOOST_TEST((lock.GetLockType() == WPUSMutex::Lock::Type::write));
                    step = 1;

                    // wait for asserts to finish
                    while(step.load() == 1);
                },
                std::reference_wrapper{write_step});

        // make sure that write lock can't be obtained as we are holding read locks
        BOOST_TEST((write.wait_for(200ms) == std::future_status::timeout));

        auto late_read_lock =
            std::async(
                std::launch::async,
                [&mutex]
                {
                    WPUSMutex::Lock read_lock;
                    mutex.ReadLock( read_lock );
                    BOOST_TEST((read_lock.GetLockType() == WPUSMutex::Lock::Type::read));
                });

        // make sure that read lock can't be obtained as write lock request is pending
        BOOST_TEST((late_read_lock.wait_for(200ms) == std::future_status::timeout));

        read_lock = {}; // this is equivalent to read_lock.Release();
        BOOST_TEST((read_lock.GetLockType() == WPUSMutex::Lock::Type::unlocked));
        // make sure that write lock can't be obtained as we are still holding one read lock
        BOOST_TEST((write.wait_for(200ms) == std::future_status::timeout));
        read_lock_2.Release();
        BOOST_TEST((read_lock_2.GetLockType() == WPUSMutex::Lock::Type::unlocked));
        // make sure that write lock is obtained as the pending read lock has lower priority
        BOOST_TEST(wait_for([&]{ return write_step.load() == 1; }, 200ms));
        // make sure that read lock request is still pending as we are holding a write lock
        BOOST_TEST((late_read_lock.wait_for(200ms) == std::future_status::timeout));

        write_step = 2;
        BOOST_TEST((write.wait_for(200ms) == std::future_status::ready));
        // make sure that read lock can now be obtained
        BOOST_TEST((late_read_lock.wait_for(200ms) == std::future_status::ready));
    }
}

BOOST_AUTO_TEST_CASE(lock_escalation)
{
    WPUSMutex mutex;

    WPUSMutex::Lock read_lock;
    BOOST_TEST((read_lock.GetLockType() == WPUSMutex::Lock::Type::unlocked));
    mutex.ReadLock( read_lock );
    BOOST_TEST((read_lock.GetLockType() == WPUSMutex::Lock::Type::read));

    // transform to write lock even though a read lock existed beforehand
    bool locked = mutex.TryWriteLock( read_lock );
    BOOST_TEST( locked == true );
    BOOST_TEST((read_lock.GetLockType() == WPUSMutex::Lock::Type::write));
}

BOOST_AUTO_TEST_CASE(duplicate_read_lock_even_if_write_lock_pending)
{
    using namespace std::literals::chrono_literals;
    WPUSMutex mutex;

    for(bool tryWriteLock : {true, false})
    {
        WPUSMutex::Lock read_lock;
        mutex.ReadLock( read_lock );
        BOOST_TEST((read_lock.GetLockType() == WPUSMutex::Lock::Type::read));

        // get an additional read lock from existing read lock even though write locks have an advantage
        WPUSMutex::Lock read_lock_2;
        mutex.ReadLock( read_lock_2 );
        BOOST_TEST((read_lock_2.GetLockType() == WPUSMutex::Lock::Type::read));

        auto write =
            std::async(
                std::launch::async,
                [&mutex, &tryWriteLock]
                {
                    WPUSMutex::Lock lock;

                    if( tryWriteLock )
                    {
                        mutex.ReadLock( lock );
                        bool locked = mutex.TryWriteLock( lock );
                        BOOST_TEST( locked == true );
                    }
                    else
                    {
                        lock = mutex.WriteLock();
                    }

                    BOOST_TEST((lock.GetLockType() == WPUSMutex::Lock::Type::write));
                });

        // make sure that write lock can't be obtained as we are holding a read lock
        BOOST_TEST((write.wait_for(200ms) == std::future_status::timeout));

        read_lock = {};
        BOOST_TEST((read_lock.GetLockType() == WPUSMutex::Lock::Type::unlocked));
        // make sure that write lock can't be obtained as we are still holding a read lock
        BOOST_TEST((write.wait_for(200ms) == std::future_status::timeout));

        read_lock_2.Release();
        BOOST_TEST((read_lock_2.GetLockType() == WPUSMutex::Lock::Type::unlocked));
        // make sure that write lock can now be obtained
        BOOST_TEST((write.wait_for(200ms) == std::future_status::ready));
    }
}

BOOST_AUTO_TEST_CASE(multiple_consecutive_exclusive_write_locks)
{
    using namespace std::literals::chrono_literals;
    WPUSMutex mutex;

    WPUSMutex::Lock write_lock = mutex.WriteLock();
    BOOST_TEST((write_lock.GetLockType() == WPUSMutex::Lock::Type::write));

    auto write_lock_task =
        [&mutex](std::atomic<int>& step)
        {
            WPUSMutex::Lock write_lock = mutex.WriteLock();
            BOOST_TEST((write_lock.GetLockType() == WPUSMutex::Lock::Type::write));
            step = 1;

            // wait for asserts to finish
            while(step.load() == 1);
        };

    std::atomic<int> two_step{0};
    auto write_2 = std::async(std::launch::async, write_lock_task, std::reference_wrapper{two_step});
    std::atomic<int> three_step{0};
    auto write_3 = std::async(std::launch::async, write_lock_task, std::reference_wrapper{three_step});

    // make sure that write lock can't be obtained as we are already holding one
    BOOST_TEST((write_2.wait_for(200ms) == std::future_status::timeout));
    BOOST_TEST((write_3.wait_for(0ms) == std::future_status::timeout)); // no need to wait again as 200ms have already passed

    write_lock = {};
    BOOST_TEST((write_lock.GetLockType() == WPUSMutex::Lock::Type::unlocked));

    // one of the locks must be held now while the other is still waiting
    BOOST_TEST(wait_for([&]{ return two_step.load() + three_step.load() == 1; }, 200ms));

    auto next_steps =
        [](
            std::atomic<int>& done_step,
            std::future<void>& done,
            std::atomic<int>& waiting_step,
            std::future<void>& waiting)
        {
            // make sure the other write lock is still waiting
            BOOST_TEST((waiting.wait_for(200ms) == std::future_status::timeout));
            BOOST_TEST(waiting_step.load() == 0);

            // release write lock
            done_step = 2;
            BOOST_TEST((done.wait_for(200ms) == std::future_status::ready));

            // we obtain the last write lock
            BOOST_TEST(wait_for([&]{ return waiting_step.load() == 1; }, 200ms));
            waiting_step = 2;
            BOOST_TEST((waiting.wait_for(200ms) == std::future_status::ready));
        };

    if(two_step.load() == 1)
    {
        next_steps(two_step, write_2, three_step, write_3);
    }
    else
    {
        next_steps(three_step, write_3, two_step, write_2);
    }
}

BOOST_AUTO_TEST_CASE(prefer_exclusive_to_non_exclusive_write_locks)
{
    using namespace std::literals::chrono_literals;
    WPUSMutex mutex;

    WPUSMutex::Lock read_lock;
    mutex.ReadLock( read_lock );
    BOOST_TEST((read_lock.GetLockType() == WPUSMutex::Lock::Type::read));
    
    // read lock needed for later (*) as we can't obtain a read lock for try write
    // after a write lock is pending
    WPUSMutex::Lock lock;
    mutex.ReadLock( lock );

    auto maybe_write =
        std::async(
            std::launch::async,
            [&mutex]
            {
                WPUSMutex::Lock read_lock;
                mutex.ReadLock( read_lock );
                BOOST_TEST((read_lock.GetLockType() == WPUSMutex::Lock::Type::read));

                // provide read lock to write lock to make sure that write lock
                // that won't step back on dead lock will not randomly be obtained
                // before it as write lock without read lock has no ordering side
                // effects so we allow such race conditions
                bool locked = mutex.TryWriteLock( read_lock );
                // make sure that we didn't obtain the lock
                BOOST_TEST( locked == false );
                BOOST_TEST((read_lock.GetLockType() == WPUSMutex::Lock::Type::read));
            });

    // make sure that write lock can't be obtained as we are already holding a
    // read lock but that it is waiting to be obtained
    BOOST_TEST((maybe_write.wait_for(200ms) == std::future_status::timeout));

    auto write =
        std::async(
            std::launch::async,
            [&mutex]
            {
                WPUSMutex::Lock write_lock = mutex.WriteLock();
                BOOST_TEST((write_lock.GetLockType() == WPUSMutex::Lock::Type::write));
            });

    // make sure that now both write locks are waiting
    BOOST_TEST((maybe_write.wait_for(200ms) == std::future_status::timeout));
    BOOST_TEST((write.wait_for(200ms) == std::future_status::timeout));

    // (*) since a write lock is already pending this maybe write lock will not even try to wait
    bool locked = mutex.TryWriteLock(lock);
    BOOST_TEST( locked == false );
    BOOST_TEST((lock.GetLockType() == WPUSMutex::Lock::Type::read));

    read_lock.Release();
    lock.Release();

    maybe_write.wait();
    write.wait();
}

BOOST_AUTO_TEST_SUITE_END()
