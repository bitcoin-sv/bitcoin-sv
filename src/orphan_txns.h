// Copyright (c) 2019 The Bitcoin SV developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include "net/net.h"
#include "txn_validation_data.h"

#include <shared_mutex>
#include <vector>

struct COrphanTxnEntry {
    TxInputDataSPtr pTxInputData {nullptr};
    int64_t nTimeExpire {};
    unsigned int size{};
};

class COrphanTxns;
using OrphanTxnsSPtr = std::shared_ptr<COrphanTxns>;
using CompactExtraTxnsVec = std::vector<std::pair<uint256, CTransactionRef>>;

/**
 * A class created to support orphan txns during validation.
 */
class COrphanTxns {
    /** Expiration time for orphan transactions in seconds */
    // NOLINTNEXTLINE(bugprone-implicit-widening-of-multiplication-result)
    static constexpr int64_t ORPHAN_TX_EXPIRE_TIME = 20 * 60;
    /** Minimum time between orphan transactions expire time checks in seconds */
    // NOLINTNEXTLINE(bugprone-implicit-widening-of-multiplication-result)
    static constexpr int64_t ORPHAN_TX_EXPIRE_INTERVAL = 5 * 60;

  public:
    // This struct is used to store details of tx (accepted by the mempool),
    // which are needed to find descendant txs in the orphan pool.
    struct CTxData final {
        TxId mTxId {};
        uint32_t mOutputsCount {};
        CTxData(const TxId& txid, uint32_t outputsCount)
        : mTxId{txid},
          mOutputsCount{outputsCount}
        {}
        // equality comparison
        bool operator==(const COrphanTxns::CTxData& rhs) const {
            return mTxId == rhs.mTxId && mOutputsCount == rhs.mOutputsCount;
        }
    };

    /** Default for -maxorphantxssize, maximum size of orphan transactions is 10 MB*/
    static constexpr uint64_t DEFAULT_MAX_ORPHAN_TRANSACTIONS_SIZE = ONE_GIGABYTE;
    /** Default number of orphan+recently-replaced txn to keep around for block
     *  reconstruction */
    static constexpr unsigned int DEFAULT_BLOCK_RECONSTRUCTION_EXTRA_TXN = 100;
    /** Default for -maxinputspertransactionoutoffirstlayerorphan */
    static constexpr uint64_t DEFAULT_MAX_INPUTS_OUTPUTS_PER_TRANSACTION = 5;
    /** Default for -maxorphansinbatchpercent */
    static constexpr uint64_t DEFAULT_MAX_PERCENTAGE_OF_ORPHANS_IN_BATCH = 60;

    COrphanTxns(
        size_t maxExtraTxnsForCompactBlock,
        size_t maxTxSizePolicy,
        size_t maxPercentageOfOrphansInBatch,
        size_t maxInputsOutputsPerTx);
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
    /** Limit the size of orphan transactions pool.
     *
     *  After the call the size of orphan pool is guaranteed to be in the range
     *  [nMaxOrphanTxnsSize - nMaxOrphanTxnsHysteresis, nMaxOrphanTxnsSize]
     */
    unsigned int limitTxnsSize(uint64_t nMaxOrphanTxnsSize, uint64_t nMaxOrphanTxnsHysteresis, bool fSkipRndEviction=false);
    /** Collect dependent transactions which might be processed later */
    std::vector<TxInputDataSPtr> collectDependentTxnsForRetry();
    /** Collect tx data which will be used to find any dependant orphan txn */
    void collectTxData(const CTransaction& tx);
    /** Erase collected tx data */
    void eraseCollectedTxData();
    /** Erase collected tx data from the given txns */
    void eraseCollectedTxDataFromTxns(const std::vector<TxId>& vRemovedTxIds);
    /** Get a number of orphan transactions queued */
    size_t getTxnsNumber();
    /** Get TxIds of known orphan transactions */
    std::vector<TxId> getTxIds() const;
    /** Get collected tx data */
    std::vector<COrphanTxns::CTxData> getCollectedTxData();
    /** Get a random orphan txn (used by UTs) */
    TxInputDataSPtr getRndOrphan();

  private:
    // Private aliasis
    using OrphanTxns = std::unordered_map<uint256, COrphanTxnEntry, SaltedTxidHasher>;
    using OrphanTxnsIter = OrphanTxns::iterator;
    using DependentOrphanTxns = std::unordered_set<const COrphanTxnEntry*>;
    using OrphanTxnsByPrev =
        std::unordered_map<COutPoint, DependentOrphanTxns, SaltedOutpointHasher>;
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

    /** Txn data collected to be used to find any dependant orphan txn */
    std::vector<COrphanTxns::CTxData> mCollectedTxData {};
    mutable std::mutex mCollectedTxDataMtx {};

    /** Extra txns used by block reconstruction */
    CompactExtraTxnsVec mExtraTxnsForCompact;
    mutable std::shared_mutex mExtraTxnsForCompactMtx {};
    size_t mExtraTxnsForCompactIdx {0};
    size_t mMaxExtraTxnsForCompactBlock {0};
    size_t mMaxStandardTxSize {0};
    size_t mMaxTxsPerBatch {0};
    size_t mMaxPercentageOfOrphansInBatch {0};
    size_t mMaxInputsOutputsPerTx {0};

    /** Control txns limit by a time slot */
    int64_t mNextSweep {0};

    /** amount of bytes added since last orphan pool trimming */
    std::atomic_size_t mUntrimmedSize {0};
};
