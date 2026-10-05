// Copyright (c) 2026 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

// Span-typed entry point for the SHA-NI streaming compression transform.
// Compiled without SHA-NI flags so it can live in the main crypto archive:
// the body does no intrinsic work, it just unwraps the span and forwards to
// the vendored raw-pointer overload in sha256d64_shani.cpp (which is compiled
// with -msha / -msse4.1 / -mssse3 and lives in libbitcoin_crypto_shani).

#include "crypto/sha256_shani.h"

#if defined(__x86_64__) || defined(__amd64__)

namespace sha256_x86_shani
{
    void Transform(std::uint32_t* s, const unsigned char* chunk, std::size_t blocks);
}

void sha256_shani::Transform(const std::span<const std::uint8_t> ip,
                             const std::span<std::uint32_t, 8> op)
{
    sha256_x86_shani::Transform(op.data(), ip.data(), ip.size_bytes() / 64);
}

#endif // __x86_64__ || __amd64__
