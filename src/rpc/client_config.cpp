// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include <chainparamsbase.h>
#include <config.h>
#include <net/netbase.h>
#include <rpc/client_config.h>
#include <rpc/protocol.h>

#include <stdexcept>

namespace rpc::client
{

// Get a string representing the server address suitable for using
// in the HTTP Host header field.
std::string RPCClientConfig::GetServerHTTPHost() const
{
    try
    {
        // Try to lookup CNetAddr without resolving. Will fail if it's not a plain IP address
        CNetAddr addr {};
        if(LookupHost(mServerIP.c_str(), addr, false))
        {
            // Format appropriately depending on IPv4 or IPv6
            if(addr.IsIPv6())
            {
                // Add [] to make RFC3986 compliant
                return "[" + mServerIP + "]";
            }
            else
            {
                // Nothing special required for IPv4
                return mServerIP;
            }
        }
        else
        {
            // Must be a hostname not an address, so just return that
            return mServerIP;
        }
    }
    catch(const std::exception& e)
    {
        throw std::runtime_error(std::string { "Unable to lookup RPC client server address: " } + e.what());
    }
}

RPCClientConfig RPCClientConfig::CreateForBitcoind()
{
    RPCClientConfig config {};

    // In preference order, we choose the following for the port:
    //     1. -rpcport
    //     2. port in -rpcconnect (ie following : in ipv4 or ]: in ipv6)
    //     3. default port for chain
    int port { BaseParams().RPCPort() };
    SplitHostPort(gArgs.GetArg("-rpcconnect", DEFAULT_RPCCONNECT), port, config.mServerIP);
    config.mServerPort = gArgs.GetArg("-rpcport", port);

    // Get credentials
    if(gArgs.GetArg("-rpcpassword", "") == "")
    {
        // Try fall back to cookie-based authentication if no password is provided
        if(!GetAuthCookie(&config.mUsernamePassword))
        {
            throw std::runtime_error(strprintf(
                _("Could not locate RPC credentials. No authentication cookie "
                  "could be found, and RPC password is not set. See "
                  "-rpcpassword and -stdinrpcpass. Configuration file: (%s)"),
                GetConfigFile(gArgs.GetArg("-conf", BITCOIN_CONF_FILENAME)).string().c_str()));
        }
    }
    else
    {
        config.mUsernamePassword = gArgs.GetArg("-rpcuser", "") + ":" + gArgs.GetArg("-rpcpassword", "");
    }

    config.mConnectionTimeout = gArgs.GetArg("-rpcclienttimeout", DEFAULT_HTTP_CLIENT_TIMEOUT);
    config.mWallet = gArgs.GetArg("-rpcwallet", "");

    return config;
}

RPCClientConfig RPCClientConfig::CreateForDoubleSpendEndpoint(
    const Config& config,
    const std::string& addr,
    int timeout,
    unsigned protocolVersion)
{
    RPCClientConfig clientConfig {};

    // Get port
    clientConfig.mServerPort = config.GetDoubleSpendEndpointPort();

    // Set timeout
    clientConfig.mConnectionTimeout = timeout;

    // Set IP
    clientConfig.mServerIP = addr;

    // Initial endpoint
    std::stringstream str {};
    str << "/dsnt/" << protocolVersion << "/";
    clientConfig.mEndpoint = str.str();

    // Server sends empty responses
    clientConfig.mValidEmptyResponse = true;

    return clientConfig;
}

RPCClientConfig RPCClientConfig::CreateForSafeModeWebhook(const Config& config)
{
    RPCClientConfig clientConfig {};

    // Set IP/port
    clientConfig.mServerIP = config.GetSafeModeWebhookAddress();
    clientConfig.mServerPort = config.GetSafeModeWebhookPort();

    // Set endpoint
    clientConfig.mEndpoint = config.GetSafeModeWebhookPath();

    // Server sends empty responses
    clientConfig.mValidEmptyResponse = true;

    return clientConfig;
}

RPCClientConfig RPCClientConfig::CreateForDoubleSpendDetectedWebhook(const Config& config)
{
    RPCClientConfig clientConfig {};

    // Set IP/port
    clientConfig.mServerIP = config.GetDoubleSpendDetectedWebhookAddress();
    clientConfig.mServerPort = config.GetDoubleSpendDetectedWebhookPort();

    // Set endpoint
    clientConfig.mEndpoint = config.GetDoubleSpendDetectedWebhookPath();

    // Server sends empty responses
    clientConfig.mValidEmptyResponse = true;

    return clientConfig;
}

RPCClientConfig RPCClientConfig::CreateForMinerIdGenerator(const Config& config, int timeout)
{
    RPCClientConfig clientConfig {};

    // Set IP/port
    clientConfig.mServerIP = config.GetMinerIdGeneratorAddress();
    clientConfig.mServerPort = config.GetMinerIdGeneratorPort();

    // Set endpoint
    clientConfig.mEndpoint = config.GetMinerIdGeneratorPath();

    // Some http endpoints are not disruptive if they fail but very disruptive if they stall.
    // We should set timeout to a low value for those cases.
    clientConfig.mConnectionTimeout = timeout;

    return clientConfig;
}

} // namespace rpc::client

