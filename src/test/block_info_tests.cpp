// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#include <boost/test/unit_test.hpp>

#include <map>
#include <vector>

#include "block_index_store.h"
#include "chain.h"
#include "chainparams.h"
#include "config.h"
#include "pow.h"

using namespace std;

BOOST_AUTO_TEST_SUITE(block_index_tests)

BOOST_AUTO_TEST_CASE(mtp)
{
    SelectParams( CBaseChainParams::REGTEST );

    BlockIndexStore blockIndexStore;
    CChain blocks;
    CBlockIndex* prev{};
    uint256 prevHash;
    for(vector<CBlockIndex>::size_type i{}; i < 14; ++i)
    {
        CBlockHeader header;
        header.nTime = i;
        header.hashPrevBlock = prevHash;
        header.nBits = GetNextWorkRequired( prev, &header, GlobalConfig::GetConfig() );

        prev = blockIndexStore.Insert( header );
        blocks.SetTip( prev );
        prevHash = prev->GetBlockHash();
    }

    BOOST_CHECK_EQUAL(0, blocks[0]->GetMedianTimePast());
    BOOST_CHECK_EQUAL(1, blocks[1]->GetMedianTimePast());
    BOOST_CHECK_EQUAL(1, blocks[2]->GetMedianTimePast());
    BOOST_CHECK_EQUAL(2, blocks[3]->GetMedianTimePast());
    BOOST_CHECK_EQUAL(2, blocks[4]->GetMedianTimePast());
    BOOST_CHECK_EQUAL(3, blocks[5]->GetMedianTimePast());
    BOOST_CHECK_EQUAL(3, blocks[6]->GetMedianTimePast());
    BOOST_CHECK_EQUAL(4, blocks[7]->GetMedianTimePast());
    BOOST_CHECK_EQUAL(4, blocks[8]->GetMedianTimePast());
    BOOST_CHECK_EQUAL(5, blocks[9]->GetMedianTimePast());
    BOOST_CHECK_EQUAL(5, blocks[10]->GetMedianTimePast());
    BOOST_CHECK_EQUAL(6, blocks[11]->GetMedianTimePast());
    BOOST_CHECK_EQUAL(7, blocks[12]->GetMedianTimePast());
    BOOST_CHECK_EQUAL(8, blocks[13]->GetMedianTimePast());
}

BOOST_AUTO_TEST_SUITE_END()
