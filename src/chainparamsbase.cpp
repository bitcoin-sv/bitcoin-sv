// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "chainparamsbase.h"

#include "tinyformat.h"
#include <cassert>

const std::string CBaseChainParams::MAIN = "main";
const std::string CBaseChainParams::TESTNET = "test";
const std::string CBaseChainParams::REGTEST = "regtest";
const std::string CBaseChainParams::STN = "stn";

static std::unique_ptr<CBaseChainParams> globalChainBaseParams;

CBaseChainParams::CBaseChainParams(int port, const std::string& data_dir)
    : nRPCPort{port}, strDataDir{data_dir}
{
}

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
