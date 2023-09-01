// Copyright (c) 2018-2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include "task.h"
#include "threadpool.h"

#include <cwchar>
#include <future>
#include <type_traits>

// Helper method to create task with a specified priority.
template<typename ThreadPool, typename Priority, typename Callable, typename... Args>
auto make_task(ThreadPool& pool, Priority priority, Callable&& call, Args&&... args)
    -> std::future<std::invoke_result_t<Callable, Args...>>
{
    CTask task { priority };
    auto future { task.injectTask(std::forward<Callable>(call), std::forward<Args>(args)...) };
    pool.submit(std::move(task));
    return future;
}

// Helper method to create a default priority task.
template<typename ThreadPool, typename Callable, typename... Args>
auto make_task(ThreadPool& pool, Callable&& call, Args&&... args)
    -> std::future<std::invoke_result_t<Callable, Args...>>
{
    // Default to medium priority
    CTask::Priority priority { CTask::Priority::Medium };
    return make_task(pool, priority, std::forward<Callable>(call), std::forward<Args>(args)...);
}
