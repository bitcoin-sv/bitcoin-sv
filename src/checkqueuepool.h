// Copyright (c) 2019 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#ifndef BITCOIN_CHECKQUEUEPOOL_H
#define BITCOIN_CHECKQUEUEPOOL_H

#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>

#include <boost/thread/thread.hpp>

#include "checkqueue.h"
#include "util.h"

namespace checkqueue
{

/**
 * A pool of CCheckQueue instances that run checks on multiple threads.
 *
 * Pool's constructor defines the max amount of checkers that will be available.
 * We can request CheckQueue-s from the pool by calling GetChecker which returns
 * a scoped checker handle. When the handle goes out of scope checker is
 * returned to the pool - if checking is still active at that point it is
 * terminated before checker is returned to the pool.
 * If there are no free queues, we block.
 *
 * The verifications are represented by a type T, which must provide an
 * operator(), returning a bool.
 */
template<typename T>
class CCheckQueuePool
{
public:
    /**
     * RAII class similar to CCheckQueueControl (which is used as a member)
     * with addition that it automatically returns the CCheckQueue instance back
     * to the pool after it gets out of scope (destructor is called).
     *
     * NOTE: CCheckQueueScopeGuard is expected to have a shorter lifespan than
     *       the owning CCheckQueuePool instance.
     */
    class CCheckQueueScopeGuard
    {
    public:
        CCheckQueueScopeGuard()
            : mPool{nullptr}
            , mQueue{nullptr}
            , mControl{nullptr}
        {/**/}

        CCheckQueueScopeGuard(CCheckQueueScopeGuard&& other) noexcept
            : mPool{other.mPool}
            , mQueue{other.mQueue}
            , mControl{std::move(other.mControl)}
        {
            other.mPool = nullptr;
            other.mQueue = nullptr;
        }

        CCheckQueueScopeGuard& operator=(CCheckQueueScopeGuard&&) = delete;
        CCheckQueueScopeGuard(const CCheckQueueScopeGuard&) = delete;
        CCheckQueueScopeGuard& operator=(const CCheckQueueScopeGuard&) = delete;

        bool Wait() {return mControl.Wait();}
        void Add(std::vector<T>& checks) {mControl.Add(checks);}

        ~CCheckQueueScopeGuard()
        {
            if(mPool)
            {
                mPool->ReturnQueueToPool(*mQueue);
            }
        }

    private:
        friend class CCheckQueuePool<T>;

        CCheckQueueScopeGuard(CCheckQueuePool* pool, CCheckQueue<T>* queue)
            : mPool{pool}
            , mQueue{queue}
            , mControl{queue}
        {/**/}

        CCheckQueuePool* mPool;
        CCheckQueue<T>* mQueue;
        CCheckQueueControl<T> mControl;
    };

    CCheckQueuePool(
        size_t poolSize,
        boost::thread_group& threadGroup,
        size_t threadCount,
        unsigned int batchSize)
    {
        assert(poolSize);
        assert(batchSize);

        mScriptCheckQueue.reserve(poolSize);

        for(size_t queueNum=0; queueNum<poolSize; ++queueNum)
        {
            auto& queue =
                mScriptCheckQueue.emplace_back(
                    std::make_unique<CCheckQueue<T>>(batchSize));

            SpawnQueueWorkerThreads(*queue, threadGroup, threadCount, queueNum);

            mIdleQueues.push(queue.get());
        }
    }

    /**
     * Returns an instance of the checker.
     *
     * NOTE: Function blocks if no idle checkers are in the queue and waits
     *       until one is returned, then it returns the handle.
     */
    CCheckQueueScopeGuard GetChecker()
    {
        std::unique_lock lock{mIdleQueuesLock};
        mIdleQueuesCV.wait(lock, [this]{return !mIdleQueues.empty();});

        CCheckQueueScopeGuard handle{this, mIdleQueues.front()};
        mIdleQueues.pop();

        return handle;
    }

private:
    void ReturnQueueToPool(CCheckQueue<T>& queue)
    {
        {
            std::lock_guard lock{mIdleQueuesLock};
            mIdleQueues.push(&queue);
        }

        mIdleQueuesCV.notify_one();
    }

    void SpawnQueueWorkerThreads(
        CCheckQueue<T>& queue,
        boost::thread_group& threadGroup,
        size_t threadCount,
        size_t queueNum)
    {
        for(size_t workerNum=0; workerNum<threadCount; ++workerNum)
        {
            threadGroup.create_thread(
                [&queue, workerNum, queueNum]()
                {
                    RenameThread(
                        strprintf(
                            "bitcoin-scriptch_%d_%d",
                            queueNum,
                            workerNum).c_str());
                    queue.Thread();
                });
        }
    }

    std::mutex mIdleQueuesLock;
    std::condition_variable mIdleQueuesCV;
    std::queue<CCheckQueue<T>*> mIdleQueues;
    std::vector<std::unique_ptr<CCheckQueue<T>>> mScriptCheckQueue;
};

}

#endif // BITCOIN_CHECKQUEUEPOOL_H