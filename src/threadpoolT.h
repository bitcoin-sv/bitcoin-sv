// Copyright (c) 2018-2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "logging.h"
#include "util.h"

// Constructor
template<typename QueueAdaptor>
CThreadPool<QueueAdaptor>::CThreadPool(const std::string& owner, size_t numThreads)
: mOwnerStr{owner}
{
    // Launch our workers
    mThreads.reserve(numThreads);
    for(size_t i = 0; i < numThreads; ++i)
    {
        mThreads.
            emplace_back(
                    std::make_shared<std::thread>(&CThreadPool::worker, this, i, ThreadPriority::Normal));
    }
}

template<typename QueueAdaptor>
CThreadPool<QueueAdaptor>::CThreadPool(const std::string& owner, size_t numHighPriorityThrs, size_t numLowPriorityThrs)
: mOwnerStr{owner}
{
    // Launch our workers
    mThreads.reserve(numHighPriorityThrs + numLowPriorityThrs);
    for(size_t i = 0; i < numHighPriorityThrs + numLowPriorityThrs; ++i)
    {
        mThreads.
            emplace_back(
                std::make_shared<std::thread>(
                    &CThreadPool::worker, this, i, i < numHighPriorityThrs ? ThreadPriority::High : ThreadPriority::Low));
    }
}

// Destructor
template<typename QueueAdaptor>
CThreadPool<QueueAdaptor>::~CThreadPool()
{
    {
        // Wake everyone up
        std::unique_lock<std::mutex> lock { mQueueMtx };
        mRunning = false;
        mQueueCondVar.notify_all();
    }

    // Reap all the workers
    for(auto& thread: mThreads)
    {
        thread->join();
    }
    mThreads.clear();
}

// The worker threads
template<typename QueueAdaptor>
void CThreadPool<QueueAdaptor>::worker(size_t n, ThreadPriority thrPriority)
{
    std::string s = strprintf("bitcoin-worker%d-%s-%s", n, enum_cast<std::string>(thrPriority), mOwnerStr.c_str());
    RenameThread(s.c_str());
    LogPrintf("%s ThreadPool thread %d starting\n", mOwnerStr.c_str(), n);
    while(mRunning)
    {
        CTask task {};

        {
            // Wait for work (or termination)
            std::unique_lock<std::mutex> lock { mQueueMtx };
            mQueueCondVar.wait(lock,
                [this]() { return !mRunning || (!mQueue.empty() && !mPaused); }
            );

            if(!mRunning)
                break;

            // Pop next task
            task = std::move(mQueue.pop(thrPriority));
        }

        // Run task
        task();
    }

    LogPrintf("%s ThreadPool thread %d stopping\n", mOwnerStr.c_str(), n);
}

// Submit a task to the pool.
template<typename QueueAdaptor>
void CThreadPool<QueueAdaptor>::submit(CTask&& task)
{
    std::unique_lock<std::mutex> lock { mQueueMtx };

    if(!mRunning)
    {   
        // Don't allow submitting new tasks when we're stopping
        throw std::runtime_error("Submitting to stopped " + mOwnerStr + " ThreadPool");
    }

    mQueue.push(std::move(task));
    mQueueCondVar.notify_one();
}

// Pause thread pool processing.
template<typename QueueAdaptor>
void CThreadPool<QueueAdaptor>::pause()
{
    std::unique_lock<std::mutex> lock { mQueueMtx };
    mPaused = true;
}

// Continue thread pool processing (unpause).
template<typename QueueAdaptor>
void CThreadPool<QueueAdaptor>::run()
{
    std::unique_lock<std::mutex> lock { mQueueMtx };
    mPaused = false;

    // On un-pause, continue processing
    mQueueCondVar.notify_all();
}

// Get whether we are paused.
template<typename QueueAdaptor>
bool CThreadPool<QueueAdaptor>::paused() const
{
    std::unique_lock<std::mutex> lock { mQueueMtx };
    return mPaused;
}

