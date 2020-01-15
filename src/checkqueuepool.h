// Copyright (c) 2019 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#ifndef BITCOIN_CHECKQUEUEPOOL_H
#define BITCOIN_CHECKQUEUEPOOL_H

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
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
 * If there are no free queues, we check whether there is a checker being used
 * that has lower priority than the currently requesting checker and demand
 * it's premature termination. After that we block until one of the checkers
 * is freed up.
 *
 * The verifications are represented by a type T, which must provide operator
 * `std::optional<bool> operator()(const task::CCancellationToken&)`. Returning
 * an empty optional indicates that the validation was canceled.
 *
 * The termination of checker with lower priority is controlled by ValueT type
 * which value is provided as parameter to GetChecker() function. ValueT type
 * must be a Sortable concept (provide comparison operators). In case two ValueT
 * are equal a tie breaker is `mValidationStartTime` - elapsed time from the
 * start of validation where longer means worse/lower priority.
 *
 * Current use example:
 * For T of type `CScriptCheck`, ValueT represents CBlockIndex::nChainWork of
 * the tip to which we are moving as longer chains for us represent higher
 * priority.
 */
template<typename T, typename ValueT>
class CCheckQueuePool
{
public:
    /**
     * RAII class for CCheckQueue that automatically returns the CCheckQueue
     * instance back to the pool after it gets out of scope (destructor is
     * called) or validation finishes and Wait() was called - whichever happens
     * first.
     *
     * NOTE: CCheckQueueScopeGuard is expected to have a shorter lifespan than
     *       the owning CCheckQueuePool instance.
     */
    class CCheckQueueScopeGuard
    {
    public:
        CCheckQueueScopeGuard()
            : mResult{true}
            , mPool{nullptr, CNullDestructor<CCheckQueuePool>{}}
            , mQueue{nullptr, CNullDestructor<CCheckQueue<T>>{}}
        {/**/}

        CCheckQueueScopeGuard(CCheckQueueScopeGuard&&) = default;
        CCheckQueueScopeGuard& operator=(CCheckQueueScopeGuard&&) = default;

        CCheckQueueScopeGuard(const CCheckQueueScopeGuard&) = delete;
        CCheckQueueScopeGuard& operator=(const CCheckQueueScopeGuard&) = delete;

        ~CCheckQueueScopeGuard()
        {
            if(mPool)
            {
                mScopeExitedSource->Cancel();
                mQueue->Wait();
                mPool->ReturnQueueToPool(*mQueue);
            }
        }

        /**
         * Wait until execution finishes, and return whether all evaluations were
         * successful. In case of early validation termination an empty optional is
         * returned.
         *
         * NOTE: Add and Wait are not thread safe and should be called from the
         *       same thread or the caller should make sure to handle thread
         *       synchronization.
         */
        std::optional<bool> Wait()
        {
            if (!mPool)
            {
                return mResult;
            }
            mResult = mQueue->Wait();

            mPool->ReturnQueueToPool(*mQueue);
            mPool.reset();
            mQueue.reset();

            return mResult;
        }

        /**
         * Add validation task. Task can not be added after `Wait()` is called
         *
         * NOTE: Add and Wait are not thread safe and should be called from the
         *       same thread or the caller should make sure to handle thread
         *       synchronization.
         */
        void Add(std::vector<T>& vChecks)
        {
            if (!mPool)
            {
                // possible cause:
                // 1) object created with default constructor
                // 2) Add called after Wait has already been called
                throw std::runtime_error("Null object!");
            }

            mQueue->Add(vChecks);
        }

    private:
        friend class CCheckQueuePool;

        CCheckQueueScopeGuard(
            CCheckQueuePool* pool,
            CCheckQueue<T>* queue,
            task::CCancellationToken&& token)
            : mScopeExitedSource{task::CCancellationSource::Make()}
            , mPool{pool, CNullDestructor<CCheckQueuePool>{}}
            , mQueue{queue, CNullDestructor<CCheckQueue<T>>{}}
        {
            mQueue->StartCheckingSession(
                task::CCancellationToken::JoinToken(
                    token, mScopeExitedSource->GetToken()));
        }

        // Does nothing - only needed so we can have default move constructor
        // and assignment operator (required by VisualC++ as copy elision
        // doesn't work through ternary operator so we need a without move
        // constructor)
        template<typename Ptr>
        struct CNullDestructor{void operator()(Ptr*){}};

        std::shared_ptr<task::CCancellationSource> mScopeExitedSource;
        std::optional<bool> mResult;
        std::unique_ptr<CCheckQueuePool, CNullDestructor<CCheckQueuePool>> mPool;
        std::unique_ptr<CCheckQueue<T>, CNullDestructor<CCheckQueue<T>>> mQueue;
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

        constexpr auto baseThreadName{"bitcoin-scriptch_"};
        for(size_t queueNum=0; queueNum<poolSize; ++queueNum)
        {
            auto& queue =
                mScriptCheckQueue.emplace_back(
                    std::make_unique<CCheckQueue<T>>(
                        batchSize,
                        threadGroup,
                        threadCount,
                        baseThreadName + std::to_string(queueNum)));

            mIdleQueues.push(queue.get());
        }
    }

    ~CCheckQueuePool()
    {
        {
            std::unique_lock lock{mQueuesLock};

            for(auto& checker : mRunningCheckers)
            {
                checker.mPrematureCheckerTerminationSource->Cancel();
            }
        }

        while(true)
        {
            {
                std::unique_lock lock{mQueuesLock};
                if(mRunningCheckers.empty())
                {
                    break;
                }
            }

            using namespace std::chrono_literals;
            std::this_thread::sleep_for(100ms);
        }
    }

    /**
     * Returns an instance of the checker.
     *
     * NOTE: Function blocks if no idle checkers are in the queue and waits
     *       until one is returned, then it returns the handle.
     *
     * checkerPoolToken optional parameter is intended for functionality that is
     * used only during testing. Returns cancellation token that is connected to
     * cancellation source for early cancellation of checkers when a new checker
     * with higher ValueT is requested but no checkers are in idle state. This
     * token is not connected to cancellation sources further down the chain (
     * added by handle, by checker...)
     */
    CCheckQueueScopeGuard GetChecker(
        const ValueT& value,
        const task::CCancellationToken& token,
        std::optional<task::CCancellationToken>* checkerPoolToken = nullptr)
    {
        std::unique_lock lock{mQueuesLock};

        if(mIdleQueues.empty())
        {
            // empty idle queues list indicates that checkers must be running
            assert(mRunningCheckers.begin() != mRunningCheckers.end());

            auto itWorst = mRunningCheckers.begin();
            for(auto it = mRunningCheckers.begin() + 1; it != mRunningCheckers.end(); ++it)
            {
                if(itWorst->mValue > it->mValue)
                {
                    itWorst = it;
                }
                else if(itWorst->mValue == it->mValue
                    && itWorst->mValidationStartTime > it->mValidationStartTime)
                {
                    itWorst = it;
                }
            }

            // only kill off checker if it's value is less than what we wish
            // to validate now
            if(itWorst->mValue < value)
            {
                itWorst->mPrematureCheckerTerminationSource->Cancel();
            }
        }

        mIdleQueuesCV.wait(lock, [this]{return !mIdleQueues.empty();});

        auto checker = mIdleQueues.front();
        auto prematureCheckerTerminationSource = task::CCancellationSource::Make();
        mRunningCheckers.push_back(
            CRunningChecker{
                prematureCheckerTerminationSource,
                value,
                checker});
        mIdleQueues.pop();

        if(checkerPoolToken)
        {
            *checkerPoolToken = prematureCheckerTerminationSource->GetToken();
        }

        return
        {
            this,
            checker,
            task::CCancellationToken::JoinToken(
                token, prematureCheckerTerminationSource->GetToken())
        };
    }

private:
    void ReturnQueueToPool(CCheckQueue<T>& queue)
    {
        std::unique_lock lock{mQueuesLock};

        // returned queue is supposed to be unused
        assert(queue.IsIdle());

        mIdleQueues.push(&queue);

        bool found = false;
        for(auto it = mRunningCheckers.begin(); it != mRunningCheckers.end(); ++it)
        {
            if(it->mChecker == &queue)
            {
                found = true;
                mRunningCheckers.erase(it);
                break;
            }
        }

        // sanity check that code has not been changed in a way that allows us
        // to return queues to pool that did not originate from current pool
        assert(found);

        mIdleQueuesCV.notify_one();
    }

    struct CRunningChecker
    {
        std::shared_ptr<task::CCancellationSource> mPrematureCheckerTerminationSource;
        ValueT mValue{};
        CCheckQueue<T>* mChecker = nullptr;
        std::chrono::time_point<std::chrono::steady_clock> mValidationStartTime =
                std::chrono::steady_clock::now();
    };

    std::mutex mQueuesLock;
    std::condition_variable mIdleQueuesCV;
    std::queue<CCheckQueue<T>*> mIdleQueues;
    std::vector<std::unique_ptr<CCheckQueue<T>>> mScriptCheckQueue;
    std::vector<CRunningChecker> mRunningCheckers;
};

}

#endif // BITCOIN_CHECKQUEUEPOOL_H