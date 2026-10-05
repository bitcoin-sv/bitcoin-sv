// Copyright (c) 2026 BSV Blockchain
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "merkle_tx.h"

#include "block_index_store.h"
#include "config.h"
#include "txn_validator.h"
#include "validation.h"

// NOLINTNEXTLINE(cert-err58-cpp)
const uint256 CMerkleTx::ABANDON_HASH(
    uint256S("0000000000000000000000000000000000000000000000000000000000000001"));

void CMerkleTx::SetMerkleBranch(const CBlockIndex* pindex, int posInBlock)
{
    // Update the tx's hashBlock
    hashBlock = pindex->GetBlockHash();

    // Set the position of the transaction in the block.
    nIndex = posInBlock;
}

int CMerkleTx::GetDepthInMainChain() const
{
    int32_t height = GetHeightInMainChain();
    if(height == MEMPOOL_HEIGHT)
    {
        return 0;
    }

    return ((nIndex == -1) ? (-1) : 1) * (chainActive.Height() - height + 1);
}

int CMerkleTx::GetHeightInMainChain() const
{
    if(hashUnset())
    {
        return MEMPOOL_HEIGHT;
    }

    AssertLockHeld(cs_main);

    // Find the block it claims to be in.
    auto pindex = mapBlockIndex.Get(hashBlock);
    if(!pindex || !chainActive.Contains(pindex))
    {
        return MEMPOOL_HEIGHT;
    }

    return pindex->GetHeight();
}

bool CMerkleTx::IsProtocolActive(ProtocolName name) const
{
    return ::IsProtocolActive(this->GetProtocolEra(), name);
}

ProtocolEra CMerkleTx::GetProtocolEra() const
{
    int32_t height = GetHeightInMainChain();
    if(height == MEMPOOL_HEIGHT)
    {
        AssertLockHeld(cs_main);
        return ::GetProtocolEra(GlobalConfig::GetConfig().GetConfigScriptPolicy(),
                                chainActive.Height() + 1);
    }
    return ::GetProtocolEra(GlobalConfig::GetConfig().GetConfigScriptPolicy(), height);
}

int CMerkleTx::GetBlocksToMaturity() const
{
    if(!IsCoinBase())
    {
        return 0;
    }

    return std::max(0, (COINBASE_MATURITY + 1) - GetDepthInMainChain());
}

bool CMerkleTx::SubmitTxToMempool(const Amount nAbsurdFee, CValidationState& state)
{
    // Check if g_connman is accessible
    if(!g_connman)
    {
        throw std::runtime_error("Error: P2P functionality missing or disabled");
    }
    // Mempool Journal ChangeSet
    mining::CJournalChangeSetPtr changeSet{mempool.getJournalBuilder().getNewChangeSet(
        mining::JournalUpdateReason::NEW_TXN)};
    // Forward transaction to the validator and wait for results.
    // To support backward compatibility (of this interface) we need
    // to wait until the transaction is processed.
    const auto& txValidator = g_connman->getTxnValidator();
    state = txValidator
                ->processValidation(std::make_shared<
                                        CTxInputData>(g_connman->GetTxIdTracker(),
                                                      tx, // a pointer to the tx
                                                      TxSource::wallet, // tx source
                                                      TxValidationPriority::
                                                          normal,        // tx validation
                                                                         // priority
                                                      TxStorage::memory, // tx storage
                                                      GetTime(),         // nAcceptTime
                                                      nAbsurdFee),       // nAbsurdFee
                                    changeSet, // an instance of the mempool journal
                                    true);     // fLimitMempoolSize
    return state.IsValid();
}

