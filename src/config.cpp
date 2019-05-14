// Copyright (c) 2017 The Bitcoin developers
// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "config.h"
#include "chainparams.h"
#include "consensus/consensus.h"
#include "validation.h"

GlobalConfig::GlobalConfig() {
    Reset();
}

void GlobalConfig::Reset()
{
    useCashAddr = false;
    excessUTXOCharge = Amount {};
    feePerKB = CFeeRate {};
    blockPriorityPercentage = DEFAULT_BLOCK_PRIORITY_PERCENTAGE;
    preferredBlockFileSize = DEFAULT_PREFERRED_BLOCKFILE_SIZE;
    
    setDefaultBlockSizeParamsCalled = false;

    blockSizeActivationTime = 0;
    maxBlockSizeBefore = 0;
    maxBlockSizeAfter = 0;
    maxBlockSizeOverridden = false;
    maxGeneratedBlockSizeBefore = 0;
    maxGeneratedBlockSizeAfter = 0;
    maxGeneratedBlockSizeOverridden =  false;

    dataCarrierSize = DEFAULT_DATA_CARRIER_SIZE;
    limitDescendantCount = DEFAULT_DESCENDANT_LIMIT;
    limitAncestorCount = DEFAULT_ANCESTOR_LIMIT;
    limitDescendantSize = DEFAULT_DESCENDANT_SIZE_LIMIT;
    limitAncestorSize = DEFAULT_ANCESTOR_SIZE_LIMIT;

}

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
    setDefaultBlockSizeParamsCalled = true;
}

void GlobalConfig::CheckSetDefaultCalled() const
{
    if (!setDefaultBlockSizeParamsCalled)
    {
        // If you hit this we created new instance of GlobalConfig without 
        // setting defaults
        throw std::runtime_error(
            "GlobalConfig::SetDefaultBlockSizeParams must be called before accessing block size related parameters");
    }
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
    CheckSetDefaultCalled();
    return maxBlockSizeAfter;
}

uint64_t GlobalConfig::GetMaxBlockSize(int64_t nMedianTimePast) const {
    CheckSetDefaultCalled();
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
    CheckSetDefaultCalled();
    return maxGeneratedBlockSizeAfter;
};

uint64_t GlobalConfig::GetMaxGeneratedBlockSize(int64_t nMedianTimePast) const {
    CheckSetDefaultCalled();
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
    CheckSetDefaultCalled();
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

void GlobalConfig::SetDataCarrierSize(uint64_t dataCarrierSizeIn) {
    dataCarrierSize = dataCarrierSizeIn;
}

uint64_t GlobalConfig::GetDataCarrierSize() const {
    return dataCarrierSize;
}

void GlobalConfig::SetLimitAncestorSize(uint64_t limitAncestorSizeIn) {
    limitAncestorSize = limitAncestorSizeIn;
}

uint64_t GlobalConfig::GetLimitAncestorSize() const {
    return limitAncestorSize;
}

void GlobalConfig::SetLimitDescendantSize(uint64_t limitDescendantSizeIn) {
    limitDescendantSize = limitDescendantSizeIn;
}

uint64_t GlobalConfig::GetLimitDescendantSize() const {
    return limitDescendantSize;
}

void GlobalConfig::SetLimitAncestorCount(uint64_t limitAncestorCountIn) {
    limitAncestorCount = limitAncestorCountIn;
}

uint64_t GlobalConfig::GetLimitAncestorCount() const {
    return limitAncestorCount;
}

void GlobalConfig::SetLimitDescendantCount(uint64_t limitDescendantCountIn) {
    limitDescendantCount = limitDescendantCountIn;
}

uint64_t GlobalConfig::GetLimitDescendantCount() const {
    return limitDescendantCount;
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
