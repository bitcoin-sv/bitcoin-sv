// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include <chrono>
#include <cstddef>

/**
 * A simple implementation of the leaky bucket algorithm.
 *
 * Values are added to the total stored in the bucket and if that total exceeds
 * a defined threshold the bucket overflows. Meanwhile over time the bucket
 * leaks and the total steadily drains out.
 *
 * This can be used to measure if the frequency of some event exceeds a set
 * limit.
 */
template<typename LeakInterval>
class LeakyBucket
{
  public:
    LeakyBucket(size_t maxFill, LeakInterval leakInterval)
        : mMaxFillLevel{maxFill}, mLeakInterval{leakInterval}
    {}
    LeakyBucket(size_t maxFill, size_t startFill, LeakInterval leakInterval)
        : mMaxFillLevel{maxFill}, mFillLevel{startFill}, mLeakInterval{leakInterval}
    {}

    // Topup the bucket and return whether or not we are overflowing
    bool operator+=(size_t amount)
    {
        // Increase fill by given amount
        mFillLevel += amount;
        return Overflowing();
    }

    // Return whether we're overflowing
    bool Overflowing() const
    {
        UpdateFillLevel();
        return mFillLevel > mMaxFillLevel;
    }

    // Return our current fill level
    size_t GetFillLevel() const
    {
        UpdateFillLevel();
        return mFillLevel;
    }

  private:

    // Calculate and update current fill level
    void UpdateFillLevel() const
    {
        // How many leak intervals have passed since we last updated?
        auto now { std::chrono::system_clock::now() };
        auto timeDiff { now - mLastDrainTime };
        auto intervals { timeDiff / mLeakInterval };

        // Calculate how much we have drained over those intervals
        size_t drained { static_cast<size_t>(intervals) };
        if(drained <= mFillLevel)
        {
            mFillLevel -= drained;
        }
        else
        {
            mFillLevel = 0;
        }

        // Update last drain time
        mLastDrainTime = now;
    }

    // Max fill level (overflow point)
    const size_t mMaxFillLevel {};

    // Our fill level
    mutable size_t mFillLevel {0};

    // Frequency at which we leak. The fill level is reduced by 1 after each
    // interval of this time.
    const LeakInterval mLeakInterval {};

    // Time of last drain
    mutable std::chrono::time_point<std::chrono::system_clock> mLastDrainTime { std::chrono::system_clock::now() };
};

