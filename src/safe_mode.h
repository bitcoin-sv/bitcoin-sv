// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include <map>

#include "sync.h"
#include "block_index.h"
#include "warnings.h"
#include "rpc/webhook_client.h"

class CJSONWriter;

class SafeMode
{
    /**
     * Checks if block is part of one of the forks that are causing safe mode
     */
    bool IsBlockPartOfExistingSafeModeFork(const CBlockIndex* pindexNew) const;

    /**
     * Method checks if fork should cause node to enter safe mode.
     *
     * @param[in]      pindexForkTip  Tip of the fork that we are validating.
     * @param[in]      pindexForkBase The first block of the fork, its parent is on the main chain.
     * @return Returns level of safe mode that this fork triggers:
     */
    SafeModeLevel ShouldForkTriggerSafeMode(const Config& config, const CBlockIndex* pindexForkTip, const CBlockIndex* pindexForkBase) const;

    /**
    * Returns block height for the ro
    */
    int64_t GetMinimumRelevantBlockHeight(const Config& config) const;

    /**
     * Creates or updates entry in the safeModeForks for fork which is 
     * determined with pindexNew
     */
    void CreateForkData(const Config& config, const CBlockIndex* pindexNew);

    /**
    * Updates all existing fork data related to possible active chain changes.
    */
    void UpdateCurentForkData();
    
    /**
     * Deletes entries in the safeModeForks which have fork distance larger than configured
     */
    void PruneStaleForkData(const Config& config);

    /**
     * Returns a new fork tip which is result of ignoring specific blocks for the safe mode,
     * and set of all blocks that should be ignored.
     * If the whole fork is marked for ignoring nullptr is returned.
     */
    std::tuple<const CBlockIndex*, std::vector<const CBlockIndex*>> ExcludeIgnoredBlocks(const Config& config, const CBlockIndex* pindexForkTip) const;

    /**
    * Represents single forking of the main chain
    */
    struct SafeModeFork
    {
        std::set<const CBlockIndex*> tips;
        const CBlockIndex* base;
        static bool CompareBlockIndex(const CBlockIndex* lhs, const CBlockIndex* rhs) 
        {
            if (lhs->GetHeight() == rhs->GetHeight())
            {
                return lhs->GetBlockHash() < rhs->GetBlockHash();
            }
            return lhs->GetHeight() < rhs->GetHeight();
        }
        bool operator == (const SafeModeFork& other) const
        {
            return tips == other.tips 
                && base == other.base;
        }
    };

    /**
    * Represents collection of forks which are causing safe mode
    */
    struct SafeModeResult
    {
        const CBlockIndex* activeChainTip;
        const CBlockIndex* reorgedFrom;
        int numberOfDisconnectedBlocks;
        std::map<const CBlockIndex*, SafeModeFork> forks;
        SafeModeLevel maxLevel;

        bool ShouldNotify(const SafeModeResult& oldResult) const;
        void AddFork(const CBlockIndex* forkTip, const CBlockIndex* forkBase, SafeModeLevel level);
        void ToJson(CJSONWriter& writer) const;
        std::string ToJson(bool pretty = false) const;
    };

    /**
    * Recalculates safe mode criteria for all forks.
    */
    SafeModeResult GetSafeModeResult(const Config& config, const CBlockIndex* prevTip);

    /**
    * Sends result in json form using webhooks.
    */
    void NotifyUsingWebhooks(const Config& config, const SafeModeResult& result);

private: // data members
    // collection of current forks that can potentially trigger the safe mode (key: fork tip, value: blocks in order from highest to lowest)
    std::map<const CBlockIndex*, std::shared_ptr<std::deque<const CBlockIndex*>>> safeModeForks;

    // all blocks and its descendants which are marked for ignoring the safe mode
    std::set<const CBlockIndex*> ignoredBlocks;
    
    // last safe mode status
    SafeModeResult currentResult;

    // last result notified by webhook
    SafeModeResult currentResultWebhook;

    // a block which was the active tip last time we have updated fork data
    const CBlockIndex* oldTip {nullptr};
    
    // protect this class members
    mutable CCriticalSection cs_safeModeLevelForks;

    // webhook objects, initialized on first access
    std::unique_ptr<rpc::client::WebhookClient> webhooks;
    std::unique_ptr<rpc::client::RPCClientConfig> webhookConfig;

public:
    /**
    * Updates safe mode status after a change in the blockchain.
    * If the change is related to a single block (connected, invalidated, ...) it is passed as pindexNew. 
    * If the change is complex (reorg) or we need to recalculate state (startup) is is called with pindexNew as nullptr; This will force recalculation of the state.
    */
    void CheckSafeModeParameters(const Config& config, const CBlockIndex* pindexNew);

    /**
    * Empties current fork data.
    */
    void Clear();

    /**
    * Serializes current status in the json format
    */
    void GetStatus(CJSONWriter& writer);
    std::string GetStatus();
};

/**
 * Calling SafeMode::Clear()
 */
void SafeModeClear();

/**
 * Calling SafeMode::CheckSafeModeParameters
 */
void CheckSafeModeParameters(const Config& conf, const CBlockIndex* pindexNew);

/**
 * Calling SafeMode::GetStatus
 */
void SafeModeGetStatus(CJSONWriter& writer);
std::string SafeModeGetStatus();