#include "write_preferring_upgradable_mutex.h"

void WPUSMutex::ReadLock(Lock& lockHandle)
{
    if(lockHandle.GetLockType() != Lock::Type::unlocked)
    {
        // is already locked - used for write->read lock transition

        assert(lockHandle.GetLockType() == Lock::Type::write);

        std::unique_lock lock{mMutex};

        lockHandle.ReleaseNL();

        lockHandle.AssignNL( Lock::MakeReadLockHandle(std::move(lock), *this) );
    }
    else
    {
        std::unique_lock lock{mMutex};

        mTryTakeLock.wait(lock, [this]{ return (!mWritePending && mLock >= 0); });

        lockHandle.AssignNL( Lock::MakeReadLockHandle(std::move(lock), *this) );
    }
}

auto WPUSMutex::WriteLock() -> Lock
{
    std::unique_lock lock{mMutex};

    // Even though mWritePending is atomic for access without mMutex lock
    // outside WPUSMutex we want to increment it only under mMutex lock to
    // prevent unnecessary mTryTakeLock wakeups
    class PendingGuard {
        std::atomic<int>& pending;
    public:
        PendingGuard(std::atomic<int>& in) : pending{in} { ++pending; }
        ~PendingGuard() { --pending; }
    } pendingGuard { mWritePending };

    mTryTakeLock.wait(lock, [this]{ return (mLock == 0); });

    return Lock::MakeWriteLockHandle(std::move(lock), *this);
}

bool WPUSMutex::TryWriteLock(Lock& lockHandle)
{
    assert(lockHandle.GetLockType() == Lock::Type::read);

    if(mWritePending != 0)
    {
        // Somebody else is already trying to obtain the write lock
        return false;
    }

    std::unique_lock lock{mMutex};

    // Even though mWritePending is atomic for access without mMutex lock
    // outside WPUSMutex we want to increment it only under mMutex lock to
    // prevent unnecessary mTryTakeLock wakeups
    class PendingGuard {
        std::atomic<int>& pending;
    public:
        PendingGuard(std::atomic<int>& in) : pending{in} { ++pending; }
        ~PendingGuard() { --pending; }
    } pendingGuard { mWritePending };

    auto canTakeWriteLock =
        [this, &lockHandle]
        {
            // we need to handle the case where we receive conditional
            // write lock request first and an unconditional later as
            // the unconditional has the priority since otherwise none
            // of the locks would be able to progress (relates to
            // the case where we are already holding a read lock
            // beforehand)
            if(mWritePending != 1)
            {
                // exiting here means that we will not obtain a write lock
                return true;
            }
            else if(mLock == 1) // we are already holding the read lock
            {
                // exiting here means that we will obtain a write lock so we
                // can release read lock since we no longer need it and we
                // are guaranteed by locked mMutex that we'll be the one
                // that obtain the write lock.
                lockHandle.ReleaseNL();

                return true;
            }

            return false;
        };

    mTryTakeLock.wait(lock, canTakeWriteLock);

    if(mWritePending != 1)
    {
        // If  there somebody else who is also waiting for write lock, then
        // go ahead  and do NOT obtain the lock by returning unmodified lock
        // handle
        return false;
    }

    lockHandle.AssignNL( Lock::MakeWriteLockHandle(std::move(lock), *this) );

    return true;
}