// Copyright (c) 2017 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_RPCTOJSON_H
#define BITCOIN_RPCTOJSON_H

#include <functional>

struct BlockStatus;
class Config;
class CTextWriter;
class HTTPRequest;
class JSONRPCRequest;
class UniValue;

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
                       const std::function<void()>& httpCallback);

void decoderawtransaction(const Config& config,
                          const JSONRPCRequest& request,
                          HTTPRequest& httpReq, 
                          bool processedInBatch);

void decoderawtransaction(const Config& config,
                          const JSONRPCRequest& request,
                          CTextWriter& textWriter,
                          bool processedInBatch,
                          const std::function<void()>& httpCallback);

#endif // BITCOIN_RPCTOJSON_H
