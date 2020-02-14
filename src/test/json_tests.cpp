// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "test/test_bitcoin.h"
#include "rpc/jsonwriter.h"
#include "core_io.h"
#include <boost/test/unit_test.hpp>
#include <univalue.h>

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

BOOST_AUTO_TEST_CASE(json_decode_tx_from_mainnet)
{
    // Hex represents transaction from mainnet https://blockchair.com/bitcoin-sv/transaction/fd999735b7a3017292d97791d56cb57730efda217d1a75842b4481a7d8ea1b46
    // that caused to produce invalid JSON because it contained 2 outputs with same address and amount
    std::string hexTx = "01000000045add7f1454066a7fe6b561cd0778bf39e2ca4e106ebecf559aff60b3b31de05d020000006a47304"\
                        "40220709a45cfb1552dfdfaa4c212f50f2cb3c15dec8c46cedbeb4810533e8979bcdb022049a91176ef08fa78"\
                        "3cf11b0728804fa3ec7a2606df0195b4e850be2da266468b412102b60a08b18231a19d22c4018c751d6cfa453"\
                        "73b888087445a55061ce8a5e53ff2ffffffff503c98354d6eacc5722adc042a9af6bfb81e01b8a7ca704cc82a"\
                        "b5a290313d91020000006a473044022058870af7632c1cefe45494660e621b0af87d153545edf3b60009b60da"\
                        "af9ee6c022079d02f0f213305b33166c7c23e632d3c83481ead407b728b4fc2e474096f80324121026272de0d"\
                        "1de6840b69cb1f0dba708fe0cfb4b75b408bd46ac334edb0c992a3e9ffffffff503c98354d6eacc5722adc042"\
                        "a9af6bfb81e01b8a7ca704cc82ab5a290313d91030000006a47304402207c85bcb754e8e16aa57697e84bef4d"\
                        "a575d99c4344fcb4797d9b7da3924f4299022030b890b9c1fd061fec80619f22f47e254b3427d2d9079c97026"\
                        "be1e7c35f67064121026272de0d1de6840b69cb1f0dba708fe0cfb4b75b408bd46ac334edb0c992a3e9ffffff"\
                        "ff503c98354d6eacc5722adc042a9af6bfb81e01b8a7ca704cc82ab5a290313d91040000006b4830450221009"\
                        "24c8d3beab5a4005fb753036695c83023f847faadf7291a80dcf6e706dc0f5e022024e1dfd870c26e309b2d24"\
                        "42f51217ae06b607401e4c92ef4e2e8c81e77488e7412103ae9720a3926ecaf08f0d4201a930ba78dac25c660"\
                        "26212db32c6ed36eefd9994ffffffff0532070000000000001976a9141f817671ee7f3fbda7ca1e8d0102bc2c"\
                        "3737e73788ac280d0000000000001976a91405186ff0710ed004229e644c0653b2985c648a2388ac8b1100000"\
                        "00000001976a914e25d089570c622510be1187b182a4956ce25886b88ac8b110000000000001976a914f9cdf6"\
                        "7175b603faec97d53ecfe8011e179809a988ac8b110000000000001976a914f9cdf67175b603faec97d53ecfe"\
                        "8011e179809a988ac00000000";
    CMutableTransaction mtx;
    DecodeHexTx(mtx, hexTx);
    CTransaction tx(mtx);

    CJSONWriter jWriter(strWriter, true, 2);
    TxToJSON(tx, uint256(), false, 0, jWriter);
    
    UniValue uv(UniValue::VOBJ);
    // Read the JSON string into UniValue object to check if JSON is well formed. 
    // Method read returns false if it was unable to read JSON string
    BOOST_CHECK(uv.read(strWriter.MoveOutString()));
}

BOOST_AUTO_TEST_CASE(json_decode_tx_with_2_same_inputs_outputs_addresses)
{
    CKey key;
    key.MakeNewKey(true);
    // Create multisig operation with 2 same addresses
    CScript multisig2;
    multisig2 << OP_1 << ToByteVector(key.GetPubKey())
        << ToByteVector(key.GetPubKey()) << OP_2 << OP_CHECKMULTISIG;

    CMutableTransaction txFrom;
    txFrom.vout.resize(1);
    txFrom.vout[0].scriptPubKey = CScript(OP_RETURN);

    // Create transaction with 2 equal inputs, 2 equal outputs and 2 equal addresses to test JSON output
    CMutableTransaction mtx;
    mtx.vout.resize(2);
    mtx.vout[0].scriptPubKey = multisig2;
    mtx.vout[1] = mtx.vout[0]; // Create copy of the first output 

    mtx.vin.resize(2);
    mtx.vin[0].prevout = COutPoint(txFrom.GetId(), 0);
    mtx.vin[1] = mtx.vin[0]; // Create copy of the first input 

    CTransaction tx(mtx);

    CJSONWriter jWriter(strWriter, true, 2);
    TxToJSON(tx, uint256(), false, 0, jWriter);

    UniValue uv(UniValue::VOBJ);
    // Read the JSON string into UniValue object to check if JSON is well formed. 
    // Method read returns false if it was unable to read JSON string
    BOOST_CHECK(uv.read(strWriter.MoveOutString()));
}

BOOST_AUTO_TEST_SUITE_END()
