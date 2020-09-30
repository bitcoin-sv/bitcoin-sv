// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include <chainparamsbase.h>
#include <rpc/client_config.h>
#include <rpc/protocol.h>
#include <util.h>
#include <utilstrencodings.h>

#include <stdexcept>

namespace
{
    // Split out host,port and method from DS authority URL
    void SplitDSAuthorityURL(const std::string& url, std::string& host, int& port, std::string& endpoint)
    {
        auto urlLastPos { url.size() - 1 };

        // Host and port either at the start or follow '://'
        auto addrStart { url.find("://") };
        if(addrStart == std::string::npos)
        {
            addrStart = 0;
        }
        else
        {
            // Check for any protocol other than http
            std::string protocol { url.substr(0, addrStart) };
            if(protocol != "http")
            {
                throw std::runtime_error("Unsupported protocol in URL: " + protocol);
            }

            addrStart += 3;
        }

        if(urlLastPos > addrStart)
        {
            // End of the address is either the rest of the URL or until a '/' seperator
            auto addrEnd { url.find('/', addrStart) };
            std::string::size_type addrLen {};
            if(addrEnd == std::string::npos)
            {
                addrLen = url.size() - addrStart;
            }
            else
            {
                addrLen = addrEnd - addrStart;
            }

            if(addrLen > 0)
            {
                // Get address and split into host & port
                std::string addr { url.substr(addrStart, addrLen) };
                SplitHostPort(addr, port, host);

                // Endpoint optionally follows address
                auto endPointStart { addrStart + addrLen };
                if(endPointStart < url.size())
                {
                    endpoint = url.substr(endPointStart);
                }

                // Got everything, so return without error
                return;
            }
        }

        throw std::runtime_error("Badly formatted URL: " + url);
    }
}

namespace rpc::client
{

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

RPCClientConfig RPCClientConfig::CreateForDSA()
{
    RPCClientConfig config {};

    // Firstly make sure all required config options have been provided
    if(! gArgs.IsArgSet("-dsauthorityurl"))
    {
        throw std::runtime_error("Missing config parameter -dsauthorityurl");
    }

    // Get host,port and method from DS authority URL
    config.mServerPort = DEFAULT_DS_AUTHORITY_PORT;
    SplitDSAuthorityURL(gArgs.GetArg("-dsauthorityurl", ""), config.mServerIP, config.mServerPort, config.mEndpoint);

    // Get timeout
    config.mConnectionTimeout = gArgs.GetArg("-dsauthoritytimeout", DEFAULT_DS_AUTHORITY_TIMEOUT);

    return config;
}

RPCClientConfig RPCClientConfig::CreateForDSA(const std::string& url)
{
    RPCClientConfig config {};

    // Get host,port and method from provided DS authority URL
    config.mServerPort = DEFAULT_DS_AUTHORITY_PORT;
    SplitDSAuthorityURL(url, config.mServerIP, config.mServerPort, config.mEndpoint);

    return config;
}

} // namespace rpc::client

