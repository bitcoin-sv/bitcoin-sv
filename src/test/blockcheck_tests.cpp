// Copyright (c) 2013-2015 The Bitcoin Core developers
// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "chainparams.h"
#include "config.h"
#include "consensus/consensus.h"
#include "consensus/validation.h"
#include "validation.h"

#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(blockcheck_tests, BasicTestingSetup)

static void RunCheckOnBlockImpl(const GlobalConfig &config, const CBlock &block,
                                CValidationState &state, bool expected) {
    block.fChecked = false;
    BlockValidationOptions validationOptions =
        BlockValidationOptions(false, false);
    bool fValid = CheckBlock(config, block, state, 0, validationOptions);

    BOOST_CHECK_EQUAL(fValid, expected);
    BOOST_CHECK_EQUAL(fValid, state.IsValid());
}

static void RunCheckOnBlock(const GlobalConfig &config, const CBlock &block) {
    CValidationState state;
    RunCheckOnBlockImpl(config, block, state, true);
}

static void RunCheckOnBlock(const GlobalConfig &config, const CBlock &block,
                            const std::string &reason) {
    CValidationState state;
    RunCheckOnBlockImpl(config, block, state, false);

    BOOST_CHECK_EQUAL(state.GetRejectCode(), REJECT_INVALID);
    BOOST_CHECK_EQUAL(state.GetRejectReason(), reason);
}

BOOST_AUTO_TEST_CASE(blockfail) {
    SelectParams(CBaseChainParams::MAIN);

    // Set max blocksize to default in case other tests left it dirty
    testConfig.SetDefaultBlockSizeParams(Params().GetDefaultBlockSizeParams());
    testConfig.SetMaxBlockSize(128*ONE_MEGABYTE);
    auto nDefaultMaxBlockSize = testConfig.GetMaxBlockSize();

    CBlock block;
    RunCheckOnBlock(testConfig, block, "bad-cb-missing");

    CMutableTransaction tx;

    // Coinbase only.
    tx.vin.resize(1);
    tx.vin[0].scriptSig.resize(10);
    tx.vout.resize(1);
    tx.vout[0].nValue = Amount(42);
    auto coinbaseTx = CTransaction(tx);

    block.vtx.resize(1);
    block.vtx[0] = MakeTransactionRef(tx);
    RunCheckOnBlock(testConfig, block);

    // No coinbase
    tx.vin[0].prevout = COutPoint(InsecureRand256(), 0);
    block.vtx[0] = MakeTransactionRef(tx);

    RunCheckOnBlock(testConfig, block, "bad-cb-missing");

    // Invalid coinbase
    tx = CMutableTransaction(coinbaseTx);
    tx.vin[0].scriptSig.resize(0);
    block.vtx[0] = MakeTransactionRef(tx);

    RunCheckOnBlock(testConfig, block, "bad-cb-length");

    // Oversize block.
    tx = CMutableTransaction(coinbaseTx);
    block.vtx[0] = MakeTransactionRef(tx);
    auto txSize = ::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION);
    auto maxTxCount = ((nDefaultMaxBlockSize - 1) / txSize) - 1;

    for (size_t i = 1; i < maxTxCount; i++) {
        tx.vin[0].prevout = COutPoint(InsecureRand256(), 0);
        block.vtx.push_back(MakeTransactionRef(tx));
    }

    // Check that at this point, we still accept the block.
    RunCheckOnBlock(testConfig, block);

    // And that serialisation works for large blocks
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << block;
    CBlock unserblock;
    ss >> unserblock;
    BOOST_CHECK(block.vtx.size() == unserblock.vtx.size());

    // But reject it with one more transaction as it goes over the maximum
    // allowed block size.
    tx.vin[0].prevout = COutPoint(InsecureRand256(), 0);
    block.vtx.push_back(MakeTransactionRef(tx));
    RunCheckOnBlock(testConfig, block, "bad-blk-length");

	// Bounds checking within GetHeightFromCoinbase()
    block.vtx.resize(1);
    block.vtx[0] = MakeTransactionRef(tx);
}

BOOST_AUTO_TEST_CASE(block_bounds_check)
{
    SelectParams(CBaseChainParams::MAIN);

    /* Bounds checking within GetHeightFromCoinbase() */

    // Invalid coinbase script which mis-reports length
    CMutableTransaction tx {};
    tx.vin.resize(1);
    tx.vin[0].scriptSig.resize(1);
    tx.vin[0].scriptSig[0] = 0xff;
    tx.vout.resize(1);
    tx.vout[0].nValue = Amount(42);
    auto coinbaseTx = CTransaction(tx);
    CBlock block {};
    block.vtx.resize(1);
    block.vtx[0] = MakeTransactionRef(tx);

    // GetHeightFromCoinbase() should throw
    BOOST_CHECK_THROW(block.GetHeightFromCoinbase(), std::runtime_error);
}
 
BOOST_AUTO_TEST_SUITE_END()
