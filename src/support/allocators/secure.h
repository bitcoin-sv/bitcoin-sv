// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_SUPPORT_ALLOCATORS_SECURE_H
#define BITCOIN_SUPPORT_ALLOCATORS_SECURE_H

#include "support/cleanse.h"
#include "support/lockedpool.h"

#include <string>

//
// Allocator that locks its contents from being paged
// out of memory and clears its contents before deletion.
//
// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions)
template <typename T> struct secure_allocator : public std::allocator<T> {
    typedef std::allocator<T> base;
    secure_allocator() throw() {}
    secure_allocator(const secure_allocator &a) throw() : base(a) {}
    template <typename U>
    secure_allocator(const secure_allocator<U> &a) throw() : base(a) {}
    ~secure_allocator() throw() {}
    // NOLINTNEXTLINE(bugprone-reserved-identifier)
    template <typename _Other> struct rebind {
        typedef secure_allocator<_Other> other;
    };

    T *allocate(std::size_t n, const void *hint = 0) {
        return static_cast<T *>(
            LockedPoolManager::Instance().alloc(sizeof(T) * n));
    }

    void deallocate(T *p, std::size_t n) {
        if (p != nullptr) {
            memory_cleanse(p, sizeof(T) * n);
        }
        LockedPoolManager::Instance().free(p);
    }
};

// This is exactly like std::string, but with a custom allocator.
typedef std::basic_string<char, std::char_traits<char>, secure_allocator<char>>
    SecureString;

#endif // BITCOIN_SUPPORT_ALLOCATORS_SECURE_H
