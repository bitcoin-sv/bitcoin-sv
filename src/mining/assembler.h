// Copyright (c) 2019 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#ifndef BITCOINSV_ASSEMBLER_H
#define BITCOINSV_ASSEMBLER_H

#include "primitives/block.h"

class Config;
class CBlockIndex;

/**
 * The CBlockTemplate is used during the assembly of a new block.
 */
class CBlockTemplate {
private:
    CBlockRef block;

public:
    CBlockTemplate() : block{std::make_shared<CBlock>()} {};
    CBlockRef GetBlockRef() const { return block; };

    std::vector<Amount> vTxFees;
    std::vector<int64_t> vTxSigOpsCount;
};


/**
 * The Block Assembler assembles a new block. It collects transactions from the mempool, prioritizes them, and ensures
 * that all required ancestors are present.
 */
class BlockAssembler {
protected:
    uint64_t nMaxGeneratedBlockSize {0};

public:
    /** Construct a new block template with coinbase to scriptPubKeyIn */
    virtual std::unique_ptr<CBlockTemplate> CreateNewBlock(const CScript& scriptPubKeyIn, CBlockIndex*& pindexPrev) = 0;

    uint64_t GetMaxGeneratedBlockSize() const { return nMaxGeneratedBlockSize; }
};


typedef std::shared_ptr<BlockAssembler> BlockAssemblerRef;

#endif //BITCOINSV_ASSEMBLER_H
