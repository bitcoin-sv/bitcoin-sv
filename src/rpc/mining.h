// Copyright (c) 2017 The Bitcoin Core developers
// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#ifndef BITCOIN_RPC_MINING_H
#define BITCOIN_RPC_MINING_H

#include "script/script.h"

#include <univalue.h>
#include <functional>
#include <memory>

#include "consensus/params.h"

class Config;
class CBlock;

/** Generate blocks (mine) */
UniValue generateBlocks(const Config& config,
                        std::shared_ptr<CReserveScript> coinbaseScript,
                        int nGenerate, uint64_t nMaxTries, bool keepScript);

UniValue processBlock(const Config& config,
	const std::shared_ptr<CBlock>& block,
	std::function<bool(const Config&, const std::shared_ptr<CBlock>&)> performBlockOperation);

#endif
