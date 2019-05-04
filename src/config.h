// Copyright (c) 2017 Amaury SÉCHET
// Copyright (c) 2019 The Bitcoin SV developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONFIG_H
#define BITCOIN_CONFIG_H

#include "amount.h"
#include "consensus/consensus.h"
#include "validation.h"
#include "policy/policy.h"

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
    virtual void SetCashAddrEncoding(bool) = 0;
    virtual bool UseCashAddrEncoding() const = 0;

    virtual void SetExcessUTXOCharge(Amount amt) = 0;
    virtual Amount GetExcessUTXOCharge() const = 0;

    virtual void SetMinFeePerKB(CFeeRate amt) = 0;
    virtual CFeeRate GetMinFeePerKB() const = 0;

    virtual void SetPreferredBlockFileSize(uint64_t preferredBlockFileSize) = 0;
    virtual uint64_t GetPreferredBlockFileSize() const = 0;
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
    void SetCashAddrEncoding(bool) override;
    bool UseCashAddrEncoding() const override;

    void SetExcessUTXOCharge(Amount) override;
    Amount GetExcessUTXOCharge() const override;

    void SetMinFeePerKB(CFeeRate amt) override;
    CFeeRate GetMinFeePerKB() const override;

    void SetPreferredBlockFileSize(uint64_t preferredBlockFileSize) override;
    uint64_t GetPreferredBlockFileSize() const override;

    // Reset state of this object to match a newly constructed one. 
    // Used in constructor and for unit testing to always start with a clean state
    void Reset(); 
    static GlobalConfig& GetConfig();

private:
    // All fileds are initialized in Reset()
    bool useCashAddr;
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

    void SetCashAddrEncoding(bool) override {}
    bool UseCashAddrEncoding() const override { return false; }

    void SetExcessUTXOCharge(Amount amt) override {}
    Amount GetExcessUTXOCharge() const override { return Amount(0); }

    void SetMinFeePerKB(CFeeRate amt) override{};
    CFeeRate GetMinFeePerKB() const override { return CFeeRate(Amount(0)); }

    void SetPreferredBlockFileSize(uint64_t preferredBlockFileSize) override {}
    uint64_t GetPreferredBlockFileSize() const override { return 0; }

private:
    std::unique_ptr<CChainParams> chainParams;
};

#endif
