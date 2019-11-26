// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include <txmempool.h>
#include <tx_mempool_info.h>

// Construct from a mempool entry
TxMempoolInfo::TxMempoolInfo(const CTxMemPoolEntry& entry)
: tx { entry.GetSharedTx() },
  nTime { entry.GetTime() },
  feeRate { entry.GetFee(), entry.GetTxSize() },
  nFeeDelta { entry.GetModifiedFee() - entry.GetFee() }
{
}
