// Copyright (c) 2019 The Bitcoin SV developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include <chrono>

/** A default ratio for max number of standard transactions per thread. */
static constexpr uint64_t DEFAULT_MAX_STD_TXNS_PER_THREAD_RATIO = 1000;
/** A default ratio for max number of non-standard transactions per thread. */
static constexpr uint64_t DEFAULT_MAX_NON_STD_TXNS_PER_THREAD_RATIO = 1000;
/** The maximum wall time for standard transaction validation before we terminate the task */
static constexpr std::chrono::milliseconds DEFAULT_MAX_STD_TXN_VALIDATION_DURATION =
	std::chrono::milliseconds{5};
/** The maximum wall time for non-standard transaction validation before we terminate the task */
static constexpr std::chrono::milliseconds DEFAULT_MAX_NON_STD_TXN_VALIDATION_DURATION =
	std::chrono::seconds{1};
