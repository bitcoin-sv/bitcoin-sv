// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2017-2018 The Bitcoin developers
// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "logging.h"
#include "util.h"
#include "utiltime.h"

constexpr auto LOGFILE = "bitcoind.log";

bool fLogIPs = DEFAULT_LOGIPS;

/**
 * NOTE: the logger instance is leaked on exit. This is ugly, but will be
 * cleaned up by the OS/libc. Defining a logger as a global object doesn't work
 * since the order of destruction of static/global objects is undefined.
 * Consider if the logger gets destroyed, and then some later destructor calls
 * LogPrintf, maybe indirectly, and you get a core dump at shutdown trying to
 * access the logger. When the shutdown sequence is fully audited and tested,
 * explicit destruction of these objects can be implemented by changing this
 * from a raw pointer to a std::unique_ptr.
 *
 * This method of initialization was originally introduced in
 * ee3374234c60aba2cc4c5cd5cac1c0aefc2d817c.
 */
BCLog::Logger &GetLogger() {
    static BCLog::Logger *const logger = new BCLog::Logger(LOGFILE);
    return *logger;
}

static int FileWriteStr(const std::string &str, FILE *fp) {
    return fwrite(str.data(), 1, str.size(), fp);
}

bool BCLog::Logger::OpenDebugLog() {
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

struct CLogCategoryDesc {
    BCLog::LogFlags flag;
    std::string category;
};

const CLogCategoryDesc LogCategories[] = {
    {BCLog::NONE, "0"},
    {BCLog::MEMPOOL, "mempool"},
    {BCLog::HTTP, "http"},
    {BCLog::BENCH, "bench"},
    {BCLog::ZMQ, "zmq"},
    {BCLog::DB, "db"},
    {BCLog::RPC, "rpc"},
    {BCLog::ADDRMAN, "addrman"},
    {BCLog::SELECTCOINS, "selectcoins"},
    {BCLog::REINDEX, "reindex"},
    {BCLog::CMPCTBLOCK, "cmpctblock"},
    {BCLog::RAND, "rand"},
    {BCLog::PRUNE, "prune"},
    {BCLog::PROXY, "proxy"},
    {BCLog::MEMPOOLREJ, "mempoolrej"},
    {BCLog::LIBEVENT, "libevent"},
    {BCLog::COINDB, "coindb"},
    {BCLog::LEVELDB, "leveldb"},
    {BCLog::TXNPROP, "txnprop"},
    {BCLog::TXNSRC, "txnsrc"},
    {BCLog::JOURNAL, "journal"},
    {BCLog::TXNVAL, "txnval"},
    {BCLog::NETCONN, "netconn"},
    {BCLog::NETMSG, "netmsg"},
    {BCLog::NETMSGVERB, "netmsgverb"},
    {BCLog::NETMSGALL, "netmsgall"},
    {BCLog::NET, "net"},
    {BCLog::DOUBLESPEND, "doublespend"},
    {BCLog::MINERID, "minerid"},
    {BCLog::ALL, "1"},
    {BCLog::ALL, "all"},
};

bool GetLogCategory(BCLog::LogFlags &flag, const std::string &str) {
    if (str == "") {
        flag = BCLog::ALL;
        return true;
    }
    for (const CLogCategoryDesc &category_desc : LogCategories) {
        if (category_desc.category == str) {
            flag = category_desc.flag;
            return true;
        }
    }
    return false;
}

std::string ListLogCategories() {
    std::string ret;
    int outcount = 0;
    for (const CLogCategoryDesc &category_desc : LogCategories) {
        // Omit the special cases.
        if (category_desc.flag != BCLog::NONE &&
            category_desc.flag != BCLog::ALL) {
            if (outcount != 0) ret += ", ";
            ret += category_desc.category;
            outcount++;
        }
    }
    return ret;
}

BCLog::Logger::Logger(const char* fileName)
: fileName(fileName)
{
}

BCLog::Logger::~Logger() {
    if (fileout) {
        fclose(fileout);
    }
}

#ifdef __MINGW32__
// MinGW with POSIX threads has a bug where destructors for thread_local
// objects are called after the memory has been already released.
// As a workaround, Boost thread specific storage is used instead.
#include <boost/thread/tss.hpp>
namespace {
const DateTimeFormatter& thread_local_log_DateTimeFormatter()
{
    static boost::thread_specific_ptr<DateTimeFormatter> dtf_tsp;
    auto* dtf = dtf_tsp.get();
    if(dtf==nullptr)
    {
        dtf_tsp.reset(new DateTimeFormatter{"%Y-%m-%d %H:%M:%S"});
        dtf = dtf_tsp.get();
    }
    return *dtf;
}
}
#endif

std::string BCLog::Logger::LogTimestampStr(const std::string& str)
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

int BCLog::Logger::LogPrintStr(const std::string &str) 
{
    return log(str.c_str());
}

// Uses const char* as str type so that log entries from all nodes can be traced during 
// functional tests. e.g.
// bpftrace -e 'u:/root/sv/src/bitcoind:*Logger*log* { printf("%d %s\n", pid, str(arg1)) }'
int BCLog::Logger::log(const char* str) {

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

void BCLog::Logger::ShrinkDebugFile() {
    // Amount of LOGFILE to save at end when shrinking (must fit in memory)
    constexpr size_t RECENT_DEBUG_HISTORY_SIZE = 10 * 1000000;
    // Scroll LOGFILE if it's getting too big.
    fs::path pathLog = GetDataDir() / this->fileName;
    FILE *file = fsbridge::fopen(pathLog, "r");
    // If LOGFILE is more than 10% bigger the RECENT_DEBUG_HISTORY_SIZE
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

void BCLog::Logger::EnableCategory(LogFlags category) {
    logCategories |= category;
}

void BCLog::Logger::DisableCategory(LogFlags category) {
    logCategories &= ~category;
}

bool BCLog::Logger::WillLogCategory(typename std::underlying_type<LogFlags>::type category) const {
    return (logCategories.load(std::memory_order_relaxed) & category) != 0;
}

bool BCLog::Logger::DefaultShrinkDebugFile() const {
    return logCategories != BCLog::NONE;
}
