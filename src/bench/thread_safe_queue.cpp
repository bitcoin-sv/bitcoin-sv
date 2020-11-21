// Copyright (c) 2020 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#include "bench.h"

#include "thread_safe_queue.h"
#include "util.h"

#include <array>
#include <cstdint>
#include <thread>


namespace {
    constexpr auto NUMBER_OF_WRITERS = 5;
    constexpr auto NUMBER_OF_ENTRIES = 100000;
    constexpr auto QUEUE_SIZE_LIMIT = NUMBER_OF_ENTRIES * NUMBER_OF_WRITERS * sizeof(std::uint64_t) / 2;
    constexpr auto DATA_END_MARKER = std::uint64_t(-1);

    using Queue = CThreadSafeQueue<std::uint64_t>;

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
        assert(queue.FillWait(std::move(tmp)));
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
        while (state.KeepRunning())
        {
            Queue queue{QUEUE_SIZE_LIMIT};

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
        while (state.KeepRunning())
        {
            Queue queue{QUEUE_SIZE_LIMIT};

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
        while (state.KeepRunning())
        {
            Queue queue{QUEUE_SIZE_LIMIT};

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
        while (state.KeepRunning())
        {
            Queue queue{QUEUE_SIZE_LIMIT};

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
