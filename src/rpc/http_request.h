// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include <cfile_util.h>
#include <event2/http.h>
#include <rpc/client_config.h>
#include <univalue.h>

#include <sstream>
#include <string>
#include <vector>

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
    HTTPRequest(const std::string& endpoint, RequestCmdType cmd)
        : mEndpoint{endpoint}, mCmdType{cmd}
    {}
    HTTPRequest(const std::string& endpoint, const std::vector<uint8_t>& contents, RequestCmdType cmd)
        : mEndpoint{endpoint}, mContents{contents}, mContentsSize{contents.size()}, mCmdType{cmd}
    {}
    HTTPRequest(const std::string& endpoint, const std::string& contents, RequestCmdType cmd)
        : mEndpoint{endpoint}, mContents{contents.begin(), contents.end()}, mContentsSize{contents.size()}, mCmdType{cmd}
    {}
    HTTPRequest(const std::string& endpoint, UniqueFileDescriptor&& contentsFD, size_t contentsSize, RequestCmdType cmd)
        : mEndpoint{endpoint}, mContentsFD{std::move(contentsFD)}, mContentsSize{contentsSize}, mCmdType{cmd}
    {}

    // Get request endpoint
    const std::string& GetEndpoint() const { return mEndpoint; }

    // Get request body contents
    const std::vector<uint8_t>& GetContents() const { return mContents; }
    const UniqueFileDescriptor& GetContentsFD() const { return mContentsFD; }
    UniqueFileDescriptor& GetContentsFD() { return mContentsFD; }
    size_t GetContentsSize() const { return mContentsSize; }

    // Get HTTP command type
    RequestCmdType GetCommand() const { return mCmdType; }

    // Get additional header fields
    using HeaderField = std::pair<std::string, std::string>;
    using HeaderList = std::vector<HeaderField>;
    const HeaderList& GetHeaders() const { return mHeaders; }

    // Add a header field
    void AddHeader(const HeaderField& header) { mHeaders.push_back(header); }


    // Factory method to make a JSON RPC request
    static HTTPRequest CreateJSONRPCRequest(const RPCClientConfig& config, const std::string& method, const UniValue& params);

    // Factory method to make a generic JSON POST request
    static HTTPRequest CreateJSONPostRequest(const RPCClientConfig& config, const UniValue& json);

    // Factory method to make a generic JSON POST request
    static HTTPRequest CreateJSONPostRequest(const RPCClientConfig& config, const std::string contents);

    // Factory method to make a query request to a double-spend endpoint
    static HTTPRequest CreateDSEndpointQueryRequest(const RPCClientConfig& config, const std::string& txid);

    // Factory method to make a submit request to a double-spend endpoint
    template<typename... Params>
    static HTTPRequest CreateDSEndpointSubmitRequest(
        const RPCClientConfig& config,
        UniqueFileDescriptor&& contentsFD,
        size_t contentsSize,
        Params... uriParamPairs)
    {
        // Format endpoint
        std::stringstream endpoint {};
        endpoint << config.GetEndpoint() << "submit";

        bool firstParam {true};
        auto isFirstParam = [&firstParam]() {
            if(firstParam) {
                firstParam = false;
                return true;
            }
            return false;
        };

        ((endpoint << (isFirstParam()? "?" : "&") << EncodeURI(uriParamPairs.first) << "=" << EncodeURI(uriParamPairs.second)), ...);

        // Create request with extra header
        HTTPRequest request { endpoint.str(), std::move(contentsFD), contentsSize, RequestCmdType::POST };
        request.AddHeader({"Content-Type", "application/octet-stream"});
        return request;
    }

    // Factory method to make a signing request to a miner ID generator
    static HTTPRequest CreateMinerIdGeneratorSigningRequest(const RPCClientConfig& config,
                                                            const std::string& alias,
                                                            const std::string& hash);

    // Factory method to make a get-minerid request to a miner ID generator
    static HTTPRequest CreateGetMinerIdRequest(const RPCClientConfig& config,
                                                     const std::string& alias);

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

    std::string mEndpoint {"/"};
    std::vector<uint8_t> mContents {};
    UniqueFileDescriptor mContentsFD {};
    size_t mContentsSize {0};
    HeaderList mHeaders {};
    RequestCmdType mCmdType {RequestCmdType::POST};
};

}

