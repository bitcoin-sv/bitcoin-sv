// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2017-2018 The Bitcoin developers
// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#ifndef BITCOIN_LOGGING_H
#define BITCOIN_LOGGING_H

#include <atomic>
#include <cstdint>
#include <list>
#include <mutex>
#include <string>

#include "tinyformat.h"

static const bool DEFAULT_LOGTIMEMICROS = false;
static const bool DEFAULT_LOGIPS = false;
static const bool DEFAULT_LOGTIMESTAMPS = true;

extern bool fLogIPs; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

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
    ALL = ~uint32_t(0),
};

// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions)
class Logger {
private:
    /**
     * Name of the log file
     */
    const char* const fileName; // NOLINT (cppcoreguidelines-avoid-const-or-ref-data-members)

    FILE *fileout = nullptr;
    std::mutex mutexDebugLog;
    std::list<std::string> vMsgsBeforeOpenLog;

    /**
     * fStartedNewLine is a state variable that will suppress printing of the
     * timestamp when multiple calls are made that don't end in a newline.
     */
    std::atomic_bool fStartedNewLine{true};

    /**
     * Log categories bitfield. Leveldb/libevent need special handling if their
     * flags are changed at runtime.
     */
    std::atomic<typename std::underlying_type<LogFlags>::type> logCategories{0};

    std::string LogTimestampStr(const std::string &str);
    int log(const char*);

public:
    bool fPrintToConsole = false;
    bool fPrintToDebugLog = true;

    bool fLogTimestamps = DEFAULT_LOGTIMESTAMPS;
    bool fLogTimeMicros = DEFAULT_LOGTIMEMICROS;

    std::atomic<bool> fReopenDebugLog{false};

    explicit Logger(const char* fileName);
    ~Logger();

    /** Send a string to the log output */
    int LogPrintStr(const std::string &str);

    bool OpenDebugLog();
    void ShrinkDebugFile();

    void EnableCategory(LogFlags category);
    void DisableCategory(LogFlags category);

    /** Return true if log accepts specified category */
    bool WillLogCategory(typename std::underlying_type<LogFlags>::type category) const;

    /** Default for whether ShrinkDebugFile should be run */
    bool DefaultShrinkDebugFile() const;
};

} // namespace BCLog

BCLog::Logger &GetLogger();

/** Return true if log accepts one of the specified categories */
static inline bool LogAcceptCategory(typename std::underlying_type<BCLog::LogFlags>::type categories) {
    return GetLogger().WillLogCategory(categories);
}

/** Returns a string with the supported log categories */
std::string ListLogCategories();

/** Return true if str parses as a log category and set the flag */
bool GetLogCategory(BCLog::LogFlags &flag, const std::string &str);

#define LogPrint(category, ...)                                                \
    do {                                                                       \
        if (LogAcceptCategory((category))) {                                   \
            GetLogger().LogPrintStr(tfm::format(__VA_ARGS__));                 \
        }                                                                      \
    } while (0)

#define LogPrintf(...)                                                         \
    do {                                                                       \
        GetLogger().LogPrintStr(tfm::format(__VA_ARGS__));                     \
    } while (0)

#endif // BITCOIN_LOGGING_H
