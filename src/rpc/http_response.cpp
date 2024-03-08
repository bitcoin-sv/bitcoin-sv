// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include <rpc/http_response.h>

namespace rpc::client
{

void StringHTTPResponse::SetBody(const unsigned char* body, size_t size)
{
    if(body)
    {
        const char* data { reinterpret_cast<const char*>(body) };
        mBody = { data, size };
    }
}

void JSONHTTPResponse::SetBody(const unsigned char* body, size_t size)
{
    if(body)
    {
        const char* data { reinterpret_cast<const char*>(body) };
        mBody.read(data, size);
    }
}

void BinaryHTTPResponse::SetBody(const unsigned char* body, size_t size)
{
    if(body)
    {
        const uint8_t* data { reinterpret_cast<const uint8_t*>(body) };
        mBody = { data, data + size };
    }
}

}

