// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include <rpc/http_request.h>
#include <rpc/protocol.h>

namespace rpc::client
{

// Create a properly formatted JSONRPCRequest
HTTPRequest HTTPRequest::CreateJSONRPCRequest(const RPCClientConfig& config, const std::string& method, const UniValue& params)
{
    // Format contents
    std::string contents { JSONRPCRequestObj(method, params, 1).write() + "\n" };

    // Format endpoint
    std::string endPoint {"/"};
    if(!config.GetWallet().empty())
    {
        // We need to pass a wallet ID via the URI
        endPoint = "/wallet/" + EncodeURI(config.GetWallet());
    }

    // Create request
    return { endPoint, contents, RequestCmdType::POST };
}

// Create a generic JSON POST request
HTTPRequest HTTPRequest::CreateJSONPostRequest(const RPCClientConfig& config, const UniValue& json)
{
    // Format contents
    std::string contents { json.write() + "\r\n" };
    return CreateJSONPostRequest(config, std::move(contents));
}

// Create a generic JSON POST request
HTTPRequest HTTPRequest::CreateJSONPostRequest(const RPCClientConfig& config, const std::string contents)
{
    // Create request
    HTTPRequest request { config.GetEndpoint(), contents, RequestCmdType::POST };
    request.AddHeader({"Content-Type", "application/json"});
    return request;
}

// Create a properly formatted query request to a double-spend endpoint
HTTPRequest HTTPRequest::CreateDSEndpointQueryRequest(const RPCClientConfig& config, const std::string& txid)
{
    // Format endpoint
    std::string endpoint { config.GetEndpoint() + "query/" + txid };

    // Create request
    return { endpoint, RequestCmdType::GET };
}

// Create a signing request to miner ID generator
HTTPRequest HTTPRequest::CreateMinerIdGeneratorSigningRequest(const RPCClientConfig& config,
                                                              const std::string& alias,
                                                              const std::string& hash)
{
    // Format endpoint
    std::string endpoint { config.GetEndpoint() + "/minerid/" + alias + "/pksign/" + hash };

    // Create request
    return { endpoint, RequestCmdType::GET };
}

// Create a request to get the current minerid from the generator
HTTPRequest HTTPRequest::CreateGetMinerIdRequest(const RPCClientConfig& config, const std::string& alias)
{
    // Format endpoint
    std::string endpoint { config.GetEndpoint() + "/minerid/" + alias};

    // Create request
    return { endpoint, RequestCmdType::GET };
}

}

