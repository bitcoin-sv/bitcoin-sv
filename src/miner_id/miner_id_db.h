// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include "dbwrapper.h"
#include "enum_cast.h"
#include "miner_id/coinbase_doc.h"
#include "serialize.h"
#include "uint256.h"

#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>

class CBlock;
class CBlockIndex;
class Config;
class CScheduler;
class MinerId;
class RevokeMid;
class UniValue;

/**
 * The miner ID database.
 */
class MinerIdDatabase final
{
  public:

    MinerIdDatabase(const Config& config);
    ~MinerIdDatabase();

    // Forbid copying/moving
    MinerIdDatabase(const MinerIdDatabase&) = delete;
    MinerIdDatabase(MinerIdDatabase&&) = delete;
    MinerIdDatabase& operator=(const MinerIdDatabase&) = delete;
    MinerIdDatabase& operator=(MinerIdDatabase&&) = delete;

    /**
     *  Trigger a database sync to the blockchain.
     */
    void TriggerSync(bool fromScratch, bool fromGenesis);

    /**
     * A new block has been added to the tip.
     * Check for a miner ID coinbase document and update the database accordingly.
     */
    void BlockAdded(const CBlock& block, CBlockIndex const * pindex);

    /**
     * A block has been removed from the tip.
     * Update the recent blocks list accordingly.
     */
    void BlockRemoved(const CBlock& block);

    /**
     * An invalid block has been received.
     * Check for a miner ID coinbase document and update the database accordingly.
     */
    void InvalidBlock(const CBlock& block, int32_t height);

    /**
     * Process a P2P revokemid message.
     * Chaeck message and update the database accordingly.
     */
    void ProcessRevokemidMessage(const RevokeMid& msg);

    /**
     * Does a miner with the specifed id have a good reputation?
     */
    [[nodiscard]] bool CheckMinerReputation(const uint256& idHash) const;

    /**
     * Get a miners coinbase document with its state information..
     */
    std::optional<std::pair<CoinbaseDocument, std::string>> GetMinerCoinbaseDocInfo(const uint256& idHash) const;

    /**
     * Prune old data from the database
     */
    void Prune();

    /**
     * Dump our contents out in JSON format.
     */
    UniValue DumpJSON() const;

    // Unit test support
    template<typename T> struct UnitTestAccess;

  private:

    // Prefix to store map of miner id values with id as a key
    static constexpr char DB_MINER_ID {'I'};
    // Prefix to store map of miners with uuid as a key
    static constexpr char DB_MINER {'U'};
    // Prefix to store map of recent blocks with hash as key
    static constexpr char DB_RECENT_BLOCKS {'B'};
    // Key into database to fetch state
    static constexpr char DB_STATE {'S'};

    // Encapsulate details about a miner's reputation
    struct MinerReputation
    {
        // M/N miner has to hit to have a good reputation
        uint32_t mM {0};

        // Whether this miner has voided its reputation (for example; by
        // previously sending us a bad block).
        bool mVoid {false};

        // If their reputation was voided, this is the miner ID in use at that time
        std::optional<CPubKey> mVoidingId { std::nullopt };

        // Last time we decreased this miners M target
        std::optional<int64_t> mLastMDecreaseTime { std::nullopt };

        // Allow serialisation/deserialisation
        ADD_SERIALIZE_METHODS
        template <typename Stream, typename Operation>
        inline void SerializationOp(Stream& s, Operation ser_action)
        {
            READWRITE(mM);
            READWRITE(mVoid);
            READWRITE(mVoidingId);
            READWRITE(mLastMDecreaseTime);
        }
    };

    // Data that describes a single entry in the MinerUUId table
    using MinerUUId = boost::uuids::uuid;
    struct MinerUUIdEntry
    {
        // This miner's reputation details
        MinerReputation mReputation {};
        
        // First and last seen blocks from this miner
        uint256 mFirstBlock {};
        uint256 mLastBlock {};

        // Most recent miner ID we've seen from this miner
        uint256 mLatestMinerId {};

        // Allow serialisation/deserialisation
        ADD_SERIALIZE_METHODS
        template <typename Stream, typename Operation>
        inline void SerializationOp(Stream& s, Operation ser_action)
        {
            READWRITE(mReputation);
            READWRITE(mFirstBlock);
            READWRITE(mLastBlock);
            READWRITE(mLatestMinerId);
        }
    };

    // Data that describes a single entry in the MinerId table
    struct MinerIdEntry
    {
        MinerIdEntry() = default;

        MinerIdEntry(const MinerUUId& uuid,
                     const uint256& block,
                     const CoinbaseDocument& doc)
            : mUUId{uuid},
              mPubKey{doc.GetMinerIdAsKey()},
              mPrevMinerId{doc.GetPrevMinerIdAsKey()},
              mState{State::CURRENT},
              mCreationBlock{block},
              mCoinbaseDoc{doc}
        {}

        // UUID of miner this entry is for
        MinerUUId mUUId {};

        // Public key this miner ID represents
        CPubKey mPubKey {};

        // Previous miner ID
        CPubKey mPrevMinerId {};

        // State of this ID
        enum class State : unsigned { UNKNOWN = 0, CURRENT, ROTATED, REVOKED };
        State mState { State::UNKNOWN };

        // For rotated miner IDs, the next ID this was rotated to
        std::optional<CPubKey> mNextMinerId { std::nullopt };

        // Block this ID was created in
        uint256 mCreationBlock {};

        // The coinbase document for this miner ID
        CoinbaseDocument mCoinbaseDoc {};

        // Allow serialisation/deserialisation
        ADD_SERIALIZE_METHODS
        template <typename Stream, typename Operation>
        inline void SerializationOp(Stream& s, Operation ser_action)
        {
            READWRITE(mUUId);
            READWRITE(mPubKey);
            READWRITE(mPrevMinerId);
            READWRITEENUM(mState);
            READWRITE(mNextMinerId);
            READWRITE(mCreationBlock);
            READWRITE(mCoinbaseDoc);
        }
    };
    friend const enumTableT<MinerIdEntry::State>& enumTable(MinerIdEntry::State);

    // Recently mined block details
    struct RecentBlock
    {
        // Hash of block
        uint256 mHash {};

        // Height of block
        int32_t mHeight {0};

        // UUID of miner who mined this block
        MinerUUId mMiner { boost::uuids::nil_uuid() };

        // Current miner ID for miner at the time they mined this block
        uint256 mMinerId {};

        // Allow serialisation/deserialisation
        ADD_SERIALIZE_METHODS
        template <typename Stream, typename Operation>
        inline void SerializationOp(Stream& s, Operation ser_action)
        {
            READWRITE(mHash);
            READWRITE(mHeight);
            READWRITE(mMiner);
            READWRITE(mMinerId);
        }
    };

    // Database state information
    struct DatabaseState
    {
        // Version of the DB schema in use
        static constexpr uint16_t DB_VERSION {1};
        uint16_t mVersion {DB_VERSION};

        // Hash of what we think is the chain tip
        uint256 mBestBlock {};

        // Flag to say whether we've finished syncing to the chain
        bool mSynced {false};

        // Allow serialisation/deserialisation
        ADD_SERIALIZE_METHODS
        template <typename Stream, typename Operation>
        inline void SerializationOp(Stream& s, Operation ser_action)
        {
            READWRITE(mVersion);
            READWRITE(mBestBlock);
            READWRITE(mSynced);
        }
    };

    // Multi-index container type for storing details of blocks mined
    struct TagBlockHash;
    struct TagBlockHeight;
    struct TagMinerUUId;
    struct TagMinerId;
    using BlocksMultiIndex = boost::multi_index_container<
        RecentBlock,
        boost::multi_index::indexed_by<
            // Set of recent blocks
            boost::multi_index::ordered_unique<
                boost::multi_index::tag<TagBlockHash>,
                boost::multi_index::member<RecentBlock, uint256, &RecentBlock::mHash>
            >,
            // Ordered by height
            boost::multi_index::ordered_unique<
                boost::multi_index::tag<TagBlockHeight>,
                boost::multi_index::member<RecentBlock, int32_t, &RecentBlock::mHeight>
            >,
            // Ordered by miner
            boost::multi_index::ordered_non_unique<
                boost::multi_index::tag<TagMinerUUId>,
                boost::multi_index::member<RecentBlock, MinerUUId, &RecentBlock::mMiner>
            >,
            // Ordered by miner ID
            boost::multi_index::ordered_non_unique<
                boost::multi_index::tag<TagMinerId>,
                boost::multi_index::member<RecentBlock, uint256, &RecentBlock::mMinerId>
            >
        >
    >;


    // Synchronise ourselves with the chain tip
    void UpdateToTip(bool syncFromGenesis);

    // Lookup miner that produced the given block (if known)
    [[nodiscard]] std::optional<MinerIdEntry> GetMinerIdForBlockNL(const CBlock& block, int32_t height) const;

    // Update miner ID details from coinbase for newly added block
    void BlockAddedNL(const CBlock& block, CBlockIndex const * pindex);

    // Process a miner ID from a brand new miner
    MinerUUId ProcessNewMinerNL(const uint256& minerIdHash,
                                const CoinbaseDocument& cbDoc,
                                const uint256& blockhash);

    // Process a reuse of an existing miner ID
    MinerUUId ProcessExistingMinerIdNL(const CoinbaseDocument& cbDoc,
                                       const uint256& blockhash,
                                       MinerIdEntry& minerIdEntry);

    // Process and apply a miner ID key roll
    MinerUUId ProcessRotationNL(const CPubKey& curMinerId,
                                const CPubKey& prevMinerId,
                                const CoinbaseDocument& cbDoc,
                                const uint256& blockhash);

    // Process and apply a miner ID key revocation
    std::pair<MinerUUId, uint256> ProcessRevocationNL(const CPubKey& curMinerId,
                                                      const CPubKey& prevMinerId,
                                                      const CoinbaseDocument& cbDoc,
                                                      const uint256& blockhash);

    // Update miner details in the database after a rotation has occured
    void UpdateMinerDetailsAfterRotationNL(const MinerUUId& minerUUId,
                                           const uint256& newMinerId,
                                           const uint256& creationBlock);

    // Update recent block list to mark blocks as no longer mined by the miner with the given miner ID
    void UpdateRecentBlocksToRemoveMinerIdNL(const uint256& minerId);

    // Check revocation keys from a new miner ID document
    void CheckRevocationKeysNL(const CPubKey& prevMinerId, const CoinbaseDocument& newCbDoc) const;

    // Check if a revocation is partial or full, and if it's partial is it a duplicate
    std::pair<bool, bool> IsPartialDuplicateRevocationNL(const uint256& minerId, const uint256& prevMinerId) const;

    // Lookup all miner IDs for the given miner and return them sorted by age
    [[nodiscard]] std::vector<MinerIdEntry> GetMinerIdsForMinerNL(const MinerUUId& miner) const;

    // Record details of a recently mined block
    void AddRecentBlockEntryNL(const RecentBlock& block);
    // Remove a block from recent blocks list
    void RemoveRecentBlockEntryNL(const uint256& blockhash);

    // Get number of blocks in the recent blocks list from a miner
    [[nodiscard]] size_t GetNumRecentBlocksForMinerNL(const MinerUUId& miner) const;

    // Get/update miner ID entry in the database
    [[nodiscard]] std::optional<MinerIdEntry> GetMinerIdFromDatabaseNL(const uint256& minerId) const;
    void UpdateMinerIdInDatabaseNL(const uint256& key, const MinerIdEntry& entry);

    // Get/update miner UUID entry in the database
    [[nodiscard]] std::optional<MinerUUIdEntry> GetMinerUUIdFromDatabaseNL(const MinerUUId& uuid) const;
    void UpdateMinerUUIdInDatabaseNL(const MinerUUId& key, const MinerUUIdEntry& entry);

    // Add/lookup/erase recent block details to the database
    void AddRecentBlockToDatabaseNL(const RecentBlock& entry);
    void RemoveRecentBlockFromDatabaseNL(const uint256& key);
    [[nodiscard]] std::optional<RecentBlock> GetRecentBlockFromDatabaseNL(const uint256& key) const;
    void ReadAllRecentBlocksFromDatabaseNL();

    // Get/update DB state form the database
    [[nodiscard]] std::optional<DatabaseState> GetDatabaseStateNL() const;
    void UpdateDatabaseStateNL(const DatabaseState& state);

    // Update best block and synced flag in DB state
    void SetBestBlockNL(const uint256& hash);
    void SetSyncCompleteNL(bool synced);

    // Get all miner IDs / miner UUIDs from the database
    [[nodiscard]] std::unordered_map<uint256, MinerIdEntry> GetAllMinerIdsNL() const;
    using MinerUUIdMap = std::unordered_map<MinerUUId, MinerUUIdEntry, boost::hash<boost::uuids::uuid>>;
    [[nodiscard]] MinerUUIdMap GetAllMinerUUIdsNL() const;

    // Open our database
    void OpenDatabaseNL(bool wipe = false);


    // Our mutex
    mutable std::mutex mMtx {};

    // Keep a reference to the config
    const Config& mConfig;

    // Our LevelDB wrapper
    std::unique_ptr<CDBWrapper> mDBWrapper {nullptr};

    // Store details of who mined the last few blocks
    BlocksMultiIndex mLastBlocksTable {};

    // Current database state
    DatabaseState mDBState {};

    // UUID generator
    boost::uuids::random_generator mUUIdGenerator {};

    // Future and promise for running initial block sync in background
    std::future<void> mFuture {};
    std::promise<void> mPromise {};

    // Record some state information to aid testing
    struct Status
    {
        bool mRebuiltFromBlockchain {false};
    } mStatus;
};

// Start MinerID database periodic tasks
void ScheduleMinerIdPeriodicTasks(CScheduler& scheduler, MinerIdDatabase& db);

// Does the miner identified with the given miner ID have a good reputation?
[[nodiscard]] bool MinerHasGoodReputation(const MinerIdDatabase& db, const MinerId& id);
[[nodiscard]] bool MinerHasGoodReputation(const MinerIdDatabase& db, const CPubKey& id);

// Get a miners coinbase document information.
std::optional<std::pair<CoinbaseDocument, std::string>> GetMinerCoinbaseDocInfo(const MinerIdDatabase& db, const CPubKey& id);

// Global reference to the MinerID database
extern std::unique_ptr<MinerIdDatabase> g_minerIDs;

// Enable enum_cast for MinerIdEntry::State
const enumTableT<MinerIdDatabase::MinerIdEntry::State>& enumTable(MinerIdDatabase::MinerIdEntry::State);

