// Copyright (c) 2020 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#include "bench.h"

#include "thread_safe_queue.h"
#include "util.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <memory>
#include <thread>
#include <unordered_map>

//#define LOGBLOCK

namespace {
    constexpr auto NUMBER_OF_WRITERS = 5;
    constexpr auto NUMBER_OF_ENTRIES = 100000;
    constexpr auto QUEUE_SIZE_LIMIT = NUMBER_OF_ENTRIES * NUMBER_OF_WRITERS * sizeof(std::uint64_t) / 2;
    constexpr auto DATA_END_MARKER = std::uint64_t(-1);

    using Queue = CThreadSafeQueue<std::uint64_t>;

    struct BlockedLogger
    {
        using Cnt = std::unordered_map<const char*, int>;
        std::unordered_map<std::string, Cnt> thread_counters;
        ~BlockedLogger()
        {
            for (const auto& [thread_name, counters] : thread_counters)
            {
                for (const auto& [method, count] : counters)
                    std::cout << "Blocked in " << method << " " << count
                              << " times in " << thread_name << std::endl;
            }
        }

        void log(const char* method)
        {
            const auto thread_name = GetThreadName();
            const auto tciter = thread_counters.emplace(thread_name, Cnt{}).first;
            const auto iter = tciter->second.emplace(method, 0).first;
            ++iter->second;
        }
    };

    Queue::OnBlockedCallback logblock()
    {
#ifdef LOGBLOCK
        auto logger = std::make_shared<BlockedLogger>();
        return [logger](const char* method, size_t, size_t)
        {
            logger->log(method);
        };
#else
        return nullptr;
#endif
    }

    void FillQueueOneByOne(Queue& queue, const char* name)
    {
        RenameThread(name);
        for (auto i = NUMBER_OF_ENTRIES; i > 0; --i)
        {
            assert(queue.PushWait(i));
        }
    }

    void PopQueueOneByOne(Queue& queue, const char* name)
    {
        RenameThread(name);
        for (;;)
        {
            const auto result = queue.PopWait();
            if (!result || *result == DATA_END_MARKER)
            {
                break;
            }
        }
    }

    void FillQueueAllAtOnce(Queue& queue, const char* name)
    {
        RenameThread(name);
        static const auto values {
            []()
            {
                std::vector<std::uint64_t> v;
                v.resize(NUMBER_OF_ENTRIES);
                for (auto i = NUMBER_OF_ENTRIES; i > 0; --i)
                {
                    v.emplace_back(i);
                }
                return v;
            }()
        };

        auto tmp {values};
        assert(queue.PushManyWait(std::move(tmp)));
    }

    void PopQueueAllAtOnce(Queue& queue, const char* name)
    {
        RenameThread(name);
        for (;;)
        {
            const auto result = queue.PopAllWait();
            if (!result || result->back() == DATA_END_MARKER)
            {
                break;
            }
        }
    }

    void ThreadSafeQueue_SingleSingle(benchmark::State& state)
    {
        const auto logger = logblock();
        Queue queue{QUEUE_SIZE_LIMIT};
        queue.SetOnPushBlockedNotifier(logger);
        queue.SetOnPopBlockedNotifier(logger);

        while (state.KeepRunning())
        {
            std::thread reader {std::bind(PopQueueOneByOne, std::ref(queue), "reader")};
            std::array<std::thread, NUMBER_OF_WRITERS> writers {
                std::thread{std::bind(FillQueueOneByOne, std::ref(queue), "writer 1")},
                std::thread{std::bind(FillQueueOneByOne, std::ref(queue), "writer 2")},
                std::thread{std::bind(FillQueueOneByOne, std::ref(queue), "writer 3")},
                std::thread{std::bind(FillQueueOneByOne, std::ref(queue), "writer 4")},
                std::thread{std::bind(FillQueueOneByOne, std::ref(queue), "writer 5")},
            };

            for (auto& t : writers)
            {
                t.join();
            }

            queue.PushWait(DATA_END_MARKER);
            reader.join();
        }
    }

    void ThreadSafeQueue_MultiMulti(benchmark::State& state)
    {
        const auto logger = logblock();
        Queue queue{QUEUE_SIZE_LIMIT};
        queue.SetOnPushBlockedNotifier(logger);
        queue.SetOnPopBlockedNotifier(logger);

        while (state.KeepRunning())
        {
            std::thread reader {std::bind(PopQueueAllAtOnce, std::ref(queue), "reader")};
            std::array<std::thread, NUMBER_OF_WRITERS> writers {
                std::thread{std::bind(FillQueueAllAtOnce, std::ref(queue), "writer 1")},
                std::thread{std::bind(FillQueueAllAtOnce, std::ref(queue), "writer 2")},
                std::thread{std::bind(FillQueueAllAtOnce, std::ref(queue), "writer 3")},
                std::thread{std::bind(FillQueueAllAtOnce, std::ref(queue), "writer 4")},
                std::thread{std::bind(FillQueueAllAtOnce, std::ref(queue), "writer 5")},
            };

            for (auto& t : writers)
            {
                t.join();
            }

            queue.PushWait(DATA_END_MARKER);
            reader.join();
        }
    }

    void ThreadSafeQueue_SingleMulti(benchmark::State& state)
    {
        const auto logger = logblock();
        Queue queue{QUEUE_SIZE_LIMIT};
        queue.SetOnPushBlockedNotifier(logger);
        queue.SetOnPopBlockedNotifier(logger);

        while (state.KeepRunning())
        {
            std::thread reader {std::bind(PopQueueAllAtOnce, std::ref(queue), "reader")};
            std::array<std::thread, NUMBER_OF_WRITERS> writers {
                std::thread{std::bind(FillQueueOneByOne, std::ref(queue), "writer 1")},
                std::thread{std::bind(FillQueueOneByOne, std::ref(queue), "writer 2")},
                std::thread{std::bind(FillQueueOneByOne, std::ref(queue), "writer 3")},
                std::thread{std::bind(FillQueueOneByOne, std::ref(queue), "writer 4")},
                std::thread{std::bind(FillQueueOneByOne, std::ref(queue), "writer 5")},
            };

            for (auto& t : writers)
            {
                t.join();
            }

            queue.PushWait(DATA_END_MARKER);
            reader.join();
        }
    }

    void ThreadSafeQueue_MultiSingle(benchmark::State& state)
    {
        const auto logger = logblock();
        Queue queue{QUEUE_SIZE_LIMIT};
        queue.SetOnPushBlockedNotifier(logger);
        queue.SetOnPopBlockedNotifier(logger);

        while (state.KeepRunning())
        {
            std::thread reader {std::bind(PopQueueOneByOne, std::ref(queue), "reader")};
            std::array<std::thread, NUMBER_OF_WRITERS> writers {
                std::thread{std::bind(FillQueueAllAtOnce, std::ref(queue), "writer 1")},
                std::thread{std::bind(FillQueueAllAtOnce, std::ref(queue), "writer 2")},
                std::thread{std::bind(FillQueueAllAtOnce, std::ref(queue), "writer 3")},
                std::thread{std::bind(FillQueueAllAtOnce, std::ref(queue), "writer 4")},
                std::thread{std::bind(FillQueueAllAtOnce, std::ref(queue), "writer 5")},
            };

            for (auto& t : writers)
            {
                t.join();
            }

            queue.PushWait(DATA_END_MARKER);
            reader.join();
        }
    }
} // anonymous namespace

BENCHMARK(ThreadSafeQueue_SingleSingle);
BENCHMARK(ThreadSafeQueue_MultiMulti);
BENCHMARK(ThreadSafeQueue_SingleMulti);
BENCHMARK(ThreadSafeQueue_MultiSingle);
