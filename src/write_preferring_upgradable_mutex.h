// Copyright (c) 2020 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include <atomic>
#include <cassert>
#include <condition_variable>
#include <mutex>

/**
 * This class is a write-preferring reader-writer lock with some additions.
 *
 * This class handles multiple cases:
 * - prefers write locks to read locks (if read lock request comes after write
 *   lock request it waits until the write lock request is fulfilled)
 * - allows multiple read locks simultaneously
 * - allows only a single write lock at the same time and only when read locks
 *   are not held
 * - there are two types of write lock requests:
 *    + is NOT allowed to return without a write lock - such locks are
 *      required to be requested without holding a read lock beforehand to
 *      guarantee that no data was accessed before the lock was requested so that
 *      they can be obtained one after the other in case multiple locks are
 *      requested consecutively.
 *    + is allowed to return without obtaining the write lock - such locks are
 *      used for optimistic task processing where we obtain a read lock, process
 *      data and escalate to write lock once we want to write the result. Since
 *      the task was completed on a certain set of data we can't allow such lock
 *      to write the result once data has changed so such locks can't be obtained
 *      one after the other - if such lock is requested and there is more than
 *      one write lock request pending, we return the read lock to the caller
 *      and expect the caller to gracefully release the read lock and re-try its
 *      task at a later point in time.
 */
class WPUSMutex
{
public:
    class Lock
    {
    public:
        enum class Type
        {
            unlocked,
            read,
            write
        };

        Lock() : mLockType{Type::unlocked} {}

        ~Lock() { Release(); }

        Lock(const Lock&) = delete;
        Lock& operator=(const Lock&) = delete;
        Lock(Lock&&) = default;

        // THIS FUNCTION CAN'T BE USED INSIDE WPUSMutex FUNCTIONS
        // as it locks the mutex that is usually locked externally by
        // WPUSMutex functions
        Lock& operator=(Lock&& other) noexcept
        {
            Release();

            AssignNL( std::move(other) );

            return *this;
        }

        void Release()
        {
            if(mLockProvider)
            {
                std::scoped_lock lock{mLockProvider->mMutex};
                ReleaseNL();
            }
        }

        Type GetLockType() const { return mLockType; }

    private:
        [[nodiscard]] static Lock MakeReadLockHandle(
            std::unique_lock<std::mutex>&& lock,
            WPUSMutex& lockProvider)
        {
            assert(lockProvider.mLock >= 0);
            ++lockProvider.mLock;
            return {std::move(lock), lockProvider};
        }
        [[nodiscard]] static Lock MakeWriteLockHandle(
            std::unique_lock<std::mutex>&& lock,
            WPUSMutex& lockProvider)
        {
            assert(lockProvider.mLock == 0);
            lockProvider.mLock = -1; // -1 indicates a write lock is held
            return {std::move(lock), lockProvider};
        }

        Lock(std::unique_lock<std::mutex>&&, WPUSMutex& lockProvider)
            : mLockType{lockProvider.mLock > 0 ? Type::read : Type::write}
            , mLockProvider{&lockProvider}
        {}

        void ReleaseNL()
        {
            int& lockRef = mLockProvider->mLock;

            if(lockRef == -1)
            {
                lockRef = 0;
            }
            else
            {
                assert(lockRef > 0);
                --lockRef;
            }

            mLockProvider->mTryTakeLock.notify_all();
            mLockProvider = nullptr;
            mLockType = Type::unlocked;
        }

        void AssignNL(Lock&& other)
        {
            assert( mLockType == Type::unlocked );

            mLockProvider = std::move(other.mLockProvider);
            mLockType = other.mLockType;
        }

        Type mLockType;

        // Does nothing - only needed so we can have default move constructor
        // and assignment operator
        struct CNullDeleter{void operator()(void*){}};
        std::unique_ptr<WPUSMutex, CNullDeleter> mLockProvider;

        friend class WPUSMutex;
    };

    /**
     * Obtain read lock (lockHandle parameter is of Type::unlocked when provided
     * to the function) or convert a write lock to read lock (lockHandle parameter
     * is of Type::write when provided to the function) in thread safe manner.
     *
     * After return from the function lockHandle parameter is of Type::read.
     */
    void ReadLock(Lock& lockHandle);

    // Obtain write lock
    [[nodiscard]] Lock WriteLock();

    /**
     * Obtain write lock if possible by escalating read lock to write lock
     * in thread safe manner.
     *
     * lockHandle must be of type Type::read
     *
     * After return from the function lockHandle parameter is of Type::write if
     * return value is true. If return value is false lockHandle is returned as
     * it was provided.
     */
    [[nodiscard]] bool TryWriteLock(Lock& lockHandle);

private:
    std::mutex mMutex;
    /**
     * Protected by mMutex
     *
     * -1: holding write lock
     * 0: no lock held
     * >0: holding read lock(s)
     */
    int mLock{0};
    std::atomic<int> mWritePending{0};

    std::condition_variable mTryTakeLock;
};