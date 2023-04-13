// Copyright (c) 2015 The Bitcoin Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chain.h"
#include "chainparams.h"
#include "config.h"
#include "pow.h"
#include "random.h"
#include "test/test_bitcoin.h"
#include "util.h"
#include "block_index_store.h"

#include <boost/test/unit_test.hpp>

#include <map>

namespace{ class pow_tests_uid; } // only used as unique identifier

template <>
struct CBlockIndex::UnitTestAccess<pow_tests_uid>
{
    UnitTestAccess() = delete;

    static void SetHeight( CBlockIndex& index, int32_t height)
    {
        index.nHeight = height;
    }
};
using TestAccessCBlockIndex = CBlockIndex::UnitTestAccess<pow_tests_uid>;

BOOST_FIXTURE_TEST_SUITE(pow_tests, BasicTestingSetup)

/* Test calculation of next difficulty target with no constraints applying */
BOOST_AUTO_TEST_CASE(get_next_work) {
    CBlockHeader header;
    header.nTime = 1262152739; // Block #32255
    header.nBits = 0x1d00ffff;

    CBlockIndex::TemporaryBlockIndex pindexLast{ header };
    DummyConfig config(CBaseChainParams::MAIN);

    int64_t nLastRetargetTime = 1261130161; // Block #30240
    TestAccessCBlockIndex::SetHeight( pindexLast, 32255 );
    BOOST_CHECK_EQUAL(
        CalculateNextWorkRequired(pindexLast, nLastRetargetTime, config),
        0x1d00d86aU);
}

/* Test the constraint on the upper bound for next work */
BOOST_AUTO_TEST_CASE(get_next_work_pow_limit) {
    CBlockHeader header;
    header.nTime = 1233061996; // Block #2015
    header.nBits = 0x1d00ffff;

    CBlockIndex::TemporaryBlockIndex pindexLast{ header };
    DummyConfig config(CBaseChainParams::MAIN);

    int64_t nLastRetargetTime = 1231006505; // Block #0
    TestAccessCBlockIndex::SetHeight( pindexLast, 2015 );
    BOOST_CHECK_EQUAL(
        CalculateNextWorkRequired(pindexLast, nLastRetargetTime, config),
        0x1d00ffffU);
}

/* Test the constraint on the lower bound for actual time taken */
BOOST_AUTO_TEST_CASE(get_next_work_lower_limit_actual) {
    CBlockHeader header;
    header.nTime = 1279297671; // Block #68543
    header.nBits = 0x1c05a3f4;

    CBlockIndex::TemporaryBlockIndex pindexLast{ header };
    DummyConfig config(CBaseChainParams::MAIN);

    int64_t nLastRetargetTime = 1279008237; // Block #66528
    TestAccessCBlockIndex::SetHeight( pindexLast, 68543 );
    BOOST_CHECK_EQUAL(
        CalculateNextWorkRequired(pindexLast, nLastRetargetTime, config),
        0x1c0168fdU);
}

/* Test the constraint on the upper bound for actual time taken */
BOOST_AUTO_TEST_CASE(get_next_work_upper_limit_actual) {
    CBlockHeader header;
    header.nTime = 1269211443; // Block #46367
    header.nBits = 0x1c387f6f;

    CBlockIndex::TemporaryBlockIndex pindexLast{ header };
    DummyConfig config(CBaseChainParams::MAIN);

    int64_t nLastRetargetTime = 1263163443; // NOTE: Not an actual block time
    TestAccessCBlockIndex::SetHeight( pindexLast, 46367 );
    BOOST_CHECK_EQUAL(
        CalculateNextWorkRequired(pindexLast, nLastRetargetTime, config),
        0x1d00e1fdU);
}

BOOST_AUTO_TEST_CASE(GetBlockProofEquivalentTime_test) {
    DummyConfig config(CBaseChainParams::MAIN);
    BlockIndexStore blockIndexStore;
    CChain blocks;

    uint256 prev;

    for (int i = 0; i < 10000; i++) {
        CBlockHeader header;
        header.nTime =
            1269211443 +
            i * config.GetChainParams().GetConsensus().nPowTargetSpacing;
        header.nBits = 0x207fffff; /* target 0x7fffff000... */
        header.hashPrevBlock = prev;

        blocks.SetTip( blockIndexStore.Insert( header ) );

        prev = blocks.Tip()->GetBlockHash();
    }

    for (int j = 0; j < 1000; j++) {
        CBlockIndex *p1 = blocks[InsecureRandRange(10000)];
        CBlockIndex *p2 = blocks[InsecureRandRange(10000)];
        CBlockIndex *p3 = blocks[InsecureRandRange(10000)];

        int64_t tdiff = GetBlockProofEquivalentTime(
            *p1, *p2, *p3, config.GetChainParams().GetConsensus());
        BOOST_CHECK_EQUAL(tdiff, p1->GetBlockTime() - p2->GetBlockTime());
    }
}

static CBlockIndex* GetBlockIndex(
    CBlockIndex* pindexPrev,
    int64_t nTimeInterval,
    uint32_t nBits,
    BlockIndexStore& blockIndexStore)
{
    CBlockHeader header;
    header.nTime = pindexPrev->GetBlockTime() + nTimeInterval;
    header.nBits = nBits;
    header.nNonce = blockIndexStore.Count();
    header.hashPrevBlock = pindexPrev->GetBlockHash();

    return blockIndexStore.Insert( header );
}

BOOST_AUTO_TEST_CASE(retargeting_test) {
    DummyConfig config(CBaseChainParams::MAIN);
    BlockIndexStore blockIndexStore;
    CChain blocks;

    const Consensus::Params &params = config.GetChainParams().GetConsensus();
    const arith_uint256 powLimit = UintToArith256(params.powLimit);
    arith_uint256 currentPow = powLimit >> 1;
    uint32_t initialBits = currentPow.GetCompact();

    // Genesis block.
    {
        CBlockHeader header;
        header.nTime = 1269211443;
        header.nBits = initialBits;
        blocks.SetTip( blockIndexStore.Insert( header ) );
    }

    // Pile up some blocks.
    for (size_t i = 1; i < 100; i++) {
        blocks.SetTip(
            GetBlockIndex(
                blocks.Tip(),
                params.nPowTargetSpacing,
                initialBits,
                blockIndexStore) );
    }

    CBlockHeader blkHeaderDummy;

    // We start getting 2h blocks time. For the first 5 blocks, it doesn't
    // matter as the MTP is not affected. For the next 5 block, MTP difference
    // increases but stays below 12h.
    for (size_t i = 100; i < 110; i++) {
        blocks.SetTip(
            GetBlockIndex(
                blocks.Tip(),
                2 * 3600,
                initialBits,
                blockIndexStore) );
        BOOST_CHECK_EQUAL(
            GetNextWorkRequired(blocks.Tip(), &blkHeaderDummy, config),
            initialBits);
    }

    // Now we expect the difficulty to decrease.
    blocks.SetTip(
        GetBlockIndex(
            blocks.Tip(),
            2 * 3600,
            initialBits,
            blockIndexStore) );
    currentPow.SetCompact(currentPow.GetCompact());
    currentPow += (currentPow >> 2);
    BOOST_CHECK_EQUAL(
        GetNextWorkRequired(blocks.Tip(), &blkHeaderDummy, config),
        currentPow.GetCompact());

    // As we continue with 2h blocks, difficulty continue to decrease.
    blocks.SetTip(
        GetBlockIndex(
            blocks.Tip(),
            2 * 3600,
            currentPow.GetCompact(),
            blockIndexStore) );
    currentPow.SetCompact(currentPow.GetCompact());
    currentPow += (currentPow >> 2);
    BOOST_CHECK_EQUAL(
        GetNextWorkRequired(blocks.Tip(), &blkHeaderDummy, config),
        currentPow.GetCompact());

    // We decrease again.
    blocks.SetTip(
        GetBlockIndex(
            blocks.Tip(),
            2 * 3600,
            currentPow.GetCompact(),
            blockIndexStore) );
    currentPow.SetCompact(currentPow.GetCompact());
    currentPow += (currentPow >> 2);
    BOOST_CHECK_EQUAL(
        GetNextWorkRequired(blocks.Tip(), &blkHeaderDummy, config),
        currentPow.GetCompact());

    // We check that we do not go below the minimal difficulty.
    blocks.SetTip(
        GetBlockIndex(
            blocks.Tip(),
            2 * 3600,
            currentPow.GetCompact(),
            blockIndexStore) );
    currentPow.SetCompact(currentPow.GetCompact());
    currentPow += (currentPow >> 2);
    BOOST_CHECK(powLimit.GetCompact() != currentPow.GetCompact());
    BOOST_CHECK_EQUAL(
        GetNextWorkRequired(blocks.Tip(), &blkHeaderDummy, config),
        powLimit.GetCompact());

    // Once we reached the minimal difficulty, we stick with it.
    blocks.SetTip(
        GetBlockIndex(
            blocks.Tip(),
            2 * 3600,
            currentPow.GetCompact(),
            blockIndexStore) );
    BOOST_CHECK(powLimit.GetCompact() != currentPow.GetCompact());
    BOOST_CHECK_EQUAL(
        GetNextWorkRequired(blocks.Tip(), &blkHeaderDummy, config),
        powLimit.GetCompact());
}

BOOST_AUTO_TEST_CASE(cash_difficulty_test) {
    DummyConfig config(CBaseChainParams::MAIN);
    BlockIndexStore blockIndexStore;
    CChain blocks;

    const Consensus::Params &params = config.GetChainParams().GetConsensus();
    const arith_uint256 powLimit = UintToArith256(params.powLimit);
    uint32_t powLimitBits = powLimit.GetCompact();
    arith_uint256 currentPow = powLimit >> 4;
    uint32_t initialBits = currentPow.GetCompact();

    // Genesis block.
    {
        CBlockHeader header;
        header.nTime = 1269211443;
        header.nBits = initialBits;
        blocks.SetTip( blockIndexStore.Insert( header ) );
    }

    // Pile up some blocks every 10 mins to establish some history.
    for (size_t i = 1; i < 2050; ++i)
    {
        blocks.SetTip(
            GetBlockIndex(
                blocks.Tip(),
                600,
                initialBits,
                blockIndexStore) );
    }

    CBlockHeader blkHeaderDummy;
    uint32_t nBits =
        GetNextCashWorkRequired(blocks[2049], &blkHeaderDummy, config);

    // Difficulty stays the same as long as we produce a block every 10 mins.
    for (size_t j = 0; j < 10; ++j)
    {
        blocks.SetTip(
            GetBlockIndex(
                blocks.Tip(),
                600,
                nBits,
                blockIndexStore) );
        BOOST_CHECK_EQUAL(
            GetNextCashWorkRequired(blocks.Tip(), &blkHeaderDummy, config),
            nBits);
    }

    // Make sure we skip over blocks that are out of wack. To do so, we produce
    // a block that is far in the future, and then produce a block with the
    // expected timestamp.
    blocks.SetTip(
        GetBlockIndex(
            blocks.Tip(),
            6000,
            nBits,
            blockIndexStore) );
    BOOST_CHECK_EQUAL(
        GetNextCashWorkRequired(blocks.Tip(), &blkHeaderDummy, config), nBits);
    blocks.SetTip(
        GetBlockIndex(
            blocks.Tip(),
            2 * 600 - 6000,
            nBits,
            blockIndexStore) );
    BOOST_CHECK_EQUAL(
        GetNextCashWorkRequired(blocks.Tip(), &blkHeaderDummy, config), nBits);

    // The system should continue unaffected by the block with a bogous
    // timestamps.
    for (size_t j = 0; j < 20; ++j)
    {
        blocks.SetTip(
            GetBlockIndex(
                blocks.Tip(),
                600,
                nBits,
                blockIndexStore) );
        BOOST_CHECK_EQUAL(
            GetNextCashWorkRequired(blocks.Tip(), &blkHeaderDummy, config),
            nBits);
    }

    // We start emitting blocks slightly faster. The first block has no impact.
    blocks.SetTip(
        GetBlockIndex(
            blocks.Tip(),
            550,
            nBits,
            blockIndexStore) );
    BOOST_CHECK_EQUAL(
        GetNextCashWorkRequired(blocks.Tip(), &blkHeaderDummy, config), nBits);

    // Now we should see difficulty increase slowly.
    for (size_t j = 0; j < 10; ++j)
    {
        blocks.SetTip(
            GetBlockIndex(
                blocks.Tip(),
                550,
                nBits,
                blockIndexStore) );
        const uint32_t nextBits =
            GetNextCashWorkRequired(blocks.Tip(), &blkHeaderDummy, config);

        arith_uint256 currentTarget;
        currentTarget.SetCompact(nBits);
        arith_uint256 nextTarget;
        nextTarget.SetCompact(nextBits);

        // Make sure that difficulty increases very slowly.
        BOOST_CHECK(nextTarget < currentTarget);
        BOOST_CHECK((currentTarget - nextTarget) < (currentTarget >> 10));

        nBits = nextBits;
    }

    // Check the actual value.
    BOOST_CHECK_EQUAL(nBits, 0x1c0fe7b1U);

    // If we dramatically shorten block production, difficulty increases faster.
    for (size_t j = 0; j < 20; ++j)
    {
        blocks.SetTip(
            GetBlockIndex(
                blocks.Tip(),
                10,
                nBits,
                blockIndexStore) );
        const uint32_t nextBits =
            GetNextCashWorkRequired(blocks.Tip(), &blkHeaderDummy, config);

        arith_uint256 currentTarget;
        currentTarget.SetCompact(nBits);
        arith_uint256 nextTarget;
        nextTarget.SetCompact(nextBits);

        // Make sure that difficulty increases faster.
        BOOST_CHECK(nextTarget < currentTarget);
        BOOST_CHECK((currentTarget - nextTarget) < (currentTarget >> 4));

        nBits = nextBits;
    }

    // Check the actual value.
    BOOST_CHECK_EQUAL(nBits, 0x1c0db19fU);

    // We start to emit blocks significantly slower. The first block has no
    // impact.
    blocks.SetTip(
        GetBlockIndex(
            blocks.Tip(),
            6000,
            nBits,
            blockIndexStore) );
    nBits = GetNextCashWorkRequired(blocks.Tip(), &blkHeaderDummy, config);

    // Check the actual value.
    BOOST_CHECK_EQUAL(nBits, 0x1c0d9222U);

    // If we dramatically slow down block production, difficulty decreases.
    for (size_t j = 0; j < 93; ++j)
    {
        blocks.SetTip(
            GetBlockIndex(
                blocks.Tip(),
                6000,
                nBits,
                blockIndexStore) );
        const uint32_t nextBits =
            GetNextCashWorkRequired(blocks.Tip(), &blkHeaderDummy, config);

        arith_uint256 currentTarget;
        currentTarget.SetCompact(nBits);
        arith_uint256 nextTarget;
        nextTarget.SetCompact(nextBits);

        // Check the difficulty decreases.
        BOOST_CHECK(nextTarget <= powLimit);
        BOOST_CHECK(nextTarget > currentTarget);
        BOOST_CHECK((nextTarget - currentTarget) < (currentTarget >> 3));

        nBits = nextBits;
    }

    // Check the actual value.
    BOOST_CHECK_EQUAL(nBits, 0x1c2f13b9U);

    // Due to the window of time being bounded, next block's difficulty actually
    // gets harder.
    blocks.SetTip(
        GetBlockIndex(
            blocks.Tip(),
            6000,
            nBits,
            blockIndexStore) );
    nBits = GetNextCashWorkRequired(blocks.Tip(), &blkHeaderDummy, config);
    BOOST_CHECK_EQUAL(nBits, 0x1c2ee9bfU);

    // And goes down again. It takes a while due to the window being bounded and
    // the skewed block causes 2 blocks to get out of the window.
    for (size_t j = 0; j < 192; ++j)
    {
        blocks.SetTip(
            GetBlockIndex(
                blocks.Tip(),
                6000,
                nBits,
                blockIndexStore) );
        const uint32_t nextBits =
            GetNextCashWorkRequired(blocks.Tip(), &blkHeaderDummy, config);

        arith_uint256 currentTarget;
        currentTarget.SetCompact(nBits);
        arith_uint256 nextTarget;
        nextTarget.SetCompact(nextBits);

        // Check the difficulty decreases.
        BOOST_CHECK(nextTarget <= powLimit);
        BOOST_CHECK(nextTarget > currentTarget);
        BOOST_CHECK((nextTarget - currentTarget) < (currentTarget >> 3));

        nBits = nextBits;
    }

    // Check the actual value.
    BOOST_CHECK_EQUAL(nBits, 0x1d00ffffU);

    // Once the difficulty reached the minimum allowed level, it doesn't get any
    // easier.
    for (size_t j = 0; j < 5; ++j)
    {
        blocks.SetTip(
            GetBlockIndex(
                blocks.Tip(),
                6000,
                nBits,
                blockIndexStore) );
        const uint32_t nextBits =
            GetNextCashWorkRequired(blocks.Tip(), &blkHeaderDummy, config);

        // Check the difficulty stays constant.
        BOOST_CHECK_EQUAL(nextBits, powLimitBits);
        nBits = nextBits;
    }
}

BOOST_AUTO_TEST_SUITE_END()
