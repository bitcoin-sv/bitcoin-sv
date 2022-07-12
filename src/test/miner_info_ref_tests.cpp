// Copyright (c) 2022 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#include <numeric>
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>
#include <boost/algorithm/hex.hpp>

#include "miner_id/miner_info.h"
#include "miner_id/miner_info_error.h"
#include "miner_id/miner_info_ref.h"

#include "script/script.h"
#include "script/instruction_iterator.h"

using namespace std;

BOOST_AUTO_TEST_SUITE(miner_info_ref_tests)

BOOST_AUTO_TEST_CASE(miref_construction)
{
    const vector<uint8_t> txid(32, 1);
    const vector<uint8_t> mmr_pbh_hash(32, 2);
    const vector<uint8_t> sig(70, 4);
    const block_bind bb{mmr_pbh_hash, sig};
    const miner_info_ref mir{txid, bb};
    const uint256 expected_txid{txid.begin(), txid.end()}; 
    BOOST_CHECK_EQUAL(expected_txid, mir.txid());
    BOOST_CHECK_EQUAL(bb, mir.blockbind());
}

BOOST_AUTO_TEST_CASE(miref_equality)
{
    const vector<uint8_t> txid(32, 1);
    const vector<uint8_t> mmr_pbh_hash(32, 2);
    const vector<uint8_t> sig(70, 4);
    const block_bind bb{mmr_pbh_hash, sig};
    const miner_info_ref a{txid, bb};
    BOOST_CHECK_EQUAL(a, a);

    const miner_info_ref b{txid, bb};
    BOOST_CHECK_EQUAL(a, b);
    BOOST_CHECK_EQUAL(b, a);

    const vector<uint8_t> txid_2(32, 5);
    const miner_info_ref c{txid_2, bb};
    BOOST_CHECK_NE(a, c);
    BOOST_CHECK_NE(c, a);
    
    const vector<uint8_t> mmr_pbh_hash_2(32, 6);
    const block_bind bb_2{mmr_pbh_hash_2, sig};
    const miner_info_ref d{txid_2, bb_2};
    BOOST_CHECK_NE(a, d);
    BOOST_CHECK_NE(d, a);

    const vector<uint8_t> sig_2(70, 7);
    const block_bind bb_4{mmr_pbh_hash_2, sig_2};
    const miner_info_ref f{txid_2, bb_4};
    BOOST_CHECK_NE(a, f);
    BOOST_CHECK_NE(f, a);
}

BOOST_AUTO_TEST_CASE(parse_miner_id_ref_happy_case)
{
    // 0 OP_FALSE (1)
    // 1 OP_RETURN (1)
    // 2 pushdata 4 (1)
    // 3 protocol-id (4) 
    // 7 pushdata 1 (1)
    // 8 version (1)
    // 9 pushdata 32 (1)
    // 10 txid (32)
    // 41 pushdata 71-73 (1)
    // 42 sig(txid) (71-73)
    // Total = 114-116 elements

    uint8_t i{};
    vector<uint8_t> script{0x0, 0x6a, 0x4, 0x60, 0x1d, 0xfa, 0xce, 0x1, 0x0};
    
    constexpr uint8_t txid_len{32};
    script.push_back(txid_len);
    generate_n(back_inserter(script), txid_len, [&i](){ return i++; });

    constexpr uint8_t mmr_pbh_hash_len{32};
    script.push_back(mmr_pbh_hash_len);
    generate_n(back_inserter(script), mmr_pbh_hash_len, [&i](){ return i++; });
    
    constexpr uint8_t sig_len{70};
    script.push_back(sig_len);
    generate_n(back_inserter(script), sig_len, [](){ static uint8_t i{}; return i++; });

    const auto status = ParseMinerInfoRef(script);
    BOOST_CHECK(std::holds_alternative<miner_info_ref>(status));
    const auto miref = std::get<miner_info_ref>(status);
    const auto txid = miref.txid();

    vector<uint8_t> expected_txid(txid_len);
    std::iota(expected_txid.begin(), expected_txid.end(), 0);
    BOOST_CHECK_EQUAL_COLLECTIONS(expected_txid.begin(), expected_txid.end(),
                                  txid.begin(), txid.end());

    constexpr auto txid_first{10};
    constexpr auto mmr_pbh_hash_first{txid_first + txid_len + 1};
    constexpr auto hash_len{32};
    constexpr auto sig_first{mmr_pbh_hash_first + hash_len + 1};
    const block_bind expected_bb{bsv::span{&script[mmr_pbh_hash_first], hash_len},
                                 bsv::span{&script[sig_first], sig_len}};

    const miner_info_ref expected_mir{bsv::span{&script[txid_first], txid_len}, expected_bb};
    BOOST_CHECK_EQUAL(expected_mir, get<miner_info_ref>(status));
}

BOOST_AUTO_TEST_CASE(parse_miner_id_ref_failure_cases)
{
    // 0 OP_FALSE (1)
    // 1 OP_RETURN (1)
    // 2 pushdata 4 (1)
    // 3 protocol-id (4)
    // 7 pushdata 1 (1)
    // 8 version (1)
    // 9 miner-info-txid (32)
    // 41 hash(modified-merkle-root || prev-block-hash) (32)
    // 73 sig(hash(modified-merkle-root || prev-block-hash)) (69-72)
    // 142-145 end
    using mie = miner_info_error;

    constexpr uint8_t txid_len{32};
    constexpr uint8_t mmr_pbh_hash_len{32};
    constexpr uint8_t sig_len{70};
    vector<uint8_t> script{0x0, 0x6a, 0x4, 0x60, 0x1d, 0xfa, 0xce, 0x1, 0x0};

    // version, txid_len_offset, mmr_pbh_hash_len_offset, sig_len_offset, expected
    // result
    const vector<tuple<uint8_t, uint8_t, uint8_t, int8_t, mie>> v{
        make_tuple(1,  0,  0,   0, mie::script_version_unsupported),
        make_tuple(0, -1,  0,   0, mie::invalid_txid_len),
        make_tuple(0,  1,  0,   0, mie::invalid_txid_len),
        make_tuple(0,  0, -1,   0, mie::invalid_mmr_pbh_hash_len),
        make_tuple(0,  0,  1,   0, mie::invalid_mmr_pbh_hash_len),
        make_tuple(0,  0,  0,  -2, mie::invalid_sig_len),
        make_tuple(0,  0,  0,   3, mie::invalid_sig_len),
    };
    for(const auto& [version,
                     txid_len_offset,
                     mmr_pbh_hash_len_offset,
                     sig_len_offset,
                     expected] : v)
    {
        vector<uint8_t> script{0x0, 0x6a, 0x4, 0x60, 0x1d, 0xfa, 0xce};
        script.push_back(1);
        script.push_back(version);

        uint8_t i{};
        script.push_back(txid_len + txid_len_offset);
        generate_n(back_inserter(script), txid_len + txid_len_offset, [&i](){ return ++i; });

        script.push_back(mmr_pbh_hash_len + mmr_pbh_hash_len_offset);
        generate_n(back_inserter(script),
                   mmr_pbh_hash_len + mmr_pbh_hash_len_offset,
                   [&i]() { return ++i; });

        script.push_back(sig_len + sig_len_offset);
        generate_n(back_inserter(script),
                   sig_len + sig_len_offset,
                   [&i]() { return ++i; });

        const auto status = ParseMinerInfoRef(script); 
        BOOST_CHECK(std::holds_alternative<miner_info_error>(status));
        BOOST_CHECK_EQUAL(expected, get<miner_info_error>(status));
    }
}

//BOOST_AUTO_TEST_CASE(cjg)
//{
//    namespace ba = boost::algorithm;
//    /*
//    00
//    6a
//    04
//    601dface
//    01
//    00  version
//    20  pushdata 
//    5effcbdb13618e8f1f94b4fce52ffa1f439a98111d2c2d0452695cc0d95cdf58    txid
//    46  pushdata
//    30  sig
//    44
//    02
//    20
//    41a3d6016af1bad07bd09c741a37cad4bb8b7ad44b183278ebdb411774efea1a
//    02
//    20
//    7171912dfbca71001b43d890847f6dc18d2e498a0d22523193dcf30f2be64d9f
//    20
//    93791ffae371b948f418e28930807db655522ac3635dab9bb652ac61844086d8
//    46  pushdata
//    30  sig
//    44
//    02
//    20
//    7214b66034de85478518456a6eb93cf4af50180d34d48cb1db03ea2571d637ee
//    02
//    20
//    555d157b08a792b68ef1989891c938d15fe1c2549c6bb918bb2ee1b931810523
//    */
//    
//    const string s{
//        "006a04601dface0100205effcbdb13618e8f1f94b4fce52ffa1f439a98111d2c2d0452"
//        "695cc0d95cdf58463044022041a3d6016af1bad07bd09c741a37cad4bb8b7ad44b1832"
//        "78ebdb411774efea1a02207171912dfbca71001b43d890847f6dc18d2e498a0d225231"
//        "93dcf30f2be64d9f2093791ffae371b948f418e28930807db655522ac3635dab9bb652"
//        "ac61844086d846304402207214b66034de85478518456a6eb93cf4af50180d34d48cb1"
//        "db03ea2571d637ee0220555d157b08a792b68ef1989891c938d15fe1c2549c6bb918bb"
//        "2ee1b931810523"};
//    
//    vector<uint8_t> script;
//    ba::unhex(s.begin(), s.end(), back_inserter(script));
//
//    bsv::instruction_iterator it{script};
//    for(;;)
//    {
//        if(it.valid())
//            cout << *it << '\n';
//        else
//            break;
//        ++it;
//    }
//
//    const auto status = ParseMinerInfoRef(script); 
//    BOOST_CHECK(std::holds_alternative<miner_info_error>(status));
//    BOOST_CHECK_EQUAL(miner_info_error::size, get<miner_info_error>(status));
//}

BOOST_AUTO_TEST_SUITE_END()

