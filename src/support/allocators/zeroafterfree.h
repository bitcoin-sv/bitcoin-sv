// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_SUPPORT_ALLOCATORS_ZEROAFTERFREE_H
#define BITCOIN_SUPPORT_ALLOCATORS_ZEROAFTERFREE_H

#include "support/cleanse.h"

#include <memory>
#include <vector>

template <typename T>
// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions)
struct zero_after_free_allocator : public std::allocator<T> {
    typedef std::allocator<T> base;
    zero_after_free_allocator() throw() {}
    zero_after_free_allocator(const zero_after_free_allocator &a) throw()
        : base(a) {}
    template <typename U>
    zero_after_free_allocator(const zero_after_free_allocator<U> &a) throw()
        : base(a) {}
    ~zero_after_free_allocator() throw() {}
    // NOLINTNEXTLINE(bugprone-reserved-identifier)
    template <typename _Other> struct rebind {
        typedef zero_after_free_allocator<_Other> other;
    };

    void deallocate(T *p, std::size_t n) {
        if (p != nullptr) memory_cleanse(p, sizeof(T) * n);
        std::allocator<T>::deallocate(p, n);
    }
};

// Byte-vector that clears its contents before deletion.
typedef std::vector<char, zero_after_free_allocator<char>> CSerializeData;

#endif // BITCOIN_SUPPORT_ALLOCATORS_ZEROAFTERFREE_H
