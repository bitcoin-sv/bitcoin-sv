// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

// Macros for disabling warnings.
// Use only if warnings cannot be corrected properly or in case of using external libraries that should not be changed.

/* Sample usage for CLANG:
 * CLANG_WARNINGS_PUSH
 * CLANG_WARNINGS_IGNORE(-Wsign-conversion)
 * int a = 4;
 * unsigned int b = a;
 * CLANG_WARNINGS_POP
 */

#pragma once

/**
  * Macros used to ignore compiler warnings in GCC builds.
  * Note that because CLANG also defines macro __gnuc__ and understands GCC pragmas,
  * the same warnings will also be ignored in CLANG builds.
 **/

#ifdef __GNUC__
#define GCC_WARNINGS_PUSH _Pragma("GCC diagnostic push")
#define GCC_WARNINGS_POP _Pragma("GCC diagnostic pop")
#define DETAIL_GCC_WARNINGS_IGNORE(x) _Pragma(#x)
#define GCC_WARNINGS_IGNORE(w) DETAIL_GCC_WARNINGS_IGNORE(GCC diagnostic ignored #w)
#else
#define GCC_WARNINGS_PUSH
#define GCC_WARNINGS_IGNORE(w)
#define GCC_WARNINGS_POP
#endif

// Macros used to ignore compiler warnings in CLANG builds.
#ifdef __clang__
#define CLANG_WARNINGS_PUSH _Pragma("clang diagnostic push")
#define CLANG_WARNINGS_POP _Pragma("clang diagnostic pop")
#define DETAIL_CLANG_WARNINGS_IGNORE(x) _Pragma(#x)
#define CLANG_WARNINGS_IGNORE(w) DETAIL_CLANG_WARNINGS_IGNORE(clang diagnostic ignored #w)
#else
#define CLANG_WARNINGS_PUSH
#define CLANG_WARNINGS_IGNORE(w)
#define CLANG_WARNINGS_POP
#endif

// Macros used to ignore compiler warnings in MSVC builds.
#ifdef _MSC_VER
#define MSVC_WARNINGS_PUSH __pragma(warning(push))
#define MSVC_WARNINGS_POP __pragma(warning(pop))
#define MSVC_WARNINGS_IGNORE(warning_number) __pragma(warning(disable : warning_number))
#else
#define MSVC_WARNINGS_PUSH
#define MSVC_WARNINGS_IGNORE(warning_number)
#define MSVC_WARNINGS_POP
#endif
