// Copyright (c) 2014-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chain.h"
#include "config.h"
#include "pow.h"
#include "test/test_bitcoin.h"
#include "util.h"
#include "block_index_store.h"

#include <map>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "sync.h"
extern CCriticalSection cs_main;

#define SKIPLIST_LENGTH 300000

BOOST_FIXTURE_TEST_SUITE(skiplist_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(skiplist_test) {
    BlockIndexStore blockIndexStore;
    CChain vIndex;

    // Genesis block.
    vIndex.SetTip(
        [&]
        {
            CBlockHeader header;
            header.nTime = GetTime();
            header.nBits =
                GetNextWorkRequired(
                    chainActive.Tip(),
                    &header,
                    GlobalConfig::GetConfig() );
            return blockIndexStore.Insert( header );
        }() );

    for (int i = 1; i < SKIPLIST_LENGTH; i++) {
        CBlockHeader header;
        header.hashPrevBlock = vIndex.Tip()->GetBlockHash();
        header.nBits = vIndex.Tip()->GetBits(); // leave same complexity as dummy bits
        vIndex.SetTip( blockIndexStore.Insert( header ) );
    }

    for (int i = 0; i < SKIPLIST_LENGTH; i++) {
        if (i > 0) {
            BOOST_CHECK(vIndex[i]->GetSkip() == vIndex[vIndex[i]->GetSkip()->GetHeight()]);
            BOOST_CHECK(vIndex[i]->GetSkip()->GetHeight() < i);
        } else {
            BOOST_CHECK(vIndex[i]->GetSkip() == nullptr);
        }
    }

    for (int i = 0; i < 1000; i++) {
        int from = InsecureRandRange(SKIPLIST_LENGTH - 1);
        int to = InsecureRandRange(from + 1);

        BOOST_CHECK(vIndex[SKIPLIST_LENGTH - 1]->GetAncestor(from) ==
                    vIndex[from]);
        BOOST_CHECK(vIndex[from]->GetAncestor(to) == vIndex[to]);
        BOOST_CHECK(vIndex[from]->GetAncestor(0) == vIndex[0]);
    }
}

BOOST_AUTO_TEST_CASE(getlocator_test) {
    BlockIndexStore blockIndexStore;
    CBlockIndex* lastIndex =
        [&]
        {
            CBlockHeader header;
            header.nTime = GetTime();
            header.nBits =
                GetNextWorkRequired(
                    nullptr,
                    &header,
                    GlobalConfig::GetConfig() );

            return blockIndexStore.Insert( header );
        }();

    BOOST_CHECK( lastIndex->IsGenesis() );
    BOOST_CHECK( lastIndex->GetPrev() == nullptr );

    CBlockIndex* splitLastIndex{};

    // Build a main chain 100000 blocks long.
    {
        for (int i = 1; i < 50000; ++i)
        {
            CBlockHeader header;
            header.nTime = GetTime();
            header.hashPrevBlock = lastIndex->GetBlockHash();
            header.nNonce = blockIndexStore.Count();
            header.nBits = lastIndex->GetBits(); // leave same complexity as dummy bits
            lastIndex = blockIndexStore.Insert( header );

            BOOST_CHECK_EQUAL( i, lastIndex->GetHeight() );
            BOOST_CHECK(lastIndex->GetHeight() == lastIndex->GetPrev()->GetHeight() + 1);
        }
        splitLastIndex = lastIndex;
        for (int i = 50000; i < 100000; ++i)
        {
            CBlockHeader header;
            header.nTime = GetTime();
            header.hashPrevBlock = lastIndex->GetBlockHash();
            header.nNonce = blockIndexStore.Count();
            header.nBits = lastIndex->GetBits(); // leave same complexity as dummy bits
            lastIndex = blockIndexStore.Insert( header );

            BOOST_CHECK_EQUAL( i, lastIndex->GetHeight() );
            BOOST_CHECK(lastIndex->GetHeight() == lastIndex->GetPrev()->GetHeight() + 1);
        }
    }

    // Build a branch that splits off at block 49999, 50000 blocks long.
    for (int i = 50000; i < 100000; ++i)
    {
        CBlockHeader header;
        header.hashPrevBlock = splitLastIndex->GetBlockHash();
        header.nBits = lastIndex->GetBits(); // leave same complexity as dummy bits
        splitLastIndex = blockIndexStore.Insert( header );

        BOOST_CHECK_EQUAL( i, splitLastIndex->GetHeight() );
        BOOST_CHECK(splitLastIndex->GetHeight() == splitLastIndex->GetPrev()->GetHeight() + 1);
    }

    // Build a CChain for the main branch.
    CChain chain;
    chain.SetTip( lastIndex );

    // And the side branch
    CChain chainSide;
    chainSide.SetTip( splitLastIndex );

    // Test 100 random starting points for locators.
    for (int n = 0; n < 100; n++) {
        int r = InsecureRandRange(150000);
        CBlockIndex *tip =
            (r < 100000) ? chain[r] : chainSide[r - 50000];
        CBlockLocator locator = chain.GetLocator(tip);

        // The first result must be the block itself, the last one must be
        // genesis.
        BOOST_CHECK(locator.vHave.front() == tip->GetBlockHash());
        BOOST_CHECK(locator.vHave.back() == chain[0]->GetBlockHash());

        // Entries 1 through 11 (inclusive) go back one step each.
        for (unsigned int i = 1; i < 12 && i < locator.vHave.size() - 1; i++) {
            BOOST_CHECK_EQUAL(blockIndexStore.Get( locator.vHave[i] )->GetHeight(),
                              tip->GetHeight() - static_cast<int32_t>(i));
        }

        // The further ones (excluding the last one) go back with exponential
        // steps.
        int32_t dist = 2;
        for (unsigned int i = 12; i < locator.vHave.size() - 1; i++) {
            BOOST_CHECK_EQUAL(blockIndexStore.Get( locator.vHave[i - 1] )->GetHeight() -
                              blockIndexStore.Get( locator.vHave[i] )->GetHeight(),
                              dist);
            dist *= 2;
        }
    }
}

BOOST_AUTO_TEST_CASE(findearliestatleast_test) {
    BlockIndexStore blockIndexStore;
    CChain chain;
    chain.SetTip(
        [&]
        {
            CBlockHeader header;
            header.nTime = GetTime();
            header.nBits =
                GetNextWorkRequired(
                    nullptr,
                    &header,
                    GlobalConfig::GetConfig() );
            return blockIndexStore.Insert( header );
        }() );

    for (unsigned int i = 1; i < 100000; ++i)
    {
        CBlockHeader header;
        header.hashPrevBlock = chain.Tip()->GetBlockHash();
        if (i < 10) {
            header.nTime = i;
        } else {
            // randomly choose something in the range [MTP, MTP*2]
            int64_t medianTimePast = chain[i-1]->GetMedianTimePast();
            int r = InsecureRandRange(medianTimePast);
            header.nTime = r + medianTimePast;
        }
        header.nBits =
            GetNextWorkRequired(
                chain.Tip(),
                &header,
                GlobalConfig::GetConfig() );

        chain.SetTip( blockIndexStore.Insert( header ) );
    }

    // Check that we set nTimeMax up correctly.
    int64_t curTimeMax = 0;
    for (size_t i = 0; i < blockIndexStore.Count(); ++i)
    {
        curTimeMax = std::max(curTimeMax, chain[i]->GetBlockTime());
        BOOST_CHECK(curTimeMax == chain[i]->GetBlockTimeMax());
    }

    // Verify that FindEarliestAtLeast is correct.
    for (size_t i = 0; i < blockIndexStore.Count(); ++i)
    {
        // Pick a random element in vBlocksMain.
        int r = InsecureRandRange(blockIndexStore.Count());
        int64_t test_time = chain[r]->GetBlockTime();
        CBlockIndex *ret = chain.FindEarliestAtLeast(test_time);
        BOOST_CHECK(ret->GetBlockTimeMax() >= test_time);
        BOOST_CHECK((ret->GetPrev() == nullptr) ||
                    ret->GetPrev()->GetBlockTimeMax() < test_time);
        BOOST_CHECK(chain[r]->GetAncestor(ret->GetHeight()) == ret);
    }
}

BOOST_AUTO_TEST_SUITE_END()
