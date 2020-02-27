// Copyright (c) 2019 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include <bloom.h>
#include <consensus/validation.h>
#include <primitives/transaction.h>
#include <tx_mempool_info.h>
#include <utiltime.h>
#include <taskcancellation.h>

#include <atomic>
#include <map>
#include <set>
#include <shared_mutex>

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/member.hpp>
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
    void addOrUpdateTransaction(const TxMempoolInfo& info, CValidationState& state);

    // Get IDs of all held transactions
    std::vector<TxId> getTxnIDs() const;

    // Does this finalise an existing time-locked transaction?
    bool finalisesExistingTransaction(const CTransactionRef& txn) const;

    // Check the given transaction doesn't try to double spend any of
    // our locked UTXOs.
    bool checkForDoubleSpend(const CTransactionRef& txn) const;

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

    // Fetch all transactions updated by the given new transaction.
    // Caller holds mutex.
    std::set<CTransactionRef> getTransactionsUpdatedByNL(const CTransactionRef& txn) const;

    // Insert a new transaction
    void insertNL(const TxMempoolInfo& info, CValidationState& state);
    // Remove an old transaction
    void removeNL(const CTransactionRef& txn);

    // Perform checks on a transaction before allowing an update
    bool validateUpdate(const CTransactionRef& newTxn,
                        const CTransactionRef& oldTxn,
                        CValidationState& state,
                        bool& finalised) const;

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
        result_type operator()(const TxMempoolInfo& info) const
        {
            return info.tx->GetId();
        }
    };

    // Multi-index types
    struct TagTxID {};
    struct TagRawTxID {};
    struct TagUnlockingTime {};
    using TxnMultiIndex = boost::multi_index_container<
        TxMempoolInfo,
        boost::multi_index::indexed_by<
            // By TxID
            boost::multi_index::ordered_unique<
                boost::multi_index::tag<TagTxID>,
                boost::multi_index::member<TxMempoolInfo, CTransactionRef, &TxMempoolInfo::tx>,
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
                boost::multi_index::member<TxMempoolInfo, CTransactionRef, &TxMempoolInfo::tx>,
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
    std::atomic<size_t>         mMaxMemory {0};     // Max memory target
    int64_t                     mPeriodRunFreq {0}; // Run frequency for periodic checks
    int64_t                     mPurgeAge {0};      // Age at which we purge unfinalised txns

    // Our mutex
    mutable std::shared_mutex   mMtx {};
};

