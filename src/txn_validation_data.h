// Copyright (c) 2019 The Bitcoin SV developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include "txn_util.h"
#include <enum_cast.h>

class Config; // NOLINT(cppcoreguidelines-virtual-class-destructor)
class TransactionSpecificConfig;

// Enumerate possible txn's source type
enum class TxSource : int
{
    unknown,
    file,
    reorg,
    wallet,
    rpc,
    p2p,
    finalised
};
// Enable enum_cast for TxSource, so we can log informatively
const enumTableT<TxSource>& enumTable(TxSource);

// Enumerate possible txn's types
enum class TxValidationPriority : int
{
    low = 0,
    normal = 1,
    high = 2
};
// Enable enum_cast for TxValidationPriority, so we can log informatively
const enumTableT<TxValidationPriority>& enumTable(TxValidationPriority);

// Describes the storage location of the original transaction.
enum class TxStorage : bool
{
    memory = 1,         // The transaction exists only in memory
    txdb = 0            // The transaction is stored in the mempoolTxDB
};

class CNode;

/**
 * This class is used to provide an input data to the TxnValidator.
 * It includes a pointer to a transaction and it's associated data.
 */
// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions)
class CTxInputData final {
public:
    // Constructor
    CTxInputData(
        TxIdTrackerWPtr pTxIdTracker,
        CTransactionRef ptx,
        TxSource txSource,
        TxValidationPriority txValidationPriority,
        TxStorage txStorage=TxStorage::memory,
        int64_t nAcceptTime=0,
        Amount nAbsurdFee=Amount(0),
        std::weak_ptr<CNode> pNode={},
        bool fOrphan=false,
        const std::shared_ptr<const TransactionSpecificConfig> tsc=nullptr);
    // Defaults
    CTxInputData(CTxInputData&&) = default;
    CTxInputData(const CTxInputData&) = delete;
    // Destructor
    ~CTxInputData();

    /**
     * Getters
     */
    // GetTxnPtr
    const CTransactionRef& GetTxnPtr() const {
        return mpTx;
    }
    // GetNodePtr
    const std::weak_ptr<CNode>& GetNodePtr() const {
        return mpNode;
    }
    // GetAbsurdFee
    Amount& GetAbsurdFee() {
        return mnAbsurdFee;
    }
    const Amount& GetAbsurdFee() const {
        return mnAbsurdFee;
    }
    // GetTxStorage
    TxStorage& GetTxStorage() {
        return mTxStorage;
    }
    const TxStorage& GetTxStorage() const {
        return mTxStorage;
    }
    // GetAcceptTime
    int64_t& GetAcceptTime() {
        return mnAcceptTime;
    }
    const int64_t& GetAcceptTime() const {
        return mnAcceptTime;
    }
    // GetTxSource
    TxSource& GetTxSource() {
        return mTxSource;
    }
    const TxSource& GetTxSource() const {
        return mTxSource;
    }
    // GetTxValidationPriority
    TxValidationPriority& GetTxValidationPriority() {
        return mTxValidationPriority;
    }
    const TxValidationPriority& GetTxValidationPriority() const {
        return mTxValidationPriority;
    }
    // IsOrphanTxn
    bool IsOrphanTxn() const {
        return mfOrphan;
    }
    // IsTxIdStored
    bool IsTxIdStored() const {
        return mfTxIdStored;
    }

    // GetSkipScriptFlags
    uint32_t GetSkipScriptFlags() const;

    using clock = std::chrono::steady_clock;

    clock::duration GetLifetime() const {
        return clock::now() - mCreated;
    }

    const Config& GetConfig( const Config& defaultConfig ) const;

    /**
     * Setters
     */
    // SetTxStorage
    void SetTxStorage(TxStorage txStorage) {
        mTxStorage = txStorage;
    }
    // SetAcceptTime
    void SetAcceptTime(int64_t acceptTime) {
        mnAcceptTime = acceptTime;
    }
    // SetTxSource
    void SetTxSource(TxSource txSource) {
        mTxSource = txSource;
    }
    // SetTxValidationPriority
    void SetTxValidationPriority(TxValidationPriority txValidationPriority) {
        mTxValidationPriority = txValidationPriority;
    }
    // SetOrphanTxn
    void SetOrphanTxn(bool fOrphan=true) {
        mfOrphan = fOrphan;
    }

// Optimizing for memory footprint:
// - members are ordered by decreasing alignment
private:
    CTransactionRef mpTx {nullptr};
    std::weak_ptr<CNode> mpNode {};
    TxIdTrackerWPtr mpTxIdTracker {};
    TxStorage mTxStorage {TxStorage::memory};
    Amount mnAbsurdFee {0};
    int64_t mnAcceptTime {0};
    clock::time_point mCreated {clock::now()};
    TxSource mTxSource {TxSource::unknown};
    TxValidationPriority mTxValidationPriority {TxValidationPriority::normal};
    bool mfOrphan {false};
    bool mfTxIdStored {false};
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
    const std::shared_ptr<const TransactionSpecificConfig> mConfig;
};

using TxInputDataSPtr = std::shared_ptr<CTxInputData>;
using TxInputDataSPtrVec = std::vector<TxInputDataSPtr>;
using TxInputDataSPtrVecIter = std::vector<TxInputDataSPtr>::iterator;
using TxInputDataSPtrRef = std::reference_wrapper<TxInputDataSPtr>;
using TxInputDataSPtrRefVec = std::vector<TxInputDataSPtrRef>;
