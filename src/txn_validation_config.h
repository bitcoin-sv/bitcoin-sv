// Copyright (c) 2019 The Bitcoin SV developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include <chrono>

enum class PTVTaskScheduleStrategy
{
    // Legacy chain detector (can't handle graphs, can't handle txn out-of-order).
    CHAIN_DETECTOR,
    // Schedules txn validation in topological order within one batch of transactions.
    TOPO_SORT
};

/** A default ratio for max number of standard transactions per thread. */
static constexpr uint64_t DEFAULT_MAX_STD_TXNS_PER_THREAD_RATIO = 1000;
/** A default ratio for max number of non-standard transactions per thread. */
static constexpr uint64_t DEFAULT_MAX_NON_STD_TXNS_PER_THREAD_RATIO = 1000;
/** Use CPU time used instead of wall clock time for validation duration measurement */
static constexpr bool DEFAULT_VALIDATION_CLOCK_CPU = true;
/** The maximum time for standard transaction validation before we terminate the task */
static constexpr std::chrono::milliseconds DEFAULT_MAX_STD_TXN_VALIDATION_DURATION =
	std::chrono::milliseconds{3};
/** The maximum time for non-standard transaction validation before we terminate the task */
static constexpr std::chrono::milliseconds DEFAULT_MAX_NON_STD_TXN_VALIDATION_DURATION =
	std::chrono::seconds{1};
/** The maximum unused validation time to carry over to the next transaction in the chain */
static constexpr std::chrono::milliseconds DEFAULT_MAX_TXN_CHAIN_VALIDATION_BUDGET =
    std::chrono::milliseconds{50};
/** Default for which scheduler to use. */
static constexpr PTVTaskScheduleStrategy DEFAULT_PTV_TASK_SCHEDULE_STRATEGY = PTVTaskScheduleStrategy::TOPO_SORT;
