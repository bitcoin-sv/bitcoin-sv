// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include <rpc/client_utils.h>

#include <string>
#include <cstdint>

class Config; // NOLINT(cppcoreguidelines-virtual-class-destructor)

namespace rpc::client
{

/**
 * Wrapper for RPC client config.
 */
class RPCClientConfig
{
  public:

    // Some default config values (here so they can be checked from unit tests)
    static constexpr int DEFAULT_DS_ENDPOINT_PORT { 80 };
    static constexpr int DEFAULT_DS_ENDPOINT_FAST_TIMEOUT { 5 };
    static constexpr int DEFAULT_DS_ENDPOINT_SLOW_TIMEOUT { 60 };

    // Factory methods
    static RPCClientConfig CreateForBitcoind();
    static RPCClientConfig CreateForDoubleSpendEndpoint(const Config& config,
                                                        const std::string& addr,
                                                        int timeout,
                                                        unsigned protocolVersion);
    static RPCClientConfig CreateForSafeModeWebhook(const Config& config);
    static RPCClientConfig CreateForDoubleSpendDetectedWebhook(const Config& config);
    static RPCClientConfig CreateForMinerIdGenerator(const Config& config, int timeout);

    // Accessors
    const std::string& GetServerIP() const { return mServerIP; }
    int GetServerPort() const { return mServerPort; }
    std::string GetServerHTTPHost() const;
    int GetConnectionTimeout() const { return mConnectionTimeout; }
    const std::string& GetCredentials() const { return mUsernamePassword; }
    const std::string& GetWallet() const { return mWallet; }
    const std::string& GetEndpoint() const { return mEndpoint; }
    bool GetValidEmptyResponse() const { return mValidEmptyResponse; }
    int64_t GetMaxResponseBodySize() const { return mMaxResponseBodySize; }
    int64_t GetMaxResponseHeadersSize() const { return mMaxResponseHeadersSize; }

    bool UsesAuth() const { return ! mUsernamePassword.empty(); }

  private:

    // Server address details
    std::string mServerIP {};
    int mServerPort {-1};

    // Connection timeout
    int mConnectionTimeout { DEFAULT_HTTP_CLIENT_TIMEOUT };

    // Server username:password, or auth cookie
    std::string mUsernamePassword {};

    // Special wallet endpoint
    std::string mWallet {};

    // The configured endpoint (may be extended elsewhere)
    std::string mEndpoint {};

    // Are empty responses to be expected?
    bool mValidEmptyResponse {false};

    // Maximum response body/headers size in bytes (0 for unlimited)
    int64_t mMaxResponseBodySize {0};
    int64_t mMaxResponseHeadersSize {0};

};

} // namespace rpc::client

