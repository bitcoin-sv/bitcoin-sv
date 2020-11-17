// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include <event2/http.h>
#include <rpc/client_config.h>
#include <univalue.h>

#include <sstream>
#include <string>

namespace rpc::client
{

/**
 * Enumerate request command types.
 */
enum class RequestCmdType { GET, POST };

/**
 * Storage class for different formats of HTTP requests.
 */
class HTTPRequest
{
  public:
    HTTPRequest() = default;
    HTTPRequest(const std::string& endpoint, const std::string& contents, RequestCmdType cmd)
        : mEndpoint{endpoint}, mContents{contents}, mCmdType{cmd}
     {}

    // Get request endpoint
    const std::string& GetEndpoint() const { return mEndpoint; }

    // Get request body contents
    const std::string& GetContents() const { return mContents; }

    // Get HTTP command type
    RequestCmdType GetCommand() const { return mCmdType; }


    // Factory method to make JSON RPC request
    static HTTPRequest CreateJSONRPCRequest(const RPCClientConfig& config, const std::string& method, const UniValue& params);

    // Factory method to make REST Post request
    static HTTPRequest CreateRESTPostRequest(const RPCClientConfig& config, const UniValue& params);

    // Factory method to make REST Get request
    template<typename... Params>
    static HTTPRequest CreateRESTGetRequest(const RPCClientConfig& config, Params... uriParams)
    {
        // Format endpoint
        std::stringstream endpoint {};
        endpoint << config.GetEndpoint();
        ((endpoint << "/" << EncodeURI(uriParams)), ...);

        // Create request
        return { endpoint.str(), "", RequestCmdType::GET };
    }

  private:

    // Helper method to encode URI components
    template<typename T>
    static std::string EncodeURI(const T& uri)
    {
        std::stringstream toEncodeStr {};
        toEncodeStr << uri;
        std::string toEncode { toEncodeStr.str() };

        std::string encoded {};
        char* encodedURI { evhttp_uriencode(toEncode.data(), toEncode.size(), false) };
        if(encodedURI)
        {   
            encoded = encodedURI;
            free(encodedURI);
        }
        else
        {   
            throw std::runtime_error("uri-encode failed for: " + toEncode);
        }

        return encoded;
    }

    std::string mEndpoint   {"/"};
    std::string mContents   {};
    RequestCmdType mCmdType {RequestCmdType::POST};
};

}

