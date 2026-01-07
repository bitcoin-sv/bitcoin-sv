// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2017-2018 The Bitcoin developers
// Copyright (c) 2019 Bitcoin Association
// Copyright (c) 2025 BSV Blockchain Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include <cstdint>

constexpr bool DEFAULT_LOGTIMEMICROS = false;
constexpr bool DEFAULT_LOGIPS = false;
constexpr bool DEFAULT_LOGTIMESTAMPS = true;

namespace BCLog {

enum LogFlags : uint32_t {
    NONE = 0,
    MEMPOOL = (1 << 1),
    HTTP = (1 << 2),
    BENCH = (1 << 3),
    ZMQ = (1 << 4),
    DB = (1 << 5),
    RPC = (1 << 6),
    ADDRMAN = (1 << 7),
    SELECTCOINS = (1 << 8),
    REINDEX = (1 << 9),
    CMPCTBLOCK = (1 << 10),
    RAND = (1 << 11),
    PRUNE = (1 << 12),
    PROXY = (1 << 13),
    MEMPOOLREJ = (1 << 14),
    LIBEVENT = (1 << 15),
    COINDB = (1 << 16),
    LEVELDB = (1 << 17),
    TXNPROP = (1 << 18),
    TXNSRC = (1 << 19),
    JOURNAL = (1 << 20),
    TXNVAL = (1 << 21),
    NETCONN = (1 << 22),
    NETMSG = (1 << 23),
    NETMSGVERB = (1 << 24),
    NETMSGALL = NETMSG | NETMSGVERB,
    NET = NETCONN | NETMSGALL,
    DOUBLESPEND = (1 << 25),
    MINERID = (1 << 26),
    BLOCKSRC = (1 << 27),
    ALL = ~uint32_t(0),
};

} // namespace BCLog
