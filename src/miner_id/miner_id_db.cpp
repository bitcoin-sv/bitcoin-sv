// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "miner_id/miner_id_db.h"

#include "block_index_store.h"
#include "blockstreams.h"
#include "config.h"
#include "logging.h"
#include "miner_id/miner_id.h"
#include "scheduler.h"
#include "univalue.h"

#include <algorithm>

#include <boost/uuid/uuid_io.hpp>

std::unique_ptr<MinerIdDatabase> g_minerIDs {nullptr};

namespace
{
    // How frequently we run the database pruning (once per day)
    constexpr int64_t PRUNE_PERIOD_SECS { 60 * 60 * 24 };
}

// Start MinerID database periodic tasks
void ScheduleMinerIdPeriodicTasks(CScheduler& scheduler, MinerIdDatabase& db)
{
    // Schedule database pruning
    scheduler.scheduleEvery(std::bind(&MinerIdDatabase::Prune, &db), PRUNE_PERIOD_SECS * 1000);
}

// Does the miner identified with the given miner ID have a good reputation?
bool MinerHasGoodReputation(const MinerIdDatabase& db, const MinerId& id)
{
    try
    {
        // Lookup miner ID
        const CoinbaseDocument& cbDoc { id.GetCoinbaseDocument() };
        const CPubKey& idPubKey { cbDoc.GetMinerIdAsKey() };

        // Check reputation
        return db.CheckMinerReputation(idPubKey.GetHash());
    }
    catch(const std::exception& e)
    {
        LogPrint(BCLog::MINERID, "Miner ID database error checking miner reputation: %s\n", e.what());
    }

    return false;
}

// Does the miner identified with the given miner ID have a good reputation?
bool MinerHasGoodReputation(const MinerIdDatabase& db, const CPubKey& id)
{
    try
    {
        // Check reputation
        return db.CheckMinerReputation(id.GetHash());
    }
    catch(const std::exception& e)
    {
        LogPrint(BCLog::MINERID, "Miner ID database error checking miner reputation: %s\n", e.what());
    }

    return false;
}


MinerIdDatabase::MinerIdDatabase(const Config& config)
: mConfig{config}
{
    // Set path and cache size
    const fs::path dbPath { GetDataDir() / "miner_id/MinerIdDB" };
    uint64_t cacheSize { 1 << 20 };

    mDBWrapper = std::make_unique<CDBWrapper>(dbPath, cacheSize);

    // Ensure we're upto date with the chain tip
    UpdateToTip(mDBWrapper->IsEmpty());
}

// A new block has been added to the tip
void MinerIdDatabase::BlockAdded(const CBlock& block, int32_t height)
{
    if(block.vtx[0])
    {
        try
        {
            std::lock_guard lock {mMtx};
            BlockAddedNL(block.GetHash(), height, *(block.vtx[0]));
        }
        catch(const std::exception& e)
        {
            LogPrint(BCLog::MINERID, "Miner ID database error processing new block: %s\n", e.what());
        }
    }
}

// A block has been removed from the tip
void MinerIdDatabase::BlockRemoved(const CBlock& block)
{
    std::lock_guard lock {mMtx};

    // Remove block from recent block list
    mLastBlocksTable.get<TagBlockId>().erase(block.GetHash());
}

// An invalid block has been received.
void MinerIdDatabase::InvalidBlock(const CBlock& block, int32_t height)
{
    // Lookup details for miner that produced this bad block
    if(block.vtx[0])
    {
        try
        {
            std::lock_guard lock {mMtx};
            std::optional<MinerUUId> minerUUId { GetMinerForBlockNL(height, *(block.vtx[0])) };
            if(minerUUId)
            {
                // Void this miners reputation
                auto entry { GetMinerUUIdEntryNL(*minerUUId) };
                if(entry.first)
                {
                    entry.second.mReputationVoid = true;
                    UpdateMinerUUIdEntryNL(*minerUUId, entry.second);
                    LogPrint(BCLog::MINERID, "Miner ID database voided reputation of miner %s due to invalid block\n",
                        boost::uuids::to_string(*minerUUId));
                }
            }
        }
        catch(const std::exception& e)
        {
            LogPrint(BCLog::MINERID, "Miner ID database error processing invalid block: %s\n", e.what());
        }
    }
}

// Check a miners reputation
bool MinerIdDatabase::CheckMinerReputation(const uint256& idHash) const
{
    std::lock_guard lock {mMtx};

    const auto& minerIdEntry { GetMinerIdEntryNL(idHash) };
    if(minerIdEntry.first)
    {
        // Lookup miner
        const MinerUUId& uuid { minerIdEntry.second.mUUId };
        const auto& minerEntry { GetMinerUUIdEntryNL(uuid) };
        if(minerEntry.first)
        {
            // Reputation void?
            if(minerEntry.second.mReputationVoid)
            {
                return false;
            }

            // Have they produced M of the last N blocks?
            return GetNumRecentBlocksForMinerNL(uuid) >= mConfig.GetMinerIdReputationM();
        }
        else
        {
            // Shouldn't ever happen
            throw std::runtime_error("Possible miner ID database corruption; Failed to lookup miner for UUID " +
                boost::uuids::to_string(uuid));
        }
    }

    return false;
}

// Dump our contents out in JSON format.
UniValue MinerIdDatabase::DumpJSON() const
{
    UniValue result { UniValue::VARR };

    std::lock_guard lock {mMtx};

    // Dump miner details
    for(const auto& [ key, value ] : GetAllMinerUUIdsNL())
    {
        UniValue miner { UniValue::VOBJ };
        miner.push_back(Pair("uuid", boost::uuids::to_string(key)));

        // Lookup name from miner contact details
        const auto& minerIdEntry { GetMinerIdEntryNL(value.mLatestMinerId) };
        if(minerIdEntry.first)
        {
            const auto& minerContact { minerIdEntry.second.mCoinbaseDoc.GetMinerContact() };
            if(minerContact)
            {
                const auto& name { (*minerContact)["name"] };
                if(name.isStr())
                {
                    miner.push_back(Pair("name", name.get_str()));
                }
            }
        }

        // Other fields for miner
        miner.push_back(Pair("firstblock", value.mFirstBlock.ToString()));
        miner.push_back(Pair("latestblock", value.mLastBlock.ToString()));

        std::stringstream numBlocksStr {};
        numBlocksStr << GetNumRecentBlocksForMinerNL(key) << "/" << mConfig.GetMinerIdReputationN();
        miner.push_back(Pair("numrecentblocks", numBlocksStr.str()));
        miner.push_back(Pair("reputationvoid", value.mReputationVoid));

        // Get and dump all ids for this miner
        UniValue ids { UniValue::VARR };
        std::vector<MinerIdEntry> minerIds { GetMinerIdsForMinerNL(key) };
        for(const auto& minerId : minerIds)
        {
            UniValue id { UniValue::VOBJ };
            id.push_back(Pair("minerid", HexStr(minerId.mPubKey)));
            id.push_back(Pair("current", minerId.mCurrent));
            if(minerId.mRotationBlock != uint256{})
            {
                id.push_back(Pair("rotationblock", minerId.mRotationBlock.ToString()));
            }
            else
            {
                id.push_back(Pair("rotationblock", UniValue { UniValue::VNULL }));
            }
            ids.push_back(id);
        }
        miner.push_back(Pair("minerids", ids));

        result.push_back(miner);
    }

    return result;
}

// Check we agree with the chain tip, and if not rebuild ourselves from scratch
void MinerIdDatabase::UpdateToTip(bool rebuild)
{
    // We don't want the main chain changing until we've finished scanning it
    LOCK(cs_main);

    std::lock_guard lock {mMtx};

    // Clear state
    mLastBlocksTable.clear();

    // Calculate starting block height to scan from so that we have all the history we need
    int32_t startHeight { chainActive.Height() - static_cast<int32_t>(mConfig.GetMinerIdReputationN()) };
    startHeight = std::max(0, startHeight);

    // Iterate from starting point to current tip
    int64_t startTime { GetTimeMillis() };
    CBlockIndex* pindex { chainActive[startHeight] };
    while(pindex != nullptr)
    {
        auto blockReader { pindex->GetDiskBlockStreamReader() };
        if(blockReader)
        {
            const uint256& hash { pindex->GetBlockHash() };
            int32_t height { pindex->GetHeight() };

            // Fetch coinbase
            const CTransaction& coinbase { blockReader->ReadTransaction() };

            // If we're rebuilding, update all the miner ID tables
            if(rebuild)
            {
                BlockAddedNL(hash, height, coinbase);
            }
            else
            {
                // Just populate recent blocks list
                std::optional<MinerUUId> minerUUId { GetMinerForBlockNL(height, coinbase) };
                if(!minerUUId)
                {
                    minerUUId = boost::uuids::nil_uuid();
                }
                mLastBlocksTable.get<TagBlockId>().insert( { hash, height, *minerUUId });
            }
        }

        // Next block
        pindex = chainActive.Next(pindex);
    }

    LogPrint(BCLog::BENCH, "Miner ID database load completed in %dms\n", GetTimeMillis() - startTime);

    if(rebuild)
    {
        mStatus.mRebuiltFromBlockchain = true;
    }
}

// Lookup miner that produced the block with the given coinbase txn (if known)
std::optional<MinerIdDatabase::MinerUUId> MinerIdDatabase::GetMinerForBlockNL(
    int32_t height,
    const CTransaction& coinbase) const
{
    try
    {
        // Look for miner ID in coinbase
        std::optional<MinerId> minerID { FindMinerId(coinbase, height) };
        if(minerID)
        {
            // Who mined using this miner ID?
            const CoinbaseDocument& cbDoc { minerID->GetCoinbaseDocument() };
            const CPubKey& curMinerId { cbDoc.GetMinerIdAsKey() };
            const auto& entry { GetMinerIdEntryNL(curMinerId.GetHash()) };
            if(entry.first)
            {
                return entry.second.mUUId;
            }
        }
    }
    catch(const std::exception& e)
    {
        LogPrint(BCLog::MINERID, "Error looking up miner for block: %s\n", e.what());
    }

    // Unknown
    return {};
}

// Lookup miner ID details in the database for the given key
std::pair<bool, MinerIdDatabase::MinerIdEntry> MinerIdDatabase::GetMinerIdEntryNL(const uint256& minerId) const
{
    const auto& key { std::make_pair(DB_MINER_ID, minerId) };

    if(mDBWrapper->Exists(key))
    {
        MinerIdEntry entry {};
        if(mDBWrapper->Read(key, entry))
        {
            return { true, entry };
        }
        else
        {
            throw std::runtime_error("Failed to read Miner ID " + minerId.ToString() + " from DB");
        }
    }
    else
    {
        return { false, {} };
    }
}

// Add or update the given miner ID details in the database
void MinerIdDatabase::UpdateMinerIdEntryNL(const uint256& key, const MinerIdEntry& entry)
{
    if(! mDBWrapper->Write(std::make_pair(DB_MINER_ID, key), entry, true))
    {
        throw std::runtime_error("Failed to add/update Miner ID to DB");
    }
}

// Lookup miner UUID details in the database for the given key
std::pair<bool, MinerIdDatabase::MinerUUIdEntry> MinerIdDatabase::GetMinerUUIdEntryNL(const MinerUUId& uuid) const
{
    const auto& key { std::make_pair(DB_MINER, uuid) };

    if(mDBWrapper->Exists(key))
    {
        MinerUUIdEntry entry {};
        if(mDBWrapper->Read(key, entry))
        {
            return { true, entry };
        }
        else
        {
            throw std::runtime_error("Failed to read Miner UUID " + boost::uuids::to_string(uuid) + " from DB");
        }
    }
    else
    {
        return { false, {} };
    }
}

// Add or update the given miner UUID details in the database
void MinerIdDatabase::UpdateMinerUUIdEntryNL(const MinerUUId& key, const MinerUUIdEntry& entry)
{
    if(! mDBWrapper->Write(std::make_pair(DB_MINER, key), entry, true))
    {
        throw std::runtime_error("Failed to add/update Miner UUID to DB");
    }
}

// Fetch all miner IDs from the database
std::unordered_map<uint256, MinerIdDatabase::MinerIdEntry> MinerIdDatabase::GetAllMinerIdsNL() const
{
    std::unordered_map<uint256, MinerIdEntry> result {};

    std::unique_ptr<CDBIterator> iter { mDBWrapper->NewIterator() };
    iter->SeekToFirst();
    for(; iter->Valid(); iter->Next())
    {
        // Fetch next key of the correct type
        auto key { std::make_pair(DB_MINER_ID, uint256{}) };
        if(iter->GetKey(key))
        {
            // Fetch entry for this key
            const auto& entry { GetMinerIdEntryNL(key.second) };
            if(entry.first)
            {
                result.emplace(std::make_pair(key.second, entry.second));
            }
        }
    }

    return result;
}

// Fetch all miner UUIDs from the database
MinerIdDatabase::MinerUUIdMap MinerIdDatabase::GetAllMinerUUIdsNL() const
{
    MinerUUIdMap result {};

    std::unique_ptr<CDBIterator> iter { mDBWrapper->NewIterator() };
    iter->SeekToFirst();
    for(; iter->Valid(); iter->Next())
    {
        // Fetch next key of the correct type
        auto key { std::make_pair(DB_MINER, MinerUUId{}) };
        if(iter->GetKey(key))
        {
            // Fetch entry for this key
            const auto& entry { GetMinerUUIdEntryNL(key.second) };
            if(entry.first)
            {
                result.emplace(std::make_pair(key.second, entry.second));
            }
        }
    }

    return result;
}

// Update miner ID details from coinbase for newly added block
void MinerIdDatabase::BlockAddedNL(const uint256& blockhash, int32_t height, const CTransaction& coinbase)
{
    // Somewhere to remember miner UUID for this block
    MinerUUId minerUUId { boost::uuids::nil_uuid() };

    try
    {
        // Look for a miner ID in the coinbase
        std::optional<MinerId> minerID { FindMinerId(coinbase, height) };
        if(minerID)
        {
            LogPrint(BCLog::MINERID, "Miner ID found in block %s at height %d\n", blockhash.ToString(), height);

            // Convert current and previous IDs to public keys
            const CoinbaseDocument& cbDoc { minerID->GetCoinbaseDocument() };
            const CPubKey& curMinerId { cbDoc.GetMinerIdAsKey() };
            const CPubKey& prevMinerId { cbDoc.GetPrevMinerIdAsKey() };

            // Lambda to create entries for a new miner
            auto AddNewMiner = [&blockhash, &curMinerId, &cbDoc, this]()
            {
                // Key for new miner ID
                const uint256& minerIdHash { curMinerId.GetHash() };

                // Create new entry for this miner and their ID
                MinerUUId newMinerUUId { mUUIdGenerator() };
                UpdateMinerUUIdEntryNL(newMinerUUId, MinerUUIdEntry { false, blockhash, blockhash, minerIdHash });
                UpdateMinerIdEntryNL(minerIdHash, MinerIdEntry { newMinerUUId, curMinerId, true, {}, cbDoc });

                LogPrint(BCLog::MINERID, "Created new miner UUID entry (%s) for previously unknown miner\n",
                    boost::uuids::to_string(newMinerUUId));

                return newMinerUUId;
            };

            // Current and previous miner IDs the same either means a new ID or a continuation of an existing ID
            if(curMinerId == prevMinerId)
            {
                // If we've no record of this miner ID, it must be a new one
                const uint256& idhash { curMinerId.GetHash() };
                auto minerIdEntry { GetMinerIdEntryNL(idhash) };
                if(! minerIdEntry.first)
                {
                    // Create new entry for this miner and their ID
                    minerUUId = AddNewMiner();
                }
                else
                {
                    // Update coinbase doc for this miner ID to be this latest version
                    minerIdEntry.second.mCoinbaseDoc = cbDoc;
                    UpdateMinerIdEntryNL(idhash, minerIdEntry.second);

                    // Update existing miner details
                    minerUUId = minerIdEntry.second.mUUId;
                    auto minerUUIdEntry { GetMinerUUIdEntryNL(minerUUId) };
                    if(minerUUIdEntry.first)
                    {
                        // Update last seen block from this miner
                        minerUUIdEntry.second.mLastBlock = blockhash;
                        UpdateMinerUUIdEntryNL(minerUUId, minerUUIdEntry.second);
                        LogPrint(BCLog::MINERID, "Updated miner ID details for miner UUID %s\n", boost::uuids::to_string(minerUUId));
                    }
                    else
                    {
                        // Should never happen
                        throw std::runtime_error("Possible miner ID database corruption; Failed to lookup miner for UUID " +
                            boost::uuids::to_string(minerUUId));
                    }
                }
            }
            else
            {
                // Different current and previous IDs means a key rotation has occurred
                // TODO: VCTX checks for key rotation
                const uint256& prevMinerIdHash { prevMinerId.GetHash() };
                auto prevMinerIdEntry { GetMinerIdEntryNL(prevMinerIdHash) };
                if(prevMinerIdEntry.first)
                {
                    // Update entry for previous ID to flag it as rotated
                    prevMinerIdEntry.second.mCurrent = false;
                    prevMinerIdEntry.second.mRotationBlock = blockhash;
                    UpdateMinerIdEntryNL(prevMinerIdHash, prevMinerIdEntry.second);

                    // Create new entry for this miner ID and link it to the existing miner UUID
                    minerUUId = prevMinerIdEntry.second.mUUId;
                    const uint256& minerIdHash { curMinerId.GetHash() };
                    UpdateMinerIdEntryNL(minerIdHash, MinerIdEntry { minerUUId, curMinerId, true, {}, cbDoc });

                    // Update details for this miner
                    auto minerUUIdEntry { GetMinerUUIdEntryNL(minerUUId) };
                    if(minerUUIdEntry.first)
                    {
                        minerUUIdEntry.second.mLastBlock = blockhash;
                        minerUUIdEntry.second.mLatestMinerId = minerIdHash;
                        UpdateMinerUUIdEntryNL(minerUUId, minerUUIdEntry.second);
                        LogPrint(BCLog::MINERID, "Rotated miner ID key for miner UUID %s\n", boost::uuids::to_string(minerUUId));
                    }
                    else
                    {
                        // Should never happen
                        throw std::runtime_error("Possible miner ID database corruption; Failed to lookup miner for UUID " +
                            boost::uuids::to_string(minerUUId));
                    }
                }
                else
                {
                    // Couldn't find previous miner ID, so treat it as a new one
                    minerUUId = AddNewMiner();
                }
            }
        }
    }
    catch(const std::exception& e)
    {
        LogPrint(BCLog::MINERID, "Error processing new block: %s\n", e.what());
    }

    // Record details for this block in recent blocks list and expire any old entries
    mLastBlocksTable.get<TagBlockId>().insert( { blockhash, height, minerUUId } );
    auto& index { mLastBlocksTable.get<TagBlockHeight>() };
    while(index.size() > mConfig.GetMinerIdReputationN())
    {
        index.erase(index.begin());
    }
}

// Lookup all miner IDs for the given miner and return them sorted by age
std::vector<MinerIdDatabase::MinerIdEntry> MinerIdDatabase::GetMinerIdsForMinerNL(const MinerUUId& miner) const
{
    std::vector<MinerIdEntry> minerIds {};

    // Pull out miner IDs for this miner
    for(const auto& minerId : GetAllMinerIdsNL())
    {
        if(minerId.second.mUUId == miner)
        {
            minerIds.push_back(minerId.second);
        }
    }

    // Sort the ids based on rotation block height (current -> oldest)
    auto IdSort = [](const MinerIdEntry& id1, const MinerIdEntry& id2)
    {
        // Current entries should come first
        if(id1.mCurrent)
        {
            // id1 < id2
            return true;
        }
        else if(id2.mCurrent)
        {
            // id2 < id1
            return false;
        }

        // Sort non-current items according to the block height they were rotated out
        CBlockIndex* id1BlockIndex { mapBlockIndex.Get(id1.mRotationBlock) };
        CBlockIndex* id2BlockIndex { mapBlockIndex.Get(id2.mRotationBlock) };
        if(id1BlockIndex && id2BlockIndex)
        {
            // We use > because we want to sort on descending height
            return id1BlockIndex->GetHeight() > id2BlockIndex->GetHeight();
        }

        // Not current but hasn't been rotated yet, put at the top of the list
        return ! id1BlockIndex;
    };
    std::sort(minerIds.begin(), minerIds.end(), IdSort);

    return minerIds;
}

// Prune old data from the database
void MinerIdDatabase::Prune()
{
    LogPrint(BCLog::MINERID, "Miner ID database pruning\n");

    // Remove any old miner IDs (non-current, rotated) beyond a configured number to keep
    size_t numIdsToKeep { mConfig.GetMinerIdsNumToKeep() };

    std::unique_lock lock {mMtx};

    for(const auto& miner : GetAllMinerUUIdsNL())
    {
        std::vector<MinerIdEntry> minerIds { GetMinerIdsForMinerNL(miner.first) };
        while(minerIds.size() > numIdsToKeep)
        {
            // Sanity check we're not going to prune a current ID (we shouldn't be
            // since there should only ever be one current ID at any time).
            if(! minerIds.back().mCurrent)
            {
                // Prune this ID from the database
                const uint256& minerIdHash { minerIds.back().mPubKey.GetHash() };
                const auto& key { std::make_pair(DB_MINER_ID, minerIdHash) };
                mDBWrapper->Erase(key, true);

                minerIds.pop_back();
            }
            else
            {
                throw std::runtime_error("Oldest miner ID for miner " + boost::uuids::to_string(miner.first) + 
                    " is still marked as current while pruning miner ID database");
            }
        }
    }
}

// Get number of blocks in the recent blocks list from a miner
size_t MinerIdDatabase::GetNumRecentBlocksForMinerNL(const MinerUUId& miner) const
{
    const auto& range { mLastBlocksTable.get<TagMinerUUId>().equal_range(miner) };
    return static_cast<size_t>(std::distance(range.first, range.second));
}

