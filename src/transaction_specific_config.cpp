// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "transaction_specific_config.h"

TransactionSpecificConfig::TransactionSpecificConfig(const GlobalConfig& config)
  : GlobalConfig( config.getGlobalConfigData() )
{
}

bool TransactionSpecificConfig::SetTransactionSpecificMaxTxSize(int64_t maxTxSizePolicyIn, std::string* err)
{
    // To avoid duplicating code from GlobalConfig we create temporary GlobalConfig object and call getter and setter
    // for specific policy setting.
    GlobalConfig tmp;
    if(!tmp.SetMaxTxSizePolicy(maxTxSizePolicyIn, err))
    {
        return false;
    }

    mMaxTxSize = tmp.GetMaxTxSize(true, false);
    return true;
}

uint64_t TransactionSpecificConfig::GetMaxTxSize(bool isGenesisEnabled, bool isConsensus) const
{
    if (isConsensus || !isGenesisEnabled)
    {
        return GlobalConfig::GetMaxTxSize(isGenesisEnabled, isConsensus);
    }

    return mMaxTxSize.has_value() ? *mMaxTxSize : GlobalConfig::GetMaxTxSize(isGenesisEnabled, isConsensus);
}

void TransactionSpecificConfig::SetTransactionSpecificDataCarrierSize(uint64_t dataCarrierSize)
{
    mDataCarrierSize = dataCarrierSize;
};

uint64_t TransactionSpecificConfig::GetDataCarrierSize() const
{
    return mDataCarrierSize.has_value() ? *mDataCarrierSize : GlobalConfig::GetDataCarrierSize();
};

bool TransactionSpecificConfig::SetTransactionSpecificMaxScriptSizePolicy(int64_t maxScriptSizePolicyIn, std::string* err)
{
    // see comment in SetTransactionSpecificMaxTxSize 
    GlobalConfig tmp;
    if(!tmp.SetMaxScriptSizePolicy(maxScriptSizePolicyIn, err))
    {
        return false;
    }

    mMaxScriptSize = tmp.GetMaxScriptSize(true, false);
    return true;
};

uint64_t TransactionSpecificConfig::GetMaxScriptSize(bool isGenesisEnabled, bool isConsensus) const
{
    if(isConsensus || !isGenesisEnabled)
    {
        GlobalConfig::GetMaxScriptSize(isGenesisEnabled, isConsensus);
    }
    return mMaxScriptSize.has_value() ? *mMaxScriptSize : GlobalConfig::GetMaxScriptSize(isGenesisEnabled, isConsensus);
};

bool TransactionSpecificConfig::SetTransactionSpecificMaxScriptNumLengthPolicy(int64_t maxScriptNumLengthIn, std::string* err)
{
    // see comment in SetTransactionSpecificMaxTxSize
    GlobalConfig tmp;
    if(!tmp.SetMaxScriptNumLengthPolicy(maxScriptNumLengthIn, err))
    {
        return false;
    }

    mMaxScriptNumLength = tmp.GetMaxScriptNumLength(true, false);
    return true;
};

uint64_t TransactionSpecificConfig::GetMaxScriptNumLength(bool isGenesisEnabled, bool isConsensus) const
{ 
    if(isConsensus || !isGenesisEnabled)
    {
        GlobalConfig::GetMaxScriptNumLength(isGenesisEnabled, isConsensus);
    }
    return mMaxScriptNumLength.has_value() ? *mMaxScriptNumLength : GlobalConfig::GetMaxScriptNumLength(isGenesisEnabled, isConsensus);
};

bool TransactionSpecificConfig::SetTransactionSpecificMaxStackMemoryUsage(int64_t maxStackMemoryUsageConsensusIn, int64_t maxStackMemoryUsagePolicyIn, std::string* err)
{
    // To avoid duplicating code from GlobalConfig we create temporary GlobalConfig object and call getter and setter
    // for specific policy setting.
    GlobalConfig tmp;
    if(!tmp.SetMaxStackMemoryUsage(maxStackMemoryUsageConsensusIn, maxStackMemoryUsagePolicyIn, err))
    {
        return false;
    }

    mMaxStackMemoryUsageConsensus = tmp.GetMaxScriptNumLength(true, true);
    mMaxStackMemoryUsagePolicy = tmp.GetMaxStackMemoryUsage(true, false);

    return true;
};

uint64_t TransactionSpecificConfig::GetMaxStackMemoryUsage(bool isGenesisEnabled, bool consensus) const
{ 
    // concept of max stack memory usage is not defined before genesis
    // before Genesis stricter limitations exist, so maxStackMemoryUsage can be infinite
    if (!isGenesisEnabled)
    {
        return GlobalConfig::GetMaxStackMemoryUsage(isGenesisEnabled, consensus);
    }

    if (consensus)
    {
        return mMaxStackMemoryUsageConsensus.has_value() ? *mMaxStackMemoryUsageConsensus : GlobalConfig::GetMaxStackMemoryUsage(isGenesisEnabled, consensus);
    }

    return mMaxStackMemoryUsagePolicy.has_value() ? *mMaxStackMemoryUsagePolicy : GlobalConfig::GetMaxStackMemoryUsage(isGenesisEnabled, consensus);
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

bool TransactionSpecificConfig::GetAcceptNonStandardOutput(bool isGenesisEnabled) const
{
    return (mAcceptNonStdOutputs.has_value() && isGenesisEnabled) ? *mAcceptNonStdOutputs : GlobalConfig::GetAcceptNonStandardOutput(isGenesisEnabled);
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

    mMinColsolidationFactor = tmp.GetMinConsolidationFactor();
    return true;
};

uint64_t TransactionSpecificConfig::GetMinConsolidationFactor() const
{
    return mMinColsolidationFactor.has_value() ? *mMinColsolidationFactor :  GlobalConfig::GetMinConsolidationFactor();
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

bool TransactionSpecificConfig::SetTransactionSpecificAcceptNonStdConsolidationInput(bool flagValue, std::string* err)
{
    mAcceptNonStdConsoldationInput = flagValue;
    return true;
};
bool TransactionSpecificConfig::GetAcceptNonStdConsolidationInput() const
{
    return mAcceptNonStdConsoldationInput.has_value() ? *mAcceptNonStdConsoldationInput : GlobalConfig::GetAcceptNonStdConsolidationInput();
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
    mDataCarrier = dataCarrier;
};
bool TransactionSpecificConfig::GetDataCarrier() const
{
    return mDataCarrier.has_value() ? *mDataCarrier : GlobalConfig::GetDataCarrier();
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
