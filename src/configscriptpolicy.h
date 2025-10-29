// Copyright (c) 2017 Amaury SÉCHET
// Copyright (c) 2019 Bitcoin Association
// Copyright (c) 2025 BSV Blockchain Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include <string>
#include <cstdint>

enum class ProtocolEra : int;

bool LessThan(
    int64_t argValue,
    std::string* err,
    const std::string& errorMessage,
    int64_t minValue);

bool LessThanZero(
    int64_t argValue,
    std::string* err,
    const std::string& errorMessage);


struct ConfigScriptPolicy {

    ConfigScriptPolicy();

    ConfigScriptPolicy(const ConfigScriptPolicy&) = default;
    ConfigScriptPolicy(ConfigScriptPolicy&&) noexcept = default;
    ConfigScriptPolicy& operator=(const ConfigScriptPolicy&) = default;
    ConfigScriptPolicy& operator=(ConfigScriptPolicy&&) noexcept = default;
    virtual ~ConfigScriptPolicy() = default;

    void ResetDefault();

    virtual uint64_t GetMaxOpsPerScript(bool isGenesisEnabled, bool isConsensus) const;
    virtual uint64_t GetMaxScriptNumLength(ProtocolEra era, bool isConsensus) const;
    virtual uint64_t GetMaxScriptSize(bool isGenesisEnabled, bool isConsensus) const;
    virtual uint64_t GetMaxPubKeysPerMultiSig(bool isGenesisEnabled, bool isConsensus) const;
    virtual uint64_t GetMaxStackMemoryUsage(bool isGenesisEnabled, bool isConsensus) const;

    int32_t GetGenesisActivationHeight() const;
    int32_t GetChronicleActivationHeight() const;
    uint64_t GetGenesisGracefulPeriod() const;
    uint64_t GetChronicleGracefulPeriod() const;

    bool SetMaxOpsPerScriptPolicy(int64_t maxOpsPerScriptPolicyIn, std::string* error);
    bool SetMaxScriptNumLengthPolicy(int64_t maxScriptNumLengthIn, std::string* err = nullptr);
    bool SetMaxScriptSizePolicy(int64_t maxScriptSizePolicyIn, std::string* err = nullptr);
    bool SetMaxPubKeysPerMultiSigPolicy(int64_t maxPubKeysPerMultiSigIn, std::string* err = nullptr);
    bool SetMaxStackMemoryUsage(int64_t maxStackMemoryUsageConsensusIn, int64_t maxStackMemoryUsagePolicyIn, std::string* err = nullptr);

    bool SetGenesisActivationHeight(int32_t genesisActivationHeightIn, std::string* err = nullptr);
    bool SetChronicleActivationHeight(int32_t chronicleActivationHeightIn, std::string* err = nullptr);
    bool SetGenesisGracefulPeriod(int64_t genesisGracefulPeriodIn, std::string* err);
    bool SetChronicleGracefulPeriod(int64_t chronicleGracefulPeriodIn, std::string* err);

private:
    uint64_t maxOpsPerScriptPolicy;
    uint64_t maxScriptNumLengthPolicy;
    uint64_t maxScriptSizePolicy;
    uint64_t maxPubKeysPerMultiSig;
    uint64_t maxStackMemoryUsageConsensus;
    uint64_t maxStackMemoryUsagePolicy;

    int32_t genesisActivationHeight;
    int32_t chronicleActivationHeight;
    uint64_t genesisGracefulPeriod;
    uint64_t chronicleGracefulPeriod;
};
