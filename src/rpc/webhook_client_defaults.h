// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

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
};

}   // namespace
