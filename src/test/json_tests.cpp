// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "test/test_bitcoin.h"
#include "rpc/jsonwriter.h"
#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(json_tests, BasicTestingSetup)

CStringWriter strWriter;
CJSONWriter jsonWriter(strWriter, false);

BOOST_AUTO_TEST_CASE(CJWriter_write_array) 
{
    jsonWriter.writeBeginArray("Array");
    jsonWriter.writeEndArray();

    BOOST_CHECK_EQUAL(strWriter.MoveOutString(), "\"Array\": [],");

    jsonWriter.writeBeginArray("Array");
    jsonWriter.writeEndArray(false);

    BOOST_CHECK_EQUAL(strWriter.MoveOutString(), "\"Array\": []");
}

BOOST_AUTO_TEST_CASE(CJWriter_write_object) 
{
    jsonWriter.writeBeginObject("Object");
    jsonWriter.writeEndObject();

    BOOST_CHECK_EQUAL(strWriter.MoveOutString(), "\"Object\": {},");

    jsonWriter.writeBeginObject("Object");
    jsonWriter.writeEndObject(false);

    BOOST_CHECK_EQUAL(strWriter.MoveOutString(), "\"Object\": {}");
}

BOOST_AUTO_TEST_CASE(CJWriter_pushK_pushV) 
{
    jsonWriter.pushK("key");
    jsonWriter.pushV("val", true);

    BOOST_CHECK_EQUAL(strWriter.MoveOutString(), "\"key\": \"val\",\n");

    jsonWriter.pushK("key");
    jsonWriter.pushV("val", false);

    BOOST_CHECK_EQUAL(strWriter.MoveOutString(), "\"key\": \"val\"\n");
}

BOOST_AUTO_TEST_CASE(CJWriter_pushKVMoney) 
{
    jsonWriter.pushKVMoney("key", "0");

    BOOST_CHECK_EQUAL(strWriter.MoveOutString(), "\"key\": 0,");

    jsonWriter.pushKVMoney("key", "0", false);

    BOOST_CHECK_EQUAL(strWriter.MoveOutString(), "\"key\": 0");
}

BOOST_AUTO_TEST_CASE(CJWriter_pushKVString) 
{
    jsonWriter.pushKV("key", "val");

    BOOST_CHECK_EQUAL(strWriter.MoveOutString(), "\"key\": \"val\",");

    jsonWriter.pushKV("key", "val", false);

    BOOST_CHECK_EQUAL(strWriter.MoveOutString(), "\"key\": \"val\"");
}

BOOST_AUTO_TEST_CASE(CJWriter_pushKVChar) 
{
    const char* c = "v";
    jsonWriter.pushKV("key", c);

    BOOST_CHECK_EQUAL(strWriter.MoveOutString(), "\"key\": \"v\",");

    jsonWriter.pushKV("key", c, false);

    BOOST_CHECK_EQUAL(strWriter.MoveOutString(), "\"key\": \"v\"");
}

BOOST_AUTO_TEST_CASE(CJWriter_pushKVInt64) 
{
    jsonWriter.pushKV("key", int64_t(100));

    BOOST_CHECK_EQUAL(strWriter.MoveOutString(), "\"key\": 100,");

    jsonWriter.pushKV("key", int64_t(100), false);

    BOOST_CHECK_EQUAL(strWriter.MoveOutString(), "\"key\": 100");
}

BOOST_AUTO_TEST_CASE(CJWriter_pushKVInt) 
{
    jsonWriter.pushKV("key", 100);

    BOOST_CHECK_EQUAL(strWriter.MoveOutString(), "\"key\": 100,");

    jsonWriter.pushKV("key", 100, false);

    BOOST_CHECK_EQUAL(strWriter.MoveOutString(), "\"key\": 100");
}

BOOST_AUTO_TEST_CASE(CJWriter_pushKVBool) 
{
    jsonWriter.pushKV("key", true);

    BOOST_CHECK_EQUAL(strWriter.MoveOutString(), "\"key\": true,");

    jsonWriter.pushKV("key", true, false);

    BOOST_CHECK_EQUAL(strWriter.MoveOutString(), "\"key\": true");
}

BOOST_AUTO_TEST_CASE(CJWriter_pushQuote) 
{
    jsonWriter.pushQuote(true);

    BOOST_CHECK_EQUAL(strWriter.MoveOutString(), "\",");

    jsonWriter.pushQuote(true, false);

    BOOST_CHECK_EQUAL(strWriter.MoveOutString(), "\"");
}

BOOST_AUTO_TEST_CASE(CJWriter_write_JSONText) 
{
    const char* c = "v";

    jsonWriter.writeBeginObject();
    jsonWriter.pushKV("int", 1);
    jsonWriter.pushKV("bool", true);
    jsonWriter.pushKV("string", "val");
    jsonWriter.pushKVMoney("money", "1");
    jsonWriter.pushKV("int64", int64_t(64));
    jsonWriter.pushKV("char", c);
    jsonWriter.pushK("quotes");
    jsonWriter.pushQuote(true, false);
    jsonWriter.getWriter().Write("test_quotes");
    jsonWriter.pushQuote(true);
    jsonWriter.writeBeginArray("array");
    jsonWriter.pushV("arr1", true);
    jsonWriter.pushV("arr2", true);
    jsonWriter.pushV("arr3", false);
    jsonWriter.writeEndArray(false);
    jsonWriter.writeEndObject(false);

    BOOST_CHECK_EQUAL(strWriter.MoveOutString(), "{\"int\": 1,\"bool\": true,\"string\": \"val\",\"money\": 1,\"int64\": 64,\"char\": \"v\",\"quotes\": \"test_quotes\",\"array\": [\"arr1\",\n\"arr2\",\n\"arr3\"\n]}");
}

BOOST_AUTO_TEST_CASE(CJWriter_write_JSONText_with_indents)
{
    const char* c = "v";
    CJSONWriter indentJsonWriter(strWriter, true, 2);

    indentJsonWriter.writeBeginObject();
    indentJsonWriter.pushKV("int", 1);
    indentJsonWriter.pushKV("bool", true);
    indentJsonWriter.pushKV("string", "val");
    indentJsonWriter.pushKVMoney("money", "1");
    indentJsonWriter.pushKV("int64", int64_t(64));
    indentJsonWriter.pushKV("char", c);
    indentJsonWriter.pushK("quotes");
    indentJsonWriter.pushQuote(true, false);
    indentJsonWriter.getWriter().Write("test_quotes");
    indentJsonWriter.pushQuote(false);
    indentJsonWriter.writeBeginArray("array");
    indentJsonWriter.pushV("arr1", true);
    indentJsonWriter.pushV("arr2", true);
    indentJsonWriter.pushV("arr3", false);
    indentJsonWriter.writeEndArray(false);
    indentJsonWriter.writeEndObject(false);

    BOOST_CHECK_EQUAL(strWriter.MoveOutString(), "{\n  \"int\": 1,\n  \"bool\": true,\n  \"string\": \"val\",\n  \"money\": 1,\n  \"int64\": 64,\n  \"char\": \"v\",\n  \"quotes\": \"test_quotes\",\n  \"array\": [\n    \"arr1\",\n    \"arr2\",\n    \"arr3\"\n  ]\n}");
}

BOOST_AUTO_TEST_SUITE_END()
