// Copyright (c) 2022 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#include <numeric>
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "miner_id/miner_info.h"
#include "miner_id/miner_info_ref.h"

#include "script/script.h"

using namespace std;

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
    BOOST_CHECK(!is_der_signature(string(136, '0')));
    BOOST_CHECK(!is_der_signature(string(137, '0')));
    BOOST_CHECK(is_der_signature(string(138, '0')));
    BOOST_CHECK(!is_der_signature(string(139, '0')));
    BOOST_CHECK(is_der_signature(string(140, '0')));
    BOOST_CHECK(!is_der_signature(string(141, '0')));
    BOOST_CHECK(is_der_signature(string(142, '0')));
    BOOST_CHECK(!is_der_signature(string(143, '0')));
    BOOST_CHECK(is_der_signature(string(144, '0')));
    BOOST_CHECK(!is_der_signature(string(145, '0')));
    BOOST_CHECK(!is_der_signature(string(146, '0')));
    
    BOOST_CHECK(is_der_signature(string(142, '1')));
    BOOST_CHECK(is_der_signature(string(142, '2')));
    BOOST_CHECK(is_der_signature(string(142, '3')));
    BOOST_CHECK(is_der_signature(string(142, '4')));
    BOOST_CHECK(is_der_signature(string(142, '5')));
    BOOST_CHECK(is_der_signature(string(142, '6')));
    BOOST_CHECK(is_der_signature(string(142, '7')));
    BOOST_CHECK(is_der_signature(string(142, '8')));
    BOOST_CHECK(is_der_signature(string(142, '9')));
    BOOST_CHECK(is_der_signature(string(142, 'a')));
    BOOST_CHECK(is_der_signature(string(142, 'b')));
    BOOST_CHECK(is_der_signature(string(142, 'c')));
    BOOST_CHECK(is_der_signature(string(142, 'd')));
    BOOST_CHECK(is_der_signature(string(142, 'e')));
    BOOST_CHECK(is_der_signature(string(142, 'f')));
    BOOST_CHECK(is_der_signature(string(142, 'A')));
    BOOST_CHECK(is_der_signature(string(142, 'B')));
    BOOST_CHECK(is_der_signature(string(142, 'C')));
    BOOST_CHECK(is_der_signature(string(142, 'D')));
    BOOST_CHECK(is_der_signature(string(142, 'E')));
    BOOST_CHECK(is_der_signature(string(142, 'F')));


    using script = vector<uint8_t>;
    BOOST_CHECK(!is_der_signature(script{}));
    BOOST_CHECK(!is_der_signature(script{0x42}));

    BOOST_CHECK(!is_der_signature(script(68, 0x42)));
    BOOST_CHECK(is_der_signature(script(69, 0x42)));
    BOOST_CHECK(is_der_signature(script(70, 0x42)));
    BOOST_CHECK(is_der_signature(script(71, 0x42)));
    BOOST_CHECK(is_der_signature(script(72, 0x42)));
    BOOST_CHECK(!is_der_signature(script(73, 0x42)));
}

BOOST_AUTO_TEST_CASE(parse_miner_info_no_miner_info_in_block)
{
// todo
// CBlock block;
// const auto s = ParseMinerInfo(block);
}

BOOST_AUTO_TEST_SUITE_END()
