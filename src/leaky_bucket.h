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

    LeakyBucket() = default;

    LeakyBucket(size_t maxFill,
                LeakInterval leakInterval,
                double drainAmount = 1)
        : mMaxFillLevel{maxFill},
          mDrainAmount{drainAmount},
          mLeakInterval{leakInterval}
    {}

    LeakyBucket(size_t maxFill,
                double startFill,
                LeakInterval leakInterval,
                double drainAmount = 1)
        : mMaxFillLevel{maxFill},
          mFillLevel{startFill},
          mDrainAmount{drainAmount},
          mLeakInterval{leakInterval}
    {}

    // Topup the bucket and return whether or not we are overflowing
    template<typename Amount>
    bool operator+=(Amount amount)
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
    double GetFillLevel() const
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
        std::chrono::duration<double> timeDiff { now - mLastDrainTime };
        double intervals { timeDiff / mLeakInterval };

        // Calculate how much we have drained over those intervals
        mFillLevel -= (intervals * mDrainAmount);
        if(mFillLevel < 0)
        {
            mFillLevel = 0;
        }

        // Update last drain time
        mLastDrainTime = now;
    }

    // Max fill level (overflow point)
    size_t mMaxFillLevel {};

    // Our fill level
    mutable double mFillLevel {0};

    // How much we drain by each interval
    double mDrainAmount {0};

    // Frequency at which we leak. The fill level is reduced by mDrainAmount
    // after each interval of this time.
    LeakInterval mLeakInterval {};

    // Time of last drain
    mutable std::chrono::time_point<std::chrono::system_clock> mLastDrainTime { std::chrono::system_clock::now() };
};

