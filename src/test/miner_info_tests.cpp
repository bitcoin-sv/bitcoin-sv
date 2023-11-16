// Copyright (c) 2022 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#include <numeric>
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>
#include <boost/algorithm/hex.hpp>

#include "miner_id/miner_info.h"
#include "miner_id/miner_info_ref.h"
#include "primitives/block.h"
#include "test/test_bitcoin.h"

#include "script/script.h"

#include "random.h"
#include "key.h"

using namespace std;
    
namespace ba = boost::algorithm;

BOOST_AUTO_TEST_SUITE(miner_info_tests)

BOOST_AUTO_TEST_CASE(block_bind_construction)
{
    const vector<uint8_t> mmr_pbh_hash(32, 2);
    const vector<uint8_t> sig(70, 4);

    block_bind bb{mmr_pbh_hash, sig};
    const uint256 expected_mm_root{mmr_pbh_hash};
    BOOST_CHECK_EQUAL(expected_mm_root, bb.mmr_pbh_hash());

    BOOST_CHECK_EQUAL_COLLECTIONS(sig.cbegin(),
                                  sig.cend(),
                                  bb.cbegin_sig(),
                                  bb.cend_sig());
}

BOOST_AUTO_TEST_CASE(block_bind_equality)
{
    const vector<uint8_t> mmr_pbh_hash(32, 1);
    const vector<uint8_t> prev_block_hash(32, 2);
    const vector<uint8_t> sig(70, 3);

    block_bind a{mmr_pbh_hash, sig};
    BOOST_CHECK_EQUAL(a, a);

    block_bind b{mmr_pbh_hash, sig};
    BOOST_CHECK_EQUAL(a, b);
    BOOST_CHECK_EQUAL(b, a);

    const vector<uint8_t> mmr_pbh_hash_2(32, 4);
    block_bind c{mmr_pbh_hash_2, sig};
    BOOST_CHECK_NE(a, c);
    BOOST_CHECK_NE(c, a);
    
    const vector<uint8_t> sig_2(70, 5);
    block_bind d{mmr_pbh_hash, sig_2};
    BOOST_CHECK_NE(a, d);
    BOOST_CHECK_NE(d, a);
}

BOOST_AUTO_TEST_CASE(is_hash_256_test)
{
    //BOOST_CHECK(is_hash_256(string{64, '0'}));
    //BOOST_CHECK(is_hash_256(string(64, '0')));
    //BOOST_CHECK(is_hash_256(string{64, '4'}));
    //BOOST_CHECK(is_hash_256(string(64, '4')));
    BOOST_CHECK(is_hash_256(string(64, '1')));
    BOOST_CHECK(is_hash_256(string(64, '2')));
    BOOST_CHECK(is_hash_256(string(64, '3')));
    BOOST_CHECK(is_hash_256(string(64, '4')));
    BOOST_CHECK(is_hash_256(string(64, '5')));
    BOOST_CHECK(is_hash_256(string(64, '6')));
    BOOST_CHECK(is_hash_256(string(64, '7')));
    BOOST_CHECK(is_hash_256(string(64, '8')));
    BOOST_CHECK(is_hash_256(string(64, '9')));
    BOOST_CHECK(is_hash_256(string(64, 'a')));
    BOOST_CHECK(is_hash_256(string(64, 'b')));
    BOOST_CHECK(is_hash_256(string(64, 'c')));
    BOOST_CHECK(is_hash_256(string(64, 'd')));
    BOOST_CHECK(is_hash_256(string(64, 'e')));
    BOOST_CHECK(is_hash_256(string(64, 'f')));
    BOOST_CHECK(is_hash_256(string(64, 'A')));
    BOOST_CHECK(is_hash_256(string(64, 'B')));
    BOOST_CHECK(is_hash_256(string(64, 'C')));
    BOOST_CHECK(is_hash_256(string(64, 'D')));
    BOOST_CHECK(is_hash_256(string(64, 'E')));
    BOOST_CHECK(is_hash_256(string(64, 'F')));

    BOOST_CHECK(!is_hash_256(string(63, '0')));
    BOOST_CHECK(!is_hash_256(string(65, '0')));
    
    BOOST_CHECK(!is_hash_256(string(64, 'g')));
}

BOOST_AUTO_TEST_CASE(is_compressed_key_test)
{
    const string prefix_2{"02"}; 
    BOOST_CHECK(is_compressed_key(prefix_2 + string(64, '0')));
    BOOST_CHECK(is_compressed_key(prefix_2 + string(64, '1')));
    BOOST_CHECK(is_compressed_key(prefix_2 + string(64, '2')));
    BOOST_CHECK(is_compressed_key(prefix_2 + string(64, '3')));
    BOOST_CHECK(is_compressed_key(prefix_2 + string(64, '4')));
    BOOST_CHECK(is_compressed_key(prefix_2 + string(64, '5')));
    BOOST_CHECK(is_compressed_key(prefix_2 + string(64, '6')));
    BOOST_CHECK(is_compressed_key(prefix_2 + string(64, '7')));
    BOOST_CHECK(is_compressed_key(prefix_2 + string(64, '8')));
    BOOST_CHECK(is_compressed_key(prefix_2 + string(64, '9')));
    BOOST_CHECK(is_compressed_key(prefix_2 + string(64, 'a')));
    BOOST_CHECK(is_compressed_key(prefix_2 + string(64, 'b')));
    BOOST_CHECK(is_compressed_key(prefix_2 + string(64, 'c')));
    BOOST_CHECK(is_compressed_key(prefix_2 + string(64, 'd')));
    BOOST_CHECK(is_compressed_key(prefix_2 + string(64, 'e')));
    BOOST_CHECK(is_compressed_key(prefix_2 + string(64, 'f')));
    BOOST_CHECK(is_compressed_key(prefix_2 + string(64, 'A')));
    BOOST_CHECK(is_compressed_key(prefix_2 + string(64, 'B')));
    BOOST_CHECK(is_compressed_key(prefix_2 + string(64, 'C')));
    BOOST_CHECK(is_compressed_key(prefix_2 + string(64, 'D')));
    BOOST_CHECK(is_compressed_key(prefix_2 + string(64, 'E')));
    BOOST_CHECK(is_compressed_key(prefix_2 + string(64, 'F')));

    BOOST_CHECK(!is_compressed_key(prefix_2 + string(63, '0')));
    BOOST_CHECK(!is_compressed_key(prefix_2 + string(65, '0')));
    
    BOOST_CHECK(!is_compressed_key(prefix_2 + string(64, 'g')));
   
    const string prefix_3{"03"}; 
    BOOST_CHECK(is_compressed_key(prefix_3 + string(64, '0')));
    BOOST_CHECK(is_compressed_key(prefix_3 + string(64, '1')));
    BOOST_CHECK(is_compressed_key(prefix_3 + string(64, '2')));
    BOOST_CHECK(is_compressed_key(prefix_3 + string(64, '3')));
    BOOST_CHECK(is_compressed_key(prefix_3 + string(64, '4')));
    BOOST_CHECK(is_compressed_key(prefix_3 + string(64, '5')));
    BOOST_CHECK(is_compressed_key(prefix_3 + string(64, '6')));
    BOOST_CHECK(is_compressed_key(prefix_3 + string(64, '7')));
    BOOST_CHECK(is_compressed_key(prefix_3 + string(64, '8')));
    BOOST_CHECK(is_compressed_key(prefix_3 + string(64, '9')));
    BOOST_CHECK(is_compressed_key(prefix_3 + string(64, 'a')));
    BOOST_CHECK(is_compressed_key(prefix_3 + string(64, 'b')));
    BOOST_CHECK(is_compressed_key(prefix_3 + string(64, 'c')));
    BOOST_CHECK(is_compressed_key(prefix_3 + string(64, 'd')));
    BOOST_CHECK(is_compressed_key(prefix_3 + string(64, 'e')));
    BOOST_CHECK(is_compressed_key(prefix_3 + string(64, 'f')));
    BOOST_CHECK(is_compressed_key(prefix_3 + string(64, 'A')));
    BOOST_CHECK(is_compressed_key(prefix_3 + string(64, 'B')));
    BOOST_CHECK(is_compressed_key(prefix_3 + string(64, 'C')));
    BOOST_CHECK(is_compressed_key(prefix_3 + string(64, 'D')));
    BOOST_CHECK(is_compressed_key(prefix_3 + string(64, 'E')));
    BOOST_CHECK(is_compressed_key(prefix_3 + string(64, 'F')));

    BOOST_CHECK(!is_compressed_key(prefix_3 + string(63, '0')));
    BOOST_CHECK(!is_compressed_key(prefix_3 + string(65, '0')));
    
    BOOST_CHECK(!is_compressed_key(prefix_3 + string(64, 'g')));
}

BOOST_AUTO_TEST_CASE(is_der_signature_test)
{
    const string preamble{"304002"};
    // check lengths
    BOOST_CHECK(!is_der_signature(preamble + string(120, '0')));
    BOOST_CHECK(!is_der_signature(preamble + string(121, '0')));
    BOOST_CHECK( is_der_signature(preamble + string(122, '0')));
    BOOST_CHECK(!is_der_signature(preamble + string(123, '0')));
    BOOST_CHECK( is_der_signature(preamble + string(124, '0')));
    BOOST_CHECK(!is_der_signature(preamble + string(125, '0')));
    BOOST_CHECK( is_der_signature(preamble + string(126, '0')));
    BOOST_CHECK(!is_der_signature(preamble + string(127, '0')));
    BOOST_CHECK( is_der_signature(preamble + string(128, '0')));
    BOOST_CHECK(!is_der_signature(preamble + string(129, '0')));
    BOOST_CHECK( is_der_signature(preamble + string(130, '0')));
    BOOST_CHECK(!is_der_signature(preamble + string(131, '0')));
    BOOST_CHECK( is_der_signature(preamble + string(132, '0')));
    BOOST_CHECK(!is_der_signature(preamble + string(133, '0')));
    BOOST_CHECK( is_der_signature(preamble + string(134, '0')));
    BOOST_CHECK(!is_der_signature(preamble + string(135, '0')));
    BOOST_CHECK( is_der_signature(preamble + string(136, '0')));
    BOOST_CHECK(!is_der_signature(preamble + string(137, '0')));
    BOOST_CHECK( is_der_signature(preamble + string(138, '0')));
    BOOST_CHECK(!is_der_signature(preamble + string(139, '0')));
    BOOST_CHECK(!is_der_signature(preamble + string(140, '0')));
    
    // check incorrect preamble
    BOOST_CHECK(!is_der_signature("204002" + string(132, '0')));
    BOOST_CHECK(!is_der_signature("404002" + string(132, '0')));
    BOOST_CHECK(!is_der_signature("314002" + string(132, '0')));
    BOOST_CHECK(!is_der_signature("303002" + string(132, '0')));
    BOOST_CHECK(!is_der_signature("305002" + string(132, '0')));
    BOOST_CHECK(!is_der_signature("304902" + string(132, '0')));
    BOOST_CHECK(!is_der_signature("304012" + string(132, '0')));
    BOOST_CHECK(!is_der_signature("304003" + string(132, '0')));

    // check accepts 0-9a-fA-F only
    BOOST_CHECK( is_der_signature(preamble + string(132, '1')));
    BOOST_CHECK( is_der_signature(preamble + string(132, '2')));
    BOOST_CHECK( is_der_signature(preamble + string(132, '3')));
    BOOST_CHECK( is_der_signature(preamble + string(132, '4')));
    BOOST_CHECK( is_der_signature(preamble + string(132, '5')));
    BOOST_CHECK( is_der_signature(preamble + string(132, '6')));
    BOOST_CHECK( is_der_signature(preamble + string(132, '7')));
    BOOST_CHECK( is_der_signature(preamble + string(132, '8')));
    BOOST_CHECK( is_der_signature(preamble + string(132, '9')));
    BOOST_CHECK( is_der_signature(preamble + string(132, 'a')));
    BOOST_CHECK( is_der_signature(preamble + string(132, 'b')));
    BOOST_CHECK( is_der_signature(preamble + string(132, 'c')));
    BOOST_CHECK( is_der_signature(preamble + string(132, 'd')));
    BOOST_CHECK( is_der_signature(preamble + string(132, 'e')));
    BOOST_CHECK( is_der_signature(preamble + string(132, 'f')));
    BOOST_CHECK( is_der_signature(preamble + string(132, 'A')));
    BOOST_CHECK( is_der_signature(preamble + string(132, 'B')));
    BOOST_CHECK( is_der_signature(preamble + string(132, 'C')));
    BOOST_CHECK( is_der_signature(preamble + string(132, 'D')));
    BOOST_CHECK( is_der_signature(preamble + string(132, 'E')));
    BOOST_CHECK( is_der_signature(preamble + string(132, 'F')));
    BOOST_CHECK(!is_der_signature(preamble + string(132, 'h')));
    BOOST_CHECK(!is_der_signature(preamble + string(132, 'H')));

    ResetGlobalRandomContext();
    CKey key {};
    std::vector<uint8_t> sig {};
    for(int i = 0; i < 1'000; ++i)
    {
        key.MakeNewKey(true);
        uint256 hash { InsecureRand256() };
        assert(key.Sign(hash, sig));
        BOOST_CHECK(is_der_signature(HexStr(sig)));
    }
}


BOOST_AUTO_TEST_CASE(parse_miner_info_empty_block)
{
    CBlock block;
    const auto s = ParseMinerInfo(block);
    BOOST_CHECK_EQUAL(miner_info_error::miner_info_ref_not_found,
                      get<miner_info_error>(s));
}

BOOST_AUTO_TEST_CASE(parse_miner_info_no_miner_info_ref_in_block)
{
    CBlock block;
    const auto s = ParseMinerInfo(block);
    BOOST_CHECK_EQUAL(miner_info_error::miner_info_ref_not_found,
                      get<miner_info_error>(s));
}

BOOST_AUTO_TEST_CASE(parse_miner_info_no_miner_info_ref_error)
{
    CBlock block;
    CMutableTransaction mtx;
    const vector<uint8_t> v{OP_FALSE, 0x6a, 0x4, 0x60, 0x1d, 0xfa, 0xce, 0x1, 0x1};

    CScript s;
    s.insert(s.begin(), v.begin(), v.end());
    CTxOut op;
    op.scriptPubKey = s;  
    mtx.vout.push_back(op);
    
    CTransaction tx{mtx};
    block.vtx.push_back(make_shared<const CTransaction>(tx));

    const auto var_mi_sig = ParseMinerInfo(block);
    BOOST_CHECK_EQUAL(miner_info_error::script_version_unsupported,
                      get<miner_info_error>(var_mi_sig));
}

BOOST_AUTO_TEST_CASE(parse_miner_info_no_miner_info_in_block)
{
    CMutableTransaction mtx;
    vector<uint8_t> v{OP_FALSE, 0x6a, 0x4, 0x60, 0x1d, 0xfa, 0xce, 0x1, 0x0};
    constexpr uint8_t hash_len{32};
    v.push_back(hash_len);
    fill_n(back_inserter(v), hash_len, 0x1); // txid
    v.push_back(hash_len);
    fill_n(back_inserter(v), hash_len, 0x2); // mmr_pbh_hash
    constexpr uint8_t sig_len{70};
    v.push_back(sig_len);
    fill_n(back_inserter(v), sig_len, 0x3); // sig

    CScript s;
    s.insert(s.begin(), v.begin(), v.end());
    CTxOut op;
    op.scriptPubKey = s;  
    mtx.vout.push_back(op);
    
    CBlock block;
    CTransaction tx{mtx};
    block.vtx.push_back(make_shared<const CTransaction>(tx));

    const auto var_mi_sig = ParseMinerInfo(block);
    BOOST_CHECK_EQUAL(miner_info_error::txid_not_found,
                      get<miner_info_error>(var_mi_sig));
}

BOOST_AUTO_TEST_CASE(parse_miner_info_invalid_miner_info_doc)
{
    constexpr uint8_t hash_len{32};
    constexpr uint8_t sig_len{70};
    const vector<uint8_t>
        preamble{OP_FALSE, 0x6a, 0x4, 0x60, 0x1d, 0xfa, 0xce, 0x1, 0x0};
    
    // miner_info_doc tx 
    CMutableTransaction mi_doc_mtx;
    vector<uint8_t> v_mi_doc{preamble};

    const string doc{R"({ })"};
    v_mi_doc.push_back(doc.size());
    v_mi_doc.insert(v_mi_doc.end(), doc.begin(), doc.end());

    v_mi_doc.push_back(sig_len);
    fill_n(back_inserter(v_mi_doc), sig_len, 0x2);

    CScript mi_doc_script;
    mi_doc_script.insert(mi_doc_script.end(), v_mi_doc.begin(), v_mi_doc.end());
    CTxOut mi_doc_op;
    mi_doc_op.scriptPubKey = mi_doc_script;
    mi_doc_mtx.vout.push_back(mi_doc_op);

    CTransaction mi_doc_tx{mi_doc_mtx};
    
    // miner_info_ref tx
    CMutableTransaction mi_ref_mtx;
    vector<uint8_t> v_mi_ref{preamble};
    v_mi_ref.push_back(hash_len);
    const auto txid{mi_doc_tx.GetId()};
    v_mi_ref.insert(v_mi_ref.end(), txid.begin(), txid.end()); 
    v_mi_ref.push_back(hash_len);
    fill_n(back_inserter(v_mi_ref), hash_len, 0x2); // mmr_pbh_hash
    v_mi_ref.push_back(sig_len);
    fill_n(back_inserter(v_mi_ref), sig_len, 0x3);

    CScript mi_ref_script;
    mi_ref_script.insert(mi_ref_script.end(), v_mi_ref.begin(), v_mi_ref.end());
    CTxOut mi_ref_op;
    mi_ref_op.scriptPubKey = mi_ref_script;
    mi_ref_mtx.vout.push_back(mi_ref_op);
    
    CTransaction mi_ref_tx{mi_ref_mtx};
    
    CBlock block;
    block.vtx.push_back(make_shared<const CTransaction>(mi_ref_tx));
    block.vtx.push_back(make_shared<const CTransaction>(mi_doc_tx));

    const auto var_mi_sig = ParseMinerInfo(block);
    BOOST_CHECK_EQUAL(miner_info_error::doc_parse_error_missing_fields,
                      get<miner_info_error>(var_mi_sig));
}

BOOST_AUTO_TEST_CASE(modified_merkle_root_test)
{
    CBlock block;

    CMutableTransaction coinbase_tx;
    coinbase_tx.vin.push_back(CTxIn{});
    coinbase_tx.vout.push_back(CTxOut{});

    const vector<uint8_t> v{OP_FALSE, 0x6a, 0x4, 0x60, 0x1d, 0xfa, 0xce, 0x1, 0x0};
    CScript mi_ref_script;
    mi_ref_script.insert(mi_ref_script.begin(), v.begin(), v.end());

    const vector<uint8_t> txid(32, 0x1);
    mi_ref_script.push_back(txid.size());
    mi_ref_script.insert(mi_ref_script.end(), txid.cbegin(), txid.cend());

    const vector<uint8_t> sig(70, 0x2);
    mi_ref_script.push_back(sig.size());
    mi_ref_script.insert(mi_ref_script.end(), sig.cbegin(), sig.cend());

    CTxOut op;
    op.scriptPubKey = mi_ref_script;  
    coinbase_tx.vout.push_back(op);
    block.vtx.push_back(make_shared<const CTransaction>(coinbase_tx)); 

    CMutableTransaction mtx;
    mtx.vin.push_back(CTxIn{});
    mtx.vout.push_back(CTxOut{});
    block.vtx.push_back(make_shared<const CTransaction>(mtx)); 

    const uint256 mm_root = modify_merkle_root(block);
    
    const string s{"1bb3aa8a509aa5a5d8bf32acb14c94d49dffb3da3dc9483c9cce5be4e9533b1c"};
    vector<uint8_t> buffer;
    buffer.reserve(32);
    ba::unhex(s.begin(), s.end(), back_inserter(buffer));

    const uint256 expected{buffer.cbegin(), buffer.cend()};
    BOOST_CHECK_EQUAL(expected, mm_root);
}

BOOST_AUTO_TEST_CASE(verify_sig_blockbind_mismatch)
{
    const vector<uint8_t> txid(32, 0x1);

    const vector<uint8_t> mmr_pbh_hash(32, 0x2); 

    const vector<uint8_t> sig(70, 0x3);
    const block_bind bb{mmr_pbh_hash, sig};

    const miner_info_ref mi_ref{txid, bb};
    
    constexpr int32_t block_height{1234};
    const string miner_id_key;
    const string miner_id_prev_key;
    const string miner_id_key_sig;
    const key_set& miner_id_ks{miner_id_key,
                               miner_id_prev_key,
                               miner_id_key_sig};
    const string rev_key;
    const string rev_prev_key;
    const string rev_key_sig;
    const key_set &revocation_ks{rev_key, rev_prev_key, rev_key_sig};

    vector<data_ref> data_refs;
    const miner_info_doc mi_doc(miner_info_doc::v0_3,
                                block_height,
                                miner_id_ks,
                                revocation_ks,
                                data_refs);
    
    CBlock block;
    CMutableTransaction coinbase_tx;
    coinbase_tx.vin.push_back(CTxIn{});
    coinbase_tx.vout.push_back(CTxOut{});
    coinbase_tx.vout.push_back(CTxOut{});
    block.vtx.push_back(make_shared<const CTransaction>(coinbase_tx)); 

    CMutableTransaction mtx;
    mtx.vin.push_back(CTxIn{});
    mtx.vout.push_back(CTxOut{});
    block.vtx.push_back(make_shared<const CTransaction>(mtx)); 

    const auto mi_err = verify(block, mi_ref.blockbind(), mi_doc.miner_id().key());
    BOOST_CHECK(mi_err);
    BOOST_CHECK_EQUAL(miner_info_error::block_bind_hash_mismatch, mi_err.value());
}

BOOST_AUTO_TEST_CASE(verify_sig_verification_fail)
{
    const vector<uint8_t> txid(32, 0x1);

    const string s{"d134cf2121d6556d6be0b697c77f819f2477da04a4c0bb65860d382fa2f5784a"};
    vector<uint8_t> mmr_pbh_hash;
    mmr_pbh_hash.reserve(32);
    ba::unhex(s.cbegin(), s.cend(), back_inserter(mmr_pbh_hash));

    const vector<uint8_t> sig(70, 0x3);
    const block_bind bb{mmr_pbh_hash, sig};

    const miner_info_ref mi_ref{txid, bb};
    
    constexpr int32_t block_height{1234};
    const string miner_id_key;
    const string miner_id_prev_key;
    const string miner_id_key_sig;
    const key_set& miner_id_ks{miner_id_key,
                               miner_id_prev_key,
                               miner_id_key_sig};
    const string rev_key;
    const string rev_prev_key;
    const string rev_key_sig;
    const key_set &revocation_ks{rev_key, rev_prev_key, rev_key_sig};

    const vector<data_ref> data_refs;
    const miner_info_doc mi_doc(miner_info_doc::v0_3,
                                block_height,
                                miner_id_ks,
                                revocation_ks,
                                data_refs);
    
    CBlock block;
    CMutableTransaction coinbase_tx;
    coinbase_tx.vin.push_back(CTxIn{});
    coinbase_tx.vout.push_back(CTxOut{});

    const vector<uint8_t> v{OP_FALSE, 0x6a, 0x4, 0x60, 0x1d, 0xfa, 0xce, 0x1, 0x1};
    CScript mi_ref_script;
    mi_ref_script.insert(mi_ref_script.begin(), v.begin(), v.end());

    mi_ref_script.push_back(txid.size());
    mi_ref_script.insert(mi_ref_script.end(), txid.cbegin(), txid.cend());

    mi_ref_script.push_back(sig.size());
    mi_ref_script.insert(mi_ref_script.end(), sig.cbegin(), sig.cend());

    CTxOut op;
    op.scriptPubKey = mi_ref_script;  
    coinbase_tx.vout.push_back(op);
    block.vtx.push_back(make_shared<const CTransaction>(coinbase_tx)); 

    CMutableTransaction mtx;
    mtx.vin.push_back(CTxIn{});
    mtx.vout.push_back(CTxOut{});
    block.vtx.push_back(make_shared<const CTransaction>(mtx)); 

    const auto mi_err = verify(block, mi_ref.blockbind(), mi_doc.miner_id().key());
    BOOST_CHECK(mi_err);
    BOOST_CHECK_EQUAL(miner_info_error::block_bind_sig_verification_failed, mi_err.value());
}

BOOST_AUTO_TEST_SUITE_END()

