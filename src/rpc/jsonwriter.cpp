// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "jsonwriter.h"
#include "tinyformat.h"

CJSONWriter::ScalarValue::ScalarValue(const Amount& amount)
{
    int64_t amt = amount.GetSatoshis();
    int64_t n_abs = std::abs(amt);
    int64_t quotient = n_abs / COIN.GetSatoshis();
    int64_t remainder = n_abs % COIN.GetSatoshis();

    _jsonValue = strprintf("%s%d.%08d", (amt < 0) ? "-" : "", quotient, remainder);
}

void CJSONWriter::writeBeginArray(const std::string& objectName)
{
    createTag("[", true, objectName);
    _doNotAddComma = true;
}

void CJSONWriter::writeEndArray()
{
    _doNotAddComma = true;
    createTag("]", false);
}

void CJSONWriter::writeBeginObject(const std::string& objectName)
{
    createTag("{", true, objectName);
    _doNotAddComma = true;
}

void CJSONWriter::writeEndObject()
{
    _doNotAddComma = true;
    createTag("}", false);
}

CTextWriter& CJSONWriter::getWriter()
{
    return jWriter;
}

void CJSONWriter::pushV(const ScalarValue& val)
{
    indentStr();
    jWriter.Write(val.str());
}

void CJSONWriter::pushV(const std::vector<std::string>& val)
{
    for (const std::string& element : val)
    {
        pushV(element);
    }
}

void CJSONWriter::pushK(const std::string& key)
{
    indentStr();
    jWriter.Write('\"');
    jWriter.Write(json_escape(key));
    jWriter.Write("\": ");
}

void CJSONWriter::pushKNoComma(const std::string& key)
{
    pushK(key);
    _doNotAddComma = true;
}

void CJSONWriter::pushKVJSONFormatted(const std::string& key, const std::string& val)
{
    pushK(key);
    jWriter.Write(val);
}

void CJSONWriter::pushVJSONFormatted(const std::string& val)
{
    indentStr();
    jWriter.Write(val);
}

void CJSONWriter::pushKV(const std::string& key, const ScalarValue& val)
{
    pushK(key);
    jWriter.Write(val.str());
}

void CJSONWriter::pushQuote()
{
    jWriter.Write('\"');
}

void CJSONWriter::flush()
{
    jWriter.Flush();
}

void CJSONWriter::indentStr()
{
    if (!_doNotAddComma)
    {
        jWriter.Write(',');
    }
    else
    {
        _doNotAddComma = false;
    }

    if (_prettyIndent)
    {
        if (!_firstWrite)
        {
            jWriter.Write('\n');
        }

        size_t indentation = _indentSize * _indentLevel;
        jWriter.Write(std::string(indentation, ' '));
        _firstWrite = false;
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
        pushK(objectName);
    }
    else
    {
        indentStr();
    }

    jWriter.Write(tag);

    if (incrementLevel)
    {
        _indentLevel++;
    }
}