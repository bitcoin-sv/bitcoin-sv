// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "tx_mempool_info.h"

#include "txmempool.h"
#include "mempooltxdb.h"

/**
 * class CTransactionWrapper
 */
CTransactionWrapper::CTransactionWrapper(const CTransactionRef &tx,
                                         const std::shared_ptr<CMempoolTxDBReader>& txdb)
    : txid{tx->GetId()},
      mempoolTxDB{txdb},
      txref{OwnedPtr{tx}}
{}

CTransactionWrapper::CTransactionWrapper(const TxId &_txid,
                                         const std::shared_ptr<CMempoolTxDBReader>& txdb)
    : txid{_txid},
      mempoolTxDB{txdb},
      txref{WeakPtr{}}
{}

const TxId& CTransactionWrapper::GetId() const noexcept
{
    return txid;
}

// This function always tries to return the same pointer to a transaction when
// it's already in memory, even if the transaction is stored on disk.
//
// NOTE: About interactions with ResetTransaction()
// ================================================
//
// In current usage, ResetTransaction() is only called from the mempool's
// asynchronous writer thread (see CAsyncMempoolTxDB) and the mempool database
// reader's GetTransaction() is *never* called from that thread.
//
// If the wrapper is constructed from a TxId (i.e., txref is initially an empty
// weak pointer, IsInMemory() returns false):
//
//    * the transaction was already written to disk in a previous incarnation;
//    * therefore, ResetTransaction() is never called;
//    * GetTx() always executes its else{} fork;
//    * there are no races or possible deadlocks.
//
// If the wrapper is constructed from a transaction (i.e., txref is initially
// shared pointer, IsInMemory() returns true):
//
//    * If ResetTransaction() is called while the mutex is locked in GetTx():
//      - GetTx() will return the owned shared pointer; then,
//      - ResetTransaction() will store a weak pointer which will hold a valid
//        reference to the transaction as long as callers of GetTx() keep a
//        copy of the shared pointer.
//    * If GetTx() is called while the mutex is locked in ResetTransaction():
//      - the transaction has alredy been written to disk;
//      - ResetTransaction() will store a weak pointer that may become invalid
//        immediately upon its return;
//      - GetTx() will execute the else{} fork and may re-read the transaction
//        from disk (this will not interfere with other asynchronous
//        write/remove operations on the txdb).
//
// In the second case, GetTx() may read the transaction from disk after it was
// removed from the mempool, iff the wrapper is accessible from outside the
// mempool (e.g., in the block journal's queue) and the call to GetTx() happens
// before the asynchronous removal of the transaction from the mempool txdb.
CTransactionRef CTransactionWrapper::GetTx() const
{
    std::unique_lock<std::mutex> lock{guard};
    if (std::holds_alternative<OwnedPtr>(txref))
    {
        return std::get<OwnedPtr>(txref);
    }
    else
    {
        assert(std::holds_alternative<WeakPtr>(txref));
        auto ptr = std::get<WeakPtr>(txref).lock();
        if (!ptr && mempoolTxDB)
        {
            mempoolTxDB->GetTransaction(txid, ptr);
            txref.emplace<WeakPtr>(ptr);
        }
        return ptr;
    }
}

CTransactionRef CTransactionWrapper::GetInMemoryTx()
{
    std::unique_lock<std::mutex> lock{guard};
    if (std::holds_alternative<OwnedPtr>(txref))
    {
        return std::get<OwnedPtr>(txref);
    }
    else
    {
        return nullptr;
    }
}

void CTransactionWrapper::ResetTransaction()
{
    std::unique_lock<std::mutex> lock{guard};
    if (std::holds_alternative<OwnedPtr>(txref))
    {
        // There may be other copies of the shared pointer floating around,
        // keep a weak reference here to avoid re-reading from disk.
        auto ptr = std::get<OwnedPtr>(txref);
        txref.emplace<WeakPtr>(ptr);
    }
}

// NOTE: We can't avoid locking the mutex here, even if we used a helper
//       atomic flag, without causing a race with ResetTransaction().
bool CTransactionWrapper::IsInMemory() const noexcept
{
    std::unique_lock<std::mutex> lock{guard};
    return std::holds_alternative<OwnedPtr>(txref);
}

bool CTransactionWrapper::HasDatabase(const std::shared_ptr<CMempoolTxDBReader>& txDB) const noexcept
{
    return mempoolTxDB == txDB;
}


// Construct from a mempool entry
TxMempoolInfo::TxMempoolInfo(const CTxMemPoolEntry& entry)
: nTime { entry.GetTime() },
  feeRate { entry.GetFee(), entry.GetTxSize() },
  nFeeDelta { entry.GetModifiedFee() - entry.GetFee() },
  nTxSize { entry.GetTxSize() },
  wrapper { entry.tx }
{}

TxMempoolInfo::TxMempoolInfo(const CTransactionRef& ptx,
                             const std::shared_ptr<CMempoolTxDBReader>& txdb)
    : wrapper { std::make_shared<CTransactionWrapper>(ptx, txdb) }
{}

bool TxMempoolInfo::IsNull() const
{
    return wrapper == nullptr;
}

const TxId& TxMempoolInfo::GetTxId() const
{
    return wrapper ? wrapper->GetId() : nullTxId;
}

const CTransactionRef& TxMempoolInfo::GetTx() const
{
    if (auto loadTx = tx.load(); loadTx.has_value())
    {
        return loadTx.value();
    }

    if (wrapper)
    {
        // this can be called multiple times by multiple threads before tx is
        // really set - that's a rare situation so it's not an issue
        return tx.store( wrapper->GetTx() );
    }

    return nullTxRef;
}

TxStorage TxMempoolInfo::GetTxStorage() const noexcept
{
    if (wrapper)
    {
        return wrapper->GetTxStorage();
    }
    return TxStorage::memory;
}

const TxId TxMempoolInfo::nullTxId {};
