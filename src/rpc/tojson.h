// Copyright (c) 2017 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_RPCTOJSON_H
#define BITCOIN_RPCTOJSON_H

#include "uint256.h"
#include "text_writer.h"
#include "httpserver.h" //for HTTPRequest
#include <univalue.h>

class CScript;

void ScriptPubKeyToJSON(const Config &config, const CScript &scriptPubKey,
                        UniValue &out, bool fIncludeHex);
std::string headerBlockToJSON(const Config &config, const CBlockHeader &blockHeader,
                     const CBlockIndex *blockindex);
UniValue blockTxToJSON(const Config &config, const CTransaction& tx, bool txDetails, bool isGenesisEnabled);
UniValue blockheaderToJSON(const CBlockIndex *blockindex);


#endif // BITCOIN_RPCTOJSON_H
