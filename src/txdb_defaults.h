// Copyright (c) 2024 BSV Blockchain Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.
  
#pragma once

#include "consensus/consensus.h"

#include <cstddef>

// Default config values for the coins DB
struct CoinsDBDefaults
{
    static constexpr size_t MIN_LEVELDB_FILE_SIZE { 256 * ONE_KIBIBYTE };
    static constexpr size_t DEFAULT_MAX_LEVELDB_FILE_SIZE { 32 * ONE_MEBIBYTE };
};

