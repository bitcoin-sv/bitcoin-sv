// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "txmempool.h"
#include "mempooltxdb.h"

/**
 * class CTransactionWrapper
 */
CTransactionWrapper::CTransactionWrapper(const CTransactionRef &_tx,
                                         const std::shared_ptr<CMempoolTxDBReader>& txdb)
    : tx{_tx},
      txid{_tx->GetId()},
      mempoolTxDB{txdb}
{}

CTransactionWrapper::CTransactionWrapper(const TxId &_txid,
                                         const std::shared_ptr<CMempoolTxDBReader>& txdb)
    : txid(_txid),
      mempoolTxDB(txdb)
{}

CTransactionRef CTransactionWrapper::GetTxFromDB() const
{
    CTransactionRef tmp;
    if (mempoolTxDB != nullptr)
    {
        mempoolTxDB->GetTransaction(txid, tmp);
    }
    return tmp;
}


const TxId& CTransactionWrapper::GetId() const noexcept
{
    return txid;
}

CTransactionRef CTransactionWrapper::GetTx() const
{
    CTransactionRef tmp = std::atomic_load(&tx);
    if (tmp != nullptr)
    {
        return tmp;
    }
    return GetTxFromDB();
}

void CTransactionWrapper::UpdateTxMovedToDisk() const
{
    std::atomic_store(&tx, CTransactionRef{nullptr});
}

bool CTransactionWrapper::IsInMemory() const noexcept
{
    return std::atomic_load(&tx) != nullptr;
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
    if (!tx && wrapper)
    {
        tx = wrapper->GetTx();
    }
    return tx;
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
