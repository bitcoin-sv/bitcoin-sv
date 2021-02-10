// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

// NOTE: This file is intended to be customised by the end user, and includes
// only local node policy logic

#include "policy/policy.h"
#include "script/script_num.h"
#include "taskcancellation.h"
#include "validation.h"
#include "config.h"

/**
 * Check transaction inputs to mitigate two potential denial-of-service attacks:
 *
 * 1. scriptSigs with extra data stuffed into them, not consumed by scriptPubKey
 * (or P2SH script)
 * 2. P2SH scripts with a crazy number of expensive CHECKSIG/CHECKMULTISIG
 * operations
 *
 * Why bother? To avoid denial-of-service attacks; an attacker can submit a
 * standard HASH... OP_EQUAL transaction, which will get accepted into blocks.
 * The redemption script can be anything; an attacker could use a very
 * expensive-to-check-upon-redemption script like:
 *   DUP CHECKSIG DROP ... repeated 100 times... OP_1
 */
bool IsStandard(const Config &config, const CScript &scriptPubKey, int32_t nScriptPubKeyHeight, txnouttype &whichType) {
    std::vector<std::vector<uint8_t>> vSolutions;
    if (!Solver(scriptPubKey, IsGenesisEnabled(config, nScriptPubKeyHeight), whichType, vSolutions)) {
        return false;
    }

    if (whichType == TX_MULTISIG) {
        // we don't require minimal encoding here because Solver method is already checking minimal encoding
        int m = CScriptNum(vSolutions.front(), false).getint();
        int n = CScriptNum(vSolutions.back(), false).getint();
        // Support up to x-of-3 multisig txns as standard
        if (n < 1 || n > 3) return false;
        if (m < 1 || m > n) return false;
    } else if (whichType == TX_NULL_DATA) {
        if (!fAcceptDatacarrier) {
            return false;
        }
    }

    return whichType != TX_NONSTANDARD;
}

// Check if a transaction is a consolidation transaction.
// A consolidation transaction is a transaction which reduces the size of the UTXO database to
// an extent that is rewarding enough for the miner to mine the transaction for free.
bool IsConsolidationTxn(const Config &config, const CTransaction &tx, const CCoinsViewCache &inputs, int32_t tipHeight)
{
    const uint64_t factor = config.GetMinConsolidationFactor();
    const uint64_t minConf = config.GetMinConfConsolidationInput();
    const uint64_t maxSize = config.GetMaxConsolidationInputScriptSize();
    const bool stdInputOnly = !config.GetAcceptNonStdConsolidationInput();


    // Allow disabling free consolidation txns via configuring
    // the consolidation factor to zero
    if (factor == 0)
        return false;

    if (tx.IsCoinBase())
        return false;

    // The consolidation transaction needs to reduce the count of UTXOS
    if (tx.vin.size() < factor * tx.vout.size())
        return false;

    // Check all UTXOs are confirmed and prevent spam via big
    // scriptSig sizes in the consolidation transaction inputs.
    uint64_t sumScriptPubKeySizeOfTxInputs = 0;
    for (CTxIn const & u: tx.vin) {

        // accept only with many confirmations
        const auto& coin = inputs.GetCoinWithScript(u.prevout);
        assert(coin.has_value());
        const auto coinHeight = coin->GetHeight();

        if (coinHeight == MEMPOOL_HEIGHT)
            return false;

        if (coinHeight && (tipHeight + 1 - coinHeight < minConf)) // older versions did not store height
            return false;

        // spam detection
        if (u.scriptSig.size() > maxSize)
            return false;

        // if not acceptnonstdconsolidationinput then check if inputs are standard
        // and fail otherwise
        txnouttype dummyType;
        if (stdInputOnly  && !IsStandard(config, coin->GetTxOut().scriptPubKey, coinHeight, dummyType))
            return false;

        // sum up some script sizes
        sumScriptPubKeySizeOfTxInputs += coin->GetTxOut().scriptPubKey.size();
    }

    // check ratio between sum of tx-scriptPubKeys to sum of parent-scriptPubKeys
    uint64_t sumScriptPubKeySizeOfTxOutputs = 0;
    for (CTxOut const & o: tx.vout) {
        sumScriptPubKeySizeOfTxOutputs += o.scriptPubKey.size();
    }

    // prevent consolidation transactions that are not advantageous enough for miners
    if(sumScriptPubKeySizeOfTxInputs < factor * sumScriptPubKeySizeOfTxOutputs)
        return false;

    return true;
}

bool IsStandardTx(const Config &config, const CTransaction &tx, int32_t nHeight, std::string &reason) {
    if (tx.nVersion > CTransaction::MAX_STANDARD_VERSION || tx.nVersion < 1) {
        reason = "version";
        return false;
    }

    // Extremely large transactions with lots of inputs can cost the network
    // almost as much to process as they cost the sender in fees, because
    // computing signature hashes is O(ninputs*txsize). Limiting transactions
    // mitigates CPU exhaustion attacks.
    unsigned int sz = tx.GetTotalSize();
    if (sz > config.GetMaxTxSize(IsGenesisEnabled(config, nHeight), false)) {
        reason = "tx-size";
        return false;
    }

    for (const CTxIn &txin : tx.vin) {
        // Biggest 'standard' txin is a 15-of-15 P2SH multisig with compressed
        // keys (remember the 520 byte limit on redeemScript size). That works
        // out to a (15*(33+1))+3=513 byte redeemScript, 513+1+15*(73+1)+3=1627
        // bytes of scriptSig, which we round off to 1650 bytes for some minor
        // future-proofing. That's also enough to spend a 20-of-20 CHECKMULTISIG
        // scriptPubKey, though such a scriptPubKey is not considered standard.
        if (!IsGenesisEnabled(config, nHeight)  && txin.scriptSig.size() > 1650) {
            reason = "scriptsig-size";
            return false;
        }
        if (!txin.scriptSig.IsPushOnly()) {
            reason = "scriptsig-not-pushonly";
            return false;
        }
    }

    unsigned int nDataSize = 0;
    txnouttype whichType;
    bool scriptpubkey = false;
    for (const CTxOut &txout : tx.vout) {
        if (!::IsStandard(config, txout.scriptPubKey, nHeight, whichType)) {
            scriptpubkey = true;
        }

        if (whichType == TX_NULL_DATA) {
            nDataSize += txout.scriptPubKey.size();
        } else if ((whichType == TX_MULTISIG) && (!fIsBareMultisigStd)) {
            reason = "bare-multisig";
            return false;
        } else if (txout.IsDust(dustRelayFee, IsGenesisEnabled(config, nHeight))) {
            reason = "dust";
            return false;
        }
    }

    // cumulative size of all OP_RETURN txout should be smaller than -datacarriersize
    if (nDataSize > config.GetDataCarrierSize()) {
        reason = "datacarrier-size-exceeded";
        return false;
    }
    
    if(scriptpubkey)
    {
        reason = "scriptpubkey";
        return false;
    }

    return true;
}

std::optional<bool> AreInputsStandard(
    const task::CCancellationToken& token,
    const Config& config,
    const CTransaction& tx,
    const CCoinsViewCache &mapInputs,
    const int32_t mempoolHeight)
{
    if (tx.IsCoinBase()) {
        // Coinbases don't use vin normally.
        return true;
    }

    for (size_t i = 0; i < tx.vin.size(); i++) {
        auto prev = mapInputs.GetCoinWithScript( tx.vin[i].prevout );
        assert(prev.has_value());
        assert(!prev->IsSpent());

        std::vector<std::vector<uint8_t>> vSolutions;
        txnouttype whichType;
        // get the scriptPubKey corresponding to this input:
        const CScript &prevScript = prev->GetTxOut().scriptPubKey;

        if (!Solver(prevScript, IsGenesisEnabled(config, prev.value(), mempoolHeight),
                    whichType, vSolutions)) {
            return false;
        }

        if (whichType == TX_SCRIPTHASH) {
            // Pre-genesis limitations are stricter than post-genesis, so LimitedStack can use UINT32_MAX as max size.
            LimitedStack stack(UINT32_MAX);
            // convert the scriptSig into a stack, so we can inspect the
            // redeemScript
            auto res =
                EvalScript(
                    config,
                    false,
                    token,
                    stack,
                    tx.vin[i].scriptSig,
                    SCRIPT_VERIFY_NONE,
                    BaseSignatureChecker());
            if (!res.has_value())
            {
                return {};
            }
            else if (!res.value())
            {
                return false;
            }
            if (stack.empty()) {
                return false;
            }
            
            // isGenesisEnabled is set to false, because TX_SCRIPTHASH is not supported after genesis
            bool sigOpCountError;
            CScript subscript(stack.back().begin(), stack.back().end());
            uint64_t nSigOpCount = subscript.GetSigOpCount(true, false, sigOpCountError);
            if (sigOpCountError || nSigOpCount > MAX_P2SH_SIGOPS) {
                return false;
            }
        }
    }

    return true;
}

CFeeRate dustRelayFee = CFeeRate(DUST_RELAY_TX_FEE);
static_assert(DUST_RELAY_TX_FEE == DEFAULT_MIN_RELAY_TX_FEE, "lowering only fees increases dust");