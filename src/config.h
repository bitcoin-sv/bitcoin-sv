// Copyright (c) 2017 Amaury SÉCHET
// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#ifndef BITCOIN_CONFIG_H
#define BITCOIN_CONFIG_H

#include "amount.h"
#include "consensus/consensus.h"
#include "validation.h"
#include "policy/policy.h"
#include "script/standard.h"
#include "validation.h"

#include <boost/noncopyable.hpp>

#include <cstdint>
#include <memory>
#include <string>

class CChainParams;
struct DefaultBlockSizeParams;

class Config : public boost::noncopyable {
public:
    // used to specify default block size related parameters
    virtual void SetDefaultBlockSizeParams(const DefaultBlockSizeParams &params) = 0;
    
    virtual bool SetMaxBlockSize(uint64_t maxBlockSize) = 0;
    virtual uint64_t GetMaxBlockSize() const = 0;
    virtual uint64_t GetMaxBlockSize(int64_t nMedianTimePast) const = 0;
    virtual bool MaxBlockSizeOverridden() const = 0;
    
    virtual bool SetMaxGeneratedBlockSize(uint64_t maxGeneratedBlockSize) = 0;
    virtual uint64_t GetMaxGeneratedBlockSize() const = 0;
    virtual uint64_t GetMaxGeneratedBlockSize(int64_t nMedianTimePast) const = 0;
    virtual bool MaxGeneratedBlockSizeOverridden() const = 0;

    virtual bool SetBlockSizeActivationTime(int64_t activationTime) = 0;
    virtual int64_t GetBlockSizeActivationTime() const = 0;

    virtual bool SetBlockPriorityPercentage(int64_t blockPriorityPercentage) = 0;
    virtual uint8_t GetBlockPriorityPercentage() const = 0;
    virtual const CChainParams &GetChainParams() const = 0;

    virtual void SetExcessUTXOCharge(Amount amt) = 0;
    virtual Amount GetExcessUTXOCharge() const = 0;

    virtual void SetMinFeePerKB(CFeeRate amt) = 0;
    virtual CFeeRate GetMinFeePerKB() const = 0;

    virtual void SetPreferredBlockFileSize(uint64_t preferredBlockFileSize) = 0;
    virtual uint64_t GetPreferredBlockFileSize() const = 0;

    virtual void SetDataCarrierSize(uint64_t dataCarrierSize) = 0;
    virtual uint64_t GetDataCarrierSize() const = 0;

    virtual void SetLimitAncestorSize(uint64_t limitAncestorSize) = 0;
    virtual uint64_t GetLimitAncestorSize() const = 0;

    virtual void SetLimitDescendantSize(uint64_t limitDescendantSize) = 0;
    virtual uint64_t GetLimitDescendantSize() const = 0;

    virtual void SetLimitAncestorCount(uint64_t limitAncestorCount) = 0;
    virtual uint64_t GetLimitAncestorCount() const = 0;

    virtual void SetLimitDescendantCount(uint64_t limitDescendantCount) = 0;
    virtual uint64_t GetLimitDescendantCount() const = 0;

    virtual void SetTestBlockCandidateValidity(bool test) = 0;
    virtual bool GetTestBlockCandidateValidity() const = 0;
};

class GlobalConfig final : public Config {
public:
    GlobalConfig();

    // Set block size related default. This must be called after constructing GlobalConfig
    void SetDefaultBlockSizeParams(const DefaultBlockSizeParams &params) override;

    bool SetMaxBlockSize(uint64_t maxBlockSize) override;
    uint64_t GetMaxBlockSize() const override;
    uint64_t GetMaxBlockSize(int64_t nMedianTimePast) const override;
    bool MaxBlockSizeOverridden() const override;

    bool SetMaxGeneratedBlockSize(uint64_t maxGeneratedBlockSize) override;
    uint64_t GetMaxGeneratedBlockSize() const override;
    uint64_t GetMaxGeneratedBlockSize(int64_t nMedianTimePast) const override;
    bool MaxGeneratedBlockSizeOverridden() const override;

    bool SetBlockSizeActivationTime(int64_t activationTime) override;
    int64_t GetBlockSizeActivationTime() const override;

    bool SetBlockPriorityPercentage(int64_t blockPriorityPercentage) override;
    uint8_t GetBlockPriorityPercentage() const override;
    const CChainParams &GetChainParams() const override;

    void SetExcessUTXOCharge(Amount) override;
    Amount GetExcessUTXOCharge() const override;

    void SetMinFeePerKB(CFeeRate amt) override;
    CFeeRate GetMinFeePerKB() const override;

    void SetPreferredBlockFileSize(uint64_t preferredBlockFileSize) override;
    uint64_t GetPreferredBlockFileSize() const override;

    void SetDataCarrierSize(uint64_t dataCarrierSize) override;
    uint64_t GetDataCarrierSize() const override;

    void SetLimitAncestorSize(uint64_t limitAncestorSize) override;
    uint64_t GetLimitAncestorSize() const override;

    void SetLimitDescendantSize(uint64_t limitDescendantSize) override;
    uint64_t GetLimitDescendantSize() const override;

    void SetLimitAncestorCount(uint64_t limitAncestorCount) override;
    uint64_t GetLimitAncestorCount() const override;

    void SetLimitDescendantCount(uint64_t limitDescendantCount) override;
    uint64_t GetLimitDescendantCount() const override;

    void SetTestBlockCandidateValidity(bool test) override;
    bool GetTestBlockCandidateValidity() const override;

    // Reset state of this object to match a newly constructed one.
    // Used in constructor and for unit testing to always start with a clean state
    void Reset(); 
    static GlobalConfig& GetConfig();

private:
    // All fileds are initialized in Reset()    
    Amount excessUTXOCharge;
    CFeeRate feePerKB;
    uint64_t blockPriorityPercentage;
    uint64_t preferredBlockFileSize;

    // Block size limits 
    // SetDefaultBlockSizeParams must be called before reading any of those
    bool  setDefaultBlockSizeParamsCalled;
    void  CheckSetDefaultCalled() const;

    int64_t blockSizeActivationTime;
    uint64_t maxBlockSizeBefore;
    uint64_t maxBlockSizeAfter;
    bool maxBlockSizeOverridden;
    uint64_t maxGeneratedBlockSizeBefore;
    uint64_t maxGeneratedBlockSizeAfter;
    bool maxGeneratedBlockSizeOverridden;

    uint64_t dataCarrierSize;
    uint64_t limitDescendantCount;
    uint64_t limitAncestorCount;
    uint64_t limitDescendantSize;
    uint64_t limitAncestorSize;

    bool testBlockCandidateValidity;
};

// Dummy for subclassing in unittests
class DummyConfig : public Config {
public:
    DummyConfig();
    DummyConfig(std::string net);

    void SetDefaultBlockSizeParams(const DefaultBlockSizeParams &params) override {  }

    bool SetMaxBlockSize(uint64_t maxBlockSize) override { return false; }
    uint64_t GetMaxBlockSize() const override { return 0; }
    uint64_t GetMaxBlockSize(int64_t nMedianTimePast) const override { return 0; }
    bool MaxBlockSizeOverridden() const override { return false; }

    bool SetMaxGeneratedBlockSize(uint64_t maxGeneratedBlockSize) override { return false; };
    uint64_t GetMaxGeneratedBlockSize() const override { return 0; };
    uint64_t GetMaxGeneratedBlockSize(int64_t nMedianTimePast) const override { return 0; }
    bool MaxGeneratedBlockSizeOverridden() const override { return false; }

    bool SetBlockSizeActivationTime(int64_t activationTime) override { return false; }
    int64_t GetBlockSizeActivationTime() const override { return 0; }

    bool SetBlockPriorityPercentage(int64_t blockPriorityPercentage) override {
        return false;
    }
    uint8_t GetBlockPriorityPercentage() const override { return 0; }

    void SetChainParams(std::string net);
    const CChainParams &GetChainParams() const override { return *chainParams; }

    void SetExcessUTXOCharge(Amount amt) override {}
    Amount GetExcessUTXOCharge() const override { return Amount(0); }

    void SetMinFeePerKB(CFeeRate amt) override{};
    CFeeRate GetMinFeePerKB() const override { return CFeeRate(Amount(0)); }

    void SetPreferredBlockFileSize(uint64_t preferredBlockFileSize) override {}
    uint64_t GetPreferredBlockFileSize() const override { return 0; }

    uint64_t GetDataCarrierSize() const override { return dataCarrierSize; }
    void SetDataCarrierSize(uint64_t dataCarrierSizeIn) override { dataCarrierSize = dataCarrierSizeIn; }

    void SetLimitAncestorSize(uint64_t limitAncestorSize) override {}
    uint64_t GetLimitAncestorSize() const override { return 0; }

    void SetLimitDescendantSize(uint64_t limitDescendantSize) override {}
    uint64_t GetLimitDescendantSize() const override { return 0; }

    void SetLimitAncestorCount(uint64_t limitAncestorCount) override {}
    uint64_t GetLimitAncestorCount() const override { return 0; }

    void SetLimitDescendantCount(uint64_t limitDescendantCount) override {}
    uint64_t GetLimitDescendantCount() const override { return 0; }

    void SetTestBlockCandidateValidity(bool skip) override {}
    bool GetTestBlockCandidateValidity() const override { return false; }

private:
    std::unique_ptr<CChainParams> chainParams;
    uint64_t dataCarrierSize { DEFAULT_DATA_CARRIER_SIZE };
};

#endif
