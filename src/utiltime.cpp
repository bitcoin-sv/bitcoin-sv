// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include "config/bitcoin-config.h"
#endif

#include "utiltime.h"
#include "tinyformat.h"

#include <atomic>

#include <boost/thread.hpp>

//!< For unit testing
namespace
{
    std::atomic_int64_t nMockTime = 0;
}

int64_t GetTime() {
    if (nMockTime) return nMockTime;

    time_t now = time(nullptr);
    assert(now > 0);
    return now;
}

void SetMockTime(int64_t nMockTimeIn) {
    nMockTime = nMockTimeIn;
}

int64_t GetTimeMillis() {
    int64_t now = (boost::posix_time::microsec_clock::universal_time() -
                   boost::posix_time::ptime(boost::gregorian::date(1970, 1, 1)))
                      .total_milliseconds();
    assert(now > 0);
    return now;
}

int64_t GetTimeMicros() {
    int64_t now = (boost::posix_time::microsec_clock::universal_time() -
                   boost::posix_time::ptime(boost::gregorian::date(1970, 1, 1)))
                      .total_microseconds();
    assert(now > 0);
    return now;
}

int64_t GetSystemTimeInSeconds() {
    return GetTimeMicros() / 1000000;
}

/** Return a time useful for the debug log */
int64_t GetLogTimeMicros() {
    if (nMockTime) return nMockTime * 1000000;

    return GetTimeMicros();
}

void MilliSleep(int64_t n) {
    boost::this_thread::sleep_for(boost::chrono::milliseconds(n));
}

DateTimeFormatter::DateTimeFormatter(const char* format)
    : locale_{std::locale::classic(), new boost::posix_time::time_facet(format)}
{
}

std::ostringstream DateTimeFormatter::operator()(const int64_t nTime) const
{
    std::ostringstream ss;
    ss.imbue(locale_);
    ss << boost::posix_time::from_time_t(nTime);
    return ss;
}

std::string DateTimeStrFormat(const char* pszFormat, int64_t nTime)
{
    static std::locale classic(std::locale::classic());
    // std::locale takes ownership of the pointer
    std::locale loc(classic, new boost::posix_time::time_facet(pszFormat));
    std::stringstream ss;
    ss.imbue(loc);
    ss << boost::posix_time::from_time_t(nTime);
    return ss.str();
}

std::string DateTimeFormatISO8601(int64_t nTime) {
    struct tm ts;
    time_t time_val = nTime;
#ifdef WIN32
    // On windows gmtime_s must be used because gmtime_r is not available
    if (gmtime_s(&ts, &time_val) != 0) {
#else
    // All other platforms are assumed to provide gmtime_r (POSIX)
    if (gmtime_r(&time_val, &ts) == nullptr) {
#endif
        return {};
    }
    return tinyformat::format("%04i-%02i-%02iT%02i:%02i:%02iZ", ts.tm_year + 1900, ts.tm_mon + 1, ts.tm_mday, ts.tm_hour, ts.tm_min, ts.tm_sec);
}
