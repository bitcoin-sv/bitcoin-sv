// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "processing_block_index.h"

#include "config.h"

DisconnectResult ProcessingBlockIndex::ApplyBlockUndo(const CBlockUndo &blockUndo,
                                const CBlock &block,
                                CCoinsViewCache &view,
                                const task::CCancellationToken& shutdownToken) const
{
    bool fClean = true;

    if (blockUndo.vtxundo.size() + 1 != block.vtx.size()) {
        error("DisconnectBlock(): block and undo data inconsistent");
        return DISCONNECT_FAILED;
    }

    // Undo transactions in reverse order.
    size_t i = block.vtx.size();
    while (i-- > 0) {

        if (shutdownToken.IsCanceled())
        {
            return DISCONNECT_FAILED;
        }

        const CTransaction &tx = *(block.vtx[i]);
        uint256 txid = tx.GetId();

        Config &config = GlobalConfig::GetConfig();
        // Check that all outputs are available and match the outputs in the
        // block itself exactly.
        for (size_t o = 0; o < tx.vout.size(); o++) {
            if (tx.vout[o].scriptPubKey.IsUnspendable(IsGenesisEnabled(config, mIndex.GetHeight()))) {
                continue;
            }

            COutPoint out(txid, o);
            CoinWithScript coin;
            bool is_spent = view.SpendCoin(out, &coin);
            if (!is_spent || tx.vout[o] != coin.GetTxOut()) {
                // transaction output mismatch
                fClean = false;
            }
        }

        // Restore inputs.
        if (i < 1) {
            // Skip the coinbase.
            continue;
        }

        const CTxUndo &txundo = blockUndo.vtxundo[i - 1];
        if (txundo.vprevout.size() != tx.vin.size()) {
            error("DisconnectBlock(): transaction and undo data inconsistent");
            return DISCONNECT_FAILED;
        }

        for (size_t j = tx.vin.size(); j-- > 0;) {
            const COutPoint &out = tx.vin[j].prevout;
            const CoinWithScript &undo = txundo.vprevout[j];
            DisconnectResult res = UndoCoinSpend(undo, view, out, config);
            if (res == DISCONNECT_FAILED) {
                return DISCONNECT_FAILED;
            }
            fClean = fClean && res != DISCONNECT_UNCLEAN;
        }
    }

    // Move best block pointer to previous block.
    view.SetBestBlock(block.hashPrevBlock);

    return fClean ? DISCONNECT_OK : DISCONNECT_UNCLEAN;
}

DisconnectResult ProcessingBlockIndex::DisconnectBlock(const CBlock &block,
                                        CCoinsViewCache &view,
                                        const task::CCancellationToken& shutdownToken) const
{
    auto blockUndo = mIndex.GetBlockUndo();

    if (!blockUndo.has_value())
    {
        return DISCONNECT_FAILED;
    }

    return
        ApplyBlockUndo(
            blockUndo.value(),
            block,
            view,
            shutdownToken );
}
