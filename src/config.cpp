// Copyright (c) 2017 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "config.h"
#include "chainparams.h"
#include "consensus/consensus.h"
#include "validation.h"

void GlobalConfig::SetPreferredBlockFileSize(uint64_t preferredSize) {
    preferredBlockFileSize = preferredSize;
}

uint64_t GlobalConfig::GetPreferredBlockFileSize() const {
    return preferredBlockFileSize;
}

void GlobalConfig::SetDefaultBlockSizeParams(const DefaultBlockSizeParams &params) {
    blockSizeActivationTime = params.blockSizeActivationTime;
    maxBlockSizeBefore = params.maxBlockSizeBefore;
    maxBlockSizeAfter = params.maxBlockSizeAfter;
    maxBlockSizeOverridden = false;
    maxGeneratedBlockSizeBefore = params.maxGeneratedBlockSizeBefore;
    maxGeneratedBlockSizeAfter = params.maxGeneratedBlockSizeAfter;
    maxGeneratedBlockSizeOverridden = false;
}

bool GlobalConfig::SetMaxBlockSize(uint64_t maxSize) {
    // Do not allow maxBlockSize to be set below historic 1MB limit
    // It cannot be equal either because of the "must be big" UAHF rule.
    if (maxSize <= LEGACY_MAX_BLOCK_SIZE) {
        return false;
    }

    maxBlockSizeAfter = maxSize;
    maxBlockSizeOverridden = true;

    return true;
}

uint64_t GlobalConfig::GetMaxBlockSize() const {
    return maxBlockSizeAfter;
}

uint64_t GlobalConfig::GetMaxBlockSize(int64_t nMedianTimePast) const {
    uint64_t maxSize;
    if (!maxBlockSizeOverridden) {
        maxSize = nMedianTimePast >= blockSizeActivationTime ? maxBlockSizeAfter : maxBlockSizeBefore;
    }
    else {
        maxSize = maxBlockSizeAfter;
    }

    return maxSize;
}

bool GlobalConfig::MaxBlockSizeOverridden() const {
    return maxBlockSizeOverridden;
}

// Allow unit tests to control whether the max block size has been overridden
void GlobalConfig::SetMaxBlockSizeOverridden(bool overridden) {
    maxBlockSizeOverridden = overridden;
}

bool GlobalConfig::SetMaxGeneratedBlockSize(uint64_t maxSize) {
    // Check generated max size does not exceed max accepted size
    if (maxSize > maxBlockSizeAfter) {
        return false;
    }

    maxGeneratedBlockSizeAfter = maxSize;
    maxGeneratedBlockSizeOverridden = true;

    return true;
}

uint64_t GlobalConfig::GetMaxGeneratedBlockSize() const {
    return maxGeneratedBlockSizeAfter;
};

uint64_t GlobalConfig::GetMaxGeneratedBlockSize(int64_t nMedianTimePast) const {
    uint64_t maxSize;
    if (!maxGeneratedBlockSizeOverridden) {
        maxSize = nMedianTimePast >= blockSizeActivationTime ? maxGeneratedBlockSizeAfter : maxGeneratedBlockSizeBefore;
    }
    else {
        maxSize = maxGeneratedBlockSizeAfter;
    }
    return maxSize;
}
bool GlobalConfig::MaxGeneratedBlockSizeOverridden() const {
    return maxGeneratedBlockSizeOverridden;
};

bool GlobalConfig::SetBlockSizeActivationTime(int64_t activationTime) {
    blockSizeActivationTime = activationTime;
    return true;
};

int64_t GlobalConfig::GetBlockSizeActivationTime() const {
    return blockSizeActivationTime;
};

bool GlobalConfig::SetBlockPriorityPercentage(int64_t percentage) {
    // blockPriorityPercentage has to belong to [0..100]
    if ((percentage < 0) || (percentage > 100)) {
        return false;
    }
    blockPriorityPercentage = percentage;
    return true;
}

uint8_t GlobalConfig::GetBlockPriorityPercentage() const {
    return blockPriorityPercentage;
}

const CChainParams &GlobalConfig::GetChainParams() const {
    return Params();
}

GlobalConfig& GlobalConfig::GetConfig()
{
    static GlobalConfig config {};
    return config;
}

void GlobalConfig::SetCashAddrEncoding(bool c) {
    useCashAddr = c;
}
bool GlobalConfig::UseCashAddrEncoding() const {
    return useCashAddr;
}

DummyConfig::DummyConfig()
    : chainParams(CreateChainParams(CBaseChainParams::REGTEST)) {}

DummyConfig::DummyConfig(std::string net)
    : chainParams(CreateChainParams(net)) {}

void DummyConfig::SetChainParams(std::string net) {
    chainParams = CreateChainParams(net);
}

void GlobalConfig::SetExcessUTXOCharge(Amount fee) {
    excessUTXOCharge = fee;
}

Amount GlobalConfig::GetExcessUTXOCharge() const {
    return excessUTXOCharge;
}

void GlobalConfig::SetMinFeePerKB(CFeeRate fee) {
    feePerKB = fee;
}

CFeeRate GlobalConfig::GetMinFeePerKB() const {
    return feePerKB;
}
