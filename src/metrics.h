// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include <atomic>
#include <vector>
#include <string>
#include <chrono>
#include <mutex>
#include <thread>

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

class OneThreadFromPool {
    using clock = std::chrono::steady_clock;
    OneThreadFromPool() = delete;
public:
    OneThreadFromPool(std::chrono::milliseconds interval)
    : mInterval {interval}
    , mNext {clock::now() + interval}
    {}

    void operator()(std::function<void()> callable) {
        if (chosen() && mNext < clock::now()) {
            mNext += mInterval;
            callable();
        }
    }

private:
    bool chosen() {
        std::call_once(mOnce, [this](){mChosenOne = std::this_thread::get_id();});
        return mChosenOne == std::this_thread::get_id();
    }

    clock::duration mInterval;
    clock::time_point mNext;
    std::once_flag mOnce {};
    std::thread::id mChosenOne;
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

