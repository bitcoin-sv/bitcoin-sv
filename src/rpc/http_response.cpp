// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include <rpc/http_response.h>

//NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast)

void rpc::client::StringHTTPResponse::SetBody(const unsigned char* body, size_t size)
{
    if(body)
    {
        const char* data { reinterpret_cast<const char*>(body) };
        mBody = { data, size };
    }
}

void rpc::client::JSONHTTPResponse::SetBody(const unsigned char* body, size_t size)
{
    if(body)
    {
        const char* data { reinterpret_cast<const char*>(body) };
        mBody.read(data, size);
    }
}

void rpc::client::BinaryHTTPResponse::SetBody(const unsigned char* body, size_t size)
{
    if(body)
    {
        const uint8_t* data { reinterpret_cast<const uint8_t*>(body) };
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        mBody = { data, data + size };
    }
}

//NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast)

