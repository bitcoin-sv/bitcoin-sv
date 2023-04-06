// Copyright (c) 2023 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE

#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>

#include "span.h"

// Requires s.data() is the start of a compact_size 
// Returns:
// bytes_read == 0, bytes_reqd
// bytes_read > 0, value
std::pair<size_t, uint64_t> parse_compact_size(const bsv::span<const uint8_t>);

