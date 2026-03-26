// Copyright (c) 2026 BSV Blockchain Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "streams.h"

#include <array>
#include <concepts>
#include <vector>
#include <cstdint>

namespace
{
    // Verify byte-sized iterators ARE accepted
    static_assert(std::constructible_from<CDataStream,
                                          std::vector<uint8_t>::iterator,
                                          std::vector<uint8_t>::iterator,
                                          int, int>);

    static_assert(std::constructible_from<CDataStream,
                                          std::vector<char>::iterator,
                                          std::vector<char>::iterator,
                                          int, int>);

    static_assert(std::constructible_from<CDataStream,
                                          const uint8_t*,
                                          const uint8_t*,
                                          int, int>);

    static_assert(std::constructible_from<CDataStream,
                                          const char*,
                                          const char*,
                                          int, int>);

    // Verify non-byte iterators are REJECTED
    static_assert(!std::constructible_from<CDataStream,
                                           std::vector<uint32_t>::iterator,
                                           std::vector<uint32_t>::iterator,
                                           int, int>);

    static_assert(!std::constructible_from<CDataStream,
                                           std::vector<int>::iterator,
                                           std::vector<int>::iterator,
                                           int, int>);

    static_assert(!std::constructible_from<CDataStream,
                                           std::vector<uint16_t>::iterator,
                                           std::vector<uint16_t>::iterator,
                                           int, int>);

    static_assert(!std::constructible_from<CDataStream,
                                           std::vector<double>::iterator,
                                           std::vector<double>::iterator,
                                           int, int>);

    static_assert(!std::constructible_from<CDataStream,
                                           std::vector<bool>::const_iterator,
                                           std::vector<bool>::const_iterator,
                                           int, int>);

    static_assert(!std::constructible_from<CDataStream,
                                           std::vector<signed char>::iterator,
                                           std::vector<signed char>::iterator,
                                           int, int>);

    static_assert(!std::constructible_from<CDataStream,
                                           std::vector<int8_t>::iterator,
                                           std::vector<int8_t>::iterator,
                                           int, int>);

    // Verify range constructor
    static_assert(std::constructible_from<CDataStream,
                                          const std::array<char, 42>&,
                                          int, int>);

    static_assert(std::constructible_from<CDataStream,
                                          const std::vector<uint8_t>&,
                                          int, int>);

    static_assert(!std::constructible_from<CDataStream,
                                           const std::vector<uint32_t>&,
                                           int, int>);

    static_assert(!std::constructible_from<CDataStream,
                                           const std::vector<int>&,
                                           int, int>);

    static_assert(!std::constructible_from<CDataStream,
                                           const std::vector<double>&,
                                           int, int>);

    static_assert(!std::constructible_from<CDataStream,
                                           const std::vector<bool>&,
                                           int, int>);

    static_assert(!std::constructible_from<CDataStream,
                                           const std::vector<signed char>&,
                                           int, int>);

    static_assert(!std::constructible_from<CDataStream,
                                           const std::vector<int8_t>&,
                                           int, int>);
}

