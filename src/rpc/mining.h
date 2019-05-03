// Copyright (c) 2017 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_RPC_MINING_H
#define BITCOIN_RPC_MINING_H

#include "script/script.h"

#include <univalue.h>

#include <memory>

#include "consensus/params.h"

class Config;
class CBlock;

/** Generate blocks (mine) */
UniValue generateBlocks(const Config& config,
                        std::shared_ptr<CReserveScript> coinbaseScript,
                        int nGenerate, uint64_t nMaxTries, bool keepScript);

UniValue SubmitBlock(const Config& config, const std::shared_ptr<CBlock>& block);

std::string gbt_vb_name(const Consensus::DeploymentPos pos);

#endif
