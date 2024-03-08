// Copyright (c) 2019 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include "primitives/block.h"

class Config; // NOLINT(cppcoreguidelines-virtual-class-destructor)
class CBlockIndex;

namespace mining
{

/**
 * The CBlockTemplate is used during the assembly of a new block.
 */
class CBlockTemplate {
private:
    CBlockRef mBlock { std::make_shared<CBlock>() };

public:
    CBlockTemplate() = default;
    CBlockTemplate(const CBlockRef& block) : mBlock{block} {}
    CBlockRef GetBlockRef() const { return mBlock; }

    std::vector<Amount> vTxFees;
};


/**
 * The Block Assembler assembles a new block. It collects transactions from the mempool, prioritizes them, and ensures
 * that all required ancestors are present.
 */
// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions)
class BlockAssembler {
public:
    BlockAssembler(const Config& config);
    virtual ~BlockAssembler() = default;

    /** Construct a new block template with coinbase to scriptPubKeyIn */
    virtual std::unique_ptr<CBlockTemplate> CreateNewBlock(const CScript& scriptPubKeyIn, CBlockIndex*& pindexPrev) = 0;

    /** Get the maximum generated block size for the current config and chain tip */
    virtual uint64_t GetMaxGeneratedBlockSize() const = 0;

    /** Get (and reset) whether we might produce an updated template */
    virtual bool GetTemplateUpdated() { return false; }

    struct BlockStats
    {
        uint64_t txCount{0};    // TxCount excluding the coinbase transaction
        uint64_t blockSize{0};  // Block size, including the coinbase transaction
    };

    /** Get the stats of the last block produced with CreateNewBlock() */
    virtual BlockStats getLastBlockStats() const = 0;

protected:
    uint64_t ComputeMaxGeneratedBlockSize(const CBlockIndex* pindexPrev) const;

    // Fill in header fields for a new block template
    void FillBlockHeader(CBlockRef& block, const CBlockIndex* pindex, const CScript& scriptPubKeyIn, const Amount& blockFees) const;

    // Keep reference to the global config
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
    const Config& mConfig;
};

using BlockAssemblerRef = std::shared_ptr<BlockAssembler>;

int64_t UpdateTime(CBlockHeader *pblock, const Config &config,
                   const CBlockIndex *pindexPrev);


}

