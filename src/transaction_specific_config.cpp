// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "transaction_specific_config.h"

SpecificConfigScriptPolicy::SpecificConfigScriptPolicy(const ConfigScriptPolicy& cfg) : ConfigScriptPolicy(cfg){}

uint64_t SpecificConfigScriptPolicy::GetMaxScriptNumLength(ProtocolEra era, bool isConsensus) const {
    if(isConsensus || !mMaxScriptNumLength.has_value())
    {
        return ConfigScriptPolicy::GetMaxScriptNumLength(era, isConsensus);
    }

    // Return value as though it were set in GlobalConfig. This ensures we get the
    // right value for the era if policy is unlimited.
    ConfigScriptPolicy tmp{};
    //NOLINTNEXTLINE(*-narrowing-conversions)
    if(std::string err; !tmp.SetMaxScriptNumLengthPolicy(*mMaxScriptNumLength, &err))
    {
        // Someone has set the policy limit to a value incompatible with the era they
        // are then requesting it for. Assume they know what they're doing and just give
        // them back the value they set.
        return *mMaxScriptNumLength;
    }

    return ConfigScriptPolicy::GetMaxScriptNumLength(era, isConsensus);
}

uint64_t SpecificConfigScriptPolicy::GetMaxScriptSize(bool isGenesisEnabled, bool isConsensus) const{
    if(isConsensus || !isGenesisEnabled)
    {
        return ConfigScriptPolicy::GetMaxScriptSize(isGenesisEnabled, isConsensus);
    }
    return mMaxScriptSize.has_value() ? *mMaxScriptSize : ConfigScriptPolicy::GetMaxScriptSize(isGenesisEnabled, isConsensus);
}

uint64_t SpecificConfigScriptPolicy::GetMaxStackMemoryUsage(bool isGenesisEnabled, bool isConsensus) const{
    // concept of max stack memory usage is not defined before genesis
    // before Genesis stricter limitations exist, so maxStackMemoryUsage can be infinite
    if (!isGenesisEnabled)
    {
        return ConfigScriptPolicy::GetMaxStackMemoryUsage(isGenesisEnabled, isConsensus);
    }

    if (isConsensus)
    {
        return mMaxStackMemoryUsageConsensus.has_value() ? *mMaxStackMemoryUsageConsensus : ConfigScriptPolicy::GetMaxStackMemoryUsage(isGenesisEnabled, isConsensus);
    }

    return mMaxStackMemoryUsagePolicy.has_value() ? *mMaxStackMemoryUsagePolicy : ConfigScriptPolicy::GetMaxStackMemoryUsage(isGenesisEnabled, isConsensus);
}

bool SpecificConfigScriptPolicy::SetSpecificMaxScriptNumLengthPolicy(ProtocolEra era, int64_t maxScriptNumLengthIn, std::string* err){
    if(!ConfigScriptPolicy::SetMaxScriptNumLengthPolicy(maxScriptNumLengthIn, err))
    {
        return false;
    }

    mMaxScriptNumLength = ConfigScriptPolicy::GetMaxScriptNumLength(era, false);
    return true;
}

bool SpecificConfigScriptPolicy::SetSpecificMaxScriptSizePolicy(int64_t maxScriptSizePolicyIn, std::string* err){
    if(!ConfigScriptPolicy::SetMaxScriptSizePolicy(maxScriptSizePolicyIn, err))
    {
        return false;
    }

    mMaxScriptSize = ConfigScriptPolicy::GetMaxScriptSize(true, false);
    return true;
}

bool SpecificConfigScriptPolicy::SetSpecificMaxStackMemoryUsage(int64_t maxStackMemoryUsageConsensusIn, int64_t maxStackMemoryUsagePolicyIn, std::string* err){
    if(!ConfigScriptPolicy::SetMaxStackMemoryUsage(maxStackMemoryUsageConsensusIn, maxStackMemoryUsagePolicyIn, err))
    {
        return false;
    }

    mMaxStackMemoryUsageConsensus = ConfigScriptPolicy::GetMaxStackMemoryUsage(true, true);
    mMaxStackMemoryUsagePolicy = ConfigScriptPolicy::GetMaxStackMemoryUsage(true, false);
    return true;
}

uint64_t SpecificConfigScriptPolicy::GetMaxTxSize(ProtocolEra era, bool isConsensus) const{
    if(isConsensus || !IsProtocolActive(era, ProtocolName::Genesis))
    {
        return ConfigScriptPolicy::GetMaxTxSize(era, isConsensus);
    }
    return mMaxTxSize.has_value() ? *mMaxTxSize : ConfigScriptPolicy::GetMaxTxSize(era, isConsensus);
}

bool SpecificConfigScriptPolicy::SetSpecificMaxTxSizePolicy(int64_t value, std::string* err){
    if(!ConfigScriptPolicy::SetMaxTxSizePolicy(value, err))
    {
        return false;
    }

    mMaxTxSize = ConfigScriptPolicy::GetMaxTxSize(ProtocolEra::PostGenesis, false);
    return true;
}

uint64_t SpecificConfigScriptPolicy::GetDataCarrierSize() const{
    return mDataCarrierSize.has_value() ? *mDataCarrierSize : ConfigScriptPolicy::GetDataCarrierSize();
}

void SpecificConfigScriptPolicy::SetSpecificDataCarrierSize(uint64_t data_carrier_size){
    mDataCarrierSize = data_carrier_size;
}

bool SpecificConfigScriptPolicy::GetDataCarrier() const{
    return mDataCarrier.has_value() ? *mDataCarrier : ConfigScriptPolicy::GetDataCarrier();
}

void SpecificConfigScriptPolicy::SetSpecificDataCarrier(bool data_carrier){
    mDataCarrier = data_carrier;
}


//////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////
TransactionSpecificConfig::TransactionSpecificConfig(const GlobalConfig& config)
  : GlobalConfig( config.getGlobalConfigData() ), mScriptPolicysettings(config.GetConfigScriptPolicy())
{
}

const ConfigScriptPolicy& TransactionSpecificConfig::GetConfigScriptPolicy() const{
    return mScriptPolicysettings;
}

bool TransactionSpecificConfig::SetTransactionSpecificMaxTxSize(int64_t maxTxSizePolicyIn, std::string* err)
{
    return mScriptPolicysettings.SetSpecificMaxTxSizePolicy(maxTxSizePolicyIn, err);
}

uint64_t TransactionSpecificConfig::GetMaxTxSize(ProtocolEra era, bool isConsensus) const
{
    return mScriptPolicysettings.GetMaxTxSize(era, isConsensus);
}

void TransactionSpecificConfig::SetTransactionSpecificDataCarrierSize(uint64_t dataCarrierSize)
{
    mScriptPolicysettings.SetSpecificDataCarrierSize(dataCarrierSize);
};

uint64_t TransactionSpecificConfig::GetDataCarrierSize() const
{
    return mScriptPolicysettings.GetDataCarrierSize();
};

bool TransactionSpecificConfig::SetTransactionSpecificMaxScriptSizePolicy(int64_t maxScriptSizePolicyIn, std::string* err)
{
    return mScriptPolicysettings.SetSpecificMaxScriptSizePolicy(maxScriptSizePolicyIn, err);
};

uint64_t TransactionSpecificConfig::GetMaxScriptSize(bool isGenesisEnabled, bool isConsensus) const
{
    return mScriptPolicysettings.GetMaxScriptSize(isGenesisEnabled, isConsensus);
};

bool TransactionSpecificConfig::SetTransactionSpecificMaxScriptNumLengthPolicy(ProtocolEra era, int64_t maxScriptNumLengthIn, std::string* err)
{
    return mScriptPolicysettings.SetSpecificMaxScriptNumLengthPolicy(era, maxScriptNumLengthIn, err);
};

uint64_t TransactionSpecificConfig::GetMaxScriptNumLength(ProtocolEra era, bool isConsensus) const
{ 
    return mScriptPolicysettings.GetMaxScriptNumLength(era, isConsensus);
};

bool TransactionSpecificConfig::SetTransactionSpecificMaxStackMemoryUsage(int64_t maxStackMemoryUsageConsensusIn, int64_t maxStackMemoryUsagePolicyIn, std::string* err)
{
    return mScriptPolicysettings.SetSpecificMaxStackMemoryUsage(maxStackMemoryUsageConsensusIn, maxStackMemoryUsagePolicyIn, err);
};

uint64_t TransactionSpecificConfig::GetMaxStackMemoryUsage(bool isGenesisEnabled, bool consensus) const
{ 
    return mScriptPolicysettings.GetMaxStackMemoryUsage(isGenesisEnabled, consensus);
};


bool TransactionSpecificConfig::SetTransactionSpecificLimitAncestorCount(int64_t limitAncestorCountIn, std::string* err)
{
    // see comment in SetTransactionSpecificMaxTxSize
    GlobalConfig tmp;
    if(!tmp.SetLimitAncestorCount(limitAncestorCountIn, err))
    {
        return false;
    }

    mLimitAncestorCount = tmp.GetLimitAncestorCount();
    return true;
};

uint64_t TransactionSpecificConfig::GetLimitAncestorCount() const
{ 
    return mLimitAncestorCount.has_value() ? *mLimitAncestorCount : GlobalConfig::GetLimitAncestorCount();
};

bool TransactionSpecificConfig::SetTransactionSpecificLimitSecondaryMempoolAncestorCount(int64_t limitSecondaryMempoolAncestorCountIn, std::string* err)
{
     // see comment in SetTransactionSpecificMaxTxSize
    GlobalConfig tmp;
    if(!tmp.SetLimitSecondaryMempoolAncestorCount(limitSecondaryMempoolAncestorCountIn, err))
    {
        return false;
    }

    mLimitCPFPGroupMembersCount = tmp.GetLimitSecondaryMempoolAncestorCount();
    return true;
};

uint64_t TransactionSpecificConfig::GetLimitSecondaryMempoolAncestorCount() const
{
    return mLimitCPFPGroupMembersCount.has_value() ? *mLimitCPFPGroupMembersCount : GlobalConfig::GetLimitSecondaryMempoolAncestorCount();
};

void TransactionSpecificConfig::SetTransactionSpecificAcceptNonStandardOutput(bool accept)
{
    mAcceptNonStdOutputs = accept;
};

bool TransactionSpecificConfig::GetAcceptNonStandardOutput(ProtocolEra era) const
{
    bool isGenesisEnabled { IsProtocolActive(era, ProtocolName::Genesis) };
    return (mAcceptNonStdOutputs.has_value() && isGenesisEnabled) ? *mAcceptNonStdOutputs : GlobalConfig::GetAcceptNonStandardOutput(era);
};

bool TransactionSpecificConfig::SetTransactionSpecificMaxStdTxnValidationDuration(int ms, std::string* err)
{
    // see comment in SetTransactionSpecificMaxTxSize
    GlobalConfig tmp;
    if(!tmp.SetMaxStdTxnValidationDuration(ms, err))
    {
        return false;
    }

    mMaxStdTxnValidationDuration = tmp.GetMaxStdTxnValidationDuration();
    return true;
};

std::chrono::milliseconds TransactionSpecificConfig::GetMaxStdTxnValidationDuration() const
{
    return mMaxStdTxnValidationDuration.has_value() ? *mMaxStdTxnValidationDuration : GlobalConfig::GetMaxStdTxnValidationDuration();
};

bool TransactionSpecificConfig::SetTransactionSpecificMaxNonStdTxnValidationDuration(int ms, std::string* err)
{ 
    // see comment in SetTransactionSpecificMaxTxSize
    GlobalConfig tmp;
    if(!tmp.SetMaxNonStdTxnValidationDuration(ms, err))
    {
        return false;
    }

    mMaxStdTxnValidationDuration = tmp.GetMaxNonStdTxnValidationDuration();
    return true;
};

std::chrono::milliseconds TransactionSpecificConfig::GetMaxNonStdTxnValidationDuration() const
{
    return mMaxNonStdTxnValidationDuration.has_value() ? *mMaxNonStdTxnValidationDuration : GlobalConfig::GetMaxNonStdTxnValidationDuration();
};

bool TransactionSpecificConfig::SetTransactionSpecificMinConsolidationFactor(int64_t minConsolidationFactorIn, std::string* err)
{
    // see comment in SetTransactionSpecificMaxTxSize
    GlobalConfig tmp;
    if(!tmp.SetMinConsolidationFactor(minConsolidationFactorIn, err))
    {
        return false;
    }

    mMinConsolidationFactor = tmp.GetMinConsolidationFactor();
    return true;
};

uint64_t TransactionSpecificConfig::GetMinConsolidationFactor() const
{
    return mMinConsolidationFactor.has_value() ? *mMinConsolidationFactor :  GlobalConfig::GetMinConsolidationFactor();
};

bool TransactionSpecificConfig::SetTransactionSpecificMaxConsolidationInputScriptSize(int64_t maxConsolidationInputScriptSizeIn, std::string* err)
{
    // see comment in SetTransactionSpecificMaxTxSize
    GlobalConfig tmp;
    if(!tmp.SetMaxConsolidationInputScriptSize(maxConsolidationInputScriptSizeIn, err))
    {
        return false;
    }

    mMaxConsolidationInputScriptSize = tmp.GetMaxConsolidationInputScriptSize();
    return true;
};

uint64_t TransactionSpecificConfig::GetMaxConsolidationInputScriptSize() const
{
    return mMaxConsolidationInputScriptSize.has_value() ? *mMaxConsolidationInputScriptSize : GlobalConfig::GetMaxConsolidationInputScriptSize();
};

bool TransactionSpecificConfig::SetTransactionSpecificMinConfConsolidationInput(int64_t minconfIn, std::string* err)
{
    // see comment in SetTransactionSpecificMaxTxSize
    GlobalConfig tmp;
    if(!tmp.SetMinConfConsolidationInput(minconfIn, err))
    {
        return false;
    }

    mMinConsolidationInput = tmp.GetMinConfConsolidationInput();
    return true;
};
uint64_t TransactionSpecificConfig::GetMinConfConsolidationInput() const
{
    return mMinConsolidationInput.has_value() ? *mMinConsolidationInput : GlobalConfig::GetMinConfConsolidationInput();
};

bool TransactionSpecificConfig::SetTransactionSpecificAcceptNonStdConsolidationInput(bool flagValue, std::string* /*err*/)
{
    mAcceptNonStdConsolidationInput = flagValue;
    return true;
};
bool TransactionSpecificConfig::GetAcceptNonStdConsolidationInput() const
{
    return mAcceptNonStdConsolidationInput.has_value() ? *mAcceptNonStdConsolidationInput : GlobalConfig::GetAcceptNonStdConsolidationInput();
};

bool TransactionSpecificConfig::SetTransactionSpecificDustLimitFactor(int64_t factor, std::string* err)
{
    // see comment in SetTransactionSpecificMaxTxSize
    GlobalConfig tmp;
    if(!tmp.SetDustLimitFactor(factor, err))
    {
        return false;
    }

    mDustLimitFactor = tmp.GetDustLimitFactor();
    return true;
};
int64_t TransactionSpecificConfig::GetDustLimitFactor() const
{
    //NOLINTNEXTLINE(*-narrowing-conversions)
    return mDustLimitFactor.has_value() ? *mDustLimitFactor : GlobalConfig::GetDustLimitFactor();
};

void TransactionSpecificConfig::SetTransactionSpecificDustRelayFee(CFeeRate amt)
{
    mDustRelayFee = amt;
};

CFeeRate TransactionSpecificConfig::GetDustRelayFee() const
{
    return mDustRelayFee.has_value() ? *mDustRelayFee : GlobalConfig::GetDustRelayFee();
};

void TransactionSpecificConfig::SetTransactionSpecificDataCarrier(bool dataCarrier)
{
    mScriptPolicysettings.SetSpecificDataCarrier(dataCarrier);
};
bool TransactionSpecificConfig::GetDataCarrier() const
{
    return mScriptPolicysettings.GetDataCarrier();
};

bool TransactionSpecificConfig::SetTransactionSpecificMaxTxnValidatorAsyncTasksRunDuration(int ms, std::string* err)
{
    // see comment in SetTransactionSpecificMaxTxSize
    GlobalConfig tmp;
    if(!tmp.SetMaxTxnValidatorAsyncTasksRunDuration(ms, err))
    {
        return false;
    }

    mMaxTxnValidatorAsyncTasksRunDuration = tmp.GetMaxTxnValidatorAsyncTasksRunDuration();
    return true;
}

std::chrono::milliseconds TransactionSpecificConfig::GetMaxTxnValidatorAsyncTasksRunDuration() const
{
    return mMaxTxnValidatorAsyncTasksRunDuration.has_value() ? *mMaxTxnValidatorAsyncTasksRunDuration : GlobalConfig::GetMaxTxnValidatorAsyncTasksRunDuration();
}

bool TransactionSpecificConfig::SetTransactionSpecificSkipScriptFlags(int skipScriptFlags, std::string* err)
{
    if(skipScriptFlags >= 0)
    {
        mSkipScriptFlags = skipScriptFlags;
        return true;
    }

    if(err)
    {
        *err = "skipscriptflags must be a positive integer";
    }
    return false;
}

uint32_t TransactionSpecificConfig::GetSkipScriptFlags() const
{
    return mSkipScriptFlags;
}
