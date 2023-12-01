// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * Server/client environment: argument handling, config file parsing,
 * thread wrappers, startup time
 */
#ifndef BITCOIN_UTIL_H
#define BITCOIN_UTIL_H

#if defined(HAVE_CONFIG_H)
#include "config/bitcoin-config.h"
#endif

#include "compat.h"
#include "fs.h"
#include "logging.h"
#include "sync.h"
#include "tinyformat.h"
#include "utiltime.h"

#include <atomic>
#include <array>
#include <cstdint>
#include <exception>
#include <map>
#include <numeric>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include <boost/signals2/signal.hpp>
#include <boost/thread/exceptions.hpp>

// Application startup time (used for uptime calculation)
int64_t GetStartupTime();

/** Signals for translation. */
class CTranslationInterface {
public:
    /** Translate a message to the native language of the user. */
    boost::signals2::signal<std::string(const char *psz)> Translate;
};

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
extern CTranslationInterface translationInterface;

extern const char *const BITCOIN_CONF_FILENAME;
extern const char *const BITCOIN_PID_FILENAME;

/**
 * Translation function: Call Translate signal on UI interface, which returns a
 * boost::optional result. If no translation slot is registered, nothing is
 * returned, and simply return the input.
 */
// NOLINTNEXTLINE(bugprone-reserved-identifier)
inline std::string _(const char *psz) {
    boost::optional<std::string> rv = translationInterface.Translate(psz);
    return rv ? (*rv) : psz;
}

void SetupEnvironment();
bool SetupNetworking();

template <typename... Args> bool error(const char *fmt, const Args &... args) {
    LogPrintf("ERROR: " + tfm::format(fmt, args...) + "\n");
    return false;
}

void PrintExceptionContinue(const std::exception *pex, const char *pszThread);
void FileCommit(FILE *file);
bool TruncateFile(FILE *file, uint64_t length);
int RaiseFileDescriptorLimit(int nMinFD);
void AllocateFileRange(FILE *file, unsigned int offset, uint64_t length);
bool RenameOver(fs::path src, fs::path dest);
bool TryCreateDirectories(const fs::path &p);
fs::path GetDefaultDataDir();
const fs::path &GetDataDir(bool fNetSpecific = true);
void ClearDatadirCache();
fs::path GetConfigFile(const std::string &confPath);
#ifndef WIN32
fs::path GetPidFile();
void CreatePidFile(const fs::path &path, pid_t pid);
#endif
#ifdef WIN32
fs::path GetSpecialFolderPath(int nFolder, bool fCreate = true);
#endif
void runCommand(const std::string &strCommand);

template <typename ITER>
std::string StringJoin(const std::string& separator, ITER begin, ITER end)
{
    std::ostringstream result;
    if (begin != end)
    {
        result << *begin;
        begin++;

        while (begin != end)
        {
            result << separator << *begin;
            begin++;
        }
    }
    return result.str();
}

template <typename CONTAINER>
std::string StringJoin(const std::string& separator, const CONTAINER& cont)
{
    return StringJoin(separator, cont.cbegin(), cont.cend());
}

inline bool IsSwitchChar(char c) {
#ifdef WIN32
    return c == '-' || c == '/';
#else
    return c == '-';
#endif
}

class ArgsManager {
private:
    int64_t parseUnit(std::string argValue, int64_t nMultiples);

protected:
    CCriticalSection cs_args;
    std::map<std::string, std::string> mapArgs;
    std::map<std::string, std::vector<std::string>> mapMultiArgs;

public:
    // NOLINTNEXTLINE(cert-err58-cpp)
    static inline const std::array<std::string, 3> sensitiveArgs{"-rpcuser", "-rpcpassword", "-rpcauth"};

    // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays)
    void ParseParameters(int argc, const char *const argv[]);
    void ReadConfigFile(const std::string &confPath);
    std::vector<std::string> GetArgs(const std::string &strArg);
    bool IsSensitiveArg(const std::string& argName);
    std::vector<std::string> GetNonSensitiveParameters();
    void LogSetParameters();

    /**
     * Return true if the given argument has been manually set.
     *
     * @param strArg Argument to get (e.g. "-foo")
     * @return true if the argument has been set
     */
    bool IsArgSet(const std::string &strArg);

    /**
     * Return string argument or default value.
     *
     * @param strArg Argument to get (e.g. "-foo")
     * @param default (e.g. "1")
     * @return command-line argument or default value
     */
    std::string GetArg(const std::string &strArg,
                       const std::string &strDefault);

    /**
     * Return integer argument or default value.
     *
     * @param strArg Argument to get (e.g. "-foo")
     * @param default (e.g. 1)
     * @return command-line argument or default value
     */
    int64_t GetArg(const std::string &strArg, int64_t nDefault);

    /**
     * Return double argument or default value.
     *
     * @param strArg Argument to get (e.g. "-foo")
     * @param default (e.g. 2.5)
     * @return command-line argument or default value
     */
    double GetDoubleArg(const std::string &strArg, double dDefault);

    /**
     * Return integer argument or default value in bytes. It's used only for byte sized arguments.
     *
     * @param strArg Argument to get (e.g. "-foo"). 
     * @param default (e.g. 1)
     * @param multiples units (e.g. 1000). If argument without a unit represents a multiple of the unit byte 
     * (for e.g. MB), nMultiples is used to get proper value in bytes.
     * @return command-line argument or default value representing bytes.
     */
    int64_t GetArgAsBytes(const std::string& strArg, int64_t nDefault, int64_t nMultiples = 1);

    /**
     * Return boolean argument or default value.
     *
     * @param strArg Argument to get (e.g. "-foo")
     * @param default (true or false)
     * @return command-line argument or default value
     */
    bool GetBoolArg(const std::string &strArg, bool fDefault);

    /**
     * Set an argument if it doesn't already have a value.
     *
     * @param strArg Argument to set (e.g. "-foo")
     * @param strValue Value (e.g. "1")
     * @return true if argument gets set, false if it already had a value
     */
    bool SoftSetArg(const std::string &strArg, const std::string &strValue);

    /**
     * Set a boolean argument if it doesn't already have a value.
     *
     * @param strArg Argument to set (e.g. "-foo")
     * @param fValue Value (e.g. false)
     * @return true if argument gets set, false if it already had a value
     */
    bool SoftSetBoolArg(const std::string &strArg, bool fValue);

    // Forces a arg setting, used only in testing
    void ForceSetArg(const std::string &strArg, const std::string &strValue);

    // Forces a boolean arg setting, used only in testing
    void ForceSetBoolArg(const std::string &strArg, bool fValue);

    // Forces a multi arg setting, used only in testing
    void ForceSetMultiArg(const std::string &strArg,
                          const std::string &strValue);

    // Remove an arg setting, used only in testing
    void ClearArg(const std::string &strArg);
};

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
extern ArgsManager gArgs;

/**
 * Format a string to be used as group of options in help messages.
 *
 * @param message Group name (e.g. "RPC server options:")
 * @return the formatted string
 */
std::string HelpMessageGroup(const std::string &message);

/**
 * Format a string to be used as option description in help messages.
 *
 * @param option Option message (e.g. "-rpcuser=<user>")
 * @param message Option description (e.g. "Username for JSON-RPC connections")
 * @return the formatted string
 */
std::string HelpMessageOpt(const std::string &option,
                           const std::string &message);

/**
 * Return the number of physical cores available on the current system.
 * @note This does not count virtual cores, such as those provided by
 * HyperThreading when boost is newer than 1.56.
 */
int GetNumCores();

void RenameThread(const char *name);
std::string GetThreadName();

/**
 * .. and a wrapper that just calls func once
 */
template <typename Callable> void TraceThread(const char *name, Callable func) {
    std::string s = strprintf("%s", name);
    RenameThread(s.c_str());
    try {
        LogPrintf("%s thread start\n", name);
        func();
        LogPrintf("%s thread exit\n", name);
    } catch (const boost::thread_interrupted &) {
        LogPrintf("%s thread interrupt\n", name);
        throw;
    } catch (const std::exception &e) {
        PrintExceptionContinue(&e, name);
        throw;
    } catch (...) {
        PrintExceptionContinue(nullptr, name);
        throw;
    }
}

std::string CopyrightHolders(const std::string &strPrefix);

/**
 * A reusable average function.
 * Pre-condition: [first, last) is non-empty.
 */
template<typename InputIterator>
auto Average(InputIterator first, InputIterator last)
{
    auto rangeSize { std::distance(first, last) };
    if(rangeSize == 0)
    {
        throw std::runtime_error("0 elements for Average");
    }

    using T = typename InputIterator::value_type;
    T sum = std::accumulate(first, last, T{});
    return sum / rangeSize;
}

template <typename T>
struct AnnotatedType {
    T value = T{};
    std::optional<std::string> hint = std::nullopt;
};


#endif // BITCOIN_UTIL_H
