// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

// NOTE: This file is intended to be customised by the end user, and includes
// only local node policy logic

#include "policy/policy.h"

#include "config.h"
#include "protocol_era.h"
#include "script/interpreter.h"
#include "script/script_flags.h"
#include "script/script_num.h"
#include "script/standard.h"
#include "taskcancellation.h"

#include <cstdint>


bool IsDustReturnTxn (const CTransaction &tx)
{
    return tx.vout.size() == 1
        && tx.vout[0].nValue.GetSatoshis() == 0U
        && IsDustReturnScript(tx.vout[0].scriptPubKey);
}


// Check if a transaction is a consolidation transaction.
// A consolidation transaction is a transaction which reduces the size of the UTXO database to
// an extent that is rewarding enough for the miner to mine the transaction for free.
// However, if a consolidation transaction is donated to the miner, then we do not need to honour the consolidation factor
AnnotatedType<bool>  IsFreeConsolidationTxn(const Config &config, const CTransaction &tx, const CCoinsViewCache &inputs, int32_t tipHeight)
{
    // Allow disabling free consolidation txns via configuring
    // the consolidation factor to zero
    if (config.GetMinConsolidationFactor() == 0)
        return {false, std::nullopt};

    const bool isDonation = IsDustReturnTxn(tx);

    const uint64_t factor = isDonation
            ? tx.vin.size()
            : config.GetMinConsolidationFactor();

    const int32_t minConf = isDonation
            ? int32_t(0)
            : config.GetMinConfConsolidationInput();

    const uint64_t maxSize = config.GetMaxConsolidationInputScriptSize();
    const bool stdInputOnly = !config.GetAcceptNonStdConsolidationInput();

    if (tx.IsCoinBase())
        return {false, std::nullopt};

    // The consolidation transaction needs to reduce the count of UTXOS
    if (tx.vin.size() < factor * tx.vout.size()) {
        // We will make an educated guess about the intentions of the transaction sender.
        // If the implied consolidation factor is greater 2 but less than the configured consolidation factor,
        // then we will emit a hint.
        if (tx.vin.size() > 2 * tx.vout.size()) {
            return{
                    false,
                    strprintf(
                            "Consolidation transaction %s has too few inputs in relation to outputs to be free."
                            " Consolidation factor is: %ld"
                            " See also configuration parameter -minconsolidationfactor.",
                            tx.GetId().ToString(),
                            factor)
            };
        }
        return {false, std::nullopt};
    }

    // Check all UTXOs are confirmed and prevent spam via big
    // scriptSig sizes in the consolidation transaction inputs.
    uint64_t sumScriptPubKeySizeOfTxInputs = 0;
    for (CTxIn const & u: tx.vin) {

        // accept only with many confirmations
        const auto& coin = inputs.GetCoinWithScript(u.prevout);
        assert(coin.has_value());
        const auto coinHeight = coin->GetHeight();

        if (minConf > 0 && coinHeight == MEMPOOL_HEIGHT) {
            return {false,
                strprintf(
                     "Consolidation transaction %s with input from unconfirmed transaction %s is not free."
                     " See also configuration parameter -minconsolidationinputmaturity",
                     tx.GetId().ToString(),
                     u.prevout.GetTxId().ToString())};
        }
        int32_t seenConf = tipHeight + 1 - coinHeight;
        if (minConf > 0 && coinHeight && seenConf < minConf) { // older versions did not store height
            return {false,
                strprintf(
                     "Consolidation transaction %s has input from transaction %s with %ld confirmations,"
                     " minimum required to be free is: %ld."
                     " See also configuration parameter -minconsolidationinputmaturity",
                     tx.GetId().ToString(),
                     u.prevout.GetTxId().ToString(),
                     seenConf,
                     minConf)};
        }

        // spam detection
        if (u.scriptSig.size() > maxSize) {
            return {false,
                 strprintf(
                     "Consolidation transaction %s has input from transaction %s with too large scriptSig %ld"
                     " to be free. Maximum is %ld."
                     " See also configuration parameter -maxconsolidationinputscriptsize",
                     tx.GetId().ToString(),
                     u.prevout.GetTxId().ToString(),
                     u.scriptSig.size(),
                     maxSize)};
        }

        // if not acceptnonstdconsolidationinput then check if inputs are standard
        // and fail otherwise
        txnouttype dummyType;
        if (stdInputOnly  && !IsStandardOutput(config.GetConfigScriptPolicy(), coin->GetTxOut().scriptPubKey, coinHeight, dummyType)) {
            return {false,
                 strprintf(
                     "Consolidation transaction %s has non-standard input from transaction %s and cannot be free."
                     " See also configuration parameter -acceptnonstdconsolidationinput",
                     tx.GetId().ToString(),
                     u.prevout.GetTxId().ToString())};
        }

        // sum up some script sizes
        sumScriptPubKeySizeOfTxInputs += coin->GetTxOut().scriptPubKey.size();
    }

    // check ratio between sum of tx-scriptPubKeys to sum of parent-scriptPubKeys
    uint64_t sumScriptPubKeySizeOfTxOutputs = 0;
    for (CTxOut const & o: tx.vout) {
        sumScriptPubKeySizeOfTxOutputs += o.scriptPubKey.size();
    }

    // prevent consolidation transactions that are not advantageous enough for miners
    if(sumScriptPubKeySizeOfTxInputs < factor * sumScriptPubKeySizeOfTxOutputs) {

        return {false,
             strprintf(
                 "Consolidation transaction %s is not free due to relation between cumulated"
                 " output to input ScriptPubKey sizes %ld/%ld less than %ld"
                 " See also documentation for configuration parameter -minconsolidationfactor",
                 tx.GetId().ToString(),
                 sumScriptPubKeySizeOfTxOutputs,
                 sumScriptPubKeySizeOfTxInputs,
                 factor)};
    }

    if (isDonation)
        return {true, strprintf("free donation transaction: %s", tx.GetId().ToString())};
    else
        return {true, strprintf("free consolidation transaction: %s", tx.GetId().ToString())};
}

std::optional<bool> AreInputsStandard(
    const task::CCancellationToken& token,
    const ConfigScriptPolicy& scriptPolicy,
    const CTransaction& tx,
    const CCoinsViewCache &mapInputs,
    const int32_t mempoolHeight)
{
    if (tx.IsCoinBase()) {
        // Coinbases don't use vin normally.
        return true;
    }

    constexpr bool consensus{};
    constexpr uint32_t flags{SCRIPT_VERIFY_NONE};
    const auto params{make_eval_script_params(scriptPolicy, flags, consensus)};

    for (size_t i = 0; i < tx.vin.size(); i++) {
        auto prev = mapInputs.GetCoinWithScript( tx.vin[i].prevout );
        assert(prev.has_value());
        assert(!prev->IsSpent());

        // get the scriptPubKey corresponding to this input:
        const CScript &prevScript = prev->GetTxOut().scriptPubKey;
        const ProtocolEra utxoEra = prev.value().GetProtocolEra(scriptPolicy, mempoolHeight);

        auto result = IsInputStandard(token, params, tx.vin[i].scriptSig, prevScript, utxoEra, flags);
        if (!result.has_value() || !result.value()) {
            return result;
        }
    }

    return true;
}
