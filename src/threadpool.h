// Copyright (c) 2018-2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#include "task.h"

/**
* An adaptor to provide a uniform interface to an unordered task queue.
* For use with a thread pool.
*
* This adaptor provides constant time queuing and unqueuing of tasks
* in a thread pool, but no prioritisation of those tasks.
*/
class CQueueAdaptor
{
  public:

    // Are we empty?
    bool empty() const { return mTasks.empty(); }

    // Push a task onto the queue
    void push(CTask&& task) { mTasks.emplace(std::move(task)); }

    // Pop and return the next task from the queue
    CTask pop()
    {
        CTask task { std::move(mTasks.front()) };
        mTasks.pop();
        return task;
    }

  private:

    // The (unordered) queue of tasks
    std::queue<CTask> mTasks {};
};

/**
* An adaptor to provide a uniform interface to a sorted task queue.
* For use with a thread pool.
*
* This adaptor provides logarithmic complexity queuing and unqueuing of tasks
* in a thread pool, with prioritised execution order of those tasks.
*/
class CPriorityQueueAdaptor
{
  public:

    // Are we empty?
    bool empty() const { return mTasks.empty(); }

    // Push a task onto the queue
    void push(CTask&& task) { mTasks.emplace(std::move(task)); }

    // Pop and return the next task from the queue
    CTask pop()
    {
        CTask task { std::move(mTasks.top()) };
        mTasks.pop();
        return task;
    }

  private:

    // The sorted queue of tasks
    std::priority_queue<CTask> mTasks {};
};


/**
* A thread pool class. Can be constructed with however many threads you
* require and then tasks to be run by the pool submitted by calling submit().
*
* Templated on the underlying queue type (sorted for priority or unsorted for
* more efficient queue handling).
*
* Any callable object can be submitted (function, class method, lambda) with
* any arguments and any return type. The result is returned in a future.
*/
template<typename QueueAdapter>
class CThreadPool final
{
  public:

    // Constructor
    CThreadPool(const std::string& owner, size_t numThreads = std::thread::hardware_concurrency());

    // Destructor
    ~CThreadPool();

    // Forbid copying/moving
    CThreadPool(const CThreadPool&) = delete;
    CThreadPool(CThreadPool&&) = delete;
    CThreadPool& operator=(const CThreadPool&) = delete;
    CThreadPool& operator=(CThreadPool&&) = delete;

    // Query size of the pool.
    size_t getPoolSize() const { return mThreads.size(); }

    // Submit a task to the pool.
    void submit(CTask&& task);

    // Pause thread pool processing.
    void pause();
    // Continue thread pool processing (unpause).
    void run();
    // Get whether we are paused.
    bool paused() const;

  private:

    // Worker thread entry point
    void worker(size_t n);

    // The task queue
    QueueAdapter mQueue {};
    mutable std::mutex mQueueMtx {};
    std::condition_variable mQueueCondVar {};

    // The worker threads
    std::vector<std::shared_ptr<std::thread>> mThreads {};

    // Flag to indicate we are shutting down
    bool mRunning {true};

    // Flag to indicate we are paused
    bool mPaused {false};

    // Owner string for logging
    const std::string mOwnerStr {};

};

#include "threadpoolT.h"
