// Copyright (c) 2019 The Bitcoin SV developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include "consensus/validation.h"
#include "txmempool.h"
#include "txn_validation_data.h"

/**
 * The class used to represent results of txn validation.
 */
class CTxnValResult {
  public:
    CValidationState mState {};
    TxInputDataSPtr mTxInputData {nullptr};
    std::vector<COutPoint> mCoinsToUncache {};
    std::shared_ptr<CTxMemPoolEntry> mpEntry {nullptr};
};
