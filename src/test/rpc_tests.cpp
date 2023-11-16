// Copyright (c) 2012-2016 The Bitcoin Core developers
// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "rpc/client.h"
#include "rpc/client_config.h"
#include "rpc/client_utils.h"
#include "rpc/http_request.h"
#include "rpc/http_response.h"
#include "rpc/server.h"

#include "base58.h"
#include "config.h"
#include "double_spend/dscallback_msg.h"
#include "net/netbase.h"
#include "policy/policy.h"
#include "util.h"

#include "test/test_bitcoin.h"
#include "rpc/tojson.h"

#include <boost/algorithm/string.hpp>
#include <boost/test/unit_test.hpp>

#include <univalue.h>

UniValue CallRPC(std::string args) {
    std::vector<std::string> vArgs;
    boost::split(vArgs, args, boost::is_any_of(" \t"));
    std::string strMethod = vArgs[0];
    vArgs.erase(vArgs.begin());
    GlobalConfig config;
    config.SetDefaultBlockSizeParams(Params().GetDefaultBlockSizeParams());
    config.SetGenesisActivationHeight(1);
    JSONRPCRequest request;
    request.strMethod = strMethod;
    request.params = RPCConvertValues(strMethod, vArgs);
    request.fHelp = false;
    if (strMethod == "getrawtransaction") {
        try {
            CStringWriter stringWriter;
            getrawtransaction(config, request, stringWriter, false, []{});
            stringWriter.Flush();
            UniValue result(UniValue::VOBJ);
            result.read(stringWriter.MoveOutString());
            return result;
        }
        catch (const UniValue& objError) {
            throw std::runtime_error(find_value(objError, "message").get_str());
        }
    }
    else if (strMethod == "decoderawtransaction") {
        try {
            CStringWriter stringWriter;
            decoderawtransaction(config, request, stringWriter, false, []{});
            stringWriter.Flush();
            UniValue result(UniValue::VOBJ);
            result.read(stringWriter.MoveOutString());
            return result;
        }
        catch (const UniValue& objError) {
            throw std::runtime_error(find_value(objError, "message").get_str());
        }
    }
    else
    {
        BOOST_CHECK(tableRPC[strMethod]);
        try {
            UniValue result = tableRPC[strMethod]->call(config, request);
            return result;
        }
        catch (const UniValue & objError) {
            throw std::runtime_error(find_value(objError, "message").get_str());
        }
    }
}

// Because some RPC methods now push JSON text in chunks, the JSON response changed.
// In this cases we first have to search for the "result" element in JSON response 
// to get the old JSON structure 
const UniValue& find_value_in_result(const UniValue& obj, const std::string& name)
{
    auto& response = find_value(obj, "result");
    if (response.isNull())
    {
        return find_value(obj, name);
    }
    return find_value(response, name);
}

bool FlagsNumericMessage(const std::runtime_error& ex)
{
    BOOST_CHECK_EQUAL(ex.what(), std::string("\"flags\" must be a numeric value"));
    return true;
}
bool FlagsValueMessage(const std::runtime_error& ex)
{
    BOOST_CHECK_EQUAL(ex.what(), std::string("verifymerkleproof only supports \"flags\" with value 2"));
    return true;
}
bool IndexNumericMessage(const std::runtime_error& ex)
{
    BOOST_CHECK_EQUAL(ex.what(), std::string("\"index\" must be a numeric value"));
    return true;
}
bool IndexValueMessage(const std::runtime_error& ex)
{
    BOOST_CHECK_EQUAL(ex.what(), std::string("\"index\" must be a positive value"));
    return true;
}
bool TxOrIdHashValueMessage(const std::runtime_error& ex)
{
    BOOST_CHECK_EQUAL(ex.what(), std::string("txOrId must be hexadecimal string (not '') and length of it must be divisible by 2"));
    return true;
}
bool TxOrIdHashValueMessage2(const std::runtime_error& ex)
{
    BOOST_CHECK_EQUAL(ex.what(), std::string("txOrId must be hexadecimal string (not 'wrong_hash') and length of it must be divisible by 2"));
    return true;
}
bool TargetObjectMessage(const std::runtime_error& ex)
{
    BOOST_CHECK_EQUAL(ex.what(), std::string("\"target\" must be a block header Json object"));
    return true;
}
bool MerkleRootHashMessage(const std::runtime_error& ex)
{
    BOOST_CHECK_EQUAL(ex.what(), std::string("merkleroot must be hexadecimal string (not '') and length of it must be divisible by 2"));
    return true;
}
bool NodesArrayMessage(const std::runtime_error& ex)
{
    BOOST_CHECK_EQUAL(ex.what(), std::string("\"nodes\" must be a Json array"));
    return true;
}
bool NodeHashValueMessage(const std::runtime_error& ex)
{
    BOOST_CHECK_EQUAL(ex.what(), std::string("node must be hexadecimal string (not '**') and length of it must be divisible by 2"));
    return true;
}

BOOST_FIXTURE_TEST_SUITE(rpc_tests, TestingSetup)

BOOST_AUTO_TEST_CASE(rpc_getinfo)
{
    // Test block size fields from getinfo
    UniValue r {};
    BOOST_CHECK_NO_THROW(
        r = CallRPC("getinfo");
    );
    BOOST_CHECK_EQUAL(find_value(r.get_obj(), "maxblocksize").get_int64(), static_cast<int64_t>(Params().GetDefaultBlockSizeParams().maxBlockSize));
    BOOST_CHECK_EQUAL(find_value(r.get_obj(), "maxminedblocksize").get_int64(), static_cast<int64_t>(Params().GetDefaultBlockSizeParams().maxGeneratedBlockSizeAfter));
}

BOOST_AUTO_TEST_CASE(rpc_rawparams) {
    // Test raw transaction API argument handling
    UniValue r;

    BOOST_CHECK_THROW(CallRPC("getrawtransaction"), std::runtime_error);
    BOOST_CHECK_THROW(CallRPC("getrawtransaction not_hex"), std::runtime_error);
    BOOST_CHECK_THROW(CallRPC("getrawtransaction "
                              "a3b807410df0b60fcb9736768df5823938b2f838694939ba"
                              "45f3c0a1bff150ed not_int"),
                      std::runtime_error);

    BOOST_CHECK_THROW(CallRPC("createrawtransaction"), std::runtime_error);
    BOOST_CHECK_THROW(CallRPC("createrawtransaction null null"),
                      std::runtime_error);
    BOOST_CHECK_THROW(CallRPC("createrawtransaction not_array"),
                      std::runtime_error);
    BOOST_CHECK_THROW(CallRPC("createrawtransaction [] []"),
                      std::runtime_error);
    BOOST_CHECK_THROW(CallRPC("createrawtransaction {} {}"),
                      std::runtime_error);
    BOOST_CHECK_NO_THROW(CallRPC("createrawtransaction [] {}"));
    BOOST_CHECK_THROW(CallRPC("createrawtransaction [] {} extra"),
                      std::runtime_error);

    BOOST_CHECK_THROW(CallRPC("decoderawtransaction"), std::runtime_error);
    BOOST_CHECK_THROW(CallRPC("decoderawtransaction null"), std::runtime_error);
    BOOST_CHECK_THROW(CallRPC("decoderawtransaction DEADBEEF"),
                      std::runtime_error);
    std::string rawtx =
        "0100000001a15d57094aa7a21a28cb20b59aab8fc7d1149a3bdbcddba9c622e4f5f6a9"
        "9ece010000006c493046022100f93bb0e7d8db7bd46e40132d1f8242026e045f03a0ef"
        "e71bbb8e3f475e970d790221009337cd7f1f929f00cc6ff01f03729b069a7c21b59b17"
        "36ddfee5db5946c5da8c0121033b9b137ee87d5a812d6f506efdd37f0affa7ffc31071"
        "1c06c7f3e097c9447c52ffffffff0100e1f505000000001976a9140389035a9225b383"
        "9e2bbf32d826a1e222031fd888ac00000000";
    BOOST_CHECK_NO_THROW(
        r = CallRPC(std::string("decoderawtransaction ") + rawtx));
    BOOST_CHECK_EQUAL(find_value_in_result(r.get_obj(), "size").get_int(), 193);
    BOOST_CHECK_EQUAL(find_value_in_result(r.get_obj(), "version").get_int(), 1);
    BOOST_CHECK_EQUAL(find_value_in_result(r.get_obj(), "locktime").get_int(), 0);

    BOOST_CHECK_THROW(CallRPC("signrawtransaction"), std::runtime_error);
    BOOST_CHECK_THROW(CallRPC("signrawtransaction null"), std::runtime_error);
    BOOST_CHECK_THROW(CallRPC("signrawtransaction ff00"), std::runtime_error);
    BOOST_CHECK_NO_THROW(CallRPC(std::string("signrawtransaction ") + rawtx));
    BOOST_CHECK_NO_THROW(CallRPC(std::string("signrawtransaction ") + rawtx +
                                 " null null NONE|FORKID|ANYONECANPAY"));
    BOOST_CHECK_NO_THROW(CallRPC(std::string("signrawtransaction ") + rawtx +
                                 " [] [] NONE|FORKID|ANYONECANPAY"));
    BOOST_CHECK_THROW(CallRPC(std::string("signrawtransaction ") + rawtx +
                              " null null badenum"),
                      std::runtime_error);

    // Only check failure cases for sendrawtransaction, there's no network to
    // send to...
    BOOST_CHECK_THROW(CallRPC("sendrawtransaction"), std::runtime_error);
    BOOST_CHECK_THROW(CallRPC("sendrawtransaction null"), std::runtime_error);
    BOOST_CHECK_THROW(CallRPC("sendrawtransaction DEADBEEF"),
                      std::runtime_error);
    BOOST_CHECK_THROW(
        CallRPC(std::string("sendrawtransaction ") + rawtx + " extra"),
        std::runtime_error);
}

BOOST_AUTO_TEST_CASE(rpc_togglenetwork) {
    UniValue r;

    r = CallRPC("getnetworkinfo");
    bool netState = find_value(r.get_obj(), "networkactive").get_bool();
    BOOST_CHECK_EQUAL(netState, true);

    BOOST_CHECK_NO_THROW(CallRPC("setnetworkactive false"));
    r = CallRPC("getnetworkinfo");
    int numConnection = find_value(r.get_obj(), "connections").get_int();
    BOOST_CHECK_EQUAL(numConnection, 0);

    netState = find_value(r.get_obj(), "networkactive").get_bool();
    BOOST_CHECK_EQUAL(netState, false);

    BOOST_CHECK_NO_THROW(CallRPC("setnetworkactive true"));
    r = CallRPC("getnetworkinfo");
    netState = find_value(r.get_obj(), "networkactive").get_bool();
    BOOST_CHECK_EQUAL(netState, true);
}

BOOST_AUTO_TEST_CASE(rpc_rawsign) {
    UniValue r;
    // input is a 1-of-2 multisig (so is output):
    std::string prevout = "[{\"txid\":"
                          "\"b4cc287e58f87cdae59417329f710f3ecd75a4ee1d2872b724"
                          "8f50977c8493f3\","
                          "\"vout\":1,\"scriptPubKey\":"
                          "\"a914b10c9df5f7edf436c697f02f1efdba4cf399615187\","
                          "\"amount\":3.14159,"
                          "\"redeemScript\":"
                          "\"512103debedc17b3df2badbcdd86d5feb4562b86fe182e5998"
                          "abd8bcd4f122c6155b1b21027e940bb73ab8732bfdf7f9216ece"
                          "fca5b94d6df834e77e108f68e66f126044c052ae\"}]";
    r = CallRPC(std::string("createrawtransaction ") + prevout + " " +
                "{\"3HqAe9LtNBjnsfM4CyYaWTnvCaUYT7v4oZ\":11}");
    std::string notsigned = r.get_str();
    std::string privkey1 =
        "\"KzsXybp9jX64P5ekX1KUxRQ79Jht9uzW7LorgwE65i5rWACL6LQe\"";
    std::string privkey2 =
        "\"Kyhdf5LuKTRx4ge69ybABsiUAWjVRK4XGxAKk2FQLp2HjGMy87Z4\"";
    r = CallRPC(std::string("signrawtransaction ") + notsigned + " " + prevout +
                " " + "[]");
    BOOST_CHECK(find_value(r.get_obj(), "complete").get_bool() == false);
    r = CallRPC(std::string("signrawtransaction ") + notsigned + " " + prevout +
                " " + "[" + privkey1 + "," + privkey2 + "]");
    BOOST_CHECK(find_value(r.get_obj(), "complete").get_bool() == true);
}

BOOST_AUTO_TEST_CASE(rpc_rawsign_missing_amount) {
    // Old format, missing amount parameter for prevout should generate
    // an RPC error.  This is because of new replay-protected tx's require
    // nonzero amount present in signed tx.
    // See: https://github.com/Bitcoin-ABC/bitcoin-abc/issues/63
    // (We will re-use the tx + keys from the above rpc_rawsign test for
    // simplicity.)
    UniValue r;
    std::string prevout = "[{\"txid\":"
                          "\"b4cc287e58f87cdae59417329f710f3ecd75a4ee1d2872b724"
                          "8f50977c8493f3\","
                          "\"vout\":1,\"scriptPubKey\":"
                          "\"a914b10c9df5f7edf436c697f02f1efdba4cf399615187\","
                          "\"redeemScript\":"
                          "\"512103debedc17b3df2badbcdd86d5feb4562b86fe182e5998"
                          "abd8bcd4f122c6155b1b21027e940bb73ab8732bfdf7f9216ece"
                          "fca5b94d6df834e77e108f68e66f126044c052ae\"}]";
    r = CallRPC(std::string("createrawtransaction ") + prevout + " " +
                "{\"3HqAe9LtNBjnsfM4CyYaWTnvCaUYT7v4oZ\":11}");
    std::string notsigned = r.get_str();
    std::string privkey1 =
        "\"KzsXybp9jX64P5ekX1KUxRQ79Jht9uzW7LorgwE65i5rWACL6LQe\"";
    std::string privkey2 =
        "\"Kyhdf5LuKTRx4ge69ybABsiUAWjVRK4XGxAKk2FQLp2HjGMy87Z4\"";
    bool exceptionThrownDueToMissingAmount = false,
         errorWasMissingAmount = false;
    try {
        r = CallRPC(std::string("signrawtransaction ") + notsigned + " " +
                    prevout + " " + "[" + privkey1 + "," + privkey2 + "]");
    } catch (const std::runtime_error &e) {
        exceptionThrownDueToMissingAmount = true;
        if (std::string(e.what()).find("amount") != std::string::npos) {
            errorWasMissingAmount = true;
        }
    }
    BOOST_CHECK(exceptionThrownDueToMissingAmount == true);
    BOOST_CHECK(errorWasMissingAmount == true);
}

BOOST_AUTO_TEST_CASE(rpc_createraw_op_return) {
    BOOST_CHECK_NO_THROW(
        CallRPC("createrawtransaction "
                "[{\"txid\":"
                "\"a3b807410df0b60fcb9736768df5823938b2f838694939ba45f3c0a1bff1"
                "50ed\",\"vout\":0}] {\"data\":\"68656c6c6f776f726c64\"}"));

    // Allow more than one data transaction output
    BOOST_CHECK_NO_THROW(CallRPC("createrawtransaction "
                                 "[{\"txid\":"
                                 "\"a3b807410df0b60fcb9736768df5823938b2f838694"
                                 "939ba45f3c0a1bff150ed\",\"vout\":0}] "
                                 "{\"data\":\"68656c6c6f776f726c64\",\"data\":"
                                 "\"68656c6c6f776f726c64\"}"));

    // Key not "data" (bad address)
    BOOST_CHECK_THROW(
        CallRPC("createrawtransaction "
                "[{\"txid\":"
                "\"a3b807410df0b60fcb9736768df5823938b2f838694939ba45f3c0a1bff1"
                "50ed\",\"vout\":0}] {\"somedata\":\"68656c6c6f776f726c64\"}"),
        std::runtime_error);

    // Bad hex encoding of data output
    BOOST_CHECK_THROW(
        CallRPC("createrawtransaction "
                "[{\"txid\":"
                "\"a3b807410df0b60fcb9736768df5823938b2f838694939ba45f3c0a1bff1"
                "50ed\",\"vout\":0}] {\"data\":\"12345\"}"),
        std::runtime_error);
    BOOST_CHECK_THROW(
        CallRPC("createrawtransaction "
                "[{\"txid\":"
                "\"a3b807410df0b60fcb9736768df5823938b2f838694939ba45f3c0a1bff1"
                "50ed\",\"vout\":0}] {\"data\":\"12345g\"}"),
        std::runtime_error);

    // Data 81 bytes long
    BOOST_CHECK_NO_THROW(
        CallRPC("createrawtransaction "
                "[{\"txid\":"
                "\"a3b807410df0b60fcb9736768df5823938b2f838694939ba45f3c0a1bff1"
                "50ed\",\"vout\":0}] "
                "{\"data\":"
                "\"010203040506070809101112131415161718192021222324252627282930"
                "31323334353637383940414243444546474849505152535455565758596061"
                "6263646566676869707172737475767778798081\"}"));
}

BOOST_AUTO_TEST_CASE(rpc_format_monetary_values) {
    BOOST_CHECK(ValueFromAmount(Amount(0LL)).write() == "0.00000000");
    BOOST_CHECK(ValueFromAmount(Amount(1LL)).write() == "0.00000001");
    BOOST_CHECK(ValueFromAmount(Amount(17622195LL)).write() == "0.17622195");
    BOOST_CHECK(ValueFromAmount(Amount(50000000LL)).write() == "0.50000000");
    BOOST_CHECK(ValueFromAmount(Amount(89898989LL)).write() == "0.89898989");
    BOOST_CHECK(ValueFromAmount(Amount(100000000LL)).write() == "1.00000000");
    BOOST_CHECK(ValueFromAmount(Amount(2099999999999990LL)).write() ==
                "20999999.99999990");
    BOOST_CHECK(ValueFromAmount(Amount(2099999999999999LL)).write() ==
                "20999999.99999999");

    BOOST_CHECK_EQUAL(ValueFromAmount(Amount(0)).write(), "0.00000000");
    BOOST_CHECK_EQUAL(ValueFromAmount(123456789 * (COIN / 10000)).write(),
                      "12345.67890000");
    BOOST_CHECK_EQUAL(ValueFromAmount(-1 * COIN).write(), "-1.00000000");
    BOOST_CHECK_EQUAL(ValueFromAmount(-1 * COIN / 10).write(), "-0.10000000");

    BOOST_CHECK_EQUAL(ValueFromAmount(100000000 * COIN).write(),
                      "100000000.00000000");
    BOOST_CHECK_EQUAL(ValueFromAmount(10000000 * COIN).write(),
                      "10000000.00000000");
    BOOST_CHECK_EQUAL(ValueFromAmount(1000000 * COIN).write(),
                      "1000000.00000000");
    BOOST_CHECK_EQUAL(ValueFromAmount(100000 * COIN).write(),
                      "100000.00000000");
    BOOST_CHECK_EQUAL(ValueFromAmount(10000 * COIN).write(), "10000.00000000");
    BOOST_CHECK_EQUAL(ValueFromAmount(1000 * COIN).write(), "1000.00000000");
    BOOST_CHECK_EQUAL(ValueFromAmount(100 * COIN).write(), "100.00000000");
    BOOST_CHECK_EQUAL(ValueFromAmount(10 * COIN).write(), "10.00000000");
    BOOST_CHECK_EQUAL(ValueFromAmount(COIN).write(), "1.00000000");
    BOOST_CHECK_EQUAL(ValueFromAmount(COIN / 10).write(), "0.10000000");
    BOOST_CHECK_EQUAL(ValueFromAmount(COIN / 100).write(), "0.01000000");
    BOOST_CHECK_EQUAL(ValueFromAmount(COIN / 1000).write(), "0.00100000");
    BOOST_CHECK_EQUAL(ValueFromAmount(COIN / 10000).write(), "0.00010000");
    BOOST_CHECK_EQUAL(ValueFromAmount(COIN / 100000).write(), "0.00001000");
    BOOST_CHECK_EQUAL(ValueFromAmount(COIN / 1000000).write(), "0.00000100");
    BOOST_CHECK_EQUAL(ValueFromAmount(COIN / 10000000).write(), "0.00000010");
    BOOST_CHECK_EQUAL(ValueFromAmount(COIN / 100000000).write(), "0.00000001");
}

static UniValue ValueFromString(const std::string &str) {
    UniValue value;
    BOOST_CHECK(value.setNumStr(str));
    return value;
}

BOOST_AUTO_TEST_CASE(rpc_parse_monetary_values) {
    BOOST_CHECK_THROW(AmountFromValue(ValueFromString("-0.00000001")),
                      UniValue);
    BOOST_CHECK_EQUAL(AmountFromValue(ValueFromString("0")), Amount(0LL));
    BOOST_CHECK_EQUAL(AmountFromValue(ValueFromString("0.00000000")),
                      Amount(0LL));
    BOOST_CHECK_EQUAL(AmountFromValue(ValueFromString("0.00000001")),
                      Amount(1LL));
    BOOST_CHECK_EQUAL(AmountFromValue(ValueFromString("0.17622195")),
                      Amount(17622195LL));
    BOOST_CHECK_EQUAL(AmountFromValue(ValueFromString("0.5")),
                      Amount(50000000LL));
    BOOST_CHECK_EQUAL(AmountFromValue(ValueFromString("0.50000000")),
                      Amount(50000000LL));
    BOOST_CHECK_EQUAL(AmountFromValue(ValueFromString("0.89898989")),
                      Amount(89898989LL));
    BOOST_CHECK_EQUAL(AmountFromValue(ValueFromString("1.00000000")),
                      Amount(100000000LL));
    BOOST_CHECK_EQUAL(AmountFromValue(ValueFromString("20999999.9999999")),
                      Amount(2099999999999990LL));
    BOOST_CHECK_EQUAL(AmountFromValue(ValueFromString("20999999.99999999")),
                      Amount(2099999999999999LL));

    BOOST_CHECK_EQUAL(AmountFromValue(ValueFromString("1e-8")),
                      COIN / 100000000);
    BOOST_CHECK_EQUAL(AmountFromValue(ValueFromString("0.1e-7")),
                      COIN / 100000000);
    BOOST_CHECK_EQUAL(AmountFromValue(ValueFromString("0.01e-6")),
                      COIN / 100000000);
    BOOST_CHECK_EQUAL(AmountFromValue(ValueFromString(
                          "0."
                          "0000000000000000000000000000000000000000000000000000"
                          "000000000000000000000001e+68")),
                      COIN / 100000000);
    BOOST_CHECK_EQUAL(
        AmountFromValue(ValueFromString("10000000000000000000000000000000000000"
                                        "000000000000000000000000000e-64")),
        COIN);
    BOOST_CHECK_EQUAL(
        AmountFromValue(ValueFromString(
            "0."
            "000000000000000000000000000000000000000000000000000000000000000100"
            "000000000000000000000000000000000000000000000000000e64")),
        COIN);

    // should fail
    BOOST_CHECK_THROW(AmountFromValue(ValueFromString("1e-9")), UniValue);
    // should fail
    BOOST_CHECK_THROW(AmountFromValue(ValueFromString("0.000000019")),
                      UniValue);
    // should pass, cut trailing 0
    BOOST_CHECK_EQUAL(AmountFromValue(ValueFromString("0.00000001000000")),
                      Amount(1LL));
    // should fail
    BOOST_CHECK_THROW(AmountFromValue(ValueFromString("19e-9")), UniValue);
    // should pass, leading 0 is present
    BOOST_CHECK_EQUAL(AmountFromValue(ValueFromString("0.19e-6")), Amount(19));

    // overflow error
    BOOST_CHECK_THROW(AmountFromValue(ValueFromString("92233720368.54775808")),
                      UniValue);
    // overflow error
    BOOST_CHECK_THROW(AmountFromValue(ValueFromString("1e+11")), UniValue);
    // overflow error signless
    BOOST_CHECK_THROW(AmountFromValue(ValueFromString("1e11")), UniValue);
    // overflow error
    BOOST_CHECK_THROW(AmountFromValue(ValueFromString("93e+9")), UniValue);
}

BOOST_AUTO_TEST_CASE(json_parse_errors) {
    // Valid
    BOOST_CHECK_EQUAL(ParseNonRFCJSONValue("1.0").get_real(), 1.0);
    // Valid, with leading or trailing whitespace
    BOOST_CHECK_EQUAL(ParseNonRFCJSONValue(" 1.0").get_real(), 1.0);
    BOOST_CHECK_EQUAL(ParseNonRFCJSONValue("1.0 ").get_real(), 1.0);

    // should fail, missing leading 0, therefore invalid JSON
    BOOST_CHECK_THROW(AmountFromValue(ParseNonRFCJSONValue(".19e-6")),
                      std::runtime_error);
    BOOST_CHECK_EQUAL(AmountFromValue(ParseNonRFCJSONValue(
                          "0.00000000000000000000000000000000000001e+30 ")),
                      Amount(1));
    // Invalid, initial garbage
    BOOST_CHECK_THROW(ParseNonRFCJSONValue("[1.0"), std::runtime_error);
    BOOST_CHECK_THROW(ParseNonRFCJSONValue("a1.0"), std::runtime_error);
    // Invalid, trailing garbage
    BOOST_CHECK_THROW(ParseNonRFCJSONValue("1.0sds"), std::runtime_error);
    BOOST_CHECK_THROW(ParseNonRFCJSONValue("1.0]"), std::runtime_error);
    // BSV addresses should fail parsing
    BOOST_CHECK_THROW(
        ParseNonRFCJSONValue("175tWpb8K1S7NmH4Zx6rewF9WQrcZv245W"),
        std::runtime_error);
    BOOST_CHECK_THROW(ParseNonRFCJSONValue("3J98t1WpEZ73CNmQviecrnyiWrnqRhWNL"),
                      std::runtime_error);
}

BOOST_AUTO_TEST_CASE(rpc_ban) {
    return;
    BOOST_CHECK_NO_THROW(CallRPC(std::string("clearbanned")));

    UniValue r;
    BOOST_CHECK_NO_THROW(r = CallRPC(std::string("setban 127.0.0.0 add")));
    // portnumber for setban not allowed
    BOOST_CHECK_THROW(r = CallRPC(std::string("setban 127.0.0.0:8334")),
                      std::runtime_error);
    BOOST_CHECK_NO_THROW(r = CallRPC(std::string("listbanned")));
    UniValue ar = r.get_array();
    UniValue o1 = ar[0].get_obj();
    UniValue adr = find_value(o1, "address");
    BOOST_CHECK_EQUAL(adr.get_str(), "127.0.0.0/32");
    BOOST_CHECK_NO_THROW(CallRPC(std::string("setban 127.0.0.0 remove")));
    BOOST_CHECK_NO_THROW(r = CallRPC(std::string("listbanned")));
    ar = r.get_array();
    BOOST_CHECK_EQUAL(ar.size(), 0UL);

    // ban the node until 1.4.2030 => 1901232000
    BOOST_CHECK_NO_THROW(
        r = CallRPC(std::string("setban 127.0.0.0/24 add 1901232000 true")));
    BOOST_CHECK_NO_THROW(r = CallRPC(std::string("listbanned")));
    ar = r.get_array();
    o1 = ar[0].get_obj();
    adr = find_value(o1, "address");
    UniValue banned_until = find_value(o1, "banned_until");
    BOOST_CHECK_EQUAL(adr.get_str(), "127.0.0.0/24");
    // absolute time check
    BOOST_CHECK_EQUAL(banned_until.get_int64(), 1901232000);

    BOOST_CHECK_NO_THROW(CallRPC(std::string("clearbanned")));

    BOOST_CHECK_NO_THROW(
        r = CallRPC(std::string("setban 127.0.0.0/24 add 200")));
    BOOST_CHECK_NO_THROW(r = CallRPC(std::string("listbanned")));
    ar = r.get_array();
    o1 = ar[0].get_obj();
    adr = find_value(o1, "address");
    banned_until = find_value(o1, "banned_until");
    BOOST_CHECK_EQUAL(adr.get_str(), "127.0.0.0/24");
    int64_t now = GetTime();
    BOOST_CHECK(banned_until.get_int64() > now);
    BOOST_CHECK(banned_until.get_int64() - now <= 200);

    // must throw an exception because 127.0.0.1 is in already banned subnet
    // range
    BOOST_CHECK_THROW(r = CallRPC(std::string("setban 127.0.0.1 add")),
                      std::runtime_error);

    BOOST_CHECK_NO_THROW(CallRPC(std::string("setban 127.0.0.0/24 remove")));
    BOOST_CHECK_NO_THROW(r = CallRPC(std::string("listbanned")));
    ar = r.get_array();
    BOOST_CHECK_EQUAL(ar.size(), 0UL);

    BOOST_CHECK_NO_THROW(
        r = CallRPC(std::string("setban 127.0.0.0/255.255.0.0 add")));
    BOOST_CHECK_THROW(r = CallRPC(std::string("setban 127.0.1.1 add")),
                      std::runtime_error);

    BOOST_CHECK_NO_THROW(CallRPC(std::string("clearbanned")));
    BOOST_CHECK_NO_THROW(r = CallRPC(std::string("listbanned")));
    ar = r.get_array();
    BOOST_CHECK_EQUAL(ar.size(), 0UL);

    // invalid IP
    BOOST_CHECK_THROW(r = CallRPC(std::string("setban test add")),
                      std::runtime_error);

    // IPv6 tests
    BOOST_CHECK_NO_THROW(
        r = CallRPC(
            std::string("setban FE80:0000:0000:0000:0202:B3FF:FE1E:8329 add")));
    BOOST_CHECK_NO_THROW(r = CallRPC(std::string("listbanned")));
    ar = r.get_array();
    o1 = ar[0].get_obj();
    adr = find_value(o1, "address");
    BOOST_CHECK_EQUAL(adr.get_str(), "fe80::202:b3ff:fe1e:8329/128");

    BOOST_CHECK_NO_THROW(CallRPC(std::string("clearbanned")));
    BOOST_CHECK_NO_THROW(r = CallRPC(std::string(
                             "setban 2001:db8::/ffff:fffc:0:0:0:0:0:0 add")));
    BOOST_CHECK_NO_THROW(r = CallRPC(std::string("listbanned")));
    ar = r.get_array();
    o1 = ar[0].get_obj();
    adr = find_value(o1, "address");
    BOOST_CHECK_EQUAL(adr.get_str(), "2001:db8::/30");

    BOOST_CHECK_NO_THROW(CallRPC(std::string("clearbanned")));
    BOOST_CHECK_NO_THROW(
        r = CallRPC(std::string(
            "setban 2001:4d48:ac57:400:cacf:e9ff:fe1d:9c63/128 add")));
    BOOST_CHECK_NO_THROW(r = CallRPC(std::string("listbanned")));
    ar = r.get_array();
    o1 = ar[0].get_obj();
    adr = find_value(o1, "address");
    BOOST_CHECK_EQUAL(adr.get_str(),
                      "2001:4d48:ac57:400:cacf:e9ff:fe1d:9c63/128");
}

BOOST_AUTO_TEST_CASE(rpc_convert_values_generatetoaddress) {
    UniValue result;

    BOOST_CHECK_NO_THROW(result = RPCConvertValues(
                             "generatetoaddress",
                             {"101", "mkESjLZW66TmHhiFX8MCaBjrhZ543PPh9a"}));
    BOOST_CHECK_EQUAL(result[0].get_int(), 101);
    BOOST_CHECK_EQUAL(result[1].get_str(),
                      "mkESjLZW66TmHhiFX8MCaBjrhZ543PPh9a");

    BOOST_CHECK_NO_THROW(result = RPCConvertValues(
                             "generatetoaddress",
                             {"101", "mhMbmE2tE9xzJYCV9aNC8jKWN31vtGrguU"}));
    BOOST_CHECK_EQUAL(result[0].get_int(), 101);
    BOOST_CHECK_EQUAL(result[1].get_str(),
                      "mhMbmE2tE9xzJYCV9aNC8jKWN31vtGrguU");

    BOOST_CHECK_NO_THROW(result = RPCConvertValues(
                             "generatetoaddress",
                             {"1", "mkESjLZW66TmHhiFX8MCaBjrhZ543PPh9a", "9"}));
    BOOST_CHECK_EQUAL(result[0].get_int(), 1);
    BOOST_CHECK_EQUAL(result[1].get_str(),
                      "mkESjLZW66TmHhiFX8MCaBjrhZ543PPh9a");
    BOOST_CHECK_EQUAL(result[2].get_int(), 9);

    BOOST_CHECK_NO_THROW(result = RPCConvertValues(
                             "generatetoaddress",
                             {"1", "mhMbmE2tE9xzJYCV9aNC8jKWN31vtGrguU", "9"}));
    BOOST_CHECK_EQUAL(result[0].get_int(), 1);
    BOOST_CHECK_EQUAL(result[1].get_str(),
                      "mhMbmE2tE9xzJYCV9aNC8jKWN31vtGrguU");
    BOOST_CHECK_EQUAL(result[2].get_int(), 9);
}

BOOST_AUTO_TEST_CASE(getminingcandidate_low_height)
{
    // Fake things so that IBD thinks it's completed and we don't care about the lack of peers
    gArgs.SoftSetBoolArg("-standalone", true);
    int64_t oldMaxAge {nMaxTipAge};
    nMaxTipAge = std::numeric_limits<int64_t>::max();

    UniValue json {};
    BOOST_CHECK_NO_THROW(json = CallRPC(std::string("getminingcandidate")));
    BOOST_CHECK_EQUAL(find_value(json.get_obj(), "height").get_int(), 1);

    nMaxTipAge = oldMaxAge;
}

// Create client configs for double-spend endpoint
BOOST_AUTO_TEST_CASE(client_config_ds_endpoint)
{
    using namespace rpc::client;
    GlobalConfig config {};

    // IPv4
    BOOST_CHECK_NO_THROW(
        // Create DSCallbackMsg to base config from
        std::string ip {"127.0.0.1"};
        DSCallbackMsg ipv4_callback(0x01, {ip}, {});

        // Create RPC config using DSCallbackMsg
        RPCClientConfig clientConfig {
            RPCClientConfig::CreateForDoubleSpendEndpoint(
                config,
                DSCallbackMsg::IPAddrToString(ipv4_callback.GetAddresses()[0]),
                RPCClientConfig::DEFAULT_DS_ENDPOINT_FAST_TIMEOUT,
                ipv4_callback.GetProtocolVersion()
            )
        };

        BOOST_CHECK_EQUAL(clientConfig.GetServerIP(), ip);
        BOOST_CHECK_EQUAL(clientConfig.GetServerPort(), RPCClientConfig::DEFAULT_DS_ENDPOINT_PORT);
        BOOST_CHECK_EQUAL(clientConfig.GetServerHTTPHost(), ip);
        BOOST_CHECK_EQUAL(clientConfig.GetConnectionTimeout(), RPCClientConfig::DEFAULT_DS_ENDPOINT_FAST_TIMEOUT);
        BOOST_CHECK(! clientConfig.UsesAuth());
        BOOST_CHECK_EQUAL(clientConfig.GetEndpoint(), "/dsnt/1/");
    );

    // IPv6
    BOOST_CHECK_NO_THROW(
        // Create DSCallbackMsg to base config from
        std::string ip {"::1"};
        std::string host { "[" + ip + "]" };
        DSCallbackMsg ipv6_callback(0x81, {ip}, {});

        // Create RPC config using DSCallbackMsg
        RPCClientConfig clientConfig {
            RPCClientConfig::CreateForDoubleSpendEndpoint(
                config,
                DSCallbackMsg::IPAddrToString(ipv6_callback.GetAddresses()[0]),
                RPCClientConfig::DEFAULT_DS_ENDPOINT_FAST_TIMEOUT,
                ipv6_callback.GetProtocolVersion()
            )
        };

        BOOST_CHECK_EQUAL(clientConfig.GetServerIP(), ip);
        BOOST_CHECK_EQUAL(clientConfig.GetServerPort(), RPCClientConfig::DEFAULT_DS_ENDPOINT_PORT);
        BOOST_CHECK_EQUAL(clientConfig.GetServerHTTPHost(), host);
        BOOST_CHECK_EQUAL(clientConfig.GetConnectionTimeout(), RPCClientConfig::DEFAULT_DS_ENDPOINT_FAST_TIMEOUT);
        BOOST_CHECK(! clientConfig.UsesAuth());
        BOOST_CHECK_EQUAL(clientConfig.GetEndpoint(), "/dsnt/1/");
    );
}

// Create client configs for bitcoind
BOOST_AUTO_TEST_CASE(client_config_bitcoind)
{
    gArgs.ForceSetArg("-rpcconnect", "localhost:8080");
    gArgs.ForceSetArg("-rpcuser", "user");
    gArgs.ForceSetArg("-rpcpassword", "passwd");
    gArgs.ForceSetArg("-rpcclienttimeout", "100");
    gArgs.ForceSetArg("-rpcwallet", "wallet");

    using namespace rpc::client;

    BOOST_CHECK_NO_THROW(
        RPCClientConfig config { RPCClientConfig::CreateForBitcoind() };
        BOOST_CHECK_EQUAL(config.GetServerIP(), "localhost");
        BOOST_CHECK_EQUAL(config.GetServerPort(), 8080);
        BOOST_CHECK_EQUAL(config.GetServerHTTPHost(), "localhost");
        BOOST_CHECK(config.UsesAuth());
        BOOST_CHECK_EQUAL(config.GetCredentials(), "user:passwd");
        BOOST_CHECK_EQUAL(config.GetConnectionTimeout(), 100);
        BOOST_CHECK_EQUAL(config.GetWallet(), "wallet");
        BOOST_CHECK_EQUAL(config.GetEndpoint(), "");
    );
}

// Create client config for miner ID generator
BOOST_AUTO_TEST_CASE(client_config_minerid_generator)
{
    using namespace rpc::client;
    GlobalConfig::GetModifiableGlobalConfig().SetMinerIdGeneratorURL("http://127.0.0.1:8080", nullptr);
    const Config& config { GlobalConfig::GetConfig() };

    BOOST_CHECK_NO_THROW(
        RPCClientConfig clientConfig { RPCClientConfig::CreateForMinerIdGenerator(config, 5) };
        BOOST_CHECK_EQUAL(clientConfig.GetServerIP(), "127.0.0.1");
        BOOST_CHECK_EQUAL(clientConfig.GetServerPort(), 8080);
        BOOST_CHECK_EQUAL(clientConfig.GetServerHTTPHost(), "127.0.0.1");
        BOOST_CHECK_EQUAL(clientConfig.GetEndpoint(), "");
    );
}

// HTTP request creation
BOOST_AUTO_TEST_CASE(http_requests)
{
    // Some parameters passed throughout
    std::string method { "somemethod" };
    std::vector<std::string> args { "tx1=1", "tx2=2" };
    UniValue params(UniValue::VARR);
    params = RPCConvertNamedValues(method, args);

    using namespace rpc::client;

    {
        // RPC request to a double-spend endpoint
        GlobalConfig config {};
        DSCallbackMsg ipv4_callback(0x01, {"127.0.0.1"}, {});
        RPCClientConfig clientConfig {
            RPCClientConfig::CreateForDoubleSpendEndpoint(
                config,
                DSCallbackMsg::IPAddrToString(ipv4_callback.GetAddresses()[0]),
                RPCClientConfig::DEFAULT_DS_ENDPOINT_FAST_TIMEOUT,
                ipv4_callback.GetProtocolVersion()
            )
        };

        auto queryRequest { rpc::client::HTTPRequest::CreateDSEndpointQueryRequest(clientConfig, "abcdef") };
        BOOST_CHECK(queryRequest.GetCommand() == RequestCmdType::GET);
        BOOST_CHECK_EQUAL(queryRequest.GetContents().size(), 0U);
        BOOST_CHECK_EQUAL(queryRequest.GetContentsSize(), 0U);
        BOOST_CHECK_EQUAL(queryRequest.GetContentsFD().Get(), -1);
        BOOST_CHECK_EQUAL(queryRequest.GetEndpoint(), "/dsnt/1/query/abcdef");

        auto submitRequest { rpc::client::HTTPRequest::CreateDSEndpointSubmitRequest(
            clientConfig,
            UniqueFileDescriptor{1}, 50,
            std::make_pair("txid", "abcdef"),
            std::make_pair("n", 0),
            std::make_pair("ctxid", "fedcba"),
            std::make_pair("cn", 1)
        ) };
        BOOST_CHECK(submitRequest.GetCommand() == RequestCmdType::POST);
        BOOST_CHECK_EQUAL(submitRequest.GetEndpoint(), "/dsnt/1/submit?txid=abcdef&n=0&ctxid=fedcba&cn=1");
        BOOST_CHECK_EQUAL(submitRequest.GetContents().size(), 0U);
        BOOST_CHECK_EQUAL(submitRequest.GetContentsSize(), 50U);
        BOOST_CHECK_EQUAL(submitRequest.GetContentsFD().Get(), 1);
        auto headers { submitRequest.GetHeaders() };
        BOOST_CHECK_EQUAL(headers.size(), 1U);
        BOOST_CHECK_EQUAL(headers[0].first, "Content-Type");
        BOOST_CHECK_EQUAL(headers[0].second, "application/octet-stream");

        // Ensure we don't actually try to close the fake file descriptor we've created above
        (void)submitRequest.GetContentsFD().Release();
    }

    {
        // JSON RPC requests to bitcoind
        gArgs.ForceSetArg("-rpcconnect", "localhost:8080");
        gArgs.ForceSetArg("-rpcuser", "user");
        gArgs.ForceSetArg("-rpcpassword", "passwd");
        gArgs.ForceSetArg("-rpcwallet", "walletname");
        RPCClientConfig config { RPCClientConfig::CreateForBitcoind() };

        auto rpcRequest { rpc::client::HTTPRequest::CreateJSONRPCRequest(config, method, params) };
        std::string strContents { rpcRequest.GetContents().begin(), rpcRequest.GetContents().end() };
        BOOST_CHECK_EQUAL(strContents, "{\"method\":\"somemethod\",\"params\":{\"tx1\":\"1\",\"tx2\":\"2\"},\"id\":1}\n");
        BOOST_CHECK_EQUAL(rpcRequest.GetContentsSize(), strContents.size());
        BOOST_CHECK_EQUAL(rpcRequest.GetEndpoint(), "/wallet/walletname");
        BOOST_CHECK_EQUAL(rpcRequest.GetHeaders().size(), 0U);
    }

    {
        // REST request to a miner ID generator
        GlobalConfig::GetModifiableGlobalConfig().SetMinerIdGeneratorURL("http://127.0.0.1:8080", nullptr);
        RPCClientConfig clientConfig { RPCClientConfig::CreateForMinerIdGenerator(GlobalConfig::GetConfig(), 5) };

        const std::string alias { "MyAlias" };
        const uint256 hash { InsecureRand256() };
        std::stringstream endpoint {};
        endpoint << "/minerid/" << alias << "/pksign/" << hash.ToString();

        auto rpcRequest {  rpc::client::HTTPRequest::CreateMinerIdGeneratorSigningRequest(clientConfig, alias, hash.ToString()) };
        BOOST_CHECK(rpcRequest.GetCommand() == RequestCmdType::GET);
        BOOST_CHECK_EQUAL(rpcRequest.GetEndpoint(), endpoint.str());
        BOOST_CHECK_EQUAL(rpcRequest.GetContents().size(), 0U);
    }
}

// HTTP response creation
BOOST_AUTO_TEST_CASE(http_responses)
{
    // BinaryHTTPResponse
    {
        // Any serialisable object will do here
        uint256 randuint { InsecureRand256() };
        CDataStream ss { SER_NETWORK, PROTOCOL_VERSION };
        ss << randuint;

        rpc::client::BinaryHTTPResponse response {};
        BOOST_CHECK(response.IsEmpty());
        response.SetBody(reinterpret_cast<const unsigned char*>(ss.data()), ss.size());
        BOOST_CHECK(! response.IsEmpty());

        uint256 deserialised {};
        response >> deserialised;
        BOOST_CHECK_EQUAL(randuint, deserialised);
    }

    // StringHTTPResponse
    {
        rpc::client::StringHTTPResponse response {};
        BOOST_CHECK(response.IsEmpty());

        std::string body { "Some string response" };
        response.SetBody(reinterpret_cast<const unsigned char*>(body.c_str()), body.size());
        BOOST_CHECK(! response.IsEmpty());
        BOOST_CHECK_EQUAL(body, response.GetBody());
    }

    // JSONHTTPResponse
    {
        rpc::client::JSONHTTPResponse response {};
        BOOST_CHECK(response.IsEmpty());

        std::string body { "{ \"field1\": \"value1\", \"field2\": \"value2\" }" };
        response.SetBody(reinterpret_cast<const unsigned char*>(body.c_str()), body.size());
        BOOST_CHECK(! response.IsEmpty());

        UniValue jsonval { response.GetBody() };
        BOOST_CHECK_EQUAL(jsonval["field1"].get_str(), "value1");
        BOOST_CHECK_EQUAL(jsonval["field2"].get_str(), "value2");
    }
}

BOOST_AUTO_TEST_CASE(rpc_verifymerkleproofparams)
{
    // Test verifymerkleproof API argument handling

    BOOST_CHECK_THROW(CallRPC("verifymerkleproof"), std::runtime_error);
    BOOST_CHECK_THROW(CallRPC("verifymerkleproof not_json"), std::runtime_error);
    BOOST_CHECK_THROW(CallRPC("verifymerkleproof []"), std::runtime_error);
    BOOST_CHECK_THROW(CallRPC("verifymerkleproof {} extra"), std::runtime_error);

    // Exceptions thrown with wrong flags values
    BOOST_CHECK_EXCEPTION(CallRPC("verifymerkleproof {"
        "\"flags\":\"my_flag\","
        "\"index\":4,"
        "\"txOrId\":\"b4cc287e58f87cdae59417329f710f3ecd75a4ee1d2872b7248f50977c8493f3\","
        "\"target\":{\"merkleroot\":\"b4cc287e58f87cdae59417329f710f3ecd75a4ee1d2872b7248f50977c8493f3\"},"
        "\"nodes\":[\"*\",\"b4cc287e58f87cdae59417329f710f3ecd75a4ee1d2872b7248f50977c8493f3\"]}"), std::runtime_error, FlagsNumericMessage);
    BOOST_CHECK_EXCEPTION(CallRPC("verifymerkleproof {"
        "\"flags\":1,"
        "\"index\":4,"
        "\"txOrId\":\"b4cc287e58f87cdae59417329f710f3ecd75a4ee1d2872b7248f50977c8493f3\","
        "\"target\":{\"merkleroot\":\"b4cc287e58f87cdae59417329f710f3ecd75a4ee1d2872b7248f50977c8493f3\"},"
        "\"nodes\":[\"*\",\"b4cc287e58f87cdae59417329f710f3ecd75a4ee1d2872b7248f50977c8493f3\"]}"), std::runtime_error, FlagsValueMessage);
    // Exceptions thrown with wrong index values
    BOOST_CHECK_EXCEPTION(CallRPC("verifymerkleproof {"
        "\"flags\":2,"
        "\"index\":\"my_index\","
        "\"txOrId\":\"b4cc287e58f87cdae59417329f710f3ecd75a4ee1d2872b7248f50977c8493f3\","
        "\"target\":{\"merkleroot\":\"b4cc287e58f87cdae59417329f710f3ecd75a4ee1d2872b7248f50977c8493f3\"},"
        "\"nodes\":[\"*\",\"b4cc287e58f87cdae59417329f710f3ecd75a4ee1d2872b7248f50977c8493f3\"]}"), std::runtime_error, IndexNumericMessage);
    BOOST_CHECK_EXCEPTION(CallRPC("verifymerkleproof {"
        "\"flags\":2,"
        "\"index\":-1,"
        "\"txOrId\":\"b4cc287e58f87cdae59417329f710f3ecd75a4ee1d2872b7248f50977c8493f3\","
        "\"target\":{\"merkleroot\":\"b4cc287e58f87cdae59417329f710f3ecd75a4ee1d2872b7248f50977c8493f3\"},"
        "\"nodes\":[\"*\",\"b4cc287e58f87cdae59417329f710f3ecd75a4ee1d2872b7248f50977c8493f3\"]}"), std::runtime_error, IndexValueMessage);
    // Exceptions thrown with wrong txOrId values
    BOOST_CHECK_EXCEPTION(CallRPC("verifymerkleproof {"
        "\"flags\":2,"
        "\"index\":4,"
        "\"txOrId\":1,"
        "\"target\":{\"merkleroot\":\"b4cc287e58f87cdae59417329f710f3ecd75a4ee1d2872b7248f50977c8493f3\"},"
        "\"nodes\":[\"*\",\"b4cc287e58f87cdae59417329f710f3ecd75a4ee1d2872b7248f50977c8493f3\"]}"), std::runtime_error, TxOrIdHashValueMessage);
    BOOST_CHECK_EXCEPTION(CallRPC("verifymerkleproof {"
        "\"flags\":2,"
        "\"index\":4,"
        "\"txOrId\":\"wrong_hash\","
        "\"target\":{\"merkleroot\":\"b4cc287e58f87cdae59417329f710f3ecd75a4ee1d2872b7248f50977c8493f3\"},"
        "\"nodes\":[\"*\",\"b4cc287e58f87cdae59417329f710f3ecd75a4ee1d2872b7248f50977c8493f3\"]}"), std::runtime_error, TxOrIdHashValueMessage2);
    // Exceptions thrown with wrong target values
    BOOST_CHECK_EXCEPTION(CallRPC("verifymerkleproof {"
        "\"flags\":2,"
        "\"index\":4,"
        "\"txOrId\":\"b4cc287e58f87cdae59417329f710f3ecd75a4ee1d2872b7248f50977c8493f3\","
        "\"target\":1,"
        "\"nodes\":[\"*\",\"b4cc287e58f87cdae59417329f710f3ecd75a4ee1d2872b7248f50977c8493f3\"]}"), std::runtime_error, TargetObjectMessage);
    BOOST_CHECK_EXCEPTION(CallRPC("verifymerkleproof {"
        "\"flags\":2,"
        "\"index\":4,"
        "\"txOrId\":\"b4cc287e58f87cdae59417329f710f3ecd75a4ee1d2872b7248f50977c8493f3\","
        "\"target\":{\"merkelroot\":\"b4cc287e58f87cdae59417329f710f3ecd75a4ee1d2872b7248f50977c8493f3\"},"
        "\"nodes\":[\"*\",\"b4cc287e58f87cdae59417329f710f3ecd75a4ee1d2872b7248f50977c8493f3\"]}"), std::runtime_error, MerkleRootHashMessage);
    // Exceptions thrown with wrong nodes values
    BOOST_CHECK_EXCEPTION(CallRPC("verifymerkleproof {"
        "\"flags\":2,"
        "\"index\":4,"
        "\"txOrId\":\"b4cc287e58f87cdae59417329f710f3ecd75a4ee1d2872b7248f50977c8493f3\","
        "\"target\":{\"merkleroot\":\"b4cc287e58f87cdae59417329f710f3ecd75a4ee1d2872b7248f50977c8493f3\"},"
        "\"nodes\":\"my_nodes\"}"), std::runtime_error, NodesArrayMessage);
    BOOST_CHECK_EXCEPTION(CallRPC("verifymerkleproof {"
        "\"flags\":2,"
        "\"index\":4,"
        "\"txOrId\":\"b4cc287e58f87cdae59417329f710f3ecd75a4ee1d2872b7248f50977c8493f3\","
        "\"target\":{\"merkleroot\":\"b4cc287e58f87cdae59417329f710f3ecd75a4ee1d2872b7248f50977c8493f3\"},"
        "\"nodes\":[\"**\",\"b4cc287e58f87cdae59417329f710f3ecd75a4ee1d2872b7248f50977c8493f3\"]}"), std::runtime_error, NodeHashValueMessage);
    // Proper Json format should not throw any exception
    BOOST_CHECK_NO_THROW(CallRPC("verifymerkleproof {"
        "\"flags\":2,"
        "\"index\":4,"
        "\"txOrId\":\"b4cc287e58f87cdae59417329f710f3ecd75a4ee1d2872b7248f50977c8493f3\","
        "\"target\":{\"merkleroot\":\"b4cc287e58f87cdae59417329f710f3ecd75a4ee1d2872b7248f50977c8493f3\"},"
        "\"nodes\":[\"*\",\"b4cc287e58f87cdae59417329f710f3ecd75a4ee1d2872b7248f50977c8493f3\"]}"));
}

BOOST_AUTO_TEST_SUITE_END()
