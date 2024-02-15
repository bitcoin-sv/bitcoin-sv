// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include <string>
#include <iomanip>
#include <sstream>
#include <vector>
#include <string_view>
#include "text_writer.h"
#include "univalue_escapes.h"
#include "amount.h"

// Class for building JSON document which streams the JSON content to the stream by writing chunks when the buffer 
// of the class implementing CTextWriter is full. Building JSON document with CJSONWriter should be done when large 
// documents are meant to be built to prevent excessive memory usage.
//
// Example of usage:
// CHttpTextWriter httpWriter(HTTPRequest);
// CJSONWriter jWriter(httpWriter, false);
// jWriter.writeBeginObject();
// jWriter.writeBeginArray("tx");
// ... add key/value items
// jWriter.writeEndArray();
// jWriter.writeEndObject();

// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions)
class CJSONWriter
{
public:
    // Class that takes care of JSON string representation of different types before being written to JSON output
    // If a desired type overload is missing, new constructor needs to be added
    class ScalarValue
    {
    public:
        ScalarValue(std::string_view val, bool ignoreQuote = false)
        {
            if (!ignoreQuote)
            {
                _jsonValue = '"';
            }
            _jsonValue += json_escape(val);
            if (!ignoreQuote)
            {
                _jsonValue += '"';
            }
        }

        ScalarValue(double val)
        {
            std::ostringstream oss;
            oss << std::setprecision(16) << val;
            _jsonValue = oss.str();
        }

        ScalarValue(const Amount& val);

        ScalarValue(const std::string& val) : ScalarValue(std::string_view{ val }) {}
        ScalarValue(const char* val) : ScalarValue(std::string_view{ val }) {}
        ScalarValue(uint64_t val) : _jsonValue(std::to_string(val)) {}
        ScalarValue(int64_t val) : _jsonValue(std::to_string(val)) {}
        ScalarValue(int val) : _jsonValue(std::to_string(val)) {}
        ScalarValue(bool val) : _jsonValue(val ? "true" : "false") {}
        ScalarValue(std::nullptr_t) : _jsonValue("null") {}

        const std::string& str() const { return this->_jsonValue; }
    private:
        std::string _jsonValue;
    };

    CJSONWriter(CTextWriter& jsonWriter, bool prettyIndent, int indentSize = 4)
        : jWriter(jsonWriter), _prettyIndent(prettyIndent), _indentSize(indentSize) {}

    ~CJSONWriter() 
    {
        flush();
    }

    void writeBeginArray(const std::string& objectName = "");
    void writeEndArray();
    void writeBeginObject(const std::string& objectName = "");
    void writeEndObject();
    CTextWriter& getWriter();

    void pushK(const std::string& key);
    void pushKNoComma(const std::string& key);
    void pushV(const ScalarValue& val);
    void pushV(const std::vector<std::string>& val);
    // Outputs the key and then string value is written without quotes as is.
    // Argument val must contain a valid JSON formatted value.
    void pushKVJSONFormatted(const std::string& key, const std::string& val);
    // Outputs the string value without quotes as is.
    // Argument val must contain a string that results in valid JSON when inserted at given point in stream.
    void pushVJSONFormatted(const std::string& val);
    void pushKV(const std::string& key, const ScalarValue& val);
    // Outputs double quote character.
    // This can be used to output string that contains no special characters or if they are already properly escaped.    
    // Example: pushQuote(); pushVJSONFormatted("abc"); pushQuote();
    void pushQuote();
    void flush();

private:

    // This method is a copy of a method with the same name in univalue_write.cpp
    static std::string json_escape(const std::string_view inS)
    {
        std::string outS;
        outS.reserve(inS.size() * 2);

        for (size_t i = 0; i < inS.size(); i++)
        {
            unsigned char ch = inS[i];
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
            const char* escStr = escapes[ch];

            if (escStr)
            {
                outS += escStr;
            }
            else
            {
                outS += ch; // NOLINT(*-narrowing-conversions)
            }
        }

        return outS;
    }

    void indentStr();
    void createTag(const std::string& tag, bool incrementLevel, const std::string& objectName = "");
    
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
    CTextWriter& jWriter;
    bool _prettyIndent;
    int _indentSize;
    int _indentLevel = 0;
    bool _doNotAddComma = true;
    bool _firstWrite = true;
};
