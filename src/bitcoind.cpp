// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying LICENSE.

#if defined(HAVE_CONFIG_H)
#include "config/bitcoin-config.h"
#endif

#include "chainparams.h"
#include "clientversion.h"
#include "compat.h"
#include "config.h"
#include "fs.h"
#include "init.h"
#include "noui.h"
#include "scheduler.h"
#include "util.h"
#include "utilstrencodings.h"
#include <boost/algorithm/string/predicate.hpp>
#include <boost/thread.hpp>

#include <cstdio>


/* Introduction text for doxygen: */

/*! \mainpage Developer documentation
 *
 * \section intro_sec Introduction
 *
 * This is the developer documentation of Bitcoin SV
 * (https://bitcoinsv.io/). Bitcoin SV is a client for the digital
 * currency called Bitcoin SV, which enables
 * instant payments to anyone, anywhere in the world. Bitcoin SV uses
 * peer-to-peer technology to operate with no central authority: managing
 * transactions and issuing money are carried out collectively by the network.
 *
 * The software is a community-driven open source project, released under the
 * Open BSV license.
 *
 * \section Navigation
 * Use the buttons <code>Namespaces</code>, <code>Classes</code> or
 * <code>Files</code> at the top of the page to start navigating the code.
 */

void WaitForShutdown(boost::thread_group *threadGroup, const task::CCancellationToken& shutdownToken) {

    // Tell the main threads to shutdown.
    while (!shutdownToken.IsCanceled()) {
        MilliSleep(200); // NOLINT(cppcoreguidelines-avoid-magic-numbers)
    }
    if (threadGroup) {
        Interrupt(*threadGroup);
        threadGroup->join_all();
    }
}

//////////////////////////////////////////////////////////////////////////////
//
// Start
//
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
// NOLINTBEGIN(cppcoreguidelines-pro-type-vararg)
// NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays)
bool AppInit(int argc, char *argv[]) {
    RenameThread("main");
    boost::thread_group threadGroup;
    CScheduler scheduler;

    // FIXME: Ideally, we'd like to build the config here, but that's currently
    // not possible as the whole application has too many global state. However,
    // this is a first step.
    auto& config = GlobalConfig::GetModifiableGlobalConfig();

    bool fRet = false;

    //
    // Parameters
    //
    // main()
    gArgs.ParseParameters(argc, argv);

    // Process help and version before taking care about datadir
    if (gArgs.IsArgSet("-?") || gArgs.IsArgSet("-h") ||
        gArgs.IsArgSet("-help") || gArgs.IsArgSet("-version")) {
        std::string strUsage = strprintf(_("%s"), _(PACKAGE_NAME)) +
                               " " + _("version") + " " + FormatFullVersion() +
                               "\n";

        if (gArgs.IsArgSet("-version")) {
            strUsage += FormatParagraph(LicenseInfo());
        } else {
            strUsage += "\n" + _("Usage:") + "\n" +
                        "  bitcoind [options]                     " +
                        strprintf(_("Start %s Daemon"), _(PACKAGE_NAME)) + "\n";

            strUsage += "\n" + HelpMessage(HMM_BITCOIND, config);
        }

        fprintf(stdout, "%s", strUsage.c_str()); // NOLINT(cert-err33-c)
        return true;
    }

    try {
        if (!fs::is_directory(GetDataDir(false))) {
            // NOLINTNEXTLINE(cert-err33-c)
            fprintf(stderr,
                    "Error: Specified data directory \"%s\" does not exist.\n",
                    gArgs.GetArg("-datadir", "").c_str());
            return false;
        }
        try {
            gArgs.ReadConfigFile(gArgs.GetArg("-conf", BITCOIN_CONF_FILENAME));
        } catch (const std::exception &e) {
            // NOLINTNEXTLINE(cert-err33-c)
            fprintf(stderr, "Error reading configuration file: %s\n", e.what());
            return false;
        }
        // Check for -testnet or -regtest parameter (Params() calls are only
        // valid after this clause)
        try {
            SelectParams(ChainNameFromCommandLine());
        } catch (const std::exception &e) {
            // NOLINTNEXTLINE(cert-err33-c)
            fprintf(stderr, "Error: %s\n", e.what());
            return false;
        }

        // Fill config with block size data
        config.SetDefaultBlockSizeParams(Params().GetDefaultBlockSizeParams());

        // maxstackmemoryusageconsensus and excessiveblocksize are required parameters
        if (!gArgs.IsArgSet("-maxstackmemoryusageconsensus") || !gArgs.IsArgSet("-excessiveblocksize"))
        {
            // NOLINTNEXTLINE(cert-err33-c)
            fprintf(stderr, "Mandatory consensus parameter is not set. In order to start bitcoind you must set the "
                            "following consensus parameters: \"excessiveblocksize\" and "
                            "\"maxstackmemoryusageconsensus\". In order to start bitcoind with no limits you can set "
                            "both of these parameters to 0 however it is strongly recommended to ensure you understand "
                            "the implications of this setting.\n\n"
                            "For more information of how to choose these settings safely for your use case refer to: "
                            "https://bitcoinsv.io/choosing-consensus-settings/");
            return false;
        }
        if (!gArgs.IsArgSet("-minminingtxfee"))
        {
            // NOLINTNEXTLINE(cert-err33-c)
            fprintf(stderr, "Mandatory policy parameter is not set. In order to start bitcoind you must set the "
                            "following policy parameters: \"minminingtxfee\"");
            return false;
        }

        // Command-line RPC
        bool fCommandLine = false;
        for (int i = 1; i < argc; i++)
            if (!IsSwitchChar(argv[i][0]) &&
                !boost::algorithm::istarts_with(argv[i], "bitcoin:"))
                fCommandLine = true;

        if (fCommandLine) {
            // NOLINTNEXTLINE(cert-err33-c)
            fprintf(stderr, "Error: There is no RPC client functionality in "
                            "bitcoind anymore. Use the bitcoin-cli utility "
                            "instead.\n");
            exit(EXIT_FAILURE);
        }
        // -server defaults to true for bitcoind
        gArgs.SoftSetBoolArg("-server", true);
        // Set this early so that parameter interactions go to console
        InitLogging();
        InitParameterInteraction();
        if (!AppInitBasicSetup()) {
            // InitError will have been called with detailed error, which ends
            // up on console
            exit(1);
        }
        if (!AppInitParameterInteraction(config)) {
            // InitError will have been called with detailed error, which ends
            // up on console
            exit(1);
        }
        if (!AppInitSanityChecks()) {
            // InitError will have been called with detailed error, which ends
            // up on console
            exit(1);
        }
        if (gArgs.GetBoolArg("-daemon", false)) {
#if HAVE_DECL_DAEMON
            // NOLINTNEXTLINE(cert-err33-c)
            fprintf(stdout, "Bitcoin server starting\n");

            // Daemonize
            if (daemon(1, 0)) {
                // don't chdir (1), do close FDs (0)
                // NOLINTNEXTLINE(cert-err33-c)
                fprintf(stderr, "Error: daemon() failed: %s\n",
                        strerror(errno));
                return false;
            }
#else
            // NOLINTNEXTLINE(cert-err33-c)
            fprintf(
                stderr,
                "Error: -daemon is not supported on this operating system\n");
            return false;
#endif // HAVE_DECL_DAEMON
        }

        fRet = AppInitMain(config, threadGroup, scheduler, GetShutdownToken());
    } catch (const std::exception &e) {
        PrintExceptionContinue(&e, "AppInit()");
    } catch (...) {
        PrintExceptionContinue(nullptr, "AppInit()");
    }
    GetAppInitCompleted().store(true);

    if (!fRet) {
        Interrupt(threadGroup);
        // threadGroup.join_all(); was left out intentionally here, because we
        // didn't re-test all of the startup-failure cases to make sure they
        // don't result in a hang due to some
        // thread-blocking-waiting-for-another-thread-during-startup case.
    } else {
        LogPrintf("Preload wait for shutdown\n");
        WaitForShutdown(&threadGroup, GetShutdownToken());
        LogPrintf("Preload wait for shutdown done\n");
    }
    LogPrintf("Checking Thread shutdown\n");
    Shutdown();

    return fRet;
}
// NOLINTEND(cppcoreguidelines-pro-type-vararg)
// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)

int main(int argc, char *argv[]) { // NOLINT(bugprone-exception-escape)
    SetupEnvironment();

    // Connect bitcoind signal handlers
    noui_connect();

    return (AppInit(argc, argv) ? EXIT_SUCCESS : EXIT_FAILURE);
}
