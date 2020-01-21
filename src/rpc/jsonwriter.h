// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include <string>
#include "text_writer.h"

class CJSONWriter
{
public:

    CJSONWriter(CTextWriter& jsonWriter, bool prettyIndent, int indentSize = 4)
        : jWriter(jsonWriter), _prettyIndent(prettyIndent), _indentSize(indentSize) {}

    ~CJSONWriter() 
    {
        flush();
    }

    void writeBeginArray(const std::string& objectName = "");
    void writeEndArray(bool addEndingComma = true);
    void writeBeginObject(const std::string& objectName = "");
    void writeEndObject(bool addEndingComma = true);
    CTextWriter& getWriter();

    void pushK(const std::string& key);
    // Used for array elements
    void pushV(const std::string& val_, bool addEndingComma = true);
    void pushKVMoney(const std::string& key, const std::string& val_, bool addEndingComma = true);
    void pushKV(const std::string& key, const std::string& val_, bool addEndingComma = true);
    void pushKV(const std::string& key, const char* val_, bool addEndingComma = true);
    void pushKV(const std::string& key, int64_t val_, bool addEndingComma = true);
    void pushKV(const std::string& key, int val_, bool addEndingComma = true);
    void pushKV(const std::string& key, bool val_, bool addEndingComma = true);
    void pushQuote(bool ignorePrettyIndent, bool addEndingComma = true);
    void flush();

    std::string getIntStr(int64_t val_);

private:

    std::string indentStr();
    void createTag(const std::string& tag, bool incrementLevel, const std::string& objectName = "");
    void writeString(const std::string& jsonText, bool addEndingComma, bool ignorePrettyIndent = false);

    CTextWriter& jWriter;
    bool _prettyIndent;
    int _indentSize;
    int _indentLevel = 0;
};