// Copyright (c) 2019 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include <bloom.h>
#include <consensus/validation.h>
#include <leaky_bucket.h>
#include <taskcancellation.h>
#include <tx_mempool_info.h>
#include <txn_validation_data.h>
#include <utiltime.h>

#include <atomic>
#include <map>
#include <set>
#include <shared_mutex>

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/ordered_index.hpp>

class CScheduler;

namespace MempoolTesting
{
    class CTimeLockedMempoolTester;
}

/**
* A class to track time-locked transactions that are waiting until they can be
* entered into the main mempool.
*/
class CTimeLockedMempool final
{
    // Make the tester our friend so it can inspect us properly
    friend class MempoolTesting::CTimeLockedMempoolTester;

  public:

    CTimeLockedMempool();
    ~CTimeLockedMempool() = default;
    CTimeLockedMempool(const CTimeLockedMempool&) = delete;
    CTimeLockedMempool(CTimeLockedMempool&&) = delete;
    CTimeLockedMempool& operator=(const CTimeLockedMempool&) = delete;
    CTimeLockedMempool& operator=(CTimeLockedMempool&&) = delete;

    // Add or update a time-locked transaction
    void addOrUpdateTransaction(const TxMempoolInfo& info,
        const TxInputDataSPtr& pTxInputData,
        CValidationState& state);

    // Get IDs of all held transactions
    std::vector<TxId> getTxnIDs() const;

    // Does this finalise an existing time-locked transaction?
    bool finalisesExistingTransaction(const CTransactionRef& txn) const;

    // Check the given transaction doesn't try to double spend any of
    // our locked UTXOs.
    std::set<CTransactionRef> checkForDoubleSpend(const CTransactionRef& txn) const;

    // Check if an coming update exceeds our configured allowable rate
    bool checkUpdateWithinRate(const CTransactionRef& txn, CValidationState& state) const;

    // Is the given txn ID for one currently held?
    bool exists(const uint256& id) const;

    // Is the given txn ID for one we held until recently?
    bool recentlyRemoved(const uint256& id) const;

    // Fetch the full entry we have for the given txn ID
    TxMempoolInfo getInfo(const uint256& id) const;

    // Launch periodic checks for finalised txns
    void startPeriodicChecks(CScheduler& scheduler);

    // Default frequency of periodic checks in milli-seconds (10 minutes)
    static constexpr unsigned DEFAULT_NONFINAL_CHECKS_FREQ { 10 * 60 * 1000 };

    // Save/restore
    void dumpMempool() const;
    bool loadMempool(const task::CCancellationToken& shutdownToken) const;

    // Get number of txns we hold
    size_t getNumTxns() const;
    // Estimate total memory usage
    size_t estimateMemoryUsage() const;
    // Get our max memory limit
    size_t getMaxMemory() const { return mMaxMemory; }

    // Load or reload our config
    void loadConfig();

  private:

    // Save file version ID
    static constexpr uint64_t DUMP_FILE_VERSION {1};

    // Transaction details we store
    using RateLeakyBucket = LeakyBucket<std::chrono::minutes>;
    struct NonFinalTxn
    {
        NonFinalTxn(const TxMempoolInfo& tmi, size_t mins, size_t maxUpdateRate)
            : info{tmi}
        {
            updateRate = { maxUpdateRate, std::chrono::minutes{mins}, static_cast<double>(maxUpdateRate) };
        }
        NonFinalTxn(const TxMempoolInfo& tmi, const RateLeakyBucket& rate)
            : info{tmi}, updateRate{rate}
        {}

        const CTransactionRef& GetTx() const;

        TxMempoolInfo info {};
        RateLeakyBucket updateRate {};
    };

    // Fetch all transactions updated by the given new transaction.
    // Caller holds mutex.
    std::set<CTransactionRef> getTransactionsUpdatedByNL(const CTransactionRef& txn) const;

    // Calculate updated replacement rate for txn
    RateLeakyBucket updateReplacementRateNL(const NonFinalTxn& txn, CValidationState& state) const;

    // Insert a new transaction
    void insertNL(NonFinalTxn&& txn, CValidationState& state);
    // Remove an old transaction
    void removeNL(const CTransactionRef& txn);

    // Perform checks on a transaction before allowing an update
    bool validateUpdateNL(const CTransactionRef& newTxn,
                          const CTransactionRef& oldTxn,
                          CValidationState& state,
                          bool& finalised,
                          RateLeakyBucket& newRate) const;

    // Estimate our memory usage
    // Caller holds mutex.
    size_t estimateMemoryUsageNL() const;

    // Do periodic checks for finalised txns and txns to purge
    void periodicChecks();

    // Compare transactions by ID
    struct CompareTxnID
    {
        bool operator()(const CTransactionRef& txn1, const CTransactionRef& txn2) const
        {
            return txn1->GetId() < txn2->GetId();
        }
    };

    // Compare transactions by unlocking time
    struct CompareTxnUnlockingTime
    {
        bool operator()(const CTransactionRef& txn1, const CTransactionRef& txn2) const
        {
            return txn1->nLockTime < txn2->nLockTime;
        }
    };

    // Key extractor for raw TxIds
    struct TxIdExtractor
    {
        using result_type = uint256;
        result_type operator()(const NonFinalTxn& nft) const
        {
            return nft.info.GetTxId();
        }
    };

    // Multi-index types
    struct TagTxID {};
    struct TagRawTxID {};
    struct TagUnlockingTime {};
    using TxnMultiIndex = boost::multi_index_container<
        NonFinalTxn,
        boost::multi_index::indexed_by<
            // By TxID
            boost::multi_index::ordered_unique<
                boost::multi_index::tag<TagTxID>,
                boost::multi_index::const_mem_fun<NonFinalTxn,const CTransactionRef&,&NonFinalTxn::GetTx>,
                CompareTxnID
            >,
            // By raw TxID
            boost::multi_index::ordered_unique<
                boost::multi_index::tag<TagRawTxID>,
                TxIdExtractor
            >,
            // By unlocking time
            boost::multi_index::ordered_non_unique<
                boost::multi_index::tag<TagUnlockingTime>,
                boost::multi_index::const_mem_fun<NonFinalTxn,const CTransactionRef&,&NonFinalTxn::GetTx>,
                CompareTxnUnlockingTime
            >
        >
    >;

    // Time-locked transactions
    TxnMultiIndex               mTransactionMap {};
    // Estimate of heap memory used by transactions in the TxnMultiIndex
    size_t                      mTxnMemoryUsage {0};

    // Map of UTXOs spent by time-locked transactions
    using OutPointMap = std::map<COutPoint, CTransactionRef>;
    OutPointMap                 mUTXOMap {};

    // Bloom filter for tracking recently seen txns that we have finished with and
    // removed from the pool.
    // Memory overhead approx 110K. If we start seeing a large number of
    // non-final transactions used in the real world we may need to increase the
    // size of this filter.
    CRollingBloomFilter         mRecentlyRemoved { 10000, 0.000001 };

    // Cached configuration values
    std::atomic<size_t>         mMaxMemory {0};         // Max memory target
    int64_t                     mPeriodRunFreq {0};     // Run frequency for periodic checks
    int64_t                     mPurgeAge {0};          // Age at which we purge unfinalised txns
    uint64_t                    mMaxUpdateRate {0};     // Max rate to accept updates to transactions (in txns / mUpdatePeriodMins minutes)
    uint64_t                    mUpdatePeriodMins {0};  // Minutes over which max rate is measured

    // Our mutex
    mutable std::shared_mutex   mMtx {};
};

