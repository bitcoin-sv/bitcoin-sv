// Copyright (c) 2018-2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include "protocol.h"
#include "txmempool.h"

/**
* Details of a transaction for sending out over P2P.
*/
class CTxnSendingDetails
{
  public:
    CTxnSendingDetails(const CInv& inv, const TxMempoolInfo& info)
        : mInv{inv}, mTxInfo{info} {}

    CTxnSendingDetails(const CInv& inv, const CTransactionRef& forcedRef)
        : mInv{inv}, mForcedTx{forcedRef} {}

    const CInv& getInv() const { return mInv; }
    const TxMempoolInfo& getInfo() const { return mTxInfo; }

    bool isForcedRelay() const { return static_cast<bool>(mForcedTx); }
    const CTransactionRef& getTxnRef() const
    {
        if(mForcedTx)
            return mForcedTx;
        else
            return mTxInfo.tx;
    }

  private:

    CInv mInv {};
    TxMempoolInfo mTxInfo {};
    CTransactionRef mForcedTx {};
};