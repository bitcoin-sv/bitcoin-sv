// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include "dbwrapper.h"
#include "miner_id/coinbase_doc.h"
#include "serialize.h"
#include "uint256.h"

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
class Config;
class CScheduler;
class CTransaction;
class MinerId;
class UniValue;

/**
 * The miner ID database.
 */
class MinerIdDatabase
{
  public:

    MinerIdDatabase(const Config& config);
    ~MinerIdDatabase() = default;

    // Forbid copying/moving
    MinerIdDatabase(const MinerIdDatabase&) = delete;
    MinerIdDatabase(MinerIdDatabase&&) = delete;
    MinerIdDatabase& operator=(const MinerIdDatabase&) = delete;
    MinerIdDatabase& operator=(MinerIdDatabase&&) = delete;

    /**
     * A new block has been added to the tip.
     * Check for a miner ID coinbase document and update the database accordingly.
     */
    void BlockAdded(const CBlock& block, int32_t height);

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
     * Does a miner with the specifed id have a good reputation?
     */
    [[nodiscard]] bool CheckMinerReputation(const uint256& idHash) const;

    /**
     * Prune old data from the database
     */
    void Prune();

    // Unit test support
    template<typename T> struct UnitTestAccess;

  private:

    // Prefix to store map of miner id values with id as a key
    static constexpr char DB_MINER_ID {'I'};
    // Prefix to store map of miners with uuid as a key
    static constexpr char DB_MINER {'U'};

    // Data that describes a single entry in the MinerUUId table
    using MinerUUId = boost::uuids::uuid;
    struct MinerUUIdEntry
    {
        // Whether this miner has voided its reputation (for example; by
        // previously sending us a bad block).
        bool mReputationVoid {false};

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
            READWRITE(mReputationVoid);
            READWRITE(mFirstBlock);
            READWRITE(mLastBlock);
            READWRITE(mLatestMinerId);
        }
    };

    // Data that describes a single entry in the MinerId table
    struct MinerIdEntry
    {
        // UUID of miner this entry is for
        MinerUUId mUUId {};

        // Public key this miner ID represents
        CPubKey mPubKey {};

        // Indicate if this ID is no longer current (can be used to prune database if required)
        bool mCurrent {true};
        uint256 mRotationBlock {};

        // The coinbase document for this miner ID
        CoinbaseDocument mCoinbaseDoc {};

        // Allow serialisation/deserialisation
        ADD_SERIALIZE_METHODS
        template <typename Stream, typename Operation>
        inline void SerializationOp(Stream& s, Operation ser_action)
        {
            READWRITE(mUUId);
            READWRITE(mPubKey);
            READWRITE(mCurrent);
            READWRITE(mRotationBlock);
            READWRITE(mCoinbaseDoc);
        }
    };

    // Recently mined block details
    struct RecentBlock
    {
        uint256 id {};
        int32_t height {0};
        MinerUUId miner { boost::uuids::nil_uuid() };
    };

    // Multi-index container type for storing details of blocks mined
    struct TagBlockId;
    struct TagBlockHeight;
    struct TagMinerUUId;
    using BlocksMultiIndex = boost::multi_index_container<
        RecentBlock,
        boost::multi_index::indexed_by<
            // Set of recent blocks
            boost::multi_index::ordered_unique<
                boost::multi_index::tag<TagBlockId>,
                boost::multi_index::member<RecentBlock, uint256, &RecentBlock::id>
            >,
            // Ordered by height
            boost::multi_index::ordered_unique<
                boost::multi_index::tag<TagBlockHeight>,
                boost::multi_index::member<RecentBlock, int32_t, &RecentBlock::height>
            >,
            // Ordered by miner
            boost::multi_index::ordered_non_unique<
                boost::multi_index::tag<TagMinerUUId>,
                boost::multi_index::member<RecentBlock, MinerUUId, &RecentBlock::miner>
            >
        >
    >;


    // Check we agree with the chain tip, and if not rebuild ourselves from scratch
    void UpdateToTip(bool rebuild);

    // Lookup miner that produced the block with the given coinbase txn (if known)
    [[nodiscard]] std::optional<MinerUUId> GetMinerForBlockNL(int32_t height, const CTransaction& coinbase) const;

    // Update miner ID details from coinbase for newly added block
    void BlockAddedNL(const uint256& blockhash, int32_t height, const CTransaction& coinbase);

    // Lookup all miner IDs for the given miner and return them sorted by age
    [[nodiscard]] std::vector<MinerIdEntry> GetMinerIdsForMinerNL(const MinerUUId& miner) const;

    // Get number of blocks in the recent blocks list from a miner
    [[nodiscard]] size_t GetNumRecentBlocksForMinerNL(const MinerUUId& miner) const;

    // Get/update miner ID entry in the database
    [[nodiscard]] std::pair<bool, MinerIdEntry> GetMinerIdEntryNL(const uint256& minerId) const;
    void UpdateMinerIdEntryNL(const uint256& key, const MinerIdEntry& entry);

    // Get/update miner UUID entry in the database
    [[nodiscard]] std::pair<bool, MinerUUIdEntry> GetMinerUUIdEntryNL(const MinerUUId& uuid) const;
    void UpdateMinerUUIdEntryNL(const MinerUUId& key, const MinerUUIdEntry& entry);

    // Get all miner IDs / miner UUIDs from the database
    [[nodiscard]] std::unordered_map<uint256, MinerIdEntry> GetAllMinerIdsNL() const;
    using MinerUUIdMap = std::unordered_map<MinerUUId, MinerUUIdEntry, boost::hash<boost::uuids::uuid>>;
    [[nodiscard]] MinerUUIdMap GetAllMinerUUIdsNL() const;


    // Our mutex
    mutable std::mutex mMtx {};

    // Keep a reference to the config
    const Config& mConfig;

    // Our LevelDB wrapper
    std::unique_ptr<CDBWrapper> mDBWrapper {nullptr};

    // Store details of who mined the last few blocks
    BlocksMultiIndex mLastBlocksTable {};

    // UUID generator
    boost::uuids::random_generator mUUIdGenerator {};

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

// Global reference to the MinerID database
extern std::unique_ptr<MinerIdDatabase> g_minerIDs;

