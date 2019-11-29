// Copyright (c) 2015-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "scheduler.h"

#include "random.h"
#include "reverselock.h"
#include "util.h"

#include <boost/bind.hpp>
#include <cassert>
#include <chrono>
#include <thread>
#include <utility>

CScheduler::CScheduler()
    : nThreadsServicingQueue(0), stopRequested(false), stopWhenEmpty(false) {}

CScheduler::~CScheduler()
{
    stop();
}

#if BOOST_VERSION < 105000
static boost::system_time
toPosixTime(const boost::chrono::system_clock::time_point &t) {
    return boost::posix_time::from_time_t(
        boost::chrono::system_clock::to_time_t(t));
}
#endif

void CScheduler::serviceQueue() {
    boost::unique_lock<boost::mutex> lock(newTaskMutex);

    // newTaskMutex is locked throughout this loop EXCEPT when the thread is
    // waiting or when the user's function is called.
    while (!shouldStop()) {
        if (!shouldStop() && taskQueue.empty()) {
            reverse_lock<boost::unique_lock<boost::mutex>> rlock(lock);
            // Use this chance to get a tiny bit more entropy
            RandAddSeedSleep();
        }
        while (!shouldStop() && taskQueue.empty()) {
            // Wait until there is something to do.
            newTaskScheduled.wait(lock);
        }

// Wait until either there is a new task, or until the time of the first item on
// the queue:

// wait_until needs boost 1.50 or later; older versions have timed_wait:
#if BOOST_VERSION < 105000
        while (!shouldStop() && !taskQueue.empty() &&
               newTaskScheduled.timed_wait(
                   lock, toPosixTime(taskQueue.begin()->first))) {
            // Keep waiting until timeout
        }
#else
        // Some boost versions have a conflicting overload of wait_until
        // that returns void. Explicitly use a template here to avoid
        // hitting that overload.
        while (!shouldStop() && !taskQueue.empty()) {
            boost::chrono::system_clock::time_point timeToWaitFor =
                taskQueue.begin()->first;
            if (newTaskScheduled.wait_until<>(lock, timeToWaitFor) ==
                boost::cv_status::timeout) {
                // Exit loop after timeout, it means we reached the time of
                // the event
                break;
            }
        }
#endif
        // If there are multiple threads, the queue can empty while we're
        // waiting (another thread may service the task we were waiting on).
        if (shouldStop() || taskQueue.empty()) continue;

        Function f = taskQueue.begin()->second;
        taskQueue.erase(taskQueue.begin());

        {
            // Unlock before calling f, so it can reschedule itself or
            // another task without deadlocking:
            reverse_lock<boost::unique_lock<boost::mutex>> rlock(lock);
            f();
        }
    }

    newTaskScheduled.notify_one();
}

void CScheduler::stop(bool drain) {
    {
        boost::unique_lock<boost::mutex> lock(newTaskMutex);
        if (drain)
            stopWhenEmpty = true;
        else
            stopRequested = true;
    }
    newTaskScheduled.notify_all();

    std::chrono::steady_clock::time_point begin =
        std::chrono::steady_clock::now();
    using namespace std::chrono_literals;

    // Try to gracefully terminate running threads
    //
    // serviceQueue function calls were most likely terminated beforehand by
    // boost::thread_pool interrupt_all call but we should be certain that it
    // really exited
    while(std::chrono::steady_clock::now() - begin < 10s)
    {
        if(nThreadsServicingQueue == 0)
        {
            break;
        }

        std::this_thread::sleep_for(100ms);
    }

    if(nThreadsServicingQueue != 0)
    {
        LogPrintf(
            "WARNING: CScheduler workers did not exit within allotted time,"
            " continuing with exit.\n");
    }
}

void CScheduler::schedule(CScheduler::Function f,
                          boost::chrono::system_clock::time_point t) {
    {
        boost::unique_lock<boost::mutex> lock(newTaskMutex);
        taskQueue.insert(std::make_pair(t, f));
    }
    newTaskScheduled.notify_one();
}

void CScheduler::scheduleFromNow(CScheduler::Function f,
                                 int64_t deltaMilliSeconds) {
    schedule(f,
             boost::chrono::system_clock::now() +
                 boost::chrono::milliseconds(deltaMilliSeconds));
}

static void Repeat(CScheduler *s, CScheduler::Function f,
                   int64_t deltaMilliSeconds) {
    f();
    s->scheduleFromNow(boost::bind(&Repeat, s, f, deltaMilliSeconds),
                       deltaMilliSeconds);
}

void CScheduler::scheduleEvery(CScheduler::Function f,
                               int64_t deltaMilliSeconds) {
    scheduleFromNow(boost::bind(&Repeat, this, f, deltaMilliSeconds),
                    deltaMilliSeconds);
}

size_t
CScheduler::getQueueInfo(boost::chrono::system_clock::time_point &first,
                         boost::chrono::system_clock::time_point &last) const {
    boost::unique_lock<boost::mutex> lock(newTaskMutex);
    size_t result = taskQueue.size();
    if (!taskQueue.empty()) {
        first = taskQueue.begin()->first;
        last = taskQueue.rbegin()->first;
    }
    return result;
}

void CScheduler::startServiceThread(boost::thread_group& threadGroup)
{
    ++nThreadsServicingQueue;
    threadGroup.create_thread(
        [this]()
        {
            try
            {
                TraceThread("scheduler", [this]{serviceQueue();});
            }
            catch(...)
            {
                --nThreadsServicingQueue;
                throw;
            }

            --nThreadsServicingQueue;
        });
}
