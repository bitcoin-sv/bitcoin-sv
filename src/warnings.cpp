// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "warnings.h"
#include "clientversion.h"
#include "sync.h"
#include "util.h"

CCriticalSection cs_warnings;
std::string strMiscWarning;
SafeModeLevel currentSafeModeLevel = SafeModeLevel::NONE;

void SetSafeModeLevel(const SafeModeLevel& safeModeLevel)
{
    LOCK(cs_warnings);
    currentSafeModeLevel = safeModeLevel;
}

SafeModeLevel GetSafeModeLevel()
{
    LOCK(cs_warnings);
    return currentSafeModeLevel;
}

void SetMiscWarning(const std::string &strWarning) {
    LOCK(cs_warnings);
    strMiscWarning = strWarning;
}

std::string GetWarnings(const std::string &strFor) {
    std::string strStatusBar;
    std::string strRPC;
    const std::string uiAlertSeperator = "<hr />";

    LOCK(cs_warnings);

    if (!CLIENT_VERSION_IS_RELEASE) {
        strStatusBar = "This is a pre-release or beta test build - use at your own "
                       "risk - do not use for mining or merchant applications";
    }

    if (gArgs.GetBoolArg("-testsafemode", DEFAULT_TESTSAFEMODE))
        strStatusBar = strRPC = "testsafemode enabled";

    // Misc warnings like out of disk space and clock is wrong
    if (strMiscWarning != "") {
        strStatusBar = strMiscWarning;
    }

    switch (currentSafeModeLevel)
    {
    case SafeModeLevel::VALID:
        strStatusBar = strRPC = "Warning: The network does not appear to fully "
                                "agree! Some miners appear to be experiencing "
                                "issues. A large valid fork has been detected.";
        break;
    case SafeModeLevel::INVALID:
        strStatusBar = strRPC = "Warning: We do not appear to fully agree with "
                                "our peers! You may need to upgrade, or other "
                                "nodes may need to upgrade. A large invalid fork "
                                "has been detected.";
        break;
    case SafeModeLevel::UNKNOWN:
        strStatusBar = strRPC = "Warning: The network does not appear to fully "
                                "agree! We received headers of a large fork. " 
                                "Still waiting for block data for more details.";
        break;
    case SafeModeLevel::NONE:
        break;
    }

    if (strFor == "statusbar")
        return strStatusBar;
    else if (strFor == "rpc")
        return strRPC;
    assert(!"GetWarnings(): invalid parameter");
    return "error";
}
