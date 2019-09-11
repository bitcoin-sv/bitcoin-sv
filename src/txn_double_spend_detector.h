// Copyright (c) 2019 The Bitcoin SV developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include "primitives/transaction.h"
#include "uint256.h"
#include <mutex>
#include <vector>

class CTxnDoubleSpendDetector;
using TxnDoubleSpendDetectorSPtr = std::shared_ptr<CTxnDoubleSpendDetector>;

/**
 * A basic class used to detect a double spend issue in an early stage of txn validation.
 */

class CTxnDoubleSpendDetector {
  public:
    CTxnDoubleSpendDetector() = default;
    ~CTxnDoubleSpendDetector() = default;
    /**
     * Insert txn's inputs into known spends only if non of it's inputs is already known.
     * @param tx A given transaction.
     * @return true if inserted, false otherwise.
     */
    bool insertTxnInputs(const CTransaction &tx);
    /**
     * Remove txn's inputs for known spends.
     * @param tx A given transaction
     */
    void removeTxnInputs(const CTransaction &tx);
    /**
     * Get a number of known spends
     * @return A size of known spends.
     */
    size_t getKnownSpendsSize() const;
    /**
     * Clear known spends.
     */
    void clear();

  private:
    /** Check if any of txn's inputs is already known */
    bool isAnyOfInputsKnownNL(const CTransaction &tx) const;

  private:
    std::vector<COutPoint> mKnownSpends = {};
    mutable std::mutex mKnownSpendsMtx {};
};
