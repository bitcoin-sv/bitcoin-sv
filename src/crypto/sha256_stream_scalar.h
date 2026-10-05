// Copyright (c) 2026 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include <cstdint>
#include <span>

// `ip` size must be a multiple of 64
namespace sha256_stream_scalar
{
    void Transform(std::span<const std::uint8_t> ip,
                   std::span<uint32_t, 8> op);
}
