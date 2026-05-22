// Copyright (c) 2024 BSV Blockchain Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.
  
#pragma once

#include "consensus/consensus.h"

#include <algorithm>
#include <cstddef>
#include <utility>

// Default config values for the coins DB
struct CoinsDBDefaults
{
    static constexpr size_t MIN_LEVELDB_FILE_SIZE { 256 * ONE_KIBIBYTE };
    static constexpr size_t DEFAULT_MAX_LEVELDB_FILE_SIZE { 128 * ONE_MEBIBYTE };

    // -coinsdbcache default - total budget for the coins LevelDB cache plus
    // the in-memory UTXO flush threshold. Split 1/8 to LevelDB (capped at
    // MAX_COINS_LEVELDB_CACHE_SIZE), remainder to in-memory.
    static constexpr int64_t DEFAULT_COINSDB_CACHE_SIZE { 8 * ONE_GIBIBYTE };
    // -coinsdbcache min
    static constexpr int64_t MIN_COINSDB_CACHE_SIZE { 4 * ONE_MEBIBYTE };
    // -coinsdbcache max
    static constexpr int64_t MAX_COINSDB_CACHE_SIZE { 64 * ONE_GIBIBYTE };

    // Max LevelDB cache for chainstate (MiB)
    static constexpr int64_t MAX_COINS_LEVELDB_CACHE_SIZE { 2 * ONE_GIBIBYTE };

    // Work out the split between LevelDB and in-memory cache based on the
    // requested total cache size and the 1/8 split to LevelDB.
    [[nodiscard]] static constexpr std::pair<int64_t, int64_t> ComputeCoinsDbCacheSplit(int64_t requestedBytes)
    {   
        const int64_t clamped = std::clamp(
            requestedBytes,
            MIN_COINSDB_CACHE_SIZE,
            MAX_COINSDB_CACHE_SIZE);
        const int64_t levelDb = std::min(clamped / 8, MAX_COINS_LEVELDB_CACHE_SIZE);
        return {levelDb, clamped - levelDb};
    }
};

