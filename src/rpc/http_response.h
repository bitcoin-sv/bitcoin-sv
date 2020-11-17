// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include <streams.h>
#include <version.h>

#include <string>
#include <vector>

namespace rpc::client
{

/**
 * Base class for HTTP RPC/REST responses.
 */
class HTTPResponse
{
  public:
    HTTPResponse() = default;
    virtual ~HTTPResponse() = default;

    void SetStatus(int status) { mStatus = status; };
    void SetError(int error) { mError = error; }
    int GetStatus() const { return mStatus; }
    int GetError() const { return mError; }

    virtual void SetBody(const unsigned char* body, size_t size) = 0;
    virtual bool IsEmpty() const = 0;

  private:

    int mStatus {0};
    int mError  {0};
};

/**
 * A string formatted HTTP response
 */
class StringHTTPResponse : public HTTPResponse
{
  public:
    const std::string& GetBody() const { return mBody; }

    void SetBody(const unsigned char* body, size_t size) override;
    bool IsEmpty() const override { return mBody.empty(); }

  private:
    std::string mBody {};
};

/**
 * A binary (byte array) HTTP response
 */
class BinaryHTTPResponse : public HTTPResponse
{
  public:
    const std::vector<uint8_t>& GetBody() const { return mBody; }

    void SetBody(const unsigned char* body, size_t size) override;
    bool IsEmpty() const override { return mBody.empty(); }

    // Helper for deserialising binary responses
    template<typename T>
    BinaryHTTPResponse& operator>>(T&& obj)
    {
        CDataStream stream { mBody, SER_NETWORK, PROTOCOL_VERSION };
        stream >> std::forward<T>(obj);
        return *this;
    }

  private:
    std::vector<uint8_t> mBody {};
};

}

