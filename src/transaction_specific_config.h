// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include "config.h"

/*
* TransactionSpecificConfig class is child of GlobalConfig. It stores std::optional values of policy settings relevant for transaction validation.
* It contains custom setters for policy settings and overrides getters from GlobalConfig. If new value is set in this class it is returned through getter,
* otherwise we call getter from GlobalConfig.
* It is needed, because we want to use send transactions with custom policy settings and we do not want to override global config values.
* It is intended to be used in sendrawtransactions RPC function.
*/
class TransactionSpecificConfig : public GlobalConfig
{
public:
    TransactionSpecificConfig(const GlobalConfig& config);

    bool SetTransactionSpecificMaxTxSize(int64_t value, std::string* err = nullptr);
    uint64_t GetMaxTxSize(bool isGenesisEnabled, bool isConsensus) const override;

    void SetTransactionSpecificDataCarrierSize(uint64_t dataCarrierSize);
    uint64_t GetDataCarrierSize() const override;

    bool SetTransactionSpecificMaxScriptSizePolicy(int64_t maxScriptSizePolicyIn, std::string* err = nullptr);
    uint64_t GetMaxScriptSize(bool isGenesisEnabled, bool isConsensus) const override;

    bool SetTransactionSpecificMaxScriptNumLengthPolicy(int64_t maxScriptNumLengthIn, std::string* err = nullptr);
    uint64_t GetMaxScriptNumLength(bool isGenesisEnabled, bool isConsensus) const override;

    bool SetTransactionSpecificMaxStackMemoryUsage(int64_t maxStackMemoryUsageConsensusIn, int64_t maxStackMemoryUsagePolicyIn, std::string* err = nullptr);
    uint64_t GetMaxStackMemoryUsage(bool isGenesisEnabled, bool consensus) const override;
    
    bool SetTransactionSpecificLimitAncestorCount(int64_t limitAncestorCount, std::string* err = nullptr);
    uint64_t GetLimitAncestorCount() const override;

    bool SetTransactionSpecificLimitSecondaryMempoolAncestorCount(int64_t limitSecondaryMempoolAncestorCountIn, std::string* err = nullptr);
    uint64_t GetLimitSecondaryMempoolAncestorCount() const override;

    void SetTransactionSpecificAcceptNonStandardOutput(bool accept);
    bool GetAcceptNonStandardOutput(bool isGenesisEnabled) const override;

    bool SetTransactionSpecificMaxStdTxnValidationDuration(int ms, std::string* err = nullptr);
    std::chrono::milliseconds GetMaxStdTxnValidationDuration() const override;

    bool SetTransactionSpecificMaxNonStdTxnValidationDuration(int ms, std::string* err = nullptr);
    std::chrono::milliseconds GetMaxNonStdTxnValidationDuration() const override;

    bool SetTransactionSpecificMinConsolidationFactor(int64_t value, std::string* err = nullptr);
    uint64_t GetMinConsolidationFactor() const  override;

    bool SetTransactionSpecificMaxConsolidationInputScriptSize(int64_t value, std::string* err = nullptr);
    uint64_t GetMaxConsolidationInputScriptSize() const  override;

    bool SetTransactionSpecificMinConfConsolidationInput(int64_t value, std::string* err = nullptr);
    uint64_t GetMinConfConsolidationInput() const override;

    bool SetTransactionSpecificAcceptNonStdConsolidationInput(bool flagValue, std::string* err = nullptr);
    bool GetAcceptNonStdConsolidationInput() const  override;

    bool SetTransactionSpecificDustLimitFactor(int64_t factor, std::string* err = nullptr);
    int64_t GetDustLimitFactor() const override;

    void SetTransactionSpecificDustRelayFee(CFeeRate amt);
    CFeeRate GetDustRelayFee() const override;

    void SetTransactionSpecificDataCarrier(bool dataCarrier);
    bool GetDataCarrier() const override;

    bool SetTransactionSpecificMaxTxnValidatorAsyncTasksRunDuration(int ms, std::string* err);
    std::chrono::milliseconds GetMaxTxnValidatorAsyncTasksRunDuration() const override;

    bool SetTransactionSpecificSkipScriptFlags(int skipScriptFlags, std::string* err = nullptr);
    uint32_t GetSkipScriptFlags() const;

private:
    std::optional<uint64_t> mMaxTxSize;
    std::optional<uint64_t> mDataCarrierSize;
    std::optional<uint64_t> mMaxScriptSize;
    std::optional<uint64_t> mMaxScriptNumLength;
    std::optional<uint64_t> mMaxStackMemoryUsageConsensus;
    std::optional<uint64_t> mMaxStackMemoryUsagePolicy;
    std::optional<uint64_t> mLimitAncestorCount;
    std::optional<uint64_t> mLimitCPFPGroupMembersCount;
    std::optional<bool> mAcceptNonStdOutputs;
    std::optional<std::chrono::milliseconds> mMaxStdTxnValidationDuration;
    std::optional<std::chrono::milliseconds> mMaxNonStdTxnValidationDuration;
    std::optional<std::chrono::milliseconds> mMaxTxnValidatorAsyncTasksRunDuration;
    std::optional<uint64_t> mMinColsolidationFactor;
    std::optional<uint64_t> mMaxConsolidationInputScriptSize;
    std::optional<uint64_t> mMinConsolidationInput;
    std::optional<bool> mAcceptNonStdConsoldationInput;
    std::optional<uint64_t> mDustLimitFactor;
    std::optional<CFeeRate> mDustRelayFee;
    std::optional<bool> mDataCarrier;
    uint32_t mSkipScriptFlags{0};
};
