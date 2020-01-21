// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.


#include "jsonwriter.h"
#include "univalue_escapes.h"
#include <univalue.h>

// This method is a copy of a method with the same name in univalue_write.cpp
static std::string json_escape(const std::string& inS)
{
    std::string outS;
    outS.reserve(inS.size() * 2);

    for (size_t i = 0; i < inS.size(); i++) 
    {
        unsigned char ch = inS[i];
        const char* escStr = escapes[ch];

        if (escStr)
        {
            outS += escStr;
        }
        else
        {
            outS += ch;
        }
    }

    return outS;
}

void CJSONWriter::writeBeginArray(const std::string& objectName)
{
    createTag("[", true, objectName);
}

void CJSONWriter::writeEndArray(bool addEndingComma)
{
    std::string endTag = "]";
    if (addEndingComma)
    {
        endTag += ",";
    }
    createTag(endTag, false);
}

void CJSONWriter::writeBeginObject(const std::string& objectName)
{
    createTag("{", true, objectName);
}

void CJSONWriter::writeEndObject(bool addEndingComma)
{
    std::string endTag = "}";
    if (addEndingComma)
    {
        endTag += ",";
    }
    createTag(endTag, false);
}

CTextWriter& CJSONWriter::getWriter()
{
    return jWriter;
}

std::string CJSONWriter::getIntStr(int64_t val)
{
    return std::to_string(val);;
}

void CJSONWriter::pushK(const std::string& key)
{
    jWriter.Write(indentStr());
    jWriter.Write('"');
    jWriter.Write(json_escape(key));
    writeString("\": ", false, true);
}

void CJSONWriter::pushV(const std::string& val, bool addEndingComma)
{
    jWriter.Write(indentStr());
    jWriter.Write('"');
    jWriter.Write(json_escape(val));
    jWriter.Write('"');

    if (addEndingComma)
    {
        jWriter.Write(',');
    }
    jWriter.WriteLine("");
}

void CJSONWriter::pushKVMoney(const std::string& key, const std::string& val, bool addEndingComma)
{
    jWriter.Write(indentStr());
    jWriter.Write('"');
    jWriter.Write(json_escape(key));
    jWriter.Write("\": ");
    writeString(val, addEndingComma);
}

void CJSONWriter::pushKV(const std::string& key, const std::string& val, bool addEndingComma)
{
    jWriter.Write(indentStr());
    jWriter.Write('"');
    jWriter.Write(json_escape(key));
    jWriter.Write("\": \"");
    jWriter.Write(json_escape(val));
    writeString("\"", addEndingComma);
}

void CJSONWriter::pushKV(const std::string& key, const char* val, bool addEndingComma)
{
    jWriter.Write(indentStr());
    jWriter.Write('"');
    jWriter.Write(json_escape(key));
    jWriter.Write("\": \"");
    jWriter.Write(json_escape(val));
    writeString("\"", addEndingComma);
}

void CJSONWriter::pushKV(const std::string& key, int64_t val, bool addEndingComma)
{
    jWriter.Write(indentStr());
    jWriter.Write('"');
    jWriter.Write(json_escape(key));
    jWriter.Write("\": ");

    writeString(getIntStr(val), addEndingComma);
}

void CJSONWriter::pushKV(const std::string& key, int val, bool addEndingComma)
{
    jWriter.Write(indentStr());
    jWriter.Write('"');
    jWriter.Write(json_escape(key));
    jWriter.Write("\": ");

    writeString(getIntStr((int64_t)val), addEndingComma);
}

void CJSONWriter::pushKV(const std::string& key, bool val, bool addEndingComma)
{
    jWriter.Write(indentStr());
    jWriter.Write('"');
    jWriter.Write(json_escape(key));
    jWriter.Write("\": ");

    std::string boolStr = val ? "true" : "false";
    writeString(boolStr, addEndingComma);
}

void CJSONWriter::pushQuote(bool ignorePrettyIndent, bool addEndingComma)
{
    writeString("\"", addEndingComma, ignorePrettyIndent);
}

void CJSONWriter::flush()
{
    jWriter.Flush();
}

std::string CJSONWriter::indentStr()
{
    if (!_prettyIndent)
    {
        return "";
    }
    else
    {
        size_t indentation = _indentSize * _indentLevel;
        return std::string(indentation, ' ');
    }
}

void CJSONWriter::createTag(const std::string& tag, bool incrementLevel, const std::string& objectName)
{
    if (!incrementLevel)
    {
        _indentLevel--;
    }

    if (!objectName.empty())
    {
        jWriter.Write(indentStr());
        jWriter.Write('"');
        jWriter.Write(json_escape(objectName));
        jWriter.Write("\": ");
        jWriter.Write(tag);
    }
    else
    {
        jWriter.Write(indentStr());
        jWriter.Write(tag);
    }

    if (_prettyIndent)
    {
        //this condition is here only to prevent adding additional new line after last curly bracket
        if (incrementLevel || _indentLevel != 0)
        {
            jWriter.Write('\n');
        }
    }

    if (incrementLevel)
    {
        _indentLevel++;
    }
}

void CJSONWriter::writeString(const std::string& jsonText, bool addEndingComma, bool ignorePrettyIndent)
{
    jWriter.Write(jsonText);
    if (addEndingComma)
    {
        jWriter.Write(',');
    }
    if (!ignorePrettyIndent && _prettyIndent)
    {
        jWriter.Write('\n');
    }
}

