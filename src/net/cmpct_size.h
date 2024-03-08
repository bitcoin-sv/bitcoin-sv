// Copyright (c) 2023 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

// Requires s.data() is the start of a compact_size 
// Returns:
// bytes_read == 0, bytes_reqd
// bytes_read > 0, value
std::pair<size_t, uint64_t> parse_compact_size(const std::span<const uint8_t>);

