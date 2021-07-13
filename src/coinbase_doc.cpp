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
    return a.version_ == b.version_ && 
           a.height_ == b.height_ &&
           a.prevMinerId_ == b.prevMinerId_ &&
           a.prevMinerIdSig_ == b.prevMinerIdSig_ && 
           a.minerId_ == b.minerId_ &&
           a.vctx_ == b.vctx_ &&
           ((!a.dataRefs_ && !b.dataRefs_) ||
            ((a.dataRefs_ && b.dataRefs_) &&
             (a.dataRefs_.value() == b.dataRefs_.value())));
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
    os << "version: " << doc.version_
       << "\nheight: " << doc.height_
       << "\nprev_miner_id: " << doc.prevMinerId_
       << "\nprev_miner_sig: " << doc.prevMinerIdSig_
       << "\nminer_id: " << doc.minerId_
       << "\noutpoint: " << doc.vctx_;
    // clang-format on

    if(!doc.dataRefs_)
        return os;

    for(const auto& dataref : doc.dataRefs_.value())
    {
        os << '\n' << dataref;
    }

    return os;
}

