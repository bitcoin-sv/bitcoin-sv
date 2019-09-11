// Copyright (c) 2019 The Bitcoin SV developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "txn_double_spend_detector.h"

bool CTxnDoubleSpendDetector::insertTxnInputs(const CTransaction &tx) {
    std::lock_guard lock(mKnownSpendsMtx);
    if (isAnyOfInputsKnownNL(tx)) {
        return false;
    }
    for (const auto& input: tx.vin) {
         mKnownSpends.emplace_back(input.prevout);
    }
    return true;
}

void CTxnDoubleSpendDetector::removeTxnInputs(const CTransaction &tx) {
    std::lock_guard lock(mKnownSpendsMtx);
    for (const auto& input: tx.vin) {
         auto it = std::find(mKnownSpends.begin(), mKnownSpends.end(), input.prevout);
         if (it != mKnownSpends.end()) {
             mKnownSpends.erase(it);
        }
    }
}

size_t CTxnDoubleSpendDetector::getKnownSpendsSize() const {
    std::lock_guard lock(mKnownSpendsMtx);
    return mKnownSpends.size();
}

void CTxnDoubleSpendDetector::clear() {
    std::lock_guard lock(mKnownSpendsMtx);
    mKnownSpends.clear();
}

bool CTxnDoubleSpendDetector::isAnyOfInputsKnownNL(const CTransaction &tx) const {
    for (const auto& input: tx.vin) {
         auto it = std::find(mKnownSpends.begin(), mKnownSpends.end(), input.prevout);
         if (it != mKnownSpends.end()) {
             return true;
         }
    }
    return false;
}

