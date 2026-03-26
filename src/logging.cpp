// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2017-2018 The Bitcoin developers
// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "fs.h"
#include "logging.h"
#include "logging_impl.h"
#include "util.h"
#include "utiltime.h"

constexpr auto LOGFILE = "bitcoind.log";
bool fLogIPs = DEFAULT_LOGIPS; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)


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
BCLog::Logger &GetLogger()
{
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    static BCLog::Logger *const logger = new BCLog::Logger(LOGFILE);
    return *logger;
}

BCLog::Logger::Logger(const char* file_name): loggerImpl{std::make_unique<LoggerImpl>(file_name)}{
}

bool BCLog::Logger::OpenDebugLog() {
    return loggerImpl->OpenDebugLog();
}

struct CLogCategoryDesc {
    BCLog::LogFlags flag;
    std::string category;
};

// NOLINTNEXTLINE(cert-err58-cpp)
const std::array<CLogCategoryDesc, 32> LogCategories
{{
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
    {BCLog::BLOCKSRC, "blocksrc"},
    {BCLog::ALL, "1"},
    {BCLog::ALL, "all"},
}};

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

int BCLog::Logger::LogPrintStr(const std::string &str)
{
    return loggerImpl->LogPrintStr(str);
}

void BCLog::Logger::ShrinkDebugFile() {
    loggerImpl->ShrinkDebugFile();
}

void BCLog::Logger::EnableCategory(BCLog::LogFlags category) {
    loggerImpl->EnableCategory(category);
}

void BCLog::Logger::DisableCategory(BCLog::LogFlags category) {
    loggerImpl->DisableCategory(category);
}


bool BCLog::Logger::WillLogCategory(typename std::underlying_type<BCLog::LogFlags>::type category) const {
    return loggerImpl->WillLogCategory(category);
}

bool BCLog::Logger::DefaultShrinkDebugFile() const {
    return loggerImpl->DefaultShrinkDebugFile();
}

void BCLog::Logger::SetPrintToConsole(bool v) { loggerImpl->fPrintToConsole = v; }
void BCLog::Logger::SetPrintToDebugLog(bool v){ loggerImpl->fPrintToDebugLog = v; }
void BCLog::Logger::SetLogTimestamps(bool v) { loggerImpl->fLogTimestamps = v; }
void BCLog::Logger::SetLogTimeMicros(bool v) { loggerImpl->fLogTimeMicros = v; }
void BCLog::Logger::SetReopenDebugLog(bool v) { loggerImpl->fReopenDebugLog = v; }

bool BCLog::Logger::PrintToConsole() const { return loggerImpl->fPrintToConsole; }
bool BCLog::Logger::PrintToDebugLog() const { return loggerImpl->fPrintToDebugLog; }
bool BCLog::Logger::LogTimestamps() const { return loggerImpl->fLogTimestamps; }
bool BCLog::Logger::LogTimeMicros() const { return loggerImpl->fLogTimeMicros; }
