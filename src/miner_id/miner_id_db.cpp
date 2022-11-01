// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "miner_id/miner_id_db.h"

#include "block_index_store.h"
#include "blockstreams.h"
#include "config.h"
#include "logging.h"
#include "merkleproof.h"
#include "merkletreestore.h"
#include "miner_id/miner_id.h"
#include "miner_id/miner_info_tracker.h"
#include "miner_id/revokemid.h"
#include "miner_id/dataref_index.h"
#include "scheduler.h"
#include "univalue.h"

#include <algorithm>

#include <boost/uuid/uuid_io.hpp>

std::unique_ptr<MinerIdDatabase> g_minerIDs {nullptr};

namespace
{
    // How frequently we run the database pruning (once per day)
    constexpr int64_t PRUNE_PERIOD_SECS { 60 * 60 * 24 };

    // Check if first block index is an ancestor of the second
    bool BlockIsAncestor(const CBlockIndex& parent, const CBlockIndex& child)
    {
        const CBlockIndex* ancestor { child.GetAncestor(parent.GetHeight()) };
        return (ancestor && ancestor->GetBlockHash() == parent.GetBlockHash());
    }
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

// Get a miners coinbase document information.
std::optional<std::pair<CoinbaseDocument, std::string>> GetMinerCoinbaseDocInfo(const MinerIdDatabase& db, const CPubKey& id)
{
    try
    {
        // Get coinbase doc.
        return db.GetMinerCoinbaseDocInfo(id.GetHash());
    }
    catch(const std::exception& e)
    {
        LogPrint(BCLog::MINERID, "Miner ID database error getting miner document info: %s\n", e.what());
    }

    return {};
}


// Constructor
MinerIdDatabase::MinerIdDatabase(const Config& config)
: mConfig{config}
{
    // Try to open database if it exists
    OpenDatabaseNL();

    // Populate recent blocks list from database
    ReadAllRecentBlocksFromDatabaseNL();

    // Fetch database state if we have it
    const auto& dbState { GetDatabaseStateNL() };
    if(dbState)
    {
        mDBState = dbState.value();
    }
    else
    {
        LogPrint(BCLog::MINERID, "Miner ID database unable to read state; will need to rebuild from scratch\n");
    }

    // Ensure we're upto date with the chain tip (run in background)
    TriggerSync(false, false);
}

// Trigger a database sync to the blockchain
void MinerIdDatabase::TriggerSync(bool fromscratch, bool fromGenesis)
{
    std::lock_guard lock {mMtx};

    // Check we don't already have a sync running
    using namespace std::chrono_literals;
    if(mFuture.valid() && mFuture.wait_for(0s) == std::future_status::timeout)
    {
        LogPrint(BCLog::MINERID, "Miner ID database sync already running; can't start another\n");
        throw std::runtime_error("Miner ID database sync already running; can't start another");
    }

    if(fromscratch)
    {
        // Wipe database
        OpenDatabaseNL(true);

        // Clear state
        mLastBlocksTable.clear();
        mDBState = {};
    }

    // Run in background
    mPromise = {};
    mFuture = std::async(std::launch::async, &MinerIdDatabase::UpdateToTip, this, fromGenesis);
}

// Destructor
MinerIdDatabase::~MinerIdDatabase()
{
    // Tell background sync to finish (if it hasn't already)
    {
        std::lock_guard lock {mMtx};
        mPromise.set_value();
    }

    // Wait for worker to finish
    mFuture.wait();
}

// A new block has been added to the tip
void MinerIdDatabase::BlockAdded(const CBlock& block, CBlockIndex const * pindex)
{
    try
    {
        std::lock_guard lock {mMtx};

        // If we've finished syncing
        if(mDBState.mSynced)
        {
            BlockAddedNL(block, pindex);
        }
    }
    catch(const std::exception& e)
    {
        LogPrint(BCLog::MINERID, "Miner ID database error processing new block: %s\n", e.what());
    }
}

// A block has been removed from the tip
void MinerIdDatabase::BlockRemoved(const CBlock& block)
{
    try
    {
        std::lock_guard lock {mMtx};

        // If we've finished syncing
        if(mDBState.mSynced)
        {
            // Remove block from recent block list
            const uint256& hash { block.GetHash() };
            RemoveRecentBlockEntryNL(hash);

            // Update best block hash in state
            SetBestBlockNL(chainActive.Tip()->GetBlockHash());
        }
    }
    catch(const std::exception& e)
    {
        LogPrint(BCLog::MINERID, "Miner ID database error removing block: %s\n", e.what());
    }
}

// An invalid block has been received.
void MinerIdDatabase::InvalidBlock(const CBlock& block, int32_t height)
{
    // Lookup details for miner that produced this bad block
    try
    {
        std::lock_guard lock {mMtx};
        const std::optional<MinerIdEntry>& minerId { GetMinerIdForBlockNL(block, height) };
        if(minerId)
        {
            // Only pay attention to blocks using the current ID for this miner
            if(minerId->mState == MinerIdEntry::State::CURRENT)
            {
                // Void this miners reputation (if not already voided)
                auto entry { GetMinerUUIdFromDatabaseNL(minerId->mUUId) };
                if(entry && ! entry->mReputation.mVoid)
                {
                    entry->mReputation.mVoid = true;
                    entry->mReputation.mVoidingId = minerId->mPubKey;
                    UpdateMinerUUIdInDatabaseNL(minerId->mUUId, entry.value());
                    LogPrint(BCLog::MINERID, "Miner ID database voided reputation of miner %s due to invalid block\n",
                        boost::uuids::to_string(minerId->mUUId));
                }
            }
            else
            {
                LogPrint(BCLog::MINERID, "Ignoring invalid block that came from %s miner ID %s\n",
                    enum_cast<std::string>(minerId->mState), HexStr(minerId->mPubKey));
            }
        }
    }
    catch(const std::exception& e)
    {
        LogPrint(BCLog::MINERID, "Miner ID database error processing invalid block: %s\n", e.what());
    }
}

// Process a revokemid P2P message
void MinerIdDatabase::ProcessRevokemidMessage(const RevokeMid& msg)
{
    LogPrint(BCLog::MINERID, "Processing revokemid message from miner ID %s\n", HexStr(msg.GetMinerId()));

    // Verify message signatures
    if(msg.VerifySignatures())
    {
        std::lock_guard lock {mMtx};

        // Lookup ID this message is from
        const auto& minerIdEntry { GetMinerIdFromDatabaseNL(msg.GetMinerId().GetHash()) };
        if(minerIdEntry)
        {
            // Check revocation key from message is current
            if(minerIdEntry->mCoinbaseDoc.GetRevocationKey() != msg.GetRevocationKey())
            {
                throw std::runtime_error("Revokemid for miner ID " + HexStr(msg.GetMinerId()) +
                    " contains wrong revocation key " + HexStr(msg.GetRevocationKey()));
            }

            // Revoke IDs back to the one given in the revocation message
            for(auto& minerId : GetMinerIdsForMinerNL(minerIdEntry->mUUId))
            {
                minerId.mState = MinerIdEntry::State::REVOKED;
                UpdateMinerIdInDatabaseNL(minerId.mPubKey.GetHash(), minerId);
                UpdateRecentBlocksToRemoveMinerIdNL(minerId.mPubKey.GetHash());

                if(minerId.mPubKey == msg.GetRevocationMessage())
                {
                    break;
                }
            }
        }
        else
        {
            throw std::runtime_error("Revokemid contains unknown miner ID " + HexStr(msg.GetMinerId()));
        }
    }
    else
    {
        throw std::runtime_error("Revokemid signature verification failed");
    }
}

// Check a miners reputation
bool MinerIdDatabase::CheckMinerReputation(const uint256& idHash) const
{
    std::lock_guard lock {mMtx};

    const auto& minerIdEntry { GetMinerIdFromDatabaseNL(idHash) };
    if(minerIdEntry)
    {
        // Lookup miner
        const MinerUUId& uuid { minerIdEntry->mUUId };
        const auto& minerEntry { GetMinerUUIdFromDatabaseNL(uuid) };
        if(minerEntry)
        {
            // Reputation void?
            if(minerEntry->mReputation.mVoid)
            {
                return false;
            }

            // Have they produced M of the last N blocks?
            return GetNumRecentBlocksForMinerNL(uuid) >= minerEntry->mReputation.mM;
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

// Get a miners coinbase document with its state information.
std::optional<std::pair<CoinbaseDocument, std::string>> MinerIdDatabase::GetMinerCoinbaseDocInfo(const uint256& idHash) const
{
    std::lock_guard lock {mMtx};

    const auto& minerIdEntry { GetMinerIdFromDatabaseNL(idHash) };
    if(minerIdEntry)
    {
        return { std::make_pair(minerIdEntry->mCoinbaseDoc, enum_cast<std::string>(minerIdEntry->mState)) };
    }

    return {};
}

// Dump our contents out in JSON format.
UniValue MinerIdDatabase::DumpJSON() const
{
    UniValue result { UniValue::VOBJ };

    std::lock_guard lock {mMtx};

    // Dump state
    UniValue state { UniValue::VOBJ };
    state.push_back(Pair("dbversion", mDBState.mVersion));
    state.push_back(Pair("synced", mDBState.mSynced));
    state.push_back(Pair("bestblock", mDBState.mBestBlock.ToString()));
    result.push_back(Pair("state", state));

    // Dump miner details
    UniValue miners { UniValue::VARR };
    for(const auto& [ key, value ] : GetAllMinerUUIdsNL())
    {
        UniValue miner { UniValue::VOBJ };
        miner.push_back(Pair("uuid", boost::uuids::to_string(key)));

        // Lookup name from miner contact details
        const auto& minerIdEntry { GetMinerIdFromDatabaseNL(value.mLatestMinerId) };
        if(minerIdEntry)
        {
            const auto& minerContact { minerIdEntry->mCoinbaseDoc.GetMinerContact() };
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

        UniValue reputation { UniValue::VOBJ };
        reputation.push_back(Pair("M", static_cast<uint64_t>(value.mReputation.mM)));
        reputation.push_back(Pair("void", value.mReputation.mVoid));
        if(value.mReputation.mVoidingId)
        {
            reputation.push_back(Pair("voidingid", HexStr(value.mReputation.mVoidingId.value())));
        }
        miner.push_back(Pair("reputation", reputation));

        // Get and dump all ids for this miner
        UniValue ids { UniValue::VARR };
        std::vector<MinerIdEntry> minerIds { GetMinerIdsForMinerNL(key) };
        for(const auto& minerId : minerIds)
        {
            UniValue id { UniValue::VOBJ };
            id.push_back(Pair("minerid", HexStr(minerId.mPubKey)));
            id.push_back(Pair("version", minerId.mCoinbaseDoc.GetVersion()));
            id.push_back(Pair("state", enum_cast<std::string>(minerId.mState)));
            if(minerId.mNextMinerId)
            {
                id.push_back(Pair("nextminerid", HexStr(minerId.mNextMinerId.value())));
            }
            id.push_back(Pair("prevminerid", HexStr(minerId.mPrevMinerId)));
            id.push_back(Pair("creationblock", minerId.mCreationBlock.ToString()));
            ids.push_back(id);
        }
        miner.push_back(Pair("minerids", ids));

        miners.push_back(miner);
    }
    result.push_back(Pair("miners", miners));

    return result;
}

// Synchronise ourselves with the chain tip
void MinerIdDatabase::UpdateToTip(bool syncFromGenesis)
{
    RenameThread("mineridsync");

    // Check for no chain yet
    if(chainActive.Tip() == nullptr)
    {
        return;
    }

    // Lambda to read and process a new block
    auto ReadAndProcessBlock = [this](const CBlockIndex* pindex)
    {
        int64_t startTime { GetTimeMillis() };

        // Check block has a file associated with it (it might have been pruned)
        if(pindex->GetFileNumber() && pindex->GetFileNumber().value() >= 0)
        {
            // Fetch block
            CBlock block {};
            if(pindex->ReadBlockFromDisk(block, mConfig))
            {
                // Process block
                BlockAddedNL(block, pindex);
            }
        }

        // Ensure we always update our best block
        SetBestBlockNL(pindex->GetBlockHash());

        // Return how long we spent in here
        return GetTimeMillis() - startTime;
    };

    const auto future { mPromise.get_future() };
    int64_t startTime { GetTimeMillis() };
    int64_t sleepTime {0};

    while(true)
    {
        // See if we're exiting early
        using namespace std::chrono_literals;
        const auto status { future.wait_for(0s) };
        if(status == std::future_status::ready)
        {
            LogPrint(BCLog::MINERID, "Miner ID database sync aborting\n");
            break;
        }

        // Give other threads a better chance of running by sleeping ourselves
        if(sleepTime)
        {
            // Limit sleep to max of 5 seconds
            constexpr int64_t MAX_SLEEP_MILLIS { 5 * 1000 };
            sleepTime = std::min(sleepTime, MAX_SLEEP_MILLIS);
            std::this_thread::sleep_for(std::chrono::milliseconds { sleepTime });
        }

        // Take cs_main without using the LOCK macro so that we can manually unlock it later
        boost::unique_lock<CCriticalSection> csMainLock { cs_main };
        std::lock_guard lock {mMtx};

        const CBlockIndex* tip { chainActive.Tip() };
        const CBlockIndex* bestblock { mapBlockIndex.Get(mDBState.mBestBlock) };

        if(!mDBState.mSynced && !bestblock)
        {
            // Start from scratch
            mStatus.mRebuiltFromBlockchain = true;

            // Calculate starting block height to scan from so that we have all the history we need
            int32_t startHeight {0};
            if(! syncFromGenesis)
            {
                startHeight = chainActive.Height() - static_cast<int32_t>(mConfig.GetMinerIdReputationN());
                startHeight = std::max(0, startHeight);
            }
            LogPrint(BCLog::MINERID, "Miner ID database starting build from scratch from height %d\n", startHeight);

            // Process first block; will set initial best block in state
            const CBlockIndex* pindex { chainActive[startHeight] };
            csMainLock.unlock();
            sleepTime = ReadAndProcessBlock(pindex);
        }
        else if(tip->GetBlockHash() == mDBState.mBestBlock)
        {
            // Looks like we're synced
            LogPrint(BCLog::MINERID, "Miner ID database synced to blockchain\n");
            SetSyncCompleteNL(true);
            break;
        }
        else if(bestblock && tip && BlockIsAncestor(*bestblock, *tip))
        {
            // Flag we're not synced yet
            SetSyncCompleteNL(false);

            // Process next block after our current best to move towards tip
            bestblock = chainActive.Next(bestblock);
            csMainLock.unlock();
            sleepTime = ReadAndProcessBlock(bestblock);
        }
        else
        {
            // Something's gone wrong, maybe a reorg is ongoing, rebuild from scratch
            LogPrintf("Miner ID database sync hit a problem; rebuilding from scratch\n");

            // Wipe database
            OpenDatabaseNL(true);

            // Clear state
            mLastBlocksTable.clear();
            mDBState = {};
        }
    }

    double loadTime { static_cast<double>(GetTimeMillis() - startTime) };
    LogPrint(BCLog::BENCH, "Miner ID database load completed in %fs\n", loadTime / 1000);
}

// Lookup miner that produced the given block (if known)
std::optional<MinerIdDatabase::MinerIdEntry> MinerIdDatabase::GetMinerIdForBlockNL(
    const CBlock& block,
    int32_t height) const
{
    try
    {
        // Look for miner ID in coinbase
        std::optional<MinerId> minerID { FindMinerId(block, height) };
        if(minerID)
        {
            // Who mined using this miner ID?
            const CoinbaseDocument& cbDoc { minerID->GetCoinbaseDocument() };
            const CPubKey& curMinerId { cbDoc.GetMinerIdAsKey() };
            return GetMinerIdFromDatabaseNL(curMinerId.GetHash());
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
std::optional<MinerIdDatabase::MinerIdEntry> MinerIdDatabase::GetMinerIdFromDatabaseNL(const uint256& minerId) const
{
    const auto& key { std::make_pair(DB_MINER_ID, minerId) };

    if(mDBWrapper->Exists(key))
    {
        MinerIdEntry entry {};
        if(mDBWrapper->Read(key, entry))
        {
            return { entry };
        }
        else
        {
            throw std::runtime_error("Failed to read Miner ID " + minerId.ToString() + " from DB");
        }
    }
    else
    {
        return {};
    }
}

// Add or update the given miner ID details in the database
void MinerIdDatabase::UpdateMinerIdInDatabaseNL(const uint256& key, const MinerIdEntry& entry)
{
    if(! mDBWrapper->Write(std::make_pair(DB_MINER_ID, key), entry))
    {
        throw std::runtime_error("Failed to add/update Miner ID " + key.ToString() + " to DB");
    }
}

// Lookup miner UUID details in the database for the given key
std::optional<MinerIdDatabase::MinerUUIdEntry> MinerIdDatabase::GetMinerUUIdFromDatabaseNL(const MinerUUId& uuid) const
{
    const auto& key { std::make_pair(DB_MINER, uuid) };

    if(mDBWrapper->Exists(key))
    {
        MinerUUIdEntry entry {};
        if(mDBWrapper->Read(key, entry))
        {
            return { entry };
        }
        else
        {
            throw std::runtime_error("Failed to read Miner UUID " + boost::uuids::to_string(uuid) + " from DB");
        }
    }
    else
    {
        return {};
    }
}

// Add or update the given miner UUID details in the database
void MinerIdDatabase::UpdateMinerUUIdInDatabaseNL(const MinerUUId& key, const MinerUUIdEntry& entry)
{
    if(! mDBWrapper->Write(std::make_pair(DB_MINER, key), entry))
    {
        throw std::runtime_error("Failed to add/update Miner UUID " + boost::uuids::to_string(key) + " to DB");
    }
}

// Add recent block details to the database
void MinerIdDatabase::AddRecentBlockToDatabaseNL(const RecentBlock& entry)
{
    if(! mDBWrapper->Write(std::make_pair(DB_RECENT_BLOCKS, entry.mHash), entry))
    {
        throw std::runtime_error("Failed to add recent block " + entry.mHash.ToString() + " to DB");
    }
}

// Remove recent block details from the database
void MinerIdDatabase::RemoveRecentBlockFromDatabaseNL(const uint256& key)
{
    if(! mDBWrapper->Erase(std::make_pair(DB_RECENT_BLOCKS, key), true))
    {
        throw std::runtime_error("Failed to remove recent block " + key.ToString() + " from DB");
    }
}

// Lookup recent block details from the database
std::optional<MinerIdDatabase::RecentBlock> MinerIdDatabase::GetRecentBlockFromDatabaseNL(const uint256& hash) const
{
    const auto& key { std::make_pair(DB_RECENT_BLOCKS, hash) };

    if(mDBWrapper->Exists(key))
    {
        RecentBlock entry {};
        if(mDBWrapper->Read(key, entry))
        {
            return { entry };
        }
        else
        {
            throw std::runtime_error("Failed to read recent block " + hash.ToString() + " from DB");
        }
    }
    else
    {
        return {};
    }
}

// Read all recent block details from the database
void MinerIdDatabase::ReadAllRecentBlocksFromDatabaseNL()
{
    std::unique_ptr<CDBIterator> iter { mDBWrapper->NewIterator() };
    iter->SeekToFirst();
    for(; iter->Valid(); iter->Next())
    {
        // Fetch next key of the correct type
        auto key { std::make_pair(DB_RECENT_BLOCKS, uint256{}) };
        if(iter->GetKey(key))
        {
            // Fetch entry for this key
            const auto& entry { GetRecentBlockFromDatabaseNL(key.second) };
            if(entry)
            {
                // Insert into recent blocks list
                mLastBlocksTable.get<TagBlockHash>().insert(entry.value());
            }
        }
    }
}

// Lookup database state information
std::optional<MinerIdDatabase::DatabaseState> MinerIdDatabase::GetDatabaseStateNL() const
{
    if(mDBWrapper->Exists(DB_STATE))
    {
        DatabaseState state {};
        if(mDBWrapper->Read(DB_STATE, state))
        {
            return { state };
        }
        else
        {
            throw std::runtime_error("Failed to read Miner ID database state from DB");
        }
    }
    else
    {
        return {};
    }
}

// Update database state information
void MinerIdDatabase::UpdateDatabaseStateNL(const DatabaseState& state)
{
    if(! mDBWrapper->Write(DB_STATE, state))
    {
        throw std::runtime_error("Failed to update Miner ID database state to DB");
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
            const auto& entry { GetMinerIdFromDatabaseNL(key.second) };
            if(entry)
            {
                result.emplace(std::make_pair(key.second, entry.value()));
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
            const auto& entry { GetMinerUUIdFromDatabaseNL(key.second) };
            if(entry)
            {
                result.emplace(std::make_pair(key.second, entry.value()));
            }
        }
    }

    return result;
}

// Update miner ID details from coinbase for newly added block
void MinerIdDatabase::BlockAddedNL(const CBlock& block, CBlockIndex const * pindex)
{
    const uint256& blockhash { block.GetHash() };
    const int32_t height = pindex->GetHeight();

    std::function<std::optional<MerkleProof>(TxId const &, uint256 const &)> GetMerkleProof =
        [this, &block, pindex, height](TxId const & txid, uint256 const & blockHash) -> std::optional<MerkleProof>
        {
            CMerkleTreeRef merkleTree = pMerkleTreeFactory->GetMerkleTree(mConfig, *pindex, height);
            if (!merkleTree)
            {
                LogPrint(BCLog::MINERID, strprintf("Can't read block from disk for blockhash%s\n", block.GetHash() ));
                return std::nullopt;
            }
            CMerkleTree::MerkleProof proof = merkleTree->GetMerkleProof(txid, true);
            if (proof.merkleTreeHashes.empty())
            {
                LogPrint(BCLog::MINERID, strprintf("Transaction(s) not found in provided block with hash %s\n", block.GetHash()));
                return std::nullopt;
            }

            return {{proof, txid, blockHash}};
        };


    // Somewhere to remember miner UUID and miner ID for this block
    MinerUUId minerUUId { boost::uuids::nil_uuid() };
    uint256 minerIdHash {};

    try
    {
        // Look for a miner ID in the coinbase
        std::optional<MinerId> minerID { FindMinerId(block, height) };
        if(minerID)
        {
            LogPrint(BCLog::MINERID, "Miner ID found in block %s at height %d\n", blockhash.ToString(), height);

            // Convert current and previous IDs to public keys
            const CoinbaseDocument& cbDoc { minerID->GetCoinbaseDocument() };
            const CPubKey& curMinerId { cbDoc.GetMinerIdAsKey() };
            const CPubKey& prevMinerId { cbDoc.GetPrevMinerIdAsKey() };

            // Add minerinfo to dataref index and to funds tracking
            const std::optional<TxId> & minerInfoTxId = minerID->GetMinerInfoTx();
            if (minerInfoTxId) {
                // add to dataref index
                g_dataRefIndex->ExtractMinerInfoTxnFromBlock (block, *minerInfoTxId, GetMerkleProof);

                // add to funding tracker
                // The last fund in the trackers mempool list is the minerinfo txn
                const auto infotx = g_MempoolDatarefTracker->funds_back();
                if (infotx && infotx->GetTxId() == *minerInfoTxId)
                {
                    move_and_store(*g_MempoolDatarefTracker, *g_BlockDatarefTracker);
                    LogPrint(BCLog::MINERID, "minerinfotx tracker and potential parents, added minerinfo txn %s to block %s\n", infotx->ToString(), blockhash.ToString());
                }
            }

            // Add datarefs to the dataref index
            if (auto datarefs = cbDoc.GetDataRefs(); datarefs && !datarefs->empty())
            {
                g_dataRefIndex->ExtractDatarefTxnsFromBlock (block, *datarefs, GetMerkleProof);
            }

            // Check revocation keys
            CheckRevocationKeysNL(prevMinerId, cbDoc);

            // Are we performing a revocation?
            if(cbDoc.GetRevocationMessage())
            {
                const auto& [ uuid, idhash ] { ProcessRevocationNL(curMinerId, prevMinerId, cbDoc, blockhash) };
                minerUUId = uuid;
                minerIdHash = idhash;
            }
            else
            {
                minerIdHash = curMinerId.GetHash();

                // Current and previous miner IDs the same either means a new ID or a continuation of an existing ID
                if(curMinerId == prevMinerId)
                {
                    // If we've no record of this miner ID, it must be a new one
                    auto minerIdEntry { GetMinerIdFromDatabaseNL(minerIdHash) };
                    if(! minerIdEntry)
                    {
                        // Create new entry for this miner and their ID
                        minerUUId = ProcessNewMinerNL(minerIdHash, cbDoc, blockhash);
                    }
                    else
                    {
                        // Process reuse of existing miner ID
                        minerUUId = ProcessExistingMinerIdNL(cbDoc, blockhash, minerIdEntry.value());
                    }
                }
                else
                {
                    // Different current and previous IDs means a key rotation has occurred
                    minerUUId = ProcessRotationNL(curMinerId, prevMinerId, cbDoc, blockhash);
                }
            }
        }
    }
    catch(const std::exception& e)
    {
        LogPrint(BCLog::MINERID, "Miner ID error processing new block (%s): %s\n", blockhash.ToString(), e.what());
    }

    // Record details for this block in recent blocks list
    AddRecentBlockEntryNL( { blockhash, height, minerUUId, minerIdHash } );

    // Update best block hash in state
    SetBestBlockNL(blockhash);
}

// Process a miner ID from a brand new miner
MinerIdDatabase::MinerUUId MinerIdDatabase::ProcessNewMinerNL(
    const uint256& minerIdHash,
    const CoinbaseDocument& cbDoc,
    const uint256& blockhash)
{
    // Create new entry for this miner and their ID
    MinerUUId newMinerUUId { mUUIdGenerator() };
    const MinerReputation rep { mConfig.GetMinerIdReputationM(), false };
    UpdateMinerUUIdInDatabaseNL(newMinerUUId, MinerUUIdEntry { rep, blockhash, blockhash, minerIdHash });
    UpdateMinerIdInDatabaseNL(minerIdHash, MinerIdEntry { newMinerUUId, blockhash, cbDoc });

    LogPrint(BCLog::MINERID, "Created new miner UUID entry (%s) for previously unknown miner\n",
        boost::uuids::to_string(newMinerUUId));

    return newMinerUUId;
}

// Process a reuse of an existing miner ID
MinerIdDatabase::MinerUUId MinerIdDatabase::ProcessExistingMinerIdNL(
    const CoinbaseDocument& cbDoc,
    const uint256& blockhash,
    MinerIdEntry& minerIdEntry)
{
    // Check miner ID is CURRENT
    if(minerIdEntry.mState == MinerIdEntry::State::CURRENT)
    {
        // Check to see if this is a recreation of an ID from a different fork
        const CBlockIndex* parent { mapBlockIndex.Get(minerIdEntry.mCreationBlock) };
        const CBlockIndex* child { mapBlockIndex.Get(blockhash) };
        if(!parent || !child || !BlockIsAncestor(*parent, *child))
        {
            // Update the creation block for this ID to match this one from the main chain
            minerIdEntry.mCreationBlock = blockhash;
        }

        // Update coinbase doc for this miner ID to be this latest version
        minerIdEntry.mCoinbaseDoc = cbDoc;
        UpdateMinerIdInDatabaseNL(minerIdEntry.mPubKey.GetHash(), minerIdEntry);

        // Update existing miner details
        auto minerUUIdEntry { GetMinerUUIdFromDatabaseNL(minerIdEntry.mUUId) };
        if(minerUUIdEntry)
        {
            // Update last seen block from this miner
            minerUUIdEntry->mLastBlock = blockhash;
            UpdateMinerUUIdInDatabaseNL(minerIdEntry.mUUId, minerUUIdEntry.value());
            LogPrint(BCLog::MINERID, "Updated miner ID details for miner UUID %s\n",
                boost::uuids::to_string(minerIdEntry.mUUId));
        }
        else
        {
            // Should never happen
            throw std::runtime_error("Possible miner ID database corruption; Failed to lookup miner for UUID " +
                boost::uuids::to_string(minerIdEntry.mUUId));
        }
    }
    else
    {
        throw std::runtime_error("Rejecting attempt to reuse " + enum_cast<std::string>(minerIdEntry.mState) +
            " miner ID " + HexStr(minerIdEntry.mPubKey));
    }

    return minerIdEntry.mUUId;
}

// Process and apply a miner ID key roll
MinerIdDatabase::MinerUUId MinerIdDatabase::ProcessRotationNL(
    const CPubKey& curMinerId,
    const CPubKey& prevMinerId,
    const CoinbaseDocument& cbDoc,
    const uint256& blockhash)
{
    // Miner UUID this is for
    MinerUUId minerUUId { boost::uuids::nil_uuid() };

    // Lookup details for miner ID we are rotating away from
    const uint256& prevIdHash { prevMinerId.GetHash() };
    auto prevMinerIdEntry { GetMinerIdFromDatabaseNL(prevIdHash) };
    if(prevMinerIdEntry)
    {
        // Check either previous miner ID is CURRENT, or this is a duplicate
        // rotation from a fork.
        const uint256& curIdHash { curMinerId.GetHash() };
        const auto& currMinerIdEntry { GetMinerIdFromDatabaseNL(curIdHash) };
        if(prevMinerIdEntry->mState == MinerIdEntry::State::CURRENT ||
          (currMinerIdEntry && currMinerIdEntry->mState == MinerIdEntry::State::CURRENT &&
           prevMinerIdEntry->mState == MinerIdEntry::State::ROTATED))
        {
            // Update entry for previous ID to flag it as rotated (if it wasn't already)
            prevMinerIdEntry->mState = MinerIdEntry::State::ROTATED;
            prevMinerIdEntry->mNextMinerId = curMinerId;
            UpdateMinerIdInDatabaseNL(prevIdHash, prevMinerIdEntry.value());

            // Create new or update existing entry for this miner ID and link it to the existing miner UUID
            minerUUId = prevMinerIdEntry->mUUId;
            UpdateMinerIdInDatabaseNL(curIdHash, MinerIdEntry { minerUUId, blockhash, cbDoc });

            // Update details for this miner
            UpdateMinerDetailsAfterRotationNL(minerUUId, curIdHash, blockhash);
        }
        else
        {
            throw std::runtime_error("Rejecting attempt to re-rotate " + enum_cast<std::string>(prevMinerIdEntry->mState) +
                " miner ID " + HexStr(curMinerId));
        }
    }
    else
    {
        // Ignore rotation for unknown miner ID; wait until next non-rotation
        // block arrives for this miner and we'll create a new entry for them then.
        LogPrint(BCLog::MINERID, "Ignoring rotation of unknown miner ID %s\n", HexStr(prevMinerId));
    }

    return minerUUId;
}

// Process and apply a miner ID key revocation
std::pair<MinerIdDatabase::MinerUUId, uint256> MinerIdDatabase::ProcessRevocationNL(
    const CPubKey& curMinerId,
    const CPubKey& prevMinerId,
    const CoinbaseDocument& cbDoc,
    const uint256& blockhash)
{
    // Get revocation message and key from the coinbase doc
    const CoinbaseDocument::RevocationMessage& revocationMessage { cbDoc.GetRevocationMessage().value() };
    const CPubKey& revocationKey { cbDoc.GetRevocationKey() };

    // We don't allow a revocation key roll at the same time as a revocation
    if(revocationKey != cbDoc.GetPrevRevocationKey())
    {
        throw std::runtime_error("Attempt to roll revocation key during a revocation");
    }

    const uint256& curIdHash { curMinerId.GetHash() };
    const uint256& prevIdHash { prevMinerId.GetHash() };

    // Partial revocation or duplicate partial revocation?
    const auto [partialRevocation, duplicate] = IsPartialDuplicateRevocationNL(curIdHash, prevIdHash);
    if(partialRevocation)
    {
        const auto& prevMinerIdEntry { GetMinerIdFromDatabaseNL(prevIdHash) };
        MinerUUId minerUUId { prevMinerIdEntry->mUUId };
        auto minerEntry { GetMinerUUIdFromDatabaseNL(minerUUId) };

        // We're doing a partial revocation, so revoke all IDs starting from the latest
        // current ID back to the one in the revocation message.
        std::optional<MinerIdEntry> lastRevokedId { std::nullopt };
        for(auto& minerId : GetMinerIdsForMinerNL(minerUUId))
        {
            lastRevokedId = minerId;

            // Revoke this ID and remove any blocks from the revoked ID from the recent blocks list,
            // unless this is a duplicate, in which case we've already done so and doing it again
            // will result in us incorerctly revoking the miner's new ID
            if(! duplicate)
            {
                minerId.mState = MinerIdEntry::State::REVOKED;
                UpdateMinerIdInDatabaseNL(minerId.mPubKey.GetHash(), minerId);
                UpdateRecentBlocksToRemoveMinerIdNL(minerId.mPubKey.GetHash());
            }

            // If this revoked ID is one that caused us to void the miner's reputation,
            // then restore their reputation but increase their target M.
            if(minerEntry->mReputation.mVoid && minerEntry->mReputation.mVoidingId.value() == minerId.mPubKey)
            {
                minerEntry->mReputation.mVoid = false;
                minerEntry->mReputation.mVoidingId = std::nullopt;
                minerEntry->mReputation.mM *= mConfig.GetMinerIdReputationMScale();
                UpdateMinerUUIdInDatabaseNL(minerUUId, *minerEntry);

                LogPrint(BCLog::MINERID, "Restored reputation for miner %s and set them a new target M %d\n",
                    boost::uuids::to_string(minerUUId), minerEntry->mReputation.mM);
            }

            if(HexStr(minerId.mPubKey) == revocationMessage.mCompromisedId)
            {
                break;
            }
        }

        // If we have the last revoked ID's previous ID available, update it to point to the new
        // next ID we're about to create.
        if(lastRevokedId)
        {
            const uint256& newIdPrevIdHash { lastRevokedId->mPrevMinerId.GetHash()  };
            auto newIdPrevMinerIdEntry { GetMinerIdFromDatabaseNL(newIdPrevIdHash) };
            if(newIdPrevMinerIdEntry)
            {
                newIdPrevMinerIdEntry->mNextMinerId = curMinerId;
                UpdateMinerIdInDatabaseNL(newIdPrevMinerIdEntry->mPubKey.GetHash(), newIdPrevMinerIdEntry.value());
            }
        }

        // Create/update new miner ID we're rotating to
        UpdateMinerIdInDatabaseNL(curIdHash, MinerIdEntry { minerUUId, blockhash, cbDoc });
        UpdateMinerDetailsAfterRotationNL(minerUUId, curIdHash, blockhash);

        LogPrint(BCLog::MINERID, "Processed partial ID revocation for miner %s compromised ID %s, rotated to new ID %s\n",
            boost::uuids::to_string(minerUUId), revocationMessage.mCompromisedId, HexStr(curMinerId));

        // Return miner UUID and miner ID for this block so we can update recent blocks list
        return { minerUUId, prevIdHash };
    }
    else
    {
        // Full revocation: revoke all IDs for this miner
        const auto& curMinerIdEntry { GetMinerIdFromDatabaseNL(curIdHash) };
        MinerUUId minerUUId { curMinerIdEntry->mUUId };
        for(auto& minerId : GetMinerIdsForMinerNL(minerUUId))
        {
            minerId.mState = MinerIdEntry::State::REVOKED;
            UpdateMinerIdInDatabaseNL(minerId.mPubKey.GetHash(), minerId);
            UpdateRecentBlocksToRemoveMinerIdNL(minerId.mPubKey.GetHash());
        }

        // For completeness, record this block as the last one from this miner
        auto minerUUIdEntry { GetMinerUUIdFromDatabaseNL(minerUUId) };
        if(minerUUIdEntry)
        {
            minerUUIdEntry->mLastBlock = blockhash;
            UpdateMinerUUIdInDatabaseNL(minerUUId, minerUUIdEntry.value());
        }

        LogPrint(BCLog::MINERID, "Processed full ID revocation for miner %s\n", boost::uuids::to_string(minerUUId));

        // Return NULL miner UUID and miner ID so that after full revocation there are no blocks
        // from this miner counted in the recent blocks list.
        return {};
    }
}

// Update miner details in the database after a rotation has occured
void MinerIdDatabase::UpdateMinerDetailsAfterRotationNL(
    const MinerUUId& minerUUId,
    const uint256& newMinerId,
    const uint256& creationBlock)
{
    auto minerUUIdEntry { GetMinerUUIdFromDatabaseNL(minerUUId) };
    if(minerUUIdEntry)
    {
        minerUUIdEntry->mLastBlock = creationBlock;
        minerUUIdEntry->mLatestMinerId = newMinerId;
        UpdateMinerUUIdInDatabaseNL(minerUUId, minerUUIdEntry.value());
        LogPrint(BCLog::MINERID, "Rotated miner ID key for miner UUID %s\n", boost::uuids::to_string(minerUUId));
    }
    else
    {
        // Should never happen
        throw std::runtime_error("Possible miner ID database corruption; Failed to lookup miner for UUID " +
            boost::uuids::to_string(minerUUId));
    }
}

// Update recent block list to mark blocks as no longer mined by the miner with the given miner ID
void MinerIdDatabase::UpdateRecentBlocksToRemoveMinerIdNL(const uint256& minerId)
{
    // Lookup all blocks mined by this ID
    auto& index { mLastBlocksTable.get<TagMinerId>() };
    const auto [lb, ub] { index.equal_range(minerId) };

    // Iterate over blocks updating them to mark as no longer from this ID and miner
    for(auto it = lb; it != ub; ++it)
    {
        RecentBlock block { *it };
        block.mMiner = boost::uuids::nil_uuid();
        index.replace(it, block);
    }
}

// Check revocation keys from a new miner ID document
void MinerIdDatabase::CheckRevocationKeysNL(
    const CPubKey& prevMinerId,
    const CoinbaseDocument& newCbDoc) const
{
    // Skip checks for older versions of the spec'
    if(std::stod(newCbDoc.GetVersion()) >= 0.3)
    {
        const CPubKey& prevRevocationKey { newCbDoc.GetPrevRevocationKey() };
        const CPubKey& revocationKey { newCbDoc.GetRevocationKey() };

        // Regardless of whether a miner ID rotation is occuring or not, and/or
        // whether a revocation key rotation is occuring or not, it should be
        // true that the previous revocation key we saw from this miner matches
        // what they are now telling us was the previous revocation key.
        const uint256& idhash { prevMinerId.GetHash() };
        const auto& minerIdEntry { GetMinerIdFromDatabaseNL(idhash) };
        if(minerIdEntry)
        {
            const auto& oldCbDoc { minerIdEntry->mCoinbaseDoc };

            if(std::stod(oldCbDoc.GetVersion()) < 0.3)
            {
                // Special case for when a miner is upgrading to v0.3 of the spec'.
                // In this case we should just check they are creating a new revocation key.
                if(!prevRevocationKey.IsFullyValid() || !revocationKey.IsFullyValid() || prevRevocationKey != revocationKey)
                {
                    std::stringstream err {};
                    err << "Bad previous revocation key (" << HexStr(prevRevocationKey) << ") "
                        << "or revocation key (" << HexStr(revocationKey) << ") "
                        << "for miner ID " << HexStr(prevMinerId) << " when upgrading to v0.3";
                    throw std::runtime_error(err.str());
                }
            }
            else if(oldCbDoc.GetRevocationKey() != prevRevocationKey)
            {
                std::stringstream err {};
                err << "Previous revocation key (" << HexStr(prevRevocationKey) << ") "
                    << "in update for miner ID (" << HexStr(prevMinerId) << ") "
                    << "doesn't match our record (" << HexStr(oldCbDoc.GetRevocationKey()) << ")";
                throw std::runtime_error(err.str());
            }
        }
        else if(prevRevocationKey != revocationKey)
        {
            LogPrint(BCLog::MINERID, "Warning: Unable to check revocation key rotation "
                "because we don't have previous miner ID details for %s\n", HexStr(prevMinerId));
        }
    }
}

// Check if a revocation is partial or full, and if it's partial is it a duplicate
std::pair<bool, bool> MinerIdDatabase::IsPartialDuplicateRevocationNL(
    const uint256& minerId,
    const uint256& prevMinerId) const
{
    // A partial revocation always rolls to a new miner ID, a full revocation never rolls
    bool partialRevocation { minerId != prevMinerId };
    bool duplicate {false};

    if(partialRevocation)
    {
        // For a partial revocation we must know about the previous ID, otherwise we can't know
        // which miner this is for.
        const auto& prevMinerIdEntry { GetMinerIdFromDatabaseNL(prevMinerId) };
        if(! prevMinerIdEntry)
        {
            throw std::runtime_error("Partial revocation for unknown previous miner ID " + HexStr(prevMinerId));
        }

        // Check for duplicate partial revocation; we will already have the previous ID as revoked
        duplicate = (prevMinerIdEntry->mState == MinerIdEntry::State::REVOKED);
    }
    else
    {
        // For a full revocation we must know about the revoked ID, otherwise we can't know
        // which miner this is for.
        const auto& curMinerIdEntry { GetMinerIdFromDatabaseNL(minerId) };
        if(! curMinerIdEntry)
        {
            throw std::runtime_error("Full revocation for unknown miner ID " + HexStr(minerId));
        }
    }

    return { partialRevocation, duplicate };
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

    // Sort the ids based on creation block height (current -> oldest)
    auto IdSort = [](const MinerIdEntry& id1, const MinerIdEntry& id2)
    {
        // Current entries should come first
        if(id1.mState == MinerIdEntry::State::CURRENT)
        {
            // id1 < id2
            return true;
        }
        if(id2.mState == MinerIdEntry::State::CURRENT)
        {
            // id2 < id1
            return false;
        }

        // Sort other items according to the block height they were created at
        CBlockIndex* id1BlockIndex { mapBlockIndex.Get(id1.mCreationBlock) };
        CBlockIndex* id2BlockIndex { mapBlockIndex.Get(id2.mCreationBlock) };
        if(id1BlockIndex && id2BlockIndex)
        {
            // We use > because we want to sort on descending height
            return id1BlockIndex->GetHeight() > id2BlockIndex->GetHeight();
        }

        // Should never happen (created at block not found), put at bottom of list
        return (id1BlockIndex != nullptr);
    };
    std::sort(minerIds.begin(), minerIds.end(), IdSort);

    return minerIds;
}

// Prune old data from the database, and other daily update tasks
void MinerIdDatabase::Prune()
{
    LogPrint(BCLog::MINERID, "Miner ID database pruning & daily updates\n");

    // Remove any old miner IDs (non-current, rotated) beyond a configured number to keep
    size_t numIdsToKeep { mConfig.GetMinerIdsNumToKeep() };

    // Always keep at least 1 ID (the current ID)
    numIdsToKeep += 1;

    std::unique_lock lock {mMtx};

    for(auto& miner : GetAllMinerUUIdsNL())
    {
        std::vector<MinerIdEntry> minerIds { GetMinerIdsForMinerNL(miner.first) };
        while(minerIds.size() > numIdsToKeep)
        {
            const auto& minerId { minerIds.back() };

            // Sanity check we're not going to prune a current ID (we shouldn't be
            // since there should only ever be one current ID at any time).
            if(minerId.mState != MinerIdEntry::State::CURRENT)
            {
                // Prune this ID from the database
                const uint256& minerIdHash { minerId.mPubKey.GetHash() };
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

        // See if we should modify this miner's M target
        auto& reputation { miner.second.mReputation };
        if(reputation.mM > mConfig.GetMinerIdReputationM())
        {
            bool hittingM { GetNumRecentBlocksForMinerNL(miner.first) >= reputation.mM };
            constexpr int64_t SECONDS_IN_ONE_DAY { 60 * 60 * 24 };
            bool timeToDescreaseM { !reputation.mLastMDecreaseTime ||
                reputation.mLastMDecreaseTime.value() <= (GetTime() - SECONDS_IN_ONE_DAY) };
            if(hittingM && timeToDescreaseM)
            {
                reputation.mM -= 1;
                reputation.mLastMDecreaseTime = GetTime();
                UpdateMinerUUIdInDatabaseNL(miner.first, miner.second);

                LogPrint(BCLog::MINERID, "Reduced M target for miner %s to %d\n",
                    boost::uuids::to_string(miner.first), reputation.mM);
            }
        }
    }
}

// Record details of a recently mined block
void MinerIdDatabase::AddRecentBlockEntryNL(const RecentBlock& block)
{
    // Record details for this block in recent blocks list
    mLastBlocksTable.get<TagBlockHash>().insert(block);

    // And insert into DB
    AddRecentBlockToDatabaseNL(block);

    // Expire any old entries
    while(mLastBlocksTable.get<TagBlockHeight>().size() > mConfig.GetMinerIdReputationN())
    {
        RemoveRecentBlockEntryNL(mLastBlocksTable.get<TagBlockHeight>().begin()->mHash);
    }
}

// Remove a block from recent blocks list
void MinerIdDatabase::RemoveRecentBlockEntryNL(const uint256& blockhash)
{
    // Copy block hash before removing so we don't use after free
    const uint256 blockhashCopy { blockhash };

    // Remove block from recent block list
    mLastBlocksTable.get<TagBlockHash>().erase(blockhashCopy);

    // And remove from DB
    RemoveRecentBlockFromDatabaseNL(blockhashCopy);
}

// Save latest block in DB state
void MinerIdDatabase::SetBestBlockNL(const uint256& hash)
{
    // Update state and save back
    mDBState.mBestBlock = hash;
    UpdateDatabaseStateNL(mDBState);
}

// Flag sync complete in DB state
void MinerIdDatabase::SetSyncCompleteNL(bool synced)
{
    // Update state and save back
    mDBState.mSynced = synced;
    UpdateDatabaseStateNL(mDBState);
}

// Get number of blocks in the recent blocks list from a miner
size_t MinerIdDatabase::GetNumRecentBlocksForMinerNL(const MinerUUId& miner) const
{
    const auto& range { mLastBlocksTable.get<TagMinerUUId>().equal_range(miner) };
    return static_cast<size_t>(std::distance(range.first, range.second));
}

// Open our database
void MinerIdDatabase::OpenDatabaseNL(bool wipe)
{
    // Set path and cache size
    const fs::path dbPath { GetDataDir() / "miner_id/MinerIdDB" };
    uint64_t cacheSize { 1 << 20 };

    mDBWrapper.reset();
    mDBWrapper = std::make_unique<CDBWrapper>(dbPath, cacheSize, false, wipe);
}

// Enable enum_cast for MinerIdEntry::State
const enumTableT<MinerIdDatabase::MinerIdEntry::State>& enumTable(MinerIdDatabase::MinerIdEntry::State)
{
    static enumTableT<MinerIdDatabase::MinerIdEntry::State> table
    {
        { MinerIdDatabase::MinerIdEntry::State::UNKNOWN, "UNKNOWN" },
        { MinerIdDatabase::MinerIdEntry::State::CURRENT, "CURRENT" },
        { MinerIdDatabase::MinerIdEntry::State::ROTATED, "ROTATED" },
        { MinerIdDatabase::MinerIdEntry::State::REVOKED, "REVOKED" }
    };
    return table;
}

