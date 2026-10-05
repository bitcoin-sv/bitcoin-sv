// Copyright (c) 2026 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include <cstdint>
#include <span>

namespace sha256_sse4
{
    void Transform(std::span<const std::uint8_t> ip,
                   std::span<uint32_t, 8> op);
}
