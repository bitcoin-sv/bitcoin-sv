// Copyright (c) 2015 The Bitcoin Core developers
// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "checkqueuepool.h"
#include "bench.h"
#include "prevector.h"
#include "random.h"
#include "util.h"
#include "validation.h"

#include <boost/thread/thread.hpp>

#include <optional>
#include <vector>

// This Benchmark tests the CheckQueue with the lightest weight Checks, so it
// should make any lock contention particularly visible
static const size_t MIN_CORES = 2;
static const size_t BATCHES = 101;
static const size_t BATCH_SIZE = 30;
static const int PREVECTOR_SIZE = 28;
static const unsigned int QUEUE_BATCH_SIZE = 128;
static void CCheckQueueSpeed(benchmark::State &state) {
    struct FakeJobNoWork {
        std::optional<bool> operator()(const task::CCancellationToken&)
        {
            return true;
        }
        void swap(FakeJobNoWork &x){};
    };
    boost::thread_group tg;
    checkqueue::CCheckQueuePool<FakeJobNoWork, int> pool{
        1, /* validator count */
        tg,
        std::max(MIN_CORES, static_cast<size_t>(GetNumCores())), // validation threads count
        QUEUE_BATCH_SIZE};
    auto source = task::CCancellationSource::Make();
    while (state.KeepRunning()) {
        auto control = pool.GetChecker(0, source->GetToken());

        // We call Add a number of times to simulate the behavior of adding a
        // block of transactions at once.
        std::vector<std::vector<FakeJobNoWork>> vBatches(BATCHES);
        for (auto &vChecks : vBatches) {
            vChecks.resize(BATCH_SIZE);
        }

        for (auto &vChecks : vBatches) {
            // We can't make vChecks in the inner loop because we want to
            // measure the cost of getting the memory to each thread and we
            // might get the same memory
            control.Add(vChecks);
        }
        // control waits for completion by RAII, but it is done explicitly here
        // for clarity
        control.Wait();
    }
    tg.interrupt_all();
    tg.join_all();
}

// This Benchmark tests the CheckQueue with a slightly realistic workload, where
// checks all contain a prevector that is indirect 50% of the time and there is
// a little bit of work done between calls to Add.
static void CCheckQueueSpeedPrevectorJob(benchmark::State &state) {
    struct PrevectorJob {
        prevector<PREVECTOR_SIZE, uint8_t> p;
        PrevectorJob() {}
        PrevectorJob(FastRandomContext &insecure_rand) {
            p.resize(insecure_rand.randrange(PREVECTOR_SIZE * 2));
        }
        std::optional<bool> operator()(const task::CCancellationToken&)
        {
            return true;
        }
        void swap(PrevectorJob &x) { p.swap(x.p); };
    };
    boost::thread_group tg;
    checkqueue::CCheckQueuePool<PrevectorJob, int> pool{
        1, /* validator count */
        tg,
        std::max(MIN_CORES, static_cast<size_t>(GetNumCores())), // validation threads count
        QUEUE_BATCH_SIZE};
    auto source = task::CCancellationSource::Make();
    while (state.KeepRunning()) {
        // Make insecure_rand here so that each iteration is identical.
        FastRandomContext insecure_rand(true);
        auto control = pool.GetChecker(0, source->GetToken());
        std::vector<std::vector<PrevectorJob>> vBatches(BATCHES);
        for (auto &vChecks : vBatches) {
            vChecks.reserve(BATCH_SIZE);
            for (size_t x = 0; x < BATCH_SIZE; ++x)
                vChecks.emplace_back(insecure_rand);
            control.Add(vChecks);
        }
        // control waits for completion by RAII, but it is done explicitly here
        // for clarity
        control.Wait();
    }
    tg.interrupt_all();
    tg.join_all();
}
BENCHMARK(CCheckQueueSpeed);
BENCHMARK(CCheckQueueSpeedPrevectorJob);
