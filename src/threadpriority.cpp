// Copyright (c) 2019 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "threadpriority.h"

// Enable enum_cast for ThreadPriority, so we can log informatively
const enumTableT<ThreadPriority>& enumTable(ThreadPriority)
{
    static enumTableT<ThreadPriority> table
    {
        { ThreadPriority::Low,      "L" },
        { ThreadPriority::Normal,   "N" },
        { ThreadPriority::High,     "H" }
    };
    return table;
}

