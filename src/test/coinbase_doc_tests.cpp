// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include <string_view>

#include <boost/test/tools/old/interface.hpp>
#include <boost/test/unit_test.hpp>

#include "miner_id/coinbase_doc.h"
#include "miner_id/miner_id.h"
#include "primitives/transaction.h"
#include "univalue.h"

#include "test/test_bitcoin.h"

using namespace std;

BOOST_FIXTURE_TEST_SUITE(coinbase_doc_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(default_constructor_equality)
{
    // reflexivity
    CoinbaseDocument a;
    BOOST_CHECK_EQUAL(a, a);

    // symmetry
    CoinbaseDocument b;
    BOOST_CHECK_EQUAL(a, b);
    BOOST_CHECK_EQUAL(b, a);

    // transitivity
    CoinbaseDocument c;
    BOOST_CHECK_EQUAL(a, b);
    BOOST_CHECK_EQUAL(b, c);
    BOOST_CHECK_EQUAL(c, a);
}

BOOST_AUTO_TEST_CASE(user_defined_constructor_equality)
{
    const string version{"version"};
    const int32_t height{42};
    const string prev_miner_id{"prev_miner_id"};
    const string prev_miner_id_sig{"prev_miner_id_sig"};
    const string miner_id{"miner_id"};
    const COutPoint op;
    const optional<UniValue> miner_contact;

    CoinbaseDocument a{"",
                       version,
                       height,
                       prev_miner_id,
                       prev_miner_id_sig,
                       miner_id,
                       op,
                       miner_contact};

    // reflexivity
    BOOST_CHECK_EQUAL(a, a);

    // symmetry
    const CoinbaseDocument b{a};
    BOOST_CHECK_EQUAL(a, b);
    BOOST_CHECK_EQUAL(b, a);

    // transitivity
    const CoinbaseDocument c{a};
    BOOST_CHECK_EQUAL(a, b);
    BOOST_CHECK_EQUAL(b, c);
    BOOST_CHECK_EQUAL(c, a);
}

BOOST_AUTO_TEST_CASE(inequality)
{
    const string v{"version"};
    const int32_t h{42};
    const string prev_id{"prev_miner_id"};
    const string prev_id_sig{"prev_miner_id_sig"};
    const string id{"miner_id"};
    const COutPoint op;
    const optional<UniValue> contact;

    CoinbaseDocument a{"", v, h, prev_id, prev_id_sig, id, op, contact};
    CoinbaseDocument b{"", "", h, prev_id, prev_id_sig, id, op, contact};
    BOOST_CHECK_NE(a, b);
    BOOST_CHECK(a != b); // check op!= is defined
    CoinbaseDocument c{"", v, 0, prev_id, prev_id_sig, id, op, contact};
    BOOST_CHECK_NE(a, c);
    CoinbaseDocument d{"", v, h, "", prev_id_sig, id, op, contact};
    BOOST_CHECK_NE(a, d);
    CoinbaseDocument e{"", v, h, prev_id, "", id, op, contact};
    BOOST_CHECK_NE(a, e);
    CoinbaseDocument f{"", v, h, prev_id, prev_id_sig, "", op, contact};
    BOOST_CHECK_NE(a, f);

    const vector<uint8_t> tmp{0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7,
                              0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf,
                              0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7,
                              0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf};
    const TxId txid{uint256{tmp}};
    const COutPoint op2{txid, 0};
    CoinbaseDocument g{"", v, h, prev_id, prev_id_sig, id, op2, contact};
    BOOST_CHECK_NE(a, g);

    // test datarefs
    CoinbaseDocument cd_dr0{a};
    CoinbaseDocument cd_dr1{a};
    CoinbaseDocument cd_dr11{a};
    CoinbaseDocument cd_dr12{a};

    vector<CoinbaseDocument::DataRef> datarefs_0;
    cd_dr0.SetDataRefs(datarefs_0);

    CoinbaseDocument::DataRef dr1{{"id1", "id2"}, txid, 0, ""};
    const TxId txid2{uint256{tmp}};
    CoinbaseDocument::DataRef dr2{{"id3", "id4"}, txid2, 0, ""};

    vector<CoinbaseDocument::DataRef> datarefs_1{dr1};
    vector<CoinbaseDocument::DataRef> datarefs_11{dr1, dr1};
    vector<CoinbaseDocument::DataRef> datarefs_12{dr1, dr2};

    cd_dr1.SetDataRefs(datarefs_1);
    cd_dr11.SetDataRefs(datarefs_11);
    cd_dr12.SetDataRefs(datarefs_12);

    BOOST_CHECK_NE(a, cd_dr0);
    BOOST_CHECK_NE(cd_dr0, a);
    BOOST_CHECK_NE(a, cd_dr1);
    BOOST_CHECK_NE(cd_dr1, a);
    BOOST_CHECK_NE(a, cd_dr11);
    BOOST_CHECK_NE(cd_dr11, a);

    BOOST_CHECK_EQUAL(cd_dr0, cd_dr0);
    BOOST_CHECK_NE(cd_dr0, cd_dr1);
    BOOST_CHECK_NE(cd_dr1, cd_dr0);
    BOOST_CHECK_NE(cd_dr0, cd_dr11);
    BOOST_CHECK_NE(cd_dr11, cd_dr0);

    BOOST_CHECK_EQUAL(cd_dr1, cd_dr1);
    BOOST_CHECK_NE(cd_dr1, cd_dr11);
    BOOST_CHECK_NE(cd_dr11, cd_dr1);

    BOOST_CHECK_EQUAL(cd_dr11, cd_dr11);
    BOOST_CHECK_NE(cd_dr11, cd_dr12);
    BOOST_CHECK_NE(cd_dr12, cd_dr11);
    BOOST_CHECK_EQUAL(cd_dr12, cd_dr12);
}

// namespace
// {
//    CoinbaseDocument from_json_static(const string& s)
//    {
//        UniValue v;
//        v.read(s);
//
//        MinerId miner_id;
//        const vector<uint8_t> sig;
//        COutPoint cout;
//        int32_t block_height{42};
//        miner_id.SetStaticCoinbaseDocument(v, sig, cout, block_height);
//
//        return miner_id.GetCoinbaseDocument();
//    }

//    CoinbaseDocument from_json_dynamic(const string& s)
//    {
//        UniValue v;
//        v.read(s);
//
//        MinerId miner_id;
//        const vector<uint8_t> sig;
//        COutPoint cout;
//        int32_t block_height{42};
//        miner_id.SetDynamicCoinbaseDocument(v, sig, cout, block_height);
//
//        return miner_id.GetCoinbaseDocument();
//    }
// }

// BOOST_AUTO_TEST_CASE(to_and_from_json_static)
//{
//    const string v{"0.1"};
//    const int32_t h{42};
//    const string prev_id{"prev_miner_id"};
//    const string prev_id_sig{"prev_miner_id_sig"};
//    const string id{"miner_id"};
//    auto x = uint256S("123456789abcdef0"
//                      "123456789abcdef0"
//                      "123456789abcdef0"
//                      "123456789abcdef0");
//    const COutPoint op{x, 0};
//    const optional<UniValue> contact;
//
//    CoinbaseDocument input{v, h, prev_id, prev_id_sig, id, op, contact};
//
//    vector<CoinbaseDocument::DataRef> dataRefs = {
//        CoinbaseDocument::DataRef{{"id1", "id2"}, x, 0},
//        CoinbaseDocument::DataRef{{"id1", "id2"}, x, 0}};
//    input.SetDataRefs(dataRefs);
//
//    const auto json{to_json(input)};
//    const auto output{from_json_static(json)};
//    BOOST_CHECK_EQUAL(output, input);
//}

// This doesn't work as only datarefs are actually set
// BOOST_AUTO_TEST_CASE(to_and_from_json_dynamic)
//{
//    const string v{"0.1"};
//    const int32_t h{42};
//    const string prev_id{"prev_miner_id"};
//    const string prev_id_sig{"prev_miner_id_sig"};
//    const string id{"miner_id"};
//    auto x = uint256S("123456789abcdef0"
//                      "123456789abcdef0"
//                      "123456789abcdef0"
//                      "123456789abcdef0");
//    const COutPoint op{x, 0};
//    const optional<UniValue> contact;
//
//    CoinbaseDocument input{v, h, prev_id, prev_id_sig, id, op, contact};
//
//    vector<CoinbaseDocument::DataRef> dataRefs = {
//        CoinbaseDocument::DataRef{{"id1", "id2"}, x, 0},
//        CoinbaseDocument::DataRef{{"id1", "id2"}, x, 0}};
//    input.SetDataRefs(dataRefs);
//
//    const auto json{to_json(input)};
//    const auto output{from_json_dynamic(json)};
//    BOOST_CHECK_EQUAL(output, input);
//}

BOOST_AUTO_TEST_SUITE_END()
