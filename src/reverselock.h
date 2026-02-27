// Copyright (c) 2015-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_REVERSELOCK_H
#define BITCOIN_REVERSELOCK_H

/**
 * An RAII-style reverse lock. Unlocks on construction and locks on destruction.
 */
template<typename Lock>
class reverse_lock
{
public:
    explicit reverse_lock(Lock &_lock) : lock(_lock) {
        _lock.unlock();
        _lock.swap(templock);
    }

    ~reverse_lock() //NOLINT(bugprone-exception-escape)
    {
        templock.lock();
        templock.swap(lock);
    }

    reverse_lock(const reverse_lock&) = delete;
    reverse_lock &operator=(const reverse_lock&) = delete;
    
    reverse_lock(reverse_lock&&) = delete;
    reverse_lock &operator=(reverse_lock&&) = delete;

private:
    Lock &lock;
    Lock templock;
};

#endif // BITCOIN_REVERSELOCK_H
