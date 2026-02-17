// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include "consensus/consensus.h"

#include <cstddef>
#include <cstdint>

namespace rpc::client
{

/**
 * Default configuration values used by the webhook client.
 *
 * Kept separately here to reduce #include pollution elsewhere within the
 * build.
 */
struct WebhookClientDefaults
{
    // Default and maximum number of threads for asynchronous submission
    static constexpr size_t DEFAULT_NUM_THREADS { 4 };
    static constexpr size_t MAX_NUM_THREADS { 16 };

    // Default port for webhook notifications
    static constexpr int16_t DEFAULT_WEBHOOK_PORT { 80 };

    // Default maximum HTTP response body size for webhook endpoints (in KB)
    static constexpr uint64_t DEFAULT_MAX_RESPONSE_BODY_SIZE { 32 };
    static constexpr int64_t DEFAULT_MAX_RESPONSE_BODY_SIZE_BYTES { static_cast<int64_t>(DEFAULT_MAX_RESPONSE_BODY_SIZE * ONE_KILOBYTE) };

    // Default maximum HTTP response headers size for webhook endpoints (in KB)
    static constexpr uint64_t DEFAULT_MAX_RESPONSE_HEADERS_SIZE { 32 };
    static constexpr int64_t DEFAULT_MAX_RESPONSE_HEADERS_SIZE_BYTES { static_cast<int64_t>(DEFAULT_MAX_RESPONSE_HEADERS_SIZE * ONE_KILOBYTE) };
};

}   // namespace
