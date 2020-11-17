// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include <rpc/client_utils.h>

#include <string>

namespace rpc::client
{

/**
 * Wrapper for RPC client config.
 */
class RPCClientConfig
{
  public:

    // Some default config values (here so they can be checked from unit tests)
    static constexpr int DEFAULT_DS_AUTHORITY_PORT { 80 };
    static constexpr int DEFAULT_DS_AUTHORITY_TIMEOUT { 60 };

    // Factory methods
    static RPCClientConfig CreateForBitcoind();
    static RPCClientConfig CreateForDSA();
    static RPCClientConfig CreateForDSA(const std::string& url);

    // Accessors
    const std::string& GetServerIP() const { return mServerIP; }
    int GetServerPort() const { return mServerPort; }
    int GetConnectionTimeout() const { return mConnectionTimeout; }
    const std::string& GetCredentials() const { return mUsernamePassword; }
    const std::string& GetWallet() const { return mWallet; }
    const std::string& GetEndpoint() const { return mEndpoint; }

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

};

} // namespace rpc::client

