// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#ifndef BITCOIN_RPCREGISTER_H
#define BITCOIN_RPCREGISTER_H

/** These are in one header file to avoid creating tons of single-function
 * headers for everything under src/rpc/ */
class CRPCTable;

/** Register block chain RPC commands */
void RegisterBlockchainRPCCommands(CRPCTable &tableRPC);
/** Register P2P networking RPC commands */
void RegisterNetRPCCommands(CRPCTable &tableRPC);
/** Register miscellaneous RPC commands */
void RegisterMiscRPCCommands(CRPCTable &tableRPC);
/** Register mining RPC commands */
void RegisterMiningRPCCommands(CRPCTable &tableRPC);
/** Register minier id RPC commands */
void RegisterMinerIdRPCCommands(CRPCTable &tableRPC);
/** Register mining-fbb RPC commands */
void RegisterMiningFBBRPCCommands(CRPCTable &tableRPC);
/** Register raw transaction RPC commands */
void RegisterRawTransactionRPCCommands(CRPCTable &tableRPC);
/** Register ABC RPC commands */
void RegisterABCRPCCommands(CRPCTable &tableRPC);
/** Register Frozen transactions RPC command */
void RegisterFrozenTransactionRPCCommands(CRPCTable& tableRPC);
/** Register safe mode RPC commands */
void RegisterSafeModeRPCCommands(CRPCTable &tableRPC);

static inline void RegisterAllRPCCommands(CRPCTable &t) {
    RegisterBlockchainRPCCommands(t);
    RegisterNetRPCCommands(t);
    RegisterMiscRPCCommands(t);
    RegisterMiningRPCCommands(t);
    RegisterMinerIdRPCCommands(t);
    RegisterMiningFBBRPCCommands(t);
    RegisterRawTransactionRPCCommands(t);
    RegisterABCRPCCommands(t);
    RegisterFrozenTransactionRPCCommands(t);
    RegisterSafeModeRPCCommands(t);
}

#endif
