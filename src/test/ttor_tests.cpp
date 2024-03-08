// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "block_index_store.h"
#include "chainparams.h"
#include "chain.h"
#include "config.h"
#include "consensus/consensus.h"
#include "pow.h"
#include "primitives/transaction.h"
#include "test/test_bitcoin.h"
#include "uint256.h"
#include "util.h"
#include "validation.h"

#include <cstdint>
#include <cstdio>
#include <vector>

#include <boost/test/unit_test.hpp>

namespace{ class Unique; }

template <>
struct CBlockIndex::UnitTestAccess<class Unique>
{
    UnitTestAccess() = delete;

    static void SetStatusWithValidity(CBlockIndex& blockIndex)
    {
        blockIndex.nStatus = blockIndex.nStatus.withValidity(BlockValidity::SCRIPTS);
    }
};
using TestAccessCBlockIndex = CBlockIndex::UnitTestAccess<class Unique>;

CBlockIndex& AddToBlockIndex(CBlockIndex& prev)
{
    CBlockHeader header;
    header.nTime = mapBlockIndex.Count(); // for uniqueness in current block hash
    header.hashPrevBlock = prev.GetBlockHash();
    header.nBits = GetNextWorkRequired( &prev, &header, GlobalConfig::GetConfig() );
    CBlockIndex& current = *mapBlockIndex.Insert( header );
    TestAccessCBlockIndex::SetStatusWithValidity( current );

    return current;
}

BOOST_FIXTURE_TEST_SUITE(ttor_tests, TestingSetup)

/**
 *
 * We test InvalidateChain function which invalidates all chains containing given block.
 * Function sets status of descendent blocks to "with failed parent".
 *
 * We generate following block situation and invalidate block 6.
 * All descendants of block 6 should be invalid.
 *
 *     Genesis
 *     |
 *     0----
 *     | \  \
 *     1  4  6
 *     |  |  |
 *     2  5  7----
 *     |     |  | \
 *     3     8  9  11
 *              |  |
 *              10 12
 *                 |
 *                 13
 */
BOOST_AUTO_TEST_CASE(invalidate_chain)
{
    // Due to static assertion checking (in debug mode), It is required to explicitly lock cs_main.
    // The checks are done (explicitly and implicitly) through functions:
    // - IsValid
    // - InvalidateChain
    LOCK(cs_main);
    std::vector<std::reference_wrapper<CBlockIndex>> blocks;

    assert( chainActive.Genesis() );
    assert( mapBlockIndex.Get( chainActive.Genesis()->GetBlockHash() ) );

    // valid active chain
    blocks.push_back( AddToBlockIndex( *chainActive.Genesis() ) );
    blocks.push_back( AddToBlockIndex( blocks.back() ) );
    blocks.push_back( AddToBlockIndex( blocks.back() ) );
    blocks.push_back( AddToBlockIndex( blocks.back() ) );

    //valid non-active chain
    blocks.push_back( AddToBlockIndex( blocks[0] ) );
    blocks.push_back( AddToBlockIndex( blocks.back() ) );

    //invalid chain with some forks
    blocks.push_back( AddToBlockIndex( blocks[0] ) );
    blocks.push_back( AddToBlockIndex( blocks.back() ) );
    blocks.push_back( AddToBlockIndex( blocks.back() ) );

    blocks.push_back( AddToBlockIndex( blocks[7] ) );
    blocks.push_back( AddToBlockIndex( blocks.back() ) );

    blocks.push_back( AddToBlockIndex( blocks[7] ) );
    blocks.push_back( AddToBlockIndex( blocks.back() ) );
    blocks.push_back( AddToBlockIndex( blocks.back() ) );

    // set current active chain tip
    chainActive.SetTip( &blocks[3].get() );

    // start with all valid blocks
    for (size_t i = 0; i < blocks.size(); ++i)
    {
        BOOST_CHECK(blocks[i].get().IsValid(BlockValidity::TREE) == true);
    }

    // invalidate block 6 and its chain
    blocks[6].get().ModifyStatusWithFailed(mapBlockIndex);
    InvalidateChain(testConfig, &blocks[6].get() );

    // block 6 should remain invalid but not with failed parent
    BOOST_CHECK(blocks[6].get().getStatus().hasFailed() == true);
    BOOST_CHECK(blocks[6].get().getStatus().hasFailedParent() == false);

    // all blocks in forks from invalid block should have failed parent status
    for (size_t i = 7; i < blocks.size(); ++i)
    {
        BOOST_CHECK(blocks[i].get().getStatus().hasFailedParent() == true);
    }

    // all blocks in active chain should be valid
    for (size_t i = 0; i < 6; ++i)
    {
        BOOST_CHECK(blocks[i].get().IsValid(BlockValidity::TREE) == true);
    }
}

/**
 * Checking that CheckBlockTTOROrder detects the violation of Topological Transaction Ordering Rule (TTOR).
 * CheckBlockTTOROrder function returns false if transactions in block are not in topological order.
 */
BOOST_AUTO_TEST_CASE(check_ttor) {

    CBlock block;
    block.vtx.reserve(2);

    CMutableTransaction mtx1 = CMutableTransaction();
    mtx1.vout.resize(1);
    mtx1.vin.resize(1);
    mtx1.vout[0].scriptPubKey = CScript() << OP_TRUE;
    mtx1.vin[0].prevout = COutPoint(InsecureRand256(), 0);

    // transaction tx2 uses output from tx1
    CMutableTransaction mtx2 = CMutableTransaction();
    mtx2.vin.resize(1);
    mtx2.vin[0].prevout = COutPoint(mtx1.GetHash(), 0);

    CTransaction tx1 = CTransaction(mtx1);
    CTransaction tx2 = CTransaction(mtx2);

    block.vtx.push_back(MakeTransactionRef(tx1));
    block.vtx.push_back(MakeTransactionRef(tx2));

    BOOST_CHECK_EQUAL(CheckBlockTTOROrder(block), true);

    // switching transactions will violate TTOR
    block.vtx[0] = MakeTransactionRef(tx2);
    block.vtx[1] = MakeTransactionRef(tx1);

    BOOST_CHECK_EQUAL(CheckBlockTTOROrder(block), false);

}

/**
 * Checking TTOR, but with more complex cases:
 * - spending transactions from previous blocks
 * - using transactions with multiple inputs
 */
BOOST_AUTO_TEST_CASE(check_ttor_advanced) {

    CBlock block0;
    block0.vtx.reserve(3);

    CMutableTransaction mtx0 = CMutableTransaction();
    mtx0.vout.resize(1);
    mtx0.vin.resize(1);
    mtx0.vout[0].scriptPubKey = CScript() << OP_TRUE;
    mtx0.vin[0].prevout = COutPoint(InsecureRand256(), 0);

    CMutableTransaction mtx1 = CMutableTransaction();
    mtx1.vout.resize(2);
    mtx1.vin.resize(1);
    mtx1.vout[0].scriptPubKey = CScript() << OP_TRUE;
    mtx1.vout[1].scriptPubKey = CScript() << OP_TRUE;
    mtx1.vin[0].prevout = COutPoint(mtx0.GetHash(), 0);

    CMutableTransaction mtx2 = CMutableTransaction();
    mtx2.vout.resize(1);
    mtx2.vin.resize(1);
    mtx2.vout[0].scriptPubKey = CScript() << OP_TRUE;
    mtx2.vin[0].prevout = COutPoint(mtx1.GetHash(), 0);

    CTransaction tx0 = CTransaction(mtx0);
    CTransaction tx1 = CTransaction(mtx1);
    CTransaction tx2 = CTransaction(mtx2);

    block0.vtx.push_back(MakeTransactionRef(tx0));
    block0.vtx.push_back(MakeTransactionRef(tx1));
    block0.vtx.push_back(MakeTransactionRef(tx2));

    BOOST_CHECK_EQUAL(CheckBlockTTOROrder(block0), true);

    CBlock block1;
    block1.vtx.reserve(2);

    CMutableTransaction mtx3 = CMutableTransaction();
    mtx3.vout.resize(1);
    mtx3.vin.resize(1);
    mtx3.vout[0].scriptPubKey = CScript() << OP_TRUE;
    mtx3.vin[0].prevout = COutPoint(mtx2.GetHash(), 0);

    CMutableTransaction mtx4 = CMutableTransaction();
    mtx4.vout.resize(1);
    mtx4.vin.resize(2);
    mtx4.vout[0].scriptPubKey = CScript() << OP_TRUE;
    mtx4.vin[0].prevout = COutPoint(mtx3.GetHash(), 0);
    mtx4.vin[1].prevout = COutPoint(mtx1.GetHash(), 1);

    CTransaction tx3 = CTransaction(mtx3);
    CTransaction tx4 = CTransaction(mtx4);

    block1.vtx.push_back(MakeTransactionRef(tx3));
    block1.vtx.push_back(MakeTransactionRef(tx4));

    BOOST_CHECK_EQUAL(CheckBlockTTOROrder(block1), true);

    // switching transactions will violate TTOR
    block1.vtx[0] = MakeTransactionRef(tx4);
    block1.vtx[1] = MakeTransactionRef(tx3);

    BOOST_CHECK_EQUAL(CheckBlockTTOROrder(block1), false);
}

BOOST_AUTO_TEST_SUITE_END()
