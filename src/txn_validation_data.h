// Copyright (c) 2019 The Bitcoin SV developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include "txn_util.h"
#include <enum_cast.h>

// Enumerate possible txn's source type
enum class TxSource : int
{
    unknown,
    file,
    reorg,
    wallet,
    rpc,
    p2p,
    finalised
};
// Enable enum_cast for TxSource, so we can log informatively
const enumTableT<TxSource>& enumTable(TxSource);

// Enumerate possible txn's types
enum class TxValidationPriority : int
{
    low = 0,
    normal = 1,
    high = 2
};
// Enable enum_cast for TxValidationPriority, so we can log informatively
const enumTableT<TxValidationPriority>& enumTable(TxValidationPriority);

class CNode;

/**
 * The class used to provide an input data to the txn Validator.
 */
class CTxInputData {
public:
    // Constructor
    CTxInputData(
        TxIdTrackerWPtr pTxIdTracker,
        CTransactionRef ptx,
        TxSource txSource,
        TxValidationPriority txValidationPriority,
        int64_t nAcceptTime=0,
        bool fLimitFree=false,
        Amount nAbsurdFee=Amount(0),
        std::weak_ptr<CNode> pNode={},
        bool fOrphan=false);
    // Defaults
    CTxInputData(CTxInputData&&) = default;
    CTxInputData(const CTxInputData&) = default;
    // Destructor
    ~CTxInputData();

public:
    CTransactionRef mpTx {nullptr};
    std::weak_ptr<CNode> mpNode {};
    TxIdTrackerWPtr mpTxIdTracker {};
    Amount mnAbsurdFee {0};
    int64_t mnAcceptTime {0};
    TxSource mTxSource {TxSource::unknown};
    TxValidationPriority mTxValidationPriority {TxValidationPriority::normal};
    bool mfLimitFree {false};
    bool mfOrphan {false};
    bool mfTxIdStored {false};
};

using TxInputDataSPtr = std::shared_ptr<CTxInputData>;
using TxInputDataSPtrVec = std::vector<TxInputDataSPtr>;
using TxInputDataSPtrVecIter = std::vector<TxInputDataSPtr>::iterator;
using TxInputDataSPtrRef = std::reference_wrapper<TxInputDataSPtr>;
using TxInputDataSPtrRefVec = std::vector<TxInputDataSPtrRef>;
