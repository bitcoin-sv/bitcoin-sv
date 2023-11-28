// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include "amount.h"
#include "primitives/transaction.h"
#include "txn_validation_data.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <optional>
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
        AtomicTxRef( CTransactionRef ref ) noexcept
        // NOLINTNEXTLINE(performance-unnecessary-value-param)
            : mValue{ ref }
        {}
        
        // NOLINTNEXTLINE(cppcoreguidelines-noexcept-move-operations, performance-noexcept-move-constructor)
        AtomicTxRef(AtomicTxRef&& other)
            : mValue{ std::atomic_load( &other.mValue ) }
        {}
        
        // NOLINTNEXTLINE(cppcoreguidelines-noexcept-move-operations, performance-noexcept-move-constructor)
        AtomicTxRef& operator=(AtomicTxRef&& other)
        {
            mValue = std::atomic_load( &other.mValue );
            return *this;
        }
        AtomicTxRef(const AtomicTxRef& other)
            : mValue{ std::atomic_load( &other.mValue ) }
        {}
    
        // NOLINTNEXTLINE(bugprone-unhandled-self-assignment)
        AtomicTxRef& operator=(const AtomicTxRef& other)
        {
            mValue = std::atomic_load( &other.mValue );
            return *this;
        }

        // Try to set the value if mValue is still nullptr otherwise
        // just return existing value so we don't invalidate any existing
        // references to the underlying pointer.
        const CTransactionRef& store( const CTransactionRef& ref )
        {
            CTransactionRef expectedNullptr;
            std::atomic_compare_exchange_strong(
                &mValue,
                &expectedNullptr,
                ref );

            return mValue;
        }
        std::optional<std::reference_wrapper<const CTransactionRef>> load() const
        {
            if (auto ref = std::atomic_load( &mValue ); ref)
            {
                return mValue;
            }

            return {};
        }

    private:
        CTransactionRef mValue;
    };

    // A local cache for the transaction which may be stored on disk in the
    // mempool transaction database and we don't want to re-read it every time
    // we need a reference.
    mutable AtomicTxRef tx;

    static const TxId nullTxId;
    inline static const CTransactionRef nullTxRef;
};

