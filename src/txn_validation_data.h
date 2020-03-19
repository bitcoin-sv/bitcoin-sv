// Copyright (c) 2019 The Bitcoin SV developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include "primitives/transaction.h"
#include <enum_cast.h>

// Enumerate possible txn's source type
enum class TxSource
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
        TxSource txSource,
        TxValidationPriority txValidationPriority,
        CTransactionRef ptx,
        int64_t nAcceptTime=0,
        bool fLimitFree=false,
        Amount nAbsurdFee=Amount(0),
        std::weak_ptr<CNode> pNode={},
        bool fOrphan=false)
    : mTxSource(txSource),
      mTxValidationPriority(txValidationPriority),
      mpTx(ptx),
      mnAcceptTime(nAcceptTime),
      mfLimitFree(fLimitFree),
      mnAbsurdFee(nAbsurdFee),
      mpNode(pNode),
      mfOrphan(fOrphan)
    {}
    // Defaults
    CTxInputData(CTxInputData&&) = default;
    CTxInputData(const CTxInputData&) = default;
    ~CTxInputData() = default;

    TxSource mTxSource {TxSource::unknown};
    TxValidationPriority mTxValidationPriority {TxValidationPriority::normal};
    CTransactionRef mpTx {nullptr};
    int64_t mnAcceptTime {0};
    bool mfLimitFree {false};
    Amount mnAbsurdFee {0};
    std::weak_ptr<CNode> mpNode {};
    bool mfOrphan {false};
};

using TxInputDataSPtr = std::shared_ptr<CTxInputData>;
using TxInputDataSPtrVec = std::vector<TxInputDataSPtr>;
using TxInputDataSPtrRef = std::reference_wrapper<TxInputDataSPtr>;
using TxInputDataSPtrRefVec = std::vector<TxInputDataSPtrRef>;
