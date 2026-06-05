// Copyright (c) 2026 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "uint256.h"

#include <type_traits>

static_assert(sizeof(uint256) == 32);
static_assert(std::is_trivially_copyable_v<uint256>);
