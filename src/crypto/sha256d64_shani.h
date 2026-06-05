// Copyright (c) 2026 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include <cstdint>
#include <span>

namespace sha256d64_shani
{
    // SHA-NI 2-way Merkle-pair double-SHA256 transform.
    // Consumes 2 contiguous 64-byte Merkle pairs (128 bytes) from `ip`
    // and produces 2 contiguous 32-byte SHA256d digests (64 bytes) in `op`.
    void Transform_2way(std::span<const std::uint8_t, 128> ip,
                        std::span<std::uint8_t, 64> op);
}
