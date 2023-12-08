// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#if defined(HAVE_CONFIG_H)
#include "config/bitcoin-config.h"
#endif

#include "chainparamsbase.h"
#include "clientversion.h"
#include "rpc/client_utils.h"
#include "rpc/protocol.h"
#include "util.h"
#include <cstdio>
#include <univalue.h>

std::string HelpMessageCli() {
    const auto defaultBaseParams =
        CreateBaseChainParams(CBaseChainParams::MAIN);
    const auto testnetBaseParams =
        CreateBaseChainParams(CBaseChainParams::TESTNET);
    std::string strUsage;
    strUsage += HelpMessageGroup(_("Options:"));
    strUsage += HelpMessageOpt("-?", _("This help message"));
    strUsage += HelpMessageOpt(
        "-conf=<file>", strprintf(_("Specify configuration file (default: %s)"),
                                  BITCOIN_CONF_FILENAME));
    strUsage += HelpMessageOpt("-datadir=<dir>", _("Specify data directory"));
    AppendParamsHelpMessages(strUsage);
    strUsage += HelpMessageGroup(_("RPC Options:"));
    strUsage += HelpMessageOpt(
        "-named",
        strprintf(_("Pass named parameters instead of positional arguments (default: %s)"),
                  DEFAULT_NAMED));
    strUsage += HelpMessageOpt(
        "-rpcconnect=<ip>",
        strprintf(_("Send commands to node running on <ip> (default: %s)"),
                  DEFAULT_RPCCONNECT));
    strUsage += HelpMessageOpt(
        "-rpcport=<port>",
        strprintf(
            _("Connect to JSON-RPC on <port> (default: %u or testnet: %u)"),
            defaultBaseParams->RPCPort(), testnetBaseParams->RPCPort()));
    strUsage += HelpMessageOpt("-rpcwait", _("Wait for RPC server to start"));
    strUsage += HelpMessageOpt("-rpcuser=<user>",
                               _("Username for JSON-RPC connections"));
    strUsage += HelpMessageOpt("-rpcpassword=<pw>",
                               _("Password for JSON-RPC connections"));
    strUsage +=
        HelpMessageOpt("-rpcclienttimeout=<n>",
                       strprintf(_("Timeout in seconds during HTTP requests, "
                                   "or 0 for no timeout. (default: %d)"),
                                 DEFAULT_HTTP_CLIENT_TIMEOUT));

    strUsage += HelpMessageOpt(
        "-stdinrpcpass",
        strprintf(_("Read RPC password from standard input as a single line.  "
                    "When combined with -stdin, the first line from standard "
                    "input is used for the RPC password.")));
    strUsage += HelpMessageOpt(
        "-stdin", _("Read extra arguments from standard input, one per line "
                    "until EOF/Ctrl-D (recommended for sensitive information "
                    "such as passphrases)"));
    strUsage += HelpMessageOpt(
        "-rpcwallet=<walletname>",
        _("Send RPC for non-default wallet on RPC server (argument is wallet "
          "filename in bitcoind directory, required if bitcoind runs with "
          "multiple wallets)"));

    return strUsage;
}

//////////////////////////////////////////////////////////////////////////////
//
// Start
//

// NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays)
int CommandLineRPC(int argc, char *argv[]) {
    std::string strPrint;
    int nRet = 0;
    try {
        // Skip switches
        while (argc > 1 && IsSwitchChar(argv[1][0])) { // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
            argc--;
            argv++;
        }
        std::string rpcPass;
        if (gArgs.GetBoolArg("-stdinrpcpass", false)) {
            if (!std::getline(std::cin, rpcPass))
                throw std::runtime_error("-stdinrpcpass specified but failed "
                                         "to read from standard input");
            gArgs.ForceSetArg("-rpcpassword", rpcPass);
        }
        std::vector<std::string> args =
            std::vector<std::string>(&argv[1], &argv[argc]); // NOLINT (cppcoreguidelines-pro-bounds-pointer-arithmetic)
        if (gArgs.GetBoolArg("-stdin", false)) {
            // Read one arg per line from stdin and append
            std::string line;
            while (std::getline(std::cin, line)) {
                args.push_back(line);
            }
        }
        if (args.size() < 1) {
            throw std::runtime_error(
                "too few parameters (need at least command)");
        }
        std::string strMethod = args[0];
        // Remove trailing method name from arguments vector
        args.erase(args.begin());

        UniValue params;
        // NOLINTNEXTLINE (bugprone-branch-clone)
        if (gArgs.GetBoolArg("-named", DEFAULT_NAMED)) {
            params = RPCConvertNamedValues(strMethod, args);
        } else {
            params = RPCConvertValues(strMethod, args);
        }

        // Execute and handle connection failures with -rpcwait
        const bool fWait = gArgs.GetBoolArg("-rpcwait", false);
        // NOLINTNEXTLINE (cppcoreguidelines-avoid-do-while)
        do {
            try {
                const UniValue reply = CallRPC(strMethod, params);

                // Parse reply
                const UniValue &result = find_value(reply, "result");
                const UniValue &error = find_value(reply, "error");

                if (!error.isNull()) {
                    // Error
                    int code = error["code"].get_int();
                    if (fWait && code == RPC_IN_WARMUP)
                        throw CConnectionFailed("server in warmup");
                    strPrint = "error: " + error.write();
                    nRet = abs(code);
                    if (error.isObject()) {
                        UniValue errCode = find_value(error, "code");
                        UniValue errMsg = find_value(error, "message");
                        strPrint =
                            errCode.isNull()
                                ? ""
                                : "error code: " + errCode.getValStr() + "\n";

                        if (errMsg.isStr()) {
                            strPrint += "error message:\n" + errMsg.get_str();
                        }

                        if (errCode.isNum() &&
                            errCode.get_int() == RPC_WALLET_NOT_SPECIFIED) {
                            strPrint += "\nTry adding "
                                        "\"-rpcwallet=<filename>\" option to "
                                        "bitcoin-cli command line.";
                        }
                    }
                } else {
                    // Result
                    if (result.isNull()) {
                        strPrint = "";
                    } else if (result.isStr()) {
                        strPrint = result.get_str();
                    } else {
                        strPrint = result.write(2);
                    }
                }
                // Connection succeeded, no need to retry.
                break;
            } catch (const CConnectionFailed &) {
                if (fWait) {
                    MilliSleep(1000);
                } else {
                    throw;
                }
            }
        } while (fWait);
    } catch (const boost::thread_interrupted &) {
        throw;
    } catch (const std::exception &e) {
        strPrint = std::string("error: ") + e.what();
        nRet = EXIT_FAILURE;
    } catch (...) {
        PrintExceptionContinue(nullptr, "CommandLineRPC()");
        throw;
    }

    if (strPrint != "") {
        // NOLINTNEXTLINE (cert-err33-c)
        fprintf((nRet == 0 ? stdout : stderr), "%s\n", strPrint.c_str()); // NOLINT (cppcoreguidelines-pro-type-vararg)
    }
    return nRet;
}

int main(int argc, char *argv[]) {
    SetupEnvironment();
    if (!SetupNetworking()) {
        // NOLINTNEXTLINE (cert-err33-c)
        fprintf(stderr, "Error: Initializing networking failed\n"); // NOLINT (cppcoreguidelines-pro-type-vararg)
        return EXIT_FAILURE;
    }

    try 
    {
       std::string appname("bitcoin-cli");
       std::string usage = "\n" + _("Usage:") + "\n" + "  " + appname + " [options] " +
                            strprintf(_("Send command to %s"), _(PACKAGE_NAME)) + "\n" + "  " + appname +
                            " [options] help                " + _("List commands") + "\n" + "  " + appname +
                            " [options] help <command>      " + _("Get help for a command") + "\n";

        int ret = AppInitRPC(argc, argv, usage, HelpMessageCli);
        if (ret != CONTINUE_EXECUTION) 
        {
            return ret;
        }
    } catch (const std::exception &e) {
        PrintExceptionContinue(&e, "AppInitRPC()");
        return EXIT_FAILURE;
    } catch (...) {
        PrintExceptionContinue(nullptr, "AppInitRPC()");
        return EXIT_FAILURE;
    }

    int ret = EXIT_FAILURE;
    try {
        ret = CommandLineRPC(argc, argv);
    } catch (const std::exception &e) {
        PrintExceptionContinue(&e, "CommandLineRPC()");
    } catch (...) {
        PrintExceptionContinue(nullptr, "CommandLineRPC()");
    }
    return ret;
}
