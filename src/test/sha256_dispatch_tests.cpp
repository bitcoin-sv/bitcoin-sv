// Copyright (c) 2026 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "crypto/sha256_dispatch.h"

#include <boost/test/unit_test.hpp>

#include <string>

BOOST_AUTO_TEST_SUITE(sha256_dispatch_tests)

BOOST_AUTO_TEST_CASE(returns_non_empty_string)
{
    // The autodetect string always describes at least the default ("standard")
    // implementation, plus comma-separated tokens for any SIMD path that was
    // installed. It must never be empty.
    BOOST_CHECK(!sha256_dispatch::AutoDetect().empty());
}

BOOST_AUTO_TEST_CASE(idempotent)
{
    // CPU capabilities don't change at runtime, so repeated calls must produce
    // the same string and leave the dispatcher in the same state.
    const auto first{sha256_dispatch::AutoDetect()};
    const auto second{sha256_dispatch::AutoDetect()};
    BOOST_CHECK_EQUAL(first, second);
}

#if defined(__x86_64__) || defined(__amd64__)
BOOST_AUTO_TEST_CASE(x86_64_advertises_known_token)
{
    // On x86_64 the host is either "standard" (no SIMD detected) or has one
    // or more of the known SIMD tokens. No other shape is currently valid.
    const auto result{sha256_dispatch::AutoDetect()};
    BOOST_CHECK(result == "standard"
             || result.find("sse4") != std::string::npos
             || result.find("shani") != std::string::npos);
}
#endif

BOOST_AUTO_TEST_SUITE_END()
