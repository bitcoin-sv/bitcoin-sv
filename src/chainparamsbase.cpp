// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "chainparamsbase.h"

#include "tinyformat.h"
#include "util.h"

#include <cassert>

const std::string CBaseChainParams::MAIN = "main";
const std::string CBaseChainParams::TESTNET = "test";
const std::string CBaseChainParams::REGTEST = "regtest";
const std::string CBaseChainParams::STN = "stn";

void AppendParamsHelpMessages(std::string &strUsage, bool debugHelp) {
    strUsage += HelpMessageGroup(_("Chain selection options:"));
    strUsage += HelpMessageOpt("-testnet", _("Use the test chain"));
    strUsage += HelpMessageOpt(
        "-regtest", "Enter regression test mode, which uses a special "
                    "chain in which blocks can be solved instantly. "
                    "This is intended for regression testing tools and app "
                    "development.");
    strUsage += HelpMessageOpt(
            "-stn", "Use the Scaling Test Network"
            );
}

CBaseChainParams::CBaseChainParams(int port, const std::string& data_dir)
    : nRPCPort{port}, strDataDir{data_dir}
{
}

static std::unique_ptr<CBaseChainParams> globalChainBaseParams;

const CBaseChainParams &BaseParams() {
    assert(globalChainBaseParams);
    return *globalChainBaseParams;
}

std::unique_ptr<CBaseChainParams>
CreateBaseChainParams(const std::string &chain) {
    if (chain == CBaseChainParams::MAIN)
        return std::unique_ptr<CBaseChainParams>(
            new CBaseChainParams(8332, ""));
    else if (chain == CBaseChainParams::TESTNET)
        return std::unique_ptr<CBaseChainParams>(
            new CBaseChainParams(18332, "testnet3"));
    else if (chain == CBaseChainParams::STN)
        return std::unique_ptr<CBaseChainParams>(
            new CBaseChainParams(9332, "stn"));
    else if (chain == CBaseChainParams::REGTEST)
        return std::unique_ptr<CBaseChainParams>(
            new CBaseChainParams(18332, "regtest"));
    else
        throw std::runtime_error(
            strprintf("%s: Unknown chain %s.", __func__, chain));
}

void SelectBaseParams(const std::string &chain) {
    globalChainBaseParams = CreateBaseChainParams(chain);
}

std::string ChainNameFromCommandLine() {
    bool fRegTest = gArgs.GetBoolArg("-regtest", false);
    bool fTestNet = gArgs.GetBoolArg("-testnet", false);
    bool fStn = gArgs.GetBoolArg("-stn", false);

    if ((fTestNet && fRegTest) || (fTestNet && fStn) || (fRegTest && fStn))
        throw std::runtime_error(
            "Invalid combination of -regtest, -stn, and -testnet.");
    if (fRegTest) return CBaseChainParams::REGTEST;
    if (fTestNet) return CBaseChainParams::TESTNET;
    if (fStn) return CBaseChainParams::STN;
    return CBaseChainParams::MAIN;
}
