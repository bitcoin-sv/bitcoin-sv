// Copyright (c) 2017 Amaury SÉCHET
// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#ifndef BITCOIN_CONFIG_H
#define BITCOIN_CONFIG_H

static_assert(sizeof(void*) >= 8, "32 bit systems are not supported");

#include "amount.h"
#include "consensus/consensus.h"
#include "miner_id/miner_id_db_defaults.h"
#include "double_spend/dsdetected_defaults.h"
#include "mining/factory.h"
#include "net/net.h"
#include "policy/policy.h"
#include "rpc/client_config.h"
#include "rpc/webhook_client_defaults.h"
#include "script/standard.h"
#include "txn_validation_config.h"
#include "validation.h"
#include "script_config.h"
#include "invalid_txn_publisher.h"
#include "txn_validator.h"

#include <boost/noncopyable.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <orphan_txns.h>
#include <shared_mutex>

class CChainParams;
struct DefaultBlockSizeParams;

// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions, cppcoreguidelines-virtual-class-destructor)
class Config : public boost::noncopyable, public CScriptConfig {
public:
    virtual uint64_t GetMaxBlockSize() const = 0;
    virtual bool SetMaxBlockSize(uint64_t maxBlockSize, std::string* err = nullptr) = 0;
    virtual uint64_t GetMaxGeneratedBlockSize() const = 0;
    virtual uint64_t GetMaxGeneratedBlockSize(int64_t nMedianTimePast) const = 0;
    virtual bool SetMaxGeneratedBlockSize(uint64_t maxGeneratedBlockSize, std::string* err = nullptr) = 0;
    virtual bool MaxGeneratedBlockSizeOverridden() const = 0;
    virtual int64_t GetBlockSizeActivationTime() const = 0;
    virtual const CChainParams &GetChainParams() const = 0;
    virtual uint64_t GetMaxTxSize(bool isGenesisEnabled, bool isConsensus) const = 0;
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
    virtual int32_t GetGenesisActivationHeight() const = 0;
    virtual int GetMaxConcurrentAsyncTasksPerNode() const = 0;
    virtual int GetMaxParallelBlocks() const = 0;
    virtual int GetPerBlockTxnValidatorThreadsCount() const = 0;
    virtual int GetPerBlockScriptValidatorThreadsCount() const = 0;
    virtual int GetPerBlockScriptValidationMaxBatchSize() const = 0;

    virtual uint64_t GetBlockValidationTxBatchSize() const = 0;

    virtual uint64_t GetMaxTxSigOpsCountConsensusBeforeGenesis() const = 0;
    virtual uint64_t GetMaxTxSigOpsCountPolicy(bool isGenesisEnabled) const = 0;
    virtual uint64_t GetMaxBlockSigOpsConsensusBeforeGenesis(uint64_t blockSize) const = 0;
    virtual std::chrono::milliseconds GetMaxStdTxnValidationDuration() const = 0;
    virtual std::chrono::milliseconds GetMaxNonStdTxnValidationDuration() const = 0;
    virtual std::chrono::milliseconds GetMaxTxnValidatorAsyncTasksRunDuration() const = 0;
    virtual bool GetValidationClockCPU() const = 0;
    virtual std::chrono::milliseconds GetMaxTxnChainValidationBudget() const = 0;
    virtual PTVTaskScheduleStrategy GetPTVTaskScheduleStrategy() const = 0;
    virtual uint64_t GetGenesisGracefulPeriod() const = 0;
    virtual bool GetAcceptNonStandardOutput(bool isGenesisEnabled) const = 0;
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

    // RPC parameters
    virtual uint64_t GetWebhookClientNumThreads() const = 0;

#if ENABLE_ZMQ
    virtual int64_t GetInvalidTxZMQMaxMessageSize() const = 0;
#endif

    virtual unsigned int GetMaxProtocolRecvPayloadLength() const = 0;
    virtual unsigned int GetMaxProtocolSendPayloadLength() const = 0;
    virtual unsigned int GetRecvInvQueueFactor() const = 0;
    virtual uint64_t GetMaxCoinsDbOpenFiles() const = 0;
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

protected:
    virtual ~Config() = default;
};

// NOLINTNEXTLINE(cppcoreguidelines-virtual-class-destructor, cppcoreguidelines-special-member-functions)
class ConfigInit : public Config {
public:
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
    virtual bool SetLimitAncestorCount(int64_t limitAncestorCount, std::string* err = nullptr) = 0;
    virtual void SetTestBlockCandidateValidity(bool test) = 0;
    virtual void SetFactorMaxSendQueuesBytes(uint64_t factorMaxSendQueuesBytes) = 0;
    virtual void SetMiningCandidateBuilder(mining::CMiningFactory::BlockAssemblerType type) = 0;
    virtual bool SetGenesisActivationHeight(int32_t genesisActivationHeightIn, std::string* err = nullptr) = 0;
    virtual bool SetMaxConcurrentAsyncTasksPerNode(
        int maxConcurrentAsyncTasksPerNode,
        std::string* error = nullptr) = 0;
    virtual bool SetBlockScriptValidatorsParams(
        int maxParallelBlocks,
        int perValidatorScriptThreadsCount,
        int perValidatorTxnThreadsCount,
        int perValidatorThreadMaxBatchSize,
        std::string* error = nullptr) = 0;
    virtual bool SetMaxOpsPerScriptPolicy(int64_t maxOpsPerScriptPolicyIn, std::string* error) = 0;
    /** Sets the maximum policy number of sigops we're willing to relay/mine in a single tx */
    virtual bool SetMaxTxSigOpsCountPolicy(int64_t maxTxSigOpsCountIn, std::string* err = nullptr) = 0;
    virtual bool SetMaxPubKeysPerMultiSigPolicy(int64_t maxPubKeysPerMultiSigIn, std::string* err = nullptr) = 0;
    virtual bool SetMaxStdTxnValidationDuration(int ms, std::string* err = nullptr) = 0;
    virtual bool SetMaxNonStdTxnValidationDuration(int ms, std::string* err = nullptr) = 0;
    virtual bool SetMaxTxnValidatorAsyncTasksRunDuration(int ms, std::string* err = nullptr) = 0;
    virtual bool SetMaxTxnChainValidationBudget(int ms, std::string* err = nullptr) = 0;
    virtual void SetValidationClockCPU(bool enable) = 0;
    virtual bool SetPTVTaskScheduleStrategy(std::string strategy, std::string* err = nullptr) = 0;
    virtual bool SetMaxStackMemoryUsage(int64_t maxStackMemoryUsageConsensusIn, int64_t maxStackMemoryUsagePolicyIn, std::string* err = nullptr) = 0;
    virtual bool SetMaxScriptSizePolicy(int64_t maxScriptSizePolicyIn, std::string* err = nullptr) = 0;
    virtual bool SetMaxScriptNumLengthPolicy(int64_t maxScriptNumLengthIn, std::string* err = nullptr) = 0;
    virtual bool SetGenesisGracefulPeriod(int64_t genesisGracefulPeriodIn, std::string* err = nullptr) = 0;
    virtual void SetAcceptNonStandardOutput(bool accept) = 0;
    virtual bool SetMaxCoinsViewCacheSize(int64_t max, std::string* err) = 0;
    virtual bool SetMaxCoinsProviderCacheSize(int64_t max, std::string* err) = 0;
    virtual bool SetMaxCoinsDbOpenFiles(int64_t max, std::string* err) = 0;
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

    // RPC parameters
    virtual bool SetWebhookClientNumThreads(int64_t num, std::string* err) = 0;

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

protected:
    // NOLINTNEXTLINE(cppcoreguidelines-explicit-virtual-functions)
    ~ConfigInit() = default;
};

class GlobalConfig : public ConfigInit {
public:
    GlobalConfig();

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
    uint64_t GetMaxTxSize(bool isGenesisEnabled, bool isConsensus) const  override;

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
    uint64_t GetMaxTxSigOpsCountPolicy(bool isGenesisEnabled) const override;

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
    
    bool SetPTVTaskScheduleStrategy(std::string strategy, std::string* err = nullptr) override;
    PTVTaskScheduleStrategy GetPTVTaskScheduleStrategy() const override;

    bool SetMaxStackMemoryUsage(int64_t maxStackMemoryUsageConsensusIn, int64_t maxStackMemoryUsagePolicyIn, std::string* err = nullptr) override;
    uint64_t GetMaxStackMemoryUsage(bool isGenesisEnabled, bool consensus) const override;

    
    bool SetMaxScriptSizePolicy(int64_t maxScriptSizePolicyIn, std::string* err = nullptr) override;
    uint64_t GetMaxScriptSize(bool isGenesisEnabled, bool isConsensus) const override;

    bool SetMaxScriptNumLengthPolicy(int64_t maxScriptNumLengthIn, std::string* err = nullptr) override;
    uint64_t GetMaxScriptNumLength(bool isGenesisEnabled, bool isConsensus) const override;

    bool SetGenesisGracefulPeriod(int64_t genesisGracefulPeriodIn, std::string* err = nullptr) override;
    uint64_t GetGenesisGracefulPeriod() const override;

    void SetAcceptNonStandardOutput(bool accept) override;
    bool GetAcceptNonStandardOutput(bool isGenesisEnabled) const override;

    bool SetMaxCoinsViewCacheSize(int64_t max, std::string* err) override;
    uint64_t GetMaxCoinsViewCacheSize() const override {return data->mMaxCoinsViewCacheSize;}

    bool SetMaxCoinsProviderCacheSize(int64_t max, std::string* err) override;
    uint64_t GetMaxCoinsProviderCacheSize() const override {return data->mMaxCoinsProviderCacheSize;}

    bool SetMaxCoinsDbOpenFiles(int64_t max, std::string* err) override;
    uint64_t GetMaxCoinsDbOpenFiles() const override {return data->mMaxCoinsDbOpenFiles; }

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

    // RPC parameters
    bool SetWebhookClientNumThreads(int64_t num, std::string* err) override;
    uint64_t GetWebhookClientNumThreads() const override;

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

    // Reset state of this object to match a newly constructed one. 
    // Used in constructor and for unit testing to always start with a clean state
    void Reset() override;

    // GetConfig() is used where read-only access to global configuration is needed.
    static Config& GetConfig();
    // GetModifiableGlobalConfig() should only be used in initialization and unit tests.
    static ConfigInit& GetModifiableGlobalConfig();

private:
    void  CheckSetDefaultCalled() const;

    struct GlobalConfigData {
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

        uint64_t maxTxSizePolicy;
        uint64_t minConsolidationFactor;
        uint64_t maxConsolidationInputScriptSize;
        uint64_t minConfConsolidationInput;
        bool acceptNonStdConsolidationInput;
        uint64_t dataCarrierSize;
        bool dataCarrier {DEFAULT_ACCEPT_DATACARRIER};
        uint64_t limitAncestorCount;
        uint64_t limitSecondaryMempoolAncestorCount;

        bool testBlockCandidateValidity;
        mining::CMiningFactory::BlockAssemblerType blockAssemblerType;

        int32_t genesisActivationHeight;

        int mMaxConcurrentAsyncTasksPerNode;

        int mMaxParallelBlocks;
        int mPerBlockTxnValidatorThreadsCount;
        int mPerBlockScriptValidatorThreadsCount;
        int mPerBlockScriptValidationMaxBatchSize;

        uint64_t maxOpsPerScriptPolicy;

        uint64_t maxTxSigOpsCountPolicy;
        uint64_t maxPubKeysPerMultiSig;
        uint64_t genesisGracefulPeriod;

        std::chrono::milliseconds mMaxStdTxnValidationDuration;
        std::chrono::milliseconds mMaxNonStdTxnValidationDuration;
        std::chrono::milliseconds mMaxTxnValidatorAsyncTasksRunDuration;
        std::chrono::milliseconds mMaxTxnChainValidationBudget;

        bool mValidationClockCPU;
    
        PTVTaskScheduleStrategy mPTVTaskScheduleStrategy;

        uint64_t maxStackMemoryUsagePolicy;
        uint64_t maxStackMemoryUsageConsensus;

        uint64_t maxScriptSizePolicy;

        uint64_t maxScriptNumLengthPolicy;

        bool mAcceptNonStandardOutput;

        uint64_t mMaxCoinsViewCacheSize;
        uint64_t mMaxCoinsProviderCacheSize;

        uint64_t mMaxCoinsDbOpenFiles;

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

        // RPC parameters
        uint64_t webhookClientNumThreads;

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

    #if ENABLE_ZMQ
        int64_t invalidTxZMQMaxMessageSize;
    #endif

        std::int32_t mSoftConsensusFreezeDuration;

        // Only for values that can change in runtime
        mutable std::shared_mutex configMtx{};
    };
    std::shared_ptr<GlobalConfigData> data = std::make_shared<GlobalConfigData>();

protected:
    GlobalConfig(std::shared_ptr<GlobalConfigData> data);

public:
    std::shared_ptr<GlobalConfigData> getGlobalConfigData() const;
};

// Dummy for subclassing in unittests
class DummyConfig : public ConfigInit {
public:
    DummyConfig();
    DummyConfig(std::string net);

    void SetDefaultBlockSizeParams(const DefaultBlockSizeParams &params) override {  }

    bool SetMaxBlockSize(uint64_t maxBlockSize, std::string* err = nullptr) override {
        SetErrorMsg(err);
        return false; 
    }
    uint64_t GetMaxBlockSize() const override { return 0; }

    bool SetMaxGeneratedBlockSize(uint64_t maxGeneratedBlockSize, std::string* err = nullptr) override {
        SetErrorMsg(err);
        return false; 
    }
    uint64_t GetMaxGeneratedBlockSize() const override { return 0; };
    uint64_t GetMaxGeneratedBlockSize(int64_t nMedianTimePast) const override { return 0; }
    bool MaxGeneratedBlockSizeOverridden() const override { return false; }

    bool SetBlockSizeActivationTime(int64_t activationTime, std::string* err = nullptr) override {
        SetErrorMsg(err);
        return false; 
    }
    int64_t GetBlockSizeActivationTime() const override { return 0; }

    bool SetMaxTxSizePolicy(int64_t value, std::string* err = nullptr) override
    {
        SetErrorMsg(err);
        maxTxSizePolicy = static_cast<uint64_t>(value);
        return false;
    }
    uint64_t GetMaxTxSize(bool isGenesisEnabled, bool isConsensus) const override { return maxTxSizePolicy; }

    bool SetMinConsolidationFactor(int64_t value, std::string* err = nullptr) override
    {
        SetErrorMsg(err);
        minConsolidationFactor = static_cast<uint64_t>(value);
        return false;
    }
    uint64_t GetMinConsolidationFactor() const override { return minConsolidationFactor; }

    bool SetMaxConsolidationInputScriptSize(int64_t value, std::string* err = nullptr) override
    {
        SetErrorMsg(err);
        maxConsolidationInputScriptSize = static_cast<uint64_t>(value);
        return false;
    }
    uint64_t GetMaxConsolidationInputScriptSize() const override { return maxConsolidationInputScriptSize; }

    bool SetMinConfConsolidationInput(int64_t value, std::string* err = nullptr) override
    {
        SetErrorMsg(err);
        minConfConsolidationInput = static_cast<uint64_t>(value);
        return false;
    }
    uint64_t GetMinConfConsolidationInput() const override { return minConfConsolidationInput; }

    bool SetAcceptNonStdConsolidationInput(bool flagValue, std::string* err = nullptr) override
    {
        SetErrorMsg(err);
        acceptNonStdConsolidationInput = flagValue;
        return false;
    }
    bool GetAcceptNonStdConsolidationInput() const override { return acceptNonStdConsolidationInput; }

    void SetChainParams(std::string net);
    const CChainParams &GetChainParams() const override { return *chainParams; }

    void SetMinFeePerKB(CFeeRate amt) override{};
    CFeeRate GetMinFeePerKB() const override { return CFeeRate(Amount(0)); }

    bool SetDustRelayFee(Amount amt, std::string* err = nullptr) override { return true; };
    CFeeRate GetDustRelayFee() const override { return CFeeRate(Amount(DUST_RELAY_TX_FEE)); };

    bool SetDustLimitFactor(int64_t factor, std::string* err = nullptr) override{return true;};
    int64_t GetDustLimitFactor() const override { return 0; }

    void SetPreferredBlockFileSize(uint64_t preferredBlockFileSize) override {}
    uint64_t GetPreferredBlockFileSize() const override { return 0; }

    uint64_t GetDataCarrierSize() const override { return dataCarrierSize; }
    void SetDataCarrierSize(uint64_t dataCarrierSizeIn) override { dataCarrierSize = dataCarrierSizeIn; }

    bool GetDataCarrier() const override { return dataCarrier; }
    void SetDataCarrier(bool dataCarrierIn) override { dataCarrier = dataCarrierIn; }

    bool SetLimitAncestorCount(int64_t limitAncestorCount, std::string* err = nullptr) override {return true;}
    uint64_t GetLimitAncestorCount() const override { return 0; }

    bool SetLimitSecondaryMempoolAncestorCount(int64_t limitSecondaryMempoolAncestorCountIn, std::string* err = nullptr) override {return true;}
    uint64_t GetLimitSecondaryMempoolAncestorCount() const override { return 0; }

    void SetTestBlockCandidateValidity(bool skip) override {}
    bool GetTestBlockCandidateValidity() const override { return false; }

    void SetFactorMaxSendQueuesBytes(uint64_t factorMaxSendQueuesBytes) override {}
    uint64_t GetFactorMaxSendQueuesBytes() const override { return 0;}
    uint64_t GetMaxSendQueuesBytes() const override { return 0; }

    void SetMiningCandidateBuilder(mining::CMiningFactory::BlockAssemblerType type) override {}
    mining::CMiningFactory::BlockAssemblerType GetMiningCandidateBuilder() const override {
        return mining::CMiningFactory::BlockAssemblerType::JOURNALING;
    }

    bool SetGenesisActivationHeight(int32_t genesisActivationHeightIn, std::string* err = nullptr) override { genesisActivationHeight = genesisActivationHeightIn; return true; }
    int32_t GetGenesisActivationHeight() const override { return genesisActivationHeight; }

    bool SetMaxConcurrentAsyncTasksPerNode(
        int maxConcurrentAsyncTasksPerNode,
        std::string* error = nullptr) override
    {
        SetErrorMsg(error);

        return false;
    }
    int GetMaxConcurrentAsyncTasksPerNode() const override;

    bool SetBlockScriptValidatorsParams(
        int maxParallelBlocks,
        int perValidatorScriptThreadsCount,
        int perValidatorTxnThreadsCount,
        int perValidatorThreadMaxBatchSize,
        std::string* error = nullptr) override
    {
        SetErrorMsg(error);

        return false;
    }
    int GetMaxParallelBlocks() const override;
    int GetPerBlockTxnValidatorThreadsCount() const override;
    int GetPerBlockScriptValidatorThreadsCount() const override;
    int GetPerBlockScriptValidationMaxBatchSize() const override;
    bool SetMaxStackMemoryUsage(int64_t maxStackMemoryUsageConsensusIn, int64_t maxStackMemoryUsagePolicyIn, std::string* err = nullptr)  override { return true; }
    uint64_t GetMaxStackMemoryUsage(bool isGenesisEnabled, bool consensus) const override { return UINT32_MAX; }

    bool SetMaxOpsPerScriptPolicy(int64_t maxOpsPerScriptPolicyIn, std::string* error) override { return true;  }
    uint64_t GetMaxOpsPerScript(bool isGenesisEnabled, bool consensus) const override
    {
        if (isGenesisEnabled)
        {
            return MAX_OPS_PER_SCRIPT_AFTER_GENESIS;
        }
        else
        {
            return MAX_OPS_PER_SCRIPT_BEFORE_GENESIS;
        }
    }
    bool SetMaxTxSigOpsCountPolicy(int64_t maxTxSigOpsCountIn, std::string* err = nullptr) override { return true; }
    uint64_t GetMaxTxSigOpsCountConsensusBeforeGenesis() const override { return MAX_TX_SIGOPS_COUNT_POLICY_BEFORE_GENESIS; }
    uint64_t GetMaxTxSigOpsCountPolicy(bool isGenesisEnabled) const override { return MAX_TX_SIGOPS_COUNT_POLICY_BEFORE_GENESIS; }

    uint64_t GetMaxBlockSigOpsConsensusBeforeGenesis(uint64_t blockSize) const override { throw std::runtime_error("DummyCofig::GetMaxBlockSigOps not implemented"); }

    bool SetGenesisGracefulPeriod(int64_t genesisGracefulPeriodIn, std::string* err = nullptr) override { return true; }
    uint64_t GetGenesisGracefulPeriod() const override { return DEFAULT_GENESIS_GRACEFULL_ACTIVATION_PERIOD; }

    bool SetMaxPubKeysPerMultiSigPolicy(int64_t maxPubKeysPerMultiSigIn, std::string* err = nullptr) override { return true; }
    uint64_t GetMaxPubKeysPerMultiSig(bool isGenesisEnabled, bool consensus) const override
    {
        if (isGenesisEnabled)
        {
            return MAX_PUBKEYS_PER_MULTISIG_AFTER_GENESIS;
        }
        else
        {
            return MAX_PUBKEYS_PER_MULTISIG_BEFORE_GENESIS;
        }
    }

    bool SetMaxStdTxnValidationDuration(int ms, std::string* err = nullptr) override
    {
        SetErrorMsg(err);

        return false;
    }
    std::chrono::milliseconds GetMaxStdTxnValidationDuration() const override
    {
        return DEFAULT_MAX_STD_TXN_VALIDATION_DURATION;
    }

    bool SetMaxNonStdTxnValidationDuration(int ms, std::string* err = nullptr) override
    {
        SetErrorMsg(err);

        return false;
    }
    std::chrono::milliseconds GetMaxNonStdTxnValidationDuration() const override
    {
        return DEFAULT_MAX_NON_STD_TXN_VALIDATION_DURATION;
    }

    bool SetMaxTxnValidatorAsyncTasksRunDuration(int ms, std::string* err = nullptr) override
    {
        SetErrorMsg(err);

        return false;
    }

    std::chrono::milliseconds GetMaxTxnValidatorAsyncTasksRunDuration() const override
    {
        return CTxnValidator::DEFAULT_MAX_ASYNC_TASKS_RUN_DURATION;
    }

    bool SetMaxTxnChainValidationBudget(int ms, std::string* err = nullptr) override
    {
        SetErrorMsg(err);

        return false;
    }
    std::chrono::milliseconds GetMaxTxnChainValidationBudget() const override
    {
        return DEFAULT_MAX_TXN_CHAIN_VALIDATION_BUDGET;
    }

    void SetValidationClockCPU(bool enable) override {}
    bool GetValidationClockCPU() const override { return DEFAULT_VALIDATION_CLOCK_CPU; }

    bool SetPTVTaskScheduleStrategy(std::string strategy, std::string* err = nullptr) override { return true; }
    PTVTaskScheduleStrategy GetPTVTaskScheduleStrategy() const override { return DEFAULT_PTV_TASK_SCHEDULE_STRATEGY; }

    bool SetMaxScriptSizePolicy(int64_t maxScriptSizePolicyIn, std::string* err = nullptr) override 
    {
        SetErrorMsg(err);
        maxScriptSizePolicy = static_cast<uint64_t>(maxScriptSizePolicyIn);
        return true; 
    };
    uint64_t GetMaxScriptSize(bool isGenesisEnabled, bool isConsensus) const override { return maxScriptSizePolicy; };

    bool SetMaxScriptNumLengthPolicy(int64_t maxScriptNumLengthIn, std::string* err = nullptr) override { return true;  }
    uint64_t GetMaxScriptNumLength(bool isGenesisEnabled, bool isConsensus) const override
    {
        if (isGenesisEnabled)
        {
            return MAX_SCRIPT_NUM_LENGTH_AFTER_GENESIS;
        }
        else
        {
            return MAX_SCRIPT_NUM_LENGTH_BEFORE_GENESIS;
        }
    }

    void SetAcceptNonStandardOutput(bool) override {}
    bool GetAcceptNonStandardOutput(bool isGenesisEnabled) const override
    {
        return isGenesisEnabled ? true : !fRequireStandard;
    }

    bool SetMaxCoinsViewCacheSize(int64_t max, std::string* err) override
    {
        SetErrorMsg(err);

        return false;
    }
    uint64_t GetMaxCoinsViewCacheSize() const override {return 0; /* unlimited */}

    bool SetMaxCoinsProviderCacheSize(int64_t max, std::string* err) override
    {
        SetErrorMsg(err);

        return false;
    }
    uint64_t GetMaxCoinsProviderCacheSize() const override {return 0; /* unlimited */}

    bool SetMaxCoinsDbOpenFiles(int64_t max, std::string* err)  override
    {
        SetErrorMsg(err);

        return false;
    }
    uint64_t GetMaxCoinsDbOpenFiles() const override {return 64; /* old default */}

    bool SetMaxMempool(int64_t maxMempool, std::string* err) override
    {
        SetErrorMsg(err);

        return true;
    }
    uint64_t GetMaxMempool() const override { return DEFAULT_MAX_MEMPOOL_SIZE * ONE_MEGABYTE; }

    bool SetMaxMempoolSizeDisk(int64_t maxMempoolSizeDisk, std::string* err) override
    {
        SetErrorMsg(err);

        return true;
    }
    uint64_t GetMaxMempoolSizeDisk() const override {
        // NOLINTNEXTLINE(bugprone-implicit-widening-of-multiplication-result)
        return DEFAULT_MAX_MEMPOOL_SIZE * DEFAULT_MAX_MEMPOOL_SIZE_DISK_FACTOR * ONE_MEGABYTE;
    }

    bool SetMempoolMaxPercentCPFP(int64_t mempoolMaxPercentCPFP, std::string* err) override
    {
        SetErrorMsg(err);

        return true;
    }
    uint64_t GetMempoolMaxPercentCPFP() const override { return DEFAULT_MEMPOOL_MAX_PERCENT_CPFP; }

    bool SetMemPoolExpiry(int64_t memPoolExpiry, std::string* err) override
    {
        SetErrorMsg(err);

        return true;
    }
    // NOLINTNEXTLINE(bugprone-implicit-widening-of-multiplication-result)
    uint64_t GetMemPoolExpiry() const override { return DEFAULT_MEMPOOL_EXPIRY * SECONDS_IN_ONE_HOUR; }

    bool SetMaxOrphanTxSize(int64_t maxOrphanTxSize, std::string* err) override
    {
        SetErrorMsg(err);

        return true;
    }
    uint64_t GetMaxOrphanTxSize() const override { return COrphanTxns::DEFAULT_MAX_ORPHAN_TRANSACTIONS_SIZE; }

    bool SetMaxOrphansInBatchPercentage(uint64_t percent, std::string* err) override
    {
        SetErrorMsg(err);

        return true;
    }

    uint64_t GetMaxOrphansInBatchPercentage() const override { return COrphanTxns::DEFAULT_MAX_PERCENTAGE_OF_ORPHANS_IN_BATCH; };
    
    bool SetMaxInputsForSecondLayerOrphan(uint64_t maxInputs, std::string* err) override
    {
        SetErrorMsg(err);

        return true;
    }
    uint64_t GetMaxInputsForSecondLayerOrphan() const override { return COrphanTxns::DEFAULT_MAX_INPUTS_OUTPUTS_PER_TRANSACTION; };

    bool SetStopAtHeight(int32_t stopAtHeight, std::string* err) override
    {
        SetErrorMsg(err);

        return true;
    }
    int32_t GetStopAtHeight() const override { return DEFAULT_STOPATHEIGHT; }

    bool SetPromiscuousMempoolFlags(int64_t promiscuousMempoolFlags, std::string* err) override
    {
        SetErrorMsg(err);

        return true;
    }
    uint64_t GetPromiscuousMempoolFlags() const override { return 0; }
    bool IsSetPromiscuousMempoolFlags() const override { return false; }

    void SetInvalidBlocks(const std::set<uint256>& hashes) override 
    { 
        mInvalidBlocks = hashes; 
    };

    const std::set<uint256>& GetInvalidBlocks() const override 
    { 
        return mInvalidBlocks; 
    };

    bool IsBlockInvalidated(const uint256& hash) const override 
    {
        return mInvalidBlocks.find(hash) != mInvalidBlocks.end(); 
    };

    void SetBanClientUA(std::set<std::string> uaClients) override
    {
        mBannedUAClients = std::move(uaClients);
    }
    
    void SetAllowClientUA(std::set<std::string> uaClients) override
    {
        mAllowedUAClients = std::move(uaClients);
    }
    
    bool IsClientUABanned(const std::string uaClient) const override
    {
        return mBannedUAClients.find(uaClient) != mBannedUAClients.end();
    }

    bool SetMaxProtocolRecvPayloadLength(uint64_t value, std::string* err) override { return true; }
    bool SetRecvInvQueueFactor(uint64_t value, std::string* err) override { return true; }
    unsigned int GetMaxProtocolRecvPayloadLength() const override { return DEFAULT_MAX_PROTOCOL_RECV_PAYLOAD_LENGTH; }
    unsigned int GetMaxProtocolSendPayloadLength() const override { return DEFAULT_MAX_PROTOCOL_RECV_PAYLOAD_LENGTH*MAX_PROTOCOL_SEND_PAYLOAD_FACTOR; }
    unsigned int GetRecvInvQueueFactor() const override { return DEFAULT_RECV_INV_QUEUE_FACTOR; }

    bool AddInvalidTxSink(const std::string& sink, std::string* err = nullptr) override { return true; };
    std::set<std::string> GetInvalidTxSinks() const override { return {"NONE"}; };
    std::set<std::string> GetAvailableInvalidTxSinks() const override { return {"NONE"}; };

    bool SetInvalidTxFileSinkMaxDiskUsage(int64_t max, std::string* err = nullptr) override { return true; };
    int64_t GetInvalidTxFileSinkMaxDiskUsage() const override { return 300* ONE_MEGABYTE; };

    bool SetInvalidTxFileSinkEvictionPolicy(std::string policy, std::string* err = nullptr) override { return true; };
    InvalidTxEvictionPolicy GetInvalidTxFileSinkEvictionPolicy() const override { return InvalidTxEvictionPolicy::IGNORE_NEW; };

    void SetEnableAssumeWhitelistedBlockDepth(bool enabled) override {}
    bool GetEnableAssumeWhitelistedBlockDepth() const override { return DEFAULT_ENABLE_ASSUME_WHITELISTED_BLOCK_DEPTH; }
    bool SetAssumeWhitelistedBlockDepth(int64_t depth, std::string* err = nullptr) override { return true; }
    int32_t GetAssumeWhitelistedBlockDepth() const override { return DEFAULT_ASSUME_WHITELISTED_BLOCK_DEPTH; }

    bool SetMinBlocksToKeep(int32_t minblocks, std::string* err = nullptr) override { return true; }
    int32_t GetMinBlocksToKeep() const override { return DEFAULT_MIN_BLOCKS_TO_KEEP; }
    bool SetBlockValidationTxBatchSize(int64_t size, std::string* err = nullptr) override { return true; }
    uint64_t GetBlockValidationTxBatchSize() const override { return DEFAULT_BLOCK_VALIDATION_TX_BATCH_SIZE; }

    // Block download
    bool SetBlockStallingMinDownloadSpeed(int64_t min, std::string* err = nullptr) override { return true; }
    uint64_t GetBlockStallingMinDownloadSpeed() const override { return DEFAULT_MIN_BLOCK_STALLING_RATE; }
    bool SetBlockStallingTimeout(int64_t timeout, std::string* err = nullptr) override { return true; }
    int64_t GetBlockStallingTimeout() const override { return DEFAULT_BLOCK_STALLING_TIMEOUT; }
    bool SetBlockDownloadWindow(int64_t window, std::string* err = nullptr) override { return true; }
    int64_t GetBlockDownloadWindow() const override { return DEFAULT_BLOCK_DOWNLOAD_WINDOW; }
    bool SetBlockDownloadLowerWindow(int64_t window, std::string* err = nullptr) override { return true; }
    int64_t GetBlockDownloadLowerWindow() const override { return DEFAULT_BLOCK_DOWNLOAD_LOWER_WINDOW; }
    bool SetBlockDownloadSlowFetchTimeout(int64_t timeout, std::string* err = nullptr) override { return true; }
    int64_t GetBlockDownloadSlowFetchTimeout() const override { return DEFAULT_BLOCK_DOWNLOAD_SLOW_FETCH_TIMEOUT; }
    bool SetBlockDownloadMaxParallelFetch(int64_t max, std::string* err = nullptr) override { return true; }
    uint64_t GetBlockDownloadMaxParallelFetch() const override { return DEFAULT_MAX_BLOCK_PARALLEL_FETCH; }
    bool SetBlockDownloadTimeoutBase(int64_t max, std::string* err = nullptr) override { return true; }
    int64_t GetBlockDownloadTimeoutBase() const override { return DEFAULT_BLOCK_DOWNLOAD_TIMEOUT_BASE; }
    bool SetBlockDownloadTimeoutBaseIBD(int64_t max, std::string* err = nullptr) override { return true; }
    int64_t GetBlockDownloadTimeoutBaseIBD() const override { return DEFAULT_BLOCK_DOWNLOAD_TIMEOUT_BASE_IBD; }
    bool SetBlockDownloadTimeoutPerPeer(int64_t max, std::string* err = nullptr) override { return true; }
    int64_t GetBlockDownloadTimeoutPerPeer() const override { return DEFAULT_BLOCK_DOWNLOAD_TIMEOUT_PER_PEER; }

    // P2P parameters
    bool SetP2PHandshakeTimeout(int64_t timeout, std::string* err = nullptr) override { return true; }
    int64_t GetP2PHandshakeTimeout() const override { return DEFAULT_P2P_HANDSHAKE_TIMEOUT_INTERVAL; }
    bool SetStreamSendRateLimit(int64_t limit, std::string* err = nullptr) override { return true; }
    int64_t GetStreamSendRateLimit() const override { return Stream::DEFAULT_SEND_RATE_LIMIT; }
    bool SetBanScoreThreshold(int64_t threshold, std::string* err = nullptr) override { return true; }
    unsigned int GetBanScoreThreshold() const override { return DEFAULT_BANSCORE_THRESHOLD; }
    bool SetBlockTxnMaxPercent(unsigned int percent, std::string* err = nullptr) override { return true; }
    unsigned int GetBlockTxnMaxPercent() const override { return DEFAULT_BLOCK_TXN_MAX_PERCENT; }
    bool SetMultistreamsEnabled(bool enabled, std::string* err = nullptr) override { return true; }
    bool GetMultistreamsEnabled() const override { return DEFAULT_STREAMS_ENABLED; }
    bool SetWhitelistRelay(bool relay, std::string* err = nullptr) override { return true; }
    bool GetWhitelistRelay() const override { return DEFAULT_WHITELISTRELAY; }
    bool SetWhitelistForceRelay(bool relay, std::string* err = nullptr) override { return true; }
    bool GetWhitelistForceRelay() const override { return DEFAULT_WHITELISTFORCERELAY; }
    bool SetRejectMempoolRequest(bool reject, std::string* err = nullptr) override { return true; }
    bool GetRejectMempoolRequest() const override { return DEFAULT_REJECTMEMPOOLREQUEST; }
    bool SetDropMessageTest(int64_t val, std::string* err = nullptr) override { return true; }
    bool DoDropMessageTest() const override { return false; }
    uint64_t GetDropMessageTest() const override { return 0; }
    bool SetInvalidChecksumInterval(int64_t val, std::string* err = nullptr) override { return true; }
    unsigned int GetInvalidChecksumInterval() const override { return DEFAULT_MIN_TIME_INTERVAL_CHECKSUM_MS; }
    bool SetInvalidChecksumFreq(int64_t val, std::string* err = nullptr) override { return true; }
    unsigned int GetInvalidChecksumFreq() const override { return DEFAULT_INVALID_CHECKSUM_FREQUENCY; }
    bool SetFeeFilter(bool feefilter, std::string* err = nullptr) override { return true; }
    bool GetFeeFilter() const override { return DEFAULT_FEEFILTER; }
    bool SetMaxAddNodeConnections(int16_t max, std::string* err = nullptr) override { return true; }
    uint16_t GetMaxAddNodeConnections() const override { return DEFAULT_MAX_ADDNODE_CONNECTIONS; }

    // RPC parameters
    bool SetWebhookClientNumThreads(int64_t num, std::string* err) override { return true; }
    uint64_t GetWebhookClientNumThreads() const override { return rpc::client::WebhookClientDefaults::DEFAULT_NUM_THREADS; }

    // Double-Spend processing parameters
    bool SetDoubleSpendNotificationLevel(int level, std::string* err) override { return true; }
    DSAttemptHandler::NotificationLevel GetDoubleSpendNotificationLevel() const override { return DSAttemptHandler::DEFAULT_NOTIFY_LEVEL; }
    bool SetDoubleSpendEndpointFastTimeout(int timeout, std::string* err) override { return true; }
    int GetDoubleSpendEndpointFastTimeout() const override { return rpc::client::RPCClientConfig::DEFAULT_DS_ENDPOINT_FAST_TIMEOUT; }
    bool SetDoubleSpendEndpointSlowTimeout(int timeout, std::string* err) override { return true; }
    int GetDoubleSpendEndpointSlowTimeout() const override { return rpc::client::RPCClientConfig::DEFAULT_DS_ENDPOINT_SLOW_TIMEOUT; }
    bool SetDoubleSpendEndpointSlowRatePerHour(int64_t rate, std::string* err) override { return true; }
    uint64_t GetDoubleSpendEndpointSlowRatePerHour() const override { return DSAttemptHandler::DEFAULT_DS_ENDPOINT_SLOW_RATE_PER_HOUR; }
    bool SetDoubleSpendEndpointPort(int port, std::string* err) override { return true; }
    int GetDoubleSpendEndpointPort() const override { return rpc::client::RPCClientConfig::DEFAULT_DS_ENDPOINT_PORT; }
    bool SetDoubleSpendTxnRemember(int64_t size, std::string* err) override { return true; }
    uint64_t GetDoubleSpendTxnRemember() const override { return DSAttemptHandler::DEFAULT_TXN_REMEMBER_COUNT; }
    bool SetDoubleSpendEndpointBlacklistSize(int64_t size, std::string* err) override { return true; }
    uint64_t GetDoubleSpendEndpointBlacklistSize() const override { return DSAttemptHandler::DEFAULT_DS_ENDPOINT_BLACKLIST_SIZE; }
    bool SetDoubleSpendEndpointSkipList(const std::string& skip, std::string* err) override { return true; }
    std::set<std::string> GetDoubleSpendEndpointSkipList() const override { return {}; }
    bool SetDoubleSpendEndpointMaxCount(int64_t max, std::string* err) override { return true; }
    uint64_t GetDoubleSpendEndpointMaxCount() const override { return DSAttemptHandler::DEFAULT_DS_ENDPOINT_MAX_COUNT; }
    bool SetDoubleSpendNumFastThreads(int64_t num, std::string* err) override { return true; }
    uint64_t GetDoubleSpendNumFastThreads() const override { return DSAttemptHandler::DEFAULT_NUM_FAST_THREADS; }
    bool SetDoubleSpendNumSlowThreads(int64_t num, std::string* err) override { return true; }
    uint64_t GetDoubleSpendNumSlowThreads() const override { return DSAttemptHandler::DEFAULT_NUM_SLOW_THREADS; }
    bool SetDoubleSpendQueueMaxMemory(int64_t max, std::string* err) override { return true; }
    uint64_t GetDoubleSpendQueueMaxMemory() const override { return DSAttemptHandler::DEFAULT_MAX_SUBMIT_MEMORY * ONE_MEGABYTE; }
    bool SetDoubleSpendDetectedWebhookURL(const std::string& url, std::string* err) override { return true; }
    std::string GetDoubleSpendDetectedWebhookAddress() const override { return ""; }
    int16_t GetDoubleSpendDetectedWebhookPort() const override { return rpc::client::WebhookClientDefaults::DEFAULT_WEBHOOK_PORT; }
    std::string GetDoubleSpendDetectedWebhookPath() const override { return ""; }
    bool SetDoubleSpendDetectedWebhookMaxTxnSize(int64_t max, std::string* err) override { return true; }
    uint64_t GetDoubleSpendDetectedWebhookMaxTxnSize() const override { return DSDetectedDefaults::DEFAULT_MAX_WEBHOOK_TXN_SIZE * ONE_MEBIBYTE; }

    // MinerID
    bool SetMinerIdEnabled(bool enabled, std::string* err) override { return true; }
    bool GetMinerIdEnabled() const override { return MinerIdDatabaseDefaults::DEFAULT_MINER_ID_ENABLED; }
    bool SetMinerIdCacheSize(int64_t size, std::string* err) override { return true; }
    uint64_t GetMinerIdCacheSize() const override { return MinerIdDatabaseDefaults::DEFAULT_CACHE_SIZE; }
    bool SetMinerIdsNumToKeep(int64_t num, std::string* err) override { return true; }
    uint64_t GetMinerIdsNumToKeep() const override { return MinerIdDatabaseDefaults::DEFAULT_MINER_IDS_TO_KEEP; }
    bool SetMinerIdReputationM(int64_t num, std::string* err) override { return true; }
    uint32_t GetMinerIdReputationM() const override { return MinerIdDatabaseDefaults::DEFAULT_MINER_REPUTATION_M; }
    bool SetMinerIdReputationN(int64_t num, std::string* err) override { return true; }
    uint32_t GetMinerIdReputationN() const override { return MinerIdDatabaseDefaults::DEFAULT_MINER_REPUTATION_N; }
    bool SetMinerIdReputationMScale(double num, std::string* err) override { return true; }
    double GetMinerIdReputationMScale() const override { return MinerIdDatabaseDefaults::DEFAULT_M_SCALE_FACTOR; }
    std::string GetMinerIdGeneratorAddress() const override { return ""; }
    int16_t GetMinerIdGeneratorPort() const override { return rpc::client::WebhookClientDefaults::DEFAULT_WEBHOOK_PORT; }
    std::string GetMinerIdGeneratorPath() const override { return ""; }
    std::string GetMinerIdGeneratorAlias() const override { return ""; }
    bool SetMinerIdGeneratorURL(const std::string& url, std::string* err) override { return true; }
    bool SetMinerIdGeneratorAlias(const std::string& alias, std::string* err) override { return true; }

#if ENABLE_ZMQ
    bool SetInvalidTxZMQMaxMessageSize(int64_t max, std::string* err = nullptr) override { return true; };
    int64_t GetInvalidTxZMQMaxMessageSize() const override { return 10 * ONE_MEGABYTE; };
#endif

    bool SetMaxMerkleTreeDiskSpace(int64_t maxDiskSpace, std::string* err = nullptr) override
    {
        return true;
    }

    uint64_t GetMaxMerkleTreeDiskSpace() const override
    {
        return 0;
    }

    bool SetPreferredMerkleTreeFileSize(int64_t preferredFileSize, std::string* err = nullptr) override
    {
        return true;
    }

    uint64_t GetPreferredMerkleTreeFileSize() const override
    {
        return 0;
    }

    bool SetMaxMerkleTreeMemoryCacheSize(int64_t maxMemoryCacheSize, std::string* err = nullptr) override
    {
        return true;
    }

    uint64_t GetMaxMerkleTreeMemoryCacheSize() const override
    {
        return 0;
    }

    bool SetDisableBIP30Checks(bool disable, std::string* err = nullptr) override
    {
        return true;
    }

    bool GetDisableBIP30Checks() const override
    {
        return true;
    }

    bool SetSoftConsensusFreezeDuration( std::int64_t duration, std::string* err ) override
    {
        return true;
    }

    std::int32_t GetSoftConsensusFreezeDuration() const override
    {
        return std::numeric_limits<std::int32_t>::max();
    }

    // Safe mode params
    bool SetSafeModeWebhookURL(const std::string& url, std::string* err = nullptr) override { return true; }
    std::string GetSafeModeWebhookAddress() const override { return ""; }
    int16_t GetSafeModeWebhookPort() const override { return rpc::client::WebhookClientDefaults::DEFAULT_WEBHOOK_PORT; }
    std::string GetSafeModeWebhookPath() const override { return ""; }
    int64_t GetSafeModeMaxForkDistance() const override { return 100;};
    bool SetSafeModeMaxForkDistance(int64_t distance, std::string* err)  override {return true;};
    int64_t GetSafeModeMinForkLength() const  override { return 3;};
    bool SetSafeModeMinForkLength(int64_t length, std::string* err) override {return true;};
    int64_t GetSafeModeMinForkHeightDifference() const  override { return 5;};
    bool SetSafeModeMinForkHeightDifference(int64_t heightDifference, std::string* err) override {return true;};

    bool GetDetectSelfishMining() const override
    {
        return true;    
    };

    void SetDetectSelfishMining(bool detectSelfishMining) override {}

    int64_t GetMinBlockMempoolTimeDifferenceSelfish() const override
    {
        return DEFAULT_MIN_BLOCK_MEMPOOL_TIME_DIFFERENCE_SELFISH;
    };

    bool SetMinBlockMempoolTimeDifferenceSelfish(int64_t minBlockMempoolTimeDiffIn, std::string* err) override
    {
        return true;
    }

    uint64_t GetSelfishTxThreshold() const override
    {
        return DEFAULT_SELFISH_TX_THRESHOLD_IN_PERCENT;
    }

    bool SetSelfishTxThreshold(uint64_t selfishTxPercentThreshold, std::string* err) override
    {
        return true;
    }

    void Reset() override;

private:
    std::unique_ptr<CChainParams> chainParams;
    uint64_t dataCarrierSize { DEFAULT_DATA_CARRIER_SIZE };
    bool dataCarrier {DEFAULT_ACCEPT_DATACARRIER};
    int32_t genesisActivationHeight;
    uint64_t maxTxSizePolicy{ DEFAULT_MAX_TX_SIZE_POLICY_AFTER_GENESIS };
    uint64_t minConsolidationFactor{ DEFAULT_MIN_CONSOLIDATION_FACTOR };
    uint64_t maxConsolidationInputScriptSize{DEFAULT_MAX_CONSOLIDATION_INPUT_SCRIPT_SIZE };
    uint64_t minConfConsolidationInput { DEFAULT_MIN_CONF_CONSOLIDATION_INPUT };
    uint64_t acceptNonStdConsolidationInput { DEFAULT_ACCEPT_NON_STD_CONSOLIDATION_INPUT };
    uint64_t maxScriptSizePolicy { DEFAULT_MAX_SCRIPT_SIZE_POLICY_AFTER_GENESIS };
    std::set<uint256> mInvalidBlocks;
    std::set<std::string> mBannedUAClients{DEFAULT_CLIENTUA_BAN_PATTERNS};
    std::set<std::string> mAllowedUAClients;

    void SetErrorMsg(std::string* err)
    {
        if (err)
        {
            *err = "This is dummy config"; 
        } 
    }
};

#endif
