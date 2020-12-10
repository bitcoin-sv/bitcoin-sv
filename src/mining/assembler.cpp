// Copyright (c) 2019 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include <chainparams.h>
#include <config.h>
#include <mining/assembler.h>
#include <timedata.h>
#include <util.h>
#include <validation.h>
#include <versionbits.h>

namespace mining {

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
void BlockAssembler::FillBlockHeader(CBlockRef& block, const CBlockIndex* pindex,
                                     const CScript& scriptPubKeyIn, const Amount& blockFees) const
{
    const CChainParams& chainparams { mConfig.GetChainParams() };

    // Create coinbase transaction
    int32_t blockHeight { pindex->nHeight + 1 };
    CMutableTransaction coinbaseTx {};
    coinbaseTx.vin.resize(1);
    coinbaseTx.vin[0].prevout = COutPoint{};
    coinbaseTx.vout.resize(1);
    coinbaseTx.vout[0].scriptPubKey = scriptPubKeyIn;
    coinbaseTx.vout[0].nValue = blockFees + GetBlockSubsidy(blockHeight, chainparams.GetConsensus());
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

int64_t UpdateTime(CBlockHeader *pblock, const Config &config,
                   const CBlockIndex *pindexPrev) {
    int64_t nOldTime = pblock->nTime;
    int64_t nNewTime =
        std::max(pindexPrev->GetMedianTimePast() + 1, GetAdjustedTime());

    if (nOldTime < nNewTime) {
        pblock->nTime = nNewTime;
    }

    const Consensus::Params &consensusParams =
        config.GetChainParams().GetConsensus();

    // Updating time can change work required on testnet:
    if (consensusParams.fPowAllowMinDifficultyBlocks) {
        pblock->nBits = GetNextWorkRequired(pindexPrev, pblock, config);
    }

    return nNewTime - nOldTime;
}

} // namespace mining

