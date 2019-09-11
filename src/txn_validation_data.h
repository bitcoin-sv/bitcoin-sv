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
    p2p
};
// Enable enum_cast for TxSource, so we can log informatively
const enumTableT<TxSource>& enumTable(TxSource);

class CNode;

/**
 * The class used to provide an input data to the txn Validator.
 */
class CTxInputData {
public:
    // Constructor
    CTxInputData(
        TxSource txSource,
        CTransactionRef ptx,
        int64_t nAcceptTime=0,
        bool fLimitFree=false,
        Amount nAbsurdFee=Amount(0),
        std::shared_ptr<CNode> pNode=nullptr,
        bool fOrphan=false)
    : mTxSource(txSource),
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
    CTransactionRef mpTx {nullptr};
    int64_t mnAcceptTime {0};
    bool mfLimitFree {false};
    Amount mnAbsurdFee {0};
    std::shared_ptr<CNode> mpNode {nullptr};
    bool mfOrphan {false};
};

using TxInputDataSPtr = std::shared_ptr<CTxInputData>;
using TxInputDataSPtrVec = std::vector<TxInputDataSPtr>;
using TxInputDataSPtrRefVec = std::vector<std::reference_wrapper<TxInputDataSPtr>>;
