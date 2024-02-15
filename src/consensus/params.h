// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#ifndef BITCOIN_CONSENSUS_PARAMS_H
#define BITCOIN_CONSENSUS_PARAMS_H

#include "uint256.h"

namespace Consensus {

/**
 * Parameters that influence chain consensus.
 */
// NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
struct Params {
    uint256 hashGenesisBlock;
    int nSubsidyHalvingInterval;
    /** Block height and hash at which BIP34 becomes active */
    int32_t BIP34Height;
    uint256 BIP34Hash;
    /** Block height at which BIP65 becomes active */
    int32_t BIP65Height;
    /** Block height at which BIP66 becomes active */
    int32_t BIP66Height;
    /** Block height at which CSV (BIP68, BIP112 and BIP113) becomes active */
    int32_t CSVHeight;
    /** Block height at which UAHF kicks in */
    int32_t uahfHeight;
    /** Block height at which the new DAA becomes active */
    int32_t daaHeight;
    /** Block height at which the Genesis becomes active.
      * The specified block height is the height of the block where the new rules are active.
      * It is not the height of the last block with the old rules.
      */
    int32_t genesisHeight;
    /** Proof of work parameters */
    uint256 powLimit;
    bool fPowAllowMinDifficultyBlocks;
    bool fPowNoRetargeting;
    int64_t nPowTargetSpacing;
    int64_t nPowTargetTimespan;
    int64_t DifficultyAdjustmentInterval() const {
        return nPowTargetTimespan / nPowTargetSpacing;
    }
    uint256 nMinimumChainWork;
    uint256 defaultAssumeValid;
};
} // namespace Consensus

#endif // BITCOIN_CONSENSUS_PARAMS_H
