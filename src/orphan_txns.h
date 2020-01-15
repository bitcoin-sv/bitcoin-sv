// Copyright (c) 2019 The Bitcoin SV developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include "net.h"
#include "primitives/transaction.h"
#include "txn_validation_data.h"

#include <shared_mutex>
#include <vector>
#include <random>

struct COrphanTxnEntry {
    TxInputDataSPtr pTxInputData {nullptr};
    int64_t nTimeExpire {};
    unsigned int size{};
};

struct IterComparator {
    template <typename I> bool operator()(const I &a, const I &b) const {
        return &(*a) < &(*b);
    }
};

struct CTxnIdComparator {
    bool operator ()(const TxInputDataSPtr& lhs, const TxInputDataSPtr& rhs) const {
        return lhs->mpTx->GetId() < rhs->mpTx->GetId();
    }
};

class COrphanTxns;
using OrphanTxnsSPtr = std::shared_ptr<COrphanTxns>;
using CompactExtraTxnsVec = std::vector<std::pair<uint256, CTransactionRef>>;

/**
 * A class created to support orphan txns during validation.
 */
class COrphanTxns {
    /** Expiration time for orphan transactions in seconds */
    static constexpr int64_t ORPHAN_TX_EXPIRE_TIME = 20 * 60;
    /** Minimum time between orphan transactions expire time checks in seconds */
    static constexpr int64_t ORPHAN_TX_EXPIRE_INTERVAL = 5 * 60;

  public:
    /** A default max limit for collected outpoints */
    static constexpr unsigned int DEFAULT_MAX_COLLECTED_OUTPOINTS = 300000;
    /** Default for -maxorphantxssize, maximum size of orphan transactions is 10 MB*/
    static constexpr uint64_t DEFAULT_MAX_ORPHAN_TRANSACTIONS_SIZE = 100 * ONE_MEGABYTE;
    /** Default number of orphan+recently-replaced txn to keep around for block
     *  reconstruction */
    static constexpr unsigned int DEFAULT_BLOCK_RECONSTRUCTION_EXTRA_TXN = 100;

    COrphanTxns(
        size_t maxCollectedOutpoints,
        size_t maxExtraTxnsForCompactBlock,
        size_t maxTxSizePolicy);
    ~COrphanTxns() = default;

    // Forbid copying/assignment
    COrphanTxns(const COrphanTxns&) = delete;
    COrphanTxns(COrphanTxns&&) = delete;
    COrphanTxns& operator=(const COrphanTxns&) = delete;
    COrphanTxns& operator=(COrphanTxns&&) = delete;

    /** Add a new txn*/
    void addTxn(const TxInputDataSPtr& pTxInputData);
    /** Add txn to the block reconstruction queue */
    void addToCompactExtraTxns(const CTransactionRef &tx);
    /** Erase a given txn */
    int eraseTxn(const uint256& hash);
    /** Erase all txns form the given peer */
    void eraseTxnsFromPeer(NodeId peer);
    /** Erase all txn */
    void eraseTxns();
    /** Check if txn exists by prevout */
    bool checkTxnExists(const COutPoint& prevout) const;
    /** Check if txn exists by it's hash */
    bool checkTxnExists(const uint256& txHash) const;
    /** Get txns hash for the given prevout */
    std::vector<uint256> getTxnsHash(const COutPoint& prevout) const;
    /** Get extra transactions needed by block's reconstruction */
    CompactExtraTxnsVec getCompactExtraTxns() const;
    /** Limit a number of orphan transactions size */
    unsigned int limitTxnsSize(uint64_t nMaxOrphanTxnsSize, bool fSkipRndEviction=false);
    /** Collect dependent transactions which might be processed later */
    std::vector<TxInputDataSPtr> collectDependentTxnsForRetry();
    /** Collect txn's outpoints which will be used to find any dependant orphan txn */
    void collectTxnOutpoints(const CTransaction& tx);
    /** Erase collected outpoints */
    void eraseCollectedOutpoints();
    /** Erase collected outpoints from the given txns */
    void eraseCollectedOutpointsFromTxns(const std::vector<TxId>& vRemovedTxIds);
    /** Get a number of orphan transactions queued */
    size_t getTxnsNumber();
    /** Get collected outpoints */
    std::vector<COutPoint> getCollectedOutpoints();
    /** Get a random orphan txn by a lower bound (needed for UTs) */
    TxInputDataSPtr getRndOrphanByLowerBound(const uint256& key);

  private:
    // Private aliasis
    using OrphanTxns = std::map<uint256, COrphanTxnEntry>;
    using OrphanTxnsIter = OrphanTxns::iterator;
    using OrphanTxnsByPrev =
            std::map<COutPoint, std::set<OrphanTxnsIter, IterComparator>>;
    using OrphanTxnsByPrevIter = OrphanTxnsByPrev::iterator;
    /** A non-locking version of addToCompactExtraTxns */
    void addToCompactExtraTxnsNL(const CTransactionRef &tx);
    /** A non-locking version of checkTxnExists */
    bool checkTxnExistsNL(const uint256& txHash) const;
    /** Execute txn's erase (private & not protected by a lock) */
    int eraseTxnNL(const uint256& hash);

    /** Orphan txns recently received */
    OrphanTxns mOrphanTxns;
    OrphanTxnsByPrev mOrphanTxnsByPrev;
    mutable std::shared_mutex mOrphanTxnsMtx {};

    /** Txn outpoints collected and waiting to be used to find any dependant orphan txn */
    std::vector<COutPoint> mCollectedOutpoints {};
    size_t mMaxCollectedOutpoints {};
    mutable std::mutex mCollectedOutpointsMtx {};

    /** Extra txns used by block reconstruction */
    CompactExtraTxnsVec mExtraTxnsForCompact;
    mutable std::shared_mutex mExtraTxnsForCompactMtx {};
    size_t mExtraTxnsForCompactIdx {0};
    size_t mMaxExtraTxnsForCompactBlock {0};
    size_t mMaxStandardTxSize {0};

    /** Control txns limit by a time slot */
    int64_t mNextSweep {0};

    /** A default generator */
    std::default_random_engine mGenerator {};
};
