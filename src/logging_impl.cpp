// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2017-2018 The Bitcoin developers
// Copyright (c) 2019 Bitcoin Association
// Copyright (c) 2025 BSV Blockchain Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "logging_impl.h"

#include <sstream>
#include <vector>

#include "fs.h"
#include "util.h"
#include "utiltime.h"

static int FileWriteStr(const std::string &str, FILE *fp) {
    return fwrite(str.data(), 1, str.size(), fp);
}

LoggerImpl::LoggerImpl(const char* file_name)
: fileName{file_name}
{
}

LoggerImpl::~LoggerImpl() {
    if (fileout) {
        fclose(fileout);
    }
}

bool LoggerImpl::OpenDebugLog() {
    std::lock_guard<std::mutex> scoped_lock(mutexDebugLog);

    assert(fileout == nullptr);
    fs::path pathDebug = GetDataDir() / this->fileName;
    fileout = fsbridge::fopen(pathDebug, "a");
    if (fileout) {
        // Unbuffered.
        setbuf(fileout, nullptr);
        // Dump buffered messages from before we opened the log.
        while (!vMsgsBeforeOpenLog.empty()) {
            FileWriteStr(vMsgsBeforeOpenLog.front(), fileout);
            vMsgsBeforeOpenLog.pop_front();
        }
        return false;
    }
    else {
        return true;
    }
}

std::string LoggerImpl::LogTimestampStr(const std::string& str)
{
    if(!fLogTimestamps)
        return str;

    std::ostringstream ss;

    if(fStartedNewLine)
    {
#ifdef __MINGW32__
        const DateTimeFormatter& dtf = thread_local_log_DateTimeFormatter();
#else
        thread_local const DateTimeFormatter dtf{"%Y-%m-%d %H:%M:%S"};
#endif

        const int64_t nTimeMicros{GetLogTimeMicros()};
        ss = dtf(nTimeMicros / 1000000);
        if(fLogTimeMicros)
            ss << strprintf(".%06d", nTimeMicros % 1000000);

        ss << " [" << GetThreadName() << "] " << str;
    }
    else
        ss << str;

    if(!str.empty() && str[str.size() - 1] == '\n')
        fStartedNewLine = true;
    else
        fStartedNewLine = false;

    return ss.str();
}

int LoggerImpl::LogPrintStr(const std::string &str)
{
    return Log(str.c_str());
}

// Uses const char* as str type so that log entries from all nodes can be traced during
// functional tests. e.g.
// bpftrace -e 'u:/root/sv/src/bitcoind:*Logger*log* { printf("%d %s\n", pid, str(arg1)) }'
int LoggerImpl::Log(const char* str) {

    // Returns total number of characters written.
    int ret = 0;

    std::string strTimestamped = LogTimestampStr(str);

    if (fPrintToConsole) {
        // Print to console.
        ret = fwrite(strTimestamped.data(), 1, strTimestamped.size(), stdout);
        fflush(stdout);
    } else if (fPrintToDebugLog) {
        std::lock_guard<std::mutex> scoped_lock(mutexDebugLog);

        // Buffer if we haven't opened the log yet.
        if (fileout == nullptr) {
            // Stop logging if buffer gets too big
            if (vMsgsBeforeOpenLog.size() > 1000) {
                ret = 0;
            }
            else {
                vMsgsBeforeOpenLog.push_back(strTimestamped);
                ret = strTimestamped.length();
            }
        } else {
            // Reopen the log file, if requested.
            if (fReopenDebugLog) {
                fReopenDebugLog = false;
                fs::path pathDebug = GetDataDir() / this->fileName;
                if (fsbridge::freopen(pathDebug, "a", fileout) != nullptr) {
                    // unbuffered.
                    setbuf(fileout, nullptr);
                }
            }

            ret = FileWriteStr(strTimestamped, fileout);
        }
    }
    return ret;
}

void LoggerImpl::ShrinkDebugFile() {
    // Amount of fileName to save at end when shrinking (must fit in memory)
    constexpr size_t RECENT_DEBUG_HISTORY_SIZE = 10 * 1000000;
    // Scroll fileName if it's getting too big.
    fs::path pathLog = GetDataDir() / this->fileName;
    FILE *file = fsbridge::fopen(pathLog, "r");
    // If fileName is more than 10% bigger the RECENT_DEBUG_HISTORY_SIZE
    // trim it down by saving only the last RECENT_DEBUG_HISTORY_SIZE bytes.
    if (file &&
        fs::file_size(pathLog) > 11 * (RECENT_DEBUG_HISTORY_SIZE / 10)) {
        // Restart the file with some of the end.
        std::vector<char> vch(RECENT_DEBUG_HISTORY_SIZE, 0);
        fseek(file, -((long)vch.size()), SEEK_END);
        int nBytes = fread(vch.data(), 1, vch.size(), file);
        fclose(file);

        file = fsbridge::fopen(pathLog, "w");
        if (file) {
            fwrite(vch.data(), 1, nBytes, file);
            fclose(file);
        }
    } else if (file != nullptr)
        fclose(file);
}

void LoggerImpl::EnableCategory(BCLog::LogFlags category) {
    logCategories |= category;
}

void LoggerImpl::DisableCategory(BCLog::LogFlags category) {
    logCategories &= ~category;
}

bool LoggerImpl::WillLogCategory(typename std::underlying_type<BCLog::LogFlags>::type category) const {
    return (logCategories.load(std::memory_order_relaxed) & category) != 0;
}

bool LoggerImpl::DefaultShrinkDebugFile() const {
    return logCategories != BCLog::NONE;
}
