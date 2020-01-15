// Copyright (c) 2019 The Bitcoin SV developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include "primitives/transaction.h"
#include "txn_validation_data.h"
#include "uint256.h"
#include <mutex>
#include <vector>
#include <set>

class CTxMemPool;
class CValidationState;

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
     * @param pTxInputData Transaction's input data
     * @param pool A reference to the mempool.
     * @param state A reference to the validation state.
     * @return true if inserted, false otherwise.
     */
    bool insertTxnInputs(
	    const TxInputDataSPtr& pTxInputData,
	    const CTxMemPool& pool,
	    CValidationState& state,
        bool isFinal);
    /**
     * Remove txn's inputs for known spends.
     * In case one of the transactions was not added this is a no-op.
     * @param tx A given transaction
     */
    void removeTxnInputs(const CTransaction& tx);
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
    std::set<const CTransaction*> mKnownSpendsTx;
    mutable std::mutex mMainMtx {};
};
