// Copyright (c) 2026 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include <cstdint>
#include <span>

namespace sha256_shani
{
    void Transform(std::span<const std::uint8_t> ip,
                   std::span<std::uint32_t, 8> op);
}
