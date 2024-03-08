// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#ifndef BITCOIN_CLIENTVERSION_H
#define BITCOIN_CLIENTVERSION_H

#if defined(HAVE_CONFIG_H)
#include "config/bitcoin-config.h"
#else

/**
 * client versioning and copyright year
 */

//! These need to be macros, as clientversion.cpp's and bitcoin*-res.rc's voodoo
//! requires it
#define CLIENT_VERSION_MAJOR 1
#define CLIENT_VERSION_MINOR 1
#define CLIENT_VERSION_REVISION 0 
#define CLIENT_VERSION_BUILD 0

//! Set to true for release, false for prerelease or test build
#define CLIENT_VERSION_IS_RELEASE true

/**
 * Copyright year (2009-this)
 * Todo: update this when changing our copyright comments in the source
 */
#define COPYRIGHT_YEAR 2024

#endif // HAVE_CONFIG_H

/**
 * Converts the parameter X to a string after macro replacement on X has been
 * performed.
 * Don't merge these into one macro!
 */
#define STRINGIZE(X) DO_STRINGIZE(X)
#define DO_STRINGIZE(X) #X

//! Copyright string used in Windows .rc files
#define COPYRIGHT_STR                                                          \
    "2009-" STRINGIZE(COPYRIGHT_YEAR) " " COPYRIGHT_HOLDERS_FINAL

/**
 * bitcoind-res.rc includes this file, but it cannot cope with real c++ code.
 * WINDRES_PREPROC is defined to indicate that its pre-processor is running.
 * Anything other than a define should be guarded below.
 */

#if !defined(WINDRES_PREPROC)

#include <string>
#include <vector>

/**
 * Initial bitcoin has its own way of calculating the client version number
 * i.e CLIENT_VERSION. Bitcoin SV start at a very low version numbers.
 * In order to keep backward compatibility, the calculated CLIENT_VERSION
 * is shifted in the way the lowest version of Bitcoin SV is still higher 
 * than the highest calculated version in the traditional Bitcoin.
 */
// NOLINTNEXTLINE(bugprone-reserved-identifier)
const int _SV_VERSION_SHIFT = 100000000;
static const int CLIENT_VERSION = _SV_VERSION_SHIFT +
    1000000 * CLIENT_VERSION_MAJOR + 10000 * CLIENT_VERSION_MINOR +
    100 * CLIENT_VERSION_REVISION + 1 * CLIENT_VERSION_BUILD;

extern const std::string CLIENT_NAME;
extern const std::string CLIENT_BUILD;

std::string FormatFullVersion();
std::string FormatSubVersion(const std::string &name, int nClientVersion,
                             const std::vector<std::string> &comments);

#endif // WINDRES_PREPROC

#endif // BITCOIN_CLIENTVERSION_H
