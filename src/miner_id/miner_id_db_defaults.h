// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include <cstddef>
#include <cstdint>

/**
 * Default configuration values used by the miner ID database.
 *
 * Kept separately here to reduce #include pollution in other places within
 * the build.
 */
struct MinerIdDatabaseDefaults
{
    // Default DB enabled or disabled
    static constexpr bool DEFAULT_MINER_ID_ENABLED {true};

    // Default LevelDB cache size
    static constexpr uint64_t DEFAULT_CACHE_SIZE { 1 << 20 };
    static constexpr uint64_t MAX_CACHE_SIZE { 1 << 24 };

    // Default number of old miner IDs to keep before pruning.
    static constexpr size_t DEFAULT_MINER_IDS_TO_KEEP {10};

    // Default and maximum values for N in miner reputation test
    static constexpr uint32_t DEFAULT_MINER_REPUTATION_N {2016};    // 2 weeks
    static constexpr uint32_t MAX_MINER_REPUTATION_N {26208};       // 6 months

    // Default and maximum values for M in miner reputation test
    static constexpr uint32_t DEFAULT_MINER_REPUTATION_M {28};
    static constexpr uint32_t MAX_MINER_REPUTATION_M { MAX_MINER_REPUTATION_N };

    // Dafualt scale factor to use for M in miner reputation test
    static constexpr double DEFAULT_M_SCALE_FACTOR {1.5};
};

