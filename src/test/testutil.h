// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * Utility functions shared by unit tests
 */
#ifndef BITCOIN_TEST_TESTUTIL_H
#define BITCOIN_TEST_TESTUTIL_H

#include <chrono>

#include "fs.h"

fs::path GetTempPath();

// Wait for a certain amount of time and continuously call provided callback
// until the callback returns success or the waiting time is used up
template<typename T>
bool wait_for(T callback, std::chrono::milliseconds duration)
{
    auto start = std::chrono::steady_clock::now();

    while(duration > (std::chrono::steady_clock::now() - start))
    {
        if(callback())
        {
            return true;
        }
    }

    return false;
}

#endif // BITCOIN_TEST_TESTUTIL_H
