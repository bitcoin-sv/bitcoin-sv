// Copyright (c) 2026 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

// Span-typed entry point for the SSE4 streaming compression transform.
// Forwards to the vendored raw-pointer overload in sha256_sse4.cpp,
// which is byte-identical to upstream.

#include "crypto/sha256_sse4.h"

#if defined(__x86_64__) || defined(__amd64__)

namespace sha256_sse4
{
    void Transform(std::uint32_t* s, const unsigned char* chunk, std::size_t blocks);
}

void sha256_sse4::Transform(const std::span<const std::uint8_t> ip,
                            const std::span<std::uint32_t, 8> op)
{
    Transform(op.data(), ip.data(), ip.size_bytes() / 64);
}

#endif // __x86_64__ || __amd64__
