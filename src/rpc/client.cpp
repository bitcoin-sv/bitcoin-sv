// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include <rpc/client.h>
#include <rpc/http_protocol.h>
#include <rpc/http_request.h>
#include <rpc/http_response.h>
#include <support/events.h>
#include <tinyformat.h>
#include <utilstrencodings.h>

#include <event2/buffer.h>

namespace
{
    // Callback for HTTP error
    void HTTPErrorCallback(enum evhttp_request_error err, void* ctx)
    {
        rpc::client::HTTPResponse* response { static_cast<rpc::client::HTTPResponse*>(ctx) };
        response->SetError(err);
    }

    // Callback for completed HTTP request
    void HTTPRequestDoneCallback(struct evhttp_request* req, void* ctx)
    {
        rpc::client::HTTPResponse* response { static_cast<rpc::client::HTTPResponse*>(ctx) };

        if(req == nullptr)
        {   
            /**
             * If req is nullptr, it means an error occurred while connecting: the
             * error code will have been passed to HTTPErrorCallback.
             */
            response->SetStatus(0);
            return;
        }

        response->SetStatus(evhttp_request_get_response_code(req));

        struct evbuffer* buf { evhttp_request_get_input_buffer(req) };
        if(buf)
        {   
            size_t size { evbuffer_get_length(buf) };
            response->SetBody(evbuffer_pullup(buf, size), size);
            evbuffer_drain(buf, size);
        }
    }

    // Convert error codes to messages
    const char* HTTPErrorString(int code)
    {
        switch (code)
        {
            case EVREQ_HTTP_TIMEOUT:
                return "timeout reached";
            case EVREQ_HTTP_EOF:
                return "EOF reached";
            case EVREQ_HTTP_INVALID_HEADER:
                return "error while reading header, or invalid header";
            case EVREQ_HTTP_BUFFER_ERROR:
                return "error encountered while reading or writing";
            case EVREQ_HTTP_REQUEST_CANCEL:
                return "request was canceled";
            case EVREQ_HTTP_DATA_TOO_LONG:
                return "response body is larger than allowed";
            default:
                return "unknown";
        }
    }

    // Convert our HTTP command type to libevent type
    evhttp_cmd_type ConvertCmdType(rpc::client::RequestCmdType cmdType)
    {
        switch(cmdType)
        {
            case(rpc::client::RequestCmdType::GET):
                return EVHTTP_REQ_GET;
            case(rpc::client::RequestCmdType::POST):
                return EVHTTP_REQ_POST;
            default:
                throw std::runtime_error("Unsupported HTTP command type");
        }
    }
}

namespace rpc::client
{

// Submit a request and wait for a response
void RPCClient::SubmitRequest(const HTTPRequest& request, HTTPResponse* response) const
{
    // Obtain event base
    raii_event_base base { obtain_event_base() };

    // Synchronously look up hostname
    raii_evhttp_connection evcon { obtain_evhttp_connection_base(base.get(), mConfig.GetServerIP(), mConfig.GetServerPort()) };
    evhttp_connection_set_timeout(evcon.get(), mConfig.GetConnectionTimeout());

    // Create request
    raii_evhttp_request req { obtain_evhttp_request(HTTPRequestDoneCallback, static_cast<void*>(response)) };
    if(req == nullptr)
    {
        throw std::runtime_error("Create http request failed");
    }
    evhttp_request_set_error_cb(req.get(), HTTPErrorCallback);

    // Add required headers
    struct evkeyvalq* output_headers { evhttp_request_get_output_headers(req.get()) };
    assert(output_headers);
    evhttp_add_header(output_headers, "Host", mConfig.GetServerIP().c_str());
    evhttp_add_header(output_headers, "Connection", "close");
    if(mConfig.UsesAuth())
    {
        evhttp_add_header(output_headers, "Authorization",
            (std::string { "Basic " } + EncodeBase64(mConfig.GetCredentials())).c_str());
    }

    // Attach request data
    const std::string& strRequest { request.GetContents() };
    struct evbuffer* output_buffer { evhttp_request_get_output_buffer(req.get()) };
    assert(output_buffer);
    evbuffer_add(output_buffer, strRequest.data(), strRequest.size());

    // Encode any endpoint to the URI and make the request
    const std::string& endPoint { request.GetEndpoint() };
    int res { evhttp_make_request(evcon.get(), req.get(), ConvertCmdType(request.GetCommand()), endPoint.c_str()) };

    // Ownership moved to evcon in above call
    req.release();
    if(res != 0)
    {   
        throw CConnectionFailed("Send http request failed");
    }

    // Send request and wait for response
    event_base_dispatch(base.get());

    // Check response
    int responseStatus { response->GetStatus() };
    if(responseStatus == 0)
    {   
        throw CConnectionFailed(strprintf(
            "couldn't connect to server: %s (code %d)\n(make sure server is "
            "running and you are connecting to the correct RPC port)",
            HTTPErrorString(response->GetError()), response->GetError()));
    }
    else if(responseStatus == HTTP_UNAUTHORIZED)
    {   
        throw std::runtime_error("incorrect rpcuser or rpcpassword (authorization failed)");
    }
    else if(responseStatus >= 400 && responseStatus != HTTP_BAD_REQUEST &&
               responseStatus != HTTP_NOT_FOUND &&
               responseStatus != HTTP_INTERNAL_SERVER_ERROR)
    {   
        throw std::runtime_error(strprintf("server returned HTTP error %d", responseStatus));
    }
    else if(response->IsEmpty())
    {
        throw std::runtime_error("no response from server");
    }
}

} // namespace rpc::client

