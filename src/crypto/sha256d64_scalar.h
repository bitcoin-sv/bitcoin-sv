// Copyright (c) 2026 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include <cstdint>
#include <span>

namespace sha256d64_scalar
{
    // Scalar Double-SHA256 transform for one 64-byte Merkle pair.
    // Consumes exactly one 64-byte pair from `ip` and produces one
    // 32-byte SHA256d digest in `op`. The fixed-extent spans encode
    // the size precondition at the type level.
    // Thin wrapper around the vendored sha256_d64_scalar::Transform
    // (originally bitcoin/bitcoin's sha256::TransformD64). Universal
    // fallback when no SIMD D64 implementation is selected at runtime.
    void Transform(std::span<const std::uint8_t, 64> ip,
                   std::span<std::uint8_t, 32> op);
}
