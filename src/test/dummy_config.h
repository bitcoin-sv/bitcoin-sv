// Copyright (c) 2025 BSV Blockchain Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.
#pragma once

#include "config.h"
#include "double_spend/dsdetected_defaults.h"
#include "miner_id/miner_id_db_defaults.h"
#include "rpc/webhook_client_defaults.h"
#include "txdb_defaults.h"
#include "txn_validator.h"

class DummyConfig : public ConfigInit
{
public:
    DummyConfig();
    DummyConfig(const std::string& net);

    void SetDefaultBlockSizeParams(const DefaultBlockSizeParams&) override {}

    bool SetMaxBlockSize(uint64_t /*maxBlockSize*/, std::string* err = nullptr) override
    {
        SetErrorMsg(err);
        return false;
    }
    uint64_t GetMaxBlockSize() const override { return 0; }

    bool SetMaxGeneratedBlockSize(uint64_t /*maxGeneratedBlockSize*/,
                                  std::string* err = nullptr) override
    {
        SetErrorMsg(err);
        return false;
    }
    uint64_t GetMaxGeneratedBlockSize() const override { return 0; };
    uint64_t GetMaxGeneratedBlockSize(int64_t /*nMedianTimePast*/) const override
    {
        return 0;
    }
    bool MaxGeneratedBlockSizeOverridden() const override { return false; }

    bool SetBlockSizeActivationTime(int64_t /*activationTime*/,
                                    std::string* err = nullptr) override
    {
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
    uint64_t GetMaxTxSize(ProtocolEra, bool /*isConsensus*/) const override
    {
        return maxTxSizePolicy;
    }

    bool SetMinConsolidationFactor(int64_t value, std::string* err = nullptr) override
    {
        SetErrorMsg(err);
        minConsolidationFactor = static_cast<uint64_t>(value);
        return false;
    }
    uint64_t GetMinConsolidationFactor() const override { return minConsolidationFactor; }

    bool SetMaxConsolidationInputScriptSize(int64_t value,
                                            std::string* err = nullptr) override
    {
        SetErrorMsg(err);
        maxConsolidationInputScriptSize = static_cast<uint64_t>(value);
        return false;
    }
    uint64_t GetMaxConsolidationInputScriptSize() const override
    {
        return maxConsolidationInputScriptSize;
    }

    bool SetMinConfConsolidationInput(int64_t value, std::string* err = nullptr) override
    {
        SetErrorMsg(err);
        minConfConsolidationInput = static_cast<uint64_t>(value);
        return false;
    }
    uint64_t GetMinConfConsolidationInput() const override
    {
        return minConfConsolidationInput;
    }

    bool SetAcceptNonStdConsolidationInput(bool flagValue,
                                           std::string* err = nullptr) override
    {
        SetErrorMsg(err);
        acceptNonStdConsolidationInput = flagValue;
        return false;
    }
    bool GetAcceptNonStdConsolidationInput() const override
    {
        return acceptNonStdConsolidationInput;
    }

    void SetChainParams(const std::string& net);
    const CChainParams& GetChainParams() const override { return *chainParams; }

    void SetMinFeePerKB(CFeeRate) override {};
    CFeeRate GetMinFeePerKB() const override { return CFeeRate(Amount(0)); }

    bool SetDustRelayFee(Amount, std::string* /*err*/ = nullptr) override
    {
        return true;
    };
    CFeeRate GetDustRelayFee() const override
    {
        return CFeeRate(Amount(DUST_RELAY_TX_FEE));
    };

    bool SetDustLimitFactor(int64_t /*factor*/, std::string* /*err*/ = nullptr) override
    {
        return true;
    };
    int64_t GetDustLimitFactor() const override { return 0; }

    void SetPreferredBlockFileSize(uint64_t /*preferredBlockFileSize*/) override {}
    uint64_t GetPreferredBlockFileSize() const override { return 0; }

    uint64_t GetDataCarrierSize() const override { return dataCarrierSize; }
    void SetDataCarrierSize(uint64_t dataCarrierSizeIn) override
    {
        dataCarrierSize = dataCarrierSizeIn;
        dummyPolicySettings.SetDataCarrierSize(dataCarrierSizeIn);
    }

    bool GetDataCarrier() const override { return dataCarrier; }
    void SetDataCarrier(bool dataCarrierIn) override
    {
        dataCarrier = dataCarrierIn;
        dummyPolicySettings.SetDataCarrier(dataCarrierIn);
    }

    bool SetLimitAncestorCount(int64_t /*limitAncestorCount*/,
                               std::string* /*err*/ = nullptr) override
    {
        return true;
    }
    uint64_t GetLimitAncestorCount() const override { return 0; }

    bool SetLimitSecondaryMempoolAncestorCount(
        int64_t /*limitSecondaryMempoolAncestorCountIn*/,
        std::string* /*err*/ = nullptr) override
    {
        return true;
    }
    uint64_t GetLimitSecondaryMempoolAncestorCount() const override { return 0; }

    void SetTestBlockCandidateValidity(bool /*skip*/) override {}
    bool GetTestBlockCandidateValidity() const override { return false; }

    void SetFactorMaxSendQueuesBytes(uint64_t /*factorMaxSendQueuesBytes*/) override {}
    uint64_t GetFactorMaxSendQueuesBytes() const override { return 0; }
    uint64_t GetMaxSendQueuesBytes() const override { return 0; }

    void SetMiningCandidateBuilder(
        mining::CMiningFactory::BlockAssemblerType /*type*/) override
    {
    }
    mining::CMiningFactory::BlockAssemblerType GetMiningCandidateBuilder() const override
    {
        return mining::CMiningFactory::BlockAssemblerType::JOURNALING;
    }

    bool SetGenesisActivationHeight(int32_t genesisActivationHeightIn,
                                    std::string* err = nullptr) override
    {
        return dummyPolicySettings.SetGenesisActivationHeight(genesisActivationHeightIn, err);
    }
    int32_t GetGenesisActivationHeight() const override
    {
        return dummyPolicySettings.GetGenesisActivationHeight();
    }
    bool SetChronicleActivationHeight(int32_t chronicleActivationHeightIn,
                                      std::string* err = nullptr) override
    {
        return dummyPolicySettings.SetChronicleActivationHeight(chronicleActivationHeightIn, err);
    }
    int32_t GetChronicleActivationHeight() const override
    {
        return dummyPolicySettings.GetChronicleActivationHeight();
    }

    bool SetMaxConcurrentAsyncTasksPerNode(int /*maxConcurrentAsyncTasksPerNode*/,
                                           std::string* error = nullptr) override
    {
        SetErrorMsg(error);

        return false;
    }
    int GetMaxConcurrentAsyncTasksPerNode() const override;

    bool SetBlockScriptValidatorsParams(int /*maxParallelBlocks*/,
                                        int /*perValidatorScriptThreadsCount*/,
                                        int /*perValidatorTxnThreadsCount*/,
                                        int /*perValidatorThreadMaxBatchSize*/,
                                        std::string* error = nullptr) override
    {
        SetErrorMsg(error);

        return false;
    }
    int GetMaxParallelBlocks() const override;
    int GetPerBlockTxnValidatorThreadsCount() const override;
    int GetPerBlockScriptValidatorThreadsCount() const override;
    int GetPerBlockScriptValidationMaxBatchSize() const override;
    bool SetMaxStackMemoryUsage(int64_t /*maxStackMemoryUsageConsensusIn*/,
                                int64_t /*maxStackMemoryUsagePolicyIn*/,
                                std::string* /*err*/ = nullptr) override
    {
        return true;
    }
    uint64_t GetMaxStackMemoryUsage(bool /*isGenesisEnabled*/, bool /*consensus*/) const override
    {
        return UINT32_MAX;
    }

    bool SetMaxOpsPerScriptPolicy(int64_t /*maxOpsPerScriptPolicyIn*/,
                                  std::string* /*error*/) override
    {
        return true;
    }
    uint64_t GetMaxOpsPerScript(bool isGenesisEnabled, bool /*consensus*/) const override
    {
        if(isGenesisEnabled)
        {
            return MAX_OPS_PER_SCRIPT_AFTER_GENESIS;
        }
        else
        {
            return MAX_OPS_PER_SCRIPT_BEFORE_GENESIS;
        }
    }
    bool SetMaxTxSigOpsCountPolicy(int64_t /*maxTxSigOpsCountIn*/,
                                   std::string* /*err*/ = nullptr) override
    {
        return true;
    }
    uint64_t GetMaxTxSigOpsCountConsensusBeforeGenesis() const override
    {
        return MAX_TX_SIGOPS_COUNT_POLICY_BEFORE_GENESIS;
    }
    uint64_t GetMaxTxSigOpsCountPolicy(ProtocolEra) const override
    {
        return MAX_TX_SIGOPS_COUNT_POLICY_BEFORE_GENESIS;
    }

    uint64_t GetMaxBlockSigOpsConsensusBeforeGenesis(uint64_t /*blockSize*/) const override
    {
        throw std::runtime_error("DummyCofig::GetMaxBlockSigOps not implemented");
    }

    bool SetGenesisGracefulPeriod(int64_t /*genesisGracefulPeriodIn*/,
                                  std::string* /*err*/ = nullptr) override
    {
        return true;
    }
    uint64_t GetGenesisGracefulPeriod() const override
    {
        return DEFAULT_GENESIS_GRACEFUL_ACTIVATION_PERIOD;
    }
    bool SetChronicleGracefulPeriod(int64_t /*chronicleGracefulPeriodIn*/,
                                    std::string* /*err*/ = nullptr) override
    {
        return true;
    }
    uint64_t GetChronicleGracefulPeriod() const override
    {
        return DEFAULT_CHRONICLE_GRACEFUL_ACTIVATION_PERIOD;
    }

    bool SetMaxPubKeysPerMultiSigPolicy(int64_t /*maxPubKeysPerMultiSigIn*/,
                                        std::string* /*err*/ = nullptr) override
    {
        return true;
    }
    uint64_t GetMaxPubKeysPerMultiSig(bool isGenesisEnabled,
                                      bool /*consensus*/) const override
    {
        if(isGenesisEnabled)
        {
            return MAX_PUBKEYS_PER_MULTISIG_AFTER_GENESIS;
        }
        else
        {
            return MAX_PUBKEYS_PER_MULTISIG_BEFORE_GENESIS;
        }
    }

    bool SetMaxStdTxnValidationDuration(int /*ms*/, std::string* err = nullptr) override
    {
        SetErrorMsg(err);

        return false;
    }
    std::chrono::milliseconds GetMaxStdTxnValidationDuration() const override
    {
        return DEFAULT_MAX_STD_TXN_VALIDATION_DURATION;
    }

    bool SetMaxNonStdTxnValidationDuration(int /*ms*/, std::string* err = nullptr) override
    {
        SetErrorMsg(err);

        return false;
    }
    std::chrono::milliseconds GetMaxNonStdTxnValidationDuration() const override
    {
        return DEFAULT_MAX_NON_STD_TXN_VALIDATION_DURATION;
    }

    bool SetMaxTxnValidatorAsyncTasksRunDuration(int /*ms*/,
                                                 std::string* err = nullptr) override
    {
        SetErrorMsg(err);

        return false;
    }

    std::chrono::milliseconds GetMaxTxnValidatorAsyncTasksRunDuration() const override
    {
        return CTxnValidator::DEFAULT_MAX_ASYNC_TASKS_RUN_DURATION;
    }

    bool SetMaxTxnChainValidationBudget(int /*ms*/, std::string* err = nullptr) override
    {
        SetErrorMsg(err);

        return false;
    }
    std::chrono::milliseconds GetMaxTxnChainValidationBudget() const override
    {
        return DEFAULT_MAX_TXN_CHAIN_VALIDATION_BUDGET;
    }

    void SetValidationClockCPU(bool /*enable*/) override {}
    bool GetValidationClockCPU() const override { return DEFAULT_VALIDATION_CLOCK_CPU; }

    bool SetPTVTaskScheduleStrategy(PTVTaskScheduleStrategy,
                                    std::string* /*err*/ = nullptr) override
    {
        return true;
    }
    PTVTaskScheduleStrategy GetPTVTaskScheduleStrategy() const override
    {
        return DEFAULT_PTV_TASK_SCHEDULE_STRATEGY;
    }

    bool SetMaxScriptSizePolicy(int64_t maxScriptSizePolicyIn,
                                std::string* err = nullptr) override
    {
        SetErrorMsg(err);
        maxScriptSizePolicy = static_cast<uint64_t>(maxScriptSizePolicyIn);
        return true;
    };
    uint64_t GetMaxScriptSize(bool /*isGenesisEnabled*/, bool /*isConsensus*/) const override
    {
        return maxScriptSizePolicy;
    };

    bool SetMaxScriptNumLengthPolicy(int64_t /*maxScriptNumLengthIn*/,
                                     std::string* /*err*/ = nullptr) override
    {
        return true;
    }
    uint64_t GetMaxScriptNumLength(ProtocolEra era, bool /*isConsensus*/) const override
    {
        if(!IsProtocolActive(era, ProtocolName::Genesis))
        {
            return MAX_SCRIPT_NUM_LENGTH_BEFORE_GENESIS;
        }
        if(!IsProtocolActive(era, ProtocolName::Chronicle))
        {
            return MAX_SCRIPT_NUM_LENGTH_AFTER_GENESIS;
        }
        return MAX_SCRIPT_NUM_LENGTH_AFTER_CHRONICLE;
    }

    void SetAcceptNonStandardOutput(bool accept) override
    {
        dummyPolicySettings.SetAcceptNonStandardOutput(accept);
    }
    bool GetAcceptNonStandardOutput(ProtocolEra era) const override
    {
        return dummyPolicySettings.GetAcceptNonStandardOutput(era);
    }
    void SetRequireStandard(bool require) override
    {
        dummyPolicySettings.SetRequireStandard(require);
    }
    void SetPermitBareMultisig(bool permit) override
    {
        dummyPolicySettings.SetPermitBareMultisig(permit);
    }

    bool SetMaxCoinsViewCacheSize(int64_t /*max*/, std::string* err) override
    {
        SetErrorMsg(err);

        return false;
    }
    uint64_t GetMaxCoinsViewCacheSize() const override { return 0; /* unlimited */ }

    bool SetMaxCoinsProviderCacheSize(int64_t /*max*/, std::string* err) override
    {
        SetErrorMsg(err);

        return false;
    }
    uint64_t GetMaxCoinsProviderCacheSize() const override { return 0; /* unlimited */ }

    bool SetMaxCoinsDbOpenFiles(int64_t /*max*/, std::string* err) override
    {
        SetErrorMsg(err);

        return false;
    }
    uint64_t GetMaxCoinsDbOpenFiles() const override { return 64; /* old default */ }

    bool SetCoinsDBMaxFileSize(int64_t /*max*/, std::string* /*err*/) override { return true; }
    uint64_t GetCoinsDBMaxFileSize() const override
    {
        return CoinsDBDefaults::DEFAULT_MAX_LEVELDB_FILE_SIZE;
    }

    bool SetMaxMempool(int64_t /*maxMempool*/, std::string* err) override
    {
        SetErrorMsg(err);

        return true;
    }
    uint64_t GetMaxMempool() const override
    {
        return DEFAULT_MAX_MEMPOOL_SIZE * ONE_MEGABYTE;
    }

    bool SetMaxMempoolSizeDisk(int64_t /*maxMempoolSizeDisk*/, std::string* err) override
    {
        SetErrorMsg(err);

        return true;
    }
    uint64_t GetMaxMempoolSizeDisk() const override
    {
        return DEFAULT_MAX_MEMPOOL_SIZE * DEFAULT_MAX_MEMPOOL_SIZE_DISK_FACTOR *
               ONE_MEGABYTE;
    }

    bool SetMempoolMaxPercentCPFP(int64_t /*mempoolMaxPercentCPFP*/,
                                  std::string* err) override
    {
        SetErrorMsg(err);

        return true;
    }
    uint64_t GetMempoolMaxPercentCPFP() const override
    {
        return DEFAULT_MEMPOOL_MAX_PERCENT_CPFP;
    }

    bool SetMemPoolExpiry(int64_t /*memPoolExpiry*/, std::string* err) override
    {
        SetErrorMsg(err);

        return true;
    }
    uint64_t GetMemPoolExpiry() const override
    {
        return DEFAULT_MEMPOOL_EXPIRY * SECONDS_IN_ONE_HOUR;
    }

    bool SetMaxOrphanTxSize(int64_t /*maxOrphanTxSize*/, std::string* err) override
    {
        SetErrorMsg(err);

        return true;
    }
    uint64_t GetMaxOrphanTxSize() const override
    {
        return COrphanTxns::DEFAULT_MAX_ORPHAN_TRANSACTIONS_SIZE;
    }

    bool SetMaxOrphansInBatchPercentage(uint64_t /*percent*/, std::string* err) override
    {
        SetErrorMsg(err);

        return true;
    }

    uint64_t GetMaxOrphansInBatchPercentage() const override
    {
        return COrphanTxns::DEFAULT_MAX_PERCENTAGE_OF_ORPHANS_IN_BATCH;
    };

    bool SetMaxInputsForSecondLayerOrphan(uint64_t /*maxInputs*/, std::string* err) override
    {
        SetErrorMsg(err);

        return true;
    }
    uint64_t GetMaxInputsForSecondLayerOrphan() const override
    {
        return COrphanTxns::DEFAULT_MAX_INPUTS_OUTPUTS_PER_TRANSACTION;
    };

    bool SetStopAtHeight(int32_t /*stopAtHeight*/, std::string* err) override
    {
        SetErrorMsg(err);

        return true;
    }
    int32_t GetStopAtHeight() const override { return DEFAULT_STOPATHEIGHT; }

    bool SetPromiscuousMempoolFlags(int64_t /*promiscuousMempoolFlags*/,
                                    std::string* err) override
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

    const std::set<uint256>& GetInvalidBlocks() const override { return mInvalidBlocks; };

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

    bool SetMaxProtocolRecvPayloadLength(uint64_t /*value*/, std::string* /*err*/) override
    {
        return true;
    }
    bool SetRecvInvQueueFactor(uint64_t /*value*/, std::string* /*err*/) override { return true; }
    unsigned int GetMaxProtocolRecvPayloadLength() const override
    {
        return DEFAULT_MAX_PROTOCOL_RECV_PAYLOAD_LENGTH;
    }
    unsigned int GetMaxProtocolSendPayloadLength() const override
    {
        return DEFAULT_MAX_PROTOCOL_RECV_PAYLOAD_LENGTH *
               MAX_PROTOCOL_SEND_PAYLOAD_FACTOR;
    }
    unsigned int GetRecvInvQueueFactor() const override
    {
        return DEFAULT_RECV_INV_QUEUE_FACTOR;
    }

    bool AddInvalidTxSink(const std::string& /*sink*/, std::string* /*err*/ = nullptr) override
    {
        return true;
    };
    std::set<std::string> GetInvalidTxSinks() const override { return {"NONE"}; };
    std::set<std::string> GetAvailableInvalidTxSinks() const override
    {
        return {"NONE"};
    };

    bool SetInvalidTxFileSinkMaxDiskUsage(int64_t /*max*/,
                                          std::string* /*err*/ = nullptr) override
    {
        return true;
    };
    int64_t GetInvalidTxFileSinkMaxDiskUsage() const override
    {
        return 300 * ONE_MEGABYTE;
    };

    bool SetInvalidTxFileSinkEvictionPolicy(std::string /*policy*/,
                                            std::string* /*err*/ = nullptr) override
    {
        return true;
    };
    InvalidTxEvictionPolicy GetInvalidTxFileSinkEvictionPolicy() const override
    {
        return InvalidTxEvictionPolicy::IGNORE_NEW;
    };

    void SetEnableAssumeWhitelistedBlockDepth(bool /*enabled*/) override {}
    bool GetEnableAssumeWhitelistedBlockDepth() const override
    {
        return DEFAULT_ENABLE_ASSUME_WHITELISTED_BLOCK_DEPTH;
    }
    bool SetAssumeWhitelistedBlockDepth(int64_t /*depth*/,
                                        std::string* /*err*/ = nullptr) override
    {
        return true;
    }
    int32_t GetAssumeWhitelistedBlockDepth() const override
    {
        return DEFAULT_ASSUME_WHITELISTED_BLOCK_DEPTH;
    }

    bool SetMinBlocksToKeep(int32_t /*minblocks*/, std::string* /*err*/ = nullptr) override
    {
        return true;
    }
    int32_t GetMinBlocksToKeep() const override { return DEFAULT_MIN_BLOCKS_TO_KEEP; }
    bool SetBlockValidationTxBatchSize(int64_t /*size*/, std::string* /*err*/ = nullptr) override
    {
        return true;
    }
    uint64_t GetBlockValidationTxBatchSize() const override
    {
        return DEFAULT_BLOCK_VALIDATION_TX_BATCH_SIZE;
    }

    // Block download
    bool SetBlockStallingMinDownloadSpeed(int64_t /*min*/,
                                          std::string* /*err*/ = nullptr) override
    {
        return true;
    }
    uint64_t GetBlockStallingMinDownloadSpeed() const override
    {
        return DEFAULT_MIN_BLOCK_STALLING_RATE;
    }
    bool SetBlockStallingTimeout(int64_t /*timeout*/, std::string* /*err*/ = nullptr) override
    {
        return true;
    }
    int64_t GetBlockStallingTimeout() const override
    {
        return DEFAULT_BLOCK_STALLING_TIMEOUT;
    }
    bool SetBlockDownloadWindow(int64_t /*window*/, std::string* /*err*/ = nullptr) override
    {
        return true;
    }
    int64_t GetBlockDownloadWindow() const override
    {
        return DEFAULT_BLOCK_DOWNLOAD_WINDOW;
    }
    bool SetBlockDownloadLowerWindow(int64_t /*window*/, std::string* /*err*/ = nullptr) override
    {
        return true;
    }
    int64_t GetBlockDownloadLowerWindow() const override
    {
        return DEFAULT_BLOCK_DOWNLOAD_LOWER_WINDOW;
    }
    bool SetBlockDownloadSlowFetchTimeout(int64_t /*timeout*/,
                                          std::string* /*err*/ = nullptr) override
    {
        return true;
    }
    int64_t GetBlockDownloadSlowFetchTimeout() const override
    {
        return DEFAULT_BLOCK_DOWNLOAD_SLOW_FETCH_TIMEOUT;
    }
    bool SetBlockDownloadMaxParallelFetch(int64_t /*max*/,
                                          std::string* /*err*/ = nullptr) override
    {
        return true;
    }
    uint64_t GetBlockDownloadMaxParallelFetch() const override
    {
        return DEFAULT_MAX_BLOCK_PARALLEL_FETCH;
    }
    bool SetBlockDownloadTimeoutBase(int64_t /*max*/, std::string* /*err*/ = nullptr) override
    {
        return true;
    }
    int64_t GetBlockDownloadTimeoutBase() const override
    {
        return DEFAULT_BLOCK_DOWNLOAD_TIMEOUT_BASE;
    }
    bool SetBlockDownloadTimeoutBaseIBD(int64_t /*max*/, std::string* /*err*/ = nullptr) override
    {
        return true;
    }
    int64_t GetBlockDownloadTimeoutBaseIBD() const override
    {
        return DEFAULT_BLOCK_DOWNLOAD_TIMEOUT_BASE_IBD;
    }
    bool SetBlockDownloadTimeoutPerPeer(int64_t /*max*/, std::string* /*err*/ = nullptr) override
    {
        return true;
    }
    int64_t GetBlockDownloadTimeoutPerPeer() const override
    {
        return DEFAULT_BLOCK_DOWNLOAD_TIMEOUT_PER_PEER;
    }

    // P2P parameters
    bool SetP2PHandshakeTimeout(int64_t /*timeout*/, std::string* /*err*/ = nullptr) override
    {
        return true;
    }
    int64_t GetP2PHandshakeTimeout() const override
    {
        return DEFAULT_P2P_HANDSHAKE_TIMEOUT_INTERVAL;
    }
    bool SetStreamSendRateLimit(int64_t /*limit*/, std::string* /*err*/ = nullptr) override
    {
        return true;
    }
    int64_t GetStreamSendRateLimit() const override
    {
        return Stream::DEFAULT_SEND_RATE_LIMIT;
    }
    bool SetBanScoreThreshold(int64_t /*threshold*/, std::string* /*err*/ = nullptr) override
    {
        return true;
    }
    unsigned int GetBanScoreThreshold() const override
    {
        return DEFAULT_BANSCORE_THRESHOLD;
    }
    bool SetBlockTxnMaxPercent(unsigned int /*percent*/, std::string* /*err*/ = nullptr) override
    {
        return true;
    }
    unsigned int GetBlockTxnMaxPercent() const override
    {
        return DEFAULT_BLOCK_TXN_MAX_PERCENT;
    }
    bool SetMultistreamsEnabled(bool /*enabled*/, std::string* /*err*/ = nullptr) override
    {
        return true;
    }
    bool GetMultistreamsEnabled() const override { return DEFAULT_STREAMS_ENABLED; }
    bool SetWhitelistRelay(bool /*relay*/, std::string* /*err*/ = nullptr) override
    {
        return true;
    }
    bool GetWhitelistRelay() const override { return DEFAULT_WHITELISTRELAY; }
    bool SetWhitelistForceRelay(bool /*relay*/, std::string* /*err*/ = nullptr) override
    {
        return true;
    }
    bool GetWhitelistForceRelay() const override { return DEFAULT_WHITELISTFORCERELAY; }
    bool SetRejectMempoolRequest(bool /*reject*/, std::string* /*err*/ = nullptr) override
    {
        return true;
    }
    bool GetRejectMempoolRequest() const override { return DEFAULT_REJECTMEMPOOLREQUEST; }
    bool SetDropMessageTest(int64_t /*val*/, std::string* /*err*/ = nullptr) override
    {
        return true;
    }
    bool DoDropMessageTest() const override { return false; }
    uint64_t GetDropMessageTest() const override { return 0; }
    bool SetInvalidChecksumInterval(int64_t /*val*/, std::string* /*err*/ = nullptr) override
    {
        return true;
    }
    unsigned int GetInvalidChecksumInterval() const override
    {
        return DEFAULT_MIN_TIME_INTERVAL_CHECKSUM_MS;
    }
    bool SetInvalidChecksumFreq(int64_t /*val*/, std::string* /*err*/ = nullptr) override
    {
        return true;
    }
    unsigned int GetInvalidChecksumFreq() const override
    {
        return DEFAULT_INVALID_CHECKSUM_FREQUENCY;
    }
    bool SetFeeFilter(bool /*feefilter*/, std::string* /*err*/ = nullptr) override
    {
        return true;
    }
    bool GetFeeFilter() const override { return DEFAULT_FEEFILTER; }
    bool SetMaxAddNodeConnections(int16_t /*max*/, std::string* /*err*/ = nullptr) override
    {
        return true;
    }
    uint16_t GetMaxAddNodeConnections() const override
    {
        return DEFAULT_MAX_ADDNODE_CONNECTIONS;
    }
    bool SetMaxRecvBuffer(int64_t /*max*/, std::string* /*err*/ = nullptr) override
    {
        return true;
    }
    uint64_t GetMaxRecvBuffer() const override
    {
        return DEFAULT_MAXRECEIVEBUFFER * ONE_KILOBYTE;
    }

    // RPC parameters
    bool SetWebhookClientNumThreads(int64_t /*num*/, std::string* /*err*/) override
    {
        return true;
    }
    uint64_t GetWebhookClientNumThreads() const override
    {
        return rpc::client::WebhookClientDefaults::DEFAULT_NUM_THREADS;
    }
    bool SetWebhookClientMaxResponseBodySize(int64_t /*size*/, std::string* /*err*/ = nullptr) override
    {
        return true;
    }
    int64_t GetWebhookClientMaxResponseBodySize() const override
    {
        return rpc::client::WebhookClientDefaults::DEFAULT_MAX_RESPONSE_BODY_SIZE_BYTES;
    }
    bool SetWebhookClientMaxResponseHeadersSize(int64_t /*size*/, std::string* /*err*/ = nullptr) override
    {
        return true;
    }
    int64_t GetWebhookClientMaxResponseHeadersSize() const override
    {
        return rpc::client::WebhookClientDefaults::DEFAULT_MAX_RESPONSE_HEADERS_SIZE_BYTES;
    }

    // Double-Spend processing parameters
    bool SetDoubleSpendNotificationLevel(int /*level*/, std::string* /*err*/) override
    {
        return true;
    }
    DSAttemptHandler::NotificationLevel GetDoubleSpendNotificationLevel() const override
    {
        return DSAttemptHandler::DEFAULT_NOTIFY_LEVEL;
    }
    bool SetDoubleSpendEndpointFastTimeout(int /*timeout*/, std::string* /*err*/) override
    {
        return true;
    }
    int GetDoubleSpendEndpointFastTimeout() const override
    {
        return rpc::client::RPCClientConfig::DEFAULT_DS_ENDPOINT_FAST_TIMEOUT;
    }
    bool SetDoubleSpendEndpointSlowTimeout(int /*timeout*/, std::string* /*err*/) override
    {
        return true;
    }
    int GetDoubleSpendEndpointSlowTimeout() const override
    {
        return rpc::client::RPCClientConfig::DEFAULT_DS_ENDPOINT_SLOW_TIMEOUT;
    }
    bool SetDoubleSpendEndpointSlowRatePerHour(int64_t /*rate*/, std::string* /*err*/) override
    {
        return true;
    }
    uint64_t GetDoubleSpendEndpointSlowRatePerHour() const override
    {
        return DSAttemptHandler::DEFAULT_DS_ENDPOINT_SLOW_RATE_PER_HOUR;
    }
    bool SetDoubleSpendEndpointPort(int /*port*/, std::string* /*err*/) override { return true; }
    int GetDoubleSpendEndpointPort() const override
    {
        return rpc::client::RPCClientConfig::DEFAULT_DS_ENDPOINT_PORT;
    }
    bool SetDoubleSpendTxnRemember(int64_t /*size*/, std::string* /*err*/) override
    {
        return true;
    }
    uint64_t GetDoubleSpendTxnRemember() const override
    {
        return DSAttemptHandler::DEFAULT_TXN_REMEMBER_COUNT;
    }
    bool SetDoubleSpendEndpointBlacklistSize(int64_t /*size*/, std::string* /*err*/) override
    {
        return true;
    }
    uint64_t GetDoubleSpendEndpointBlacklistSize() const override
    {
        return DSAttemptHandler::DEFAULT_DS_ENDPOINT_BLACKLIST_SIZE;
    }
    bool SetDoubleSpendEndpointSkipList(const std::string& /*skip*/,
                                        std::string* /*err*/) override
    {
        return true;
    }
    std::set<std::string> GetDoubleSpendEndpointSkipList() const override { return {}; }
    bool SetDoubleSpendEndpointMaxCount(int64_t /*max*/, std::string* /*err*/) override
    {
        return true;
    }
    uint64_t GetDoubleSpendEndpointMaxCount() const override
    {
        return DSAttemptHandler::DEFAULT_DS_ENDPOINT_MAX_COUNT;
    }
    bool SetDoubleSpendNumFastThreads(int64_t /*num*/, std::string* /*err*/) override
    {
        return true;
    }
    uint64_t GetDoubleSpendNumFastThreads() const override
    {
        return DSAttemptHandler::DEFAULT_NUM_FAST_THREADS;
    }
    bool SetDoubleSpendNumSlowThreads(int64_t /*num*/, std::string* /*err*/) override
    {
        return true;
    }
    uint64_t GetDoubleSpendNumSlowThreads() const override
    {
        return DSAttemptHandler::DEFAULT_NUM_SLOW_THREADS;
    }
    bool SetDoubleSpendQueueMaxMemory(int64_t /*max*/, std::string* /*err*/) override
    {
        return true;
    }
    uint64_t GetDoubleSpendQueueMaxMemory() const override
    {
        return DSAttemptHandler::DEFAULT_MAX_SUBMIT_MEMORY * ONE_MEGABYTE;
    }
    bool SetDoubleSpendDetectedWebhookURL(const std::string& /*url*/,
                                          std::string* /*err*/) override
    {
        return true;
    }
    std::string GetDoubleSpendDetectedWebhookAddress() const override { return ""; }
    int16_t GetDoubleSpendDetectedWebhookPort() const override
    {
        return rpc::client::WebhookClientDefaults::DEFAULT_WEBHOOK_PORT;
    }
    std::string GetDoubleSpendDetectedWebhookPath() const override { return ""; }
    bool SetDoubleSpendDetectedWebhookMaxTxnSize(int64_t /*max*/, std::string* /*err*/) override
    {
        return true;
    }
    uint64_t GetDoubleSpendDetectedWebhookMaxTxnSize() const override
    {
        return DSDetectedDefaults::DEFAULT_MAX_WEBHOOK_TXN_SIZE * ONE_MEBIBYTE;
    }

    // MinerID
    bool SetMinerIdEnabled(bool /*enabled*/, std::string* /*err*/) override { return true; }
    bool GetMinerIdEnabled() const override
    {
        return MinerIdDatabaseDefaults::DEFAULT_MINER_ID_ENABLED;
    }
    bool SetMinerIdCacheSize(int64_t /*size*/, std::string* /*err*/) override { return true; }
    uint64_t GetMinerIdCacheSize() const override
    {
        return MinerIdDatabaseDefaults::DEFAULT_CACHE_SIZE;
    }
    bool SetMinerIdsNumToKeep(int64_t /*num*/, std::string* /*err*/) override { return true; }
    uint64_t GetMinerIdsNumToKeep() const override
    {
        return MinerIdDatabaseDefaults::DEFAULT_MINER_IDS_TO_KEEP;
    }
    bool SetMinerIdReputationM(int64_t /*num*/, std::string* /*err*/) override { return true; }
    uint32_t GetMinerIdReputationM() const override
    {
        return MinerIdDatabaseDefaults::DEFAULT_MINER_REPUTATION_M;
    }
    bool SetMinerIdReputationN(int64_t /*num*/, std::string* /*err*/) override { return true; }
    uint32_t GetMinerIdReputationN() const override
    {
        return MinerIdDatabaseDefaults::DEFAULT_MINER_REPUTATION_N;
    }
    bool SetMinerIdReputationMScale(double /*num*/, std::string* /*err*/) override
    {
        return true;
    }
    double GetMinerIdReputationMScale() const override
    {
        return MinerIdDatabaseDefaults::DEFAULT_M_SCALE_FACTOR;
    }
    std::string GetMinerIdGeneratorAddress() const override { return ""; }
    int16_t GetMinerIdGeneratorPort() const override
    {
        return rpc::client::WebhookClientDefaults::DEFAULT_WEBHOOK_PORT;
    }
    std::string GetMinerIdGeneratorPath() const override { return ""; }
    std::string GetMinerIdGeneratorAlias() const override { return ""; }
    bool SetMinerIdGeneratorURL(const std::string& /*url*/, std::string* /*err*/) override
    {
        return true;
    }
    bool SetMinerIdGeneratorAlias(const std::string& /*alias*/, std::string* /*err*/) override
    {
        return true;
    }

#if ENABLE_ZMQ
    bool SetInvalidTxZMQMaxMessageSize(int64_t /*max*/, std::string* /*err*/ = nullptr) override
    {
        return true;
    };
    int64_t GetInvalidTxZMQMaxMessageSize() const override { return 10 * ONE_MEGABYTE; };
#endif

    bool SetMaxMerkleTreeDiskSpace(int64_t /*maxDiskSpace*/,
                                   std::string* /*err*/ = nullptr) override
    {
        return true;
    }

    uint64_t GetMaxMerkleTreeDiskSpace() const override { return 0; }

    bool SetPreferredMerkleTreeFileSize(int64_t /*preferredFileSize*/,
                                        std::string* /*err*/ = nullptr) override
    {
        return true;
    }

    uint64_t GetPreferredMerkleTreeFileSize() const override { return 0; }

    bool SetMaxMerkleTreeMemoryCacheSize(int64_t /*maxMemoryCacheSize*/,
                                         std::string* /*err*/ = nullptr) override
    {
        return true;
    }

    uint64_t GetMaxMerkleTreeMemoryCacheSize() const override { return 0; }

    bool SetDisableBIP30Checks(bool /*disable*/, std::string* /*err*/ = nullptr) override
    {
        return true;
    }

    bool GetDisableBIP30Checks() const override { return true; }

    bool SetSoftConsensusFreezeDuration(std::int64_t /*duration*/, std::string* /*err*/) override
    {
        return true;
    }

    std::int32_t GetSoftConsensusFreezeDuration() const override
    {
        return std::numeric_limits<std::int32_t>::max();
    }

    // Safe mode params
    bool SetSafeModeWebhookURL(const std::string& /*url*/,
                               std::string* /*err*/ = nullptr) override
    {
        return true;
    }
    std::string GetSafeModeWebhookAddress() const override { return ""; }
    int16_t GetSafeModeWebhookPort() const override
    {
        return rpc::client::WebhookClientDefaults::DEFAULT_WEBHOOK_PORT;
    }
    std::string GetSafeModeWebhookPath() const override { return ""; }
    int64_t GetSafeModeMaxForkDistance() const override { return 100; };
    bool SetSafeModeMaxForkDistance(int64_t /*distance*/, std::string* /*err*/) override
    {
        return true;
    };
    int64_t GetSafeModeMinForkLength() const override { return 3; };
    bool SetSafeModeMinForkLength(int64_t /*length*/, std::string* /*err*/) override
    {
        return true;
    };
    int64_t GetSafeModeMinForkHeightDifference() const override { return 5; };
    bool SetSafeModeMinForkHeightDifference(int64_t /*heightDifference*/,
                                            std::string* /*err*/) override
    {
        return true;
    };

    bool GetDetectSelfishMining() const override { return true; };

    void SetDetectSelfishMining(bool /*detectSelfishMining*/) override {}

    int64_t GetMinBlockMempoolTimeDifferenceSelfish() const override
    {
        return DEFAULT_MIN_BLOCK_MEMPOOL_TIME_DIFFERENCE_SELFISH;
    };

    bool SetMinBlockMempoolTimeDifferenceSelfish(int64_t /*minBlockMempoolTimeDiffIn*/,
                                                 std::string* /*err*/) override
    {
        return true;
    }

    uint64_t GetSelfishTxThreshold() const override
    {
        return DEFAULT_SELFISH_TX_THRESHOLD_IN_PERCENT;
    }

    bool SetSelfishTxThreshold(uint64_t /*selfishTxPercentThreshold*/,
                               std::string* /*err*/) override
    {
        return true;
    }

    // Mempool syncing
    int64_t GetMempoolSyncAge() const override { return MempoolMsg::DEFAULT_AGE; }
    bool SetMempoolSyncAge(int64_t /*age*/, std::string* /*err*/) override { return true; }
    int64_t GetMempoolSyncPeriod() const override { return MempoolMsg::DEFAULT_PERIOD; }
    bool SetMempoolSyncPeriod(int64_t /*period*/, std::string* /*err*/) override { return true; }

    void Reset() override;

    const ConfigScriptPolicy& GetConfigScriptPolicy() const override { return dummyPolicySettings; };

private:

    ConfigScriptPolicy dummyPolicySettings{};
    uint64_t maxScriptSizePolicy{DEFAULT_MAX_SCRIPT_SIZE_POLICY_AFTER_GENESIS};

    std::unique_ptr<CChainParams> chainParams;
    uint64_t dataCarrierSize{DEFAULT_DATA_CARRIER_SIZE};
    bool dataCarrier{DEFAULT_ACCEPT_DATACARRIER};

    uint64_t maxTxSizePolicy{DEFAULT_MAX_TX_SIZE_POLICY_AFTER_GENESIS};
    uint64_t minConsolidationFactor{DEFAULT_MIN_CONSOLIDATION_FACTOR};
    uint64_t maxConsolidationInputScriptSize{DEFAULT_MAX_CONSOLIDATION_INPUT_SCRIPT_SIZE};
    uint64_t minConfConsolidationInput{DEFAULT_MIN_CONF_CONSOLIDATION_INPUT};
    uint64_t acceptNonStdConsolidationInput{DEFAULT_ACCEPT_NON_STD_CONSOLIDATION_INPUT};

    std::set<uint256> mInvalidBlocks;
    std::set<std::string> mBannedUAClients{DEFAULT_CLIENTUA_BAN_PATTERNS};
    std::set<std::string> mAllowedUAClients;

    void SetErrorMsg(std::string* err)
    {
        if(err)
        {
            *err = "This is dummy config";
        }
    }
};
