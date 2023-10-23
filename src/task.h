// Copyright (c) 2018-2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include <functional>
#include <future>
#include <type_traits>
#include <utility>

/**
* A task for running via a thread pool.
* Any callable object can be wrapped as a task (function, class method, lambda,
* etc.) with any arguments and any return type.
*/
class CTask
{
  public:

    // Some pre-defined priority levels.
    enum class Priority : int
    {
        Low = 0,
        Medium = 1,
        High = 2
    };

    // Some pre-defined status levels.
    enum class Status : int
    {
        Canceled = 0,
        Created = 1,
        Faulted = 2,
        RanToCompletion = 3,
        Running = 4,
        WaitingToRun = 5
    };

  private:

    // Helper method for converting a pre-defined priority level to the underlying
    // integer type.
    static typename std::underlying_type<Priority>::type convertPriority(Priority priority)
    {
        return static_cast<typename std::underlying_type<Priority>::type>(priority);
    }

  public:

    // Constructor for default priority task.
    CTask() : CTask {Priority::Medium} {}

    // Constructor for pre-defined priority task.
    CTask(Priority priority) : CTask {convertPriority(priority)} {}

    // Constructor for arbitrary priority task.
    CTask(int priority) : mPriority {priority} {}

    /**
    * Inject a callable object plus its arguments into this class.
    *
    * This needs to be a separate method on this class because:
    * (1) we need the callable object and the variadic arguments to be universal
    * references for perfect forwarding, which rules out using class template
    * parameters.
    * (2) We need to return a future to the caller for them to access the result
    * of running the task which depends on the type of the callable object and
    * its arguments, which rules out using a templated constructor or storing
    * the future as a member variable to be retrieved by an accessor.
    */
    template<typename Callable, typename... Args>
    auto injectTask(Callable&& call, Args&&... args)
        -> std::future<std::invoke_result_t<Callable, Args...>>
    {
        // Use packaged_task with bind to get us a task we can easily call
        // without needing to remember all its args.
        // FIXME: Once we get generalised lambda capture & C++14 we could use
        // make_unique here instead of make_shared.
        using returnType = std::invoke_result_t<Callable, Args...>;
        auto task { std::make_shared<std::packaged_task<returnType()>>(
            std::bind(std::forward<Callable>(call), std::forward<Args>(args)...))
        };

        mTask = [task](){ (*task)(); };
        return task->get_future();
    }

    // Run the stored callable task.
    void operator()() const { mTask(); }

    // Get our assigned priority level.
    int getPriority() const { return mPriority; }

  private:

    // Our stored task.
    std::function<void()> mTask {};

    // Our priority level.
    int mPriority {0};
};

// Specialise std::less for sorting tasks based on priority
namespace std
{
    template<>
    struct less<CTask>
    {
        bool operator()(const CTask& a, const CTask& b) const
        {
            return ( a.getPriority() < b.getPriority() );
        }
    };
}

