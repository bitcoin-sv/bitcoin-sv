// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_THREADSAFETY_H
#define BITCOIN_THREADSAFETY_H

// TL;DR Add GUARDED_BY(mutex) to member variables.
// The others are rarely necessary.
// Ex: int nFoo GUARDED_BY(cs_foo);
//
// See https://clang.llvm.org/docs/ThreadSafetyAnalysis.html
// The clang compiler can do advanced static analysis of
// locking when given the -Wthread-safety option.
//
// GCC does not support these attributes and rejects them
// syntactically when placed between a function's parameter
// list and its body, so under non-clang compilers all
// macros here expand to nothing.
#ifdef __clang__
#define CAPABILITY(x) [[clang::capability(x)]]
#define LOCKABLE CAPABILITY("mutex")
#define SCOPED_LOCKABLE [[clang::scoped_lockable]]
// The remaining attributes must use __attribute__ syntax
// because [[clang::...]] cannot be applied to function
// declarations or variable declarations.
#define GUARDED_BY(x) __attribute__((guarded_by(x)))
#define ACQUIRE(...) __attribute__((acquire_capability(__VA_ARGS__)))
#define EXCLUSIVE_LOCK_FUNCTION ACQUIRE
#define TRY_ACQUIRE(...) __attribute__((try_acquire_capability(__VA_ARGS__)))
#define EXCLUSIVE_TRYLOCK_FUNCTION TRY_ACQUIRE
#define RELEASE(...) __attribute__((release_capability(__VA_ARGS__)))
#define UNLOCK_FUNCTION RELEASE
#define REQUIRES(...) __attribute__((requires_capability(__VA_ARGS__)))
#define EXCLUDES(...) __attribute__((locks_excluded(__VA_ARGS__)))
#define LOCKS_EXCLUDED EXCLUDES
#define NO_THREAD_SAFETY_ANALYSIS __attribute__((no_thread_safety_analysis))
#else
#define CAPABILITY(x)
#define LOCKABLE
#define SCOPED_LOCKABLE
#define GUARDED_BY(x)
#define ACQUIRE(...)
#define EXCLUSIVE_LOCK_FUNCTION(...)
#define TRY_ACQUIRE(...)
#define EXCLUSIVE_TRYLOCK_FUNCTION(...)
#define RELEASE(...)
#define UNLOCK_FUNCTION(...)
#define REQUIRES(...)
#define EXCLUDES(...)
#define LOCKS_EXCLUDED(...)
#define NO_THREAD_SAFETY_ANALYSIS
#endif

#endif // BITCOIN_THREADSAFETY_H
