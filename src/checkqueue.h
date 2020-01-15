// Copyright (c) 2012-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CHECKQUEUE_H
#define BITCOIN_CHECKQUEUE_H

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <boost/thread/thread.hpp>
#include <boost/thread/condition_variable.hpp>
#include <boost/thread/locks.hpp>
#include <boost/thread/mutex.hpp>

#include "taskcancellation.h"
#include "util.h"

/**
 * Queue for verifications that have to be performed.
 * The verifications are represented by a type T, which must provide operator
 * `std::optional<bool> operator()(const task::CCancellationToken&)`. Returning
 * an empty optional indicates that the validation was canceled.
 *
 * One thread (the master) is assumed to push batches of verifications onto the
 * queue, where they are processed by N-1 worker threads. When the master is
 * done adding work, it temporarily joins the worker pool as an N'th worker,
 * until all jobs are done.
 *
 * NOTE: This class is intended to be used through CCheckQueuePool and not by
 *       itself.
 */
template <typename T> class CCheckQueue {
private:
    // make sure that T provides expected function signature and return type
    static_assert(
        std::is_same_v<std::optional<bool>,
        decltype(std::declval<T>()(std::declval<task::CCancellationToken>()))>);
    /**
     * Scope guard that makes sure that even if an exception is thrown inside
     * Loop() (e.g. by cond.wait(lock);) the worker count will be correct.
     */
    class CTotalScopeGuard
    {
    public:
        CTotalScopeGuard(
            boost::mutex& mutex,
            int& counter)
            : mMutex{mutex}
            , mCounter{counter}
        {/**/}

        ~CTotalScopeGuard()
        {
            if(mRunning)
            {
                boost::unique_lock<boost::mutex> lock{mMutex};
                --mCounter;
            }
        }

        CTotalScopeGuard(CTotalScopeGuard&&) = delete;
        CTotalScopeGuard& operator=(CTotalScopeGuard&&) = delete;
        CTotalScopeGuard(const CTotalScopeGuard&) = delete;
        CTotalScopeGuard& operator=(const CTotalScopeGuard&) = delete;

        void runNL()
        {
            ++mCounter;
            mRunning = true;
        }

    private:
        boost::mutex& mMutex;
        int& mCounter;
        bool mRunning = false;
    };

    //! Mutex to protect the inner state
    boost::mutex mutex;

    //! Worker threads block on this when out of work
    boost::condition_variable condWorker;

    //! Master thread blocks on this when out of work
    boost::condition_variable condMaster;

    //! The queue of elements to be processed.
    //! As the order of booleans doesn't matter, it is used as a LIFO (stack)
    std::vector<T> queue;

    //! The number of workers (including the master) that are idle.
    int nIdle = 0;

    //! The total number of workers (including the master).
    int nTotal = 0;

    /**
     * The total number of workers that were spawned on separate threads (even
     * those that were spawned but have not yet been run by the thread pool)
     * used for graceful shutdown
     */
    std::atomic<int> mSpawnedWorkersCount = 0;
    /**
     * Used in destructor for graceful shutdown to notify worker threads that
     * they should quit.
     */
    bool mQuit = false;

    //! The temporary evaluation result.
    std::optional<bool> fAllOk = true;

    /**
     * Number of verifications that haven't completed yet.
     * This includes elements that are no longer queued, but still in the
     * worker's own batches.
     */
    unsigned int nTodo = 0;

    std::optional<task::CCancellationToken> mSessionToken;

    //! The maximum number of elements to be processed in one batch
    unsigned int nBatchSize;

    /**
     * flag that is used only to enforce that after wait is called/object is
     * moved we are no longer allowed to call `Add()`
     */
    bool mWaitCalled = true;

    /** Internal function that does bulk of the verification work. */
    std::optional<bool> Loop(bool fMaster = false)
    {
        boost::condition_variable &cond = fMaster ? condMaster : condWorker;
        std::vector<T> vChecks;
        vChecks.reserve(nBatchSize);
        unsigned int nNow = 0;
        std::optional<bool> fOk = true;
        CTotalScopeGuard guard{mutex, nTotal};

        do {
            {
                boost::unique_lock<boost::mutex> lock(mutex);
                // first do the clean-up of the previous loop run (allowing us
                // to do it in the same critsect)
                if (nNow) {
                    if(fAllOk.has_value() && fOk.has_value())
                    {
                        fAllOk.value() &= fOk.value();
                    }
                    else
                    {
                        fAllOk = {};
                    }

                    nTodo -= nNow;

                    if (mSessionToken->IsCanceled())
                    {
                        // drain remaining work from the queue (there can still
                        // be some work in other workers)
                        nTodo -= queue.size();
                        queue.clear();
                    }

                    if (nTodo == 0 && !fMaster)
                    {
                        // We processed the last element; inform the master it
                        // can exit and return the result
                        condMaster.notify_one();
                    }
                }
                else
                {
                    // first iteration
                    guard.runNL();
                }

                // logically, the do loop starts here
                while (queue.empty())
                {
                    if(mQuit)
                    {
                        return {};
                    }

                    // master will exit only after all the work has been done
                    // so we need to wait for all the workers to finish at which
                    // point nTodo == 0 is true
                    if (fMaster && nTodo == 0)
                    {
                        // At this point we know that all the workers have
                        // completed their tasks (nTodo == 0) so we can return
                        // the result without the fear of some workers still
                        // being active from current session once the next
                        // session starts
                        if (mSessionToken->IsCanceled())
                        {
                            fAllOk = {};
                        }

                        // return the current status
                        return fAllOk;
                    }

                    // increment idle counter and decrement it once the guard goes out of scope
                    nIdle++;
                    auto decrement = [](int* counter) {--(*counter);};
                    std::unique_ptr<int, decltype(decrement)> idleGuard{&nIdle, decrement};

                    // wait for either more work to continue processing
                    // or thread termination in which case this function exits
                    // with an exception that is automatically caught inside
                    // boost thread object before thread termination inside join
                    cond.wait(lock);
                }
                // Decide how many work units to process now.
                // * Do not try to do everything at once, but aim for
                // increasingly smaller batches so all workers finish
                // approximately simultaneously.
                // * Try to account for idle jobs which will instantly start
                // helping.
                // * Don't do batches smaller than 1 (duh), or larger than
                // nBatchSize.
                nNow = std::max(1U, std::min(nBatchSize,
                                             (unsigned int)queue.size() /
                                                 (nTotal + nIdle + 1)));
                auto it = std::next(queue.begin(), nNow);
                std::move(queue.begin(), it, std::back_inserter(vChecks));
                queue.erase(queue.begin(), it);
                // Check whether we need to do work at all
                fOk = fAllOk;
            }
            // execute work
            for (T &check : vChecks) {
                if (!fOk.has_value() || !fOk.value() || mSessionToken->IsCanceled())
                {
                    break;
                }

                fOk = check(*mSessionToken);
            }
            vChecks.clear();
        } while (true);
    }

public:
    //! Create a new check queue
    CCheckQueue(unsigned int nBatchSizeIn)
        : nBatchSize(nBatchSizeIn) {}

    CCheckQueue(
        unsigned int nBatchSizeIn,
        boost::thread_group& threadGroup,
        size_t workerThreadCount,
        const std::string& baseThreadName)
        : nBatchSize{nBatchSizeIn}
    {
        // spawn worker threads
        for(size_t workerNum=0; workerNum<workerThreadCount; ++workerNum)
        {
            ++mSpawnedWorkersCount;
            threadGroup.create_thread(
                [this, baseThreadName, workerNum]()
                {
                    try
                    {
                        TraceThread(
                            (baseThreadName + '_' + std::to_string(workerNum)).c_str(),
                            [this]{Loop();});
                    }
                    catch(...)
                    {
                        --mSpawnedWorkersCount;
                        throw;
                    }

                    --mSpawnedWorkersCount;
                });
        }
    }

    ~CCheckQueue()
    {
        {
            boost::unique_lock lock{mutex};
            mQuit = true;
            condWorker.notify_all();
        }

        std::chrono::steady_clock::time_point begin =
            std::chrono::steady_clock::now();
        using namespace std::chrono_literals;

        // Try to gracefully terminate running threads
        //
        // 10s is the longest duration that we expect one script OP code to take
        // before checking mQuit (session token is assumed to already being
        // canceled at this point) so we wait for 20s which should be more than
        // enough.
        while(std::chrono::steady_clock::now() - begin < 20s)
        {
            if(mSpawnedWorkersCount == 0)
            {
                break;
            }

            std::this_thread::sleep_for(100ms);
        }

        if(mSpawnedWorkersCount != 0)
        {
            // This can result in crash during shutdown, if threads are still
            // accessing memory associated with *this
            LogPrintf(
                "WARNING: CCheckQueue workers did not exit within allotted time"
                ", continuing with exit.\n");
        }
    }

    /**
     * Wait until execution finishes, and return whether all evaluations were
     * successful. In case of early validation termination an empty optional is
     * returned.
     *
     * NOTE: StartCheckingSession, Add and Wait are not thread safe and should
     *       be called from the same thread or the caller should make sure to
     *       handle thread synchronization.
     */
    std::optional<bool> Wait()
    {
        if (!mSessionToken)
        {
            throw std::runtime_error("Session token not set!");
        }

        mWaitCalled = true;

        return Loop(true);
    }

    /**
     * Add a batch of checks to the queue. Add can not be performed before a
     * session is opened and can't be performed after `Wait()` has been called.
     *
     * NOTE: StartCheckingSession, Add and Wait are not thread safe and should
     *       be called from the same thread or the caller should make sure to
     *       handle thread synchronization.
     */
    void Add(std::vector<T> &vChecks)
    {
        if (!mSessionToken)
        {
            throw std::runtime_error("Session token not set!");
        }
        else if (mWaitCalled)
        {
            throw std::runtime_error("Add() called after Wait()!");
        }

        boost::unique_lock<boost::mutex> lock(mutex);

        for (T &check : vChecks) {
            queue.push_back(std::move(check));
        }
        nTodo += vChecks.size();
        if (vChecks.size() == 1) {
            condWorker.notify_one();
        } else if (vChecks.size() > 1) {
            condWorker.notify_all();
        }
    }

    bool IsIdle() {
        boost::unique_lock<boost::mutex> lock(mutex);
        return (nTotal == nIdle && nTodo == 0);
    }

    /**
     * Start new checking session - must be called before Add/Wait.
     *
     * New session can be started only after IsIdle() is true.
     *
     * NOTE: StartCheckingSession, Add and Wait are not thread safe and should
     *       be called from the same thread or the caller should make sure to
     *       handle thread synchronization.
     */
    void StartCheckingSession(task::CCancellationToken&& token)
    {
        if(!mWaitCalled || !IsIdle())
        {
            throw std::runtime_error("Session already in progress!");
        }

        boost::unique_lock<boost::mutex> lock(mutex);

        mSessionToken = token;
        fAllOk = true;
        mWaitCalled = false;
    }
};

#endif // BITCOIN_CHECKQUEUE_H
