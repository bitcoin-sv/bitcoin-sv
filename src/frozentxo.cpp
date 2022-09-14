// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "frozentxo.h"

#include "block_index.h"
#include "frozentxo_db.h"
#include "frozentxo_logging.h"
#include "primitives/transaction.h"
#include "uint256.h"

namespace
{
    uint256 zeroHash = {};
}

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

CFrozenTXOCheck::CFrozenTXOCheck( const CBlockIndex& blockIndex )
    : CFrozenTXOCheck{
        blockIndex.GetHeight(),
        blockIndex.GetBlockSource().ToString(),
        (blockIndex.GetPrev() ? blockIndex.GetPrev()->GetBlockHash() : zeroHash),
        blockIndex.GetHeaderReceivedTime() }
{
    mBlockIndex = &blockIndex;
}

bool CFrozenTXOCheck::Check(const COutPoint& outpoint, std::uint8_t& effectiveBlacklist) const
{
    if(IsCheckOnBlock() && mBlockIndex->IsInExplicitSoftConsensusFreeze())
    {
        return true;
    }

    auto ftd=CFrozenTXODB::FrozenTXOData::Create_Uninitialized();
    if(!CFrozenTXODB::Instance().GetFrozenTXOData(outpoint, ftd))
    {
        // If frozen TXO data cannot be obtained (i.e. does not exist), TXO is not frozen
        return true;
    }

    CFrozenTXODB::FrozenTXOData::Blacklist effective_blacklist;
    if(IsCheckOnBlock())
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
    // NOTE: Value of effective blacklist is returned as integer to avoid leaking implementation details
    //       of frozen TXO database so that header file remains independent of CFrozenTXODB class.
    effectiveBlacklist = static_cast<std::uint8_t>(effective_blacklist);

    return false;
}

void CFrozenTXOCheck::LogAttemptToSpendFrozenTXO(const COutPoint& outpoint, const CTransaction& tx, std::uint8_t effectiveBlacklist, std::int64_t receivedTime) const
{
    CFrozenTXOLogger::LogEntry_Rejected entry{
        (mReceivedTime ? mReceivedTime : receivedTime),
        static_cast<CFrozenTXODB::FrozenTXOData::Blacklist>(effectiveBlacklist),
        tx,
        mSource,
        outpoint,
        mPreviousActiveBlockHash
    };

    if(IsCheckOnBlock())
    {
        CFrozenTXOLogger::Instance().LogRejectedBlock(entry, mBlockIndex->GetBlockHash());
    }
    else
    {
        CFrozenTXOLogger::Instance().LogRejectedTransaction(entry);
    }
}

bool CFrozenTXOCheck::Check(const COutPoint& outpoint, const CTransaction& tx, std::int64_t receivedTime) const
{
    std::uint8_t effectiveBlacklist;
    bool result = Check(outpoint, effectiveBlacklist);
    if(!result)
    {
        LogAttemptToSpendFrozenTXO(outpoint, tx, effectiveBlacklist, receivedTime);
    }
    return result;
}

std::int32_t CFrozenTXOCheck::Get_max_FrozenTXOData_enforceAtHeight_stop()
{
    return CFrozenTXODB::Instance().Get_max_FrozenTXOData_enforceAtHeight_stop();
}


bool CFrozenTXOCheck::IsConfiscationTx(const CTransaction& tx)
{
    return CFrozenTXODB::IsConfiscationTx(tx);
}

bool CFrozenTXOCheck::ValidateConfiscationTxContents(const CTransaction& confiscation_tx)
{
    return CFrozenTXODB::ValidateConfiscationTxContents(confiscation_tx);
}

bool CFrozenTXOCheck::CheckConfiscationTxWhitelisted(const CTransaction& tx, std::int64_t receivedTime) const
{
    auto whitelistedTxData = CFrozenTXODB::WhitelistedTxData::Create_Uninitialized();
    bool isWhitelisted = CFrozenTXODB::Instance().IsTxWhitelisted(tx.GetId(), whitelistedTxData);
    if(isWhitelisted && this->nHeight >= whitelistedTxData.enforceAtHeight)
    {
        // Confiscation transaction is whitelisted and can be spent at specified height
        return true;
    }

    // Confiscation transaction is not whitelisted or cannot be spent at specified height
    // Add log entry to blacklist log file.
    CFrozenTXOLogger::LogEntry_Rejected entry{
        (mReceivedTime ? mReceivedTime : receivedTime),
        CFrozenTXODB::FrozenTXOData::Blacklist::Consensus,
        tx,
        mSource,
        COutPoint{},
        mPreviousActiveBlockHash
    };
    std::optional<std::int32_t> whitelistEnforceAtHeight;
    if(isWhitelisted)
    {
        whitelistEnforceAtHeight = whitelistedTxData.enforceAtHeight;
    }
    if(IsCheckOnBlock())
    {
        CFrozenTXOLogger::Instance().LogRejectedBlockCTNotWhitelisted(entry, whitelistEnforceAtHeight, mBlockIndex->GetBlockHash(), disableEnforcingConfiscationTransactionChecks);
        if(disableEnforcingConfiscationTransactionChecks)
        {
            return true;
        }
    }
    else
    {
        CFrozenTXOLogger::Instance().LogRejectedTransactionCTNotWhitelisted(entry, whitelistEnforceAtHeight);
    }
    return false;
}

std::int32_t CFrozenTXOCheck::Get_max_WhitelistedTxData_enforceAtHeight()
{
    return CFrozenTXODB::Instance().Get_max_WhitelistedTxData_enforceAtHeight();
}
