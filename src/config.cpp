// Copyright (c) 2017 The Bitcoin developers
// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "config.h"
#include "chainparams.h"
#include "consensus/consensus.h"
#include "validation.h"
#include "net.h"
#include "util.h"
#include <boost/algorithm/string.hpp>

GlobalConfig::GlobalConfig() {
    Reset();
}

void GlobalConfig::Reset()
{
    feePerKB = CFeeRate {};
    blockPriorityPercentage = DEFAULT_BLOCK_PRIORITY_PERCENTAGE;
    preferredBlockFileSize = DEFAULT_PREFERRED_BLOCKFILE_SIZE;
    factorMaxSendQueuesBytes = DEFAULT_FACTOR_MAX_SEND_QUEUES_BYTES;

    setDefaultBlockSizeParamsCalled = false;

    blockSizeActivationTime = 0;
    maxBlockSize = 0;
    defaultBlockSize = 0;
    maxGeneratedBlockSizeBefore = 0;
    maxGeneratedBlockSizeAfter = 0;
    maxGeneratedBlockSizeOverridden =  false;
    maxTxSizePolicy = DEFAULT_MAX_TX_SIZE_POLICY_AFTER_GENESIS;

    dataCarrierSize = DEFAULT_DATA_CARRIER_SIZE;
    limitDescendantCount = DEFAULT_DESCENDANT_LIMIT;
    limitAncestorCount = DEFAULT_ANCESTOR_LIMIT;
    limitDescendantSize = DEFAULT_DESCENDANT_SIZE_LIMIT;
    limitAncestorSize = DEFAULT_ANCESTOR_SIZE_LIMIT;

    testBlockCandidateValidity = false;
    blockAssemblerType = mining::DEFAULT_BLOCK_ASSEMBLER_TYPE;

    genesisActivationHeight = 0;

    mMaxConcurrentAsyncTasksPerNode = DEFAULT_NODE_ASYNC_TASKS_LIMIT;

    mMaxParallelBlocks = DEFAULT_SCRIPT_CHECK_POOL_SIZE;
    mPerBlockScriptValidatorThreadsCount = DEFAULT_SCRIPTCHECK_THREADS;
    mPerBlockScriptValidationMaxBatchSize = DEFAULT_SCRIPT_CHECK_MAX_BATCH_SIZE;
    maxOpsPerScriptPolicy = DEFAULT_OPS_PER_SCRIPT_POLICY_AFTER_GENESIS;
    maxTxSigOpsCountPolicy = DEFAULT_TX_SIGOPS_COUNT_POLICY_AFTER_GENESIS;
    maxPubKeysPerMultiSig = DEFAULT_PUBKEYS_PER_MULTISIG_POLICY_AFTER_GENESIS;

    mMaxStdTxnValidationDuration = DEFAULT_MAX_STD_TXN_VALIDATION_DURATION;
    mMaxNonStdTxnValidationDuration = DEFAULT_MAX_NON_STD_TXN_VALIDATION_DURATION;

    maxStackMemoryUsagePolicy = DEFAULT_STACK_MEMORY_USAGE_POLICY_AFTER_GENESIS;
    maxStackMemoryUsageConsensus = DEFAULT_STACK_MEMORY_USAGE_CONSENSUS_AFTER_GENESIS;
    maxScriptSizePolicy = DEFAULT_MAX_SCRIPT_SIZE_POLICY_AFTER_GENESIS;

    maxScriptNumLengthPolicy = DEFAULT_SCRIPT_NUM_LENGTH_POLICY_AFTER_GENESIS;
    genesisGracefulPeriod = DEFAULT_GENESIS_GRACEFULL_ACTIVATION_PERIOD;

    mAcceptNonStandardOutput = true;

    mMaxCoinsViewCacheSize = 0;
}

void GlobalConfig::SetPreferredBlockFileSize(uint64_t preferredSize) {
    preferredBlockFileSize = preferredSize;
}

uint64_t GlobalConfig::GetPreferredBlockFileSize() const {
    return preferredBlockFileSize;
}

void GlobalConfig::SetDefaultBlockSizeParams(const DefaultBlockSizeParams &params) {
    blockSizeActivationTime = params.blockSizeActivationTime;
    maxBlockSize = params.maxBlockSize;
    defaultBlockSize = maxBlockSize;
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

bool GlobalConfig::SetMaxBlockSize(uint64_t maxSize, std::string* err) {
    // Do not allow maxBlockSize to be set below historic 1MB limit
    // It cannot be equal either because of the "must be big" UAHF rule.
    if (maxSize && maxSize <= LEGACY_MAX_BLOCK_SIZE) {
        if (err)
            *err = _("Excessive block size (excessiveblocksize) must be larger than ") + std::to_string(LEGACY_MAX_BLOCK_SIZE);
        return false;
    }

    // Unlimited value depends on each definition of CChainParams
    maxBlockSize = maxSize ? maxSize : defaultBlockSize;

    return true;
}

uint64_t GlobalConfig::GetMaxBlockSize() const {
    CheckSetDefaultCalled();
    return maxBlockSize;
}

void GlobalConfig::SetFactorMaxSendQueuesBytes(uint64_t factorMaxSendQueuesBytesIn) {
    factorMaxSendQueuesBytes = factorMaxSendQueuesBytesIn;
}

uint64_t GlobalConfig::GetFactorMaxSendQueuesBytes() const {
    return factorMaxSendQueuesBytes;
}

uint64_t GlobalConfig::GetMaxSendQueuesBytes() const {

    // Use the "after upgrade" excessive block size to determine the maximum size of 
    // block related messages that we are prepared to queue
    uint64_t maxBlockSize = GetMaxBlockSize();
    if (factorMaxSendQueuesBytes > UINT64_MAX / maxBlockSize)
    {
        return UINT64_MAX;
    }
    return factorMaxSendQueuesBytes * maxBlockSize;
}

bool GlobalConfig::SetMaxGeneratedBlockSize(uint64_t maxSize, std::string* err) {
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

bool GlobalConfig::SetBlockSizeActivationTime(int64_t activationTime, std::string* err) {
    blockSizeActivationTime = activationTime;
    return true;
};

int64_t GlobalConfig::GetBlockSizeActivationTime() const {
    CheckSetDefaultCalled();
    return blockSizeActivationTime;
};

bool GlobalConfig::SetBlockPriorityPercentage(int64_t percentage, std::string* err) {
    // blockPriorityPercentage has to belong to [0..100]
    if ((percentage < 0) || (percentage > 100)) {
        if (err)
            *err = _("Block priority percentage has to belong to the [0..100] interval.");
        return false;
    }
    blockPriorityPercentage = percentage;
    return true;
}

uint8_t GlobalConfig::GetBlockPriorityPercentage() const {
    return blockPriorityPercentage;
}

bool GlobalConfig::SetMaxTxSizePolicy(int64_t maxTxSizePolicyIn, std::string* err)
{
    if (maxTxSizePolicyIn < 0)
    {
        if (err)
        {
            *err = "Policy value for max tx size must not be less than 0";
        }
        return false;
    }
    if (maxTxSizePolicyIn == 0)
    {
        maxTxSizePolicy = MAX_TX_SIZE_CONSENSUS_AFTER_GENESIS;
        return true;
    }
    uint64_t maxTxSizePolicyInUnsigned = static_cast<uint64_t>(maxTxSizePolicyIn);
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

uint64_t GlobalConfig::GetMaxTxSize(bool isGenesisEnabled, bool isConsensus) const
{
    if (!isGenesisEnabled) // no changes before genesis
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

bool GlobalConfig::SetMaxPubKeysPerMultiSigPolicy(int64_t maxPubKeysPerMultiSigIn, std::string* err)
{
    if (maxPubKeysPerMultiSigIn < 0)
    {
        if (err)
        {
            *err = "Policy value for maximum public keys per multisig must not be less than zero";
        }
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

uint64_t GlobalConfig::GetMaxPubKeysPerMultiSig(bool isGenesisEnabled, bool consensus) const
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

bool GlobalConfig::SetGenesisGracefulPeriod(int64_t genesisGracefulPeriodIn, std::string* err)
{
    if (genesisGracefulPeriodIn < 0)
    {
        if (err)
        {
            *err = "Value for Genesis graceful period must not be less than zero.";
        }
        return false;
    }

    uint64_t genesisGracefulPeriodUnsigned = static_cast<uint64_t>(genesisGracefulPeriodIn);
    if (genesisGracefulPeriodUnsigned > MAX_GENESIS_GRACEFULL_ACTIVATION_PERIOD)
    {
        if (err)
        {
            *err = "Value for maximum number of blocks for Genesis graceful period must not exceed the limit of " + std::to_string(MAX_GENESIS_GRACEFULL_ACTIVATION_PERIOD) + ".";
        }
        return false;
    }
    else
    {
        genesisGracefulPeriod = genesisGracefulPeriodUnsigned;
    }

    return true;

}

uint64_t GlobalConfig::GetGenesisGracefulPeriod() const
{
    return genesisGracefulPeriod;
}

GlobalConfig& GlobalConfig::GetConfig()
{
    static GlobalConfig config {};
    return config;
}

void GlobalConfig::SetTestBlockCandidateValidity(bool test) {
    testBlockCandidateValidity = test;
}

bool GlobalConfig::GetTestBlockCandidateValidity() const {
    return testBlockCandidateValidity;
}

void GlobalConfig::SetMiningCandidateBuilder(mining::CMiningFactory::BlockAssemblerType type) {
    blockAssemblerType = type;
}

mining::CMiningFactory::BlockAssemblerType GlobalConfig::GetMiningCandidateBuilder() const {
    return blockAssemblerType;
}

bool GlobalConfig::SetGenesisActivationHeight(int64_t genesisActivationHeightIn, std::string* err) {
    if (genesisActivationHeightIn <= 0)
    {
        if (err)
        {
            *err = "Genesis activation height cannot be configured with a zero or negative value.";
        }
        return false;
    }
    genesisActivationHeight = static_cast<uint64_t>(genesisActivationHeightIn);
    return true;
}

uint64_t GlobalConfig::GetGenesisActivationHeight() const {
    return genesisActivationHeight;
}

bool GlobalConfig::SetMaxConcurrentAsyncTasksPerNode(
    int maxConcurrentAsyncTasksPerNode,
    std::string* error)
{
    if (maxConcurrentAsyncTasksPerNode < 1
        || maxConcurrentAsyncTasksPerNode > mMaxParallelBlocks)
    {
        if(error)
        {
            *error =
                strprintf(
                _("Max parallel tasks per node count must be at least 1 and at most"
                    " maxParallelBlocks"));
        }

        return false;
    }

    mMaxConcurrentAsyncTasksPerNode = maxConcurrentAsyncTasksPerNode;

    return true;
}

int GlobalConfig::GetMaxConcurrentAsyncTasksPerNode() const
{
    return mMaxConcurrentAsyncTasksPerNode;
}

bool GlobalConfig::SetBlockScriptValidatorsParams(
    int maxParallelBlocks,
    int perValidatorThreadsCount,
    int perValidatorThreadMaxBatchSize,
    std::string* error)
{
    {
        constexpr int max = 100;
        if (maxParallelBlocks < 1 || maxParallelBlocks > max)
        {
            if(error)
            {
                *error =
                    strprintf(
                    _("Max parallel blocks count must be at least 1 and at most %d"),
                    max);
            }

            return false;
        }

        mMaxParallelBlocks = maxParallelBlocks;

        // limit dependent variable
        mMaxConcurrentAsyncTasksPerNode =
            std::min(mMaxConcurrentAsyncTasksPerNode, mMaxParallelBlocks);
    }

    {
        // perValidatorThreadsCount==0 means autodetect,
        // but nScriptCheckThreads==0 means no concurrency
        if (perValidatorThreadsCount == 0)
        {
            perValidatorThreadsCount =
                std::clamp(GetNumCores(), 0, MAX_SCRIPTCHECK_THREADS);
        }
        else if (perValidatorThreadsCount < 0
            || perValidatorThreadsCount > MAX_SCRIPTCHECK_THREADS)
        {
            if(error)
            {
                *error =
                    strprintf(
                        _("Per block script validation threads count must be at "
                          "least 0 and at most %d"), MAX_SCRIPTCHECK_THREADS);
            }

            return false;
        }

        mPerBlockScriptValidatorThreadsCount = perValidatorThreadsCount;
    }

    {
        if (perValidatorThreadMaxBatchSize < 1
            || perValidatorThreadMaxBatchSize > std::numeric_limits<uint8_t>::max())
        {
            if(error)
            {
                *error =
                    strprintf(
                        _("Per block script validation max batch size must be at "
                          "least 1 and at most %d"),
                        std::numeric_limits<uint8_t>::max());
            }

            return false;
        }
        mPerBlockScriptValidationMaxBatchSize = perValidatorThreadMaxBatchSize;
    }

    return true;
}

int GlobalConfig::GetMaxParallelBlocks() const
{
    return mMaxParallelBlocks;
}

int GlobalConfig::GetPerBlockScriptValidatorThreadsCount() const
{
    return mPerBlockScriptValidatorThreadsCount;
}

int GlobalConfig::GetPerBlockScriptValidationMaxBatchSize() const
{
    return mPerBlockScriptValidationMaxBatchSize;
}

bool GlobalConfig::SetMaxOpsPerScriptPolicy(int64_t maxOpsPerScriptPolicyIn, std::string* error)
{
    if (maxOpsPerScriptPolicyIn < 0)
    {
        if (error)
        {
            *error = "Policy value for MaxOpsPerScript cannot be less than zero.";
        }
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

uint64_t GlobalConfig::GetMaxOpsPerScript(bool isGenesisEnabled, bool consensus) const
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

bool GlobalConfig::SetMaxStdTxnValidationDuration(int ms, std::string* err)
{
    if(ms < 5)
    {
        if(err)
        {
            *err =
                strprintf(
                    _("Per transaction max validation duration must be at least 5ms"));
        }

        return false;
    }

    mMaxStdTxnValidationDuration = std::chrono::milliseconds{ms};

    return true;
}

std::chrono::milliseconds GlobalConfig::GetMaxStdTxnValidationDuration() const
{
    return mMaxStdTxnValidationDuration;
}

bool GlobalConfig::SetMaxNonStdTxnValidationDuration(int ms, std::string* err)
{
    if(ms < 10)
    {
        if(err)
        {
            *err =
                strprintf(
                    _("Per transaction max validation duration must be at least 10ms"));
        }

        return false;
    }

    mMaxNonStdTxnValidationDuration = std::chrono::milliseconds{ms};

    return true;
}

std::chrono::milliseconds GlobalConfig::GetMaxNonStdTxnValidationDuration() const
{
    return mMaxNonStdTxnValidationDuration;
}

/**
 * Compute the maximum number of sigops operations that can be contained in a block
 * given the block size as parameter. It is computed by multiplying the upper sigops limit
 * MAX_BLOCK_SIGOPS_PER_MB_BEFORE_GENESIS by the size of the block in MB rounded up to the
 * closest integer.
 */

uint64_t GlobalConfig::GetMaxBlockSigOpsConsensusBeforeGenesis(uint64_t blockSize) const
{
    auto nMbRoundedUp = 1 + ((blockSize - 1) / ONE_MEGABYTE);
    return nMbRoundedUp * MAX_BLOCK_SIGOPS_PER_MB_BEFORE_GENESIS;
}

bool GlobalConfig::SetMaxStackMemoryUsage(int64_t maxStackMemoryUsageConsensusIn, int64_t maxStackMemoryUsagePolicyIn, std::string* err)
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
            *err = _("Policy value of max stack memory usage must not exceed consensus limit of ") + std::to_string(maxStackMemoryUsageConsensus);
        }
        return false;
    }

    return true;
}

uint64_t GlobalConfig::GetMaxStackMemoryUsage(bool isGenesisEnabled, bool consensus) const
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

bool GlobalConfig::SetMaxScriptNumLengthPolicy(int64_t maxScriptNumLengthIn, std::string* err)
{
    if (maxScriptNumLengthIn < 0)
    {
        if (err)
        {
            *err = "Policy value for maximum script number length must not be less than 0.";
        }
        return false;
    }

    uint64_t maxScriptNumLengthUnsigned = static_cast<uint64_t>(maxScriptNumLengthIn);
    if (maxScriptNumLengthUnsigned > MAX_SCRIPT_NUM_LENGTH_AFTER_GENESIS)
    {
        if (err)
        {
            *err = "Policy value for maximum script number length must not exceed consensus limit of " + std::to_string(MAX_SCRIPT_NUM_LENGTH_AFTER_GENESIS) + ".";
        }
        return false;
    }
    else if (maxScriptNumLengthUnsigned == 0)
    {
        maxScriptNumLengthPolicy = MAX_SCRIPT_NUM_LENGTH_AFTER_GENESIS;
    }
    else if (maxScriptNumLengthUnsigned < MAX_SCRIPT_NUM_LENGTH_BEFORE_GENESIS)
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

uint64_t GlobalConfig::GetMaxScriptNumLength(bool isGenesisEnabled, bool isConsensus) const
{
    if (!isGenesisEnabled)
    {
        return MAX_SCRIPT_NUM_LENGTH_BEFORE_GENESIS; // no changes before genesis
    }

    if (isConsensus)
    {
        return MAX_SCRIPT_NUM_LENGTH_AFTER_GENESIS; // use new limit after genesis
    }
    return maxScriptNumLengthPolicy; // use policy
}

void GlobalConfig::SetAcceptNonStandardOutput(bool accept)
{
    mAcceptNonStandardOutput = accept;
}

bool GlobalConfig::GetAcceptNonStandardOutput(bool isGenesisEnabled) const
{
    return isGenesisEnabled ? mAcceptNonStandardOutput : !fRequireStandard;
}

bool GlobalConfig::SetMaxCoinsViewCacheSize(int64_t max, std::string* err)
{
    if (max < 0)
    {
        if (err)
        {
            *err = _("Policy value for maximum coins view cache size must not be less than 0.");
        }
        return false;
    }

    mMaxCoinsViewCacheSize = static_cast<uint64_t>(max);

    return true;
}

void GlobalConfig::SetInvalidBlocks(const std::set<uint256>& hashes)
{
    mInvalidBlocks = hashes;
}

const std::set<uint256>& GlobalConfig::GetInvalidBlocks() const
{
    return mInvalidBlocks;
}

bool GlobalConfig::IsBlockInvalidated(const uint256& hash) const
{
    return mInvalidBlocks.find(hash) != mInvalidBlocks.end(); 
}

void GlobalConfig::SetBanClientUA(const std::set<std::string> uaClients)
{
    mBannedUAClients = uaClients;
}

bool GlobalConfig::IsClientUABanned(const std::string uaClient) const
{
    for (std::string invUAClient : mBannedUAClients)
    {
        if (boost::icontains(uaClient, invUAClient))
        {
            return true;
        }
    }
    return false;
}

DummyConfig::DummyConfig()
    : chainParams(CreateChainParams(CBaseChainParams::REGTEST)) {}

DummyConfig::DummyConfig(std::string net)
    : chainParams(CreateChainParams(net)) {}

void DummyConfig::SetChainParams(std::string net) {
    chainParams = CreateChainParams(net);
}

int DummyConfig::GetMaxConcurrentAsyncTasksPerNode() const
{
    return DEFAULT_NODE_ASYNC_TASKS_LIMIT;
}

int DummyConfig::GetMaxParallelBlocks() const
{
    return DEFAULT_SCRIPT_CHECK_POOL_SIZE;
}

int DummyConfig::GetPerBlockScriptValidatorThreadsCount() const
{
    return DEFAULT_SCRIPTCHECK_THREADS;
}

int DummyConfig::GetPerBlockScriptValidationMaxBatchSize() const
{
    return DEFAULT_SCRIPT_CHECK_MAX_BATCH_SIZE;
}

void GlobalConfig::SetMinFeePerKB(CFeeRate fee) {
    feePerKB = fee;
}

CFeeRate GlobalConfig::GetMinFeePerKB() const {
    return feePerKB;
}
bool GlobalConfig::SetMaxTxSigOpsCountPolicy(int64_t maxTxSigOpsCountIn, std::string* err)
{
    if (maxTxSigOpsCountIn < 0)
    {
        if (err)
        {
            *err = _("Policy value for maximum allowed number of signature operations per transaction cannot be less than 0");
        }
        return false;
    }
    uint64_t maxTxSigOpsCountInUnsigned = static_cast<uint64_t>(maxTxSigOpsCountIn);
    if (maxTxSigOpsCountInUnsigned > MAX_TX_SIGOPS_COUNT_POLICY_AFTER_GENESIS)
    {
        if (err)
        {
            *err = _("Policy value for maximum allowed number of signature operations per transaction must not exceed limit of ") + std::to_string(MAX_TX_SIGOPS_COUNT_POLICY_AFTER_GENESIS);
        }
        return false;
    }
    if (maxTxSigOpsCountInUnsigned == 0)
    {
        maxTxSigOpsCountPolicy = MAX_TX_SIGOPS_COUNT_POLICY_AFTER_GENESIS;
    }
    else
    {
        maxTxSigOpsCountPolicy = maxTxSigOpsCountInUnsigned;
    }
    return true;
}

uint64_t GlobalConfig::GetMaxTxSigOpsCountConsensusBeforeGenesis() const
{
    return MAX_TX_SIGOPS_COUNT_BEFORE_GENESIS;
}

uint64_t GlobalConfig::GetMaxTxSigOpsCountPolicy(bool isGenesisEnabled) const
{
    if (!isGenesisEnabled)
    {
        return MAX_TX_SIGOPS_COUNT_POLICY_BEFORE_GENESIS;
    }

    return maxTxSigOpsCountPolicy;
}

bool GlobalConfig::SetMaxScriptSizePolicy(int64_t maxScriptSizePolicyIn, std::string* err) {
    if (maxScriptSizePolicyIn < 0)
    {
        if (err)
        {
            *err = "Policy value for max script size must not be less than 0";
        }
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

uint64_t GlobalConfig::GetMaxScriptSize(bool isGenesisEnabled, bool isConsensus) const {
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