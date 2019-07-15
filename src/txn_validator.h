// Copyright (c) 2019 The Bitcoin SV developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include "txn_double_spend_detector.h"
#include "txn_handlers.h"
#include "txn_validation_data.h"

/**
 * A class representing txn Validator.
 *
 * The first implementation provieds synchronous validation interface between
 * the caller and the validator.
 */
class CTxnValidator final
{
  public:
    // Construction/destruction
    CTxnValidator(
        const Config& mConfig,
        CTxMemPool& mpool,
        TxnDoubleSpendDetectorSPtr dsDetector);
    ~CTxnValidator();

    // Forbid copying/assignment
    CTxnValidator(const CTxnValidator&) = delete;
    CTxnValidator(CTxnValidator&&) = delete;
    CTxnValidator& operator=(const CTxnValidator&) = delete;
    CTxnValidator& operator=(CTxnValidator&&) = delete;

    /**
     * Synchronous txn validation interface.
     */
    /** Process a new txn with wait */
    CValidationState processValidation(
        const TxInputDataSPtr& txInputData,
        mining::CJournalChangeSetPtr& changeSet,
        bool fLimitMempoolSize=false);

  private:
    // A reference to the configuration
    const Config& mConfig;

    // A reference to the mempool
    CTxMemPool& mMempool;

    /** Double spend detector */
    TxnDoubleSpendDetectorSPtr mpTxnDoubleSpendDetector {nullptr};
};
