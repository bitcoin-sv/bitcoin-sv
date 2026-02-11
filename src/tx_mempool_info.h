// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include "amount.h"
#include "primitives/transaction.h"
#include "txn_validation_data.h"

#include <functional>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <variant>

#include <boost/noncopyable.hpp>

class CAsyncMempoolTxDB;
class CTxMemPoolEntry;
class CMempoolTxDBReader;

/**
 * Wrapper for on-disk transactions.
 * Once the transaction is moved to disk, further uses of transaction will
 * bring it in memory as a transient copy for that user only. The wrapper will
 * not store the reference.
 */
class CTransactionWrapper : boost::noncopyable {
public:
    CTransactionWrapper(const CTransactionRef &tx,
                        const std::shared_ptr<CMempoolTxDBReader>& txDB);
    CTransactionWrapper(const TxId &txid,
                        const std::shared_ptr<CMempoolTxDBReader>& txDB);

    CTransactionRef GetTx() const;
    const TxId& GetId() const noexcept;
    bool IsInMemory() const noexcept;
    TxStorage GetTxStorage() const noexcept
    {
        return (IsInMemory() ? TxStorage::memory : TxStorage::txdb);
    }
    bool HasDatabase(const std::shared_ptr<CMempoolTxDBReader>& txDB) const noexcept;

    void ResetTransaction();

private:
    const TxId txid;
    const std::shared_ptr<CMempoolTxDBReader> mempoolTxDB;

    // Documentation typedefs
    using OwnedPtr = CTransactionRef;
    using WeakPtr = CWeakTransactionRef;

    // Must be mutable so that accessors can be const.
    mutable std::mutex guard;
    mutable std::variant<OwnedPtr, WeakPtr> txref;

    // Accessor for the CAsyncMempoolTxDB worker thread.
    friend class CAsyncMempoolTxDB;
    CTransactionRef GetInMemoryTx();
};

using CTransactionWrapperRef = std::shared_ptr<CTransactionWrapper>;

/**
 * Information about a mempool transaction.
 */
struct TxMempoolInfo
{
    explicit TxMempoolInfo() = default;
    explicit TxMempoolInfo(const CTxMemPoolEntry& entry);
    TxMempoolInfo(const CTransactionRef& ptx,
                  const std::shared_ptr<CMempoolTxDBReader>& txdb = {nullptr});

    bool IsNull() const;

    const TxId& GetTxId() const;

    const CTransactionRef& GetTx() const;

    TxStorage GetTxStorage() const noexcept;

    /** Time the transaction entered the mempool. */
    int64_t nTime {0};

    /** Feerate of the transaction. */
    CFeeRate feeRate {};

    /** The fee delta. */
    Amount nFeeDelta {};

    /** size of the serialized transaction */
    size_t nTxSize {};

private:
    /** The transaction wrapper */
    CTransactionWrapperRef wrapper {nullptr};

    // Wrapper that enables us to use TxMempoolInfo implicit copy/move
    // construction and assignment.
    // Class guarantees that once the value is set it won't be overwritten.
    // NOLINTNEXTLINE(cppcoreguidelines-special-member-functions)
    class AtomicTxRef
    {
    public:
        AtomicTxRef() = default;
        AtomicTxRef(CTransactionRef ref) noexcept
            : mValue{std::move(ref)}
        {}

        AtomicTxRef(AtomicTxRef&& other) //NOLINT(*-noexcept-move-*)
        {
            std::lock_guard lockOther {other.mMtx};
            mValue = std::move(other.mValue); //NOLINT(cppcoreguidelines-prefer-member-initializer)
        }

        AtomicTxRef& operator=(AtomicTxRef&& other) // NOLINT(*-noexcept-move-*)
        {
            if(this == &other)
                return *this;

            std::scoped_lock locks {mMtx, other.mMtx};
            mValue = std::move(other.mValue);
            return *this;
        }

        AtomicTxRef(const AtomicTxRef& other)
        {
            std::shared_lock lockOther {other.mMtx};
            mValue = other.mValue; //NOLINT(cppcoreguidelines-prefer-member-initializer)
        }

        AtomicTxRef& operator=(const AtomicTxRef& other)
        {
            if(this == &other)
                return *this;

            // Lock other for read and us for write
            std::shared_lock lockOther {other.mMtx, std::defer_lock};
            std::scoped_lock locks {mMtx, lockOther};
            mValue = other.mValue;
            return *this;
        }

        // Try to set the value if mValue is still nullptr otherwise
        // just return existing value so we don't invalidate any existing
        // references to the underlying pointer.
        const CTransactionRef& store(const CTransactionRef& ref)
        {
            std::lock_guard lock {mMtx};
            if(!mValue)
            {
                mValue = ref;
            }
            return mValue;
        }

        std::optional<std::reference_wrapper<const CTransactionRef>> load() const
        {
            std::shared_lock lock {mMtx};
            if(mValue)
            {
                return mValue;
            }

            return {};
        }

    private:
        mutable std::shared_mutex mMtx {};
        CTransactionRef mValue {nullptr};
    };

    // A local cache for the transaction which may be stored on disk in the
    // mempool transaction database and we don't want to re-read it every time
    // we need a reference.
    mutable AtomicTxRef tx;

    static const TxId nullTxId;
    inline static const CTransactionRef nullTxRef;
};

