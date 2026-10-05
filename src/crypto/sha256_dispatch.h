// Copyright (c) 2026 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#pragma once

#include "crypto/sha256_stream_scalar.h"
#include "crypto/sha256d64_scalar.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace sha256_dispatch
{
    constexpr std::size_t hash_bytes{32};
    constexpr std::size_t d_pair_bytes{2 * hash_bytes};
    
    // NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)

    //   ip: input message data; must be a multiple of 64 bytes (one or
    //       more SHA-256 blocks). Dynamic extent because the call
    //       processes any number of blocks per call.
    //   op: the SHA-256 working state; 8 native-endian uint32_t words.
    //       Fixed extent because SHA-256 has exactly 8 state words
    //       (NIST FIPS 180-4). Read and written in place.
    using StreamType = void (*)(std::span<const std::uint8_t> ip,
                                std::span<std::uint32_t, 8> op);
    inline StreamType stream{sha256_stream_scalar::Transform};

    using DBlock2wayType = void (*)(std::span<const std::uint8_t, 2 * d_pair_bytes>,
                                    std::span<std::uint8_t, 2 * hash_bytes>);
    inline DBlock2wayType d_block_2way{nullptr};

    using DBlock1wayType = void (*)(std::span<const std::uint8_t, d_pair_bytes>,
                                    std::span<std::uint8_t, hash_bytes>);
    inline DBlock1wayType d_block_1way{sha256d64_scalar::Transform};

    // NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

    std::string AutoDetect();
}

