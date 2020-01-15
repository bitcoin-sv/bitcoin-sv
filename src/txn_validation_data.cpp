// Copyright (c) 2019 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "txn_validation_data.h"

// Enable enum_cast for TxSource, so we can log informatively
const enumTableT<TxSource>& enumTable(TxSource)
{
    static enumTableT<TxSource> table
    {
        { TxSource::unknown,      "unknown" },
        { TxSource::file,         "file" },
        { TxSource::reorg,        "reorg" },
        { TxSource::wallet,       "wallet" },
        { TxSource::rpc,          "rpc" },
        { TxSource::p2p,          "p2p" },
        { TxSource::finalised,    "finalised" }
    };
    return table;
}

// Enable enum_cast for TxValidationPriority, so we can log informatively
const enumTableT<TxValidationPriority>& enumTable(TxValidationPriority)
{
    static enumTableT<TxValidationPriority> table
    {
        { TxValidationPriority::low,      "low" },
        { TxValidationPriority::normal,   "normal" },
        { TxValidationPriority::high,     "high" }
    };
    return table;
}
