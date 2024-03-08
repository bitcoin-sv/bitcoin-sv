// Copyright (c) 2015 The Bitcoin Core developers
// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#ifndef BITCOIN_MERKLE
#define BITCOIN_MERKLE

#include <cstdint>
#include <vector>

#include "primitives/block.h"
#include "uint256.h"

uint256 ComputeMerkleRoot(const std::vector<uint256> &leaves,
                          bool *mutated = nullptr);
std::vector<uint256> ComputeMerkleBranch(const std::vector<uint256> &leaves,
                                         uint32_t position);
uint256 ComputeMerkleRootFromBranch(const uint256 &leaf,
                                    const std::vector<uint256> &branch,
                                    uint32_t position);

/**
 * Compute the Merkle root of the transactions in a block.
 * *mutated is set to true if a duplicated subtree was found.
 */
uint256 BlockMerkleRoot(const CBlock &block, bool *mutated = nullptr);

/**
 * Compute the Merkle branch for the tree of transactions in a block, for a
 * given position. This can be verified using ComputeMerkleRootFromBranch.
 */
std::vector<uint256> BlockMerkleBranch(const CBlock &block, uint32_t position);

#endif
