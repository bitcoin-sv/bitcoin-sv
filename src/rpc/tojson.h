// Copyright (c) 2017 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_RPCTOJSON_H
#define BITCOIN_RPCTOJSON_H

#include "uint256.h"
#include "httpserver.h" //for HTTPRequest
#include <univalue.h>

class CScript;

UniValue blockStatusToJSON(const BlockStatus&);

// Following functions are implemented in rawtransaction.cpp
void getrawtransaction(const Config& config,
                       const JSONRPCRequest& request,
                       HTTPRequest& httpReq,
                       bool processedInBatch);
void getrawtransaction(const Config& config,
                       const JSONRPCRequest& request,
                       CTextWriter& textWriter,
                       bool processedInBatch,
                       std::function<void()>HttpCallback);
void decoderawtransaction(const Config& config,
                          const JSONRPCRequest& request,
                          HTTPRequest& httpReq, 
                          bool processedInBatch);
void decoderawtransaction(const Config& config,
                          const JSONRPCRequest& request,
                          CTextWriter& textWriter,
                          bool processedInBatch,
                          std::function<void()>HttpCallback);



#endif // BITCOIN_RPCTOJSON_H
