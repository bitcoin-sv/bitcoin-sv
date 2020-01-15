// Copyright (c) 2019 The Bitcoin SV developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "txn_double_spend_detector.h"

#include "consensus/validation.h"
#include "txmempool.h"

bool CTxnDoubleSpendDetector::insertTxnInputs(
    const TxInputDataSPtr& pTxInputData,
    const CTxMemPool& pool,
    CValidationState& state,
    bool isFinal) {

    const CTransactionRef& ptx = pTxInputData->mpTx;
    const CTransaction &tx = *ptx;

    if (tx.vin.empty()) {
        // only a coinbase can have empty inputs and those should never get here
        // unless transaction is invalid and we still try to add it to double
        // spend detector
        return false;
    }

    // To avoid race conditions in double spends we need to take this mutex first.
    // This approach guarantees that:
    // a) if dstxn1 is accepted to the mempool then dstxn2 will be rejected as a mempool conflict
    // b) if dstxn1 and dstxn2 are valid txns (at this stage) then the first of them is allowed to
    //    continue processing but the other one is rejected as a double spend
    std::lock_guard lock(mMainMtx);
    // Check for conflicts with in-memory transactions.
    //
    // Double spend txns are allowed to be processed simultaneously.
    // In that case, it is possible that a valid txn is being processed and accepted
    // before other txn is detected as a double spend txn.
    // It might happen when two transactions have a common input but the first one
    // has less inputs than the second one.
    if (pool.CheckTxConflicts(ptx, isFinal)) {
        state.SetMempoolConflictDetected();
        return false;
    }
    // Check double spend attempt for the given txn.
    //
    // Motivation:
    // a) we want to process any number of potentially invalid double spends
    //    (detected and rejected by previous validation conditions) at the same time as the valid txn.
    // b) we want to select only the first valid txn if double spend occurs
    if (isAnyOfInputsKnownNL(tx)) {
        state.SetDoubleSpendDetected();
        return false;
    }
    // Store the inputs
    mKnownSpendsTx.insert(&tx);
    for (const auto& input: tx.vin) {
         mKnownSpends.emplace_back(input.prevout);
    }
    return true;
}

void CTxnDoubleSpendDetector::removeTxnInputs(const CTransaction &tx)
{
    if (tx.vin.empty()) {
        // skip as such transactions are guaranteed to never have been added
        // in insertTxnInputs but since we call removeTxnInputs on all transactions
        // - even the invalid ones - such a transaction could get to this point
        return;
    }

    std::lock_guard lock(mMainMtx);
    if(mKnownSpendsTx.find(&tx) == mKnownSpendsTx.end())
    {
        return;
    }

    // At this point we know that transaction inputs were added to the vector
    // and that the entries are both not duplicated and are laid out in
    // consecutive order (guaranteed by the way they were inserted).
    mKnownSpendsTx.erase(&tx);
    const auto& it = std::find(mKnownSpends.begin(), mKnownSpends.end(), tx.vin[0].prevout);
    mKnownSpends.erase(it, it + tx.vin.size());
}

size_t CTxnDoubleSpendDetector::getKnownSpendsSize() const {
    std::lock_guard lock(mMainMtx);
    return mKnownSpends.size();
}

void CTxnDoubleSpendDetector::clear() {
    std::lock_guard lock(mMainMtx);
    mKnownSpends.clear();
}

bool CTxnDoubleSpendDetector::isAnyOfInputsKnownNL(const CTransaction &tx) const {
    for (const auto& input: tx.vin) {
         if (std::find(mKnownSpends.begin(), mKnownSpends.end(), input.prevout) != mKnownSpends.end()) {
             return true;
         }
    }
    return false;
}

