// Copyright (c) 2017 The Bitcoin developers
// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "config.h"
#include "chainparams.h"
#include "consensus/consensus.h"
#include "validation.h"
#include "util.h"
#include "merkletree.h"

#include <boost/algorithm/string.hpp>
#include <limits>

namespace
{
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
}

GlobalConfig::GlobalConfig() {
    Reset();
}

void GlobalConfig::Reset()
{
    data->feePerKB = CFeeRate {};
    data->dustLimitFactor = DEFAULT_DUST_LIMIT_FACTOR;
    data->preferredBlockFileSize = DEFAULT_PREFERRED_BLOCKFILE_SIZE;
    data->factorMaxSendQueuesBytes = DEFAULT_FACTOR_MAX_SEND_QUEUES_BYTES;

    data->setDefaultBlockSizeParamsCalled = false;

    data->blockSizeActivationTime = 0;
    data->maxBlockSize = 0;
    data->defaultBlockSize = 0;
    data->maxGeneratedBlockSizeBefore = 0;
    data->maxGeneratedBlockSizeAfter = 0;
    data->maxGeneratedBlockSizeOverridden =  false;
    data->maxTxSizePolicy = DEFAULT_MAX_TX_SIZE_POLICY_AFTER_GENESIS;
    data->minConsolidationFactor = DEFAULT_MIN_CONSOLIDATION_FACTOR;
    data->maxConsolidationInputScriptSize = DEFAULT_MAX_CONSOLIDATION_INPUT_SCRIPT_SIZE;
    data->minConfConsolidationInput = DEFAULT_MIN_CONF_CONSOLIDATION_INPUT;
    data->acceptNonStdConsolidationInput = DEFAULT_ACCEPT_NON_STD_CONSOLIDATION_INPUT;

    data->dataCarrierSize = DEFAULT_DATA_CARRIER_SIZE;
    data->limitAncestorCount = DEFAULT_ANCESTOR_LIMIT;
    data->limitSecondaryMempoolAncestorCount = DEFAULT_SECONDARY_MEMPOOL_ANCESTOR_LIMIT;
    
    data->testBlockCandidateValidity = false;
    data->blockAssemblerType = mining::DEFAULT_BLOCK_ASSEMBLER_TYPE;

    data->genesisActivationHeight = 0;

    data->mMaxConcurrentAsyncTasksPerNode = DEFAULT_NODE_ASYNC_TASKS_LIMIT;

    data->mMaxParallelBlocks = DEFAULT_SCRIPT_CHECK_POOL_SIZE;
    data->mPerBlockTxnValidatorThreadsCount = DEFAULT_TXNCHECK_THREADS;
    data->mPerBlockScriptValidatorThreadsCount = DEFAULT_SCRIPTCHECK_THREADS;
    data->mPerBlockScriptValidationMaxBatchSize = DEFAULT_SCRIPT_CHECK_MAX_BATCH_SIZE;
    data->maxOpsPerScriptPolicy = DEFAULT_OPS_PER_SCRIPT_POLICY_AFTER_GENESIS;
    data->maxTxSigOpsCountPolicy = DEFAULT_TX_SIGOPS_COUNT_POLICY_AFTER_GENESIS;
    data->maxPubKeysPerMultiSig = DEFAULT_PUBKEYS_PER_MULTISIG_POLICY_AFTER_GENESIS;

    data->mMaxStdTxnValidationDuration = DEFAULT_MAX_STD_TXN_VALIDATION_DURATION;
    data->mMaxNonStdTxnValidationDuration = DEFAULT_MAX_NON_STD_TXN_VALIDATION_DURATION;
    data->mMaxTxnValidatorAsyncTasksRunDuration = CTxnValidator::DEFAULT_MAX_ASYNC_TASKS_RUN_DURATION;

    data->maxStackMemoryUsagePolicy = DEFAULT_STACK_MEMORY_USAGE_POLICY_AFTER_GENESIS;
    data->maxStackMemoryUsageConsensus = DEFAULT_STACK_MEMORY_USAGE_CONSENSUS_AFTER_GENESIS;
    data->maxScriptSizePolicy = DEFAULT_MAX_SCRIPT_SIZE_POLICY_AFTER_GENESIS;

    data->maxScriptNumLengthPolicy = DEFAULT_SCRIPT_NUM_LENGTH_POLICY_AFTER_GENESIS;
    data->genesisGracefulPeriod = DEFAULT_GENESIS_GRACEFULL_ACTIVATION_PERIOD;

    data->mAcceptNonStandardOutput = true;

    data->mMaxCoinsViewCacheSize = 0;
    data->mMaxCoinsProviderCacheSize = DEFAULT_COINS_PROVIDER_CACHE_SIZE;

    data->maxProtocolRecvPayloadLength = DEFAULT_MAX_PROTOCOL_RECV_PAYLOAD_LENGTH;
    data->maxProtocolSendPayloadLength = DEFAULT_MAX_PROTOCOL_RECV_PAYLOAD_LENGTH * MAX_PROTOCOL_SEND_PAYLOAD_FACTOR;

    data->recvInvQueueFactor = DEFAULT_RECV_INV_QUEUE_FACTOR;

    data->mMaxMempool = DEFAULT_MAX_MEMPOOL_SIZE * ONE_MEGABYTE;
    data->mMaxMempoolSizeDisk = data->mMaxMempool * DEFAULT_MAX_MEMPOOL_SIZE_DISK_FACTOR;
    data->mMempoolMaxPercentCPFP = DEFAULT_MEMPOOL_MAX_PERCENT_CPFP;
    data->mMemPoolExpiry = DEFAULT_MEMPOOL_EXPIRY * SECONDS_IN_ONE_HOUR;
    data->mMaxOrphanTxSize = COrphanTxns::DEFAULT_MAX_ORPHAN_TRANSACTIONS_SIZE;
    data->mMaxPercentageOfOrphansInMaxBatchSize = COrphanTxns::DEFAULT_MAX_PERCENTAGE_OF_ORPHANS_IN_BATCH;
    data->mMaxInputsForSecondLayerOrphan = COrphanTxns::DEFAULT_MAX_INPUTS_OUTPUTS_PER_TRANSACTION;
    data->mStopAtHeight = DEFAULT_STOPATHEIGHT;
    data->mPromiscuousMempoolFlags = 0;
    data->mIsSetPromiscuousMempoolFlags = false;

    data->invalidTxFileSinkSize = CInvalidTxnPublisher::DEFAULT_FILE_SINK_DISK_USAGE;
    data->invalidTxFileSinkEvictionPolicy = CInvalidTxnPublisher::DEFAULT_FILE_SINK_EVICTION_POLICY;
    data->enableAssumeWhitelistedBlockDepth = DEFAULT_ENABLE_ASSUME_WHITELISTED_BLOCK_DEPTH;
    data->assumeWhitelistedBlockDepth = DEFAULT_ASSUME_WHITELISTED_BLOCK_DEPTH;

    data->minBlocksToKeep = DEFAULT_MIN_BLOCKS_TO_KEEP;
    data->blockValidationTxBatchSize = DEFAULT_BLOCK_VALIDATION_TX_BATCH_SIZE;

    // Block download
    data->blockStallingMinDownloadSpeed = DEFAULT_MIN_BLOCK_STALLING_RATE;
    data->blockStallingTimeout = DEFAULT_BLOCK_STALLING_TIMEOUT;
    data->blockDownloadWindow = DEFAULT_BLOCK_DOWNLOAD_WINDOW;
    data->blockDownloadLowerWindow = DEFAULT_BLOCK_DOWNLOAD_LOWER_WINDOW;
    data->blockDownloadSlowFetchTimeout = DEFAULT_BLOCK_DOWNLOAD_SLOW_FETCH_TIMEOUT;
    data->blockDownloadMaxParallelFetch = DEFAULT_MAX_BLOCK_PARALLEL_FETCH;
    data->blockDownloadTimeoutBase = DEFAULT_BLOCK_DOWNLOAD_TIMEOUT_BASE;
    data->blockDownloadTimeoutBaseIBD = DEFAULT_BLOCK_DOWNLOAD_TIMEOUT_BASE_IBD;
    data->blockDownloadTimeoutPerPeer = DEFAULT_BLOCK_DOWNLOAD_TIMEOUT_PER_PEER;

    // P2P parameters
    data->p2pHandshakeTimeout = DEFAULT_P2P_HANDSHAKE_TIMEOUT_INTERVAL;
    data->streamSendRateLimit = Stream::DEFAULT_SEND_RATE_LIMIT;
    data->banScoreThreshold = DEFAULT_BANSCORE_THRESHOLD;
    data->blockTxnMaxPercent = DEFAULT_BLOCK_TXN_MAX_PERCENT;
    data->multistreamsEnabled = DEFAULT_STREAMS_ENABLED;
    data->whitelistRelay = DEFAULT_WHITELISTRELAY;
    data->whitelistForceRelay = DEFAULT_WHITELISTFORCERELAY;
    data->rejectMempoolRequest = DEFAULT_REJECTMEMPOOLREQUEST;
    data->dropMessageTest = std::nullopt;
    data->invalidChecksumInterval = DEFAULT_MIN_TIME_INTERVAL_CHECKSUM_MS;
    data->invalidChecksumFreq = DEFAULT_INVALID_CHECKSUM_FREQUENCY;
    data->feeFilter = DEFAULT_FEEFILTER;
    data->maxAddNodeConnections = DEFAULT_MAX_ADDNODE_CONNECTIONS;

    // banclientua
    data->mBannedUAClients = DEFAULT_CLIENTUA_BAN_PATTERNS;
    data->mAllowedUAClients = {};

    // RPC parameters
    data->webhookClientNumThreads = rpc::client::WebhookClientDefaults::DEFAULT_NUM_THREADS;

    // Double-Spend parameters
    data->dsNotificationLevel = DSAttemptHandler::DEFAULT_NOTIFY_LEVEL;
    data->dsEndpointFastTimeout = rpc::client::RPCClientConfig::DEFAULT_DS_ENDPOINT_FAST_TIMEOUT;
    data->dsEndpointSlowTimeout = rpc::client::RPCClientConfig::DEFAULT_DS_ENDPOINT_SLOW_TIMEOUT;
    data->dsEndpointSlowRatePerHour = DSAttemptHandler::DEFAULT_DS_ENDPOINT_SLOW_RATE_PER_HOUR;
    data->dsEndpointPort = rpc::client::RPCClientConfig::DEFAULT_DS_ENDPOINT_PORT;
    data->dsEndpointBlacklistSize = DSAttemptHandler::DEFAULT_DS_ENDPOINT_BLACKLIST_SIZE;
    data->dsEndpointSkipList = {};
    data->dsEndpointMaxCount = DSAttemptHandler::DEFAULT_DS_ENDPOINT_MAX_COUNT;
    data->dsAttemptTxnRemember = DSAttemptHandler::DEFAULT_TXN_REMEMBER_COUNT;
    data->dsAttemptNumFastThreads = DSAttemptHandler::DEFAULT_NUM_FAST_THREADS;
    data->dsAttemptNumSlowThreads = DSAttemptHandler::DEFAULT_NUM_SLOW_THREADS;
    data->dsAttemptQueueMaxMemory = DSAttemptHandler::DEFAULT_MAX_SUBMIT_MEMORY;
    data->dsDetectedWebhookAddress = "";
    data->dsDetectedWebhookPort = rpc::client::WebhookClientDefaults::DEFAULT_WEBHOOK_PORT;
    data->dsDetectedWebhookPath = "";
    data->dsDetectedWebhookMaxTxnSize = DSDetectedDefaults::DEFAULT_MAX_WEBHOOK_TXN_SIZE * ONE_MEBIBYTE;

    // MinerID
    data->minerIdEnabled = MinerIdDatabaseDefaults::DEFAULT_MINER_ID_ENABLED;
    data->minerIdCacheSize = MinerIdDatabaseDefaults::DEFAULT_CACHE_SIZE;
    data->numMinerIdsToKeep = MinerIdDatabaseDefaults::DEFAULT_MINER_IDS_TO_KEEP;
    data->minerIdReputationM = MinerIdDatabaseDefaults::DEFAULT_MINER_REPUTATION_M;
    data->minerIdReputationN = MinerIdDatabaseDefaults::DEFAULT_MINER_REPUTATION_N;
    data->minerIdReputationMScale = MinerIdDatabaseDefaults::DEFAULT_M_SCALE_FACTOR;
    data->minerIdGeneratorAddress = "";
    data->minerIdGeneratorPort = rpc::client::WebhookClientDefaults::DEFAULT_WEBHOOK_PORT;
    data->minerIdGeneratorPath = "";
    data->minerIdGeneratorAlias = "";

    data->mDisableBIP30Checks = std::nullopt;

#if ENABLE_ZMQ
    data->invalidTxZMQMaxMessageSize = CInvalidTxnPublisher::DEFAULT_ZMQ_SINK_MAX_MESSAGE_SIZE;
#endif

    data->maxMerkleTreeDiskSpace = MIN_DISK_SPACE_FOR_MERKLETREE_FILES;
    data->preferredMerkleTreeFileSize = DEFAULT_PREFERRED_MERKLETREE_FILE_SIZE;
    data->maxMerkleTreeMemoryCacheSize = DEFAULT_MAX_MERKLETREE_MEMORY_CACHE_SIZE;

    data->mSoftConsensusFreezeDuration = DEFAULT_SOFT_CONSENSUS_FREEZE_DURATION;
    // Safe mode activation
    data->safeModeWebhookAddress = "";
    data->safeModeWebhookPort = rpc::client::WebhookClientDefaults::DEFAULT_WEBHOOK_PORT;
    data->safeModeWebhookPath = "";
    data->safeModeMaxForkDistance = SAFE_MODE_DEFAULT_MAX_FORK_DISTANCE;
    data->safeModeMinForkLength = SAFE_MODE_DEFAULT_MIN_FORK_LENGTH;
    data->safeModeMinHeightDifference = SAFE_MODE_DEFAULT_MIN_POW_DIFFERENCE;

    // Detect selfish mining
    data->minBlockMempoolTimeDifferenceSelfish = DEFAULT_MIN_BLOCK_MEMPOOL_TIME_DIFFERENCE_SELFISH;
    data->mDetectSelfishMining = DEFAULT_DETECT_SELFISH_MINING;
    data->mSelfishTxThreshold = DEFAULT_SELFISH_TX_THRESHOLD_IN_PERCENT;
}

void GlobalConfig::SetPreferredBlockFileSize(uint64_t preferredSize) {
    data->preferredBlockFileSize = preferredSize;
}

uint64_t GlobalConfig::GetPreferredBlockFileSize() const {
    return data->preferredBlockFileSize;
}

void GlobalConfig::SetDefaultBlockSizeParams(const DefaultBlockSizeParams &params) {
    data->blockSizeActivationTime = params.blockSizeActivationTime;
    data->maxBlockSize = params.maxBlockSize;
    data->defaultBlockSize = data->maxBlockSize;
    data->maxGeneratedBlockSizeBefore = params.maxGeneratedBlockSizeBefore;
    data->maxGeneratedBlockSizeAfter = params.maxGeneratedBlockSizeAfter;
    data->maxGeneratedBlockSizeOverridden = false;
    data->setDefaultBlockSizeParamsCalled = true;
}

void GlobalConfig::CheckSetDefaultCalled() const
{
    if (!data->setDefaultBlockSizeParamsCalled)
    {
        // If you hit this we created new instance of GlobalConfig without 
        // setting defaults
        throw std::runtime_error(
            "GlobalConfig::SetDefaultBlockSizeParams must be called before accessing block size related parameters");
    }
}

bool GlobalConfig::SetMaxBlockSize(uint64_t maxSize, std::string* err) {
    std::scoped_lock<std::shared_mutex> lock{data->configMtx};
    // Do not allow maxBlockSize to be set below historic 1MB limit
    // It cannot be equal either because of the "must be big" UAHF rule.
    if (maxSize && maxSize <= LEGACY_MAX_BLOCK_SIZE) {
        if (err)
            *err = _("Excessive block size (excessiveblocksize) must be larger than ") + std::to_string(LEGACY_MAX_BLOCK_SIZE);
        return false;
    }

    // Unlimited value depends on each definition of CChainParams
    data->maxBlockSize = maxSize ? maxSize : data->defaultBlockSize;

    return true;
}

uint64_t GlobalConfig::GetMaxBlockSize() const {
    std::shared_lock<std::shared_mutex> lock{data->configMtx};
    CheckSetDefaultCalled();
    return data->maxBlockSize;
}

void GlobalConfig::SetFactorMaxSendQueuesBytes(uint64_t factorMaxSendQueuesBytesIn) {
    data->factorMaxSendQueuesBytes = factorMaxSendQueuesBytesIn;
}

uint64_t GlobalConfig::GetFactorMaxSendQueuesBytes() const {
    return data->factorMaxSendQueuesBytes;
}

uint64_t GlobalConfig::GetMaxSendQueuesBytes() const {

    // Use the "after upgrade" excessive block size to determine the maximum size of 
    // block related messages that we are prepared to queue
    uint64_t maxBlockSize = GetMaxBlockSize();
    if (data->factorMaxSendQueuesBytes > UINT64_MAX / maxBlockSize)
    {
        return UINT64_MAX;
    }
    return data->factorMaxSendQueuesBytes * maxBlockSize;
}

bool GlobalConfig::SetMaxGeneratedBlockSize(uint64_t maxSize, std::string* err) {
    std::scoped_lock<std::shared_mutex> lock{data->configMtx};
    data->maxGeneratedBlockSizeAfter = maxSize;
    data->maxGeneratedBlockSizeOverridden = true;

    return true;
}

uint64_t GlobalConfig::GetMaxGeneratedBlockSize() const {
    std::shared_lock<std::shared_mutex> lock{data->configMtx};
    CheckSetDefaultCalled();
    return data->maxGeneratedBlockSizeAfter;
}

uint64_t GlobalConfig::GetMaxGeneratedBlockSize(int64_t nMedianTimePast) const {
    std::shared_lock<std::shared_mutex> lock{data->configMtx};
    CheckSetDefaultCalled();
    uint64_t maxSize;
    if (!data->maxGeneratedBlockSizeOverridden) {
        maxSize = nMedianTimePast >= data->blockSizeActivationTime ? data->maxGeneratedBlockSizeAfter : data->maxGeneratedBlockSizeBefore;
    }
    else {
        maxSize = data->maxGeneratedBlockSizeAfter;
    }
    return maxSize;
}
bool GlobalConfig::MaxGeneratedBlockSizeOverridden() const {
    return data->maxGeneratedBlockSizeOverridden;
}

bool GlobalConfig::SetBlockSizeActivationTime(int64_t activationTime, std::string* err) {
    data->blockSizeActivationTime = activationTime;
    return true;
}

int64_t GlobalConfig::GetBlockSizeActivationTime() const {
    CheckSetDefaultCalled();
    return data->blockSizeActivationTime;
}

bool GlobalConfig::SetMaxTxSizePolicy(int64_t maxTxSizePolicyIn, std::string* err)
{
    if (LessThanZero(maxTxSizePolicyIn, err, "Policy value for max tx size must not be less than 0"))
    {
        return false;
    }
    if (maxTxSizePolicyIn == 0)
    {
        data->maxTxSizePolicy = MAX_TX_SIZE_CONSENSUS_AFTER_GENESIS;
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

    data->maxTxSizePolicy = maxTxSizePolicyInUnsigned;
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
    return data->maxTxSizePolicy;
}

bool GlobalConfig::SetMinConsolidationFactor(int64_t minConsolidationFactorIn, std::string* err)
{
    if (LessThanZero(minConsolidationFactorIn, err, "Minimum consolidation factor cannot be less than zero."))
    {
        return false;
    }
    data->minConsolidationFactor = static_cast<uint64_t>(minConsolidationFactorIn);
    return true;
}

uint64_t GlobalConfig::GetMinConsolidationFactor() const
{
    return data->minConsolidationFactor;
}

bool GlobalConfig::SetMaxConsolidationInputScriptSize(int64_t maxConsolidationInputScriptSizeIn, std::string* err)
{
    if (LessThanZero(maxConsolidationInputScriptSizeIn, err, "Maximum length for a scriptSig input in a consolidation txn cannot be less than zero."))
    {
        return false;
    }
    else if (maxConsolidationInputScriptSizeIn == 0) {
        data->maxConsolidationInputScriptSize = DEFAULT_MAX_CONSOLIDATION_INPUT_SCRIPT_SIZE;
    } else {
        data->maxConsolidationInputScriptSize = static_cast<uint64_t>(maxConsolidationInputScriptSizeIn);
    }
    return true;
}

uint64_t GlobalConfig::GetMaxConsolidationInputScriptSize() const
{
    return data->maxConsolidationInputScriptSize;
}

bool GlobalConfig::SetMinConfConsolidationInput(int64_t minconfIn, std::string* err)
{
    if (LessThanZero(minconfIn, err, "Minimum number of confirmations of inputs spent by consolidation transactions cannot be less than 0"))
    {
        return false;
    }
    if (minconfIn == 0)
    {
        data->minConfConsolidationInput = DEFAULT_MIN_CONF_CONSOLIDATION_INPUT;
    }
    else
    {
        data->minConfConsolidationInput = static_cast<uint64_t>(minconfIn);
    }
    return true;
}

uint64_t GlobalConfig::GetMinConfConsolidationInput() const
{
    return data->minConfConsolidationInput;
}

bool GlobalConfig::SetAcceptNonStdConsolidationInput(bool flagValue, std::string* err)
{
    data->acceptNonStdConsolidationInput = flagValue;
    return true;
}

bool GlobalConfig::GetAcceptNonStdConsolidationInput() const
{
    return data->acceptNonStdConsolidationInput;
}

void GlobalConfig::SetDataCarrierSize(uint64_t dataCarrierSizeIn) {
    data->dataCarrierSize = dataCarrierSizeIn;
}

uint64_t GlobalConfig::GetDataCarrierSize() const {
    return data->dataCarrierSize;
}

void GlobalConfig::SetDataCarrier(bool dataCarrierIn) {
    data->dataCarrier = dataCarrierIn;
}

bool GlobalConfig::GetDataCarrier() const {
    return data->dataCarrier;
}


bool  GlobalConfig::SetLimitAncestorCount(int64_t limitAncestorCountIn, std::string* err) {
    if (limitAncestorCountIn <= 0)
    {
        if (err)
        {
            *err = "The maximal number of the in-mempool ancestors must be greater than 0.";
        }
        return false;
    }
    data->limitAncestorCount = static_cast<uint64_t>(limitAncestorCountIn);
    return true;
}

uint64_t GlobalConfig::GetLimitAncestorCount() const {
    return data->limitAncestorCount;
}

bool GlobalConfig::SetLimitSecondaryMempoolAncestorCount(int64_t limitSecondaryMempoolAncestorCountIn, std::string* err) {
    if (limitSecondaryMempoolAncestorCountIn <= 1)
    {
        if (err)
        {
            *err = "The maximal number of the CPFP group members must be greater than 1.";
        }
        return false;
    }
    data->limitSecondaryMempoolAncestorCount = static_cast<uint64_t>(limitSecondaryMempoolAncestorCountIn);
    return true;
}

uint64_t GlobalConfig::GetLimitSecondaryMempoolAncestorCount()const {
    return data->limitSecondaryMempoolAncestorCount;
}

const CChainParams &GlobalConfig::GetChainParams() const {
    return Params();
}

bool GlobalConfig::SetMaxPubKeysPerMultiSigPolicy(int64_t maxPubKeysPerMultiSigIn, std::string* err)
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
        data->maxPubKeysPerMultiSig = MAX_PUBKEYS_PER_MULTISIG_AFTER_GENESIS;
    }
    else
    {
        data->maxPubKeysPerMultiSig = maxPubKeysPerMultiSigUnsigned;
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

    return data->maxPubKeysPerMultiSig;
}

bool GlobalConfig::SetGenesisGracefulPeriod(int64_t genesisGracefulPeriodIn, std::string* err)
{
    if (LessThanZero(genesisGracefulPeriodIn, err, "Value for Genesis graceful period must not be less than zero."))
    {
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
        data->genesisGracefulPeriod = genesisGracefulPeriodUnsigned;
    }

    return true;

}

uint64_t GlobalConfig::GetGenesisGracefulPeriod() const
{
    return data->genesisGracefulPeriod;
}

Config& GlobalConfig::GetConfig()
{
    static GlobalConfig config {};
    return config;
}

ConfigInit& GlobalConfig::GetModifiableGlobalConfig() 
{
    static Config& config = GlobalConfig::GetConfig();
    return static_cast<ConfigInit&>(config);
}

void GlobalConfig::SetTestBlockCandidateValidity(bool test) {
    data->testBlockCandidateValidity = test;
}

bool GlobalConfig::GetTestBlockCandidateValidity() const {
    return data->testBlockCandidateValidity;
}

void GlobalConfig::SetMiningCandidateBuilder(mining::CMiningFactory::BlockAssemblerType type) {
    data->blockAssemblerType = type;
}

mining::CMiningFactory::BlockAssemblerType GlobalConfig::GetMiningCandidateBuilder() const {
    return data->blockAssemblerType;
}

bool GlobalConfig::SetGenesisActivationHeight(int32_t genesisActivationHeightIn, std::string* err) {
    if (genesisActivationHeightIn <= 0)
    {
        if (err)
        {
            *err = "Genesis activation height cannot be configured with a zero or negative value.";
        }
        return false;
    }
    data->genesisActivationHeight = genesisActivationHeightIn;
    return true;
}

int32_t GlobalConfig::GetGenesisActivationHeight() const {
    return data->genesisActivationHeight;
}

bool GlobalConfig::SetMaxConcurrentAsyncTasksPerNode(
    int maxConcurrentAsyncTasksPerNode,
    std::string* error)
{
    if (maxConcurrentAsyncTasksPerNode < 1
        || maxConcurrentAsyncTasksPerNode > data->mMaxParallelBlocks)
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

    data->mMaxConcurrentAsyncTasksPerNode = maxConcurrentAsyncTasksPerNode;

    return true;
}

int GlobalConfig::GetMaxConcurrentAsyncTasksPerNode() const
{
    return data->mMaxConcurrentAsyncTasksPerNode;
}

bool GlobalConfig::SetBlockScriptValidatorsParams(
    int maxParallelBlocks,
    int perValidatorScriptThreadsCount,
    int perValidatorTxnThreadsCount,
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

        data->mMaxParallelBlocks = maxParallelBlocks;

        // limit dependent variable
        data->mMaxConcurrentAsyncTasksPerNode =
            std::min(data->mMaxConcurrentAsyncTasksPerNode, data->mMaxParallelBlocks);
    }

    {
        if (perValidatorScriptThreadsCount < 0 || perValidatorScriptThreadsCount > MAX_TXNSCRIPTCHECK_THREADS)
        {
            if(error)
            {
                *error =
                    strprintf(
                        _("Per block script validation threads count must be at "
                          "least 0 and at most %d"), MAX_TXNSCRIPTCHECK_THREADS);
            }

            return false;
        }
        // perValidatorScriptThreadsCount==0 means autodetect
        else if (perValidatorScriptThreadsCount == 0)
        {
            // There's no observable benefit from using more than 8 cores for
            // just parallel script validation
            constexpr int defaultScriptMaxThreads {8};
            perValidatorScriptThreadsCount = std::clamp(GetNumCores(), 0, defaultScriptMaxThreads);
        }

        data->mPerBlockScriptValidatorThreadsCount = perValidatorScriptThreadsCount;
    }

    {
        if (perValidatorTxnThreadsCount < 0 || perValidatorTxnThreadsCount > MAX_TXNSCRIPTCHECK_THREADS)
        {
            if(error)
            {
                *error =
                    strprintf(
                        _("Per block transaction validation threads count must be at "
                          "least 0 and at most %d"), MAX_TXNSCRIPTCHECK_THREADS);
            }

            return false;
        }
        // perValidatorTxnThreadsCount==0 means autodetect
        else if (perValidatorTxnThreadsCount == 0)
        {
            perValidatorTxnThreadsCount = std::clamp(GetNumCores(), 0, MAX_TXNSCRIPTCHECK_THREADS);
        }

        data->mPerBlockTxnValidatorThreadsCount = perValidatorTxnThreadsCount;
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
        data->mPerBlockScriptValidationMaxBatchSize = perValidatorThreadMaxBatchSize;
    }

    return true;
}

int GlobalConfig::GetMaxParallelBlocks() const
{
    return data->mMaxParallelBlocks;
}

int GlobalConfig::GetPerBlockTxnValidatorThreadsCount() const
{
    return data->mPerBlockTxnValidatorThreadsCount;
}

int GlobalConfig::GetPerBlockScriptValidatorThreadsCount() const
{
    return data->mPerBlockScriptValidatorThreadsCount;
}

int GlobalConfig::GetPerBlockScriptValidationMaxBatchSize() const
{
    return data->mPerBlockScriptValidationMaxBatchSize;
}

bool GlobalConfig::SetMaxOpsPerScriptPolicy(int64_t maxOpsPerScriptPolicyIn, std::string* error)
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
        data->maxOpsPerScriptPolicy = MAX_OPS_PER_SCRIPT_AFTER_GENESIS;
    }
    else
    {
        data->maxOpsPerScriptPolicy = maxOpsPerScriptPolicyInUnsigned;
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
    return data->maxOpsPerScriptPolicy;
}

bool GlobalConfig::SetMaxStdTxnValidationDuration(int ms, std::string* err)
{
    if(ms < 1)
    {
        if(err)
        {
            *err =
                strprintf(
                    _("Per transaction max validation duration must be at least 1ms"));
        }

        return false;
    }

    data->mMaxStdTxnValidationDuration = std::chrono::milliseconds{ms};

    return true;
}

std::chrono::milliseconds GlobalConfig::GetMaxStdTxnValidationDuration() const
{
    return data->mMaxStdTxnValidationDuration;
}

bool GlobalConfig::SetMaxTxnValidatorAsyncTasksRunDuration(int ms, std::string* err)
{
    if(ms > 0)
    {
        data->mMaxTxnValidatorAsyncTasksRunDuration = std::chrono::milliseconds{ms};
        return true;
    }

    if(err)
    {
        *err = "maxtxnvalidatorasynctasksrunduration must be at least 1ms";
    }

    return false;
}

std::chrono::milliseconds GlobalConfig::GetMaxTxnValidatorAsyncTasksRunDuration() const
{
    return data->mMaxTxnValidatorAsyncTasksRunDuration;
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

    data->mMaxNonStdTxnValidationDuration = std::chrono::milliseconds{ms};

    return true;
}

std::chrono::milliseconds GlobalConfig::GetMaxNonStdTxnValidationDuration() const
{
    return data->mMaxNonStdTxnValidationDuration;
}

bool GlobalConfig::SetMaxTxnChainValidationBudget(int ms, std::string* err)
{
    if(LessThanZero(ms, err, "Per chain max validation duration budget must be non-negative"))
    {

        return false;
    }

    data->mMaxTxnChainValidationBudget = std::chrono::milliseconds{ms};

    return true;
}

std::chrono::milliseconds GlobalConfig::GetMaxTxnChainValidationBudget() const {
    return data->mMaxTxnChainValidationBudget;
}

void GlobalConfig::SetValidationClockCPU(bool enable) {
    data->mValidationClockCPU = enable;
}

bool GlobalConfig::GetValidationClockCPU() const {
    return data->mValidationClockCPU;
}

bool GlobalConfig::SetPTVTaskScheduleStrategy(std::string strategy, std::string *err)
{
    if (strategy == "CHAIN_DETECTOR")
    {
        data->mPTVTaskScheduleStrategy = PTVTaskScheduleStrategy::CHAIN_DETECTOR;
        return true;
    }
    else if (strategy == "TOPO_SORT")
    {
        data->mPTVTaskScheduleStrategy = PTVTaskScheduleStrategy::TOPO_SORT;
        return true;
    }

    if (err)
    {
        *err = "Invalid value for task scheduling strategy. Available strategies are CHAIN_DETECTOR and TOPO_SORT. Got " + strategy;
    }

    return false;
}

PTVTaskScheduleStrategy GlobalConfig::GetPTVTaskScheduleStrategy() const
{
    return data->mPTVTaskScheduleStrategy;
}

bool GlobalConfig::SetBlockValidationTxBatchSize(int64_t size, std::string* err)
{
    if(size <= 0)
    {
        if(err)
        {
            *err = "Block validation transaction batch size must be greater than 0.";
        }
        return false;
    }

    data->blockValidationTxBatchSize = static_cast<uint64_t>(size);
    return true;
}
uint64_t GlobalConfig::GetBlockValidationTxBatchSize() const
{
    return data->blockValidationTxBatchSize;
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
        data->maxStackMemoryUsageConsensus = DEFAULT_STACK_MEMORY_USAGE_CONSENSUS_AFTER_GENESIS;
    }
    else
    {
        data->maxStackMemoryUsageConsensus = static_cast<uint64_t>(maxStackMemoryUsageConsensusIn);
    }

    if (maxStackMemoryUsagePolicyIn == 0)
    {
        data->maxStackMemoryUsagePolicy = DEFAULT_STACK_MEMORY_USAGE_CONSENSUS_AFTER_GENESIS;
    }
    else
    {
        data->maxStackMemoryUsagePolicy = static_cast<uint64_t>(maxStackMemoryUsagePolicyIn);
    }

    if (data->maxStackMemoryUsagePolicy > data->maxStackMemoryUsageConsensus)
    {
        if (err)
        {
            *err = _("Policy value of max stack memory usage must not exceed consensus limit of ") + std::to_string(data->maxStackMemoryUsageConsensus);
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
        return data->maxStackMemoryUsageConsensus;
    }

    return data->maxStackMemoryUsagePolicy;
}

bool GlobalConfig::SetMaxScriptNumLengthPolicy(int64_t maxScriptNumLengthIn, std::string* err)
{
    if (LessThanZero(maxScriptNumLengthIn, err, "Policy value for maximum script number length must not be less than 0."))
    {
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
        data->maxScriptNumLengthPolicy = MAX_SCRIPT_NUM_LENGTH_AFTER_GENESIS;
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
        data->maxScriptNumLengthPolicy = maxScriptNumLengthUnsigned;
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
    return data->maxScriptNumLengthPolicy; // use policy
}

void GlobalConfig::SetAcceptNonStandardOutput(bool accept)
{
    data->mAcceptNonStandardOutput = accept;
}

bool GlobalConfig::GetAcceptNonStandardOutput(bool isGenesisEnabled) const
{
    return isGenesisEnabled ? data->mAcceptNonStandardOutput : !fRequireStandard;
}

bool GlobalConfig::SetMaxCoinsViewCacheSize(int64_t max, std::string* err)
{
    if (LessThanZero(max, err, "Policy value for maximum coins view cache size must not be less than 0."))
    {
        return false;
    }

    data->mMaxCoinsViewCacheSize = static_cast<uint64_t>(max);

    return true;
}

bool GlobalConfig::SetMaxCoinsProviderCacheSize(int64_t max, std::string* err)
{
    static_assert( MIN_COINS_PROVIDER_CACHE_SIZE <= std::numeric_limits<int64_t>::max() );

    if (LessThan(
            max,
            err,
            "Policy value for maximum coins provider cache size must not be less than "
                + std::to_string(MIN_COINS_PROVIDER_CACHE_SIZE),
            MIN_COINS_PROVIDER_CACHE_SIZE))
    {
        return false;
    }

    data->mMaxCoinsProviderCacheSize = static_cast<uint64_t>(max);

    return true;
}

bool GlobalConfig::SetMaxCoinsDbOpenFiles(int64_t max, std::string* err)
{
    if (LessThanZero(max - 1, err, "Minimum value for max number of leveldb open files for coinsdb size must not be less than 1."))
    {
        return false;
    }

    data->mMaxCoinsDbOpenFiles = static_cast<uint64_t>(max);

    return true;
}

void GlobalConfig::SetInvalidBlocks(const std::set<uint256>& hashes)
{
    data->mInvalidBlocks = hashes;
}

const std::set<uint256>& GlobalConfig::GetInvalidBlocks() const
{
    return data->mInvalidBlocks;
}

bool GlobalConfig::IsBlockInvalidated(const uint256& hash) const
{
    return data->mInvalidBlocks.find(hash) != data->mInvalidBlocks.end(); 
}

void GlobalConfig::SetBanClientUA(const std::set<std::string> uaClients)
{
    data->mBannedUAClients = std::move(uaClients);
}

void GlobalConfig::SetAllowClientUA(const std::set<std::string> uaClients)
{
    data->mAllowedUAClients = std::move(uaClients);
}

bool GlobalConfig::IsClientUABanned(const std::string uaClient) const
{
    auto matchClient =  [&uaClient](std::string const & s)
            {
                return boost::icontains(uaClient,s);
            };
    auto searchForMatch = [&matchClient](auto const & container)
            {
                return std::find_if(container.cbegin(), container.cend(), matchClient) != container.cend();
            };

    if (searchForMatch(data->mBannedUAClients))
        if (!searchForMatch(data->mAllowedUAClients))
            return true;

    return false;
}

bool GlobalConfig::SetMaxMerkleTreeDiskSpace(int64_t maxDiskSpace, std::string* err)
{
    if (LessThanZero(maxDiskSpace, err, "Maximum disk space taken by merkle tree files cannot be configured with a negative value."))
    {
        return false;
    }
    uint64_t setMaxDiskSpace = static_cast<uint64_t>(maxDiskSpace);
    data->maxMerkleTreeDiskSpace = setMaxDiskSpace;
    return true;
}

uint64_t GlobalConfig::GetMaxMerkleTreeDiskSpace() const
{
    return data->maxMerkleTreeDiskSpace;
}

bool GlobalConfig::SetPreferredMerkleTreeFileSize(int64_t preferredFileSize, std::string* err)
{
    if (LessThanZero(preferredFileSize, err, "Merkle tree file size cannot be configured with a negative value."))
    {
        return false;
    }
    data->preferredMerkleTreeFileSize = static_cast<uint64_t>(preferredFileSize);
    return true;
}

uint64_t GlobalConfig::GetPreferredMerkleTreeFileSize() const
{
    return data->preferredMerkleTreeFileSize;
}

bool GlobalConfig::AddInvalidTxSink(const std::string& sink, std::string* err)
{
    auto availableSinks = GetAvailableInvalidTxSinks();
    if (availableSinks.find(sink) == availableSinks.end())
    {
        if (err)
        {
            *err =sink + " is not valid transaction sink. Valid transactions sinks are: ";
            *err += StringJoin(", ", availableSinks.begin(), availableSinks.end());
        }
        return false;
    }
    data->invalidTxSinks.insert(sink);
    return true;
}

std::set<std::string> GlobalConfig::GetInvalidTxSinks() const
{
    return data->invalidTxSinks;
}

std::set<std::string> GlobalConfig::GetAvailableInvalidTxSinks() const
{
#if ENABLE_ZMQ
    return {"FILE", "ZMQ"};
#else
    return {"FILE"};
#endif
}

bool GlobalConfig::SetInvalidTxFileSinkMaxDiskUsage(int64_t max, std::string* err)
{
    if (LessThanZero(max, err, "Invalid transaction file usage can not be negative."))
    {
        return false;
    }

    data->invalidTxFileSinkSize = (max == 0 ? std::numeric_limits<int64_t>::max() : max);
    return true;
}

int64_t GlobalConfig::GetInvalidTxFileSinkMaxDiskUsage() const
{
    return data->invalidTxFileSinkSize;
}

bool GlobalConfig::SetInvalidTxFileSinkEvictionPolicy(std::string policy, std::string* err)
{
    if(policy == "IGNORE_NEW")
    {
        data->invalidTxFileSinkEvictionPolicy = InvalidTxEvictionPolicy::IGNORE_NEW;
        return true;
    }
    else if (policy == "DELETE_OLD")
    {
        data->invalidTxFileSinkEvictionPolicy = InvalidTxEvictionPolicy::DELETE_OLD;
        return true;
    }

    if (err)
    {
        *err = "Invalid value for invalid transactions eviction policy. Available policies are IGNORE_NEW and DELETE_OLD. Got " + policy;
    }

    return false;
}

InvalidTxEvictionPolicy GlobalConfig::GetInvalidTxFileSinkEvictionPolicy() const
{
    return data->invalidTxFileSinkEvictionPolicy;
}

void GlobalConfig::SetEnableAssumeWhitelistedBlockDepth(bool enabled)
{
    data->enableAssumeWhitelistedBlockDepth = enabled;
}

bool GlobalConfig::GetEnableAssumeWhitelistedBlockDepth() const
{
    return data->enableAssumeWhitelistedBlockDepth;
}

bool GlobalConfig::SetAssumeWhitelistedBlockDepth(int64_t depth, std::string* err)
{
    // Note that every value is logically correct (e.g. -1 means one block above current tip).
    // Here we just check for non-sensical values.
    if(depth<0 || depth>INT32_MAX)
    {
        if(err)
        {
            *err = "Invalid value ("+std::to_string(depth)+") for 'assume whitelisted block depth' policy";
        }
        return false;
    }

    data->assumeWhitelistedBlockDepth = depth;
    return true;
}

int32_t GlobalConfig::GetAssumeWhitelistedBlockDepth() const
{
    return data->assumeWhitelistedBlockDepth;
}

// Safe mode activation
bool GlobalConfig::SetSafeModeWebhookURL(const std::string& url, std::string* err)
{
    try
    {
        int port { rpc::client::WebhookClientDefaults::DEFAULT_WEBHOOK_PORT };
        std::string host {};
        std::string protocol {};
        std::string endpoint {};
        SplitURL(url, protocol, host, port, endpoint);

        // Check for any protocol other than http
        if(protocol != "http")
        {
            if(err)
            {
                *err = "Unsupported protocol in safe mode webhook notification URL";
            }
            return false;
        }

        data->safeModeWebhookAddress = host;
        data->safeModeWebhookPort = port;
        data->safeModeWebhookPath = endpoint;
    }
    catch(const std::exception&)
    {
        if(err)
        {
            *err = "Badly formatted safe mode webhook URL";
        }
        return false;
    }

    return true;
}
std::string GlobalConfig::GetSafeModeWebhookAddress() const
{
    return data->safeModeWebhookAddress;
}
int16_t GlobalConfig::GetSafeModeWebhookPort() const
{
    return data->safeModeWebhookPort;
}
std::string GlobalConfig::GetSafeModeWebhookPath() const
{
    return data->safeModeWebhookPath;
}

bool GlobalConfig::SetMinBlocksToKeep(int32_t minblocks, std::string* err)
{
    if(minblocks < MIN_MIN_BLOCKS_TO_KEEP)
    {
        if(err)
        {
            *err = "Minimum blocks to keep must be >= " + std::to_string(MIN_MIN_BLOCKS_TO_KEEP);
        }
        return false;
    }

    data->minBlocksToKeep = minblocks;
    return true;
}
int32_t GlobalConfig::GetMinBlocksToKeep() const
{
    return data->minBlocksToKeep;
}

// Block download
bool GlobalConfig::SetBlockStallingMinDownloadSpeed(int64_t min, std::string* err)
{
    if(min < 0)
    {
        if(err)
        {
            *err = "Block stalling minimum download speed must be >= 0";
        }
        return false;
    }

    data->blockStallingMinDownloadSpeed = min;
    return true;
}
uint64_t GlobalConfig::GetBlockStallingMinDownloadSpeed() const
{
    return data->blockStallingMinDownloadSpeed;
}

bool GlobalConfig::SetBlockStallingTimeout(int64_t timeout, std::string* err)
{
    if(timeout <= 0)
    {
        if(err)
        {
            *err = "Block stalling timeout must be greater than 0.";
        }
        return false;
    }

    data->blockStallingTimeout = timeout;
    return true;
}
int64_t GlobalConfig::GetBlockStallingTimeout() const
{
    return data->blockStallingTimeout;
}

bool GlobalConfig::SetBlockDownloadWindow(int64_t window, std::string* err)
{
    if(window <= 0)
    {
        if(err)
        {
            *err = "Block download window must be greater than 0.";
        }
        return false;
    }

    data->blockDownloadWindow = window;

    // Lower download window must not be > than full window
    if(data->blockDownloadLowerWindow > data->blockDownloadWindow)
    {
        data->blockDownloadLowerWindow = data->blockDownloadWindow;
    }

    return true;
}
int64_t GlobalConfig::GetBlockDownloadWindow() const
{
    return data->blockDownloadWindow;
}

bool GlobalConfig::SetBlockDownloadLowerWindow(int64_t window, std::string* err)
{
    if(window <= 0 || window > data->blockDownloadWindow)
    {
        if(err)
        {
            *err = "Block download lower window must be greater than 0 and less than or equal to the full download window.";
        }
        return false;
    }

    data->blockDownloadLowerWindow = window;
    return true;
}
int64_t GlobalConfig::GetBlockDownloadLowerWindow() const
{
    return data->blockDownloadLowerWindow;
}

bool GlobalConfig::SetBlockDownloadSlowFetchTimeout(int64_t timeout, std::string* err)
{
    if(timeout <= 0)
    {
        if(err)
        {
            *err = "Block download slow fetch timeout must be greater than 0.";
        }
        return false;
    }

    data->blockDownloadSlowFetchTimeout = timeout;
    return true;
}
int64_t GlobalConfig::GetBlockDownloadSlowFetchTimeout() const
{
    return data->blockDownloadSlowFetchTimeout;
}

bool GlobalConfig::SetBlockDownloadMaxParallelFetch(int64_t max, std::string* err)
{
    if(max <= 0)
    {
        if(err)
        {
            *err = "Block download maximum parallel fetch must be greater than 0.";
        }
        return false;
    }

    data->blockDownloadMaxParallelFetch = max;
    return true;
}
uint64_t GlobalConfig::GetBlockDownloadMaxParallelFetch() const
{
    return data->blockDownloadMaxParallelFetch;
}

bool GlobalConfig::SetBlockDownloadTimeoutBase(int64_t timeout, std::string* err)
{
    if(timeout <= 0)
    {
        if(err)
        {
            *err = "Block download timeout base (as percentage of the block interval) must be greater than 0.";
        }
        return false;
    }
    data->blockDownloadTimeoutBase = timeout;
    return true;
}
int64_t GlobalConfig::GetBlockDownloadTimeoutBase() const
{
    return data->blockDownloadTimeoutBase;
}

bool GlobalConfig::SetBlockDownloadTimeoutBaseIBD(int64_t timeout, std::string* err)
{
    if(timeout <= 0)
    {
        if(err)
        {
            *err = "Block download timeout base during the initial block download (as percentage of the block interval) must be greater than 0.";
        }
        return false;
    }
    data->blockDownloadTimeoutBaseIBD = timeout;
    return true;
}
int64_t GlobalConfig::GetBlockDownloadTimeoutBaseIBD() const
{
    return data->blockDownloadTimeoutBaseIBD;
}

bool GlobalConfig::SetBlockDownloadTimeoutPerPeer(int64_t timeout, std::string* err)
{
    if(timeout <= 0)
    {
        if(err)
        {
            *err = "Block download timeout per peer (as percentage of the block interval) must be greater than 0.";
        }
        return false;
    }
    data->blockDownloadTimeoutPerPeer = timeout;
    return true;
}
int64_t GlobalConfig::GetBlockDownloadTimeoutPerPeer() const
{
    return data->blockDownloadTimeoutPerPeer;
}

// P2P parameters
bool GlobalConfig::SetP2PHandshakeTimeout(int64_t timeout, std::string* err)
{
    if(timeout <= 0)
    {
        if(err)
        {
            *err = "P2P handshake timeout must be greater than 0.";
        }
        return false;
    }

    data->p2pHandshakeTimeout = timeout;
    return true;
}

bool GlobalConfig::SetStreamSendRateLimit(int64_t limit, std::string* err)
{
    data->streamSendRateLimit = limit;
    return true;
}
int64_t GlobalConfig::GetStreamSendRateLimit() const
{
    return data->streamSendRateLimit;
}

bool GlobalConfig::SetBanScoreThreshold(int64_t threshold, std::string* err)
{
    auto maxThreshold { std::numeric_limits<decltype(data->banScoreThreshold)>::max() };
    if(threshold <= 0 || threshold > maxThreshold)
    {
        if(err)
        {
            *err = "Ban score threshold must be greater than 0 and less then " + std::to_string(maxThreshold);
        }
        return false;
    }

    data->banScoreThreshold = static_cast<decltype(data->banScoreThreshold)>(threshold);
    return true;
}
unsigned int GlobalConfig::GetBanScoreThreshold() const
{
    return data->banScoreThreshold;
}

bool GlobalConfig::SetBlockTxnMaxPercent(unsigned int percent, std::string* err)
{
    if(percent > 100)
    {
        if (err)
        {
            *err = "Max percentage of blocktxn transactions in a request must be between 0 and 100.";
        }
        return false;
    }

    data->blockTxnMaxPercent = percent;
    return true;
}
unsigned int GlobalConfig::GetBlockTxnMaxPercent() const
{
    return data->blockTxnMaxPercent;
}

bool GlobalConfig::SetMultistreamsEnabled(bool enabled, std::string* err)
{
    data->multistreamsEnabled = enabled;
    return true;
}
bool GlobalConfig::GetMultistreamsEnabled() const
{
    return data->multistreamsEnabled;
}

bool GlobalConfig::SetWhitelistRelay(bool relay, std::string* err)
{
    data->whitelistRelay = relay;
    return true;
}
bool GlobalConfig::GetWhitelistRelay() const
{
    return data->whitelistRelay;
}

bool GlobalConfig::SetWhitelistForceRelay(bool relay, std::string* err)
{
    data->whitelistForceRelay = relay;
    return true;
}
bool GlobalConfig::GetWhitelistForceRelay() const
{
    return data->whitelistForceRelay;
}

bool GlobalConfig::SetRejectMempoolRequest(bool reject, std::string* err)
{
    data->rejectMempoolRequest = reject;
    return true;
}
bool GlobalConfig::GetRejectMempoolRequest() const
{
    return data->rejectMempoolRequest;
}

bool GlobalConfig::SetDropMessageTest(int64_t val, std::string* err)
{
    if(val < 0)
    {
        if(err)
        {
            *err = "Drop message test value must be >= 0";
        }
        data->dropMessageTest = std::nullopt;
        return false;
    }

    data->dropMessageTest = static_cast<uint64_t>(val);
    return true;
}
bool GlobalConfig::DoDropMessageTest() const
{
    return data->dropMessageTest.has_value();
}
uint64_t GlobalConfig::GetDropMessageTest() const
{
    return data->dropMessageTest.value();
}

bool GlobalConfig::SetInvalidChecksumInterval(int64_t val, std::string* err)
{
    if(val < 0)
    {
        if(err)
        {
            *err = "Invalid checksum interval must be >= 0";
        }
        return false;
    }

    data->invalidChecksumInterval = static_cast<unsigned int>(val);
    return true;
}
unsigned int GlobalConfig::GetInvalidChecksumInterval() const
{
    return data->invalidChecksumInterval;
}

bool GlobalConfig::SetInvalidChecksumFreq(int64_t val, std::string* err)
{
    if(val < 0)
    {
        if(err)
        {
            *err = "Invalid checksum frequency must be >= 0";
        }
        return false;
    }

    data->invalidChecksumFreq = static_cast<unsigned int>(val);
    return true;
}
unsigned int GlobalConfig::GetInvalidChecksumFreq() const
{
    return data->invalidChecksumFreq;
}

bool GlobalConfig::SetFeeFilter(bool feefilter, std::string* err)
{
    data->feeFilter = feefilter;
    return true;
}
bool GlobalConfig::GetFeeFilter() const
{
    return data->feeFilter;
}


bool GlobalConfig::SetMaxAddNodeConnections(int16_t max, std::string* err)
{
    if(max < 0)
    {
        if(err)
        {
            *err = "Maximum addnode connection limit must be >= 0";
        }
        return false;
    }

    data->maxAddNodeConnections = max;
    return true;
}
uint16_t GlobalConfig::GetMaxAddNodeConnections() const
{
    return data->maxAddNodeConnections;
}


// RPC parameters
bool GlobalConfig::SetWebhookClientNumThreads(int64_t num, std::string* err)
{
    if(num <= 0 || num > static_cast<int64_t>(rpc::client::WebhookClientDefaults::MAX_NUM_THREADS))
    {
        if(err)
        {
            *err = "Webhook client number of threads must be between 1 and " +
                std::to_string(rpc::client::WebhookClientDefaults::MAX_NUM_THREADS);
        }
        return false;
    }

    data->webhookClientNumThreads = static_cast<uint64_t>(num);
    return true;
}
uint64_t GlobalConfig::GetWebhookClientNumThreads() const
{
    return data->webhookClientNumThreads;
}

// Double-Spend Parameters
bool GlobalConfig::SetDoubleSpendNotificationLevel(int level, std::string* err)
{
    if(level < static_cast<int>(DSAttemptHandler::NotificationLevel::NONE) ||
       level > static_cast<int>(DSAttemptHandler::NotificationLevel::ALL))
    {
        if(err)
        {
            *err = "Invalid value for double-spend notification level.";
        }
        return false;
    }

    data->dsNotificationLevel = static_cast<DSAttemptHandler::NotificationLevel>(level);
    return true;
}
DSAttemptHandler::NotificationLevel GlobalConfig::GetDoubleSpendNotificationLevel() const
{
    return data->dsNotificationLevel;
}

bool GlobalConfig::SetDoubleSpendEndpointFastTimeout(int timeout, std::string* err)
{
    if(timeout <= 0)
    {
        if(err)
        {
            *err = "Double-Spend endpoint fast timeout must be greater than 0.";
        }
        return false;
    }

    data->dsEndpointFastTimeout = timeout;
    return true;
}
int GlobalConfig::GetDoubleSpendEndpointFastTimeout() const
{
    return data->dsEndpointFastTimeout;
}

bool GlobalConfig::SetDoubleSpendEndpointSlowTimeout(int timeout, std::string* err)
{
    if(timeout <= 0)
    {
        if(err)
        {
            *err = "Double-Spend endpoint slow timeout must be greater than 0.";
        }
        return false;
    }

    data->dsEndpointSlowTimeout = timeout;
    return true;
}
int GlobalConfig::GetDoubleSpendEndpointSlowTimeout() const
{
    return data->dsEndpointSlowTimeout;
}

bool GlobalConfig::SetDoubleSpendEndpointSlowRatePerHour(int64_t rate, std::string* err)
{
    if(rate <= 0 || rate > 60)
    {
        if(err)
        {
            *err = "Double-Spend endpoint slow rate per hour must be between 1 and 60.";
        }
        return false;
    }

    data->dsEndpointSlowRatePerHour = static_cast<uint64_t>(rate);
    return true;
}
uint64_t GlobalConfig::GetDoubleSpendEndpointSlowRatePerHour() const
{
    return data->dsEndpointSlowRatePerHour;
}

bool GlobalConfig::SetDoubleSpendEndpointPort(int port, std::string* err)
{
    if(port <= 0 || port > 65535)
    {
        if(err)
        {
            *err = "Double-Spend endpoint port must be between 1 and 65535.";
        }
        return false;
    }

    data->dsEndpointPort = port;
    return true;
}
int GlobalConfig::GetDoubleSpendEndpointPort() const
{
    return data->dsEndpointPort;
}

bool GlobalConfig::SetDoubleSpendTxnRemember(int64_t size, std::string* err)
{
    if(size <= 0)
    {
        if(err)
        {
            *err = "Double-Spend maximum number of remembered transactions must be greater than 0.";
        }
        return false;
    }

    data->dsAttemptTxnRemember = static_cast<uint64_t>(size);
    return true;
}
uint64_t GlobalConfig::GetDoubleSpendTxnRemember() const
{
    return data->dsAttemptTxnRemember;
}

bool GlobalConfig::SetDoubleSpendEndpointBlacklistSize(int64_t size, std::string* err)
{
    if(size <= 0)
    {
        if(err)
        {
            *err = "Double-Spend maximum size of endpoint blacklist must be greater than 0.";
        }
        return false;
    }

    data->dsEndpointBlacklistSize = static_cast<uint64_t>(size);
    return true;
}
uint64_t GlobalConfig::GetDoubleSpendEndpointBlacklistSize() const
{
    return data->dsEndpointBlacklistSize;
}

bool GlobalConfig::SetDoubleSpendEndpointSkipList(const std::string& skip, std::string* err)
{
    // Split comma separated list of IPs and trim whitespace
    std::vector<std::string> ips {};
    boost::split(ips, skip, boost::is_any_of(","));
    for(auto& ip : ips)
    {
        boost::algorithm::trim(ip);
        data->dsEndpointSkipList.insert(ip);
    }

    return true;
}
std::set<std::string> GlobalConfig::GetDoubleSpendEndpointSkipList() const
{
    return data->dsEndpointSkipList;
}

bool GlobalConfig::SetDoubleSpendEndpointMaxCount(int64_t max, std::string* err)
{
    if(max <= 0)
    {
        if(err)
        {
            *err = "Double-Spend endpoint maximum count must be greater than 0.";
        }
        return false;
    }

    data->dsEndpointMaxCount = static_cast<uint64_t>(max);
    return true;
}
uint64_t GlobalConfig::GetDoubleSpendEndpointMaxCount() const
{
    return data->dsEndpointMaxCount;
}

bool GlobalConfig::SetDoubleSpendNumFastThreads(int64_t num, std::string* err)
{
    if(num <= 0 || num > static_cast<int64_t>(DSAttemptHandler::MAX_NUM_THREADS))
    {
        if(err)
        {
            *err = "Double-Spend maximum number of high priority processing threads must be between 1 and " +
                std::to_string(DSAttemptHandler::MAX_NUM_THREADS);
        }
        return false;
    }

    data->dsAttemptNumFastThreads = static_cast<uint64_t>(num);
    return true;
}
uint64_t GlobalConfig::GetDoubleSpendNumFastThreads() const
{
    return data->dsAttemptNumFastThreads;
}

bool GlobalConfig::SetDoubleSpendNumSlowThreads(int64_t num, std::string* err)
{
    if(num <= 0 || num > static_cast<int64_t>(DSAttemptHandler::MAX_NUM_THREADS))
    {
        if(err)
        {
            *err = "Double-Spend maximum number of low priority processing threads must be between 1 and " +
                std::to_string(DSAttemptHandler::MAX_NUM_THREADS);
        }
        return false;
    }

    data->dsAttemptNumSlowThreads = static_cast<uint64_t>(num);
    return true;
}
uint64_t GlobalConfig::GetDoubleSpendNumSlowThreads() const
{
    return data->dsAttemptNumSlowThreads;
}

bool GlobalConfig::SetDoubleSpendQueueMaxMemory(int64_t max, std::string* err)
{
    if(max <= 0)
    {
        if(err)
        {
            *err = "Double-Spend maximum queue memory must be greater than 0.";
        }
        return false;
    }

    data->dsAttemptQueueMaxMemory = static_cast<uint64_t>(max);
    return true;
}
uint64_t GlobalConfig::GetDoubleSpendQueueMaxMemory() const
{
    return data->dsAttemptQueueMaxMemory;
}

int64_t GlobalConfig::GetSafeModeMaxForkDistance() const
{
    return data->safeModeMaxForkDistance;
}

bool GlobalConfig::SetSafeModeMaxForkDistance(int64_t distance, std::string* err)
{
    if(distance < 1)
    {
       if(err)
        {
            *err = "Safe mode maximum fork distance must be greater than 0.";
        }
        return false;
    }

    data->safeModeMaxForkDistance = distance;
    return true;
}

int64_t GlobalConfig::GetSafeModeMinForkLength() const
{
    return data->safeModeMinForkLength;
}

bool GlobalConfig::SetSafeModeMinForkLength(int64_t length, std::string* err)
{
    if(length < 1)
    {
       if(err)
        {
            *err = "Safe mode minimal fork length must be greater than 0.";
        }
        return false;
    }

    data->safeModeMinForkLength = length;
    return true;
}

int64_t GlobalConfig::GetSafeModeMinForkHeightDifference() const
{
    return data->safeModeMinHeightDifference;
}

bool GlobalConfig::SetSafeModeMinForkHeightDifference(int64_t heightDifference, std::string* err)
{
    data->safeModeMinHeightDifference = heightDifference;
    return true;
}

bool GlobalConfig::SetDoubleSpendDetectedWebhookURL(const std::string& url, std::string* err)
{
    try
    {
        int port { rpc::client::WebhookClientDefaults::DEFAULT_WEBHOOK_PORT };
        std::string host {};
        std::string protocol {};
        std::string endpoint {};
        SplitURL(url, protocol, host, port, endpoint);

        // Check for any protocol other than http
        if(protocol != "http")
        {
            if(err)
            {
                *err = "Unsupported protocol in double-spend detected webhook notification URL";
            }
            return false;
        }

        data->dsDetectedWebhookAddress = host;
        data->dsDetectedWebhookPort = port;
        data->dsDetectedWebhookPath = endpoint;
    }
    catch(const std::exception&)
    {
        if(err)
        {
            *err = "Badly formatted double-spend detected webhook URL";
        }
        return false;
    }
    return true;
}
std::string GlobalConfig::GetDoubleSpendDetectedWebhookAddress() const
{
    return data->dsDetectedWebhookAddress;
}
int16_t GlobalConfig::GetDoubleSpendDetectedWebhookPort() const
{
    return data->dsDetectedWebhookPort;
}
std::string GlobalConfig::GetDoubleSpendDetectedWebhookPath() const
{
    return data->dsDetectedWebhookPath;
}

bool GlobalConfig::SetDoubleSpendDetectedWebhookMaxTxnSize(int64_t max, std::string* err)
{
    if(max <= 0)
    {
        if(err)
        {
            *err = "Double-spend detected webhook maximum transaction size must be greater than 0.";
        }
        return false;
    }

    data->dsDetectedWebhookMaxTxnSize = static_cast<uint64_t>(max);
    return true;
}
uint64_t GlobalConfig::GetDoubleSpendDetectedWebhookMaxTxnSize() const
{
    return data->dsDetectedWebhookMaxTxnSize;
}

bool GlobalConfig::SetDisableBIP30Checks(bool disable, std::string* err)
{
    if(!GetChainParams().CanDisableBIP30Checks())
    {
        if(err)
        {
            *err = "Can not change disabling of BIP30 checks on " + GetChainParams().NetworkIDString() + " network.";
            return false;
        }
    }
    data->mDisableBIP30Checks = disable;
    return true;
}

bool GlobalConfig::GetDisableBIP30Checks() const
{
    return data->mDisableBIP30Checks.value_or(GetChainParams().DisableBIP30Checks());
}

// MinerID
bool GlobalConfig::SetMinerIdEnabled(bool enabled, std::string* err)
{
    data->minerIdEnabled = enabled;
    return true;
}
bool GlobalConfig::GetMinerIdEnabled() const
{
    return data->minerIdEnabled;
}

bool GlobalConfig::SetMinerIdCacheSize(int64_t size, std::string* err)
{
    if(size < 0 || static_cast<uint64_t>(size) > MinerIdDatabaseDefaults::MAX_CACHE_SIZE)
    {
        if(err)
        {
            *err = "Miner ID database cache size must be >= 0 and <= " + std::to_string(MinerIdDatabaseDefaults::MAX_CACHE_SIZE);
        }
        return false;
    }

    data->minerIdCacheSize = static_cast<uint64_t>(size);
    return true;
}
uint64_t GlobalConfig::GetMinerIdCacheSize() const
{
    return data->minerIdCacheSize;
}

bool GlobalConfig::SetMinerIdsNumToKeep(int64_t num, std::string* err)
{
    if(num < 2)
    {
        if(err)
        {
            *err = "Number of miner IDs to keep must be >= 2.";
        }
        return false;
    }

    data->numMinerIdsToKeep = static_cast<uint64_t>(num);
    return true;
}
uint64_t GlobalConfig::GetMinerIdsNumToKeep() const
{
    return data->numMinerIdsToKeep;
}

bool GlobalConfig::SetMinerIdReputationM(int64_t num, std::string* err)
{
    if(num < 1 || static_cast<uint32_t>(num) > MinerIdDatabaseDefaults::MAX_MINER_REPUTATION_M)
    {
        if(err)
        {
            *err = "Miner ID reputation M must be > 0 and <= " + std::to_string(MinerIdDatabaseDefaults::MAX_MINER_REPUTATION_M) + ".";
        }
        return false;
    }
    else if(static_cast<uint32_t>(num) > GetMinerIdReputationN())
    {
        if(err)
        {
            *err = "Miner ID reputation M must be <= the value of miner ID reputation N.";
        }
        return false;
    }

    data->minerIdReputationM = static_cast<uint32_t>(num);
    return true;
}
uint32_t GlobalConfig::GetMinerIdReputationM() const
{
    return data->minerIdReputationM;
}

bool GlobalConfig::SetMinerIdReputationN(int64_t num, std::string* err)
{
    if(num < 1 || static_cast<uint32_t>(num) > MinerIdDatabaseDefaults::MAX_MINER_REPUTATION_N)
    {
        if(err)
        {
            *err = "Miner ID reputation N must be > 0 and <= " + std::to_string(MinerIdDatabaseDefaults::MAX_MINER_REPUTATION_N) + ".";
        }
        return false;
    }
    else if(static_cast<uint32_t>(num) < GetMinerIdReputationM())
    {
        if(err)
        {
            *err = "Miner ID reputation N must be >= the value of miner ID reputation M.";
        }
        return false;
    }

    data->minerIdReputationN = static_cast<uint32_t>(num);
    return true;
}
uint32_t GlobalConfig::GetMinerIdReputationN() const
{
    return data->minerIdReputationN;
}

bool GlobalConfig::SetMinerIdReputationMScale(double num, std::string* err)
{
    if(num < 1)
    {
        if(err)
        {
            *err = "Miner ID reputation M scale factor must be >= 1.";
        }
        return false;
    }

    data->minerIdReputationMScale = num;
    return true;
}
double GlobalConfig::GetMinerIdReputationMScale() const
{
    return data->minerIdReputationMScale;
}

bool GlobalConfig::SetMinerIdGeneratorURL(const std::string& url, std::string* err)
{
    try
    {   
        int port { rpc::client::WebhookClientDefaults::DEFAULT_WEBHOOK_PORT };
        std::string host {};
        std::string protocol {};
        std::string endpoint {};
        SplitURL(url, protocol, host, port, endpoint);

        // Check for any protocol other than http
        if(protocol != "http")
        {   
            if(err)
            {   
                *err = "Unsupported protocol in miner ID generator URL";
            }
            return false;
        }

        data->minerIdGeneratorAddress = host;
        data->minerIdGeneratorPort = port;
        data->minerIdGeneratorPath = endpoint;
    }
    catch(const std::exception&)
    {   
        if(err)
        {   
            *err = "Badly formatted miner ID generator URL";
        }
        return false;
    }
    return true;
}
std::string GlobalConfig::GetMinerIdGeneratorAddress() const
{
    return data->minerIdGeneratorAddress;
}
int16_t GlobalConfig::GetMinerIdGeneratorPort() const
{
    return data->minerIdGeneratorPort;
}
std::string GlobalConfig::GetMinerIdGeneratorPath() const
{
    return data->minerIdGeneratorPath;
}

bool GlobalConfig::SetMinerIdGeneratorAlias(const std::string& alias, std::string* err)
{
    data->minerIdGeneratorAlias = alias;
    return true;
}
std::string GlobalConfig::GetMinerIdGeneratorAlias() const
{
    return data->minerIdGeneratorAlias;
}

#if ENABLE_ZMQ
bool GlobalConfig::SetInvalidTxZMQMaxMessageSize(int64_t max, std::string* err)
{
    if (LessThanZero(max, err, "Invalid transaction ZMQ max message size can not be negative."))
    {
        return false;
    }

    data->invalidTxZMQMaxMessageSize = (max == 0 ? std::numeric_limits<int64_t>::max() : max);
    return true;
}

int64_t GlobalConfig::GetInvalidTxZMQMaxMessageSize() const
{
    return data->invalidTxZMQMaxMessageSize;
}
#endif

bool GlobalConfig::SetMaxMerkleTreeMemoryCacheSize(int64_t maxMemoryCacheSize, std::string* err)
{
    if (LessThanZero(maxMemoryCacheSize, err, "Maximum merkle tree memory cache size cannot be configured with a negative value."))
    {
        return false;
    }

    data->maxMerkleTreeMemoryCacheSize = static_cast<uint64_t>(maxMemoryCacheSize);
    return true;
}

uint64_t GlobalConfig::GetMaxMerkleTreeMemoryCacheSize() const
{
    return data->maxMerkleTreeMemoryCacheSize;
}

bool GlobalConfig::SetMaxProtocolRecvPayloadLength(uint64_t value, std::string* err)
{
    // sending maxRecvPayloadLength less than LEGACY_MAX_PROTOCOL_PAYLOAD_LENGTH is considered protocol violation
    if (value < LEGACY_MAX_PROTOCOL_PAYLOAD_LENGTH)
    {
        if (err)
        {
            *err = "MaxProtocolRecvPayloadLength should be at least: " + std::to_string(LEGACY_MAX_PROTOCOL_PAYLOAD_LENGTH) + ".";
        }
        return false;
    }
    
    if (value > MAX_PROTOCOL_RECV_PAYLOAD_LENGTH )
    {
        if (err)
        {
            *err = "MaxProtocolRecvPayloadLength should be less than: " + std::to_string(MAX_PROTOCOL_RECV_PAYLOAD_LENGTH ) + ".";
        }
        return false;
    }

    data->maxProtocolRecvPayloadLength = value;

    // Since value is between LEGACY_MAX_PROTOCOL_PAYLOAD_LENGTH and ONE_GIGABYTE and MAX_PROTOCOL_SEND_PAYLOAD_FACTOR is set to 4
    // this cannot overflow unsigned int
    data->maxProtocolSendPayloadLength = static_cast<unsigned int>(value * MAX_PROTOCOL_SEND_PAYLOAD_FACTOR);
    
    return true;
}

bool GlobalConfig::SetRecvInvQueueFactor(uint64_t value, std::string* err)
{
    if(value < MIN_RECV_INV_QUEUE_FACTOR || value > MAX_RECV_INV_QUEUE_FACTOR)
    {
        if(err)
        {
            *err = "RecvInvQueueFactor should be between: " + std::to_string(MIN_RECV_INV_QUEUE_FACTOR) + " and " + 
                   std::to_string(MAX_RECV_INV_QUEUE_FACTOR) + ".";
        }
        return false;
    }
    data->recvInvQueueFactor = value;
    return true;
}

unsigned int GlobalConfig::GetMaxProtocolRecvPayloadLength() const
{
  return data->maxProtocolRecvPayloadLength;
}

unsigned int GlobalConfig::GetMaxProtocolSendPayloadLength() const
{
  return data->maxProtocolSendPayloadLength;
}

unsigned int GlobalConfig::GetRecvInvQueueFactor() const
{
  return data->recvInvQueueFactor;
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

int DummyConfig::GetPerBlockTxnValidatorThreadsCount() const
{
    return DEFAULT_SCRIPTCHECK_THREADS;
}

int DummyConfig::GetPerBlockScriptValidatorThreadsCount() const
{
    return DEFAULT_SCRIPTCHECK_THREADS;
}

int DummyConfig::GetPerBlockScriptValidationMaxBatchSize() const
{
    return DEFAULT_SCRIPT_CHECK_MAX_BATCH_SIZE;
}

void DummyConfig::Reset() {}

void GlobalConfig::SetMinFeePerKB(CFeeRate fee) {
    data->feePerKB = fee;
}

CFeeRate GlobalConfig::GetMinFeePerKB() const {
    return data->feePerKB;
}

bool GlobalConfig::SetDustRelayFee(Amount n, std::string* err)
{
    if(n == Amount(0))
    {
        if(err)
        {
            *err = "DustRelayFee amount should be larger than 0";
        }
        return false;
    }

    data->dustRelayFee = CFeeRate(n);
    return true;
};

CFeeRate GlobalConfig::GetDustRelayFee() const 
{
    return data->dustRelayFee;
};

bool GlobalConfig::SetDustLimitFactor(int64_t factor, std::string* err) {
    if (factor < 0 || factor > DEFAULT_DUST_LIMIT_FACTOR)
    {
        if (err)
        {
            *err = _("The dust limit factor must be between 0% and ") + std::to_string(DEFAULT_DUST_LIMIT_FACTOR) + "%";
        }
        return false;
    }
    data->dustLimitFactor = factor;
    return true;
}

int64_t GlobalConfig::GetDustLimitFactor() const {
    return data->dustLimitFactor;
}

bool GlobalConfig::SetMaxTxSigOpsCountPolicy(int64_t maxTxSigOpsCountIn, std::string* err)
{
    if (LessThanZero(maxTxSigOpsCountIn, err, "Policy value for maximum allowed number of signature operations per transaction cannot be less than 0"))
    {
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
        data->maxTxSigOpsCountPolicy = MAX_TX_SIGOPS_COUNT_POLICY_AFTER_GENESIS;
    }
    else
    {
        data->maxTxSigOpsCountPolicy = maxTxSigOpsCountInUnsigned;
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

    return data->maxTxSigOpsCountPolicy;
}

bool GlobalConfig::SetMaxScriptSizePolicy(int64_t maxScriptSizePolicyIn, std::string* err) {
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
        data->maxScriptSizePolicy = MAX_SCRIPT_SIZE_AFTER_GENESIS;
    }
    else
    {
        data->maxScriptSizePolicy = maxScriptSizePolicyInUnsigned;
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
    return data->maxScriptSizePolicy;
}

bool GlobalConfig::SetMaxMempool(int64_t maxMempool, std::string* err) {
    if (LessThanZero(maxMempool, err, "Policy value for maximum resident memory pool must not be less than 0."))
    {
        return false;
    }
    if (maxMempool > 0 && maxMempool < DEFAULT_MAX_MEMPOOL_SIZE * ONE_MEGABYTE * 0.3)
    {
        if (err)
        {
            *err = strprintf(_("Policy value for maximum resident memory pool must be at least %d MB"), std::ceil(DEFAULT_MAX_MEMPOOL_SIZE * 0.3));
        }
        return false;
    }

    data->mMaxMempool = static_cast<uint64_t>(maxMempool);

    return true;
}

uint64_t GlobalConfig::GetMaxMempool() const {
    return data->mMaxMempool;
}

bool GlobalConfig::SetMaxMempoolSizeDisk(int64_t maxMempoolSizeDisk, std::string* err) {
    if (LessThanZero(maxMempoolSizeDisk, err, "Policy value for maximum on-disk memory pool must not be less than 0."))
    {
        return false;
    }

    data->mMaxMempoolSizeDisk = static_cast<uint64_t>(maxMempoolSizeDisk);

    return true;
}

uint64_t GlobalConfig::GetMaxMempoolSizeDisk() const {
    return data->mMaxMempoolSizeDisk;
}

bool GlobalConfig::SetMempoolMaxPercentCPFP(int64_t mempoolMaxPercentCPFP, std::string* err) {
    if (LessThanZero(mempoolMaxPercentCPFP, err, "Policy value for percentage of memory for low paying transactions must not be less than 0."))
    {
        return false;
    }

    if (mempoolMaxPercentCPFP > 100)
    {
        if (err)
        {
            *err = "Policy value for percentage of memory for low paying transactions must not be greater than 100";
        }
        return false;
    }

    data->mMempoolMaxPercentCPFP = static_cast<uint64_t>(mempoolMaxPercentCPFP);

    return true;
}

uint64_t GlobalConfig::GetMempoolMaxPercentCPFP() const {
    return data->mMempoolMaxPercentCPFP;
}

bool GlobalConfig::SetMemPoolExpiry(int64_t memPoolExpiry, std::string* err) {
    if (LessThanZero(memPoolExpiry, err, "Policy value for memory pool expiry must not be less than 0."))
    {
        return false;
    }

    data->mMemPoolExpiry = static_cast<uint64_t>(memPoolExpiry);

    return true;
}

uint64_t GlobalConfig::GetMemPoolExpiry() const {
    return data->mMemPoolExpiry;
}

bool GlobalConfig::SetMaxOrphanTxSize(int64_t maxOrphanTxSize, std::string* err) {
    if (LessThanZero(maxOrphanTxSize, err, "Policy value for maximum orphan transaction size must not be less than 0."))
    {
        return false;
    }

    data->mMaxOrphanTxSize = static_cast<uint64_t>(maxOrphanTxSize);

    return true;
}

uint64_t GlobalConfig::GetMaxOrphanTxSize() const {
    return data->mMaxOrphanTxSize;
}


bool GlobalConfig::SetMaxOrphansInBatchPercentage(uint64_t percent, std::string* err) {
    if (percent < 1 || percent > 100)
    {
        if (err)
        {
            *err = "Max percentage of orphans as percentage of maximal batch size must be between 1 and 100.";
        }
        return false;
    }

    data->mMaxPercentageOfOrphansInMaxBatchSize = percent;
    return true;
}

uint64_t GlobalConfig::GetMaxOrphansInBatchPercentage() const {
    return data->mMaxPercentageOfOrphansInMaxBatchSize;
}

bool GlobalConfig::SetMaxInputsForSecondLayerOrphan(uint64_t maxInputs, std::string* err) {
    if (LessThanZero(maxInputs, err, "Max inputs for out of first layer orphan txs must not be less than 0."))
    {
        return false;
    }

    data->mMaxInputsForSecondLayerOrphan = maxInputs;
    return true;
}

uint64_t GlobalConfig::GetMaxInputsForSecondLayerOrphan() const {
    return data->mMaxInputsForSecondLayerOrphan;
}

bool GlobalConfig::SetStopAtHeight(int32_t stopAtHeight, std::string* err) {
    if (LessThanZero(stopAtHeight, err, "Policy value for stop at height in the main chain must not be less than 0."))
    {
        return false;
    }

    data->mStopAtHeight = stopAtHeight;
    return true;
}

int32_t GlobalConfig::GetStopAtHeight() const {
    return data->mStopAtHeight;
}

bool GlobalConfig::SetPromiscuousMempoolFlags(int64_t promiscuousMempoolFlags, std::string* err) {
    if (LessThanZero(promiscuousMempoolFlags, err, "Promiscuous mempool flags value must not be less than 0."))
    {
        return false;
    }
    data->mPromiscuousMempoolFlags = static_cast<uint64_t>(promiscuousMempoolFlags);
    data->mIsSetPromiscuousMempoolFlags = true;

    return true;
}

uint64_t GlobalConfig::GetPromiscuousMempoolFlags() const {
    return data->mPromiscuousMempoolFlags;
}
bool GlobalConfig::IsSetPromiscuousMempoolFlags() const {
    return data->mIsSetPromiscuousMempoolFlags;
}

bool GlobalConfig::SetSoftConsensusFreezeDuration( std::int64_t duration, std::string* err )
{
    if (LessThanZero(duration, err, "Soft consensus freeze cannot be configured with a negative value."))
    {
        return false;
    }

    data->mSoftConsensusFreezeDuration =
        duration ? duration : std::numeric_limits<std::int32_t>::max();

    return true;
}

std::int32_t GlobalConfig::GetSoftConsensusFreezeDuration() const
{
    return data->mSoftConsensusFreezeDuration;
}

bool GlobalConfig::GetDetectSelfishMining() const
{
    return data->mDetectSelfishMining;
}

void GlobalConfig::SetDetectSelfishMining(bool detectSelfishMining)
{
    data->mDetectSelfishMining = detectSelfishMining;
}

int64_t GlobalConfig::GetMinBlockMempoolTimeDifferenceSelfish() const
{
    return data->minBlockMempoolTimeDifferenceSelfish;
}

bool GlobalConfig::SetMinBlockMempoolTimeDifferenceSelfish(int64_t minBlockMempoolTimeDiffIn, std::string* err) {
    if (LessThanZero(minBlockMempoolTimeDiffIn, err, "Value for min block - mempool tx time difference must not be less than 0"))
    {
        return false;
    }
    data->minBlockMempoolTimeDifferenceSelfish = minBlockMempoolTimeDiffIn;
    return true;
}

uint64_t GlobalConfig::GetSelfishTxThreshold() const
{
    return data->mSelfishTxThreshold;
}

bool GlobalConfig::SetSelfishTxThreshold(uint64_t selfishTxPercentThreshold, std::string* err)
{
    if (selfishTxPercentThreshold > 100)
    {
        if (err)
        {
            *err = "Selfish tx percentage threshold must be between 0 and 100.";
        }
        return false;
    }
    data->mSelfishTxThreshold = selfishTxPercentThreshold;
    return true;
}

std::shared_ptr<GlobalConfig::GlobalConfigData> GlobalConfig::getGlobalConfigData() const
{
    return data;
}

GlobalConfig::GlobalConfig(std::shared_ptr<GlobalConfigData> dataIn)
  : data{dataIn}
{
}
