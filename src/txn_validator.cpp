// Copyright (c) 2019 The Bitcoin SV developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "txn_validation_result.h"
#include "txn_validator.h"
#include "config.h"

/** Constructor */
CTxnValidator::CTxnValidator(
    const Config& config,
    CTxMemPool& mpool,
    TxnDoubleSpendDetectorSPtr dsDetector)
    : mConfig(config),
      mMempool(mpool),
      mpTxnDoubleSpendDetector(dsDetector) {
}

/** Destructor */
CTxnValidator::~CTxnValidator() {
}

/** processValidation */
CValidationState CTxnValidator::processValidation(
    const TxInputDataSPtr& pTxInputData,
    mining::CJournalChangeSetPtr& changeSet,
    bool fLimitMempoolSize) {

    const CTransactionRef& ptx = pTxInputData->mpTx;
    const CTransaction &tx = *ptx;
    LogPrint(BCLog::TXNVAL,
            "Txnval-synch: Got a new txn= %s \n",
             tx.GetId().ToString());
    CTxnValResult result {};
    {
        // TODO: A temporary workaroud uses cs_main lock to control pcoinsTip change
        LOCK(cs_main);
        result = TxnValidation(
                    pTxInputData,
                    mConfig,
                    mMempool,
                    mpTxnDoubleSpendDetector);
        // Special handlers
        CTxnHandlers handlers {
            // Mempool Journal ChangeSet
            changeSet,
            // Double Spend Detector
            mpTxnDoubleSpendDetector,
        };
        // Process validated results for the given txn
        ProcessValidatedTxn(mMempool, result, handlers, fLimitMempoolSize);
        // After we've (potentially) uncached entries, ensure our coins cache is
        // still within its size limits
        CValidationState dummyState;
        FlushStateToDisk(mConfig.GetChainParams(), dummyState, FLUSH_STATE_PERIODIC);
    }
    return result.mState;
}

