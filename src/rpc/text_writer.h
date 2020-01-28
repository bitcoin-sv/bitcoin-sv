// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include "consensus/consensus.h" // For ONE_MEGABYTE
#include "httpserver.h" // For HTTPRequest
#include <string>

class CTextWriter
{
public:
    virtual ~CTextWriter() {};
    virtual void Write(char val) = 0;
    virtual void Write(const std::string& jsonText) = 0;
    virtual void WriteLine(const std::string& jsonText) = 0;
    virtual void Flush() = 0;
    virtual void ReserveAdditional(size_t size) = 0;
}; 

class CStringWriter : public CTextWriter
{
public:

    ~CStringWriter() override {};

    void Write(char val) override
    {
        strBuffer.push_back(val);
    }

    void Write(const std::string& jsonText) override
    {
        strBuffer.append(jsonText);
    }

    void WriteLine(const std::string& jsonText = "") override
    {
        strBuffer.append(jsonText);
        strBuffer.push_back('\n');
    }

    void Flush() override {}


    // We implement our own buffering
    void ReserveAdditional(size_t size) override
    {
        if (size > 0)
        {
            strBuffer.reserve(strBuffer.size() + size);
        }
    }

    std::string MoveOutString()
    {
        return std::move(strBuffer); // resulting state of strBuffer is undefined
    }

private:
    std::string strBuffer;
};

class CHttpTextWriter : public CTextWriter
{
public:

    CHttpTextWriter(HTTPRequest& request) : _request(request)
    {
        strBuffer.reserve(BUFFER_SIZE);
    }

    ~CHttpTextWriter() override
    {
        Flush();
    }

    void Write(char val) override
    {
        WriteToBuff(val);
    }

    void Write(const std::string& jsonText) override
    {
        WriteToBuff(jsonText);
    }

    void WriteLine(const std::string& jsonText = "") override
    {
        WriteToBuff(jsonText);
        WriteToBuff('\n');
    }

    void Flush() override
    {
        if (!strBuffer.empty())
        {
            _request.WriteReplyChunk(strBuffer);
            strBuffer.clear();
        }
    }

    void ReserveAdditional(size_t size) override {}

private:
    static constexpr size_t BUFFER_SIZE = ONE_MEGABYTE;
    HTTPRequest& _request;
    std::string strBuffer;

    void WriteToBuff(std::string jsonText)
    {
        if (jsonText.size() > BUFFER_SIZE)
        {
            Flush();
            _request.WriteReplyChunk(jsonText);
            return;
        }

        strBuffer.append(jsonText);
        if (strBuffer.size() > BUFFER_SIZE)
        {
            Flush();
        }
    }

    void WriteToBuff(char jsonText)
    {
        strBuffer.push_back(jsonText);
        if (strBuffer.size() > BUFFER_SIZE)
        {
            Flush();
        }
    }

};