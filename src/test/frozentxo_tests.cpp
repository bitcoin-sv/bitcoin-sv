// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "validation.h"

#include "block_index_store.h"
#include "config.h"
#include "frozentxo.h"
#include "frozentxo_db.h"
#include "pow.h"
#include "test/test_bitcoin.h"
#include <boost/test/unit_test.hpp>
#include <vector>
#include <cstdint>

BOOST_FIXTURE_TEST_SUITE(frozentxo, TestingSetup)

namespace {

void validateInputs(
    const CTransaction& tx,
    const CCoinsViewCache& inputs,
    CFrozenTXOCheck& frozenTXOCheck,
    const std::string& errorReason)
{
    CValidationState state;

    bool valid =
        Consensus::CheckTxInputs(
            tx,
            state,
            inputs,
            0, // ignoring spend height as the test should not reach that code
            frozenTXOCheck);

    BOOST_CHECK( !valid );
    BOOST_CHECK_EQUAL( state.GetRejectReason(), errorReason );
}

void populateBlocks( BlockIndexStore& blockIndexStore, CChain& blocks, std::size_t count )
{
    CBlockIndex* prev{};
    uint256 prevHash;
    for(std::size_t i = 0; i < count; ++i)
    {
        CBlockHeader header;
        header.nTime = i;
        header.hashPrevBlock = prevHash;
        header.nBits = GetNextWorkRequired( prev, &header, GlobalConfig::GetConfig() );

        prev = blockIndexStore.Insert( header );
        blocks.SetTip( prev );
        prevHash = prev->GetBlockHash();
    }
}

} // anonymous namespace

BOOST_AUTO_TEST_CASE(negative_Consensus_CheckTxInputs)
{
    CCoinsViewEmpty dummy;
    CCoinsViewCache inputs{dummy};

    CMutableTransaction txTemplate;
    txTemplate.vin.resize(1);
    txTemplate.vin[0].prevout = COutPoint{InsecureRand256(), 3};
    txTemplate.vin[0].scriptSig.resize(10);
    txTemplate.vout.resize(1);
    txTemplate.vout[0].nValue = Amount(42);
    CTransaction tx{txTemplate};

    inputs.AddCoin(
        txTemplate.vin[0].prevout,
        CoinWithScript::MakeOwning(CTxOut{Amount{3}, {}}, 1, false, false),
        false,
        0);

    CChain blocks;
    BlockIndexStore blockIndexStore;
    populateBlocks( blockIndexStore, blocks, 3 );

    std::vector<CFrozenTXOCheck> frozenTXOCheckTransaction; // NOTE: element index is block height at which check is performed
    std::vector<CFrozenTXOCheck> frozenTXOCheckBlock;
    uint256 prevHash;

    for(std::int32_t h=0; h<3; ++h)
    {
        frozenTXOCheckTransaction.emplace_back(
            h,
            "test transaction",
            prevHash
        );

        frozenTXOCheckBlock.emplace_back( *blocks[h] );

        prevHash = blocks[h]->GetBlockHash();
    }

    // enforce policy level freeze
    CFrozenTXODB::Instance().FreezeTXOPolicyOnly(txTemplate.vin[0].prevout);
    validateInputs(tx, inputs, frozenTXOCheckTransaction[0], "bad-txns-inputs-frozen"); // frozen at all heights for txs not yet in block
    validateInputs(tx, inputs, frozenTXOCheckTransaction[1], "bad-txns-inputs-frozen");
    validateInputs(tx, inputs, frozenTXOCheckTransaction[2], "bad-txns-inputs-frozen");
    validateInputs(tx, inputs, frozenTXOCheckBlock[0], "bad-txns-in-belowout"); // not frozen at any height for txs in block
    validateInputs(tx, inputs, frozenTXOCheckBlock[1], "bad-txns-in-belowout");
    validateInputs(tx, inputs, frozenTXOCheckBlock[2], "bad-txns-in-belowout");

    // start enforcing consensus level freeze at height 1
    CFrozenTXODB::Instance().FreezeTXOConsensus(txTemplate.vin[0].prevout, {{1}}, false);
    validateInputs(tx, inputs, frozenTXOCheckTransaction[0], "bad-txns-inputs-frozen"); // frozen at all heights for txs not yet in block
    validateInputs(tx, inputs, frozenTXOCheckTransaction[1], "bad-txns-inputs-frozen");
    validateInputs(tx, inputs, frozenTXOCheckTransaction[2], "bad-txns-inputs-frozen");
    validateInputs(tx, inputs, frozenTXOCheckBlock[0], "bad-txns-in-belowout"); // not frozen for txs in block at height 0
    validateInputs(tx, inputs, frozenTXOCheckBlock[1], "bad-txns-inputs-frozen"); // frozen for txs in block at height 1
    validateInputs(tx, inputs, frozenTXOCheckBlock[2], "bad-txns-inputs-frozen"); // frozen for txs in block at height 2

    // stop enforcing consensus level freeze at height 2 but keep enforcing policy level
    CFrozenTXODB::Instance().FreezeTXOConsensus(txTemplate.vin[0].prevout, {{1,2}}, false);
    validateInputs(tx, inputs, frozenTXOCheckTransaction[0], "bad-txns-inputs-frozen"); // frozen at all heights for txs not yet in block
    validateInputs(tx, inputs, frozenTXOCheckTransaction[1], "bad-txns-inputs-frozen");
    validateInputs(tx, inputs, frozenTXOCheckTransaction[2], "bad-txns-inputs-frozen");
    validateInputs(tx, inputs, frozenTXOCheckBlock[0], "bad-txns-in-belowout"); // not frozen for txs in block at height 0
    validateInputs(tx, inputs, frozenTXOCheckBlock[1], "bad-txns-inputs-frozen"); // frozen for txs in block at height 1
    validateInputs(tx, inputs, frozenTXOCheckBlock[2], "bad-txns-in-belowout"); // not frozen for txs in block at height 2

    // stop enforcing consensus and policy level freeze at height 2
    CFrozenTXODB::Instance().FreezeTXOConsensus(txTemplate.vin[0].prevout, {{1,2}}, true);
    validateInputs(tx, inputs, frozenTXOCheckTransaction[0], "bad-txns-inputs-frozen"); // frozen only at heights before stop height for txs not yet in block
    validateInputs(tx, inputs, frozenTXOCheckTransaction[1], "bad-txns-inputs-frozen");
    validateInputs(tx, inputs, frozenTXOCheckTransaction[2], "bad-txns-in-belowout");
    validateInputs(tx, inputs, frozenTXOCheckBlock[0], "bad-txns-in-belowout"); // not frozen for txs in block at height 0
    validateInputs(tx, inputs, frozenTXOCheckBlock[1], "bad-txns-inputs-frozen"); // frozen for txs in block at height 1
    validateInputs(tx, inputs, frozenTXOCheckBlock[2], "bad-txns-in-belowout"); // not frozen for txs in block at height 2
}

BOOST_AUTO_TEST_CASE(confiscationTransaction_CheckTxInputs)
{
    CMutableTransaction ctxTemplate;
    ctxTemplate.vin.resize(1);
    ctxTemplate.vin[0].prevout = COutPoint{InsecureRand256(), 3};
    ctxTemplate.vin[0].scriptSig.resize(10);
    ctxTemplate.vout.resize(2);
    ctxTemplate.vout[0].scriptPubKey = CScript() << OP_FALSE << OP_RETURN << std::vector<std::uint8_t>{'c','f','t','x'} << std::vector<std::uint8_t>{1, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
    ctxTemplate.vout[0].nValue = Amount(0);
    ctxTemplate.vout[1].scriptPubKey = CScript() << OP_TRUE;
    ctxTemplate.vout[1].nValue = Amount(42);
    CTransaction ctx{ctxTemplate};
    BOOST_REQUIRE( CFrozenTXOCheck::IsConfiscationTx(ctx) );
    BOOST_REQUIRE( CFrozenTXOCheck::ValidateConfiscationTxContents(ctx) );

    CCoinsViewEmpty dummy;
    CCoinsViewCache inputs{dummy};
    inputs.AddCoin(
        ctxTemplate.vin[0].prevout,
        CoinWithScript::MakeOwning(CTxOut{Amount{3}, {}}, 1, false, false),
        false,
        0);

    CChain blocks;
    BlockIndexStore blockIndexStore;
    populateBlocks( blockIndexStore, blocks, 3 );

    std::vector<CFrozenTXOCheck> frozenTXOCheckTransaction; // NOTE: element index is block height at which check is performed
    std::vector<CFrozenTXOCheck> frozenTXOCheckBlock;
    uint256 prevHash;
    for(std::int32_t h=0; h<3; ++h)
    {
        frozenTXOCheckTransaction.emplace_back(
            h,
            "test transaction",
            prevHash
        );

        frozenTXOCheckBlock.emplace_back( *blocks[h] );

        prevHash = blocks[h]->GetBlockHash();
    }

    // Helper to run validateInputs on both frozenTXOCheckTransaction and frozenTXOCheckBlock
    // because for confiscation transaction the result must be the same.
    auto validateInputs2 = [&](std::size_t i, const std::string& errorReason) {
        validateInputs(ctx, inputs, frozenTXOCheckTransaction[i], errorReason);
        validateInputs(ctx, inputs, frozenTXOCheckBlock[i], errorReason);
    };

    // start enforcing consensus level freeze at height 1 to be able to confiscate this TXO
    CFrozenTXODB::Instance().FreezeTXOConsensus(ctx.vin[0].prevout, {{1,2}}, false);
    CFrozenTXODB::Instance().WhitelistTx(1, ctx);
    validateInputs2(0, "bad-ctx-not-whitelisted"); // not whitelisted at height 0
    validateInputs2(1, "bad-txns-in-belowout"); // whitelisted and frozen at height 1
    validateInputs2(2, "bad-txns-in-belowout"); // whitelisted and frozen at height 2 because TXO is confiscated and therefore consensus frozen at all heights

    // stopping enforcing consensus level freeze at all heights should have no effect since TXO is already confiscated and therefore consensus frozen at all heights
    CFrozenTXODB::Instance().FreezeTXOConsensus(ctxTemplate.vin[0].prevout, {}, true);
    validateInputs2(0, "bad-ctx-not-whitelisted");
    validateInputs2(1, "bad-txns-in-belowout");
    validateInputs2(2, "bad-txns-in-belowout");

    // enforceAtHeight of confiscation transaction can be decreased
    CFrozenTXODB::Instance().WhitelistTx(0, ctx);
    validateInputs2(0, "bad-txns-in-belowout");
    validateInputs2(1, "bad-txns-in-belowout");
    validateInputs2(2, "bad-txns-in-belowout");
}

BOOST_AUTO_TEST_SUITE_END()
