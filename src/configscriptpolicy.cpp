// Copyright (c) 2017 The Bitcoin developers
// Copyright (c) 2019 Bitcoin Association
// Copyright (c) 2025 BSV Blockchain Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "configscriptpolicy.h"
#include "consensus/consensus.h"
#include "policy/policy.h"
#include "protocol_era.h"
#include "script/standard.h"

bool LessThan(
    int64_t argValue,
    std::string* err,
    const std::string& errorMessage,
    int64_t minValue)
{
    if (argValue < minValue)
    {
        if (err)
        {
            *err = errorMessage;
        }
        return true;
    }
    return false;
}

bool LessThanZero(
    int64_t argValue,
    std::string* err,
    const std::string& errorMessage)
{
    return LessThan( argValue, err, errorMessage, 0 );
}

//NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
ConfigScriptPolicy::ConfigScriptPolicy()
{
    ResetDefault();
}

void ConfigScriptPolicy::ResetDefault(){

    maxOpsPerScriptPolicy = DEFAULT_OPS_PER_SCRIPT_POLICY_AFTER_GENESIS;
    maxScriptNumLengthPolicy = DEFAULT_SCRIPT_NUM_LENGTH_POLICY;
    maxScriptSizePolicy = DEFAULT_MAX_SCRIPT_SIZE_POLICY_AFTER_GENESIS;
    maxPubKeysPerMultiSig = DEFAULT_PUBKEYS_PER_MULTISIG_POLICY_AFTER_GENESIS;
    maxStackMemoryUsagePolicy = DEFAULT_STACK_MEMORY_USAGE_POLICY_AFTER_GENESIS;
    maxStackMemoryUsageConsensus = DEFAULT_STACK_MEMORY_USAGE_CONSENSUS_AFTER_GENESIS;

    genesisActivationHeight = 0;
    chronicleActivationHeight = 0;
    genesisGracefulPeriod = DEFAULT_GENESIS_GRACEFUL_ACTIVATION_PERIOD;
    chronicleGracefulPeriod = DEFAULT_CHRONICLE_GRACEFUL_ACTIVATION_PERIOD;

    maxTxSizePolicy = DEFAULT_MAX_TX_SIZE_POLICY_AFTER_GENESIS;
    dataCarrierSize = DEFAULT_DATA_CARRIER_SIZE;
    dataCarrier = DEFAULT_ACCEPT_DATACARRIER;
    acceptNonStandardOutput = true;
    requireStandard = true;
    permitBareMultisig = DEFAULT_PERMIT_BAREMULTISIG;
}

uint64_t ConfigScriptPolicy::GetMaxOpsPerScript(bool isGenesisEnabled, bool consensus) const
{
    if (!isGenesisEnabled)
    {
        return MAX_OPS_PER_SCRIPT_BEFORE_GENESIS; // no changes before genesis
    }

    if (consensus)
    {
        return MAX_OPS_PER_SCRIPT_AFTER_GENESIS; // use new limit after genesis
    }
    return maxOpsPerScriptPolicy;
}

uint64_t ConfigScriptPolicy::GetMaxScriptNumLength(ProtocolEra era, bool isConsensus) const
{
    const bool isGenesisActive = IsProtocolActive(era, ProtocolName::Genesis);
    const bool isChronicleActive = IsProtocolActive(era, ProtocolName::Chronicle);
    if (!isGenesisActive)
    {
        return MAX_SCRIPT_NUM_LENGTH_BEFORE_GENESIS; // no changes before genesis
    }

    if (isConsensus)
    {
        if(!isChronicleActive)
        {
            return MAX_SCRIPT_NUM_LENGTH_AFTER_GENESIS; // limit after Genesis
        }
        return MAX_SCRIPT_NUM_LENGTH_AFTER_CHRONICLE;   // Chronicle consensus limit
    }

    // Use policy limit
    if(maxScriptNumLengthPolicy == 0)
    {
        // Unlimited policy depends on consensus limit for whichever protocol is active
        if(!isChronicleActive)
        {
            return MAX_SCRIPT_NUM_LENGTH_AFTER_GENESIS;
        }
        return MAX_SCRIPT_NUM_LENGTH_AFTER_CHRONICLE;
    }

    // Configured policy limit
    return maxScriptNumLengthPolicy;
}

uint64_t ConfigScriptPolicy::GetMaxScriptSize(bool isGenesisEnabled, bool isConsensus) const {
    if (!isGenesisEnabled) 
    {
        return MAX_SCRIPT_SIZE_BEFORE_GENESIS;
    }
    if (isConsensus) 
    {
        return MAX_SCRIPT_SIZE_AFTER_GENESIS;
    }
    return maxScriptSizePolicy;
}

uint64_t ConfigScriptPolicy::GetMaxPubKeysPerMultiSig(bool isGenesisEnabled, bool consensus) const
{
    if (!isGenesisEnabled)
    {
        return MAX_PUBKEYS_PER_MULTISIG_BEFORE_GENESIS; // no changes before  genesis
    }

    if (consensus)
    {
        return MAX_PUBKEYS_PER_MULTISIG_AFTER_GENESIS; // use new limit after genesis
    }

    return maxPubKeysPerMultiSig;
}

uint64_t ConfigScriptPolicy::GetMaxStackMemoryUsage(bool isGenesisEnabled, bool consensus) const
{
    // concept of max stack memory usage is not defined before genesis
    // before Genesis stricter limitations exist, so maxStackMemoryUsage can be infinite
    if (!isGenesisEnabled)
    {
        return INT64_MAX;
    }

    if (consensus)
    {
        return maxStackMemoryUsageConsensus;
    }

    return maxStackMemoryUsagePolicy;
}

int32_t ConfigScriptPolicy::GetGenesisActivationHeight() const {
    return genesisActivationHeight;
}

int32_t ConfigScriptPolicy::GetChronicleActivationHeight() const {
    return chronicleActivationHeight;
}

bool ConfigScriptPolicy::SetMaxOpsPerScriptPolicy(int64_t maxOpsPerScriptPolicyIn, std::string* error)
{
    if (LessThanZero(maxOpsPerScriptPolicyIn, error, "Policy value for MaxOpsPerScript cannot be less than zero."))
    {
        return false;
    }
    uint64_t maxOpsPerScriptPolicyInUnsigned = static_cast<uint64_t>(maxOpsPerScriptPolicyIn);

    if (maxOpsPerScriptPolicyInUnsigned > MAX_OPS_PER_SCRIPT_AFTER_GENESIS)
    {
        if (error)
        {
            *error = "Policy value for MaxOpsPerScript must not exceed consensus limit of " + std::to_string(MAX_OPS_PER_SCRIPT_AFTER_GENESIS) + ".";
        }
        return false;
    }
    else if (maxOpsPerScriptPolicyInUnsigned == 0)
    {
        maxOpsPerScriptPolicy = MAX_OPS_PER_SCRIPT_AFTER_GENESIS;
    }
    else
    {
        maxOpsPerScriptPolicy = maxOpsPerScriptPolicyInUnsigned;
    }

    return true;
}

bool ConfigScriptPolicy::SetMaxScriptNumLengthPolicy(int64_t maxScriptNumLengthIn, std::string* err)
{
    if (LessThanZero(maxScriptNumLengthIn, err, "Policy value for maximum script number length must not be less than 0."))
    {
        return false;
    }

    uint64_t maxScriptNumLengthUnsigned = static_cast<uint64_t>(maxScriptNumLengthIn);

    if (maxScriptNumLengthUnsigned != 0 && maxScriptNumLengthUnsigned < MAX_SCRIPT_NUM_LENGTH_BEFORE_GENESIS)
    {
        if (err)
        {
            *err = "Policy value for maximum script number length must not be less than " + std::to_string(MAX_SCRIPT_NUM_LENGTH_BEFORE_GENESIS) + ".";
        }
        return false;
    }
    else
    {
        maxScriptNumLengthPolicy = maxScriptNumLengthUnsigned;
    }

    return true;
}

bool ConfigScriptPolicy::SetMaxScriptSizePolicy(int64_t maxScriptSizePolicyIn, std::string* err) {
    if (LessThanZero(maxScriptSizePolicyIn, err, "Policy value for max script size must not be less than 0"))
    {
        return false;
    }
    uint64_t maxScriptSizePolicyInUnsigned = static_cast<uint64_t>(maxScriptSizePolicyIn);
    if (maxScriptSizePolicyInUnsigned > MAX_SCRIPT_SIZE_AFTER_GENESIS)
    {
        if (err)
        {
            *err = "Policy value for max script size must not exceed consensus limit of " + std::to_string(MAX_SCRIPT_SIZE_AFTER_GENESIS);
        }
        return false;
    }
    else if (maxScriptSizePolicyInUnsigned == 0 ) {
        maxScriptSizePolicy = MAX_SCRIPT_SIZE_AFTER_GENESIS;
    }
    else
    {
        maxScriptSizePolicy = maxScriptSizePolicyInUnsigned;
    }
    return true;
}

bool ConfigScriptPolicy::SetMaxPubKeysPerMultiSigPolicy(int64_t maxPubKeysPerMultiSigIn, std::string* err)
{
    if (LessThanZero(maxPubKeysPerMultiSigIn, err, "Policy value for maximum public keys per multisig must not be less than zero"))
    {
        return false;
    }
    
    uint64_t maxPubKeysPerMultiSigUnsigned = static_cast<uint64_t>(maxPubKeysPerMultiSigIn);
    if (maxPubKeysPerMultiSigUnsigned > MAX_PUBKEYS_PER_MULTISIG_AFTER_GENESIS)
    {
        if (err)
        {
            *err = "Policy value for maximum public keys per multisig must not exceed consensus limit of " + std::to_string(MAX_PUBKEYS_PER_MULTISIG_AFTER_GENESIS) + ".";
        }
        return false;
    }
    else if (maxPubKeysPerMultiSigUnsigned == 0)
    {
        maxPubKeysPerMultiSig = MAX_PUBKEYS_PER_MULTISIG_AFTER_GENESIS;
    }
    else
    {
        maxPubKeysPerMultiSig = maxPubKeysPerMultiSigUnsigned;
    }

    return true;
}

bool ConfigScriptPolicy::SetMaxStackMemoryUsage(int64_t maxStackMemoryUsageConsensusIn, int64_t maxStackMemoryUsagePolicyIn, std::string* err)
{
    if (maxStackMemoryUsageConsensusIn < 0 || maxStackMemoryUsagePolicyIn < 0)
    {
        if (err)
        {
            *err = "Policy and consensus value for max stack memory usage must not be less than 0.";
        }
        return false;
    }

    if (maxStackMemoryUsageConsensusIn == 0)
    {
        maxStackMemoryUsageConsensus = DEFAULT_STACK_MEMORY_USAGE_CONSENSUS_AFTER_GENESIS;
    }
    else
    {
        maxStackMemoryUsageConsensus = static_cast<uint64_t>(maxStackMemoryUsageConsensusIn);
    }

    if (maxStackMemoryUsagePolicyIn == 0)
    {
        maxStackMemoryUsagePolicy = DEFAULT_STACK_MEMORY_USAGE_CONSENSUS_AFTER_GENESIS;
    }
    else
    {
        maxStackMemoryUsagePolicy = static_cast<uint64_t>(maxStackMemoryUsagePolicyIn);
    }

    if (maxStackMemoryUsagePolicy > maxStackMemoryUsageConsensus)
    {
        if (err)
        {
            *err = "Policy value of max stack memory usage must not exceed consensus limit of " + std::to_string(maxStackMemoryUsageConsensus);
        }
        return false;
    }

    return true;
}

bool ConfigScriptPolicy::SetGenesisActivationHeight(int32_t genesisActivationHeightIn, std::string* err) {
    if (genesisActivationHeightIn <= 0)
    {
        if (err)
        {
            *err = "Genesis activation height cannot be configured with a zero or negative value.";
        }
        return false;
    }
    genesisActivationHeight = genesisActivationHeightIn;
    return true;
}

bool ConfigScriptPolicy::SetGenesisGracefulPeriod(int64_t genesisGracefulPeriodIn, std::string* err)
{
    if (LessThanZero(genesisGracefulPeriodIn, err, "Value for Genesis graceful period must not be less than zero."))
    {
        return false;
    }

    uint64_t genesisGracefulPeriodUnsigned = static_cast<uint64_t>(genesisGracefulPeriodIn);
    if (genesisGracefulPeriodUnsigned > MAX_GENESIS_GRACEFUL_ACTIVATION_PERIOD)
    {
        if (err)
        {
            *err = "Value for maximum number of blocks for Genesis graceful period must not exceed the limit of " + std::to_string(MAX_GENESIS_GRACEFUL_ACTIVATION_PERIOD) + ".";
        }
        return false;
    }
    else
    {
        genesisGracefulPeriod = genesisGracefulPeriodUnsigned;
    }

    return true;

}

uint64_t ConfigScriptPolicy::GetGenesisGracefulPeriod() const
{
    return genesisGracefulPeriod;
}

bool ConfigScriptPolicy::SetChronicleActivationHeight(int32_t chronicleActivationHeightIn, std::string* err) {
    if (chronicleActivationHeightIn <= 0)
    {
        if (err)
        {
            *err = "Chronicle activation height cannot be configured with a zero or negative value.";
        }
        return false;
    }
    chronicleActivationHeight = chronicleActivationHeightIn;
    return true;
}

bool ConfigScriptPolicy::SetChronicleGracefulPeriod(int64_t chronicleGracefulPeriodIn, std::string* err)
{
    if (LessThanZero(chronicleGracefulPeriodIn, err, "Value for Chronicle graceful period must not be less than zero."))
    {
        return false;
    }

    uint64_t chronicleGracefulPeriodUnsigned = static_cast<uint64_t>(chronicleGracefulPeriodIn);
    if (chronicleGracefulPeriodUnsigned > MAX_CHRONICLE_GRACEFUL_ACTIVATION_PERIOD)
    {
        if (err)
        {
            *err = "Value for maximum number of blocks for Chronicle graceful period must not exceed the limit of " +
                std::to_string(MAX_CHRONICLE_GRACEFUL_ACTIVATION_PERIOD) + ".";
        }
        return false;
    }
    else
    {
        chronicleGracefulPeriod = chronicleGracefulPeriodUnsigned;
    }

    return true;

}

uint64_t ConfigScriptPolicy::GetChronicleGracefulPeriod() const
{
    return chronicleGracefulPeriod;
}

uint64_t ConfigScriptPolicy::GetMaxTxSize(ProtocolEra era, bool isConsensus) const
{
    if (!IsProtocolActive(era, ProtocolName::Genesis)) // no changes before genesis
    {
        if (isConsensus)
        {
            return MAX_TX_SIZE_CONSENSUS_BEFORE_GENESIS;
        }
        return MAX_TX_SIZE_POLICY_BEFORE_GENESIS;
    }

    if (isConsensus)
    {
        return MAX_TX_SIZE_CONSENSUS_AFTER_GENESIS;
    }
    return maxTxSizePolicy;
}

bool ConfigScriptPolicy::SetMaxTxSizePolicy(int64_t value, std::string* err)
{
    if (LessThanZero(value, err, "Policy value for max tx size must not be less than 0"))
    {
        return false;
    }
    if (value == 0)
    {
        maxTxSizePolicy = MAX_TX_SIZE_CONSENSUS_AFTER_GENESIS;
        return true;
    }
    uint64_t maxTxSizePolicyInUnsigned = static_cast<uint64_t>(value);
    if (maxTxSizePolicyInUnsigned > MAX_TX_SIZE_CONSENSUS_AFTER_GENESIS)
    {
        if (err)
        {
            *err = "Policy value for max tx size must not exceed consensus limit of " + std::to_string(MAX_TX_SIZE_CONSENSUS_AFTER_GENESIS);
        }
        return false;
    }
    else if (maxTxSizePolicyInUnsigned < MAX_TX_SIZE_POLICY_BEFORE_GENESIS)
    {
        if (err)
        {
            *err = "Policy value for max tx size must not be less than " + std::to_string(MAX_TX_SIZE_POLICY_BEFORE_GENESIS);
        }
        return false;
    }

    maxTxSizePolicy = maxTxSizePolicyInUnsigned;
    return true;
}

uint64_t ConfigScriptPolicy::GetDataCarrierSize() const
{
    return dataCarrierSize;
}

void ConfigScriptPolicy::SetDataCarrierSize(uint64_t dataCarrierSizeIn)
{
    dataCarrierSize = dataCarrierSizeIn;
}

bool ConfigScriptPolicy::GetDataCarrier() const
{
    return dataCarrier;
}

void ConfigScriptPolicy::SetDataCarrier(bool dataCarrierIn)
{
    dataCarrier = dataCarrierIn;
}

bool ConfigScriptPolicy::GetAcceptNonStandardOutput(ProtocolEra era) const
{
    return IsProtocolActive(era, ProtocolName::Genesis) ? acceptNonStandardOutput : !requireStandard;
}

void ConfigScriptPolicy::SetAcceptNonStandardOutput(bool accept)
{
    acceptNonStandardOutput = accept;
}

bool ConfigScriptPolicy::GetRequireStandard() const
{
    return requireStandard;
}

void ConfigScriptPolicy::SetRequireStandard(bool require)
{
    requireStandard = require;
}

bool ConfigScriptPolicy::GetPermitBareMultisig() const
{
    return permitBareMultisig;
}

void ConfigScriptPolicy::SetPermitBareMultisig(bool permit)
{
    permitBareMultisig = permit;
}
