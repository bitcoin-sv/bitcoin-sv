// Copyright (c) 2019 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include <chainparams.h>
#include <config.h>
#include <mining/assembler.h>
#include <timedata.h>
#include <util.h>
#include <validation.h>
#include <versionbits.h>

using mining::BlockAssembler;

BlockAssembler::BlockAssembler(const Config& config)
: mConfig{config}
{
}

uint64_t BlockAssembler::ComputeMaxGeneratedBlockSize(const CBlockIndex* pindexPrev) const
{
    // Block resource limits
    uint64_t maxGeneratedBlockSize {};
    uint64_t maxBlockSize {};

    if(pindexPrev == nullptr)
    {
        maxGeneratedBlockSize = mConfig.GetMaxGeneratedBlockSize();
        maxBlockSize = mConfig.GetMaxBlockSize();
    }
    else
    {
        auto medianPastTime { pindexPrev->GetMedianTimePast() };
        maxGeneratedBlockSize = mConfig.GetMaxGeneratedBlockSize(medianPastTime);
        maxBlockSize = mConfig.GetMaxBlockSize();
    }

    // Limit size to between 1K and MaxBlockSize-1K for sanity:
    maxGeneratedBlockSize = std::max(uint64_t(ONE_KILOBYTE), std::min(maxBlockSize - ONE_KILOBYTE, maxGeneratedBlockSize));
    return maxGeneratedBlockSize;
}

// Fill in header fields for a new block template
void BlockAssembler::FillBlockHeader(CBlockRef& block, const CBlockIndex* pindex, const CScript& scriptPubKeyIn) const
{
    const CChainParams& chainparams { mConfig.GetChainParams() };

    // Create coinbase transaction
    int blockHeight { pindex->nHeight + 1 };
    CMutableTransaction coinbaseTx {};
    coinbaseTx.vin.resize(1);
    coinbaseTx.vin[0].prevout = COutPoint{};
    coinbaseTx.vout.resize(1);
    coinbaseTx.vout[0].scriptPubKey = scriptPubKeyIn;
    coinbaseTx.vout[0].nValue = mBlockFees + GetBlockSubsidy(blockHeight, chainparams.GetConsensus());
    // BIP34 only requires that the block height is available as a CScriptNum, but generally
    // miner software which reads the coinbase tx will not support SCriptNum.
    // Adding the extra 00 byte makes it look like a int32.
    coinbaseTx.vin[0].scriptSig = CScript() << blockHeight << OP_0;
    block->vtx[0] = MakeTransactionRef(coinbaseTx);

    // Fill in the block header
    block->nVersion = VERSIONBITS_TOP_BITS;
    if(chainparams.MineBlocksOnDemand())
    {
        // -regtest only: allow overriding block.nVersion with
        // -blockversion=N to test forking scenarios
        block->nVersion = gArgs.GetArg("-blockversion", block->nVersion);
    }
    block->nTime = GetAdjustedTime();
    block->hashPrevBlock = pindex->GetBlockHash();
    UpdateTime(block.get(), mConfig, pindex);
    block->nBits = GetNextWorkRequired(pindex, block.get(), mConfig);
    block->nNonce = 0;
}

