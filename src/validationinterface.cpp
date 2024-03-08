// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "validationinterface.h"

static CMainSignals g_signals;

CMainSignals &GetMainSignals() {
    return g_signals;
}

void UnregisterAllValidationInterfaces() {
    g_signals.BlockChecked.disconnect_all_slots();
    g_signals.Broadcast.disconnect_all_slots();
    g_signals.Inventory.disconnect_all_slots();
    g_signals.SetBestChain.disconnect_all_slots();
    g_signals.TransactionAddedToMempool.disconnect_all_slots();
    g_signals.TransactionRemovedFromMempool.disconnect_all_slots();
    g_signals.TransactionRemovedFromMempoolBlock.disconnect_all_slots();
    g_signals.BlockConnected.disconnect_all_slots();
    g_signals.BlockConnected2.disconnect_all_slots();
    g_signals.ScriptForMining.disconnect_all_slots();
    g_signals.BlockDisconnected.disconnect_all_slots();
    g_signals.UpdatedBlockTip.disconnect_all_slots();
    g_signals.NewPoWValidBlock.disconnect_all_slots();
    g_signals.InvalidTxMessageZMQ.disconnect_all_slots();
}
