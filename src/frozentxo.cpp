// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "frozentxo.h"

#include "frozentxo_db.h"
#include "frozentxo_logging.h"
#include "primitives/transaction.h"
#include "uint256.h"

CFrozenTXOCheck::CFrozenTXOCheck(
    std::int32_t nHeight,
    const std::string& source,
    const uint256& previousActiveBlockHash,
    std::int64_t receivedTime)
    : nHeight(nHeight)
    , mSource{source}
    , mPreviousActiveBlockHash{previousActiveBlockHash}
    , mReceivedTime{receivedTime}
{}

CFrozenTXOCheck::CFrozenTXOCheck(
    std::int32_t nHeight,
    const std::string& source,
    const uint256& previousActiveBlockHash,
    std::int64_t receivedTime,
    const uint256& blockHash)
    : CFrozenTXOCheck{nHeight, source, previousActiveBlockHash, receivedTime}
{
    mBlockHash = &blockHash;
}

bool CFrozenTXOCheck::Check(const COutPoint& outpoint, TxGetter& txGetter)
{
    auto ftd=CFrozenTXODB::FrozenTXOData::Create_Uninitialized();
    if(!CFrozenTXODB::Instance().GetFrozenTXOData(outpoint, ftd))
    {
        // If frozen TXO data cannot be obtained (i.e. does not exist), TXO is not frozen
        return true;
    }

    CFrozenTXODB::FrozenTXOData::Blacklist effective_blacklist;
    bool validatingBlock = (mBlockHash != nullptr);
    if(validatingBlock)
    {
        // When validating block, we only consider TXOs frozen on consensus blacklist
        if(!ftd.IsFrozenOnConsensus(this->nHeight)) // NOTE: Assuming specified height is equal to height of the block that is currently being validated
        {
            // TXO is not frozen on consensus blacklist
            return true;
        }

        effective_blacklist = CFrozenTXODB::FrozenTXOData::Blacklist::Consensus;
    }
    else
    {
        // When not validating block, we consider TXOs frozen on policy blacklist which includes those frozen on consensus
        if(!ftd.IsFrozenOnPolicy(this->nHeight)) // NOTE: Assuming specified height is equal to height of the first block in which transaction could be included.
        {
            // TXO is not frozen on policy blacklist
            return true;
        }

        // Effective blacklist can be either policy-only or consensus, depending on frozen txo data.
        effective_blacklist = ftd.IsFrozenOnConsensus(this->nHeight) ? CFrozenTXODB::FrozenTXOData::Blacklist::Consensus
                                                                     : CFrozenTXODB::FrozenTXOData::Blacklist::PolicyOnly;
    }

    // This TXO is considered frozen.
    // Add log entry to blacklist log file.
    auto txData = txGetter.GetTxData();
    CFrozenTXOLogger::LogEntry_Rejected entry{
        (mReceivedTime ? mReceivedTime : txData.receivedTime),
        effective_blacklist,
        txData.tx,
        mSource,
        outpoint,
        mPreviousActiveBlockHash
    };

    if(validatingBlock)
    {
        CFrozenTXOLogger::Instance().LogRejectedBlock(entry, *mBlockHash);
    }
    else
    {
        CFrozenTXOLogger::Instance().LogRejectedTransaction(entry);
    }

    return false;
}

bool CFrozenTXOCheck::Check(const COutPoint& outpoint, const CTransaction& tx, std::int64_t receivedTime)
{
    struct TxGetterRef : TxGetter
    {
        TxGetterRef(const CTransaction& tx, std::int64_t receivedTime)
        : txData(tx, receivedTime)
        {}

        TxData GetTxData() override
        {
            return txData;
        }

        TxData txData;
    } txGetter(tx, receivedTime);

    return Check(outpoint, txGetter);
}

std::int32_t CFrozenTXOCheck::Get_max_FrozenTXOData_enforceAtHeight_stop()
{
    return CFrozenTXODB::Instance().Get_max_FrozenTXOData_enforceAtHeight_stop();
}
