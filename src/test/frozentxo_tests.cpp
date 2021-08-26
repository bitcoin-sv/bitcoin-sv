// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "validation.h"

#include "frozentxo.h"
#include "frozentxo_db.h"
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
        CoinWithScript::MakeOwning(CTxOut{Amount{3}, {}}, 1, false),
        false,
        0);

    // dummy block hashes
    auto parentHash = InsecureRand256();
    auto childHash = InsecureRand256();
    std::int64_t childReceiveTime{999};

    std::vector<CFrozenTXOCheck> frozenTXOCheckTransaction; // NOTE: element index is block height at which check is performed
    std::vector<CFrozenTXOCheck> frozenTXOCheckBlock;
    for(std::int32_t h=0; h<3; ++h)
    {
        frozenTXOCheckTransaction.emplace_back(
            h,
            "test transaction",
            parentHash
        );
        
        frozenTXOCheckBlock.emplace_back(
            h,
            "test block",
            parentHash,
            childReceiveTime,
            childHash
        );
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

BOOST_AUTO_TEST_SUITE_END()
