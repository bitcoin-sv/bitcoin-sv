// Copyright (c) 2015-2016 The Bitcoin Core developers
// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "bench.h"
#include "crypto/sha256.h"
#include "fs.h"
#include "random.h"
#include "util.h"

#include <cstdlib>

namespace {

/**
 * RAII helper to set up a temporary data directory for benchmarks and clean it
 * up on exit.
 */
class BenchmarkDataDir
{
    fs::path m_path;

public:
    BenchmarkDataDir()
    {
        try
        {
            // Clear any cached datadir path from previous runs
            ClearDatadirCache();

            // Create a unique temporary directory
            FastRandomContext rng{GetRandHash()};
            m_path = fs::temp_directory_path() /
            strprintf("bench_bitcoin_%lu_%u",
                static_cast<unsigned long>(GetTimeMicros()),  // microseconds
                static_cast<unsigned>(rng.randrange(1000000))); // 0-999999

            if(!fs::create_directories(m_path))
            {
                throw std::runtime_error("Failed to create temporary benchmark datadir: " + m_path.string());
            }

            // Point the global datadir to our temporary directory
            gArgs.ForceSetArg("-datadir", m_path.string());
        }
        catch(...)
        {
            // Clean up on failure
            if(!m_path.empty())
            {
                try { fs::remove_all(m_path); } catch (...) {}
            }
            throw;
        }
    }

    ~BenchmarkDataDir() noexcept
    {
        try
        {
            // Clean up the temporary directory
            fs::remove_all(m_path);
        }
        catch(const std::exception& e)
        {
            std::cerr << "Warning: failed to remove temporary benchmark datadir " << m_path << ": " << e.what() << std::endl;
        }
        catch(...) {}
    }

    // Non-copyable, non-movable
    BenchmarkDataDir(const BenchmarkDataDir&) = delete;
    BenchmarkDataDir& operator=(const BenchmarkDataDir&) = delete;
    BenchmarkDataDir(BenchmarkDataDir&&) = delete;
    BenchmarkDataDir& operator=(BenchmarkDataDir&&) = delete;
};

} // namespace

int main(int /*argc*/, char** /*argv*/)
{
    SHA256AutoDetect();
    RandomInit();
    SetupEnvironment();

    // don't want to write to bitcoind.log file
    GetLogger().SetPrintToDebugLog(false);

    // Set up a temporary datadir before running benchmarks.
    BenchmarkDataDir benchDataDir {};

    benchmark::BenchRunner::RunAll();
}
