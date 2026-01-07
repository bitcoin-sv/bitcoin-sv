// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2017-2018 The Bitcoin developers
// Copyright (c) 2019 Bitcoin Association
// Copyright (c) 2025 BSV Blockchain Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include <atomic>
#include <cstdio>
#include <list>
#include <mutex>
#include <string>
#include <type_traits>

#include "logging_flags.h"

class LoggerImpl {

public:

    explicit LoggerImpl(const char* fileName);

    LoggerImpl(const LoggerImpl&) = delete;
    LoggerImpl& operator=(const LoggerImpl&) = delete;
    LoggerImpl(LoggerImpl&&) = delete;
    LoggerImpl& operator=(LoggerImpl&&) = delete;

    ~LoggerImpl();

    std::string LogTimestampStr(const std::string &str);

    int Log(const char*);

    /** Send a string to the log output */
    int LogPrintStr(const std::string &str);

    bool OpenDebugLog();
    void ShrinkDebugFile();

    void EnableCategory(BCLog::LogFlags category);
    void DisableCategory(BCLog::LogFlags category);

    /** Return true if log accepts specified category */
    bool WillLogCategory(typename std::underlying_type<BCLog::LogFlags>::type category) const;

    /** Default for whether ShrinkDebugFile should be run */
    bool DefaultShrinkDebugFile() const;

    bool fPrintToConsole = false;
    bool fPrintToDebugLog = true;

    bool fLogTimestamps = DEFAULT_LOGTIMESTAMPS;
    bool fLogTimeMicros = DEFAULT_LOGTIMEMICROS;

    std::atomic<bool> fReopenDebugLog{false};

private:
    /**
     * Name of the log file
     */
    const char* const fileName; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)

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
    std::atomic<typename std::underlying_type<BCLog::LogFlags>::type> logCategories{0};
};
