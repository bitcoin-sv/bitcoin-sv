// Copyright (c) 2017 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_RPCBLOCKCHAIN_H
#define BITCOIN_RPCBLOCKCHAIN_H

#include <univalue.h>
#include "streams.h"
#include "httpserver.h"
#include "uint256.h"
#include "chain.h"

class CBlockIndex;
class Config;
class JSONRPCRequest;

UniValue getblockchaininfo(const Config &config, const JSONRPCRequest &request);
void getblock(const Config &config, const JSONRPCRequest &request, HTTPRequest *req, bool processedInBatch);
void writeBlockJsonChunksAndUpdateMetadata(const Config &config, HTTPRequest &req,
                          bool showTxDetails, CBlockIndex& blockindex);
void writeBlockChunksAndUpdateMetadata(bool isHexEncoded, HTTPRequest &req,
                          CForwardReadonlyStream& stream, CBlockIndex& blockIndex);

double GetDifficulty(const CBlockIndex *blockindex);

enum class GetBlockVerbosity {
    RAW_BLOCK = 0,
    DECODE_HEADER = 1,
    DECODE_TRANSACTIONS = 2
};

class GetBlockVerbosityNames {
public:
    static bool TryParse(const std::string& name, GetBlockVerbosity& result) {
        if (name == "RAW_BLOCK") {
            result = GetBlockVerbosity::RAW_BLOCK;
            return true;
        } else if (name == "DECODE_HEADER") {
            result = GetBlockVerbosity::DECODE_HEADER;
            return true;
        } else if (name == "DECODE_TRANSACTIONS") {
            result = GetBlockVerbosity::DECODE_TRANSACTIONS;
            return true;
        }
        return false;
    }
};

#endif // BITCOIN_RPCBLOCKCHAIN_H
