// Copyright (c) 2026 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

// Span-typed entry point for the SHA-NI 2-way D64 transform. Compiled
// without SHA-NI flags so it can live in the main crypto archive: the
// body does no intrinsic work, it just unwraps the fixed-extent spans
// and forwards to the vendored raw-pointer overload in
// sha256d64_shani.cpp (which is compiled with -msha / -msse4.1 / -mssse3
// and lives in libbitcoin_crypto_shani).

#include "crypto/sha256d64_shani.h"

#if defined(__x86_64__) || defined(__amd64__)

namespace sha256d64_x86_shani
{
    void Transform_2way(unsigned char* out, const unsigned char* in);
}

void sha256d64_shani::Transform_2way(const std::span<const std::uint8_t, 128> ip,
                                     std::span<std::uint8_t, 64> op)
{
    sha256d64_x86_shani::Transform_2way(op.data(), ip.data());
}

#endif // __x86_64__ || __amd64__
