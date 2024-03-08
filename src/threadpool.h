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
#include "threadpriority.h"

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
    [[nodiscard]] bool empty() const { return mTasks.empty(); }
    // Get size
    size_t size() const { return mTasks.size(); }

    // Push a task onto the queue
    void push(CTask&& task) { mTasks.emplace(std::move(task)); }

    // Pop and return the next task from the queue
    CTask pop(ThreadPriority)
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
* An adaptor to provide a uniform interface to dual unordered task queue.
* For use with a thread pool.
*
* This adaptor provides constant time queuing and unqueuing of high and low priority tasks
* in a thread pool, but no prioritisation of those tasks.
*
* Properties:
* 1. High priority tasks are queued into mStdTasks.
* 2. Low priority tasks are queued into mNonStdTasks.
* 3. If there are no tasks in the mStdTasks the pool processes existing tasks from the mNonStdTasks.
* 4. If there are no tasks in the mNonStdTasks the pool processes existing tasks from the mStdTasks.
*
* This approach allows to execute tasks from the both queues (independently) by low and high priority
* threads from the pool.
*/
class CDualQueueAdaptor
{
  public:

    // Are we empty?
    [[nodiscard]] bool empty() const
    {
        return mStdTasks.empty() && mNonStdTasks.empty();
    }

    // Get size
    size_t size() const
    {
        return mStdTasks.size() + mNonStdTasks.size();
    }

    // Push a task onto the queue
    void push(CTask&& task)
    {
        // Convert it back into CTask::Priority
        auto taskPriority = static_cast<CTask::Priority>(task.getPriority());
        if (CTask::Priority::High == taskPriority) {
            mStdTasks.emplace(std::move(task));
        }
        else if (CTask::Priority::Low == taskPriority) {
            mNonStdTasks.emplace(std::move(task));
        }
    }

    // Pop and return the next task from the queue
    CTask pop(ThreadPriority thrPriority)
    {
        CTask task {};
        if (ThreadPriority::High == thrPriority) {
            if (!mStdTasks.empty()) {
                task = std::move(mStdTasks.front());
                mStdTasks.pop();
            }
            else if (!mNonStdTasks.empty()) {
                task = std::move(mNonStdTasks.front());
                mNonStdTasks.pop();
            }
        }
        else if (ThreadPriority::Low == thrPriority) {
            if (!mNonStdTasks.empty()) {
                task = std::move(mNonStdTasks.front());
                mNonStdTasks.pop();
            }
            else if (!mStdTasks.empty()) {
                task = std::move(mStdTasks.front());
                mStdTasks.pop();
            }
        }
        return task;
    }

  private:
    // The (unordered) queues of tasks
    std::queue<CTask> mStdTasks {};
    std::queue<CTask> mNonStdTasks {};
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
    [[nodiscard]] bool empty() const { return mTasks.empty(); }
    // Get size
    size_t size() const { return mTasks.size(); }

    // Push a task onto the queue
    void push(CTask&& task) { mTasks.emplace(std::move(task)); }

    // Pop and return the next task from the queue
    CTask pop(ThreadPriority)
    {
        CTask task { mTasks.top() }; // NOTE: CTask object must be copied because std::priority_queue::top() returns a const_reference.
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
* The class template allows to instantiate a thread pool with a defined number of piority threads:
* - normal priority threads (default)
* - high and low priority threads
*
* Templated on the underlying queue type (sorted for priority or unsorted for
* more efficient queue handling).
*
* Any callable object can be submitted (function, class method, lambda) with
* any arguments and any return type. The result is returned in a future.
*
*/
template<typename QueueAdapter>
class CThreadPool final
{
  public:

    // Constructor
    CThreadPool(bool logMsgs, const std::string& owner, size_t numThreads = std::thread::hardware_concurrency());
    CThreadPool(bool logMsgs, const std::string& owner, size_t numHighPriorityThrs, size_t numLowPriorityThrs);

    // Destructor
    ~CThreadPool();

    // Forbid copying/moving
    CThreadPool(const CThreadPool&) = delete;
    CThreadPool(CThreadPool&&) = delete;
    CThreadPool& operator=(const CThreadPool&) = delete;
    CThreadPool& operator=(CThreadPool&&) = delete;

    // Query size of the pool.
    size_t getPoolSize() const { return mThreads.size(); }
    // Query number of queued tasks
    size_t getTaskDepth() const;

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
    void worker(size_t n, ThreadPriority thrPriority);

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
    bool mLogMsgs {true};

};

#include "threadpoolT.h"
