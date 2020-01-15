// Copyright (c) 2017 The Bitcoin developers
// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "config.h"
#include "consensus/consensus.h"
#include "rpc/server.h"
#include "utilstrencodings.h"
#include "validation.h"

#include <univalue.h>

#include <boost/lexical_cast.hpp>

static UniValue getexcessiveblock(const Config &config,
                                  const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() != 0) {
        throw std::runtime_error(
            "getexcessiveblock\n"
            "\nReturn the excessive block size."
            "\nResult\n"
            "  excessiveBlockSize (integer) block size in bytes\n"
            "\nExamples:\n" +
            HelpExampleCli("getexcessiveblock", "") +
            HelpExampleRpc("getexcessiveblock", ""));
    }

    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("excessiveBlockSize", config.GetMaxBlockSize()));
    return ret;
}

static UniValue setexcessiveblock(Config &config,
                                  const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() != 1) {
        throw std::runtime_error(
            "setexcessiveblock blockSize\n"
            "\nSet the excessive block size. Excessive blocks will not be used "
            "in the active chain or relayed. This discourages the propagation "
            "of blocks that you consider excessively large."
            "\nResult\n"
            "  blockSize (integer) excessive block size in bytes\n"
            "\nExamples:\n" +
            HelpExampleCli("setexcessiveblock", "") +
            HelpExampleRpc("setexcessiveblock", ""));
    }

    uint64_t ebs = 0;
    if (request.params[0].isNum()) {
        ebs = request.params[0].get_int64();
    } else {
        std::string temp = request.params[0].get_str();
        if (temp[0] == '-') boost::throw_exception(boost::bad_lexical_cast());
        ebs = boost::lexical_cast<uint64_t>(temp);
    }

    // Set the new max block size.
    std::string err("Unexpected error");
    if ( !config.SetMaxBlockSize(ebs, &err)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, err);
    }

    // settingsToUserAgentString();
    std::ostringstream ret;
    ret << "Excessive Block set to ";
    if (ebs)
    {
        ret << ebs << " bytes.";
    }
    else
    {
        ret << "unlimited size.";
    }
    return UniValue(ret.str());
}

static UniValue setblockmaxsize(Config &config,
                                const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() != 1) {
        throw std::runtime_error(
            "setblockmaxsize blockSize\n"
            "\nSets maximum size of produced block."
            "\nResult\n"
            "  blockSize (integer) block size in bytes\n"
            "\nExamples:\n" +
            HelpExampleCli("setblockmaxsize", "") +
            HelpExampleRpc("setblockmaxsize", ""));
    }

    uint64_t mbs = 0;
    if (request.params[0].isNum()) {
        mbs = request.params[0].get_int64();
    } else {
        std::string temp = request.params[0].get_str();
        if (temp[0] == '-') boost::throw_exception(boost::bad_lexical_cast());
        mbs = boost::lexical_cast<uint64_t>(temp);
    }

    // Set the new max block size.
    std::string err("Unexpected error");
    if (!config.SetMaxGeneratedBlockSize(mbs, &err)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, err);
    }
    
    std::ostringstream ret;
    ret << "Maximal generated block size set to " << mbs << " bytes.";
    return UniValue(ret.str());
}

// clang-format off
static const CRPCCommand commands[] = {
    //  category            name                      actor (function)        okSafeMode
    //  ------------------- ------------------------  ----------------------  ----------
    { "network",            "getexcessiveblock",      getexcessiveblock,      true, {}},
    { "network",            "setexcessiveblock",      setexcessiveblock,      true, {"maxBlockSize"}},
    { "network",            "setblockmaxsize",        setblockmaxsize,        true, {"maxBlockSize"}},
};
// clang-format on

void RegisterABCRPCCommands(CRPCTable &tableRPC) {
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++) {
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
    }
}
