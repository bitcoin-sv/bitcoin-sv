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
enum class TxType : int
{
    unknown = 0,
    standard = 1,
    nonstandard = 2
};
// Enable enum_cast for TxType, so we can log informatively
const enumTableT<TxType>& enumTable(TxType);

class CNode;

/**
 * The class used to provide an input data to the txn Validator.
 */
class CTxInputData {
public:
    // Constructor
    CTxInputData(
        TxSource txSource,
        TxType txType,
        CTransactionRef ptx,
        int64_t nAcceptTime=0,
        bool fLimitFree=false,
        Amount nAbsurdFee=Amount(0),
        std::shared_ptr<CNode> pNode=nullptr,
        bool fOrphan=false)
    : mTxSource(txSource),
      mTxType(txType),
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
    TxType mTxType {TxType::unknown};
    CTransactionRef mpTx {nullptr};
    int64_t mnAcceptTime {0};
    bool mfLimitFree {false};
    Amount mnAbsurdFee {0};
    std::shared_ptr<CNode> mpNode {nullptr};
    bool mfOrphan {false};
};

using TxInputDataSPtr = std::shared_ptr<CTxInputData>;
using TxInputDataSPtrVec = std::vector<TxInputDataSPtr>;
using TxInputDataSPtrRef = std::reference_wrapper<TxInputDataSPtr>;
using TxInputDataSPtrRefVec = std::vector<TxInputDataSPtrRef>;
