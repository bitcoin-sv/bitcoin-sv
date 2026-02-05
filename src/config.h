// Copyright (c) 2017 Amaury SÉCHET
// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#ifndef BITCOIN_CONFIG_H
#define BITCOIN_CONFIG_H

#include "amount.h"
#include "configscriptpolicy.h"
#include "invalid_txn_publisher.h"
#include "mining/factory.h"
#include "net/net.h"
#include "policy/policy.h"
#include "rpc/client_config.h"
#include "script/standard.h"
#include "txn_validation_config.h"
#include "validation.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <orphan_txns.h>
#include <shared_mutex>
#include <string>

class CChainParams;
struct DefaultBlockSizeParams;

// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions, cppcoreguidelines-virtual-class-destructor)
class Config : public boost::noncopyable
{
public:

    // Policies settings for script eval/verify
    virtual uint64_t GetMaxOpsPerScript(bool isGenesisEnabled, bool isConsensus) const = 0;
    virtual uint64_t GetMaxScriptNumLength(ProtocolEra era, bool isConsensus) const = 0;
    virtual uint64_t GetMaxScriptSize(bool isGenesisEnabled, bool isConsensus) const = 0;
    virtual uint64_t GetMaxPubKeysPerMultiSig(bool isGenesisEnabled, bool isConsensus) const = 0;
    virtual uint64_t GetMaxStackMemoryUsage(bool isGenesisEnabled, bool isConsensus) const = 0;
    virtual int32_t GetGenesisActivationHeight() const = 0;
    virtual int32_t GetChronicleActivationHeight() const = 0;
    virtual const ConfigScriptPolicy& GetConfigScriptPolicy() const = 0;

    virtual uint64_t GetMaxBlockSize() const = 0;
    virtual bool SetMaxBlockSize(uint64_t maxBlockSize, std::string* err = nullptr) = 0;
    virtual uint64_t GetMaxGeneratedBlockSize() const = 0;
    virtual uint64_t GetMaxGeneratedBlockSize(int64_t nMedianTimePast) const = 0;
    virtual bool SetMaxGeneratedBlockSize(uint64_t maxGeneratedBlockSize, std::string* err = nullptr) = 0;
    virtual bool MaxGeneratedBlockSizeOverridden() const = 0;
    virtual int64_t GetBlockSizeActivationTime() const = 0;
    virtual const CChainParams &GetChainParams() const = 0;
    virtual uint64_t GetMaxTxSize(ProtocolEra era, bool isConsensus) const = 0;
    virtual uint64_t GetMinConsolidationFactor() const = 0;
    virtual uint64_t GetMaxConsolidationInputScriptSize() const = 0;
    virtual uint64_t GetMinConfConsolidationInput() const = 0;
    virtual bool GetAcceptNonStdConsolidationInput() const = 0;
    virtual CFeeRate GetMinFeePerKB() const = 0;
    virtual CFeeRate GetDustRelayFee() const = 0;
    virtual int64_t GetDustLimitFactor() const = 0;
    virtual uint64_t GetPreferredBlockFileSize() const = 0;
    virtual uint64_t GetDataCarrierSize() const = 0;
    virtual bool GetDataCarrier() const = 0;
    virtual uint64_t GetLimitAncestorCount() const = 0;
    virtual uint64_t GetLimitSecondaryMempoolAncestorCount() const = 0;
    virtual bool GetTestBlockCandidateValidity() const = 0;
    virtual uint64_t GetFactorMaxSendQueuesBytes() const = 0;
    virtual uint64_t GetMaxSendQueuesBytes() const = 0; // calculated based on factorMaxSendQueuesBytes
    virtual mining::CMiningFactory::BlockAssemblerType GetMiningCandidateBuilder() const = 0;
    virtual int GetMaxConcurrentAsyncTasksPerNode() const = 0;
    virtual int GetMaxParallelBlocks() const = 0;
    virtual int GetPerBlockTxnValidatorThreadsCount() const = 0;
    virtual int GetPerBlockScriptValidatorThreadsCount() const = 0;
    virtual int GetPerBlockScriptValidationMaxBatchSize() const = 0;

    virtual uint64_t GetBlockValidationTxBatchSize() const = 0;

    virtual uint64_t GetMaxTxSigOpsCountConsensusBeforeGenesis() const = 0;
    virtual uint64_t GetMaxTxSigOpsCountPolicy(ProtocolEra era) const = 0;
    virtual uint64_t GetMaxBlockSigOpsConsensusBeforeGenesis(uint64_t blockSize) const = 0;
    virtual std::chrono::milliseconds GetMaxStdTxnValidationDuration() const = 0;
    virtual std::chrono::milliseconds GetMaxNonStdTxnValidationDuration() const = 0;
    virtual std::chrono::milliseconds GetMaxTxnValidatorAsyncTasksRunDuration() const = 0;
    virtual bool GetValidationClockCPU() const = 0;
    virtual std::chrono::milliseconds GetMaxTxnChainValidationBudget() const = 0;
    virtual PTVTaskScheduleStrategy GetPTVTaskScheduleStrategy() const = 0;
    virtual uint64_t GetGenesisGracefulPeriod() const = 0;
    virtual uint64_t GetChronicleGracefulPeriod() const = 0;
    virtual bool GetAcceptNonStandardOutput(ProtocolEra era) const = 0;
    virtual uint64_t GetMaxCoinsViewCacheSize() const = 0;
    virtual uint64_t GetMaxCoinsProviderCacheSize() const = 0;
    virtual const std::set<uint256>& GetInvalidBlocks() const = 0;
    virtual bool IsBlockInvalidated(const uint256& hash) const = 0;
    virtual bool IsClientUABanned(const std::string uaClient) const = 0;
    virtual uint64_t GetMaxMerkleTreeDiskSpace() const = 0;
    virtual uint64_t GetPreferredMerkleTreeFileSize() const = 0;
    virtual uint64_t GetMaxMerkleTreeMemoryCacheSize() const = 0;
    virtual uint64_t GetMaxMempool() const = 0;
    virtual uint64_t GetMemPoolExpiry() const = 0;
    virtual uint64_t GetMaxOrphanTxSize() const = 0;
    virtual uint64_t GetMaxOrphansInBatchPercentage() const = 0;
    virtual uint64_t GetMaxInputsForSecondLayerOrphan() const = 0;
    virtual int32_t GetStopAtHeight() const = 0;
    virtual uint64_t GetPromiscuousMempoolFlags() const = 0;
    virtual bool IsSetPromiscuousMempoolFlags() const = 0;
    virtual std::set<std::string> GetInvalidTxSinks() const = 0;
    virtual std::set<std::string> GetAvailableInvalidTxSinks() const = 0;
    virtual int64_t GetInvalidTxFileSinkMaxDiskUsage() const = 0;
    virtual InvalidTxEvictionPolicy GetInvalidTxFileSinkEvictionPolicy() const = 0;
    virtual bool GetEnableAssumeWhitelistedBlockDepth() const = 0;
    virtual int32_t GetAssumeWhitelistedBlockDepth() const = 0;

    virtual int32_t GetMinBlocksToKeep() const = 0;

    // Block download
    virtual uint64_t GetBlockStallingMinDownloadSpeed() const = 0;
    virtual int64_t GetBlockStallingTimeout() const = 0;
    virtual int64_t GetBlockDownloadWindow() const = 0;
    virtual int64_t GetBlockDownloadLowerWindow() const = 0;
    virtual int64_t GetBlockDownloadSlowFetchTimeout() const = 0;
    virtual uint64_t GetBlockDownloadMaxParallelFetch() const = 0;
    virtual int64_t GetBlockDownloadTimeoutBase() const = 0;
    virtual int64_t GetBlockDownloadTimeoutBaseIBD() const = 0;
    virtual int64_t GetBlockDownloadTimeoutPerPeer() const = 0;

    // P2P parameters
    virtual int64_t GetP2PHandshakeTimeout() const = 0;
    virtual int64_t GetStreamSendRateLimit() const = 0;
    virtual unsigned int GetBanScoreThreshold() const = 0;
    virtual unsigned int GetBlockTxnMaxPercent() const = 0;
    virtual bool GetMultistreamsEnabled() const = 0;
    virtual bool GetWhitelistRelay() const = 0;
    virtual bool GetWhitelistForceRelay() const = 0;
    virtual bool GetRejectMempoolRequest() const = 0;
    virtual bool DoDropMessageTest() const = 0;
    virtual uint64_t GetDropMessageTest() const = 0;
    virtual unsigned int GetInvalidChecksumInterval() const = 0;
    virtual unsigned int GetInvalidChecksumFreq() const = 0;
    virtual bool GetFeeFilter() const = 0;
    virtual uint16_t GetMaxAddNodeConnections() const = 0;
    virtual uint64_t GetMaxRecvBuffer() const = 0;

    // RPC parameters
    virtual uint64_t GetWebhookClientNumThreads() const = 0;
    virtual int64_t GetWebhookClientMaxResponseBodySize() const = 0;
    virtual int64_t GetWebhookClientMaxResponseHeadersSize() const = 0;

#if ENABLE_ZMQ
    virtual int64_t GetInvalidTxZMQMaxMessageSize() const = 0;
#endif

    virtual unsigned int GetMaxProtocolRecvPayloadLength() const = 0;
    virtual unsigned int GetMaxProtocolSendPayloadLength() const = 0;
    virtual unsigned int GetRecvInvQueueFactor() const = 0;
    virtual uint64_t GetMaxCoinsDbOpenFiles() const = 0;
    virtual uint64_t GetCoinsDBMaxFileSize() const = 0;
    virtual uint64_t GetMaxMempoolSizeDisk() const = 0;
    virtual uint64_t GetMempoolMaxPercentCPFP() const = 0;
    virtual bool GetDisableBIP30Checks() const = 0;

    // Double-Spend processing parameters
    virtual DSAttemptHandler::NotificationLevel GetDoubleSpendNotificationLevel() const = 0;
    virtual int GetDoubleSpendEndpointFastTimeout() const = 0;
    virtual int GetDoubleSpendEndpointSlowTimeout() const = 0;
    virtual uint64_t GetDoubleSpendEndpointSlowRatePerHour() const = 0;
    virtual int GetDoubleSpendEndpointPort() const = 0;
    virtual uint64_t GetDoubleSpendTxnRemember() const = 0;
    virtual uint64_t GetDoubleSpendEndpointBlacklistSize() const = 0;
    virtual std::set<std::string> GetDoubleSpendEndpointSkipList() const = 0;
    virtual uint64_t GetDoubleSpendEndpointMaxCount() const = 0;
    virtual uint64_t GetDoubleSpendNumFastThreads() const = 0;
    virtual uint64_t GetDoubleSpendNumSlowThreads() const = 0;
    virtual uint64_t GetDoubleSpendQueueMaxMemory() const = 0;
    virtual std::string GetDoubleSpendDetectedWebhookAddress() const = 0;
    virtual int16_t GetDoubleSpendDetectedWebhookPort() const = 0;
    virtual std::string GetDoubleSpendDetectedWebhookPath() const = 0;
    virtual uint64_t GetDoubleSpendDetectedWebhookMaxTxnSize() const = 0;

    virtual std::int32_t GetSoftConsensusFreezeDuration() const = 0;

    // Safe mode params
    virtual std::string GetSafeModeWebhookAddress() const = 0;
    virtual int16_t GetSafeModeWebhookPort() const = 0;
    virtual std::string GetSafeModeWebhookPath() const = 0;
    virtual int64_t GetSafeModeMaxForkDistance() const = 0;
    virtual int64_t GetSafeModeMinForkLength() const = 0;
    virtual int64_t GetSafeModeMinForkHeightDifference() const = 0;;

    // MinerID
    virtual bool GetMinerIdEnabled() const = 0;
    virtual uint64_t GetMinerIdCacheSize() const = 0;
    virtual uint64_t GetMinerIdsNumToKeep() const = 0;
    virtual uint32_t GetMinerIdReputationM() const = 0;
    virtual uint32_t GetMinerIdReputationN() const = 0;
    virtual double GetMinerIdReputationMScale() const = 0;
    virtual std::string GetMinerIdGeneratorAddress() const = 0;
    virtual int16_t GetMinerIdGeneratorPort() const = 0;
    virtual std::string GetMinerIdGeneratorPath() const = 0;
    virtual std::string GetMinerIdGeneratorAlias() const = 0;

    // Detect selfish mining
    virtual bool GetDetectSelfishMining() const = 0;
    virtual int64_t GetMinBlockMempoolTimeDifferenceSelfish() const = 0;
    virtual uint64_t GetSelfishTxThreshold() const = 0;

    // Mempool syncing
    virtual int64_t GetMempoolSyncAge() const = 0;
    virtual int64_t GetMempoolSyncPeriod() const = 0;

protected:
    virtual ~Config() = default;
};

// NOLINTNEXTLINE(cppcoreguidelines-virtual-class-destructor, cppcoreguidelines-special-member-functions)
class ConfigInit : public Config {
public:

    // Policies settings for script eval/verify
    virtual bool SetMaxOpsPerScriptPolicy(int64_t maxOpsPerScriptPolicyIn, std::string* error) = 0;
    virtual bool SetMaxScriptNumLengthPolicy(int64_t maxScriptNumLengthIn, std::string* err = nullptr) = 0;
    virtual bool SetMaxScriptSizePolicy(int64_t maxScriptSizePolicyIn, std::string* err = nullptr) = 0;
    virtual bool SetMaxPubKeysPerMultiSigPolicy(int64_t maxPubKeysPerMultiSigIn, std::string* err = nullptr) = 0;
    virtual bool SetMaxStackMemoryUsage(int64_t maxStackMemoryUsageConsensusIn, int64_t maxStackMemoryUsagePolicyIn, std::string* err = nullptr) = 0;

    virtual bool SetGenesisActivationHeight(int32_t genesisActivationHeightIn, std::string* err = nullptr) = 0;
    virtual bool SetChronicleActivationHeight(int32_t chronicleActivationHeightIn, std::string* err = nullptr) = 0;

    // used to specify default block size related parameters
    virtual void SetDefaultBlockSizeParams(const DefaultBlockSizeParams& params) = 0;
    virtual bool SetBlockSizeActivationTime(int64_t activationTime, std::string* err = nullptr) = 0;
    virtual bool SetMaxTxSizePolicy(int64_t value, std::string* err = nullptr) = 0;
    virtual bool SetMinConsolidationFactor(int64_t value, std::string* err = nullptr) = 0;
    virtual bool SetMaxConsolidationInputScriptSize(int64_t value, std::string* err = nullptr) = 0;
    virtual bool SetMinConfConsolidationInput(int64_t value, std::string* err = nullptr) = 0;
    virtual bool SetAcceptNonStdConsolidationInput(bool flagValue, std::string* err = nullptr) = 0;
    virtual void SetMinFeePerKB(CFeeRate amt) = 0;
    virtual bool SetDustRelayFee(Amount amt, std::string* err = nullptr) = 0;
    virtual bool SetDustLimitFactor(int64_t factor, std::string* err = nullptr) = 0;
    virtual void SetPreferredBlockFileSize(uint64_t preferredBlockFileSize) = 0;
    virtual void SetDataCarrierSize(uint64_t dataCarrierSize) = 0;
    virtual void SetDataCarrier(bool dataCarrier) = 0;
    virtual void SetPermitBareMultisig(bool permit) = 0;
    virtual bool SetLimitAncestorCount(int64_t limitAncestorCount, std::string* err = nullptr) = 0;
    virtual void SetTestBlockCandidateValidity(bool test) = 0;
    virtual void SetFactorMaxSendQueuesBytes(uint64_t factorMaxSendQueuesBytes) = 0;
    virtual void SetMiningCandidateBuilder(mining::CMiningFactory::BlockAssemblerType type) = 0;
    virtual bool SetMaxConcurrentAsyncTasksPerNode(
        int maxConcurrentAsyncTasksPerNode,
        std::string* error = nullptr) = 0;
    virtual bool SetBlockScriptValidatorsParams(
        int maxParallelBlocks,
        int perValidatorScriptThreadsCount,
        int perValidatorTxnThreadsCount,
        int perValidatorThreadMaxBatchSize,
        std::string* error = nullptr) = 0;
    /** Sets the maximum policy number of sigops we're willing to relay/mine in a single tx */
    virtual bool SetMaxTxSigOpsCountPolicy(int64_t maxTxSigOpsCountIn, std::string* err = nullptr) = 0;
    virtual bool SetMaxStdTxnValidationDuration(int ms, std::string* err = nullptr) = 0;
    virtual bool SetMaxNonStdTxnValidationDuration(int ms, std::string* err = nullptr) = 0;
    virtual bool SetMaxTxnValidatorAsyncTasksRunDuration(int ms, std::string* err = nullptr) = 0;
    virtual bool SetMaxTxnChainValidationBudget(int ms, std::string* err = nullptr) = 0;
    virtual void SetValidationClockCPU(bool enable) = 0;
    virtual bool SetPTVTaskScheduleStrategy(PTVTaskScheduleStrategy strategy, std::string* err = nullptr) = 0;

    virtual bool SetGenesisGracefulPeriod(int64_t genesisGracefulPeriodIn, std::string* err = nullptr) = 0;
    virtual bool SetChronicleGracefulPeriod(int64_t chronicleGracefulPeriodIn, std::string* err = nullptr) = 0;
    virtual void SetAcceptNonStandardOutput(bool accept) = 0;
    virtual void SetRequireStandard(bool require) = 0;
    virtual bool SetMaxCoinsViewCacheSize(int64_t max, std::string* err) = 0;
    virtual bool SetMaxCoinsProviderCacheSize(int64_t max, std::string* err) = 0;
    virtual bool SetMaxCoinsDbOpenFiles(int64_t max, std::string* err) = 0;
    virtual bool SetCoinsDBMaxFileSize(int64_t max, std::string* err) = 0;
    virtual void SetInvalidBlocks(const std::set<uint256>& hashes) = 0;
    virtual void SetBanClientUA(std::set<std::string> uaClients) = 0;
    virtual void SetAllowClientUA(std::set<std::string> uaClients) = 0;
    virtual bool SetMaxMerkleTreeDiskSpace(int64_t maxDiskSpace, std::string* err = nullptr) = 0;
    virtual bool SetPreferredMerkleTreeFileSize(int64_t preferredFileSize, std::string* err = nullptr) = 0;
    virtual bool SetMaxMerkleTreeMemoryCacheSize(int64_t maxMemoryCacheSize, std::string* err = nullptr) = 0;
    virtual bool SetMaxMempool(int64_t maxMempool, std::string* err) = 0;
    virtual bool SetMaxMempoolSizeDisk(int64_t maxMempoolSizeDisk, std::string* err) = 0;
    virtual bool SetMempoolMaxPercentCPFP(int64_t mempoolMaxPercentCPFP, std::string* err) = 0;
    virtual bool SetMemPoolExpiry(int64_t memPoolExpiry, std::string* err) = 0;
    virtual bool SetMaxOrphanTxSize(int64_t maxOrphanTxSize, std::string* err) = 0;
    virtual bool SetMaxOrphansInBatchPercentage(uint64_t percent, std::string* err) = 0;
    virtual bool SetMaxInputsForSecondLayerOrphan(uint64_t maxInputs, std::string* err) = 0;
    virtual bool SetStopAtHeight(int32_t StopAtHeight, std::string* err) = 0;
    virtual bool SetPromiscuousMempoolFlags(int64_t promiscuousMempoolFlags, std::string* err) = 0;
    virtual bool AddInvalidTxSink(const std::string& sink, std::string* err = nullptr) = 0;
    virtual bool SetInvalidTxFileSinkMaxDiskUsage(int64_t max, std::string* err = nullptr) = 0;
    virtual bool SetInvalidTxFileSinkEvictionPolicy(std::string policy, std::string* err = nullptr) = 0;
    virtual void SetEnableAssumeWhitelistedBlockDepth(bool enabled) = 0;
    virtual bool SetAssumeWhitelistedBlockDepth(int64_t depth, std::string* err = nullptr) = 0;

    virtual bool SetMinBlocksToKeep(int32_t minblocks, std::string* err = nullptr) = 0;
    virtual bool SetBlockValidationTxBatchSize(int64_t size, std::string* err = nullptr) = 0;

    // Block download
    virtual bool SetBlockStallingMinDownloadSpeed(int64_t min, std::string* err = nullptr) = 0;
    virtual bool SetBlockStallingTimeout(int64_t timeout, std::string* err = nullptr) = 0;
    virtual bool SetBlockDownloadWindow(int64_t window, std::string* err = nullptr) = 0;
    virtual bool SetBlockDownloadLowerWindow(int64_t window, std::string* err = nullptr) = 0;
    virtual bool SetBlockDownloadSlowFetchTimeout(int64_t timeout, std::string* err = nullptr) = 0;
    virtual bool SetBlockDownloadMaxParallelFetch(int64_t max, std::string* err = nullptr) = 0;
    virtual bool SetBlockDownloadTimeoutBase(int64_t max, std::string* err = nullptr) = 0;
    virtual bool SetBlockDownloadTimeoutBaseIBD(int64_t max, std::string* err = nullptr) = 0;
    virtual bool SetBlockDownloadTimeoutPerPeer(int64_t max, std::string* err = nullptr) = 0;

    // P2P parameters
    virtual bool SetP2PHandshakeTimeout(int64_t timeout, std::string* err = nullptr) = 0;
    virtual bool SetStreamSendRateLimit(int64_t limit, std::string* err = nullptr) = 0;
    virtual bool SetBanScoreThreshold(int64_t threshold, std::string* err = nullptr) = 0;
    virtual bool SetBlockTxnMaxPercent(unsigned int percent, std::string* err = nullptr) = 0;
    virtual bool SetMultistreamsEnabled(bool enabled, std::string* err = nullptr) = 0;
    virtual bool SetWhitelistRelay(bool relay, std::string* err = nullptr) = 0;
    virtual bool SetWhitelistForceRelay(bool relay, std::string* err = nullptr) = 0;
    virtual bool SetRejectMempoolRequest(bool reject, std::string* err = nullptr) = 0;
    virtual bool SetDropMessageTest(int64_t val, std::string* err = nullptr) = 0;
    virtual bool SetInvalidChecksumInterval(int64_t val, std::string* err = nullptr) = 0;
    virtual bool SetInvalidChecksumFreq(int64_t val, std::string* err = nullptr) = 0;
    virtual bool SetFeeFilter(bool feefilter, std::string* err = nullptr) = 0;
    virtual bool SetMaxAddNodeConnections(int16_t max, std::string* err = nullptr) = 0;
    virtual bool SetMaxRecvBuffer(int64_t max, std::string* err = nullptr) = 0;

    // RPC parameters
    virtual bool SetWebhookClientNumThreads(int64_t num, std::string* err) = 0;
    virtual bool SetWebhookClientMaxResponseBodySize(int64_t size, std::string* err = nullptr) = 0;
    virtual bool SetWebhookClientMaxResponseHeadersSize(int64_t size, std::string* err = nullptr) = 0;

    virtual bool SetDisableBIP30Checks(bool disable, std::string* err = nullptr) = 0;

#if ENABLE_ZMQ
    virtual bool SetInvalidTxZMQMaxMessageSize(int64_t max, std::string* err = nullptr) = 0;
#endif

    virtual bool SetMaxProtocolRecvPayloadLength(uint64_t value, std::string* err) = 0;
    virtual bool SetRecvInvQueueFactor(uint64_t value, std::string* err) = 0;
    virtual bool SetLimitSecondaryMempoolAncestorCount(int64_t limitSecondaryMempoolAncestorCountIn, std::string* err = nullptr) = 0;

    // Reset state of this object to match a newly constructed one.
    // Used in constructor and for unit testing to always start with a clean
    // state
    virtual void Reset() = 0;

    // Check maxtxnvalidatorasynctasksrunduration, maxstdtxvalidationduration  and maxnonstdtxvalidationduration values
    bool CheckTxValidationDurations(std::string& err)
    {
        if (!(this->GetMaxStdTxnValidationDuration().count() < this->GetMaxNonStdTxnValidationDuration().count()))
        {
            err = "maxstdtxvalidationduration must be less than maxnonstdtxvalidationduration";
            return false;
        }

        if(this->GetMaxTxnValidatorAsyncTasksRunDuration().count() <= this->GetMaxNonStdTxnValidationDuration().count())
        {
            err = "maxtxnvalidatorasynctasksrunduration must be greater than maxnonstdtxvalidationduration";
            return false;
        }

        return true;
    }


    // Double-Spend processing parameters
    virtual bool SetDoubleSpendNotificationLevel(int level, std::string* err) = 0;
    virtual bool SetDoubleSpendEndpointFastTimeout(int timeout, std::string* err) = 0;
    virtual bool SetDoubleSpendEndpointSlowTimeout(int timeout, std::string* err) = 0;
    virtual bool SetDoubleSpendEndpointSlowRatePerHour(int64_t rate, std::string* err) = 0;
    virtual bool SetDoubleSpendEndpointPort(int port, std::string* err) = 0;
    virtual bool SetDoubleSpendTxnRemember(int64_t size, std::string* err) = 0;
    virtual bool SetDoubleSpendEndpointBlacklistSize(int64_t size, std::string* err) = 0;
    virtual bool SetDoubleSpendEndpointSkipList(const std::string& skip, std::string* err) = 0;
    virtual bool SetDoubleSpendEndpointMaxCount(int64_t max, std::string* err) = 0;
    virtual bool SetDoubleSpendNumFastThreads(int64_t num, std::string* err) = 0;
    virtual bool SetDoubleSpendNumSlowThreads(int64_t num, std::string* err) = 0;
    virtual bool SetDoubleSpendQueueMaxMemory(int64_t max, std::string* err) = 0;
    virtual bool SetDoubleSpendDetectedWebhookURL(const std::string& url, std::string* err = nullptr) = 0;
    virtual bool SetDoubleSpendDetectedWebhookMaxTxnSize(int64_t max, std::string* err = nullptr) = 0;

    virtual bool SetSoftConsensusFreezeDuration( std::int64_t duration, std::string* err ) = 0;
    // Safe mode params
    virtual bool SetSafeModeWebhookURL(const std::string& url, std::string* err = nullptr) = 0;
    virtual bool SetSafeModeMaxForkDistance(int64_t distance, std::string* err) = 0;
    virtual bool SetSafeModeMinForkLength(int64_t length, std::string* err) = 0;
    virtual bool SetSafeModeMinForkHeightDifference(int64_t heightDifference, std::string* err) = 0;


    // MinerID
    virtual bool SetMinerIdEnabled(bool enabled, std::string* err) = 0;
    virtual bool SetMinerIdCacheSize(int64_t size, std::string* err) = 0;
    virtual bool SetMinerIdsNumToKeep(int64_t num, std::string* err) = 0;
    virtual bool SetMinerIdReputationM(int64_t num, std::string* err) = 0;
    virtual bool SetMinerIdReputationN(int64_t num, std::string* err) = 0;
    virtual bool SetMinerIdReputationMScale(double num, std::string* err) = 0;
    virtual bool SetMinerIdGeneratorURL(const std::string& url, std::string* err) = 0;
    virtual bool SetMinerIdGeneratorAlias(const std::string& alias, std::string* err) = 0;

    // Detect selfish mining
    virtual void SetDetectSelfishMining(bool detectSelfishMining) = 0;
    virtual bool SetMinBlockMempoolTimeDifferenceSelfish(int64_t minBlockMempoolTimeDiffIn, std::string* err = nullptr) = 0;
    virtual bool SetSelfishTxThreshold(uint64_t selfishTxPercentThreshold, std::string* err = nullptr) = 0;

    // Mempool syncing
    virtual bool SetMempoolSyncAge(int64_t age, std::string* err = nullptr) = 0;
    virtual bool SetMempoolSyncPeriod(int64_t period, std::string* err = nullptr) = 0;

protected:
    // NOLINTNEXTLINE(cppcoreguidelines-explicit-virtual-functions)
    ~ConfigInit() = default;
};

class GlobalConfig : public ConfigInit {
public:
    GlobalConfig();

    const ConfigScriptPolicy& GetConfigScriptPolicy() const override;

    // Set block size related default. This must be called after constructing GlobalConfig
    void SetDefaultBlockSizeParams(const DefaultBlockSizeParams &params) override;

    bool SetMaxBlockSize(uint64_t maxBlockSize, std::string* err = nullptr) override;
    uint64_t GetMaxBlockSize() const override;

    bool SetMaxGeneratedBlockSize(uint64_t maxGeneratedBlockSize, std::string* err = nullptr) override;
    uint64_t GetMaxGeneratedBlockSize() const override;   
    uint64_t GetMaxGeneratedBlockSize(int64_t nMedianTimePast) const override;
    bool MaxGeneratedBlockSizeOverridden() const override;

    bool SetBlockSizeActivationTime(int64_t activationTime, std::string* err = nullptr) override;
    int64_t GetBlockSizeActivationTime() const override;

    const CChainParams &GetChainParams() const override;

    bool SetMaxTxSizePolicy(int64_t value, std::string* err = nullptr) override;
    uint64_t GetMaxTxSize(ProtocolEra era, bool isConsensus) const  override;

    bool SetMinConsolidationFactor(int64_t value, std::string* err = nullptr) override;
    uint64_t GetMinConsolidationFactor() const  override;

    bool SetMaxConsolidationInputScriptSize(int64_t value, std::string* err = nullptr) override;
    uint64_t GetMaxConsolidationInputScriptSize() const  override;

    bool SetMinConfConsolidationInput(int64_t value, std::string* err = nullptr) override;
    uint64_t GetMinConfConsolidationInput() const override;

    bool SetAcceptNonStdConsolidationInput(bool flagValue, std::string* err = nullptr) override;
    bool GetAcceptNonStdConsolidationInput() const  override;

    void SetMinFeePerKB(CFeeRate amt) override;
    CFeeRate GetMinFeePerKB() const override;

    bool SetDustRelayFee(Amount amt, std::string* err = nullptr) override;
    CFeeRate GetDustRelayFee() const override;

    bool SetDustLimitFactor(int64_t factor, std::string* err = nullptr) override;
    int64_t GetDustLimitFactor() const override;

    void SetPreferredBlockFileSize(uint64_t preferredBlockFileSize) override;
    uint64_t GetPreferredBlockFileSize() const override;

    void SetDataCarrierSize(uint64_t dataCarrierSize) override;
    uint64_t GetDataCarrierSize() const override;

    void SetDataCarrier(bool dataCarrier) override;
    bool GetDataCarrier() const override;

    bool SetLimitAncestorCount(int64_t limitAncestorCount, std::string* err = nullptr) override;
    uint64_t GetLimitAncestorCount() const override;

    bool SetLimitSecondaryMempoolAncestorCount(int64_t limitSecondaryMempoolAncestorCountIn, std::string* err = nullptr) override;
    uint64_t GetLimitSecondaryMempoolAncestorCount() const override;
    
    void SetTestBlockCandidateValidity(bool test) override;
    bool GetTestBlockCandidateValidity() const override;

    void SetFactorMaxSendQueuesBytes(uint64_t factorMaxSendQueuesBytes) override;
    uint64_t GetFactorMaxSendQueuesBytes() const override;
    uint64_t GetMaxSendQueuesBytes() const override;

    void SetMiningCandidateBuilder(mining::CMiningFactory::BlockAssemblerType type) override;
    mining::CMiningFactory::BlockAssemblerType GetMiningCandidateBuilder() const override;

    bool SetGenesisActivationHeight(int32_t genesisActivationHeightIn, std::string* err = nullptr) override;
    int32_t GetGenesisActivationHeight() const override;
    bool SetChronicleActivationHeight(int32_t chronicleActivationHeightIn, std::string* err = nullptr) override;
    int32_t GetChronicleActivationHeight() const override;

    bool SetMaxConcurrentAsyncTasksPerNode(
        int maxConcurrentAsyncTasksPerNode,
        std::string* error = nullptr) override;
    int GetMaxConcurrentAsyncTasksPerNode() const override;

    bool SetBlockScriptValidatorsParams(
        int maxParallelBlocks,
        int perValidatorScriptThreadsCount,
        int perValidatorTxnThreadsCount,
        int perValidatorThreadMaxBatchSize,
        std::string* error = nullptr) override;
    int GetMaxParallelBlocks() const override;
    int GetPerBlockTxnValidatorThreadsCount() const override;
    int GetPerBlockScriptValidatorThreadsCount() const override;
    int GetPerBlockScriptValidationMaxBatchSize() const override;

    bool SetMaxOpsPerScriptPolicy(int64_t maxOpsPerScriptPolicyIn, std::string* error) override;
    uint64_t GetMaxOpsPerScript(bool isGenesisEnabled, bool consensus) const override;

    bool SetMaxTxSigOpsCountPolicy(int64_t maxTxSigOpsCountIn, std::string* err = nullptr) override;
    uint64_t GetMaxTxSigOpsCountConsensusBeforeGenesis() const override;
    uint64_t GetMaxTxSigOpsCountPolicy(ProtocolEra era) const override;

    uint64_t GetMaxBlockSigOpsConsensusBeforeGenesis(uint64_t blockSize) const override;

    bool SetMaxPubKeysPerMultiSigPolicy(int64_t maxPubKeysPerMultiSigIn, std::string* error = nullptr) override;
    uint64_t GetMaxPubKeysPerMultiSig(bool isGenesisEnabled, bool consensus) const override;

    bool SetMaxStdTxnValidationDuration(int ms, std::string* err = nullptr) override;
    std::chrono::milliseconds GetMaxStdTxnValidationDuration() const override;

    bool SetMaxNonStdTxnValidationDuration(int ms, std::string* err = nullptr) override;
    std::chrono::milliseconds GetMaxNonStdTxnValidationDuration() const override;

    bool SetMaxTxnValidatorAsyncTasksRunDuration(int ms, std::string* err = nullptr) override;
    std::chrono::milliseconds GetMaxTxnValidatorAsyncTasksRunDuration() const override;

    bool SetMaxTxnChainValidationBudget(int ms, std::string* err = nullptr) override;
    std::chrono::milliseconds GetMaxTxnChainValidationBudget() const override;

    void SetValidationClockCPU(bool enable) override;
    bool GetValidationClockCPU() const override;
    
    bool SetPTVTaskScheduleStrategy(PTVTaskScheduleStrategy strategy, std::string* err = nullptr) override;
    PTVTaskScheduleStrategy GetPTVTaskScheduleStrategy() const override;

    bool SetMaxStackMemoryUsage(int64_t maxStackMemoryUsageConsensusIn, int64_t maxStackMemoryUsagePolicyIn, std::string* err = nullptr) override;
    uint64_t GetMaxStackMemoryUsage(bool isGenesisEnabled, bool consensus) const override;

    
    bool SetMaxScriptSizePolicy(int64_t maxScriptSizePolicyIn, std::string* err = nullptr) override;
    uint64_t GetMaxScriptSize(bool isGenesisEnabled, bool isConsensus) const override;

    bool SetMaxScriptNumLengthPolicy(int64_t maxScriptNumLengthIn, std::string* err = nullptr) override;
    uint64_t GetMaxScriptNumLength(ProtocolEra era, bool isConsensus) const override;

    bool SetGenesisGracefulPeriod(int64_t genesisGracefulPeriodIn, std::string* err = nullptr) override;
    uint64_t GetGenesisGracefulPeriod() const override;
    bool SetChronicleGracefulPeriod(int64_t chronicleGracefulPeriodIn, std::string* err = nullptr) override;
    uint64_t GetChronicleGracefulPeriod() const override;

    void SetAcceptNonStandardOutput(bool accept) override;
    bool GetAcceptNonStandardOutput(ProtocolEra era) const override;
    void SetRequireStandard(bool require) override;
    void SetPermitBareMultisig(bool permit) override;

    bool SetMaxCoinsViewCacheSize(int64_t max, std::string* err) override;
    uint64_t GetMaxCoinsViewCacheSize() const override {return data->mMaxCoinsViewCacheSize;}

    bool SetMaxCoinsProviderCacheSize(int64_t max, std::string* err) override;
    uint64_t GetMaxCoinsProviderCacheSize() const override {return data->mMaxCoinsProviderCacheSize;}

    bool SetMaxCoinsDbOpenFiles(int64_t max, std::string* err) override;
    uint64_t GetMaxCoinsDbOpenFiles() const override {return data->mMaxCoinsDbOpenFiles; }

    bool SetCoinsDBMaxFileSize(int64_t max, std::string* err) override;
    uint64_t GetCoinsDBMaxFileSize() const override {return data->mMaxCoinsDBFileSize; }

    void SetInvalidBlocks(const std::set<uint256>& hashes) override; 
    const std::set<uint256>& GetInvalidBlocks() const override;
    bool IsBlockInvalidated(const uint256& hash) const override;

    void SetBanClientUA(std::set<std::string> uaClients) override;
    void SetAllowClientUA(std::set<std::string> uaClients) override;
    bool IsClientUABanned(const std::string uaClient) const override;
    bool SetMaxMerkleTreeDiskSpace(int64_t maxDiskSpace, std::string* err = nullptr) override;
    uint64_t GetMaxMerkleTreeDiskSpace() const override;
    bool SetPreferredMerkleTreeFileSize(int64_t preferredFileSize, std::string* err = nullptr) override;
    uint64_t GetPreferredMerkleTreeFileSize() const override;
    bool SetMaxMerkleTreeMemoryCacheSize(int64_t maxMemoryCacheSize, std::string* err = nullptr) override;
    uint64_t GetMaxMerkleTreeMemoryCacheSize() const override;

    bool SetMaxMempool(int64_t maxMempool, std::string* err) override;
    uint64_t GetMaxMempool() const override;

    bool SetMaxMempoolSizeDisk(int64_t maxMempoolSizeDisk, std::string* err) override;
    uint64_t GetMaxMempoolSizeDisk() const override;

    bool SetMempoolMaxPercentCPFP(int64_t mempoolMaxPercentCPFP, std::string* err) override;
    uint64_t GetMempoolMaxPercentCPFP() const override;

    bool SetMemPoolExpiry(int64_t memPoolExpiry, std::string* err) override;
    uint64_t GetMemPoolExpiry() const override;

    bool SetMaxOrphanTxSize(int64_t maxOrphanTxSize, std::string* err) override;
    uint64_t GetMaxOrphanTxSize() const override;

    bool SetMaxOrphansInBatchPercentage(uint64_t percent, std::string* err) override;
    uint64_t GetMaxOrphansInBatchPercentage() const override;
    
    bool SetMaxInputsForSecondLayerOrphan(uint64_t maxInputs, std::string* err) override;
    uint64_t GetMaxInputsForSecondLayerOrphan() const override;

    bool SetStopAtHeight(int32_t stopAtHeight, std::string* err) override;
    int32_t GetStopAtHeight() const override;

    bool SetPromiscuousMempoolFlags(int64_t promiscuousMempoolFlags, std::string* err) override;
    uint64_t GetPromiscuousMempoolFlags() const override;
    bool IsSetPromiscuousMempoolFlags() const override;

    bool AddInvalidTxSink(const std::string& sink, std::string* err) override;
    std::set<std::string> GetInvalidTxSinks() const override;
    std::set<std::string> GetAvailableInvalidTxSinks() const override;

    bool SetInvalidTxFileSinkMaxDiskUsage(int64_t max, std::string* err) override;
    int64_t GetInvalidTxFileSinkMaxDiskUsage() const override;

    bool SetInvalidTxFileSinkEvictionPolicy(std::string policy, std::string* err = nullptr) override;
    InvalidTxEvictionPolicy GetInvalidTxFileSinkEvictionPolicy() const override;

    void SetEnableAssumeWhitelistedBlockDepth(bool enabled) override;
    bool GetEnableAssumeWhitelistedBlockDepth() const override;
    bool SetAssumeWhitelistedBlockDepth(int64_t depth, std::string* err = nullptr) override;
    int32_t GetAssumeWhitelistedBlockDepth() const override;

    bool SetMinBlocksToKeep(int32_t minblocks, std::string* err = nullptr) override;
    int32_t GetMinBlocksToKeep() const override;
    bool SetBlockValidationTxBatchSize(int64_t size, std::string* err = nullptr) override;
    uint64_t GetBlockValidationTxBatchSize() const override;

    // Block download
    bool SetBlockStallingMinDownloadSpeed(int64_t min, std::string* err = nullptr) override;
    uint64_t GetBlockStallingMinDownloadSpeed() const override;
    bool SetBlockStallingTimeout(int64_t timeout, std::string* err = nullptr) override;
    int64_t GetBlockStallingTimeout() const override;
    bool SetBlockDownloadWindow(int64_t window, std::string* err = nullptr) override;
    int64_t GetBlockDownloadWindow() const override;
    bool SetBlockDownloadLowerWindow(int64_t window, std::string* err = nullptr) override;
    int64_t GetBlockDownloadLowerWindow() const override;
    bool SetBlockDownloadSlowFetchTimeout(int64_t timeout, std::string* err = nullptr) override;
    int64_t GetBlockDownloadSlowFetchTimeout() const override;
    bool SetBlockDownloadMaxParallelFetch(int64_t max, std::string* err = nullptr) override;
    uint64_t GetBlockDownloadMaxParallelFetch() const override;
    bool SetBlockDownloadTimeoutBase(int64_t max, std::string* err = nullptr) override;
    int64_t GetBlockDownloadTimeoutBase() const override;
    bool SetBlockDownloadTimeoutBaseIBD(int64_t max, std::string* err = nullptr) override;
    int64_t GetBlockDownloadTimeoutBaseIBD() const override;
    bool SetBlockDownloadTimeoutPerPeer(int64_t max, std::string* err = nullptr) override;
    int64_t GetBlockDownloadTimeoutPerPeer() const override;

    // P2P parameters
    bool SetP2PHandshakeTimeout(int64_t timeout, std::string* err = nullptr) override;
    int64_t GetP2PHandshakeTimeout() const override { return data->p2pHandshakeTimeout; }
    bool SetStreamSendRateLimit(int64_t limit, std::string* err = nullptr) override;
    int64_t GetStreamSendRateLimit() const override;
    bool SetBanScoreThreshold(int64_t threshold, std::string* err = nullptr) override;
    unsigned int GetBanScoreThreshold() const override;
    bool SetBlockTxnMaxPercent(unsigned int percent, std::string* err = nullptr) override;
    unsigned int GetBlockTxnMaxPercent() const override;
    bool SetMultistreamsEnabled(bool enabled, std::string* err = nullptr) override;
    bool GetMultistreamsEnabled() const override;
    bool SetWhitelistRelay(bool relay, std::string* err = nullptr) override;
    bool GetWhitelistRelay() const override;
    bool SetWhitelistForceRelay(bool relay, std::string* err = nullptr) override;
    bool GetWhitelistForceRelay() const override;
    bool SetRejectMempoolRequest(bool reject, std::string* err = nullptr) override;
    bool GetRejectMempoolRequest() const override;
    bool SetDropMessageTest(int64_t val, std::string* err = nullptr) override;
    bool DoDropMessageTest() const override;
    uint64_t GetDropMessageTest() const override;
    bool SetInvalidChecksumInterval(int64_t val, std::string* err = nullptr) override;
    unsigned int GetInvalidChecksumInterval() const override;
    bool SetInvalidChecksumFreq(int64_t val, std::string* err = nullptr) override;
    unsigned int GetInvalidChecksumFreq() const override;
    bool SetFeeFilter(bool feefilter, std::string* err = nullptr) override;
    bool GetFeeFilter() const override;
    bool SetMaxAddNodeConnections(int16_t max, std::string* err = nullptr) override;
    uint16_t GetMaxAddNodeConnections() const override;
    bool SetMaxRecvBuffer(int64_t max, std::string* err = nullptr) override;
    uint64_t GetMaxRecvBuffer() const override;

    // RPC parameters
    bool SetWebhookClientNumThreads(int64_t num, std::string* err) override;
    uint64_t GetWebhookClientNumThreads() const override;
    bool SetWebhookClientMaxResponseBodySize(int64_t size, std::string* err = nullptr) override;
    int64_t GetWebhookClientMaxResponseBodySize() const override;
    bool SetWebhookClientMaxResponseHeadersSize(int64_t size, std::string* err = nullptr) override;
    int64_t GetWebhookClientMaxResponseHeadersSize() const override;

    bool SetDisableBIP30Checks(bool disable, std::string* err = nullptr) override;
    bool GetDisableBIP30Checks() const override;

#if ENABLE_ZMQ
    bool SetInvalidTxZMQMaxMessageSize(int64_t max, std::string* err = nullptr) override;
    int64_t GetInvalidTxZMQMaxMessageSize() const override;
#endif

    bool SetMaxProtocolRecvPayloadLength(uint64_t value, std::string* err) override;
    bool SetRecvInvQueueFactor(uint64_t value, std::string* err) override;
    unsigned int GetMaxProtocolRecvPayloadLength() const override;
    unsigned int GetMaxProtocolSendPayloadLength() const override;
    unsigned int GetRecvInvQueueFactor() const override;

    // Double-Spend processing parameters
    bool SetDoubleSpendNotificationLevel(int level, std::string* err) override;
    DSAttemptHandler::NotificationLevel GetDoubleSpendNotificationLevel() const override;
    bool SetDoubleSpendEndpointFastTimeout(int timeout, std::string* err) override;
    int GetDoubleSpendEndpointFastTimeout() const override;
    bool SetDoubleSpendEndpointSlowTimeout(int timeout, std::string* err) override;
    int GetDoubleSpendEndpointSlowTimeout() const override;
    bool SetDoubleSpendEndpointSlowRatePerHour(int64_t rate, std::string* err) override;
    uint64_t GetDoubleSpendEndpointSlowRatePerHour() const override;
    bool SetDoubleSpendEndpointPort(int port, std::string* err) override;
    int GetDoubleSpendEndpointPort() const override;
    bool SetDoubleSpendTxnRemember(int64_t size, std::string* err) override;
    uint64_t GetDoubleSpendTxnRemember() const override;
    bool SetDoubleSpendEndpointBlacklistSize(int64_t size, std::string* err) override;
    uint64_t GetDoubleSpendEndpointBlacklistSize() const override;
    bool SetDoubleSpendEndpointSkipList(const std::string& skip, std::string* err) override;
    std::set<std::string> GetDoubleSpendEndpointSkipList() const override;
    bool SetDoubleSpendEndpointMaxCount(int64_t max, std::string* err) override;
    uint64_t GetDoubleSpendEndpointMaxCount() const override;
    bool SetDoubleSpendNumFastThreads(int64_t num, std::string* err) override;
    uint64_t GetDoubleSpendNumFastThreads() const override;
    bool SetDoubleSpendNumSlowThreads(int64_t num, std::string* err) override;
    uint64_t GetDoubleSpendNumSlowThreads() const override;
    bool SetDoubleSpendQueueMaxMemory(int64_t max, std::string* err) override;
    uint64_t GetDoubleSpendQueueMaxMemory() const override;
    bool SetDoubleSpendDetectedWebhookURL(const std::string& url, std::string* err) override;
    std::string GetDoubleSpendDetectedWebhookAddress() const override;
    int16_t GetDoubleSpendDetectedWebhookPort() const override;
    std::string GetDoubleSpendDetectedWebhookPath() const override;
    bool SetDoubleSpendDetectedWebhookMaxTxnSize(int64_t max, std::string* err) override;
    uint64_t GetDoubleSpendDetectedWebhookMaxTxnSize() const override;

    bool SetSoftConsensusFreezeDuration( std::int64_t duration, std::string* err ) override;
    std::int32_t GetSoftConsensusFreezeDuration() const override;

    // Safe mode params
    bool SetSafeModeWebhookURL(const std::string& url, std::string* err = nullptr) override;
    std::string GetSafeModeWebhookAddress() const override;
    int16_t GetSafeModeWebhookPort() const override;
    std::string GetSafeModeWebhookPath() const override;
    int64_t GetSafeModeMaxForkDistance() const override;
    bool SetSafeModeMaxForkDistance(int64_t distance, std::string* err)  override;
    int64_t GetSafeModeMinForkLength() const  override;
    bool SetSafeModeMinForkLength(int64_t length, std::string* err) override;
    int64_t GetSafeModeMinForkHeightDifference() const  override;
    bool SetSafeModeMinForkHeightDifference(int64_t heightDifference, std::string* err) override;


    // MinerID
    bool SetMinerIdEnabled(bool enabled, std::string* err) override;
    bool GetMinerIdEnabled() const override;
    bool SetMinerIdCacheSize(int64_t size, std::string* err) override;
    uint64_t GetMinerIdCacheSize() const override;
    bool SetMinerIdsNumToKeep(int64_t num, std::string* err) override;
    uint64_t GetMinerIdsNumToKeep() const override;
    bool SetMinerIdReputationM(int64_t num, std::string* err) override;
    uint32_t GetMinerIdReputationM() const override;
    bool SetMinerIdReputationN(int64_t num, std::string* err) override;
    uint32_t GetMinerIdReputationN() const override;
    bool SetMinerIdReputationMScale(double num, std::string* err) override;
    double GetMinerIdReputationMScale() const override;
    std::string GetMinerIdGeneratorAddress() const override;
    int16_t GetMinerIdGeneratorPort() const override;
    std::string GetMinerIdGeneratorPath() const override;
    std::string GetMinerIdGeneratorAlias() const override;
    bool SetMinerIdGeneratorURL(const std::string& url, std::string* err) override;
    bool SetMinerIdGeneratorAlias(const std::string& alias, std::string* err) override;

    // Detect selfish mining
    bool GetDetectSelfishMining() const override;
    void SetDetectSelfishMining(bool detectSelfishMining) override;
    int64_t GetMinBlockMempoolTimeDifferenceSelfish() const override;
    bool SetMinBlockMempoolTimeDifferenceSelfish(int64_t minBlockMempoolTimeDiffIn, std::string* err = nullptr) override;
    uint64_t GetSelfishTxThreshold() const override;
    bool SetSelfishTxThreshold(uint64_t selfishTxPercentThreshold, std::string* err = nullptr) override;

    // Mempool syncing
    int64_t GetMempoolSyncAge() const override;
    bool SetMempoolSyncAge(int64_t age, std::string* err) override;
    int64_t GetMempoolSyncPeriod() const override;
    bool SetMempoolSyncPeriod(int64_t period, std::string* err) override;

    // Reset state of this object to match a newly constructed one. 
    // Used in constructor and for unit testing to always start with a clean state
    void Reset() override;

    // GetConfig() is used where read-only access to global configuration is needed.
    static Config& GetConfig();
    // GetModifiableGlobalConfig() should only be used in initialization and unit tests.
    static ConfigInit& GetModifiableGlobalConfig();

private:
    void  CheckSetDefaultCalled() const;

    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
    struct GlobalConfigData  //NOLINT(clang-analyzer-optin.performance.Padding)
    {
    private: friend class GlobalConfig;
        // All fields are initialized in Reset()
        CFeeRate feePerKB;
        CFeeRate dustRelayFee{DUST_RELAY_TX_FEE};
        int64_t dustLimitFactor;
        uint64_t preferredBlockFileSize;
        uint64_t factorMaxSendQueuesBytes;

        // Block size limits 
        // SetDefaultBlockSizeParams must be called before reading any of those
        bool  setDefaultBlockSizeParamsCalled;

        // Defines when either maxGeneratedBlockSizeBefore or maxGeneratedBlockSizeAfter is used
        int64_t blockSizeActivationTime;
        uint64_t maxBlockSize;
        // Used when SetMaxBlockSize is called with value 0
        uint64_t defaultBlockSize;
        uint64_t maxGeneratedBlockSizeBefore;
        uint64_t maxGeneratedBlockSizeAfter;
        bool maxGeneratedBlockSizeOverridden;

        uint64_t minConsolidationFactor;
        uint64_t maxConsolidationInputScriptSize;
        uint64_t minConfConsolidationInput;
        bool acceptNonStdConsolidationInput;
        uint64_t limitAncestorCount;
        uint64_t limitSecondaryMempoolAncestorCount;

        bool testBlockCandidateValidity;
        mining::CMiningFactory::BlockAssemblerType blockAssemblerType;

        int mMaxConcurrentAsyncTasksPerNode;

        int mMaxParallelBlocks;
        int mPerBlockTxnValidatorThreadsCount;
        int mPerBlockScriptValidatorThreadsCount;
        int mPerBlockScriptValidationMaxBatchSize;

        uint64_t maxTxSigOpsCountPolicy;
        uint64_t maxPubKeysPerMultiSig;

        std::chrono::milliseconds mMaxStdTxnValidationDuration;
        std::chrono::milliseconds mMaxNonStdTxnValidationDuration;
        std::chrono::milliseconds mMaxTxnValidatorAsyncTasksRunDuration;
        std::chrono::milliseconds mMaxTxnChainValidationBudget;

        bool mValidationClockCPU;

        PTVTaskScheduleStrategy mPTVTaskScheduleStrategy;

        uint64_t mMaxCoinsViewCacheSize;
        uint64_t mMaxCoinsProviderCacheSize;

        uint64_t mMaxCoinsDbOpenFiles;
        uint64_t mMaxCoinsDBFileSize;

        uint64_t mMaxMempool;
        uint64_t mMaxMempoolSizeDisk;
        uint64_t mMempoolMaxPercentCPFP;
        uint64_t mMemPoolExpiry;
        uint64_t mMaxOrphanTxSize;
        uint64_t mMaxPercentageOfOrphansInMaxBatchSize;
        uint64_t mMaxInputsForSecondLayerOrphan;
        int32_t mStopAtHeight;
        uint64_t mPromiscuousMempoolFlags;
        bool mIsSetPromiscuousMempoolFlags;

        std::set<uint256> mInvalidBlocks;
        bool enableAssumeWhitelistedBlockDepth;
        int32_t assumeWhitelistedBlockDepth;

        std::set<std::string> mBannedUAClients{DEFAULT_CLIENTUA_BAN_PATTERNS};
        std::set<std::string> mAllowedUAClients;

        uint64_t maxMerkleTreeDiskSpace;
        uint64_t preferredMerkleTreeFileSize;
        uint64_t maxMerkleTreeMemoryCacheSize;

        std::set<std::string> invalidTxSinks;
        int64_t invalidTxFileSinkSize;
        InvalidTxEvictionPolicy invalidTxFileSinkEvictionPolicy;

        int32_t minBlocksToKeep;
        uint64_t blockValidationTxBatchSize;

        // Block download
        uint64_t blockStallingMinDownloadSpeed;
        int64_t blockStallingTimeout;
        int64_t blockDownloadWindow;
        int64_t blockDownloadLowerWindow;
        int64_t blockDownloadSlowFetchTimeout;
        uint64_t blockDownloadMaxParallelFetch;
        int64_t blockDownloadTimeoutBase;
        int64_t blockDownloadTimeoutBaseIBD;
        int64_t blockDownloadTimeoutPerPeer;

        // P2P parameters
        int64_t p2pHandshakeTimeout;
        int64_t streamSendRateLimit;
        unsigned int maxProtocolRecvPayloadLength;
        unsigned int maxProtocolSendPayloadLength;
        unsigned int recvInvQueueFactor;
        unsigned int banScoreThreshold;
        unsigned int blockTxnMaxPercent;
        bool multistreamsEnabled;
        bool whitelistRelay;
        bool whitelistForceRelay;
        bool rejectMempoolRequest;
        std::optional<uint64_t> dropMessageTest;
        unsigned int invalidChecksumInterval;
        unsigned int invalidChecksumFreq;
        bool feeFilter;
        uint16_t maxAddNodeConnections;
        uint64_t maxRecvBuffer;

        // RPC parameters
        uint64_t webhookClientNumThreads;
        int64_t webhookClientMaxResponseBodySize;
        int64_t webhookClientMaxResponseHeadersSize;

        // Double-Spend parameters
        DSAttemptHandler::NotificationLevel dsNotificationLevel;
        int dsEndpointFastTimeout;
        int dsEndpointSlowTimeout;
        uint64_t dsEndpointSlowRatePerHour;
        int dsEndpointPort;
        uint64_t dsEndpointBlacklistSize;
        std::set<std::string> dsEndpointSkipList;
        uint64_t dsEndpointMaxCount;
        uint64_t dsAttemptTxnRemember;
        uint64_t dsAttemptNumFastThreads;
        uint64_t dsAttemptNumSlowThreads;
        uint64_t dsAttemptQueueMaxMemory;
        std::string dsDetectedWebhookAddress;
        int16_t dsDetectedWebhookPort;
        std::string dsDetectedWebhookPath;
        uint64_t dsDetectedWebhookMaxTxnSize;

        std::string safeModeWebhookAddress;
        int16_t safeModeWebhookPort;
        std::string safeModeWebhookPath;
        int64_t safeModeMaxForkDistance;
        int64_t safeModeMinForkLength;
        int64_t safeModeMinHeightDifference;

        // MinerID
        bool minerIdEnabled;
        uint64_t minerIdCacheSize;
        uint64_t numMinerIdsToKeep;
        uint32_t minerIdReputationM;
        uint32_t minerIdReputationN;
        double minerIdReputationMScale;
        std::string minerIdGeneratorAddress;
        int16_t minerIdGeneratorPort;
        std::string minerIdGeneratorPath;
        std::string minerIdGeneratorAlias;

        std::optional<bool> mDisableBIP30Checks;

        bool mDetectSelfishMining;
        int64_t minBlockMempoolTimeDifferenceSelfish;
        uint64_t mSelfishTxThreshold;

        // Mempool syncing
        int64_t mempoolSyncAge;
        int64_t mempoolSyncPeriod;

    #if ENABLE_ZMQ
        int64_t invalidTxZMQMaxMessageSize;
    #endif

        std::int32_t mSoftConsensusFreezeDuration;

        ConfigScriptPolicy scriptPolicysettings;

        // Only for values that can change in runtime
        mutable std::shared_mutex configMtx{};
    };
    std::shared_ptr<GlobalConfigData> data = std::make_shared<GlobalConfigData>();

protected:
    GlobalConfig(std::shared_ptr<GlobalConfigData> data);

public:
    std::shared_ptr<GlobalConfigData> getGlobalConfigData() const;
};

#endif
