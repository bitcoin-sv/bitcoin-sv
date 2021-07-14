// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "coinbase_doc.h"

#include "hash.h"
#include "logging.h"
#include "primitives/transaction.h"
#include "pubkey.h"
#include <ostream>

using namespace std;

bool operator==(const CoinbaseDocument::DataRef& a,
                const CoinbaseDocument::DataRef& b)
{
    // clang-format off
    return a.brfcIds.size() == b.brfcIds.size() &&
           std::equal(a.brfcIds.begin(), a.brfcIds.end(), b.brfcIds.begin()) &&
           a.txid == b.txid &&
           a.vout == b.vout;
    // clang-format on
}

bool operator==(const CoinbaseDocument& a, const CoinbaseDocument& b)
{
    // clang-format off
    return a.mVersion == b.mVersion && 
           a.mHeight == b.mHeight &&
           a.mPrevMinerId == b.mPrevMinerId &&
           a.mPrevMinerIdSig == b.mPrevMinerIdSig && 
           a.mMinerId == b.mMinerId &&
           a.mVctx == b.mVctx &&
           ((!a.mDataRefs && !b.mDataRefs) ||
            ((a.mDataRefs && b.mDataRefs) &&
             (a.mDataRefs.value() == b.mDataRefs.value())));
    // clang-format on
}

namespace
{
    //    ostream& operator<<(ostream& os, const uint256& i)
    //    {
    //        os << i.ToString();
    //        return os;
    //    }
}

std::ostream& operator<<(std::ostream& os,
                         const CoinbaseDocument::DataRef& dataref)
{
    // clang-format off
    os << "txid: " << dataref.txid.ToString()
       << "\nvout: " << dataref.vout;
    // clang-format on

    for(const auto& brfcId : dataref.brfcIds)
        os << "\nbrfcs: " << brfcId;

    return os;
}

std::ostream& operator<<(std::ostream& os, const CoinbaseDocument& doc)
{
    // clang-format off
    os << "version: " << doc.mVersion
       << "\nheight: " << doc.mHeight
       << "\nprev_miner_id: " << doc.mPrevMinerId
       << "\nprev_miner_sig: " << doc.mPrevMinerIdSig
       << "\nminer_id: " << doc.mMinerId
       << "\noutpoint: " << doc.mVctx;
    // clang-format on

    if(!doc.mDataRefs)
        return os;

    for(const auto& dataref : doc.mDataRefs.value())
    {
        os << '\n' << dataref;
    }

    return os;
}

