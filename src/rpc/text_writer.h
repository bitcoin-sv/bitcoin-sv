// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include "consensus/consensus.h" // For ONE_MEGABYTE
#include "httpserver.h" // For HTTPRequest
#include <string>
#include <fstream>

// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions)
class CTextWriter
{
public:
    virtual ~CTextWriter() = default;
    virtual void Write(char val) = 0;
    virtual void Write(const std::string& jsonText) = 0;
    virtual void Flush() = 0;
    virtual void ReserveAdditional(size_t size){}

    void WriteLine(const std::string& jsonText) {
        Write(jsonText);
        Write('\n');
    }
    void WriteLine() {
        Write('\n');
    }
};

class CStringWriter : public CTextWriter
{
public:

    void Write(char val) override
    {
        strBuffer.push_back(val);
    }

    void Write(const std::string& jsonText) override
    {
        strBuffer.append(jsonText);
    }

    void Flush() override {}


    // We implement our own buffering
    void ReserveAdditional(size_t size) override;

    std::string MoveOutString()
    {
        return std::move(strBuffer); // resulting state of strBuffer is undefined
    }

private:
    std::string strBuffer;
};

// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions)
class CHttpTextWriter : public CTextWriter
{
public:

    explicit CHttpTextWriter(HTTPRequest& request) : _request(request)
    {
        strBuffer.reserve(BUFFER_SIZE);
    }

    ~CHttpTextWriter() override
    {
        FlushNonVirtual();
    }

    void Write(char val) override
    {
        WriteToBuff(val);
    }

    void Write(const std::string& jsonText) override
    {
        WriteToBuff(jsonText);
    }

    void Flush() override
    {
        FlushNonVirtual();
    }

private:
    static constexpr size_t BUFFER_SIZE = ONE_MEGABYTE;
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
    HTTPRequest& _request;
    std::string strBuffer;

    void WriteToBuff(std::string const & jsonText)
    {
        if (jsonText.size() > BUFFER_SIZE)
        {
            FlushNonVirtual();
            _request.WriteReplyChunk(jsonText);
            return;
        }

        strBuffer.append(jsonText);
        if (strBuffer.size() > BUFFER_SIZE)
        {
            FlushNonVirtual();
        }
    }

    void WriteToBuff(char jsonText)
    {
        strBuffer.push_back(jsonText);
        if (strBuffer.size() > BUFFER_SIZE)
        {
            FlushNonVirtual();
        }
    }
    void FlushNonVirtual()
    {
        if (!strBuffer.empty())
        {
            _request.WriteReplyChunk(strBuffer);
            strBuffer.clear();
        }
    }
};

// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions)
class CFileTextWriter : public CTextWriter
{
public:
    explicit CFileTextWriter(const std::string& path)
    {
        file.open(path, std::ios::out | std::ios::trunc);
        CheckForError();
    }

    ~CFileTextWriter () override
    {
        FlushNonVirtual();
    }

    void Write(char val) override
    {
        if (error.empty())
        {
            file << val;
            CheckForError();
        }
    }

    void Write(const std::string& jsonText) override
    {
        if (error.empty())
        {
            file << jsonText;
            CheckForError();
        }
    }

    void Flush() override
    {
        FlushNonVirtual();
    }

    // returns empty string if no errors occurred
    std::string GetError()
    {
        return error;
    }

private:
    void CheckForError()
    {
        if(file.fail())
        {
            error =  "Failed to write to file: " + std::generic_category().message(errno);
        }
    }
    void FlushNonVirtual()
    {
        if (error.empty())
        {
            file.flush();
            CheckForError();
        }
    }

    std::ofstream file;
    std::string error;
};
