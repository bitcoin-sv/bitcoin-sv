// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include <atomic>
#include <vector>
#include <string>
#include <chrono>
#include <mutex>
#include <thread>

#include "util.h"

namespace metrics {

/**
* Sample usage:
*
*    static Histogram durations_t {"T", 5000};
*    static Histogram durations_cpu {"CPU", 5000};
*    static OneThreadFromPool histogramLogger {std::chrono::milliseconds {1000}};
*    histogramLogger([]() { durations_t.dump(); durations_cpu.dump();});
*    {
*                auto timeTimer = TimedScope<std::chrono::steady_clock, std::chrono::milliseconds> { durations_t };
*                auto cpuTimer = TimedScope<task::thread_clock, std::chrono::milliseconds> { durations_cpu };
*                // measured code
*    }
* 
* Use test/functional/test_framework/metrics/histogram.py script to draw graphs from histogram logs.
*/

class Histogram {
public:
    explicit Histogram(std::string what, size_t size) : mWhat{what}, mCounts(size), mOverMax{0}, mOverCount{0} {
        for(auto&count: mCounts) {
            count = 0;
        }
    }
    void count(size_t value) {
        if (value >= mCounts.size()) {
            for (auto old = mOverMax.load(); value > old;) {
                mOverMax.compare_exchange_weak(old, value);
            }
            ++mOverCount;
        } else {
            ++mCounts[value];
        }
    }
    void dump() const {
        std::stringstream stat;
        size_t i = 0;
        stat << mWhat << " = Histogram({";
        for (auto& count: mCounts) {
            if (count) {
                stat <<i << ":" << count << ",";
            }
            ++i;
        }
        stat << "}";
        if (mOverCount) {
            stat << ", " << mOverMax << ", " << mOverCount;
        }
        stat << ")\n";
        LogPrintf("%s", stat.str());
    }

private:
    std::string mWhat;
    std::vector<std::atomic_size_t> mCounts;
    std::atomic_size_t mOverMax;
    std::atomic_size_t mOverCount;
};

class HistogramWriter {
    using clock = std::chrono::steady_clock;
    using Callable = std::function<void()>;
    HistogramWriter() = delete;
public:
    HistogramWriter(std::string name, std::chrono::milliseconds interval, Callable callable)
    : mInterval {interval}
    , mCallable {callable}
    , mThread {[this, name](){run(name);}}
    {}

    ~HistogramWriter() {
        {
            auto lock = std::unique_lock<std::mutex> { mLock };
            mStopping = true;
        }
        mWait.notify_all();
        mThread.join();
    }

private:

    void run(std::string name) {
        RenameThread((std::string("HistogramWriter-") + name).c_str());
        auto lock = std::unique_lock<std::mutex> { mLock };
        auto next = clock::now() + mInterval;
        while (!mWait.wait_until(lock, next, [this](){return mStopping;})) {
            mCallable();
            next += mInterval;
        }
        mCallable();       // we're shutting down, record the final stats
    }

    std::mutex mLock {};
    std::condition_variable mWait {};
    bool mStopping {false};
    clock::duration mInterval;
    Callable mCallable;
    std::thread mThread;
};

template <typename Clock, typename Interval> class TimedScope {
public:
    TimedScope(Histogram& histogram)
    : mHistogram {histogram}
    , mStart {Clock::now()}
    {}

    ~TimedScope()
    {
        mHistogram.count(std::chrono::duration_cast<Interval>(Clock::now() - mStart).count());
    }
private:
    Histogram& mHistogram;
    typename Clock::time_point mStart;
};


}

