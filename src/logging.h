// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2017-2018 The Bitcoin developers
// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#ifndef BITCOIN_LOGGING_H
#define BITCOIN_LOGGING_H

#include <memory>
#include <string>
#include <type_traits>

#include "logging_flags.h"

#if !defined(DISABLE_LOGGING) && !defined(__EMSCRIPTEN__)
#include "tinyformat.h"
#endif

extern bool fLogIPs; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
class LoggerImpl;

namespace BCLog {

class Logger {

public:

    Logger(const char* file_name);

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    Logger(Logger&&) = default;
    Logger& operator=(Logger&&) = default;

    ~Logger() = default;

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

    void SetPrintToConsole(bool v);
    void SetPrintToDebugLog(bool v);
    void SetLogTimestamps(bool v);
    void SetLogTimeMicros(bool v);
    void SetReopenDebugLog(bool v);

    bool PrintToConsole() const;
    bool PrintToDebugLog() const;
    bool LogTimestamps() const;
    bool LogTimeMicros() const;

private:
    std::unique_ptr<LoggerImpl> loggerImpl;
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

// Building with __EMSCRIPTEN__ will need to disable logging
#if defined(DISABLE_LOGGING) || defined(__EMSCRIPTEN__)
    #define LogPrint(category, ...) do {} while (0)
    #define LogPrintf(...)          do {} while (0)
#else
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
#endif

#endif // BITCOIN_LOGGING_H
