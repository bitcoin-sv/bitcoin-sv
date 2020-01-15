// Copyright (c) 2019 The Bitcoin SV developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include <mutex>
#include <shared_mutex>
#include <utility>

/**
* A basic wrapper for an object which also contains and locks a mutex
* for its lifetime.
*/
template<typename Wrapped, typename Lock>
class CLockedRef
{
  public:

    // Construction
    CLockedRef() = default;
    CLockedRef(const CLockedRef& that) = delete;
    CLockedRef(CLockedRef&& that) = default;

    CLockedRef(const Wrapped& wrapped, typename Lock::mutex_type& mtx)
        : mWrapped{wrapped}, mLock{mtx}
    {}
    CLockedRef(Wrapped&& wrapped, typename Lock::mutex_type& mtx)
        : mWrapped{std::move(wrapped)}, mLock{mtx}
    {}

    // Assignment
    CLockedRef& operator=(const CLockedRef& that) = delete;
    CLockedRef& operator=(CLockedRef&& that) = default;

    // Wrapped member access
    Wrapped& get() { return mWrapped; }
    const Wrapped& get() const { return mWrapped; }

  private:

    // The object we wrap
    Wrapped mWrapped {};

    // The lock
    Lock mLock {};

};
