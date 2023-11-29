// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2019-2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#ifndef BITCOIN_TXMEMPOOL_H
#define BITCOIN_TXMEMPOOL_H

#include "amount.h"
#include "cfile_util.h"
#include "coins.h"
#include "mining/journal_entry.h"
#include "mining/journal_builder.h"
#include "primitives/transaction.h"
#include "sync.h"
#include "time_locked_mempool.h"
#include "txhasher.h"
#include "tx_mempool_info.h"
#include "txn_validation_data.h"
#include "policy/policy.h"

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/member.hpp>

#include <boost/signals2/signal.hpp>
#include <boost/uuid/uuid.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>

class CAutoFile;
class CBlockIndex;
class CEvictionCandidateTracker;
class Config; // NOLINT(cppcoreguidelines-virtual-class-destructor)
class CoinsDB;
class CoinsDBView;
class CAsyncMempoolTxDB;

/**
 * Fake height value used in Coins to signify they are only in the memory
 * pool(since 0.8)
 */
static const int32_t MEMPOOL_HEIGHT = 0x7FFFFFFF;

struct LockPoints {
    // Will be set to the blockchain height and median time past values that
    // would be necessary to satisfy all relative locktime constraints (BIP68)
    // of this tx given our view of block chain history
    int32_t height; // NOLINT(cppcoreguidelines-use-default-member-init)
    int64_t time; // NOLINT(cppcoreguidelines-use-default-member-init)
    // As long as the current chain descends from the highest height block
    // containing one of the inputs used in the calculation, then the cached
    // values are still valid even after a reorg.
    const CBlockIndex *maxInputBlock; // NOLINT(cppcoreguidelines-use-default-member-init)

    LockPoints() : height(0), time(0), maxInputBlock(nullptr) {}
};

class CTxMemPool;
class CPFPGroup;

/**
 * Statistics for the secondary mempool and CPFP groups.
 *
 * In the secondary mempool, the stats accumulated from parents are approximate
 * because they don't account for possible duplicates.
 *
 * In the primary mempool CPFP group, duplicates are eliminated and the
 * accumulated data are precise, except for @c ancestorsCount, which is not used.
 */
struct SecondaryMempoolEntryData
{
    // Accumulated data from parents or for the whole group.
    Amount fee {0};
    Amount feeDelta {0};
    size_t size {0};

    // We only count ancestors and chain length in the secondary mempool.
    size_t ancestorsCount {0};
};

inline bool operator==(const SecondaryMempoolEntryData& a,
                       const SecondaryMempoolEntryData& b)
{
    return ((&a == &b) || ((a.fee == b.fee) &&
                           (a.feeDelta == b.feeDelta) &&
                           (a.size == b.size) &&
                           (a.ancestorsCount == b.ancestorsCount)));
}

/** \class CTxPrioritizer
 *
 * The aim of this class is to support txn prioritisation and cleanup
 * for the given set of transactions (in RAII style).
 * If txn, that was prioritised, did not go to the mempool, then it's
 * prioritisation entry needs to be cleared during destruction of the object.
 */
class CTxPrioritizer final
{
private:
    CTxMemPool& mMempool;
    std::vector<TxId> mTxnsToPrioritise {};

public:
    CTxPrioritizer(CTxMemPool& mempool, const TxId& txnToPrioritise = TxId());
    CTxPrioritizer(CTxMemPool& mempool, std::vector<TxId> txnsToPrioritise);
    ~CTxPrioritizer();
    // Forbid copying/assignment
    CTxPrioritizer(const CTxPrioritizer&) = delete;
    CTxPrioritizer(CTxPrioritizer&&) = delete;
    CTxPrioritizer& operator=(const CTxPrioritizer&) = delete;
    CTxPrioritizer& operator=(CTxPrioritizer&&) = delete;
};

/** \class CTxMemPoolEntry
 *
 * CTxMemPoolEntry stores data about the corresponding transaction.
 *
 * When a new entry is added to the mempool, we update the descendant state
 * (nCountWithDescendants, nSizeWithDescendants, and nModFeesWithDescendants)
 * for all ancestors of the newly added transaction.
 *
 * If updating the descendant state is skipped, we can mark the entry as
 * "dirty", and set nSizeWithDescendants/nModFeesWithDescendants to equal
 * nTxSize/nFee+feeDelta. (This can potentially happen during a reorg, where we
 * limit the amount of work we're willing to do to avoid consuming too much
 * CPU.)
 */
// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions)
class CTxMemPoolEntry {
private:
    CTransactionWrapperRef tx;
    // The mempool info constructor needs access to the wrapper reference.
    friend TxMempoolInfo::TxMempoolInfo(const CTxMemPoolEntry&);
    // So does the journal entry constructor.
    friend mining::CJournalEntry::CJournalEntry(const CTxMemPoolEntry&);

    //!< Cached to avoid expensive parent-transaction lookups
    Amount nFee;
    //!< ... and avoid recomputing tx size
    size_t nTxSize;
    //!< ... and total memory usage
    size_t nUsageSize;
    //!< Local time when entering the mempool
    int64_t nTime;
    //!< Used for determining the priority of the transaction for mining in a
    //! block
    Amount feeDelta;
    //!< Track the height and time at which tx was final
    LockPoints lockPoints;

    // Grouping data for long chains/CPFP evaluation.
    std::shared_ptr<CPFPGroup> group {nullptr};
    std::optional<SecondaryMempoolEntryData> groupingData {std::nullopt};
    // The mempool needs access to the group statistics.
    friend class CTxMemPool;

    //!< Chain height when entering the mempool
    int32_t entryHeight;
    //!< keep track of transactions that spend a coinbase or confiscation
    bool spendsCoinbaseOrConfiscation;
    // index of insertion to mempool, entry with smaller index is inserted before the one with larger
    uint64_t insertionIndex;
    // ancestors count
    size_t ancestorsCount;
    
public:
    CTxMemPoolEntry(const CTransactionRef &_tx, const Amount _nFee,
                    int64_t _nTime,
                    int32_t _entryHeight,
                    bool spendsCoinbaseOrConfiscation, LockPoints lp);

    CTxMemPoolEntry(const CTxMemPoolEntry &other) = default;
    CTxMemPoolEntry& operator=(const CTxMemPoolEntry&) = default;

    // CPFP group, if any that this transaction belongs to.
    mining::GroupID GetCPFPGroupId() const;
    CTransactionRef GetSharedTx() const { return tx->GetTx(); }
    const TxId& GetTxId() const { return tx->GetId(); }

    Amount GetFee() const { return nFee; }
    Amount GetFeeDelta() const { return feeDelta; }
    size_t GetTxSize() const { return nTxSize; }
    int64_t GetTime() const { return nTime; }
    int32_t GetHeight() const { return entryHeight; }
    Amount GetModifiedFee() const { return nFee + feeDelta; }
    size_t DynamicMemoryUsage() const { return nUsageSize; }
    const LockPoints &GetLockPoints() const { return lockPoints; }

    // Updates the fee delta used for mining priority score, and the
    // modified fees with descendants.
    void UpdateFeeDelta(Amount feeDelta);
    // Update the LockPoints after a reorg
    void UpdateLockPoints(const LockPoints &lp);

    bool GetSpendsCoinbaseOrConfiscation() const { return spendsCoinbaseOrConfiscation; }

    bool IsCPFPGroupMember() const { return group != nullptr; }
    bool IsInPrimaryMempool() const { return !groupingData.has_value(); }
    std::shared_ptr<const CPFPGroup> GetCPFPGroup() const { return group; }

    bool IsInMemory() const noexcept { return tx->IsInMemory(); }
    TxStorage GetTxStorage() const noexcept { return tx->GetTxStorage(); }

    template<typename X> struct UnitTestAccess;

    void SetInsertionIndex(uint64_t ndx) { insertionIndex = ndx; }
    uint64_t GetInsertionIndex() const { return insertionIndex; }
};

struct update_fee_delta {
    // NOLINTNEXTLINE(performance-unnecessary-value-param)
    update_fee_delta(Amount _feeDelta) : feeDelta(_feeDelta) {}

    void operator()(CTxMemPoolEntry &e) { e.UpdateFeeDelta(feeDelta); }

private:
    Amount feeDelta;
};

struct update_lock_points {
    update_lock_points(const LockPoints &_lp) : lp(_lp) {}

    void operator()(CTxMemPoolEntry &e) { e.UpdateLockPoints(lp); }

private:
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
    const LockPoints &lp;
};

// extracts a transaction hash from CTxMempoolEntry or CTransactionRef
struct mempoolentry_txid {
    typedef uint256 result_type;
    result_type operator()(const CTxMemPoolEntry &entry) const {
        return entry.GetTxId();
    }

    result_type operator()(const CTransactionRef &tx) const {
        return tx->GetId();
    }
};

/** \class CompareTxMemPoolEntryByScore
 *
 *  Sort by score of entry ((fee+delta)/size) in descending order
 */
class CompareTxMemPoolEntryByScore {
public:
    bool operator()(const CTxMemPoolEntry &a, const CTxMemPoolEntry &b) const {
        double f1 = double(b.GetTxSize() * a.GetModifiedFee().GetSatoshis());
        double f2 = double(a.GetTxSize() * b.GetModifiedFee().GetSatoshis());
        if (f1 == f2) {
            return b.GetTxId() < a.GetTxId();
        }
        return f1 > f2;
    }
};

class CompareTxMemPoolEntryByEntryTime {
public:
    bool operator()(const CTxMemPoolEntry &a, const CTxMemPoolEntry &b) const {
        return a.GetTime() < b.GetTime();
    }
};

// Multi_index tag names
struct transaction_id {};
struct entry_time {};
struct insertion_order {};

struct by_prevout {};
struct by_txiter {};


/**
 * Reason why a transaction was removed from the mempool, this is passed to the
 * notification signal.
 */
enum class MemPoolRemovalReason {
    //! Manually removed or unknown reason
    UNKNOWN = 0,
    //! Expired from mempool
    EXPIRY,
    //! Removed in size limiting
    SIZELIMIT,
    //! Removed for reorganization
    REORG,
    //! Removed for block
    BLOCK,
    //! Removed for conflict with in-block transaction
    CONFLICT,
    //! Removed for replacement
    REPLACED,
    //! Removed because input was frozen
    FROZEN_INPUT,
    //! Removed because it is no longer whitelisted
    NOT_WHITELISTED
};

inline const enumTableT<MemPoolRemovalReason>& enumTable(MemPoolRemovalReason)
{
    static enumTableT<MemPoolRemovalReason> table
    {
        {MemPoolRemovalReason::UNKNOWN, "UNKNOWN"},
        {MemPoolRemovalReason::EXPIRY, "EXPIRY"},
        {MemPoolRemovalReason::SIZELIMIT, "SIZELIMIT"},
        {MemPoolRemovalReason::REORG, "REORG"},
        {MemPoolRemovalReason::BLOCK, "BLOCK"},
        {MemPoolRemovalReason::CONFLICT, "CONFLICT"},
        {MemPoolRemovalReason::REPLACED, "REPLACED"},
        {MemPoolRemovalReason::FROZEN_INPUT, "FROZEN_INPUT"},
        {MemPoolRemovalReason::NOT_WHITELISTED, "NOT_WHITELISTED"},
    };
    return table;
}

std::ostream& operator<<(std::ostream&, const MemPoolRemovalReason&);

struct DisconnectedBlockTransactions;

struct CTransactionConflictData {
    // NOLINTBEGIN(cppcoreguidelines-avoid-const-or-ref-data-members)
    const CTransaction* const conflictedWith;
    const uint256* const blockhash;
    // NOLINTEND(cppcoreguidelines-avoid-const-or-ref-data-members)

    CTransactionConflictData(const CTransaction* conflictedWith_, const uint256* blockhash_)
    : conflictedWith{conflictedWith_}
    , blockhash{blockhash_}
    {
        assert(conflictedWith != nullptr);
    }
};

using CTransactionConflict = std::optional<CTransactionConflictData>;

/**
 * CTxMemPool stores valid-according-to-the-current-best-chain transactions that
 * may be included in the next block.
 *
 * Transactions are added when they are seen on the network (or created by the
 * local node), but not all transactions seen are added to the pool. For
 * example, the following new transactions may not be added to the mempool:
 * - a transaction which doesn't meet the minimum fee requirements.
 * - a new transaction that double-spends an input of a transaction already in
 * the pool
 * - a non-standard transaction.
 * However, note that the determination of whether a transaction is to be added to
 * the pool is not made in this class.
 *
 * CTxMemPool::mapTx, and CTxMemPoolEntry bookkeeping:
 *
 * mapTx is a boost::multi_index that sorts the mempool on 2 criteria:
 * - transaction hash
 * - time in mempool
 *
 * Note: mapTx will become private and may be modified extensively in the future. It will
 * not be part of the public definition of this class.
 *
 * Note: the term "descendant" refers to in-mempool transactions that depend on
 * this one, while "ancestor" refers to in-mempool transactions that a given
 * transaction depends on.
 *
 * Note: tracking of ancestors and descendants may be removed in the future.
 *
 * Usually when a new transaction is added to the mempool, it has no in-mempool
 * children (because any such children would be an orphan). So in
 * AddUnchecked(), we:
 * - update a new entry's setMemPoolParents to include all in-mempool parents
 * - update the new entry's direct parents to include the new tx as a child
 * - update all ancestors of the transaction to include the new tx's size/fee
 *
 * When a transaction is removed from the mempool, we must:
 * - update all in-mempool parents to not track the tx in setMemPoolChildren
 * - update all ancestors to not include the tx's size/fees in descendant state
 * - update all in-mempool children to not include it as a parent
 *
 * These happen in updateForRemoveFromMempoolNL(). (Note that when removing a
 * transaction along with its descendants, we must calculate that set of
 * transactions to be removed before doing the removal, or else the mempool can
 * be in an inconsistent state where it's impossible to walk the ancestors of a
 * transaction.)
 *
 * In the event of a reorg, the assumption that a newly added tx has no
 * in-mempool children is false.  In particular, the mempool is in an
 * inconsistent state while new transactions are being added, because there may
 * be descendant transactions of a tx coming from a disconnected block that are
 * unreachable from just looking at transactions in the mempool (the linking
 * transactions may also be in the disconnected block, waiting to be added).
 * Because of this, there's not much benefit in trying to search for in-mempool
 * children in AddUnchecked(). Instead, in the special case of transactions
 * being added from a disconnected block, we require the caller to clean up the
 * state, to account for in-mempool, out-of-block descendants for all the
 * in-block transactions by calling UpdateTransactionsFromBlock(). Note that
 * until this is called, the mempool state is not consistent, and in particular
 * mapLinks may not be correct (and therefore functions like
 * CalculateMemPoolAncestorsNL() and CalculateDescendantsNL() that rely on them
 * to walk the mempool are not generally safe to use).
 *
 * Computational limits:
 *
 * Updating all in-mempool ancestors of a newly added transaction can be slow,
 * if no bound exists on how many in-mempool ancestors there may be.
 * CalculateMemPoolAncestorsNL() takes configurable limits that are designed to
 * prevent these calculations from being too CPU intensive.
 *
 * Adding transactions from a disconnected block can be very time consuming,
 * because we don't have a way to limit the number of in-mempool descendants. To
 * bound CPU processing, we limit the amount of work we're willing to do to
 * properly update the descendant information for a tx being added from a
 * disconnected block. If we would exceed the limit, then we instead mark the
 * entry as "dirty", and set the feerate for sorting purposes to be equal the
 * feerate of the transaction without any descendants.
 */
// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions)
class CTxMemPool {
private:
    static constexpr int MAX_NUMBER_OF_TX_TO_VISIT_IN_ONE_GO = 1000;

    //!< Value n means that n times in 2^32 we check.
    // Sanity checks off by default for performance, because otherwise accepting
    // transactions becomes O(N^2) where N is the number of transactions in the
    // pool
    std::atomic_uint32_t nCheckFrequency {0};

    // Flag to temporarily suspend sanity checking
    std::atomic_bool suspendSanityCheck {false};

    std::atomic_uint nTransactionsUpdated {0};
    std::atomic_uint mFrozenTxnUpdatedAt {0};

    // fee that a transaction or a group needs to pay to enter the primary mempool
    CFeeRate blockMinTxfee {DEFAULT_BLOCK_MIN_TX_FEE};
    CFeeRate minDebugRejectionFee {Amount{0}};

    //!< sum of all mempool tx's virtual sizes.
    uint64_t totalTxSize;

    //!< sum of dynamic memory usage of all the map elements (NOT the maps
    //! themselves)
    uint64_t cachedInnerUsage;

    mutable int64_t lastRollingFeeUpdate;
    mutable bool blockSinceLastRollingFeeBump;
    //!< minimum fee to get into the pool, decreases exponentially
    mutable double rollingMinimumFeeRate;

    // Our journal builder
    mutable mining::CJournalBuilder mJournalBuilder;

    // Sub-pool for time locked txns
    CTimeLockedMempool mTimeLockedPool {};

    // The group definition needs access to the mempool index iterator type.
    friend class CPFPGroup;

private:
    // FIXME: DEPRECATED - ultimately this will be changed or removed
    typedef boost::multi_index_container<
        CTxMemPoolEntry, boost::multi_index::indexed_by<
                             // sorted by txid
                             boost::multi_index::hashed_unique<
                                 boost::multi_index::tag<transaction_id>,
                                 mempoolentry_txid, SaltedTxidHasher>,
                             // sorted by entry time
                             boost::multi_index::ordered_non_unique<
                                 boost::multi_index::tag<entry_time>,
                                 boost::multi_index::identity<CTxMemPoolEntry>,
                                 CompareTxMemPoolEntryByEntryTime>,
                             // arranged by insertion order
                             boost::multi_index::sequenced<
                                 boost::multi_index::tag<insertion_order>>>>
        indexed_transaction_set;

    // FIXME: DEPRECATED - ultimately this will be changed or removed
    mutable std::shared_mutex smtx;
    // FIXME: DEPRECATED - ultimately this will be changed or removed
    indexed_transaction_set mapTx;

    int64_t halflife_{MAX_ROLLING_FEE_HALFLIFE};

    using txiter = indexed_transaction_set::index<transaction_id>::type::const_iterator;

    struct CompareIteratorByHash {
        bool operator()(const txiter &a, const txiter &b) const {
            return a->GetTxId() < b->GetTxId();
        }
    };

    class SaltedTxiterHasher : private SaltedTxidHasher {
    public:
        size_t operator()(const txiter& entry) const {
            return SaltedTxidHasher::operator()(entry->GetTxId());
        }
    };

    using setEntries = std::unordered_set<txiter, SaltedTxiterHasher>;
     
    class InsrtionOrderComparator
    {
    public:
        bool operator() (const txiter& lhs, const txiter& rhs) const
        { 
            return lhs->GetInsertionIndex() < rhs->GetInsertionIndex(); 
        }
    };
    // when inserting transaction to mempool, all of its mempool parents must be already inserted and none of its children should be inserted.
    // transaction which is inserted later has larger insertion index. thus if we sort a sequence by insertion index it will be topo sorted
    using setEntriesTopoSorted = std::set<txiter, InsrtionOrderComparator>;

    const setEntries &GetMemPoolParentsNL(txiter entry) const;
    const setEntries &GetMemPoolChildrenNL(txiter entry) const;

    struct TxLinks {
        setEntries parents;
        setEntries children;
    };


    using txlinksMap = std::unordered_map<txiter, TxLinks, SaltedTxiterHasher>;
    txlinksMap mapLinks;

    void updateParentNL(txiter entry, txiter parent, bool add);
    void updateChildNL(txiter entry, txiter child, bool add);

    struct OutpointTxPair
    {
        COutPoint outpoint;
        txiter spentBy;
    };

    using MapNextTx =
    boost::multi_index_container<
        OutpointTxPair,
        boost::multi_index::indexed_by<
            boost::multi_index::hashed_unique<
                boost::multi_index::tag<by_prevout>,
                boost::multi_index::member<
                    OutpointTxPair,
                    COutPoint,
                    &OutpointTxPair::outpoint
                >,
                SaltedOutpointHasher
            >,
            boost::multi_index::hashed_non_unique<
                boost::multi_index::tag<by_txiter>,
                boost::multi_index::member<
                    OutpointTxPair,
                    txiter,
                    &OutpointTxPair::spentBy
                >,
                SaltedTxiterHasher
            >
        >
    >;

    MapNextTx mapNextTx;

    std::map<uint256, Amount> mapDeltas;

    class InsertionIndex
    {
        uint64_t nextIndex = 1;
    public:
        uint64_t GetNext()
        {
            return nextIndex++;
        }
    } insertionIndex;

    // The eviction tracker must be declared after mapLinks because it refers to
    // it and must be destroyed first.
    friend class CEvictionCandidateTracker;
    std::shared_ptr<CEvictionCandidateTracker> evictionTracker;
    static int64_t evaluateEvictionCandidateNL(txiter entry);

    // Shortcuts for eviction tracking. Must be called with the mempool locked.
    void TrackEntryAdded(CTxMemPool::txiter entry);
    void TrackEntryRemoved(const TxId& txId, const setEntries& immediateParents);
    void TrackEntryModified(CTxMemPool::txiter entry);

    std::vector<COutPoint> GetOutpointsSpentByNL(CTxMemPool::txiter entry) const;

    class SecondaryMempoolStats {
        // Primary and secondary mempool sizes are tracked separately.
        uint64_t secondaryMempoolSize;

        //!< sum of transaction sizes of secondary mempool map elements (NOT the maps
        //! themselves)
        uint64_t cachedSecondaryInnerUsage;

        public:
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
        SecondaryMempoolStats()
        {
            Clear();
        }
        void Clear() {
            secondaryMempoolSize = 0;
            cachedSecondaryInnerUsage = 0;
        }
        void Add(const txiter& entryIt) {
            secondaryMempoolSize++;
            cachedSecondaryInnerUsage += entryIt->DynamicMemoryUsage();
        }
        void Remove(const txiter& entryIt) {
            secondaryMempoolSize--;
            cachedSecondaryInnerUsage -= entryIt->DynamicMemoryUsage();
        }
        unsigned long Size() const { return secondaryMempoolSize; }
        uint64_t InnerUsage() const { return cachedSecondaryInnerUsage; }
    };
    SecondaryMempoolStats secondaryMempoolStats;


public:
    /** Create a new CTxMemPool. */
    CTxMemPool();
    ~CTxMemPool();

    constexpr static int MIN_ROLLING_FEE_HALFLIFE = 60 * 30;
    constexpr static int MAX_ROLLING_FEE_HALFLIFE = 60 * 60 * 12;

    int64_t GetRollingMinFee() const { return halflife_; }
    bool SetRollingMinFee(int64_t fee);

    /**
     * If sanity-checking is turned on, check makes sure the pool is consistent
     * (does not contain two transactions that spend the same inputs, all inputs
     * are in the mapNextTx array, journal is in agreement with mempool).
     * If sanity-checking is turned off, check does nothing.
     */
    void CheckMempool(
        CoinsDB& db,
        const mining::CJournalChangeSetPtr& changeSet) const;

    std::string CheckJournal() const;

    void SetSanityCheck(double dFrequency = 1.0);
    void SuspendSanityCheck() { suspendSanityCheck.store(true); }
    void ResumeSanityCheck() { suspendSanityCheck.store(false); }

    // NOLINTNEXTLINE(performance-unnecessary-value-param)
    void SetBlockMinTxFee(CFeeRate feerate) { blockMinTxfee = feerate; };
    CFeeRate GetBlockMinTxFee() const { return blockMinTxfee; };
    // NOLINTNEXTLINE(performance-unnecessary-value-param)
    void SetMinDebugRejectionFee(CFeeRate feerate) { minDebugRejectionFee = feerate; };
    CFeeRate GetMinDebugRejectionFee() const { return minDebugRejectionFee; };

    /** Rebuilds the mempool by reseting it and then resubmitting transactions that were inside before.
        As consequence the journal is rebuilt. The caller must apply the changeset. */
    mining::CJournalChangeSetPtr RebuildMempool();

    // AddUnchecked must update the state for all ancestors of a given
    // transaction, to track size/count of descendant transactions.
    void AddUnchecked(
            const uint256 &hash,
            const CTxMemPoolEntry &entry,
            const TxStorage txStorage,
            const mining::CJournalChangeSetPtr& changeSet,
            size_t* pnPrimaryMempoolSize = nullptr,
            size_t* pnSecondaryMempoolSize = nullptr,
            size_t* pnDynamicMemoryUsage = nullptr);

private:
    // Check the contents of the mempool transaction database against the mempool.
    // hardErrors=true means the implementation will assert() on error.
    bool CheckMempoolTxDBNL(bool hardErrors = true) const;

    // walks through ancestors and collect all that are still in the secondary mempool
    // payingTx will be includes in this set also
    setEntriesTopoSorted GetSecondaryMempoolAncestorsNL(CTxMemPool::txiter payingTx) const;

    // it will calculate grouping data for the actual group, used when a group is accepted
    SecondaryMempoolEntryData FillGroupingDataNL(const CTxMemPool::setEntriesTopoSorted& groupMembers) const;

    // checks groupingData if ti pays enough to enter the primary mempool
    bool IsPayingEnough(const SecondaryMempoolEntryData& groupingData) const;

    // calculate grouping data for the given entry based on it's direct parents, does not modify entry
    SecondaryMempoolEntryData CalculateSecondaryMempoolData(txiter entryIt) const;

    // modifies entry by adding grouping data
    void SetGroupingDataNL(txiter entryIt, std::optional<SecondaryMempoolEntryData> groupingData);

    // forms a group out of groupMembers, modifys mempool entry (remove grouping data and create group object) and adds
    // changes to the changeSet
    void AcceptSingleGroupNL(const setEntriesTopoSorted& groupMembers, mining::CJournalChangeSet& changeSet);
    
    // result of the UpdateEntryGroupingDataNL
    // designates what kind of change took place
    enum class ResultOfUpdateEntryGroupingDataNL
    {
        NOTHING,                         // entry is not modified
        GROUPING_DATA_MODIFIED,          // grouping data is modified, children needs to be updated
        ADD_TO_PRIMARY_STANDALONE,       // grouping data is deleted as the entry should be accepted to the primary mempool
        ADD_TO_PRIMARY_GROUP_PAYING_TX   // grouping data is modified, entry is detected as paying transaction for the group
    };

    // recalculates grouping data for the given entry, entry must not be in the primary mempool
    ResultOfUpdateEntryGroupingDataNL UpdateEntryGroupingDataNL(CTxMemPool::txiter iter);

    // tries to accept transactions to primary mempool. it recalculates grouping data, forms groups or accepts transactions as standalone
    // this may result that some other transaction, which are not in the toUpdate, to be updated and accepted to primary mempool
    void TryAcceptToPrimaryMempoolNL(CTxMemPool::setEntriesTopoSorted toUpdate, mining::CJournalChangeSet& changeSet, bool limitTheLoop);

    // tries to accept entry to the primary mempool, may result in accepting other transactions (i.e. if the entry is groups paying tx)
    void TryAcceptChildlessTxToPrimaryMempoolNL(CTxMemPool::txiter entry, mining::CJournalChangeSet& changeSet);

    // removes transactions from the primary mempool, together with all descedants. if we happen to remove a group then the group is disbanded
    // and all its members are removed, together with all their children. 
    //   -if the entries are expected to be revisited later one can set putDummyGroupingData to false to avoid recalculation of the grouping data
    //   -one can set which entries can be ignored when disbanding groups by setting variable "entriesToIgnore"
    setEntriesTopoSorted RemoveFromPrimaryMempoolNL(setEntriesTopoSorted toRemove, mining::CJournalChangeSet& changeSet, 
        bool putDummyGroupingData = false, const setEntries* entriesToIgnore = nullptr);

    struct ResubmitContext {
        indexed_transaction_set oldMapTx {};
        std::unordered_map<TxId, std::vector<COutPoint>, SaltedTxidHasher> outpointsSpentByStored {};
    };
    // prepare resubmit context
    ResubmitContext PrepareResubmitContextAndClearNL(const mining::CJournalChangeSetPtr& changeSet);
    // in AddToMempoolForReorg and RebuildMempool we need to submit transactions that already were in the mempool
    void ResubmitEntriesToMempoolNL(ResubmitContext& resubmitContext, const mining::CJournalChangeSetPtr& changeSet);

    // walks recursively through all descedants of the items in the set and updates theirs ancestorsCouunt
    void UpdateAncestorsCountNL(setEntriesTopoSorted entries);


    void RemoveFrozenNL(const mining::CJournalChangeSetPtr& changeSet);
    void RemoveInvalidCTXsNL(const mining::CJournalChangeSetPtr& changeSet);

public:
    void RemoveForBlock(
            const std::vector<CTransactionRef> &vtx,
            const mining::CJournalChangeSetPtr& changeSet,
            const uint256& blockhash,
            std::vector<CTransactionRef>& txNew,
            const Config& config);

    /**
    * Returns the result of checking if the received block is selfishly mined.
    * Block's txs are compared with mempool's txs. If the time difference between
    * the last transaction that was included in the block and the last transaction
    * that was left in the mempool is more than time difference selfish threshold
    * (-minblockmempooltimedifferenceselfish), then it is checked if there are
    * txs in mempool that were not included in the block.
    * All txs that were not included are checked against block fee rate.
    * If percentage of those txs in mempool that are not included
    * in received block is above threshold (-selfishtxpercentthreshold),
    * the block is considered as selfishly mined. The results are logged.
    * Functionality is enabled with the -detectselfishmining parameter
    * set to true (by default).
    */
    bool CheckSelfishNL(const setEntries& block_entries,
                        int64_t last_block_tx_time,
                        const CFeeRate& minBlockFeeRate, 
                        const Config& config);


    void RemoveFrozen(const mining::CJournalChangeSetPtr& changeSet);

    /**
     * Remove all confiscation transactions that are no longer valid (i.e. not whitelisted).
     */
    void RemoveInvalidCTXs(const mining::CJournalChangeSetPtr& changeSet);

    void Clear();

    void QueryHashes(std::vector<uint256> &vtxid);
    bool IsSpent(const COutPoint &outpoint);
    CTransactionWrapperRef IsSpentBy(const COutPoint &outpoint) const;

    unsigned int GetTransactionsUpdated() const;
    unsigned int GetFrozenTxnUpdatedAt() const {return mFrozenTxnUpdatedAt;}
    void AddTransactionsUpdated(unsigned int n);

    /**
     * Check that none of this transactions inputs are in the mempool, and thus
     * the tx is not dependent on other mempool transactions to be included in a
     * block.
     */
    bool HasNoInputsOf(const CTransaction &tx) const;

    /** Affect CreateNewBlock prioritisation of transactions */
    void PrioritiseTransaction(
            const uint256& hash,
            const std::string& strHash,
            const Amount nFeeDelta);
    void PrioritiseTransaction(
            const std::vector<TxId>& vTxToPrioritise,
            const Amount nFeeDelta);

    void ApplyDeltas(
            const uint256& hash,
            Amount &nFeeDelta) const;

    // Get a reference to the journal builder
    mining::CJournalBuilder& getJournalBuilder() { return mJournalBuilder; }

    // Get a reference to the time-locked (non-final txn) mempool
    CTimeLockedMempool& getNonFinalPool() { return mTimeLockedPool; }

    /**
     * Execute callback function on coins that are unspent in view and in mempool.
     * Callback parameters: coin and consecutive index of view outpoints
     */
    void OnUnspentCoinsWithScript(
        const CoinsDBView& tip,
        const std::vector<COutPoint>& outpoints,
        const std::function<void(const CoinWithScript&, size_t)>& callback) const;

private:
    // Mempool transaction database
    std::once_flag db_initialized {};
    std::shared_ptr<CAsyncMempoolTxDB> mempoolTxDB {nullptr};

    // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
    static std::atomic_int mempoolTxDB_uniqueInit;
    int mempoolTxDB_uniqueSuffix {mempoolTxDB_uniqueInit.fetch_add(1)};

    // Safely opens the transaction database
    void OpenMempoolTxDB(const bool clearDatabase = false);

    // This is the workhorse behind InitMempoolTxDB() and friends.
    void DoInitMempoolTxDB();

    // These are the default properties of a mempool that are used by
    // OpenMempoolTxDB() without a prior call to one of the InitMempoolTxDB()
    // variants, which is most of the time during tests:

    //   - use an in-memory transaction database
    bool mempoolTxDB_inMemory {true};

    //   - use a uniquified name for the transaction database
    bool mempoolTxDB_unique {true};

    // Like InitMempoolTxDB(), but uses a unique name for the on-disk database.
    void InitUniqueMempoolTxDB();

    // Like InitMempoolTxDB(), but uses an in-memory database.
    void InitInMemoryMempoolTxDB();

public:
    // Called at process init, opens the transaction database and checks its
    // cross-reference with mempool.dat.
    void InitMempoolTxDB();
    uint64_t GetDiskUsage();
    uint64_t GetDiskTxCount();
    void SaveTxsToDisk(uint64_t requiredSize);

    // May be called during validation.
    // The transaction must not exist in the mempool.
    void RemoveTxFromDisk(const CTransactionRef& transaction);

    /**
     * Remove a set of transactions from the mempool. If a transaction is in
     * this set, then all in-mempool descendants must also be in the set, unless
     * this transaction is being removed for being in a block. Set
     * updateDescendants to true when removing a tx that was in a block, so that
     * any in-mempool descendants have their ancestor state updated.
     */
    void
    RemoveStaged(
        setEntries &stage,
        bool updateDescendants,
        const mining::CJournalChangeSetPtr& changeSet,
        MemPoolRemovalReason reason = MemPoolRemovalReason::UNKNOWN);

    /**
     * When adding transactions from a disconnected block back to the mempool,
     * new mempool entries may have children in the mempool (which is generally
     * not the case when otherwise adding transactions).
     * UpdateTransactionsFromBlock() will find child transactions and update the
     * descendant state for each transaction in hashesToUpdate (excluding any
     * child transactions present in hashesToUpdate, which are already accounted
     * for).  Note: hashesToUpdate should be the set of transactions from the
     * disconnected block that have been accepted back into the mempool.
     */
    void UpdateTransactionsFromBlock(
            const std::vector<uint256> &hashesToUpdate);
    /**
     * Check if @a entry and its ancestors in the mempool conform to the
     * provided limits (these are all calculated including @a entry itself):
     * @param limitAncestorCount   max number of ancestors
     * @param limitSecondaryMempoolAncestorCount    max size of ancestors which are in the secondary mempool
     *
     * Returns @c true if the set of ancestors is within the limits. Otherwise,
     * if @a errString is provided, it is populated with the failure reason.
     */
    bool CheckAncestorLimits(
        const CTxMemPoolEntry& entry,
        uint64_t limitAncestorCount,
        uint64_t limitSecondaryMempoolAncestorCount,
        std::optional<std::reference_wrapper<std::string>> errString) const;

    /**
     * The minimum fee to get into the mempool, which may itself not be enough
     * for larger-sized transactions.
     */
    CFeeRate GetMinFee(size_t sizelimit) const;

    /**
     * Remove transactions from the mempool until its dynamic size is <=
     * sizelimit. pvNoSpendsRemaining, if set, will be populated with the list
     * of outpoints which are not in mempool which no longer have any spends in
     * this mempool. Return TxIds which were removed (if pvNoSpendsRemaining is set).
     */
    std::vector<TxId> TrimToSize(
        size_t sizelimit,
        const mining::CJournalChangeSetPtr& changeSet,
        std::vector<COutPoint> *pvNoSpendsRemaining = nullptr);

    /** Expire all transaction (and their dependencies) in the mempool older
     * than time. Return the number of removed transactions. */
    int Expire(int64_t time, const mining::CJournalChangeSetPtr& changeSet);

    /** Remove transactions from the mempool when discarding minerid/dataref transactions for
     * blocks after the next block.
     * This is  needed for replacement mineridinfo transactions and for removing minerid/dataref txns during reorg*/
    int RemoveTxAndDescendants(const TxId & txid, const mining::CJournalChangeSetPtr& changeSet);
    std::vector<TxId> RemoveTxnsAndDescendants(const std::vector<TxId>& txid, const mining::CJournalChangeSetPtr& changeSet);


    /**
     * Check for conflicts with in-mempool transactions.
     * @param tx A reference to the given txn
     * @param nonFinal A flag to indicate if tx is a non-final transaction
     */
    std::set<CTransactionRef> CheckTxConflicts(const CTransactionRef& tx, bool isFinal) const;

    /** Returns false if the transaction is in the mempool and not within the
     * chain limit specified. */
    bool TransactionWithinChainLimit(const uint256 &txid, int64_t maxAncestorCount, 
                                     int64_t maxSecondaryMempoolAncestorCount) const;

    unsigned long Size();

    uint64_t GetTotalTxSize();

    bool Exists(const uint256& hash) const;
    // A non-locking version of Exists
    // FIXME: DEPRECATED - this will become private and ultimately changed or removed
    bool ExistsNL(const uint256& hash) const;

    bool Exists(const COutPoint &outpoint) const;
    // A non-locking version of Exists
    // FIXME: DEPRECATED - this will become private and ultimately changed or removed
    bool ExistsNL(const COutPoint &outpoint) const;

    CTransactionRef Get(const uint256& hash) const;
    // A non-locking version of Get
    // FIXME: DEPRECATED - this will become private and ultimately changed or removed
    CTransactionRef GetNL(const uint256& hash) const;

    TxMempoolInfo Info(const uint256& hash) const;

    std::vector<TxMempoolInfo> InfoAll() const;

    size_t DynamicMemoryUsage() const;
    size_t SecondaryMempoolUsage() const;

    CFeeRate estimateFee() const;

    boost::signals2::signal<void(const CTransactionWrapper&)> NotifyEntryAdded;
    boost::signals2::signal<void(const CTransactionWrapper&, MemPoolRemovalReason)>
        NotifyEntryRemoved;

    void ClearPrioritisation(const uint256 &hash);
    void ClearPrioritisation(const std::vector<TxId>& vTxIds);

private:
    /**
     * Retreive mempool data needed by DumpMempool().
     */
    void GetDeltasAndInfo(std::map<uint256, Amount>& deltas, std::vector<TxMempoolInfo>& info) const;

private:
    // A non-locking version of InfoAll
    std::vector<TxMempoolInfo> InfoAllNL() const;

    /**
     * Checks mempool chain limits by finding parents of the entry
     * summing ancestorsCount for all of them
     *
     * Assumes that @a entry may not be in the mampool, so it searches the
     * transaction's @c vin for in-mempool parents.
     */
    bool CheckAncestorLimitsNL(
        const CTxMemPoolEntry& entry,
        uint64_t limitAncestorCount,
        uint64_t limitSecondaryMempoolAncestorCount,
        std::optional<std::reference_wrapper<std::string>> errString) const;


    /**
     * Try to calculate all in-mempool ancestors of the entry.
     *
     * Returns the ancestors in @a setAncestors.
     *
     * Assumes that @a entryIter is in the mempool and it can look up parents
     * from #mapLinks.
     */
    void GetMemPoolAncestorsNL(
        const txiter& entryIter,
        setEntries& setAncestors) const;


    /**
     * Populate setDescendants with all in-mempool descendants of hash.
     * Assumes that setDescendants includes all in-mempool descendants of
     * anything already in it.
     */
    void GetDescendantsNL(
        txiter it,
        setEntries &setDescendants) const;

    // Non-locking, returns the size of the primary mempool.
    unsigned long PrimaryMempoolSizeNL() const;

public:
    /** \class CTxMemPool::Snapshot
     *
     * CTxMemPool::Snapshot contains a read-only snapshot-in-time of the (partial)
     * contents of the mempool.
     *
     * The snapshot contains two things: a copy of a number of mempool entries,
     * and a set of additional relevant transaction IDs. Which transaction IDs
     * are relevant depends on how the snapshot was created (for example, see
     * CTxMemPool::GetTxSnapshot()).
     *
     * @note This class is non-moveable and non-copyable.
     */
    // NOLINTNEXTLINE(cppcoreguidelines-special-member-functions)
    class Snapshot final
    {
        // Only CTxMemPool is allowed to call the constructor.
        friend class CTxMemPool;

        using Contents = std::vector<CTxMemPoolEntry>;
        using CachedTxIds = std::vector<TxId>;
        using CachedTxIdsRef = std::unique_ptr<CachedTxIds>;

        /**
         * The default constructor returns an invalid snapshot:
         * IsValid() will return @c false.
         */
        explicit Snapshot() = default;

        /**
         * Creates a copy of the mempool with the given @a contents and an
         * optional set of transaction IDs that will be used for TxIdExists()
         * checks.
         */
        explicit Snapshot(Contents&& contents,
                          CachedTxIdsRef&& relevantTxIds);

        Snapshot(Snapshot&&) = delete;
        Snapshot(const Snapshot&) = delete;

    public:
        using size_type = Contents::size_type;
        using value_type = Contents::value_type;
        using const_iterator = Contents::const_iterator;

        bool empty() const noexcept { return mContents.empty(); }
        size_type size() const noexcept { return mContents.size(); }

        const_iterator begin() const noexcept { return mContents.begin(); }
        const_iterator cbegin() const noexcept { return begin(); }

        const_iterator end() const noexcept { return mContents.end(); }
        const_iterator cend() const noexcept { return end(); }

        /// Returns an immutable iterator to a mempool entry in the snapshot
        /// contents, or cend() if no such entry exists.
        const_iterator find(const uint256& hash) const;

        /// Checks if @a hash exists in the snapshot, even if the mempool entry
        /// for that hash is not actually in the snapshot contents. May return
        /// @c true when find() for the same hash returns cend().
        bool TxIdExists(const uint256& hash) const;

        /// Checks whether the contents are valid.
        bool IsValid() const noexcept { return mValid; };
        operator bool() const noexcept { return IsValid(); }

    private:
        const bool mValid {false};
        const Contents mContents{};
        const CachedTxIdsRef mRelevantTxIds{};

        // The transaction lookup index.
        using TxIdIndex = std::unordered_map<uint256, Snapshot::const_iterator>;
        mutable TxIdIndex mIndex{};
        mutable std::once_flag mCreateIndexOnce{};
        void CreateIndex() const;
    };

    /**
     * Returns a read-only snapshot of the mempool contents. This will copy all
     * the mempool entries.
     */
    Snapshot GetSnapshot() const;
    
    /**
     * Retreival modes for GetTxSnapshot().
     */
    enum class TxSnapshotKind
    {
        /// Retreive only one transaction.
        SINGLE,
        /// Retreive the transaction and all its ancestors.
        TX_WITH_ANCESTORS,
        /// Retreive only the transaction's ancestors.
        ONLY_ANCESTORS,
        /// Retreive the transaction and all its descendants.
        TX_WITH_DESCENDANTS,
        /// Retreive only the transaction's descendants.
        ONLY_DESCENDANTS
    };

    /**
     * Returns a read-only snapshot of the mempool for one entry, identified by
     * @a hash, and/or optionally its ancestors or descendants, selected by
     * @a kind. This will also fill the transaction ID cache with the inputs of
     * the copied mempool entries (Snapshot#TxIdExists() will return @c true for
     * the hashes of those input transactions even if the entries are not in the
     * snapshot).
     *
     * If a transaction with the given @a hash does not exist, the returned
     * snapshot will be invalid, that is: Snapshot#IsValid() will return @c false.
     */
    Snapshot GetTxSnapshot(const uint256& hash, TxSnapshotKind kind) const;


    /**
     * Returns shared references to all the transactions in the mempool, without
     * the mempool entries. The transactions are not indexed and are returned in
     * an unpredictable order.
     */
    std::vector<CTransactionRef> GetTransactions() const;


    /**
     * Make mempool consistent after a reorg, by re-adding
     * disconnected block transactions from the mempool, and also removing any other
     * transactions from the mempool that are no longer valid given the new
     * tip/height, most notably coinbase transactions and their descendants.
     *
     * Note: we assume that disconnectpool only contains transactions that are NOT
     * confirmed in the current chain nor already in the mempool (otherwise,
     * in-mempool descendants of such transactions would be removed).
     *
     * Note: this function currently still runs under `cs_main` and does *not*
     * lock mempool. It calls mempool-locking functions internally, and
     * not their `NL` variants. Therefore it should *not* have the `NL` postfix.
     */
    void AddToMempoolForReorg(
        const Config &config,
        DisconnectedBlockTransactions &disconnectpool,
        const mining::CJournalChangeSetPtr& changeSet);

    /**
     * Make mempool consistent after a reorg, by recursively erasing
     * disconnected block transactions from the mempool
     *
     * Note: this function currently still runs under `cs_main` and does *not*
     * lock mempool. It calls mempool-locking functions internally, and
     * not their `NL` variants. Therefore it should *not* have the `NL` postfix.
     */
    void RemoveFromMempoolForReorg(
        const Config &config,
        DisconnectedBlockTransactions &disconnectpool,
        const mining::CJournalChangeSetPtr& changeSet);

    /**
     * Add vtx to disconnectpool observing the limit. block transactions that do not make it
     * into disconnectpool need to have their descendants removed from mempool, too
     */
    void AddToDisconnectPoolUpToLimit(
        const mining::CJournalChangeSetPtr& changeSet,
        DisconnectedBlockTransactions* disconnectpool,
        uint64_t maxDisconnectedTxPoolSize,
        const CBlock& block,
        int32_t height);

private:
    // A non-locking version of CheckMempool
    void CheckMempoolNL(
            CoinsDBView& view,
            const mining::CJournalChangeSetPtr& changeSet) const;

    // The implementation of CheckMempool
    void CheckMempoolImplNL(
            CoinsDBView& view,
            const mining::CJournalChangeSetPtr& changeSet) const;

    // common condition for CheckMempool and CheckMempoolNL
    bool ShouldCheckMempool() const;

    // A non-locking version of IsSpent
    bool IsSpentNL(const COutPoint &outpoint) const;

    void RemoveForReorgNL(
        const Config &config,
        const CoinsDB& coinsTip,
        const mining::CJournalChangeSetPtr& changeSet,
        const CBlockIndex& tip,
        int flags);


    // A non-locking version of AddUnchecked
    // A signal NotifyEntryAdded is decoupled from AddUncheckedNL.
    // It needs to be called explicitly by a user if AddUncheckedNL is used.
    using SpentOutputs = std::optional<std::reference_wrapper<const std::vector<COutPoint>>>;
    void AddUncheckedNL(
            const uint256& hash,
            const CTxMemPoolEntry &entry,
            const TxStorage txStorage,
            const mining::CJournalChangeSetPtr& changeSet,
            SpentOutputs spentOutputs = std::nullopt,
            size_t* pnPrimaryMempoolSize = nullptr,
            size_t* pnSecondaryMempoolSize = nullptr,
            size_t* pnDynamicMemoryUsage = nullptr);

    // A non-locking version of ApplyDeltas
    void ApplyDeltasNL(
        const uint256& hash,
        Amount &nFeeDelta) const;

    /**
     * Update ancestors of hash to add/remove it as a descendant transaction.
     */
    void updateAncestorsOfNL(
            bool add,
            txiter hash);

    // typesafe default for conflictedWith parameter of remove* functions
    const CTransactionConflict noConflict {};

    /**
     * Before calling removeUnchecked for a given transaction,
     * updateForRemoveFromMempoolNL must be called on the entire (dependent) set
     * of transactions being removed at the same time. We use each
     * CTxMemPoolEntry's setMemPoolParents in order to walk ancestors of a given
     * transaction that is removed, so we can't remove intermediate transactions
     * in a chain before we've updated all the state for the removal.
     */
    void removeUncheckedNL(
            const setEntries& entries,
            mining::CJournalChangeSet& changeSet,
            const CTransactionConflict& conflictedWith,
            MemPoolRemovalReason reason = MemPoolRemovalReason::UNKNOWN);

    void clearNL(bool skipTransactionDatabase = false);

    void trackPackageRemovedNL(const CFeeRate &rate, bool haveSecondaryMempoolTxs);

    /**
     * Remove a set of transactions from the mempool. If a transaction is in
     * this set, then all in-mempool descendants must also be in the set. 
     */
    void removeStagedNL(
            setEntries& stage,
            mining::CJournalChangeSet& changeSet,
            const CTransactionConflict& conflictedwith,
            MemPoolRemovalReason reason = MemPoolRemovalReason::UNKNOWN);

    void prioritiseTransactionNL(
            const uint256& hash,
            const Amount nFeeDelta);

    void clearPrioritisationNL(const uint256& hash);

    // A non-locking version of RemoveRecursive
    void removeRecursiveNL(
            const CTransaction& origTx,
            const mining::CJournalChangeSetPtr& changeSet,
            const CTransactionConflict& conflictedWith,
            MemPoolRemovalReason reason = MemPoolRemovalReason::UNKNOWN);

    // A non-locking version of checkJournal
    std::string checkJournalNL() const;

    // A non-locking estimate of usage of mempool indexes without actual transactions
    size_t DynamicMemoryIndexUsageNL() const;

    // A non-locking version of DynamicMemoryUsage.
    size_t DynamicMemoryUsageNL() const;

    // A non-locking version of SecondaryMempoolUsage.
    size_t SecondaryMempoolUsageNL() const;

    // Dumping and loading the mempool.
    using DumpFileID = boost::uuids::uuid;
    UniqueCFile OpenDumpFile(uint64_t& version, DumpFileID& instanceId);

    // Mempool dump and load for testing different file formats
    // and custion validation.
    void DumpMempool(uint64_t version);
    bool LoadMempool(const Config &config,
                     const task::CCancellationToken& shutdownToken,
                     const std::function<CValidationState(
                         const TxInputDataSPtr& txInputData,
                         const mining::CJournalChangeSetPtr& changeSet,
                         bool limitMempoolSize)>& processValidation);

public:
    // Allow access to some mempool internals from unit tests.
    template<typename X> struct UnitTestAccess;

    /** Dump the mempool to disk. */
    void DumpMempool();

    /** Load the mempool from disk. */
    bool LoadMempool(const Config &config, const task::CCancellationToken& shutdownToken);
};

// Group definition in the secondary mempool.
class CPFPGroup
{
    SecondaryMempoolEntryData evaluationParams {};

    // Transactions are topologically sorted.
    std::vector<CTxMemPool::txiter> transactions {};

    // txid of the groups paying transaction
    TxId payingTxId {};

    // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
    static std::atomic<uint64_t> counter;
    mining::GroupID groupId {};

public:
    // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved)
    explicit CPFPGroup(SecondaryMempoolEntryData evalParams, std::vector<CTxMemPool::txiter>&& txs)
    // NOLINTNEXTLINE(performance-unnecessary-value-param)
        : evaluationParams{evalParams}
        , transactions{txs}
        , groupId{counter++}
    {
        // NOLINTNEXTLINE(cppcoreguidelines-prefer-member-initializer)
        payingTxId = transactions.back()->GetTxId();
    }

    const TxId& PayingTransactionId() const { return payingTxId; }
    mining::GroupID Id() const { return groupId; }
    const SecondaryMempoolEntryData& EvaluationParams() const { return evaluationParams; }
    const std::vector<CTxMemPool::txiter>& Transactions() const { return transactions; }
};


/**
 * ICoinsView that brings transactions from a memorypool into view.
 * If a transaction is in mempool this class will return an unspent coin even
 *
 * - If GetCoin/HaveCoin finds a coin  it will always return it in subsequent calls.
 * - If GetCoin/HaveCoin does NOT return a coin on first call it can still return
 *   them on later calls - coins can be added to mempool when transaction are added).
 * - If GetCoin/HaveCoin return coin from a transaction that is stored inside
 *   mempool for one outpoint it is guaranteed that it will also return coins
 *   for all the other outpoints that exist inside that transaction.
 * - GetCoin/HaveCoin return true even if the coin is spend by another transaction
 *   in the mempool (This is different from implementation of provider used by
 *   pcoinsTip as there we cant have a parent and a child coin present in the database
 *   at the same time). The final check against coins that are spent in mempool
 *   is done by CTxMemPool::CheckTxConflicts which is called from two places:
 *   + TxnValidation (this is preliminary check, subject to race conditions)
 *   + CTxnDoubleSpendDetector::insertTxnInputs  - this is final check, executed
 *     while holding double spend detector lock
 */
// NOLINTNEXTLINE(cppcoreguidelines-virtual-class-destructor)
class CCoinsViewMemPool : public ICoinsView {
private:
    const CTxMemPool &mempool;

public:
    CCoinsViewMemPool(const CoinsDBView& DBView, const CTxMemPool &mempoolIn);

    /**
     * Returns cached transaction reference.
     * In case the reference is not found in cache it tries to load it from the
     * underlying mempool.
     */
    CTransactionRef GetCachedTransactionRef(const COutPoint& outpoint) const;

    std::optional<CoinWithScript> GetCoinWithScript(const COutPoint& outpoint) const
    {
        auto coinData = GetCoin(outpoint, std::numeric_limits<size_t>::max());
        if(coinData.has_value())
        {
            assert(coinData->HasScript());

            return std::move(coinData.value());
        }

        return {};
    }

    std::optional<Coin> GetCoinFromDB(const COutPoint& outpoint) const;

    std::optional<CoinImpl> GetCoin(const COutPoint &outpoint, uint64_t maxScriptSize) const override;

    void CacheAllCoins(const std::vector<CTransactionRef>& txns) const override;

protected:
    uint256 GetBestBlock() const override;

private:
    const CoinsDBView& mDBView;

    mutable std::mutex mMutex;
    mutable std::map<TxId, CTransactionRef> mCache;
};

/**
 * DisconnectedBlockTransactions

 * During the reorg, it's desirable to re-add previously confirmed transactions
 * to the mempool, so that anything not re-confirmed in the new chain is
 * available to be mined. However, it's more efficient to wait until the reorg
 * is complete and process all still-unconfirmed transactions at that time,
 * since we expect most confirmed transactions to (typically) still be
 * confirmed in the new chain, and re-accepting to the memory pool is expensive
 * (and therefore better to not do in the middle of reorg-processing).
 * Instead, store the disconnected transactions (in order!) as we go, remove any
 * that are included in blocks in the new chain, and then process the remaining
 * still-unconfirmed transactions at the end.
 */

// multi_index tag names
struct txid_index {};

// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions)
struct DisconnectedBlockTransactions {
    typedef boost::multi_index_container<
        CTransactionRef, boost::multi_index::indexed_by<
                             // sorted by txid
                             boost::multi_index::hashed_unique<
                                 boost::multi_index::tag<txid_index>,
                                 mempoolentry_txid, SaltedTxidHasher>,
                             // sorted by order in the blockchain
                             boost::multi_index::sequenced<
                                 boost::multi_index::tag<insertion_order>>>>
        indexed_disconnected_transactions;

    // It's almost certainly a logic bug if we don't clear out queuedTx before
    // destruction, as we add to it while disconnecting blocks, and then we
    // need to re-process remaining transactions to ensure mempool consistency.
    // For now just log it as an ERROR since we don't want to loose stack trace
    // in case of an exception and loosing some transactions from mempool is not
    // a critical error.
    ~DisconnectedBlockTransactions();

private:
    std::set<TxId> filteredTx {};
    indexed_disconnected_transactions queuedTx {};
    uint64_t cachedInnerUsage {0};

    friend class CTxMemPool;
public:
    // Estimate the overhead of queuedTx to be 6 pointers + an allocation, as
    // no exact formula for boost::multi_index_contained is implemented.
    size_t DynamicMemoryUsage() const
    {
        return memusage::MallocUsage(
            sizeof(CTransactionRef) +
            6 * sizeof(void*)) * queuedTx.size() +
            cachedInnerUsage;
    }

    void addTransaction(const CTransactionRef& tx, bool filter)
    {
        queuedTx.insert(tx);
        cachedInnerUsage += RecursiveDynamicUsage(tx);
        if(filter)
        {
            filteredTx.insert(tx->GetId());
        }
    }

    bool isFiltered(const TxId& txid) const
    {
        return (filteredTx.count(txid) > 0);
    }

    // Remove entries based on txid_index, and update memory usage.
    void removeForBlock(const std::vector<CTransactionRef>& vtx)
    {
        // Short-circuit in the common case of a block being added to the tip
        if(queuedTx.empty() && filteredTx.empty())
        {
            return;
        }
        for(auto const& tx : vtx)
        {
            auto txit { queuedTx.find(tx->GetHash()) };
            if(txit != queuedTx.end())
            {
                cachedInnerUsage -= RecursiveDynamicUsage(*txit);
                queuedTx.erase(txit);
            }

            // Remove from filtered set (if it exists)
            filteredTx.erase(tx->GetId());
        }
    }

    // Remove an entry by insertion_order index, and update memory usage.
    void removeEntry(indexed_disconnected_transactions::index<
                     insertion_order>::type::iterator entry)
    {
        // Remove from filtered set (if it exists)
        filteredTx.erase((*entry)->GetId());

        cachedInnerUsage -= RecursiveDynamicUsage(*entry);
        queuedTx.get<insertion_order>().erase(entry);
    }

    void clear()
    {
        cachedInnerUsage = 0;
        queuedTx.clear();
        filteredTx.clear();
    }
};

#endif // BITCOIN_TXMEMPOOL_H
