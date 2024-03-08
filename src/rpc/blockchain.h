// Copyright (c) 2017 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_RPCBLOCKCHAIN_H
#define BITCOIN_RPCBLOCKCHAIN_H

#include <optional>
#include <univalue.h>
#include "streams.h"
#include "httpserver.h"
#include "uint256.h"
#include "chain.h"
#include "blockstreams.h"
#include "jsonwriter.h"

class Config;
class JSONRPCRequest;

enum class GetBlockVerbosity {
    RAW_BLOCK = 0,
    DECODE_HEADER = 1,
    DECODE_TRANSACTIONS = 2,
    DECODE_HEADER_AND_COINBASE = 3
};

enum class GetHeaderVerbosity
{
    RAW_HEADER = 0,
    DECODE_HEADER = 1,
    DECODE_HEADER_EXTENDED = 2

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
        } else if (name == "DECODE_HEADER_AND_COINBASE") {
            result = GetBlockVerbosity::DECODE_HEADER_AND_COINBASE;
            return true;
        }
        return false;
    }
};

class GetHeaderVerbosityNames
{
public:
    static bool TryParse(const std::string& name, GetHeaderVerbosity& result)
    {
        if (name == "RAW_HEADER")
        {
            result = GetHeaderVerbosity::RAW_HEADER;
            return true;
        }
        else if (name == "DECODE_HEADER")
        {
            result = GetHeaderVerbosity::DECODE_HEADER;
            return true;
        }
        else if (name == "DECODE_HEADER_EXTENDED")
        {
            result = GetHeaderVerbosity::DECODE_HEADER_EXTENDED;
            return true;
        }
        return false;
    }
};

enum RetFormat {
    RF_UNDEF,
    RF_BINARY,
    RF_HEX,
    RF_JSON,
};

static const struct {
    enum RetFormat rf;
    const char* name;
} rf_names[] = {
    {RF_UNDEF, ""}, {RF_BINARY, "bin"}, {RF_HEX, "hex"}, {RF_JSON, "json"},
};

class block_parse_error : public std::runtime_error
{
public:
    block_parse_error(const std::string& msg) : std::runtime_error(msg)  {}
};

int ComputeNextBlockAndDepthNL(const CBlockIndex* tip, const CBlockIndex* blockindex, std::optional<uint256>& nextBlockHash);
UniValue getblockchaininfo(const Config &config, const JSONRPCRequest &request);
void getblock(const Config &config, const JSONRPCRequest &request, HTTPRequest &req, bool processedInBatch);
void getblockbyheight(const Config& config, const JSONRPCRequest& request,
                      HTTPRequest& req, bool processedInBatch);
void getblockdata(CBlockIndex& pblockindex, const Config& config, const JSONRPCRequest& jsonRPCReq,
                  HTTPRequest& httpReq, bool processedInBatch,
                  const int confirmations, const std::optional<uint256>& nextBlockHash);

void writeBlockJsonChunksAndUpdateMetadata(const Config& config, HTTPRequest& req, bool showTxDetails,
                                           CBlockIndex& blockIndex, bool showOnlyCoinbase,
                                           bool processedInBatch, const int confirmations,
                                           const std::optional<uint256>& nextBlockHash,
                                           const std::string& rpcReqId);
void writeBlockChunksAndUpdateMetadata(bool isHexEncoded, HTTPRequest& req,
                                       CBlockIndex& blockIndex, const std::string& rpcReqId,
                                       bool processedInBatch, const RetFormat& rf);

/**
 * Return JSON object with block header for specified blockindex
 *
 * @param blockindex
 * @param confirmations Number of block confirmations.
 * @param nextBlockHash If specified, field 'nextblockhash' is also included with value from this parameter.
 * @param diskBlockMetaData If specified, field 'blocksize' is also included with value obtained from obtained this parameter.
 *
 * @note All parameters after blockindex could be obtained from blockindex, but must be provided explicitly so that this function can be run without holding cs_main lock.
 */
UniValue blockheaderToJSON(const CBlockIndex *blockindex, int confirmations,
    const std::optional<uint256>& nextBlockHash,
    const std::optional<CDiskBlockMetaData>& diskBlockMetaData);

/**
 * Write JSON fields of a block header to jWriter
 *
 * @note Caller must call jWriter.writeBeginObject() before calling this method.
 *
 * @see blockheaderToJSON for parameter description.
 */
void writeBlockHeaderJSONFields(CJSONWriter& jWriter,
    const CBlockIndex* blockindex, int confirmations,
    const std::optional<uint256>& nextBlockHash,
    const std::optional<CDiskBlockMetaData>& diskBlockMetaData);

/**
 * Write JSON fields of an enhanced block header to jWriter
 *
 * @note Includes fields of non-enhanced block header.
 *
 * @param coinbaseMerkleProof If specified, coinbase Merkle proof will be included in header. This value must be provided explicitly so that this function can be run without holding cs_main lock.
 * @param coinbaseTx If not null, details of this coinbase transaction will be included in header in field `tx`.
 * @param config
 *
 * @see writeBlockHeaderJSONFields
 */
void writeBlockHeaderEnhancedJSONFields(CJSONWriter& jWriter,
    const CBlockIndex* blockindex, int confirmations,
    const std::optional<uint256>& nextBlockHash,
    const std::optional<CDiskBlockMetaData>& diskBlockMetaData,
    const std::optional<std::vector<uint256>>& coinbaseMerkleProof,
    const CTransaction* coinbaseTx,
    const Config& config);

double GetDifficulty(const CBlockIndex *blockindex);


#endif // BITCOIN_RPCBLOCKCHAIN_H
