// Copyright (c) 2019 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#ifndef BITCOIN_TASKCANCELLATION_H
#define BITCOIN_TASKCANCELLATION_H

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <vector>
#include <optional>
#include <boost/chrono.hpp>

namespace task
{
    class CCancellationSource;

    /**
     * An immutable token that can be provided to a long running task which can
     * periodically check whether it should cancel before completion.
     * Cancellation is triggered from the attached CCancellationSource(s).
     *
     * Tokens can be joined together from different sources so that if any of
     * the sources trigger cancellation the token is also canceled. Since tokens
     * are immutable `JoinToken()` returns a new token that is attached to all
     * the sources to which the source tokens are attached - this makes tokens
     * thread safe.
     */
    class CCancellationToken
    {
    public:
        CCancellationToken(std::shared_ptr<CCancellationSource> source)
            : mSource{std::move(source)}
        {/**/}

        bool IsCanceled() const;

        static CCancellationToken JoinToken(
            const CCancellationToken& token1,
            const CCancellationToken& token2)
        {
            CCancellationToken newToken;
            // we don't care if some of the sources are duplicates as we don't
            // expect a large amount of sources and even less duplicates
            newToken.mSource.reserve(
                token1.mSource.size() + token2.mSource.size());
            newToken.mSource.insert(
                newToken.mSource.end(),
                token1.mSource.begin(),
                token1.mSource.end());
            newToken.mSource.insert(
                newToken.mSource.end(),
                token2.mSource.begin(),
                token2.mSource.end());

            return newToken;
        }

    private:
        CCancellationToken() = default;
        std::vector<std::shared_ptr<CCancellationSource>> mSource;
    };

    /**
     * A long running task cancellation source which is kept on the caller side
     * while the associated token is provided to the task to periodically check
     * whether it should terminate before completion.
     *
     * To create a new cancellation source static member function Make() should
     * be called.
     */
    // NOLINTNEXTLINE(cppcoreguidelines-special-member-functions)
    class CCancellationSource : public std::enable_shared_from_this<CCancellationSource>
    {
    public:
        virtual ~CCancellationSource() = default;

        static std::shared_ptr<CCancellationSource> Make()
        {
            return
                std::shared_ptr<CCancellationSource>{new CCancellationSource{}};
        }

        CCancellationToken GetToken()
        {
            return CCancellationToken{shared_from_this()};
        }

        void Cancel() { mCanceled = true; }
        virtual bool IsCanceled() { return mCanceled; }

    protected:
        CCancellationSource() = default;

    private:
        std::atomic<bool> mCanceled = false;
    };

    /** A time budget for chained transactions
     *
     * Accumulate unused part of one timed cancellation source to be available
     * for the next cancellation source, upto a limit
     */

    class CTimedCancellationBudget final
    {
        static constexpr std::chrono::microseconds zero = std::chrono::microseconds{0};
    public:
        CTimedCancellationBudget(): mLimit {0} {}
        CTimedCancellationBudget(std::chrono::milliseconds limit): mLimit {limit} {}

        std::chrono::microseconds DrainBudget(std::chrono::milliseconds allowance) {
            auto ret = mBudget;
            mBudget = zero;
            return ret + allowance;
        }
        void FillBudget(std::chrono::microseconds remaining) {
            mBudget = std::min(mLimit, std::max(remaining, zero));
        }
    private:
        std::chrono::microseconds mLimit;
        std::chrono::microseconds mBudget {0};
    };


    /**
     * A long running task cancellation source with same features as
     * CCancellationSource but can additionally be set so that it auto cancels
     * after N wall time elapsed.
     */
    template <typename Clock>
    // NOLINTNEXTLINE(cppcoreguidelines-special-member-functions)
    class CTimedCancellationSourceT final : public CCancellationSource
    {
    public:
        static std::shared_ptr<CCancellationSource> Make(
            std::chrono::milliseconds const& after)
        {
            return
                std::shared_ptr<CCancellationSource>{
                    new CTimedCancellationSourceT<Clock>{after, nullptr}};
        }
        static std::shared_ptr<CCancellationSource> Make(
            std::chrono::milliseconds const& after,
            CTimedCancellationBudget& budget)
        {
            return
                std::shared_ptr<CCancellationSource>{
                    new CTimedCancellationSourceT<Clock>{after, &budget}};
        }
        bool IsCanceled() override
        {
            if(CCancellationSource::IsCanceled())
            {
                return true;
            }

            if(mCancelAfter < (Clock::now() - mStart_))
            {
                Cancel();
                return true;
            }

            return false;
        }

        // NOLINTNEXTLINE(cppcoreguidelines-explicit-virtual-functions)
        ~CTimedCancellationSourceT() {
            if (mBudget) {
                mBudget->FillBudget(std::chrono::duration_cast<std::chrono::microseconds>(mStart_ + mCancelAfter - Clock::now()));
            }
        }

    private:
        CTimedCancellationSourceT(std::chrono::milliseconds const& after,
                                 CTimedCancellationBudget* budget)
            : mStart_{Clock::now()}
            , mCancelAfter{budget ? budget->DrainBudget(after) : after}
            , mBudget{budget}
        {/**/}

        typename Clock::time_point mStart_;
        typename Clock::duration mCancelAfter;
        CTimedCancellationBudget* mBudget;
    };

    using CTimedCancellationSource = CTimedCancellationSourceT<std::chrono::steady_clock>;

#ifdef BOOST_CHRONO_HAS_THREAD_CLOCK
    // std::chrono adapter of boost::thread_clock
    class thread_clock final
    {
        using btc = boost::chrono::thread_clock;
    public:
        using rep = btc::rep;
        using period = std::ratio<btc::period::num, btc::period::den>;
        using duration = std::chrono::duration<rep, period>;
        using time_point = std::chrono::time_point<thread_clock, duration>;
        static time_point now()
        {
            return time_point{} + duration{btc::now().time_since_epoch().count()};
        }
    };
#else
# ifdef _MSC_VER
#  pragma message("boost::chrono::thread_clock not available using std::chrono::steady_clock")
# else
#  warning "boost::chrono::thread_clock not available using std::chrono::steady_clock"
# endif
    using thread_clock = std::chrono::steady_clock
#endif

    using CThreadTimedCancellationSource = CTimedCancellationSourceT<thread_clock>;

}

#endif // BITCOIN_TASKCANCELLATION_H
