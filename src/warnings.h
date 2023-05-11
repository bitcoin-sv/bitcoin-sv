// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_WARNINGS_H
#define BITCOIN_WARNINGS_H

#include <cstdint>
#include <cstdlib>
#include <string>

enum class SafeModeLevel : uint32_t
{
    /**
     * No safe mode
     */
    NONE = 0,

    /**
     * Indicates there is a large fork that causes node to enter safe mode but
     * we have only block headers so we don't know if this is valid or invalid fork.
     */
     UNKNOWN = 1,

     /**
      * Indicates there is an invalid large fork that causes node to enter safe mode.
      */
      INVALID = 2,

      /**
       * Indicates there is a valid large fork that causes node to enter safe mode.
       */
       VALID = 3,
};

void SetSafeModeLevel(const SafeModeLevel& safeModeLevel);
SafeModeLevel GetSafeModeLevel();

void SetMiscWarning(const std::string &strWarning);
std::string GetWarnings(const std::string &strFor);

static const bool DEFAULT_TESTSAFEMODE = false;

#endif //  BITCOIN_WARNINGS_H
