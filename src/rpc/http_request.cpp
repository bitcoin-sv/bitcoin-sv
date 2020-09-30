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

// Create a properly formatted RESTPostRequest
HTTPRequest HTTPRequest::CreateRESTPostRequest(const RPCClientConfig& config, const UniValue& params)
{
    // Format contents
    UniValue request { UniValue::VOBJ };
    request.pushKVs(params);
    std::string contents { request.write() + "\n" };

    // Format endpoint
    std::string endpoint { config.GetEndpoint() + "/submit" };

    // Create request
    return { endpoint, contents, RequestCmdType::POST };
}

}
