// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2019 The Bitcoin Core developers
// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#include "rpc/blockchain.h"

#include "amount.h"
#include "block_file_access.h"
#include "block_index_store.h"
#include "chain.h"
#include "chainparams.h"
#include "coins.h"
#include "config.h"
#include "consensus/validation.h"
#include "core_io.h"
#include "hash.h"
#include "merkletreestore.h"
#include "policy/policy.h"
#include "primitives/transaction.h"
#include "rpc/http_protocol.h"
#include "rpc/protocol.h"
#include "rpc/server.h"
#include "rpc/tojson.h"
#include "streams.h"
#include "sync.h"
#include "taskcancellation.h"
#include "txdb.h"
#include "txmempool.h"
#include "txn_validator.h"
#include "util.h"
#include "utilstrencodings.h"
#include "validation.h"
#include "init.h"
#include "invalid_txn_publisher.h"
#include <boost/algorithm/string/case_conv.hpp> // for boost::to_upper
#include <boost/thread/thread.hpp>              // boost::thread::interrupt
#include <condition_variable>
#include <cstdint>
#include <mutex>

static std::mutex cs_blockchange;
static std::condition_variable cond_blockchange;

static double GetDifficultyFromBits(uint32_t nBits) {
    int nShift = (nBits >> 24) & 0xff;
    double dDiff = 0x0000ffff / double(nBits & 0x00ffffff);

    while (nShift < 29) {
        dDiff *= 256.0;
        nShift++;
    }

    while (nShift > 29) {
        dDiff /= 256.0;
        nShift--;
    }

    return dDiff;
}

double GetDifficulty(const CBlockIndex *blockindex) {
    // Floating point number that is a multiple of the minimum difficulty,
    // minimum difficulty = 1.0.
    if (blockindex == nullptr) {
        return 1.0;
    }

    return GetDifficultyFromBits(blockindex->GetBits());
}

int ComputeNextBlockAndDepthNL(const CBlockIndex* tip, const CBlockIndex* blockindex, std::optional<uint256>& nextBlockHash)
{
    AssertLockHeld(cs_main);
    int confirmations = -1;
    nextBlockHash = std::nullopt;
    if (chainActive.Contains(blockindex))
    {
        confirmations = tip->GetHeight() - blockindex->GetHeight() + 1;
        if (tip != blockindex)
        {
            nextBlockHash = chainActive.Next(blockindex)->GetBlockHash();
        }
    }

    return confirmations;
}

UniValue blockheaderToJSON(const CBlockIndex *blockindex, 
                           const int confirmations, 
                           const std::optional<uint256>& nextBlockHash,
                           const std::optional<CDiskBlockMetaData>& diskBlockMetaData) 
{
    UniValue result(UniValue::VOBJ);

    result.push_back(Pair("hash", blockindex->GetBlockHash().GetHex()));
    result.push_back(Pair("confirmations", confirmations));
    if(diskBlockMetaData.has_value())
    {
        // Include size of block to header if we have it
        result.push_back(Pair("size", diskBlockMetaData->diskDataSize));
    }
    result.push_back(Pair("height", blockindex->GetHeight()));
    result.push_back(Pair("version", blockindex->GetVersion()));
    result.push_back(
        Pair("versionHex", strprintf("%08x", blockindex->GetVersion())));
    result.push_back(Pair("merkleroot", blockindex->GetMerkleRoot().GetHex()));
    if (blockindex->GetBlockTxCount() > 0) {
        result.push_back(Pair("num_tx", uint64_t(blockindex->GetBlockTxCount())));
    }
    result.push_back(Pair("time", blockindex->GetBlockTime()));
    result.push_back(Pair("mediantime", blockindex->GetMedianTimePast()));
    result.push_back(Pair("nonce", uint64_t(blockindex->GetNonce())));
    result.push_back(Pair("bits", strprintf("%08x", blockindex->GetBits())));
    result.push_back(Pair("difficulty", GetDifficulty(blockindex)));
    result.push_back(Pair("chainwork", blockindex->GetChainWork().GetHex()));

    if (!blockindex->IsGenesis())
    {
        result.push_back(
            Pair(
                "previousblockhash",
                blockindex->GetPrev()->GetBlockHash().GetHex()));
    }

    if (nextBlockHash.has_value()) {
        result.push_back(Pair("nextblockhash", nextBlockHash.value().GetHex()));
    }
    
    const auto status{blockStatusToJSON(blockindex->getStatus())}; 
    result.push_back(Pair("status", status));

    return result;
}

UniValue blockStatusToJSON(const BlockStatus& block_status) 
{
    UniValue uv(UniValue::VOBJ);

    const auto v = block_status.getValidity();
    uv.push_back(Pair("validity", to_string(v)));


    uv.push_back(Pair("data", block_status.hasData()));
    uv.push_back(Pair("undo", block_status.hasUndo()));
    uv.push_back(Pair("failed", block_status.hasFailed()));
    uv.push_back(Pair("parent failed", block_status.hasFailedParent()));
    uv.push_back(Pair("disk meta", block_status.hasDiskBlockMetaData()));
    uv.push_back(Pair("soft reject", block_status.hasDataForSoftRejection()));
    uv.push_back(Pair("double spend", block_status.hasDoubleSpend()));
    uv.push_back(Pair("soft consensus frozen", block_status.hasDataForSoftConsensusFreeze()));

    return uv;
}

void writeBlockHeaderJSONFields(CJSONWriter& jWriter,
    const CBlockIndex* blockindex, int confirmations,
    const std::optional<uint256>& nextBlockHash,
    const std::optional<CDiskBlockMetaData>& diskBlockMetaData)
{
    UniValue block_header_json = blockheaderToJSON(blockindex, confirmations, nextBlockHash, diskBlockMetaData);

    const auto& keys = block_header_json.getKeys();
    const auto& values = block_header_json.getValues();
    assert(keys.size()==values.size());

    for(std::size_t i=0; i<keys.size(); ++i)
    {
        jWriter.pushKVJSONFormatted(keys[i], values[i].write());
    }
}

void writeBlockHeaderEnhancedJSONFields(CJSONWriter& jWriter,
    const CBlockIndex* blockindex, int confirmations,
    const std::optional<uint256>& nextBlockHash,
    const std::optional<CDiskBlockMetaData>& diskBlockMetaData,
    const std::optional<std::vector<uint256>>& coinbaseMerkleProof,
    const CTransaction* coinbaseTx,
    const Config& config)
{
    if(coinbaseTx)
    {
        jWriter.writeBeginArray("tx");
        TxToJSON(*coinbaseTx, uint256(), IsGenesisEnabled(config, blockindex->GetHeight()), RPCSerializationFlags(), jWriter);
        jWriter.writeEndArray();
    }

    writeBlockHeaderJSONFields(jWriter, blockindex, confirmations, nextBlockHash, diskBlockMetaData);

    if(coinbaseMerkleProof.has_value())
    {
        jWriter.writeBeginArray("merkleproof");
        for (uint256 hash : coinbaseMerkleProof.value())
        {
            jWriter.pushV(hash.GetHex());
        }
        jWriter.writeEndArray();
    }
}

UniValue getblockcount(const Config &config, const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() != 0) {
        throw std::runtime_error(
            "getblockcount\n"
            "\nReturns the number of blocks in the longest blockchain.\n"
            "\nResult:\n"
            "n    (numeric) The current block count\n"
            "\nExamples:\n" +
            HelpExampleCli("getblockcount", "") +
            HelpExampleRpc("getblockcount", ""));
    }

    return chainActive.Height();
}

UniValue getbestblockhash(const Config &config, const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() != 0) {
        throw std::runtime_error(
            "getbestblockhash\n"
            "\nReturns the hash of the best (tip) block in the "
            "longest blockchain.\n"
            "\nResult:\n"
            "\"hex\"      (string) the block hash hex encoded\n"
            "\nExamples:\n" +
            HelpExampleCli("getbestblockhash", "") +
            HelpExampleRpc("getbestblockhash", ""));
    }

    return chainActive.Tip()->GetBlockHash().GetHex();
}

void RPCNotifyBlockChange(bool ibd, const CBlockIndex *pindex) {
    cond_blockchange.notify_all();
}

UniValue waitfornewblock(const Config &config, const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() > 1) {
        throw std::runtime_error(
            "waitfornewblock (timeout)\n"
            "\nWaits for a specific new block and returns "
            "useful info about it.\n"
            "\nReturns the current block on timeout or exit.\n"
            "\nArguments:\n"
            "1. timeout (int, optional, default=0) Time in "
            "milliseconds to wait for a response. 0 indicates "
            "no timeout.\n"
            "\nResult:\n"
            "{                           (json object)\n"
            "  \"hash\" : {       (string) The blockhash\n"
            "  \"height\" : {     (int) Block height\n"
            "}\n"
            "\nExamples:\n" +
            HelpExampleCli("waitfornewblock", "1000") +
            HelpExampleRpc("waitfornewblock", "1000"));
    }

    int timeout = 0;
    if (request.params.size() > 0) {
        timeout = request.params[0].get_int();
    }

    const CBlockIndex* indexNext = chainActive.Tip();
    std::unique_lock<std::mutex> lock(cs_blockchange);
    if (timeout) {
        cond_blockchange.wait_for(
            lock, std::chrono::milliseconds(timeout), [index = chainActive.Tip(), &indexNext] {
            indexNext = chainActive.Tip();
            return (indexNext != index || !IsRPCRunning());
        });
    } else {
        cond_blockchange.wait(lock, [index = chainActive.Tip(), &indexNext] {
            indexNext = chainActive.Tip();
            return (indexNext != index || !IsRPCRunning());
        });
    }

    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("hash", indexNext->GetBlockHash().GetHex()));
    ret.push_back(Pair("height", indexNext->GetHeight()));
    return ret;
}

UniValue waitforblockheight(const Config &config,
                            const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() < 1 ||
        request.params.size() > 2) {
        throw std::runtime_error(
            "waitforblockheight <height> (timeout)\n"
            "\nWaits for (at least) block height and returns the height and "
            "hash\n"
            "of the current tip.\n"
            "\nReturns the current block on timeout or exit.\n"
            "\nArguments:\n"
            "1. height  (required, int) Block height to wait for (int)\n"
            "2. timeout (int, optional, default=0) Time in milliseconds to "
            "wait for a response. 0 indicates no timeout.\n"
            "\nResult:\n"
            "{                           (json object)\n"
            "  \"hash\" : {       (string) The blockhash\n"
            "  \"height\" : {     (int) Block height\n"
            "}\n"
            "\nExamples:\n" +
            HelpExampleCli("waitforblockheight", "\"100\", 1000") +
            HelpExampleRpc("waitforblockheight", "\"100\", 1000"));
    }

    int timeout = 0;

    int32_t height = request.params[0].get_int();

    if (request.params.size() > 1) {
        timeout = request.params[1].get_int();
    }

    const CBlockIndex* indexNext = chainActive.Tip();
    std::unique_lock<std::mutex> lock(cs_blockchange);
    if (timeout) {
        cond_blockchange.wait_for(
            lock, std::chrono::milliseconds(timeout), [&height, &indexNext] {
            indexNext = chainActive.Tip();
            return indexNext->GetHeight() >= height || !IsRPCRunning();
            });
    } else {
        cond_blockchange.wait(lock, [&height, &indexNext] {
            indexNext = chainActive.Tip();
            return indexNext->GetHeight() >= height || !IsRPCRunning();
        });
    }

    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("hash", indexNext->GetBlockHash().GetHex()));
    ret.push_back(Pair("height", indexNext->GetHeight()));
    return ret;
}

UniValue getdifficulty(const Config &config, const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() != 0) {
        throw std::runtime_error("getdifficulty\n"
                                 "\nReturns the proof-of-work difficulty as a "
                                 "multiple of the minimum difficulty.\n"
                                 "\nResult:\n"
                                 "n.nnn       (numeric) the proof-of-work "
                                 "difficulty as a multiple of the minimum "
                                 "difficulty.\n"
                                 "\nExamples:\n" +
                                 HelpExampleCli("getdifficulty", "") +
                                 HelpExampleRpc("getdifficulty", ""));
    }

    return GetDifficulty(chainActive.Tip());
}

std::string EntryDescriptionString() {
    return "    \"size\" : n,             (numeric) transaction size.\n"
           "    \"fee\" : n,              (numeric) transaction fee in " +
           CURRENCY_UNIT +
           "\n"
           "    \"modifiedfee\" : n,      (numeric) transaction fee with fee "
           "deltas used for mining priority\n"
           "    \"time\" : n,             (numeric) local time transaction "
           "entered pool in seconds since 1 Jan 1970 GMT\n"
           "    \"height\" : n,           (numeric) block height when "
           "transaction entered pool\n"
           "    \"depends\" : [           (array) unconfirmed transactions "
           "used as inputs for this transaction\n"
           "        \"transactionid\",    (string) parent transaction id\n"
           "       ... ]\n";
}

void writeMempoolEntryToJsonNL(const CTxMemPoolEntry& e,
                               const CTxMemPool::Snapshot &snapshot,
                               CJSONWriter& jWriter, bool pushId = true)
{
    if (pushId)
    {
        jWriter.writeBeginObject(e.GetTxId().ToString());
    }
    else
    {
        jWriter.writeBeginObject();
    }

    jWriter.pushKV("size", static_cast<uint64_t>(e.GetTxSize()));
    jWriter.pushKV("fee", e.GetFee());
    jWriter.pushKV("modifiedfee", e.GetModifiedFee());
    jWriter.pushKV("time", e.GetTime());
    jWriter.pushKV("height", static_cast<uint64_t>(e.GetHeight()));
    std::set<std::string> deps;
    const auto tx = e.GetSharedTx();
    for (const CTxIn &txin : tx->vin)
    {
        const auto& hash = txin.prevout.GetTxId();
        if (snapshot.TxIdExists(hash)) {
            deps.insert(hash.ToString());
        }
    }
    jWriter.writeBeginArray("depends");
    for (const auto& dep : deps)
    {
        jWriter.pushV(dep);
    }
    jWriter.writeEndArray();
    jWriter.writeEndObject();
}

void writeMempoolToJson(CJSONWriter& jWriter, bool fVerbose = false)
{
    if (fVerbose)
    {
        const auto snapshot = mempool.GetSnapshot();
        jWriter.writeBeginObject();
        for (const auto& entry : snapshot)
        {
            writeMempoolEntryToJsonNL(entry, snapshot, jWriter);
        }
        jWriter.writeEndObject();
    }
    else
    {
        std::vector<uint256> vtxids;
        mempool.QueryHashes(vtxids);

        jWriter.writeBeginArray();
        for (const uint256 &txid : vtxids)
        {
            jWriter.pushV(txid.ToString());
        }
        jWriter.writeEndArray();
    }
}

void getrawmempool(const Config& config,
                   const JSONRPCRequest& request,
                   HTTPRequest* httpReq,
                   bool processedInBatch)
{
    if (request.fHelp || request.params.size() > 1) {
        throw std::runtime_error(
            "getrawmempool ( verbose )\n"
            "\nReturns all transaction ids in memory pool as a json array of "
            "string transaction ids.\n"
            "\nArguments:\n"
            "1. verbose (boolean, optional, default=false) True for a json "
            "object, false for array of transaction ids\n"
            "\nResult: (for verbose = false):\n"
            "[                     (json array of string)\n"
            "  \"transactionid\"     (string) The transaction id\n"
            "  ,...\n"
            "]\n"
            "\nResult: (for verbose = true):\n"
            "{                           (json object)\n"
            "  \"transactionid\" : {       (json object)\n" +
            EntryDescriptionString() +
            "  }, ...\n"
            "}\n"
            "\nExamples:\n" +
            HelpExampleCli("getrawmempool", "true") +
            HelpExampleRpc("getrawmempool", "true"));
    }

    if(httpReq == nullptr)
        return;

    bool fVerbose = false;
    if (request.params.size() > 0) {
        fVerbose = request.params[0].get_bool();
    }

    if (!processedInBatch)
    {
        httpReq->WriteHeader("Content-Type", "application/json");
        httpReq->StartWritingChunks(HTTP_OK);
    }

    {
        CHttpTextWriter httpWriter(*httpReq);
        CJSONWriter jWriter(httpWriter, false);

        jWriter.writeBeginObject();
        jWriter.pushKNoComma("result");
        writeMempoolToJson(jWriter, fVerbose);
        jWriter.pushKV("error", nullptr);
        jWriter.pushKVJSONFormatted("id", request.id.write());
        jWriter.writeEndObject();
        jWriter.flush();
    }

    if (!processedInBatch)
    {
        httpReq->StopWritingChunks();
    }
}

void getrawnonfinalmempool(const Config& config,
                           const JSONRPCRequest& request,
                           HTTPRequest* httpReq,
                           bool processedInBatch)
{
    if (request.fHelp || request.params.size() > 0) {
        throw std::runtime_error(
            "getrawnonfinalmempool\n"
            "\nReturns all transaction ids in the non-final memory pool as a "
            "json array of "
            "string transaction ids.\n"
            "\nResult:\n"
            "[                     (json array of string)\n"
            "  \"transactionid\"     (string) The transaction id\n"
            "  ,...\n"
            "]\n"
            "\nExamples:\n" +
            HelpExampleCli("getrawnonfinalmempool", "") +
            HelpExampleRpc("getrawnonfinalmempool", ""));
    }

    if(httpReq == nullptr)
        return;

    if (!processedInBatch)
    {
        httpReq->WriteHeader("Content-Type", "application/json");
        httpReq->StartWritingChunks(HTTP_OK);
    }

    {
        CHttpTextWriter httpWriter(*httpReq);
        CJSONWriter jWriter(httpWriter, false);

        jWriter.writeBeginObject();
        jWriter.pushKNoComma("result");

        jWriter.writeBeginArray();
        for (const uint256 &txid : mempool.getNonFinalPool().getTxnIDs())
        {
            jWriter.pushV(txid.ToString());
        }

        jWriter.writeEndArray();

        jWriter.pushKV("error", nullptr);
        jWriter.pushKVJSONFormatted("id", request.id.write());
        jWriter.writeEndObject();
        jWriter.flush();
    }

    if (!processedInBatch)
    {
        httpReq->StopWritingChunks();
    }
}

void getmempoolancestors(const Config& config,
                         const JSONRPCRequest& request,
                         HTTPRequest* httpReq,
                         bool processedInBatch)
{
    if (request.fHelp || request.params.size() < 1 ||
        request.params.size() > 2) {
        throw std::runtime_error(
            "getmempoolancestors txid (verbose)\n"
            "\nIf txid is in the mempool, returns all in-mempool ancestors.\n"
            "\nArguments:\n"
            "1. \"txid\"                 (string, required) The transaction id "
            "(must be in mempool)\n"
            "2. verbose                  (boolean, optional, default=false) "
            "True for a json object, false for array of transaction ids\n"
            "\nResult (for verbose=false):\n"
            "[                       (json array of strings)\n"
            "  \"transactionid\"           (string) The transaction id of an "
            "in-mempool ancestor transaction\n"
            "  ,...\n"
            "]\n"
            "\nResult (for verbose=true):\n"
            "{                           (json object)\n"
            "  \"transactionid\" : {       (json object)\n" +
            EntryDescriptionString() +
            "  }, ...\n"
            "}\n"
            "\nExamples:\n" +
            HelpExampleCli("getmempoolancestors", "\"mytxid\"") +
            HelpExampleRpc("getmempoolancestors", "\"mytxid\""));
    }

    if(httpReq == nullptr)
        return;

    bool fVerbose = false;
    if (request.params.size() > 1) {
        fVerbose = request.params[1].get_bool();
    }

    uint256 hash = ParseHashV(request.params[0], "parameter 1");

    const auto kind = CTxMemPool::TxSnapshotKind::ONLY_ANCESTORS;
    const auto snapshot = mempool.GetTxSnapshot(hash, kind);

    // Check if tx is present in the mempool
    if (!snapshot.IsValid()) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,
                           "Transaction not in mempool");
    }
    if (!processedInBatch)
    {
        httpReq->WriteHeader("Content-Type", "application/json");
        httpReq->StartWritingChunks(HTTP_OK);
    }

    {
        CHttpTextWriter httpWriter(*httpReq);
        CJSONWriter jWriter(httpWriter, false);

        jWriter.writeBeginObject();
        jWriter.pushKNoComma("result");

        if (!fVerbose) {
            jWriter.writeBeginArray();
            for (const auto& entry : snapshot)
            {
                jWriter.pushV(entry.GetTxId().ToString());
            }
            jWriter.writeEndArray();
        } else {
            jWriter.writeBeginObject();
            for (const auto& entry : snapshot)
            {
                writeMempoolEntryToJsonNL(entry, snapshot, jWriter);
            }
            jWriter.writeEndObject();
        }

        jWriter.pushKV("error", nullptr);
        jWriter.pushKVJSONFormatted("id", request.id.write());
        jWriter.writeEndObject();
        jWriter.flush();
    }

    if (!processedInBatch)
    {
        httpReq->StopWritingChunks();
    }
}

void getmempooldescendants(const Config& config,
                           const JSONRPCRequest& request,
                           HTTPRequest* httpReq,
                           bool processedInBatch)
{
    if (request.fHelp || request.params.size() < 1 ||
        request.params.size() > 2) {
        throw std::runtime_error(
            "getmempooldescendants txid (verbose)\n"
            "\nIf txid is in the mempool, returns all in-mempool descendants.\n"
            "\nArguments:\n"
            "1. \"txid\"                 (string, required) The transaction id "
            "(must be in mempool)\n"
            "2. verbose                  (boolean, optional, default=false) "
            "True for a json object, false for array of transaction ids\n"
            "\nResult (for verbose=false):\n"
            "[                       (json array of strings)\n"
            "  \"transactionid\"           (string) The transaction id of an "
            "in-mempool descendant transaction\n"
            "  ,...\n"
            "]\n"
            "\nResult (for verbose=true):\n"
            "{                           (json object)\n"
            "  \"transactionid\" : {       (json object)\n" +
            EntryDescriptionString() +
            "  }, ...\n"
            "}\n"
            "\nExamples:\n" +
            HelpExampleCli("getmempooldescendants", "\"mytxid\"") +
            HelpExampleRpc("getmempooldescendants", "\"mytxid\""));
    }

    if(httpReq == nullptr)
        return;

    bool fVerbose = false;
    if (request.params.size() > 1) fVerbose = request.params[1].get_bool();

    uint256 hash = ParseHashV(request.params[0], "parameter 1");

    const auto kind = CTxMemPool::TxSnapshotKind::ONLY_DESCENDANTS;
    const auto snapshot = mempool.GetTxSnapshot(hash, kind);

    // Check if tx is present in the mempool
    if (!snapshot.IsValid()) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,
                           "Transaction not in mempool");
    }

    if (!processedInBatch)
    {
        httpReq->WriteHeader("Content-Type", "application/json");
        httpReq->StartWritingChunks(HTTP_OK);
    }

    {
        CHttpTextWriter httpWriter(*httpReq);
        CJSONWriter jWriter(httpWriter, false);

        jWriter.writeBeginObject();
        jWriter.pushKNoComma("result");

        if (!fVerbose) {
            jWriter.writeBeginArray();
            for (const auto& entry : snapshot)
            {
                jWriter.pushV(entry.GetTxId().ToString());
            }
            jWriter.writeEndArray();
        } else {
            jWriter.writeBeginObject();
            for (const auto& entry : snapshot)
            {
                writeMempoolEntryToJsonNL(entry, snapshot, jWriter);
            }
            jWriter.writeEndObject();
        }

        jWriter.pushKV("error", nullptr);
        jWriter.pushKVJSONFormatted("id", request.id.write());
        jWriter.writeEndObject();
        jWriter.flush();
    }

    if (!processedInBatch)
    {
        httpReq->StopWritingChunks();
    }
}

void getmempoolentry(const Config& config,
                     const JSONRPCRequest& request,
                     HTTPRequest* httpReq,
                     bool processedInBatch)
{
    if (request.fHelp || request.params.size() != 1) {
        throw std::runtime_error(
            "getmempoolentry txid\n"
            "\nReturns mempool data for given transaction\n"
            "\nArguments:\n"
            "1. \"txid\"                   (string, required) "
            "The transaction id (must be in mempool)\n"
            "\nResult:\n"
            "{                           (json object)\n" +
            EntryDescriptionString() +
            "}\n"
            "\nExamples:\n" +
            HelpExampleCli("getmempoolentry", "\"mytxid\"") +
            HelpExampleRpc("getmempoolentry", "\"mytxid\""));
    }

    if(httpReq == nullptr)
        return;

    uint256 hash = ParseHashV(request.params[0], "parameter 1");

    const auto kind = CTxMemPool::TxSnapshotKind::SINGLE;
    const auto snapshot = mempool.GetTxSnapshot(hash, kind);

    // Check if tx is present in the mempool
    if (!snapshot.IsValid() || snapshot.empty()) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,
                           "Transaction not in mempool");
    }
    if (!processedInBatch)
    {
        httpReq->WriteHeader("Content-Type", "application/json");
        httpReq->StartWritingChunks(HTTP_OK);
    }

    {
        CHttpTextWriter httpWriter(*httpReq);
        CJSONWriter jWriter(httpWriter, false);

        jWriter.writeBeginObject();
        jWriter.pushKNoComma("result");

        writeMempoolEntryToJsonNL(*snapshot.cbegin(), snapshot, jWriter, false);

        jWriter.pushKV("error", nullptr);
        jWriter.pushKVJSONFormatted("id", request.id.write());
        jWriter.writeEndObject();
        jWriter.flush();
    }

    if (!processedInBatch)
    {
        httpReq->StopWritingChunks();
    }
}

UniValue getblockhash(const Config &config, const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() != 1) {
        throw std::runtime_error(
            "getblockhash height\n"
            "\nReturns hash of block in best-block-chain at height provided.\n"
            "\nArguments:\n"
            "1. height         (numeric, required) The height index\n"
            "\nResult:\n"
            "\"hash\"         (string) The block hash\n"
            "\nExamples:\n" +
            HelpExampleCli("getblockhash", "1000") +
            HelpExampleRpc("getblockhash", "1000"));
    }
    
    LOCK(cs_main);

    int32_t nHeight = request.params[0].get_int();
    if (nHeight < 0 || nHeight > chainActive.Height()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Block height out of range");
    }

    CBlockIndex *pblockindex = chainActive[nHeight];
    return pblockindex->GetBlockHash().GetHex();
}

/**
 * Verbosity can be passed in multiple forms:
 *  - as bool true/false
 *  - as integer 0/1/2
 *  - as enum value RAW_HEADER / DECODE_HEADER / DECODE_HEADER_EXTENDED /
 * To maintain compatibility with different clients
 * we also try to parse JSON string as booleans and integers.
 */
static void parseGetBlockHeaderVerbosity(const UniValue& verbosityParam,
    GetHeaderVerbosity& verbosity)
{

    if (verbosityParam.isNum())
    {
        auto verbosityNum = verbosityParam.get_int();
        if (verbosityNum < 0 || verbosityNum > 2)
        {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Verbosity value out of range");
        }
        verbosity = static_cast<GetHeaderVerbosity>(verbosityNum);
    }
    else if (verbosityParam.isStr())
    {
        std::string verbosityStr = verbosityParam.get_str();
        boost::to_upper(verbosityStr);

        if (verbosityStr == "0" || verbosityStr == "FALSE")
        {
            verbosity = GetHeaderVerbosity::RAW_HEADER;
        }
        else if (verbosityStr == "1" || verbosityStr == "TRUE")
        {
            verbosity = GetHeaderVerbosity::DECODE_HEADER;
        }
        else if (verbosityStr == "2")
        {
            verbosity = GetHeaderVerbosity::DECODE_HEADER_EXTENDED;
        }
        else
        {
            if (!GetHeaderVerbosityNames::TryParse(verbosityStr, verbosity))
            {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Verbosity value not recognized");
            }
        }
    }
    else if (verbosityParam.isBool())
    {
        verbosity = (verbosityParam.get_bool() ? GetHeaderVerbosity::DECODE_HEADER
            : GetHeaderVerbosity::RAW_HEADER);
    }
    else
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid verbosity input type");
    }
}

void getblockheader(const Config &config, const JSONRPCRequest &request, HTTPRequest* httpReq, bool processedInBatch) 
{
    if (request.fHelp || request.params.size() < 1 ||
        request.params.size() > 2) 
    {
        throw std::runtime_error(
            "getblockheader \"hash\" ( verbosity )\n"
            "\nIf verbosity is 0, false or RAW_HEADER, returns a string that is "
            "serialized, hex-encoded data for blockheader 'hash'.\n"
            "If verbosity is 1, true or DECODE_HEADER, returns an Object with "
            "information about blockheader <hash>.\n"
            "If verbosity is 2 or DECODE_HEADER_EXTENDED, returns an Object "
            "with information about blockheader <hash>, merkle proof and coinbase transaction.\n"
            "\nArguments:\n"
            "1. \"hash\"          (string, required) The block hash\n"
            "2. verbosity       (boolean, numeric or string, optional, default=1) "
            "0 (false, RAW_HEADER) for the hex encoded data, 1 (true, DECODE_HEADER) for a "
            "json object, 2 (DECODE_HEADER_EXTENDED) for a json object with "
            "coinbase transaction and proof of inclusion.\n"
            "\nResult (for verbosity = true or 2):\n"
            "{\n"
            "  \"tx\" : [ ... ],        (array of transactions) Only coinbase transaction is included. Field is only present with verbosity 2 and if transaction details are available.\n"
            "  \"hash\" : \"hash\",     (string) the block hash (same as "
            "provided)\n"
            "  \"confirmations\" : n,   (numeric) The number of confirmations, "
            "or -1 if the block is not on the main chain\n"
            "  \"size\" : n,            (numeric) Size of block\n"
            "  \"height\" : n,          (numeric) The block height or index\n"
            "  \"version\" : n,         (numeric) The block version\n"
            "  \"versionHex\" : \"00000000\", (string) The block version "
            "formatted in hexadecimal\n"
            "  \"merkleroot\" : \"xxxx\", (string) The merkle root\n"
            "  \"num_tx\" : n,          (numeric) The number of transactions\n"
            "  \"time\" : ttt,          (numeric) The block time in seconds "
            "since epoch (Jan 1 1970 GMT)\n"
            "  \"mediantime\" : ttt,    (numeric) The median block time in "
            "seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"nonce\" : n,           (numeric) The nonce\n"
            "  \"bits\" : \"1d00ffff\", (string) The bits\n"
            "  \"difficulty\" : x.xxx,  (numeric) The difficulty\n"
            "  \"chainwork\" : \"0000...1f3\"     (string) Expected number of "
            "hashes required to produce the current chain (in hex)\n"
            "  \"previousblockhash\" : \"hash\",  (string) The hash of the "
            "previous block\n"
            "  \"nextblockhash\" : \"hash\",      (string) The hash of the "
            "next block\n"
            "  \"merkleproof\" : [      (array) Merkle proof for coinbase transaction. Field is only present with verbosity 2 and if transaction details are available.\n"
            "      \"node\" : \"hash\", (string) Hash of the node in merkle proof\n"
            "      \"position\" \"Right\" (string) Position of the hash in the Merkle tree\n"
            "  ]\n"
            "status: {\n"
            "  \"validity\" : (string) Validation state of the block\n"
            "  \"data\" : (boolean) Data flag\n"
            "  \"undo\" : (boolean) Undo flag\n"
            "  \"failed\" : (boolean) Failed flag\n"
            "  \"parent failed\" : (boolean) Parent failed flag\n"
            "  \"disk meta\" : (boolean) Disk meta flag\n"
            "  \"soft reject\" : (boolean) Soft reject flag\n"
            "  \"double spend\" : (boolean) May contain a double spend tx\n"
            "  \"soft consensus frozen\" : (boolean) Soft consensus frozen flag\n"
            "  }\n"
            "}\n"
            "\nResult (for verbosity=false):\n"
            "\"data\"             (string) A string that is serialized, "
            "hex-encoded data for block 'hash'.\n"
            "\nExamples:\n" +
            HelpExampleCli("getblockheader", "\"00000000c937983704a73af28acdec3"
                                             "7b049d214adbda81d7e2a3dd146f6ed09"
                                             "\"") +
            HelpExampleRpc("getblockheader", "\"00000000c937983704a73af28acdec3"
                                             "7b049d214adbda81d7e2a3dd146f6ed09"
                                             "\""));
    }

    if (httpReq == nullptr)
    {
        return;
    }

    std::string strHash = request.params[0].get_str();
    uint256 hash(uint256S(strHash));

    // Parse verbosity parameter which can be false/true, numeric or string.
    // Default is true, which means the same as DECODE_HEADER.
    GetHeaderVerbosity verbosity = GetHeaderVerbosity::DECODE_HEADER;
    if (request.params.size() > 1)
    {
        parseGetBlockHeaderVerbosity(request.params[1], verbosity);
    }

    int confirmations;
    std::optional<uint256> nextBlockHash;
    CBlockIndex* pblockindex = mapBlockIndex.Get(hash);

    if (!pblockindex)
    {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
    }

    {
        LOCK(cs_main);
        confirmations = ComputeNextBlockAndDepthNL(chainActive.Tip(), pblockindex, nextBlockHash);
    }

    if (!processedInBatch) 
    {
        httpReq->WriteHeader("Content-Type", "application/json");
        httpReq->StartWritingChunks(HTTP_OK);
    }

    CHttpTextWriter httpWriter(*httpReq);
    CJSONWriter jWriter(httpWriter, false);

    jWriter.writeBeginObject();

    if (verbosity == GetHeaderVerbosity::RAW_HEADER)
    {
        CDataStream ssBlock(SER_NETWORK, PROTOCOL_VERSION);
        ssBlock << pblockindex->GetBlockHeader();
        jWriter.pushKV("result", HexStr(ssBlock.begin(), ssBlock.end()));
    }
    else
    {
        CDiskBlockMetaData diskBlockMetaData = pblockindex->GetDiskBlockMetaData();
        jWriter.writeBeginObject("result");
        if(verbosity == GetHeaderVerbosity::DECODE_HEADER_EXTENDED)
        {
            // Read coinbase txn and store pointer to CTransaction object that is stored in reader.
            // If block was already pruned, then reader is not available and coinbase transaction will not be returned in enriched header.
            const CTransaction* coinbaseTx = nullptr;
            auto reader = pblockindex->GetDiskBlockStreamReader(diskBlockMetaData.diskDataHash.IsNull());
            if(reader)
            {
                try
                {
                    coinbaseTx = &reader->ReadTransaction();
                }
                catch(...)
                {
                    // Exceptions cannot be thrown while we already started streaming the result.
                    // If coinbase txn could not be read, it will not be returned.
                    LogPrint(BCLog::RPC, "getblockheader: Reading of coinbase txn failed.\n");
                }
            }

            std::optional<std::vector<uint256>> coinbaseMerkleProof;
            if (coinbaseTx) // Merkle proof for CB is only needed if we were able to get CB txn
            {
                if (CMerkleTreeRef merkleTree = pMerkleTreeFactory->GetMerkleTree(config, *pblockindex, chainActive.Height()))
                {
                    coinbaseMerkleProof = merkleTree->GetMerkleProof(0, false).merkleTreeHashes;
                }
                else
                {
                    // Do not return just CB txn if we were unable to get its Merkle proof.
                    coinbaseTx = nullptr;
                }
            }
            
            writeBlockHeaderEnhancedJSONFields(jWriter, pblockindex, confirmations, nextBlockHash, diskBlockMetaData.diskDataHash.IsNull() ? std::nullopt : std::optional<CDiskBlockMetaData>{ diskBlockMetaData }, coinbaseMerkleProof, coinbaseTx, config);
        }
        else
        {
            writeBlockHeaderJSONFields(jWriter, pblockindex, confirmations, nextBlockHash, diskBlockMetaData.diskDataHash.IsNull() ? std::nullopt : std::optional<CDiskBlockMetaData>{ diskBlockMetaData });
        }
        jWriter.writeEndObject();
    }

    jWriter.pushKV("error", nullptr);
    jWriter.pushKVJSONFormatted("id", request.id.write());

    jWriter.writeEndObject();
    jWriter.flush();
    if (!processedInBatch)
    {
        httpReq->StopWritingChunks();
    }
}

/**
 * Verbosity can be passed in multiple forms:
 *  - as bool true/false
 *  - as integer 0/1/2/3
 *  - as enum value RAW_BLOCK / DECODE_HEADER / DECODE_TRANSACTIONS /
 * DECODE_HEADER_AND_COINBASE To maintain compatibility with different clients
 * we also try to parse JSON string as booleans and integers.
 */
static void parseGetBlockVerbosity(const UniValue &verbosityParam,
                                   GetBlockVerbosity &verbosity) {

    if (verbosityParam.isNum()) {
        auto verbosityNum = verbosityParam.get_int();
        if (verbosityNum < 0 || verbosityNum > 3)
            throw JSONRPCError(RPC_INVALID_PARAMETER,
                               "Verbosity value out of range");
        verbosity = static_cast<GetBlockVerbosity>(verbosityNum);
    } else if (verbosityParam.isStr()) {
        std::string verbosityStr = verbosityParam.get_str();
        boost::to_upper(verbosityStr);

        if (verbosityStr == "0" || verbosityStr == "FALSE") {
            verbosity = GetBlockVerbosity::RAW_BLOCK;
        } else if (verbosityStr == "1" || verbosityStr == "TRUE") {
            verbosity = GetBlockVerbosity::DECODE_HEADER;
        } else if (verbosityStr == "2") {
            verbosity = GetBlockVerbosity::DECODE_TRANSACTIONS;
        } else if (verbosityStr == "3") {
            verbosity = GetBlockVerbosity::DECODE_HEADER_AND_COINBASE;
        } else {
            if (!GetBlockVerbosityNames::TryParse(verbosityStr, verbosity)) {
                throw JSONRPCError(RPC_INVALID_PARAMETER,
                                   "Verbosity value not recognized");
            }
        }
    } else if (verbosityParam.isBool()) {
        verbosity =
            (verbosityParam.get_bool() ? GetBlockVerbosity::DECODE_HEADER
                                       : GetBlockVerbosity::RAW_BLOCK);
    } else {
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                           "Invalid verbosity input type");
    }
}

void getblock(const Config& config,
              const JSONRPCRequest& jsonRPCReq,
              HTTPRequest* httpReq,
              bool processedInBatch)
{

    if (jsonRPCReq.fHelp || jsonRPCReq.params.size() < 1 ||
        jsonRPCReq.params.size() > 2) {
        throw std::runtime_error(
            "getblock \"blockhash\" ( verbosity )\n"
            "\nIf verbosity is 0 or RAW_BLOCK, returns a string that is "
            "serialized, "
            "hex-encoded data for block 'hash'.\n"
            "If verbosity is 1 or DECODE_HEADER, returns an Object with "
            "information about block <hash>.\n"
            "If verbosity is 2 or DECODE_TRANSACTIONS, returns an Object with "
            "information about "
            "block <hash> and information about each transaction. \n"
            "If verbosity is 3 or DECODE_HEADER_AND_COINBASE, returns a json "
            "object with block information "
            "and the coinbase transaction. \n"
            "\nArguments:\n"
            "1. \"blockhash\"          (string, required) The block hash\n"
            "2. verbosity              (numeric or string, optional, "
            "default=1) 0 (RAW_BLOCK) for hex encoded data, "
            "1 (DECODE_HEADER) for a json object, 2 (DECODE_TRANSACTIONS) for "
            "json object with transaction data and "
            "3 (DECODE_HEADER_AND_COINBASE) for a json object with coinbase "
            "only\n"
            "\nResult (for verbosity = 0 or verbosity = RAW_BLOCK):\n"
            "\"data\"             (string) A string that is serialized, "
            "hex-encoded data for block 'hash'.\n"
            "\nResult (for verbosity = 1 or verbosity = DECODE_HEADER):\n"
            "{\n"
            "  \"hash\" : \"hash\",     (string) the block hash (same as "
            "provided)\n"
            "  \"confirmations\" : n,   (numeric) The number of confirmations, "
            "or -1 if the block is not on the main chain\n"
            "  \"size\" : n,            (numeric) The block size\n"
            "  \"height\" : n,          (numeric) The block height or index\n"
            "  \"version\" : n,         (numeric) The block version\n"
            "  \"versionHex\" : \"00000000\", (string) The block version "
            "formatted in hexadecimal\n"
            "  \"merkleroot\" : \"xxxx\", (string) The merkle root\n"
            "  \"num_tx\" : n,          (numeric) The number of transactions\n"
            "  \"tx\" : [               (array of string) The transaction ids\n"
            "     \"transactionid\"     (string) The transaction id\n"
            "     ,...\n"
            "  ],\n"
            "  \"time\" : ttt,          (numeric) The block time in seconds "
            "since epoch (Jan 1 1970 GMT)\n"
            "  \"mediantime\" : ttt,    (numeric) The median block time in "
            "seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"nonce\" : n,           (numeric) The nonce\n"
            "  \"bits\" : \"1d00ffff\", (string) The bits\n"
            "  \"difficulty\" : x.xxx,  (numeric) The difficulty\n"
            "  \"chainwork\" : \"xxxx\",  (string) Expected number of hashes "
            "required to produce the chain up to this block (in hex)\n"
            "  \"previousblockhash\" : \"hash\",  (string) The hash of the "
            "previous block\n"
            "  \"nextblockhash\" : \"hash\"       (string) The hash of the "
            "next block\n"
            "}\n"
            "\nResult (for verbosity = 2 or verbosity = DECODE_TRANSACTIONS):\n"
            "\"data\"             (string) A string that is serialized, "
            "hex-encoded data for block 'hash'.\n"
            "{\n"
            "  \"hash\" : \"hash\",     (string) the block hash (same as "
            "provided)\n"
            "  \"confirmations\" : n,   (numeric) The number of confirmations, "
            "or -1 if the block is not on the main chain\n"
            "  \"size\" : n,            (numeric) The block size\n"
            "  \"height\" : n,          (numeric) The block height or index\n"
            "  \"version\" : n,         (numeric) The block version\n"
            "  \"versionHex\" : \"00000000\", (string) The block version "
            "formatted in hexadecimal\n"
            "  \"merkleroot\" : \"xxxx\", (string) The merkle root\n"
            "  \"num_tx\" : n,          (numeric) The number of transactions\n"
            "  \"tx\" : [               (array of Objects) The transactions in "
            "the format of the getrawtransaction RPC. Different from verbosity "
            "= 1 \"tx\" result.\n"
            "         ,...\n"
            "  ],\n"
            "  \"time\" : ttt,          (numeric) The block time in seconds "
            "since epoch (Jan 1 1970 GMT)\n"
            "  \"mediantime\" : ttt,    (numeric) The median block time in "
            "seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"nonce\" : n,           (numeric) The nonce\n"
            "  \"bits\" : \"1d00ffff\", (string) The bits\n"
            "  \"difficulty\" : x.xxx,  (numeric) The difficulty\n"
            "  \"chainwork\" : \"xxxx\",  (string) Expected number of hashes "
            "required to produce the chain up to this block (in hex)\n"
            "  \"previousblockhash\" : \"hash\",  (string) The hash of the "
            "previous block\n"
            "  \"nextblockhash\" : \"hash\"       (string) The hash of the "
            "next block\n"
            "}\n"
            "\nResult (for verbosity = 3 or verbosity = "
            "DECODE_HEADER_AND_COINBASE):\n"
            "{\n"
            "  \"hash\" : \"hash\",     (string) the block hash (same as "
            "provided)\n"
            "  \"confirmations\" : n,   (numeric) The number of confirmations, "
            "or -1 if the block is not on the main chain\n"
            "  \"size\" : n,            (numeric) The block size\n"
            "  \"height\" : n,          (numeric) The block height or index\n"
            "  \"version\" : n,         (numeric) The block version\n"
            "  \"versionHex\" : \"00000000\", (string) The block version "
            "formatted in hexadecimal\n"
            "  \"merkleroot\" : \"xxxx\", (string) The merkle root\n"
            "  \"num_tx\" : n,          (numeric) The number of transactions\n"
            "  \"tx\" : [               The coinbase transaction in the format "
            "of the getrawtransaction RPC. Different from verbosity = 1 \"tx\" "
            "result.\n"
            "         ,...\n"
            "  ],\n"
            "  \"time\" : ttt,          (numeric) The block time in seconds "
            "since epoch (Jan 1 1970 GMT)\n"
            "  \"mediantime\" : ttt,    (numeric) The median block time in "
            "seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"nonce\" : n,           (numeric) The nonce\n"
            "  \"bits\" : \"1d00ffff\", (string) The bits\n"
            "  \"difficulty\" : x.xxx,  (numeric) The difficulty\n"
            "  \"chainwork\" : \"xxxx\",  (string) Expected number of hashes "
            "required to produce the chain up to this block (in hex)\n"
            "  \"previousblockhash\" : \"hash\",  (string) The hash of the "
            "previous block\n"
            "  \"nextblockhash\" : \"hash\"       (string) The hash of the "
            "next block\n"
            "}\n"
            "\nExamples:\n" +
            HelpExampleCli("getblock", "\"00000000c937983704a73af28acdec37b049d214adbda"
                "81d7e2a3dd146f6ed09\"") +
            HelpExampleRpc("getblock", "\"00000000c937983704a73af28acdec37b049d214adbda"
                "81d7e2a3dd146f6ed09\""));
    }

    if(httpReq == nullptr)
        return;

    std::string strHash = jsonRPCReq.params[0].get_str();
    uint256 hash(uint256S(strHash));

    int confirmations;
    std::optional<uint256> nextBlockHash;
    const auto pblockindex = mapBlockIndex.Get(hash);

    if (!pblockindex) 
    {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
    }

    {
        LOCK(cs_main);
        confirmations = ComputeNextBlockAndDepthNL(chainActive.Tip(), pblockindex, nextBlockHash);
    }

    getblockdata(*pblockindex,
                 config,
                 jsonRPCReq,
                 *httpReq,
                 processedInBatch,
                 confirmations,
                 nextBlockHash);
}

void getblockbyheight(const Config& config,
                      const JSONRPCRequest& jsonRPCReq,
                      HTTPRequest* httpReq,
                      bool processedInBatch)
{

    if (jsonRPCReq.fHelp || jsonRPCReq.params.size() < 1 ||
        jsonRPCReq.params.size() > 2) {
        throw std::runtime_error(
            "getblockbyheight height ( verbosity )\n"
            "\nIf verbosity is 0 or RAW_BLOCK, returns a string that is "
            "serialized, "
            "hex-encoded data for block 'hash'.\n"
            "If verbosity is 1 or DECODE_HEADER, returns an Object with "
            "information about block <hash>.\n"
            "If verbosity is 2 or DECODE_TRANSACTIONS, returns an Object with "
            "information about "
            "block <hash> and information about each transaction. \n"
            "If verbosity is 3 or DECODE_HEADER_AND_COINBASE, returns a json "
            "object with block information and the coinbase transaction. \n"
            "\nArguments:\n"
            "1. \"height\"             (numeric, required) The block height\n"
            "2. verbosity              (numeric or string, optional, "
            "default=1) 0 (RAW_BLOCK) for hex encoded data, "
            "1 (DECODE_HEADER) for a json object, 2 (DECODE_TRANSACTIONS) for "
            "json object with transaction data and "
            "3 (DECODE_HEADER_AND_COINBASE) for a json object with coinbase "
            "only\n"
            "\nResult (for verbosity = 0 or verbosity = RAW_BLOCK):\n"
            "\"data\"             (string) A string that is serialized, "
            "hex-encoded data for block 'hash'.\n"
            "\nResult (for verbosity = 1 or verbosity = DECODE_HEADER):\n"
            "{\n"
            "  \"hash\" : \"hash\",     (string) the block hash (same as "
            "provided)\n"
            "  \"confirmations\" : n,   (numeric) The number of confirmations, "
            "or -1 if the block is not on the main chain\n"
            "  \"size\" : n,            (numeric) The block size\n"
            "  \"height\" : n,          (numeric) The block height or index\n"
            "  \"version\" : n,         (numeric) The block version\n"
            "  \"versionHex\" : \"00000000\", (string) The block version "
            "formatted in hexadecimal\n"
            "  \"merkleroot\" : \"xxxx\", (string) The merkle root\n"
            "  \"num_tx\" : n,          (numeric) The number of transactions\n"
            "  \"tx\" : [               (array of string) The transaction ids\n"
            "     \"transactionid\"     (string) The transaction id\n"
            "     ,...\n"
            "  ],\n"
            "  \"time\" : ttt,          (numeric) The block time in seconds "
            "since epoch (Jan 1 1970 GMT)\n"
            "  \"mediantime\" : ttt,    (numeric) The median block time in "
            "seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"nonce\" : n,           (numeric) The nonce\n"
            "  \"bits\" : \"1d00ffff\", (string) The bits\n"
            "  \"difficulty\" : x.xxx,  (numeric) The difficulty\n"
            "  \"chainwork\" : \"xxxx\",  (string) Expected number of hashes "
            "required to produce the chain up to this block (in hex)\n"
            "  \"previousblockhash\" : \"hash\",  (string) The hash of the "
            "previous block\n"
            "  \"nextblockhash\" : \"hash\"       (string) The hash of the "
            "next block\n"
            "}\n"
            "\nResult (for verbosity = 2 or verbosity = DECODE_TRANSACTIONS):\n"
            "{\n"
            "  \"hash\" : \"hash\",     (string) the block hash (same as "
            "provided)\n"
            "  \"confirmations\" : n,   (numeric) The number of confirmations, "
            "or -1 if the block is not on the main chain\n"
            "  \"size\" : n,            (numeric) The block size\n"
            "  \"height\" : n,          (numeric) The block height or index\n"
            "  \"version\" : n,         (numeric) The block version\n"
            "  \"versionHex\" : \"00000000\", (string) The block version "
            "formatted in hexadecimal\n"
            "  \"merkleroot\" : \"xxxx\", (string) The merkle root\n"
            "  \"num_tx\" : n,          (numeric) The number of transactions\n"
            "  \"tx\" : [               (array of Objects) The transactions in "
            "the format of the getrawtransaction RPC. Different from verbosity "
            "= 1 \"tx\" result.\n"
            "         ,...\n"
            "  ],\n"
            "  \"time\" : ttt,          (numeric) The block time in seconds "
            "since epoch (Jan 1 1970 GMT)\n"
            "  \"mediantime\" : ttt,    (numeric) The median block time in "
            "seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"nonce\" : n,           (numeric) The nonce\n"
            "  \"bits\" : \"1d00ffff\", (string) The bits\n"
            "  \"difficulty\" : x.xxx,  (numeric) The difficulty\n"
            "  \"chainwork\" : \"xxxx\",  (string) Expected number of hashes "
            "required to produce the chain up to this block (in hex)\n"
            "  \"previousblockhash\" : \"hash\",  (string) The hash of the "
            "previous block\n"
            "  \"nextblockhash\" : \"hash\"       (string) The hash of the "
            "next block\n"
            "}\n"
            "\nResult (for verbosity = 3 or verbosity = "
            "DECODE_HEADER_AND_COINBASE):\n"
            "{\n"
            "  \"hash\" : \"hash\",     (string) the block hash (same as "
            "provided)\n"
            "  \"confirmations\" : n,   (numeric) The number of confirmations, "
            "or -1 if the block is not on the main chain\n"
            "  \"size\" : n,            (numeric) The block size\n"
            "  \"height\" : n,          (numeric) The block height or index\n"
            "  \"version\" : n,         (numeric) The block version\n"
            "  \"versionHex\" : \"00000000\", (string) The block version "
            "formatted in hexadecimal\n"
            "  \"merkleroot\" : \"xxxx\", (string) The merkle root\n"
            "  \"num_tx\" : n,          (numeric) The number of transactions\n"
            "  \"tx\" : [               The coinbase transaction in the format "
            "of the getrawtransaction RPC. Different from verbosity = 1 \"tx\" "
            "result.\n"
            "         ,...\n"
            "  ],\n"
            "  \"time\" : ttt,          (numeric) The block time in seconds "
            "since epoch (Jan 1 1970 GMT)\n"
            "  \"mediantime\" : ttt,    (numeric) The median block time in "
            "seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"nonce\" : n,           (numeric) The nonce\n"
            "  \"bits\" : \"1d00ffff\", (string) The bits\n"
            "  \"difficulty\" : x.xxx,  (numeric) The difficulty\n"
            "  \"chainwork\" : \"xxxx\",  (string) Expected number of hashes "
            "required to produce the chain up to this block (in hex)\n"
            "  \"previousblockhash\" : \"hash\",  (string) The hash of the "
            "previous block\n"
            "  \"nextblockhash\" : \"hash\"       (string) The hash of the "
            "next block\n"
            "}\n"
            "\nExamples:\n" +
            HelpExampleCli("getblockbyheight", "\"1214adbda81d7e2a3dd146f6ed09\"") +
            HelpExampleRpc("getblockbyheight", "\"1214adbda81d7e2a3dd146f6ed09\""));
    }

    if(httpReq == nullptr)
        return;

    int32_t nHeight = jsonRPCReq.params[0].get_int();
    if (nHeight < 0 || nHeight > chainActive.Height()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Block height out of range");
    }
    
    int confirmations;
    std::optional<uint256> nextBlockHash;
    CBlockIndex* pblockindex;
    {
        LOCK(cs_main);
        pblockindex = chainActive[nHeight];
        confirmations = ComputeNextBlockAndDepthNL(chainActive.Tip(), pblockindex, nextBlockHash);
    }

    getblockdata(*pblockindex,
                 config,
                 jsonRPCReq,
                 *httpReq,
                 processedInBatch,
                 confirmations,
                 nextBlockHash);
}

void getblockdata(CBlockIndex &pblockindex, const Config &config,
                  const JSONRPCRequest &jsonRPCReq, HTTPRequest &httpReq,
                  bool processedInBatch, const int confirmations,
                  const std::optional<uint256>& nextBlockHash) {

    // previously, false and true were accepted for verbosity 0 and 1
    // respectively. this code maintains backward compatibility.
    GetBlockVerbosity verbosity = GetBlockVerbosity::DECODE_HEADER;

    if (jsonRPCReq.params.size() > 1) {
        parseGetBlockVerbosity(jsonRPCReq.params[1], verbosity);
    }

    try
    {
        switch (verbosity)
        {
            case GetBlockVerbosity::RAW_BLOCK:
                writeBlockChunksAndUpdateMetadata(true, httpReq, pblockindex, jsonRPCReq.id.write(), 
                                                    processedInBatch, RetFormat::RF_JSON);
                break;
            case GetBlockVerbosity::DECODE_HEADER:
                writeBlockJsonChunksAndUpdateMetadata(config, httpReq, false, pblockindex,
                                                        false, processedInBatch, confirmations,
                                                        nextBlockHash, jsonRPCReq.id.write());
                break;
            case GetBlockVerbosity::DECODE_HEADER_AND_COINBASE:
                writeBlockJsonChunksAndUpdateMetadata(config, httpReq, true, pblockindex,
                                                        true, processedInBatch, confirmations,
                                                        nextBlockHash, jsonRPCReq.id.write());
                break;
            case GetBlockVerbosity::DECODE_TRANSACTIONS:
                writeBlockJsonChunksAndUpdateMetadata(config, httpReq, true, pblockindex,
                                                        false, processedInBatch, confirmations,
                                                        nextBlockHash, jsonRPCReq.id.write());
                break;
            default:
                throw JSONRPCError(RPC_MISC_ERROR, "Invalid verbosity type.");
        }
    }
    catch (block_parse_error& ex)
    {
        throw JSONRPCError(RPC_MISC_ERROR, std::string(ex.what()));
    }
}

void writeBlockChunksAndUpdateMetadata(bool isHexEncoded, HTTPRequest &req,
                                       CBlockIndex& blockIndex, const std::string& rpcReqId,
                                       bool processedInBatch, const RetFormat& rf)
{
    CDiskBlockMetaData metadata = blockIndex.GetDiskBlockMetaData();
    bool hasDiskBlockMetaData = !metadata.diskDataHash.IsNull();
    
    std::pair<bool, std::string> range_header = req.GetHeader("Range");
    bool hasRangeHeader = range_header.first;

    uint64_t offset {0};
    uint64_t contentLen {0};
    std::string totalLen = "*";
    std::unique_ptr<CForwardReadonlyStream> stream {nullptr};

    switch (rf)
    {
        case RF_BINARY:
        {
            if (hasRangeHeader)
            {
                try
                {
                    std::string s = range_header.second;
                    if (s.find("bytes=") != 0)
                    {
                        throw block_parse_error("Invalid Range header format, should starts with 'bytes='");
                    }
                    s.erase(0, 6);
                    std::string delimiter = "-";
                    std::string::size_type delimiterPos = s.find(delimiter);
                    if(delimiterPos == std::string::npos)
                    {
                        throw block_parse_error("Invalid Range header format, bytes delimiter not found");
                    }
                    std::string rs_s = s.substr(0, delimiterPos);
                    s.erase(0, delimiterPos + delimiter.length());
                    std::string re_s = s;

                    uint64_t rs = std::stoll(rs_s);
                    uint64_t re = std::stoll(re_s);

                    if (rs > re)
                    {
                        throw block_parse_error("Invalid Range parameter, start > end");
                    }
                    contentLen = re - rs + 1;
                    offset = rs;

                    if (hasDiskBlockMetaData)
                    {
                        if (rs >= metadata.diskDataSize)
                        {
                            throw block_parse_error("Invalid Range parameter, start >= data_size");
                        }

                        uint64_t remain = metadata.diskDataSize - offset;
                        contentLen = std::min(remain, contentLen);
                        totalLen = std::to_string(metadata.diskDataSize);
                    }

                    req.WriteHeader("Content-Length", std::to_string(contentLen));
                    req.WriteHeader("Content-Range", "bytes " + std::to_string(offset) + "-" + 
                        std::to_string(contentLen - 1) + "/" + totalLen);
                }
                catch (const block_parse_error&)
                {
                    // Rethrow
                    throw;
                }
                catch (...)
                {
                    throw block_parse_error("Invalid Range parameter");
                }
            }
            else
            {
                if (hasDiskBlockMetaData)
                {
                    req.WriteHeader("Content-Length", std::to_string(metadata.diskDataSize));
                }
            }
            req.WriteHeader("Content-Type", "application/octet-stream");
            break;
        }
        case RF_HEX:
        {
            if (hasDiskBlockMetaData)
            {
                req.WriteHeader("Content-Length", std::to_string(metadata.diskDataSize * 2));
            }
            req.WriteHeader("Content-Type", "text/plain");
            break;
        }
        case RF_JSON:
        {
            if (!processedInBatch)
            {
                req.WriteHeader("Content-Type", "application/json");
            }
            break;
        }
        default :
            throw block_parse_error("Invalid return type.");

    }

    if (!processedInBatch)
    {
        if (hasRangeHeader) {
            req.StartWritingChunks(HTTP_PARTIAL_CONTENT);
        } else {
            req.StartWritingChunks(HTTP_OK);
        }
    }

    // RPC requests have additional layer around the actual response 
    if (!rpcReqId.empty())
    {
        req.WriteReplyChunk("{\"result\": \"");
    }

    if (hasRangeHeader)
    {
        stream = blockIndex.StreamSyncPartialBlockFromDisk(offset, contentLen);
    }
    else
    {
        stream = blockIndex.StreamSyncBlockFromDisk();
    }
    if (!stream) 
    {
        // Block not found on disk. This could be because we have the block
        // header in our index but don't have the block (for example if a
        // non-whitelisted node sends us an unrequested long chain of valid
        // blocks, we add the headers to our index, but don't accept the block).
        throw block_parse_error(blockIndex.GetBlockHash().GetHex() + " not found on disk");
    }

    CHash256 hasher;
    do
    {
        auto chunk = stream->Read(4096);
        auto begin = reinterpret_cast<const char *>(chunk.Begin());
        if (!isHexEncoded)
        {
            req.WriteReplyChunk({begin, chunk.Size()});
        } 
        else 
        {
            req.WriteReplyChunk(HexStr(begin, begin + chunk.Size()));
        }

        if (!hasDiskBlockMetaData && !hasRangeHeader)
        {
            hasher.Write(chunk.Begin(), chunk.Size());
            metadata.diskDataSize += chunk.Size();
        }
    } while (!stream->EndOfStream());

    if (!hasDiskBlockMetaData)
    {
        hasher.Finalize(reinterpret_cast<uint8_t *>(&metadata.diskDataHash));
        blockIndex.SetBlockIndexFileMetaDataIfNotSet(metadata, mapBlockIndex);
    }

    // RPC requests have additional layer around the actual response 
    if (!rpcReqId.empty())
    {
        req.WriteReplyChunk("\", \"error\": " + NullUniValue.write() + ", \"id\": " + rpcReqId + "}");
    }

    if (!processedInBatch)
    {
        req.StopWritingChunks();
    }
}

void writeBlockJsonChunksAndUpdateMetadata(const Config &config, HTTPRequest &req, bool showTxDetails,
                                           CBlockIndex& blockIndex, bool showOnlyCoinbase, 
                                           bool processedInBatch, const int confirmations, 
                                           const std::optional<uint256>& nextBlockHash,
                                           const std::string& rpcReqId)
{
    CDiskBlockMetaData diskBlockMetaData = blockIndex.GetDiskBlockMetaData();
    
    auto reader = blockIndex.GetDiskBlockStreamReader(diskBlockMetaData.diskDataHash.IsNull());

    if (!reader) 
    {
        throw block_parse_error("Block file " + blockIndex.GetBlockHash().GetHex() + " not available.");
    }

    if (!processedInBatch) 
    {
        req.WriteHeader("Content-Type", "application/json");
        req.StartWritingChunks(HTTP_OK);
    }

    CHttpTextWriter httpWriter(req);
    CJSONWriter jWriter(httpWriter, false);
    jWriter.writeBeginObject();

    // RPC requests have additional layer around the actual response 
    if (!rpcReqId.empty())
    {
        jWriter.writeBeginObject("result");
    }

    jWriter.writeBeginArray("tx");
    bool isGenesisEnabled = IsGenesisEnabled(config, blockIndex.GetHeight());
    do
    {
        const CTransaction& transaction = reader->ReadTransaction();
        if (showTxDetails)
        {
            TxToJSON(transaction, uint256(), isGenesisEnabled, RPCSerializationFlags(), jWriter);
        }
        else
        {
            jWriter.pushV(transaction.GetId().GetHex());
        }
    } while(!reader->EndOfStream() && !showOnlyCoinbase);
    jWriter.writeEndArray();

    // set metadata so it is available when setting header in the next step
    if (diskBlockMetaData.diskDataHash.IsNull() && reader->EndOfStream())
    {
        diskBlockMetaData = reader->getDiskBlockMetadata();
        blockIndex.SetBlockIndexFileMetaDataIfNotSet(diskBlockMetaData, mapBlockIndex);
    }

    writeBlockHeaderJSONFields(jWriter, &blockIndex, confirmations, nextBlockHash, diskBlockMetaData.diskDataHash.IsNull() ? std::nullopt : std::optional<CDiskBlockMetaData>{ diskBlockMetaData });
    // RPC requests have additional layer around the actual response 
    if (!rpcReqId.empty())
    {
        jWriter.writeEndObject();
        jWriter.pushKV("error", nullptr);
        jWriter.pushKVJSONFormatted("id", rpcReqId);

    }

    jWriter.writeEndObject();
    jWriter.flush();

    if (!processedInBatch)
    {
        req.StopWritingChunks();
    }
}

struct CCoinsStats {
    int32_t nHeight;
    uint256 hashBlock;
    uint64_t nTransactions;
    uint64_t nTransactionOutputs;
    uint64_t nBogoSize;
    uint256 hashSerialized;
    uint64_t nDiskSize;
    Amount nTotalAmount;

    CCoinsStats()
        : nHeight(0), nTransactions(0), nTransactionOutputs(0), nBogoSize(0),
          nDiskSize(0), nTotalAmount(0) {}
};

static void ApplyStats(CCoinsStats &stats, CHashWriter &ss, const uint256 &hash,
                       const std::map<uint32_t, CoinWithScript> &outputs) {
    assert(!outputs.empty());
    ss << hash;
    ss << VARINT(outputs.begin()->second.GetHeight() * 2 +
                 outputs.begin()->second.IsCoinBase());
    stats.nTransactions++;
    for (const auto& output : outputs) {
        ss << VARINT(output.first + 1);
        ss << output.second.GetTxOut().scriptPubKey;
        ss << VARINT(output.second.GetTxOut().nValue.GetSatoshis());
        stats.nTransactionOutputs++;
        stats.nTotalAmount += output.second.GetTxOut().nValue;
        stats.nBogoSize +=
            32 /* txid */ + 4 /* vout index */ + 4 /* height + coinbase */ +
            8 /* amount */ + 2 /* scriptPubKey len */ +
            output.second.GetTxOut().scriptPubKey.size() /* scriptPubKey */;
    }
    ss << VARINT(0);
}

//! Calculate statistics about the unspent transaction output set
static bool GetUTXOStats(CoinsDB& coinsTip, CCoinsStats &stats) {
    std::unique_ptr<CCoinsViewDBCursor> pcursor(coinsTip.Cursor());

    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    stats.hashBlock = pcursor->GetBestBlock();
    stats.nHeight = mapBlockIndex.Get(stats.hashBlock)->GetHeight();
    ss << stats.hashBlock;
    uint256 prevkey;
    std::map<uint32_t, CoinWithScript> outputs;
    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        COutPoint key;
        CoinWithScript coin;
        if (pcursor->GetKey(key) && pcursor->GetValue(coin)) {
            if (!outputs.empty() && key.GetTxId() != prevkey) {
                ApplyStats(stats, ss, prevkey, outputs);
                outputs.clear();
            }
            prevkey = key.GetTxId();
            outputs[key.GetN()] = std::move(coin);
        } else {
            return error("%s: unable to read value", __func__);
        }
        pcursor->Next();
    }
    if (!outputs.empty()) {
        ApplyStats(stats, ss, prevkey, outputs);
    }
    stats.hashSerialized = ss.GetHash();
    stats.nDiskSize = coinsTip.EstimateSize();
    return true;
}

UniValue pruneblockchain(const Config &config, const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() != 1) {
        throw std::runtime_error(
            "pruneblockchain\n"
            "\nArguments:\n"
            "1. \"height\"       (numeric, required) The block height to prune "
            "up to. May be set to a discrete height, or a unix timestamp\n"
            "                  to prune blocks whose block time is at least 2 "
            "hours older than the provided timestamp.\n"
            "\nResult:\n"
            "n    (numeric) Height of the last block pruned.\n"
            "\nExamples:\n" +
            HelpExampleCli("pruneblockchain", "1000") +
            HelpExampleRpc("pruneblockchain", "1000"));
    }

    if (!fPruneMode) {
        throw JSONRPCError(
            RPC_MISC_ERROR,
            "Cannot prune blocks because node is not in prune mode.");
    }

    int32_t heightParam = request.params[0].get_int();
    if (heightParam < 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Negative block height.");
    }

    int32_t chainHeight;

    {
        LOCK(cs_main);

        // Height value more than a billion is too high to be a block height, and
        // too low to be a block time (corresponds to timestamp from Sep 2001).
        if (heightParam > 1000000000) {
            // Add a 2 hour buffer to include blocks which might have had old
            // timestamps
            CBlockIndex *pindex =
                chainActive.FindEarliestAtLeast(heightParam - TIMESTAMP_WINDOW);
            if (!pindex) {
                throw JSONRPCError(
                    RPC_INVALID_PARAMETER,
                    "Could not find block with at least the specified timestamp.");
            }
            heightParam = pindex->GetHeight();
        }

        chainHeight = chainActive.Height();
    }

    int32_t height = heightParam;
    if (chainHeight < config.GetChainParams().PruneAfterHeight()) {
        throw JSONRPCError(RPC_MISC_ERROR,
                           "Blockchain is too short for pruning.");
    } else if (height > chainHeight) {
        throw JSONRPCError(
            RPC_INVALID_PARAMETER,
            "Blockchain is shorter than the attempted prune height.");
    } else if (height > chainHeight - config.GetMinBlocksToKeep()) {
        LogPrint(BCLog::RPC, "Attempt to prune blocks close to the tip. "
                             "Retaining the minimum number of blocks.\n");
        height = chainHeight - config.GetMinBlocksToKeep();
    }

    PruneBlockFilesManual(height);
    return uint64_t(height);
}

UniValue gettxoutsetinfo(const Config &config, const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() != 0) {
        throw std::runtime_error(
            "gettxoutsetinfo\n"
            "\nReturns statistics about the unspent transaction output set.\n"
            "Note this call may take some time.\n"
            "\nResult:\n"
            "{\n"
            "  \"height\":n,     (numeric) The current block height (index)\n"
            "  \"bestblock\": \"hex\",   (string) the best block hash hex\n"
            "  \"transactions\": n,      (numeric) The number of transactions\n"
            "  \"txouts\": n,            (numeric) The number of output "
            "transactions\n"
            "  \"bogosize\": n,          (numeric) A database-independent "
            "metric for UTXO set size\n"
            "  \"hash_serialized\": \"hash\",   (string) The serialized hash\n"
            "  \"disk_size\": n,         (numeric) The estimated size of the "
            "chainstate on disk\n"
            "  \"total_amount\": x.xxx          (numeric) The total amount\n"
            "}\n"
            "\nExamples:\n" +
            HelpExampleCli("gettxoutsetinfo", "") +
            HelpExampleRpc("gettxoutsetinfo", ""));
    }

    UniValue ret(UniValue::VOBJ);

    CCoinsStats stats;
    FlushStateToDisk();
    if (GetUTXOStats(*pcoinsTip, stats)) {
        ret.push_back(Pair("height", int64_t(stats.nHeight)));
        ret.push_back(Pair("bestblock", stats.hashBlock.GetHex()));
        ret.push_back(Pair("transactions", int64_t(stats.nTransactions)));
        ret.push_back(Pair("txouts", int64_t(stats.nTransactionOutputs)));
        ret.push_back(Pair("bogosize", int64_t(stats.nBogoSize)));
        ret.push_back(Pair("hash_serialized", stats.hashSerialized.GetHex()));
        ret.push_back(Pair("disk_size", stats.nDiskSize));
        ret.push_back(
            Pair("total_amount", ValueFromAmount(stats.nTotalAmount)));
    } else {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Unable to read UTXO set");
    }
    return ret;
}

UniValue gettxout(const Config &config, const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() < 2 ||
        request.params.size() > 3) {
        throw std::runtime_error(
            "gettxout \"txid\" n ( include_mempool )\n"
            "\nReturns details about an unspent transaction output.\n"
            "\nArguments:\n"
            "1. \"txid\"             (string, required) The transaction id\n"
            "2. \"n\"                (numeric, required) vout number\n"
            "3. \"include_mempool\"  (boolean, optional) Whether to include "
            "the mempool. Default: true."
            "     Note that an unspent output that is spent in the mempool "
            "won't appear.\n"
            "\nResult:\n"
            "{\n"
            "  \"bestblock\" : \"hash\",    (string) the block hash\n"
            "  \"confirmations\" : n,       (numeric) The number of "
            "confirmations\n"
            "  \"value\" : x.xxx,           (numeric) The transaction value "
            "in " +
            CURRENCY_UNIT +
            "\n"
            "  \"scriptPubKey\" : {         (json object)\n"
            "     \"asm\" : \"code\",       (string) \n"
            "     \"hex\" : \"hex\",        (string) \n"
            "     \"reqSigs\" : n,          (numeric) Number of required "
            "signatures\n"
            "     \"type\" : \"pubkeyhash\", (string) The type, eg pubkeyhash\n"
            "     \"addresses\" : [          (array of string) array of "
            "bitcoin addresses\n"
            "        \"address\"     (string) bitcoin address\n"
            "        ,...\n"
            "     ]\n"
            "  },\n"
            "  \"coinbase\" : true|false   (boolean) Coinbase or not\n"
            "  \"confiscation\" : true|false (boolean) Output of confiscation transaction or not\n"
            "}\n"

            "\nExamples:\n"
            "\nGet unspent transactions\n" +
            HelpExampleCli("listunspent", "") + "\nView the details\n" +
            HelpExampleCli("gettxout", "\"txid\" 1") +
            "\nAs a json rpc call\n" +
            HelpExampleRpc("gettxout", "\"txid\", 1"));
    }

    UniValue ret(UniValue::VOBJ);

    std::string strHash = request.params[0].get_str();
    uint256 hash(uint256S(strHash));
    int n = request.params[1].get_int();
    COutPoint out(hash, n);
    bool fMempool = true;
    if (request.params.size() > 2) {
        fMempool = request.params[2].get_bool();
    }

    CoinsDBView tipView{*pcoinsTip};

    auto writeCoin =
        [&](const CoinWithScript& coin)
        {
            auto pindex = mapBlockIndex.Get(tipView.GetBestBlock());

            ret.push_back(Pair("bestblock", pindex->GetBlockHash().GetHex()));
            if (coin.GetHeight() == MEMPOOL_HEIGHT) {
                ret.push_back(Pair("confirmations", 0));
            } else {
                ret.push_back(Pair("confirmations",
                                   int64_t(pindex->GetHeight() - coin.GetHeight() + 1)));
            }
            ret.push_back(Pair("value", ValueFromAmount(coin.GetTxOut().nValue)));
            UniValue o(UniValue::VOBJ);
            int height = (coin.GetHeight() == MEMPOOL_HEIGHT)
                             ? (chainActive.Height() + 1)
                             : coin.GetHeight();
            ScriptPubKeyToUniv(coin.GetTxOut().scriptPubKey, true,
                               IsGenesisEnabled(config, height), o);
            ret.push_back(Pair("scriptPubKey", o));
            ret.push_back(Pair("coinbase", coin.IsCoinBase()));
            ret.push_back(Pair("confiscation", coin.IsConfiscation()));
        };

    if (fMempool) {
        CCoinsViewMemPool view(tipView, mempool);

        if (auto coin = view.GetCoinWithScript(out);
            coin.has_value() && !mempool.IsSpent(out))
        {
            writeCoin( coin.value() );

            return ret;
        }
    }
    else if (auto coin = tipView.GetCoinWithScript(out); coin.has_value())
    {
        writeCoin( coin.value() );

        return ret;
    }

    return NullUniValue;
}

void gettxouts(const Config& config,
               const JSONRPCRequest& request,
               HTTPRequest* httpReq,
               bool processedInBatch)
{
    if (request.fHelp || request.params.size() < 2 ||
        request.params.size() > 3) {
        throw std::runtime_error(
            "gettxouts txidVoutList returnFields ( include_mempool )\n"
            "\nReturns details about an unspent transaction output.\n"
            "Function does not guarantee consistent view (if TXOs statuses change during RPC function execution)\n"
            "\nArguments:\n"
            "1. \"txidVoutList\"          \"[{\"txid\": txid1, \"n\" : n1}, {\"txid\": txid2, \"n\" : n2}]\" \n"
            "(array, required) Array of elements consisting of transaction ids and vout numbers\n"
            "2. \"returnFields\"                (array, required) Fields that we wish to return\n"
            "Options are: scriptPubKey, scriptPubKeyLen, value, isStandard, confirmations, \n"
            " * (meaning all return fields. It should not be used with other return fields.)\n"
            "3. \"include_mempool\"  (boolean, optional) Whether to include "
            "the mempool. Default: true.\n"
            "Note that an unspent output that is spent in the mempool \n"
            "will be displayed as spent.\n"
            "\nResult:\n"
            "{'txouts':\n"
            "[\n"
            "{\n"
            "  \"scriptPubKey\" : \"scriptPubKey \",    (string) scriptPubKey in hexadecimal\n"
            "  \"scriptPubKeyLen\" : n,       (numeric) Length of scriptPubKey\n"
            "  \"value\" : x.xxx,           (numeric) The output value "
            "in " +
            CURRENCY_UNIT + "\n"
            "  \"isStandard\" : true|false,   (boolean) Standard output or not\n"
            "  \"confirmations\" : n,       (numeric) Number of confirmations\n"
            "}\n"
            ", ...\n"
            "]\n"
            "}\n"
            "In case where we cannot get coin we return element: {\"error\" : \"missing\"}\n"
            "In case where coin is in mempool, but is spent we return element: {\"error\" : \"spent\", \n"
            "\"collidedWith\" : {\"txid\" : txid, \"size\" : size, \"hex\" : hex }} \n"
            "collidedWith contains a transaction id, size and hex of transaction that spends TXO. Hex field is not present in output "
            "if transaction already appeared in collidedWith. \n"

            "\nExamples:\n"
            "\nGet unspent transactions\n" +
            HelpExampleCli("listunspent", "") + "\nView the details\n" +
            HelpExampleCli("gettxouts", "\"[{\\\"txid\\\": \\\"txid1\\\", \\\"n\\\": 0}]\" \"[\\\"*\\\"]\" true") +
            "\nAs a json rpc call\n" +
            HelpExampleRpc("gettxouts", "[{\"txid\": \"txid1\", \"n\" : 0}, {\"txid\": \"txid2\", \"n\" : 0}], [\"*\"], true"));
    }

    if(httpReq == nullptr)
        return;

    RPCTypeCheck(request.params, {UniValue::VARR, UniValue::VARR});
    
    UniValue txid_n_pairs = request.params[0].get_array();
    UniValue returnFields = request.params[1].get_array();

    bool fMempool = true;
    if (request.params.size() > 2)
    {
        fMempool = request.params[2].get_bool();
    }

    // parse return fields and save them as flags in returnFieldsFlags variable
    uint32_t returnFieldsFlags = 0;
    const uint32_t scriptPubKeyFlag = 1<<0;
    const uint32_t scriptPubKeyLenFlag = 1<<1;
    const uint32_t valueFlag = 1<<2;
    const uint32_t isStandardFlag = 1<<3;
    const uint32_t confirmationsFlag = 1<<4;


    for(size_t arrayIndex = 0; arrayIndex < returnFields.size(); arrayIndex++)
    {
        std::string returnField = returnFields[arrayIndex].get_str();
        if(returnField == "*")
        {
            // set all flags to true
            returnFieldsFlags = scriptPubKeyFlag | scriptPubKeyLenFlag | valueFlag
                                | isStandardFlag | confirmationsFlag;
            if(returnFields.size() > 1)
            {
                throw JSONRPCError(RPC_INVALID_PARAMS, "\"*\" should not be used with other return fields");
            }
        }
        else if (returnField == "scriptPubKey")
        {
            returnFieldsFlags |= scriptPubKeyFlag;
        }
        else if (returnField == "scriptPubKeyLen")  
        {
            returnFieldsFlags |= scriptPubKeyLenFlag;
        }
        else if (returnField == "value")
        {
            returnFieldsFlags |= valueFlag;
        }
        else if (returnField == "isStandard")
        {
            returnFieldsFlags |= isStandardFlag;
        }
        else if (returnField == "confirmations")
        {
            returnFieldsFlags |= confirmationsFlag;
        }
        else
        {
            throw JSONRPCError(RPC_INVALID_PARAMS, strprintf("Wrong return field: %s", returnField));
        }
    }

    if(returnFieldsFlags == 0)
    {
        throw JSONRPCError(RPC_INVALID_PARAMS, "No return fields set");
    }

    // parse parameters and save them in an array of COutPoint
    std::vector<COutPoint> outPoints; 
    outPoints.reserve(txid_n_pairs.size());
    for (size_t arrayIndex = 0; arrayIndex < txid_n_pairs.size(); arrayIndex++)
    {
        UniValue element = txid_n_pairs[arrayIndex].get_obj();

        std::string txid;
        int n;

        std::vector<std::string> keys = element.getKeys();
        if (keys.size() == 2 && element.exists("txid") && element.exists("n"))
        {
            if(element["txid"].isStr())
            {
                txid = element["txid"].getValStr();
            }
            else
            {
                throw JSONRPCError(RPC_INVALID_PARAMS, "txid is in wrong format");
            }
            
            if(element["n"].isNum())
            {
                n = element["n"].get_int();
            }
            else
            {
                throw JSONRPCError(RPC_INVALID_PARAMS, "vout is not an integer");
            }
        }
        else
        {
            throw JSONRPCError(RPC_INVALID_PARAMS, "Wrong format. Exactly \"txid\" and \"n\" are required fields.");
        }

        uint256 hash(uint256S(txid));
        outPoints.push_back(COutPoint(hash, n));
    }

    if (!processedInBatch)
    {
        httpReq->WriteHeader("Content-Type", "application/json");
        httpReq->StartWritingChunks(HTTP_OK);
    }

    CHttpTextWriter httpWriter(*httpReq);
    CJSONWriter jWriter(httpWriter, false);

    jWriter.writeBeginObject();
    jWriter.writeBeginObject("result");
    jWriter.writeBeginArray("txouts");

    CoinsDBView tipView{ *pcoinsTip };

    auto writeCoin =
        [&](const CoinWithScript& coin)
        {
            if(returnFieldsFlags & scriptPubKeyFlag)
            {
                jWriter.pushKV("scriptPubKey", HexStr(coin.GetTxOut().scriptPubKey));
            }

            if(returnFieldsFlags & scriptPubKeyLenFlag)
            {
                jWriter.pushKV("scriptPubKeyLen", static_cast<int64_t>(coin.GetTxOut().scriptPubKey.size()));
            }

            if(returnFieldsFlags & valueFlag)
            {
                jWriter.pushKVJSONFormatted("value", ValueFromAmount(coin.GetTxOut().nValue).getValStr());
            }

            if(returnFieldsFlags & isStandardFlag)
            {
                int height = (coin.GetHeight() == MEMPOOL_HEIGHT)
                         ? (chainActive.Height() + 1)
                         : coin.GetHeight();
                txnouttype txOutType;
                jWriter.pushKV("isStandard", IsStandard(config, coin.GetTxOut().scriptPubKey, height, txOutType));
            }

            if(returnFieldsFlags & confirmationsFlag)
            {
                int64_t confirmations;
                if (coin.GetHeight() == MEMPOOL_HEIGHT)
                {
                    confirmations = 0;
                }
                else
                {
                    auto pindex = mapBlockIndex.Get(tipView.GetBestBlock());
                    confirmations = int64_t(pindex->GetHeight() - coin.GetHeight() + 1);
                }
                jWriter.pushKV("confirmations", confirmations);
            }
        };

    if (fMempool)
    {
        std::set<TxId> missingTxIds;
        for(size_t arrayIndex = 0; arrayIndex < outPoints.size(); arrayIndex++)
        {
            jWriter.writeBeginObject();

            CCoinsViewMemPool view(tipView, mempool);
            if (auto coin = view.GetCoinWithScript(outPoints[arrayIndex]); !coin.has_value())
            {
                jWriter.pushKV("error", "missing");
            }
            else if(const auto wrapper = mempool.IsSpentBy(outPoints[arrayIndex]))
            {
                // FIXME: This could be reading the transaction from disk!
                const auto tx = wrapper->GetTx();
                jWriter.pushKV("error", "spent");
                jWriter.writeBeginObject("collidedWith");
                jWriter.pushKV("txid", tx->GetId().GetHex());
                jWriter.pushKV("size", int64_t(tx->GetTotalSize()));
                if(missingTxIds.insert(tx->GetId()).second)
                {
                    jWriter.pushK("hex");
                    jWriter.pushQuote();
                    jWriter.flush();
                    // EncodeHexTx supports streaming (large transaction's hex should be chunked)
                    EncodeHexTx(*tx, jWriter.getWriter(), RPCSerializationFlags());
                    jWriter.pushQuote();
                }
                jWriter.writeEndObject();
            }
            else
            {
                writeCoin( coin.value() );
            }

            jWriter.writeEndObject();
        }
    }
    else
    {
        for(size_t arrayIndex = 0; arrayIndex < outPoints.size(); arrayIndex++)
        {
            jWriter.writeBeginObject();

            if (auto coin = tipView.GetCoinWithScript(outPoints[arrayIndex]); !coin.has_value())
            {
                jWriter.pushKV("error", "missing");
            }
            else
            {
                writeCoin( coin.value() );
            }

            jWriter.writeEndObject();
        }
    }

    jWriter.writeEndArray();
    jWriter.writeEndObject();
    jWriter.pushKV("error", nullptr);
    jWriter.pushKVJSONFormatted("id", request.id.write());
    jWriter.writeEndObject();
    jWriter.flush();

    if (!processedInBatch)
    {
        httpReq->StopWritingChunks();
    }
}

UniValue verifychain(const Config &config, const JSONRPCRequest &request) {
    int nCheckLevel = gArgs.GetArg("-checklevel", DEFAULT_CHECKLEVEL);
    int nCheckDepth = gArgs.GetArg("-checkblocks", DEFAULT_CHECKBLOCKS);
    if (request.fHelp || request.params.size() > 2) {
        throw std::runtime_error(
            "verifychain ( checklevel nblocks )\n"
            "\nVerifies blockchain database.\n"
            "\nArguments:\n"
            "1. checklevel   (numeric, optional, 0-4, default=" +
            strprintf("%d", nCheckLevel) +
            ") How thorough the block verification is.\n"
            "2. nblocks      (numeric, optional, default=" +
            strprintf("%d", nCheckDepth) +
            ", 0=all) The number of blocks to check.\n"
            "\nResult:\n"
            "true|false       (boolean) Verified or not\n"
            "\nExamples:\n" +
            HelpExampleCli("verifychain", "") +
            HelpExampleRpc("verifychain", ""));
    }

    if (request.params.size() > 0) {
        nCheckLevel = request.params[0].get_int();
    }
    if (request.params.size() > 1) {
        nCheckDepth = request.params[1].get_int();
    }

    return CVerifyDB().VerifyDB(config, *pcoinsTip, nCheckLevel, nCheckDepth, task::CCancellationSource::Make()->GetToken());
}

/** Implementation of IsSuperMajority with better feedback */
static UniValue SoftForkMajorityDesc(int version, CBlockIndex *pindex,
                                     const Consensus::Params &consensusParams) {
    UniValue rv(UniValue::VOBJ);
    bool activated = false;
    switch (version) {
        case 2:
            activated = pindex->GetHeight() >= consensusParams.BIP34Height;
            break;
        case 3:
            activated = pindex->GetHeight() >= consensusParams.BIP66Height;
            break;
        case 4:
            activated = pindex->GetHeight() >= consensusParams.BIP65Height;
            break;
        case 5:
            activated = pindex->GetHeight() >= consensusParams.CSVHeight;
            break;
    }
    rv.push_back(Pair("status", activated));
    return rv;
}

static UniValue SoftForkDesc(const std::string &name, int version,
                             CBlockIndex *pindex,
                             const Consensus::Params &consensusParams) {
    UniValue rv(UniValue::VOBJ);
    rv.push_back(Pair("id", name));
    rv.push_back(Pair("version", version));
    rv.push_back(
        Pair("reject", SoftForkMajorityDesc(version, pindex, consensusParams)));
    return rv;
}

UniValue getblockchaininfo(const Config &config,
                           const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() != 0) {
        throw std::runtime_error(
            "getblockchaininfo\n"
            "Returns an object containing various state info regarding "
            "blockchain processing.\n"
            "\nResult:\n"
            "{\n"
            "  \"chain\": \"xxxx\",        (string) current network name as "
            "defined in BIP70 (main, test, regtest)\n"
            "  \"blocks\": xxxxxx,         (numeric) the current number of "
            "blocks processed in the server\n"
            "  \"headers\": xxxxxx,        (numeric) the current number of "
            "headers we have validated\n"
            "  \"bestblockhash\": \"...\", (string) the hash of the currently "
            "best block\n"
            "  \"difficulty\": xxxxxx,     (numeric) the current difficulty\n"
            "  \"mediantime\": xxxxxx,     (numeric) median time for the "
            "current best block\n"
            "  \"verificationprogress\": xxxx, (numeric) estimate of "
            "verification progress [0..1]\n"
            "  \"chainwork\": \"xxxx\"     (string) total amount of work in "
            "active chain, in hexadecimal\n"
            "  \"pruned\": xx,             (boolean) if the blocks are subject "
            "to pruning\n"
            "  \"pruneheight\": xxxxxx,    (numeric) lowest-height complete "
            "block stored\n"
            "  \"softforks\": [            (array) status of softforks in "
            "progress\n"
            "     {\n"
            "        \"id\": \"xxxx\",        (string) name of softfork\n"
            "        \"version\": xx,         (numeric) block version\n"
            "        \"reject\": {            (object) progress toward "
            "rejecting pre-softfork blocks\n"
            "           \"status\": xx,       (boolean) true if threshold "
            "reached\n"
            "        },\n"
            "     }, ...\n"
            "  ]\n"
            "}\n"
            "\nExamples:\n" +
            HelpExampleCli("getblockchaininfo", "") +
            HelpExampleRpc("getblockchaininfo", ""));
    }
    
    auto tip = chainActive.Tip();

    UniValue obj(UniValue::VOBJ);
    obj.push_back(Pair("chain", config.GetChainParams().NetworkIDString()));
    obj.push_back(Pair("blocks", int(chainActive.Height())));
    obj.push_back(Pair("headers", mapBlockIndex.GetBestHeader().GetHeight()));
    obj.push_back(
        Pair("bestblockhash", tip->GetBlockHash().GetHex()));
    obj.push_back(Pair("difficulty", double(GetDifficulty(tip))));
    obj.push_back(Pair("mediantime", tip->GetMedianTimePast()));
    obj.push_back(
        Pair("verificationprogress",
             GuessVerificationProgress(config.GetChainParams().TxData(), tip)));
    obj.push_back(Pair("chainwork", tip->GetChainWork().GetHex()));
    obj.push_back(Pair("pruned", fPruneMode));

    const Consensus::Params &consensusParams =
        config.GetChainParams().GetConsensus();
    UniValue softforks(UniValue::VARR);
    softforks.push_back(SoftForkDesc("bip34", 2, tip, consensusParams));
    softforks.push_back(SoftForkDesc("bip66", 3, tip, consensusParams));
    softforks.push_back(SoftForkDesc("bip65", 4, tip, consensusParams));
    // version 5 is introduced only for this RPC (we will never receive block
    // with version 5)
    softforks.push_back(SoftForkDesc("csv", 5, tip, consensusParams));
    obj.push_back(Pair("softforks", softforks));

    if (fPruneMode) {
        // No need for extra locking:
        // We don't care about hasData() stability here as data is always
        // pruned from older to newer and this result is only informative in
        // nature as it can already differ once the result gets beck to the RPC
        // caller.
        const CBlockIndex* block = tip;
        while (block && !block->IsGenesis() && block->GetPrev()->getStatus().hasData())
        {
            block = block->GetPrev();
        }

        obj.push_back(Pair("pruneheight", block->GetHeight()));
    }
    return obj;
}

/** Comparison function for sorting the getchaintips heads.  */
struct CompareBlocksByHeight {
    bool operator()(const CBlockIndex *a, const CBlockIndex *b) const {
        // Make sure that unequal blocks with the same height do not compare
        // equal. Use the pointers themselves to make a distinction.
        if (a->GetHeight() != b->GetHeight()) {
            return (a->GetHeight() > b->GetHeight());
        }

        return a < b;
    }
};

UniValue getchaintips(const Config &config, const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() != 0) {
        throw std::runtime_error(
            "getchaintips\n"
            "Return information about all known tips in the block tree,"
            " including the main chain as well as orphaned branches.\n"
            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"height\": xxxx,         (numeric) height of the chain tip\n"
            "    \"hash\": \"xxxx\",         (string) block hash of the tip\n"
            "    \"branchlen\": 0          (numeric) zero for main chain\n"
            "    \"status\": \"active\"      (string) \"active\" for the main "
            "chain\n"
            "  },\n"
            "  {\n"
            "    \"height\": xxxx,\n"
            "    \"hash\": \"xxxx\",\n"
            "    \"branchlen\": 1          (numeric) length of branch "
            "connecting the tip to the main chain\n"
            "    \"status\": \"xxxx\"        (string) status of the chain "
            "(active, valid-fork, valid-headers, headers-only, invalid)\n"
            "  }\n"
            "]\n"
            "Possible values for status:\n"
            "1.  \"invalid\"               This branch contains at least one "
            "invalid block\n"
            "2.  \"headers-only\"          Not all blocks for this branch are "
            "available, but the headers are valid\n"
            "3.  \"valid-headers\"         All blocks are available for this "
            "branch, but they were never fully validated\n"
            "4.  \"valid-fork\"            This branch is not part of the "
            "active chain, but is fully validated\n"
            "5.  \"active\"                This is the tip of the active main "
            "chain, which is certainly valid\n"
            "\nExamples:\n" +
            HelpExampleCli("getchaintips", "") +
            HelpExampleRpc("getchaintips", ""));
    }

    LOCK(cs_main);

    /**
     * Idea:  the set of chain tips is chainActive.tip, plus orphan blocks which
     * do not have another orphan building off of them.
     * Algorithm:
     *  - Make one pass through mapBlockIndex, picking out the orphan blocks,
     * and also storing a set of the orphan block's pprev pointers.
     *  - Iterate through the orphan blocks. If the block isn't pointed to by
     * another orphan, it is a chain tip.
     *  - add chainActive.Tip()
     */
    std::set<const CBlockIndex *, CompareBlocksByHeight> setTips;
    std::set<const CBlockIndex *> setOrphans;
    std::set<const CBlockIndex *> setPrevs;

    mapBlockIndex.ForEach(
        [&](const CBlockIndex& index)
        {
            if (!chainActive.Contains(&index))
            {
                setOrphans.insert(&index);
                setPrevs.insert(index.GetPrev());
            }
        });

    for (std::set<const CBlockIndex *>::iterator it = setOrphans.begin();
         it != setOrphans.end(); ++it) {
        if (setPrevs.erase(*it) == 0) {
            setTips.insert(*it);
        }
    }

    // Always report the currently active tip.
    setTips.insert(chainActive.Tip());

    /* Construct the output array.  */
    UniValue res(UniValue::VARR);
    for (const CBlockIndex *block : setTips) {
        UniValue obj(UniValue::VOBJ);
        obj.push_back(Pair("height", block->GetHeight()));
        obj.push_back(Pair("hash", block->GetBlockHash().GetHex()));

        const int branchLen =
            block->GetHeight() - chainActive.FindFork(block)->GetHeight();
        obj.push_back(Pair("branchlen", branchLen));

        std::string status;
        if (chainActive.Contains(block)) {
            // This block is part of the currently active chain.
            status = "active";
        } else if (block->getStatus().isInvalid()) {
            // This block or one of its ancestors is invalid.
            status = "invalid";
        } else if (block->GetChainTx() == 0) {
            // This block cannot be connected because full block data for it or
            // one of its parents is missing.
            status = "headers-only";
        } else if (block->IsValid(BlockValidity::SCRIPTS)) {
            // This block is fully validated, but no longer part of the active
            // chain. It was probably the active block once, but was
            // reorganized.
            status = "valid-fork";
        } else if (block->IsValid(BlockValidity::TREE)) {
            // The headers for this block are valid, but it has not been
            // validated. It was probably never part of the most-work chain.
            status = "valid-headers";
        } else {
            // No clue.
            status = "unknown";
        }
        obj.push_back(Pair("status", status));

        res.push_back(obj);
    }

    return res;
}

UniValue mempoolInfoToJSON(const Config& config) {
    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("size", (int64_t)mempool.Size()));
    ret.push_back(Pair(
        "journalsize",
        (int64_t)mempool.getJournalBuilder().getCurrentJournal()->size()));
    ret.push_back(
        Pair("nonfinalsize", (int64_t)mempool.getNonFinalPool().getNumTxns()));
    ret.push_back(Pair("bytes", (int64_t)mempool.GetTotalTxSize()));
    ret.push_back(Pair("usage", (int64_t)mempool.DynamicMemoryUsage()));
    ret.push_back(Pair("usagedisk", (int64_t)mempool.GetDiskUsage()));
    ret.push_back(Pair("usagecpfp", (int64_t)mempool.SecondaryMempoolUsage()));
    ret.push_back(
        Pair("nonfinalusage",
             (int64_t)mempool.getNonFinalPool().estimateMemoryUsage()));
    MempoolSizeLimits limits = MempoolSizeLimits::FromConfig();
    ret.push_back(Pair("maxmempool", static_cast<int64_t>(limits.Memory())));
    ret.push_back(Pair("maxmempoolsizedisk", static_cast<int64_t>(limits.Disk())));
    ret.push_back(Pair("maxmempoolsizecpfp", static_cast<int64_t>(limits.Secondary())));
    ret.push_back(
        Pair("mempoolminfee",
             ValueFromAmount(mempool.GetMinFee(limits.Total()).GetFeePerK())));

    return ret;
}

UniValue getmempoolinfo(const Config &config, const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() != 0) {
        throw std::runtime_error(
            "getmempoolinfo\n"
            "\nReturns details on the active state of the TX memory pool.\n"
            "\nResult:\n"
            "{\n"
            "  \"size\": xxxxx,               (numeric) Current tx count\n"
            "  \"journalsize\": xxxxx,        (numeric) Current tx count "
            "within the journal\n"
            "  \"nonfinalsize\": xxxxx,       (numeric) Current non-final tx "
            "count\n"
            "  \"bytes\": xxxxx,              (numeric) Transaction size.\n"
            "  \"usage\": xxxxx,              (numeric) Total memory usage for the mempool\n"
            "  \"usagedisk\": xxxxx,          (numeric) Total disk usage for storing mempool transactions\n"
            "  \"usagecpfp\": xxxxx,          (numeric) Total memory usage for the low paying transactions\n"
            "  \"nonfinalusage\": xxxxx,      (numeric) Total memory usage for "
            "the non-final mempool\n"
            "  \"maxmempool\": xxxxx,         (numeric) Maximum memory usage for the mempool\n"
            "  \"maxmempoolsizedisk\": xxxxx, (numeric) Maximum disk usage for storing mempool transactions\n"
            "  \"maxmempoolsizecpfp\": xxxxx, (numeric) Maximum memory usage for the low paying transactions\n"
            "  \"mempoolminfee\": xxxxx       (numeric) Minimum fee (in BSV/kB) for tx to be accepted\n"
            "}\n"
            "\nExamples:\n" +
            HelpExampleCli("getmempoolinfo", "") +
            HelpExampleRpc("getmempoolinfo", ""));
    }

    return mempoolInfoToJSON(config);
}

UniValue getorphaninfo(const Config &config, const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() != 0) {
        throw std::runtime_error(
            "getorphaninfo\n"
            "\nReturns details on the active state of the orphan pool.\n"
            "\nResult:\n"
            "{\n"
            "  \"size\": xxxxx,               (numeric) Current tx count\n"
            "}\n"
            "\nExamples:\n" +
            HelpExampleCli("getorphaninfo", "") +
            HelpExampleRpc("getorphaninfo", ""));
    }

    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("size", (int64_t)g_connman->getTxnValidator()->getOrphanTxnsPtr()->getTxnsNumber()));
    return ret;
}


UniValue preciousblock(const Config &config, const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() != 1) {
        throw std::runtime_error(
            "preciousblock \"blockhash\"\n"
            "\nTreats a block as if it were received before others with the "
            "same work.\n"
            "\nA later preciousblock call can override the effect of an "
            "earlier one.\n"
            "\nThe effects of preciousblock are not retained across restarts.\n"
            "\nArguments:\n"
            "1. \"blockhash\"   (string, required) the hash of the block to "
            "mark as precious\n"
            "\nResult:\n"
            "\nExamples:\n" +
            HelpExampleCli("preciousblock", "\"blockhash\"") +
            HelpExampleRpc("preciousblock", "\"blockhash\""));
    }

    std::string strHash = request.params[0].get_str();
    uint256 hash(uint256S(strHash));
    CBlockIndex *pblockindex = mapBlockIndex.Get(hash);

    if (!pblockindex)
    {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
    }

    CValidationState state;
    PreciousBlock(config, state, pblockindex);

    if (!state.IsValid()) {
        throw JSONRPCError(RPC_DATABASE_ERROR, state.GetRejectReason());
    }

    return NullUniValue;
}

UniValue invalidateblock(const Config &config, const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() != 1) {
        throw std::runtime_error(
            "invalidateblock \"blockhash\"\n"
            "\nPermanently marks a block as invalid, as if it "
            "violated a consensus rule.\n"
            "\nArguments:\n"
            "1. \"blockhash\"   (string, required) the hash of "
            "the block to mark as invalid\n"
            "\nResult:\n"
            "\nExamples:\n" +
            HelpExampleCli("invalidateblock", "\"blockhash\"") +
            HelpExampleRpc("invalidateblock", "\"blockhash\""));
    }

    std::string strHash = request.params[0].get_str();
    uint256 hash(uint256S(strHash));
    CValidationState state;

    {
        auto pblockindex = mapBlockIndex.Get(hash);
        if (!pblockindex)
        {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
        }
        
        LOCK(cs_main);

        InvalidateBlock(config, state, pblockindex);
    }

    if (!state.IsValid()) {
        throw JSONRPCError(RPC_DATABASE_ERROR, state.GetRejectReason());
    }

    return NullUniValue;
}

UniValue reconsiderblock(const Config &config, const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() != 1) {
        throw std::runtime_error(
            "reconsiderblock \"blockhash\"\n"
            "\nRemoves invalidity status of a block and its descendants, "
            "reconsider them for activation.\n"
            "This can be used to undo the effects of invalidateblock.\n"
            "\nArguments:\n"
            "1. \"blockhash\"   (string, required) the hash of the block to "
            "reconsider\n"
            "\nResult:\n"
            "\nExamples:\n" +
            HelpExampleCli("reconsiderblock", "\"blockhash\"") +
            HelpExampleRpc("reconsiderblock", "\"blockhash\""));
    }

    std::string strHash = request.params[0].get_str();
    uint256 hash(uint256S(strHash));

    {
        auto pblockindex = mapBlockIndex.Get(hash);
        if (!pblockindex)
        {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
        }

        LOCK(cs_main);

        ResetBlockFailureFlags(pblockindex);
    }

    // state is used to report errors, not block related invalidity
    // (see description of ActivateBestChain)
    CValidationState state;
    mining::CJournalChangeSetPtr changeSet{
        mempool.getJournalBuilder().getNewChangeSet(
            mining::JournalUpdateReason::REORG)};
    
    CScopedBlockOriginRegistry reg(hash, "reconsiderblock");
    
    auto source = task::CCancellationSource::Make();
    ActivateBestChain(task::CCancellationToken::JoinToken(source->GetToken(), GetShutdownToken()), config, state, changeSet);

    if (!state.IsValid()) {
        throw JSONRPCError(RPC_DATABASE_ERROR, state.GetRejectReason());
    }

    return NullUniValue;
}

UniValue softrejectblock(const Config& config, const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 2)
    {
        throw std::runtime_error(R"(softrejectblock "blockhash" numblocks

Marks a block as soft rejected.
Its descendants (up to num_blocks of them) are also automatically soft rejected. This is true for blocks that are already known as well as for future blocks.
Chains whose tip is soft rejected are not considered when selecting best chain.
If tip of active chain becomes soft rejected, it is reorged back to the first block that is not soft rejected.
Block can only be marked as soft rejected if it is currently not considered soft rejected and it would not affect descendant blocks that are already marked as soft rejected.
Value of numblocks can also be increased on a block that was previously marked as soft rejected by calling this function again on the same block. In this case the value of numblocks must be higher than existing value. acceptblock can be used to decrease the value. 

Arguments:
1. "blockhash"   (string, required)  The hash of the block to mark as soft rejected
2. numblocks     (numeric, required) Number of blocks after this one that will also be considered soft rejected (on all possible branches derived from this block)

Result:
Nothing (JSON null value) if successful and an error code otherwise.
    -1: Specified block cannot be marked as soft rejected.
        Response contains general error description while details are provided in bitcoind log file.
        Common reasons for this error are:
            - Block is already considered soft rejected because of its parent and cannot be marked independently.
            - Block is currently marked as soft rejected for the next N block(s) and this number can only be increased when rejecting.
            - Marking block as soft rejected would affect a descendant block that is also marked as soft rejected.
            - Genesis block cannot be soft rejected.
    -8: Invalid parameter value
    -5: Unknown block hash
   -20: Database error. There was an error when trying to reorg active chain to a different tip.
        Soft rejection status of a block was not changed, but active chain may be in unspecified state.

Examples:
)"
            + HelpExampleCli("softrejectblock", "\"blockhash\" 2")
            + HelpExampleRpc("softrejectblock", "\"blockhash\", 2"));
    }

    std::string strHash = request.params[0].get_str();
    uint256 hash(uint256S(strHash));

    int numBlocks = request.params[1].get_int();
    if(numBlocks<0)
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Parameter numblocks must not be negative");
    }

    CValidationState state;
    {
        LOCK(cs_main);

        CBlockIndex* pblockindex = [&hash]{
            auto it = mapBlockIndex.Get(hash);
            if(!it)
            {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
            }
            return it;
        }();

        bool result = SoftRejectBlockNL(config, state, pblockindex, numBlocks);
        if(!result)
        {
            throw JSONRPCError(RPC_MISC_ERROR, "Error marking block as soft rejected");
        }
    }

    if (!state.IsValid())
    {
        throw JSONRPCError(RPC_DATABASE_ERROR, state.GetRejectReason());
    }

    return NullUniValue;
}

UniValue acceptblock(const Config& config, const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
    {
        throw std::runtime_error(R"(acceptblock "blockhash" numblocks

Unmarks a block as soft rejected and update soft rejection status of its descendants.
If best chain is changed as a result of that, active chain is reorged.
Only blocks that were previously marked as soft rejected can be unmarked. I.e.: It is not possible to unmark block that is considered soft rejected because of its parent.
Value of numblocks can also be decreased on a block that was previously marked as soft rejected by calling this function again on the same block. In this case the value of numblocks must be lower than existing value. softrejectblock can be used to increase the value.

Arguments:
1. "blockhash"   (string, required)  The hash of the block that was previously marked as soft rejected
2. numblocks     (numeric, optional) Number of blocks after this one that should still be considered soft rejected (on all possible branches derived from this block)

Result:
Nothing (JSON null value) if successful and an error code otherwise.
    -1: Specified block cannot be unmarked as soft rejected.
        Response contains general error description while details are provided in bitcoind log file.
        Common reasons for this error are:
            - Block is not soft rejected.
            - Block is soft rejected because of its parent and cannot be accepted independently.
            - Block is currently marked as soft rejected for the next N block(s) and this number can only be decreased when accepting.
    -8: Invalid parameter value
    -5: Unknown block hash
   -20: Database error. There was an error when trying to reorg active chain to a different tip.
        Soft rejection status of a block was changed, but active chain may be in unspecified state.

Examples:
)"
            + HelpExampleCli("acceptblock", "\"blockhash\"")
            + HelpExampleRpc("acceptblock", "\"blockhash\"")
            + HelpExampleCli("acceptblock", "\"blockhash\" 2")
            + HelpExampleRpc("acceptblock", "\"blockhash\", 2"));
    }

    std::string strHash = request.params[0].get_str();
    uint256 hash(uint256S(strHash));

    std::optional<int> numBlocks;
    if (request.params.size() > 1)
    {
        numBlocks = request.params[1].get_int();
        if(*numBlocks<0)
        {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Parameter numBlocks must not be negative");
        }
    }


    {
        LOCK(cs_main);

        CBlockIndex* pblockindex = [&hash]{
            auto it = mapBlockIndex.Get(hash);
            if(!it)
            {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
            }
            return it;
        }();

        bool result;
        if(numBlocks.has_value())
        {
            result = AcceptSoftRejectedBlockNL(pblockindex, *numBlocks);
        }
        else
        {
            result = AcceptSoftRejectedBlockNL(pblockindex);
        }
        if(!result)
        {
            throw JSONRPCError(RPC_MISC_ERROR, "Error unmarking block as soft rejected");
        }
    }

    // Activate best chain, since it may be different now when the block is no longer soft rejected.
    // NOTE: We do the same thing as it is done in the function reconsiderblock.

    CValidationState state;
    mining::CJournalChangeSetPtr changeSet{
        mempool.getJournalBuilder().getNewChangeSet(
            mining::JournalUpdateReason::REORG)};

    CScopedBlockOriginRegistry reg(hash, "acceptblock");

    auto source = task::CCancellationSource::Make();
    ActivateBestChain(task::CCancellationToken::JoinToken(source->GetToken(), GetShutdownToken()), config, state, changeSet);

    if (!state.IsValid())
    {
        throw JSONRPCError(RPC_DATABASE_ERROR, state.GetRejectReason());
    }

    return NullUniValue;
}

UniValue getsoftrejectedblocks(const Config& config, const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 1)
    {
        throw std::runtime_error(R"(getsoftrejectedblocks onlymarked

Returns information about blocks that are considered soft rejected. Order of blocks in returned array is unspecified.

Arguments:
1. onlymarked (boolean, optional, default=true) If true, only blocks that are explicitly marked as soft rejected are returned.
                                                If false, blocks that are considered soft rejected because of parent are also returned.

Result:
[
  {
    "blockhash" : "<hash>",          (string)  The block hash
    "height" : <n>,                  (numeric) The block height
    "previousblockhash" : "<hash>",  (string)  The hash of the previous block
    "numblocks": <n>                 (numeric) Number of blocks after this one that are also considered soft rejected (on all possible branches derived from this block)
  }, ...
]

Examples:
)"
            + HelpExampleCli("getsoftrejectedblocks", "")
            + HelpExampleRpc("getsoftrejectedblocks", "")
            + HelpExampleCli("getsoftrejectedblocks", "false")
            + HelpExampleRpc("getsoftrejectedblocks", "false"));
    }

    bool onlyMarked = true;
    if (request.params.size() > 0)
    {
        onlyMarked = request.params[0].get_bool();
    }

    UniValue result(UniValue::VARR);
    mapBlockIndex.ForEach(
    [&](const CBlockIndex& index)
    {
        if(!index.IsSoftRejected())
        {
            return;
        }

        if(index.ShouldBeConsideredSoftRejectedBecauseOfParent() && onlyMarked)
        {
            return;
        }

        UniValue v(UniValue::VOBJ);
        v.push_back(Pair("blockhash", index.GetBlockHash().ToString()));
        v.push_back(Pair("height", index.GetHeight()));
        v.push_back(Pair("previousblockhash", index.GetPrev()->GetBlockHash().ToString()));
        v.push_back(Pair("numblocks", index.GetSoftRejectedFor()));
        result.push_back(v);
    });

    return result;
}

UniValue getchaintxstats(const Config &config, const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() > 2) {
        throw std::runtime_error(
            "getchaintxstats ( nblocks blockhash )\n"
            "\nCompute statistics about the total number and rate of "
            "transactions in the chain.\n"
            "\nArguments:\n"
            "1. nblocks      (numeric, optional) Size of the window in number "
            "of blocks (default: one month).\n"
            "2. \"blockhash\"  (string, optional) The hash of the block that "
            "ends the window.\n"
            "\nResult:\n"
            "{\n"
            "  \"time\": xxxxx,                (numeric) The timestamp for the "
            "final block in the window in UNIX format.\n"
            "  \"txcount\": xxxxx,             (numeric) The total number of "
            "transactions in the chain up to that point.\n"
            "  \"window_block_count\": xxxxx,  (numeric) Size of the window in "
            "number of blocks.\n"
            "  \"window_tx_count\": xxxxx,     (numeric) The number of "
            "transactions in the window. Only returned if "
            "\"window_block_count\" is > 0.\n"
            "  \"window_interval\": xxxxx,     (numeric) The elapsed time in "
            "the window in seconds. Only returned if \"window_block_count\" is "
            "> 0.\n"
            "  \"txrate\": x.xx,               (numeric) The average rate of "
            "transactions per second in the window. Only returned if "
            "\"window_interval\" is > 0.\n"
            "}\n"
            "\nExamples:\n" +
            HelpExampleCli("getchaintxstats", "") +
            HelpExampleRpc("getchaintxstats", "2016"));
    }

    const CBlockIndex *pindex;

    // By default: 1 month
    int blockcount = 30 * 24 * 60 * 60 /
                     config.GetChainParams().GetConsensus().nPowTargetSpacing;

    bool havehash = !request.params[1].isNull();
    uint256 hash;
    if (havehash) {
        hash = uint256S(request.params[1].get_str());
    }

    if (havehash) {
        pindex = mapBlockIndex.Get(hash);
        if (!pindex)
        {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,
                               "Block not found");
        }

        LOCK(cs_main);

        if (!chainActive.Contains(pindex)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER,
                               "Block is not in main chain");
        }
    } else {
        pindex = chainActive.Tip();
    }

    assert(pindex != nullptr);

    if (request.params[0].isNull()) {
        blockcount = std::max(0, std::min(blockcount, pindex->GetHeight() - 1));
    } else {
        blockcount = request.params[0].get_int();

        if (blockcount < 0 ||
            (blockcount > 0 && blockcount >= pindex->GetHeight())) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid block count: "
                                                      "should be between 0 and "
                                                      "the block's height - 1");
        }
    }

    const CBlockIndex *pindexPast =
        pindex->GetAncestor(pindex->GetHeight() - blockcount);
    int nTimeDiff =
        pindex->GetMedianTimePast() - pindexPast->GetMedianTimePast();
    int nTxDiff = pindex->GetChainTx() - pindexPast->GetChainTx();

    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("time", pindex->GetBlockTime()));
    ret.push_back(Pair("txcount", int64_t(pindex->GetChainTx())));
    ret.push_back(Pair("window_block_count", blockcount));
    if (blockcount > 0) {
        ret.push_back(Pair("window_tx_count", nTxDiff));
        ret.push_back(Pair("window_interval", nTimeDiff));
        if (nTimeDiff > 0) {
            ret.push_back(Pair("txrate", double(nTxDiff) / nTimeDiff));
        }
    }

    return ret;
}

template <typename T>
static T CalculateTruncatedMedian(std::vector<T> &scores) {
    size_t size = scores.size();
    if (size == 0) {
        return T();
    }

    std::sort(scores.begin(), scores.end());
    if (size % 2 == 0) {
        return (scores[size / 2 - 1] + scores[size / 2]) / 2;
    } else {
        return scores[size / 2];
    }
}

template <typename T> static inline bool SetHasKeys(const std::set<T> &set) {
    return false;
}
template <typename T, typename Tk, typename... Args>
static inline bool SetHasKeys(const std::set<T> &set, const Tk &key,
                              const Args &... args) {
    return (set.count(key) != 0) || SetHasKeys(set, args...);
}

// outpoint (needed for the utxo index) + nHeight + fCoinBase
static constexpr size_t PER_UTXO_OVERHEAD =
    sizeof(COutPoint) + sizeof(uint32_t) + sizeof(bool);

UniValue getblockstats_impl(const Config &config, const JSONRPCRequest &request, CBlockIndex *pindex);

static UniValue getblockstats(const Config &config,
                              const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() < 1 ||
        request.params.size() > 4) {
        throw std::runtime_error(
            "getblockstats blockhash ( stats )\n"
            "\nCompute per block statistics for a given window. All amounts "
            "are in " +
            CURRENCY_UNIT +
            ".\n"
            "It won't work for some heights with pruning.\n"
            "It won't work without -txindex for utxo_size_inc, *fee or "
            "*feerate stats.\n"
            "\nArguments:\n"
            "1. \"blockhash\"          (string, required) The block hash of "
            "the target block\n"
            "2. \"stats\"              (array,  optional) Values to plot, by "
            "default all values (see result below)\n"
            "    [\n"
            "      \"height\",         (string, optional) Selected statistic\n"
            "      \"time\",           (string, optional) Selected statistic\n"
            "      ,...\n"
            "    ]\n"
            "\nResult:\n"
            "{                           (json object)\n"
            "  \"avgfee\": x.xxx,          (numeric) Average fee in the block\n"
            "  \"avgfeerate\": x.xxx,      (numeric) Average feerate (in " +
            CURRENCY_UNIT +
            " per byte)\n"
            "  \"avgtxsize\": xxxxx,       (numeric) Average transaction size\n"
            "  \"blockhash\": xxxxx,       (string) The block hash (to check "
            "for potential reorgs)\n"
            "  \"height\": xxxxx,          (numeric) The height of the block\n"
            "  \"ins\": xxxxx,             (numeric) The number of inputs "
            "(excluding coinbase)\n"
            "  \"maxfee\": xxxxx,          (numeric) Maximum fee in the block\n"
            "  \"maxfeerate\": xxxxx,      (numeric) Maximum feerate (in " +
            CURRENCY_UNIT +
            " per byte)\n"
            "  \"maxtxsize\": xxxxx,       (numeric) Maximum transaction size\n"
            "  \"medianfee\": x.xxx,       (numeric) Truncated median fee in "
            "the block\n"
            "  \"medianfeerate\": x.xxx,   (numeric) Truncated median feerate "
            "(in " +
            CURRENCY_UNIT +
            " per byte)\n"
            "  \"mediantime\": xxxxx,      (numeric) The block median time "
            "past\n"
            "  \"mediantxsize\": xxxxx,    (numeric) Truncated median "
            "transaction size\n"
            "  \"minfee\": x.xxx,          (numeric) Minimum fee in the block\n"
            "  \"minfeerate\": xx.xx,      (numeric) Minimum feerate (in " +
            CURRENCY_UNIT +
            " per byte)\n"
            "  \"mintxsize\": xxxxx,       (numeric) Minimum transaction size\n"
            "  \"outs\": xxxxx,            (numeric) The number of outputs\n"
            "  \"subsidy\": x.xxx,         (numeric) The block subsidy\n"
            "  \"time\": xxxxx,            (numeric) The block time\n"
            "  \"total_out\": x.xxx,       (numeric) Total amount in all "
            "outputs (excluding coinbase and thus reward [ie subsidy + "
            "totalfee])\n"
            "  \"total_size\": xxxxx,      (numeric) Total size of all "
            "non-coinbase transactions\n"
            "  \"totalfee\": x.xxx,        (numeric) The fee total\n"
            "  \"txs\": xxxxx,             (numeric) The number of "
            "transactions (excluding coinbase)\n"
            "  \"utxo_increase\": xxxxx,   (numeric) The increase/decrease in "
            "the number of unspent outputs\n"
            "  \"utxo_size_inc\": xxxxx,   (numeric) The increase/decrease in "
            "size for the utxo index (not discounting op_return and similar)\n"
            "}\n"
            "\nExamples:\n" +
            HelpExampleCli("getblockstats",
                           "000000000000000001618b0a11306363725fbb8dbecbb0201c2b4064cda00790 \"[\\\"minfeerate\\\",\\\"avgfeerate\\\"]\"") +
            HelpExampleRpc("getblockstats",
                           "\"000000000000000001618b0a11306363725fbb8dbecbb0201c2b4064cda00790\", [\"minfeerate\",\"avgfeerate\"]"));
    }

    const std::string strHash = request.params[0].get_str();
    const uint256 hash(uint256S(strHash));
    auto pindex = mapBlockIndex.Get(hash);
    if (!pindex) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
    }

    LOCK(cs_main);

    if (!chainActive.Contains(pindex)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                            strprintf("Block is not in chain %s",
                                        Params().NetworkIDString()));
    }

    assert(pindex != nullptr);
    return getblockstats_impl(config, request, pindex);
}


static UniValue getblockstatsbyheight(const Config &config,
                                      const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() < 1 ||
        request.params.size() > 4) {
        throw std::runtime_error(
            "getblockstatsbyheight height ( stats )\n"
            "\nCompute per block statistics for a given window. All amounts "
            "are in " +
            CURRENCY_UNIT +
            ".\n"
            "It won't work for some heights with pruning.\n"
            "It won't work without -txindex for utxo_size_inc, *fee or "
            "*feerate stats.\n"
            "\nArguments:\n"
            "1. \"height\"             (numeric, required) The height of "
            "the target block\n"
            "2. \"stats\"              (array,  optional) Values to plot, by "
            "default all values (see result below)\n"
            "    [\n"
            "      \"height\",         (string, optional) Selected statistic\n"
            "      \"time\",           (string, optional) Selected statistic\n"
            "      ,...\n"
            "    ]\n"
            "\nResult:\n"
            "{                           (json object)\n"
            "  \"avgfee\": x.xxx,          (numeric) Average fee in the block\n"
            "  \"avgfeerate\": x.xxx,      (numeric) Average feerate (in " +
            CURRENCY_UNIT +
            " per byte)\n"
            "  \"avgtxsize\": xxxxx,       (numeric) Average transaction size\n"
            "  \"blockhash\": xxxxx,       (string) The block hash (to check "
            "for potential reorgs)\n"
            "  \"height\": xxxxx,          (numeric) The height of the block\n"
            "  \"ins\": xxxxx,             (numeric) The number of inputs "
            "(excluding coinbase)\n"
            "  \"maxfee\": xxxxx,          (numeric) Maximum fee in the block\n"
            "  \"maxfeerate\": xxxxx,      (numeric) Maximum feerate (in " +
            CURRENCY_UNIT +
            " per byte)\n"
            "  \"maxtxsize\": xxxxx,       (numeric) Maximum transaction size\n"
            "  \"medianfee\": x.xxx,       (numeric) Truncated median fee in "
            "the block\n"
            "  \"medianfeerate\": x.xxx,   (numeric) Truncated median feerate "
            "(in " +
            CURRENCY_UNIT +
            " per byte)\n"
            "  \"mediantime\": xxxxx,      (numeric) The block median time "
            "past\n"
            "  \"mediantxsize\": xxxxx,    (numeric) Truncated median "
            "transaction size\n"
            "  \"minfee\": x.xxx,          (numeric) Minimum fee in the block\n"
            "  \"minfeerate\": xx.xx,      (numeric) Minimum feerate (in " +
            CURRENCY_UNIT +
            " per byte)\n"
            "  \"mintxsize\": xxxxx,       (numeric) Minimum transaction size\n"
            "  \"outs\": xxxxx,            (numeric) The number of outputs\n"
            "  \"subsidy\": x.xxx,         (numeric) The block subsidy\n"
            "  \"time\": xxxxx,            (numeric) The block time\n"
            "  \"total_out\": x.xxx,       (numeric) Total amount in all "
            "outputs (excluding coinbase and thus reward [ie subsidy + "
            "totalfee])\n"
            "  \"total_size\": xxxxx,      (numeric) Total size of all "
            "non-coinbase transactions\n"
            "  \"totalfee\": x.xxx,        (numeric) The fee total\n"
            "  \"txs\": xxxxx,             (numeric) The number of "
            "transactions (excluding coinbase)\n"
            "  \"utxo_increase\": xxxxx,   (numeric) The increase/decrease in "
            "the number of unspent outputs\n"
            "  \"utxo_size_inc\": xxxxx,   (numeric) The increase/decrease in "
            "size for the utxo index (not discounting op_return and similar)\n"
            "}\n"
            "\nExamples:\n" +
            HelpExampleCli("getblockstatsbyheight",
                           "620538 \"[\\\"minfeerate\\\",\\\"avgfeerate\\\"]\"") +
            HelpExampleRpc("getblockstatsbyheight",
                           "630538, [\"minfeerate\",\"avgfeerate\"]"));
    }

    LOCK(cs_main);

    CBlockIndex *pindex;
    const int32_t height = request.params[0].get_int();
    const int current_tip = chainActive.Height();
    if (height < 0) {
        throw JSONRPCError(
            RPC_INVALID_PARAMETER,
            strprintf("Target block height %d is negative", height));
    }
    if (height > current_tip) {
        throw JSONRPCError(
            RPC_INVALID_PARAMETER,
            strprintf("Target block height %d after current tip %d", height,
                        current_tip));
    }
    pindex = chainActive[height];

    assert(pindex != nullptr);
    return getblockstats_impl(config, request, pindex);
}


UniValue getblockstats_impl(const Config &config,
                            const JSONRPCRequest &request,
                            CBlockIndex *pindex)
{
    LOCK(cs_main);

    std::set<std::string> stats;
    if (!request.params[1].isNull()) {
        const UniValue stats_univalue = request.params[1].get_array();
        for (unsigned int i = 0; i < stats_univalue.size(); i++) {
            const std::string stat = stats_univalue[i].get_str();
            stats.insert(stat);
        }
    }

    bool txindexFlag = gArgs.GetBoolArg("-txindex", DEFAULT_TXINDEX);

    CBlock block;

    auto reader = pindex->GetDiskBlockStreamReader(false);
    if (!reader)
    {
        throw JSONRPCError(RPC_MISC_ERROR, "Block not available");
    }

    // Calculate everything if nothing selected (default)
    const bool do_all = stats.size() == 0;
    const bool do_mediantxsize = do_all || stats.count("mediantxsize") != 0;
    const bool do_medianfee = do_all || stats.count("medianfee") != 0;
    const bool do_medianfeerate = do_all || stats.count("medianfeerate") != 0;
    const bool loop_inputs =
        do_all || do_medianfee || do_medianfeerate ||
        SetHasKeys(stats, "utxo_size_inc", "totalfee", "avgfee", "avgfeerate",
                   "minfee", "maxfee", "minfeerate", "maxfeerate");
    const bool loop_outputs = do_all || loop_inputs || stats.count("total_out");
    const bool do_calculate_size =
        do_mediantxsize || loop_inputs ||
        SetHasKeys(stats, "total_size", "avgtxsize", "mintxsize", "maxtxsize");

    const int64_t blockMaxSize = config.GetMaxBlockSize();
    Amount maxfee = Amount();
    Amount maxfeerate = Amount();
    Amount minfee = MAX_MONEY;
    Amount minfeerate = MAX_MONEY;
    Amount total_out = Amount();
    Amount totalfee = Amount();
    int64_t inputs = 0;
    int64_t maxtxsize = 0;
    int64_t mintxsize = blockMaxSize;
    int64_t outputs = 0;
    int64_t total_size = 0;
    int64_t utxo_size_inc = 0;
    std::vector<Amount> fee_array;
    std::vector<Amount> feerate_array;
    std::vector<int64_t> txsize_array;

    do
    {
        const CTransaction& transaction = reader->ReadTransaction();
        const CTransaction *tx = &transaction;

        outputs += tx->vout.size();
        Amount tx_total_out = Amount();
        if (loop_outputs) {
            for (const CTxOut &out : tx->vout) {
                tx_total_out += out.nValue;
                utxo_size_inc +=
                    GetSerializeSize(out, SER_NETWORK, PROTOCOL_VERSION) +
                    PER_UTXO_OVERHEAD;
            }
        }

        if (tx->IsCoinBase()) {
            continue;
        }

        // Don't count coinbase's fake input
        inputs += tx->vin.size();
        // Don't count coinbase reward
        total_out += tx_total_out;

        int64_t tx_size = 0;
        if (do_calculate_size) {

            tx_size = tx->GetTotalSize();
            if (do_mediantxsize) {
                txsize_array.push_back(tx_size);
            }
            maxtxsize = std::max(maxtxsize, tx_size);
            mintxsize = std::min(mintxsize, tx_size);
            total_size += tx_size;
        }


        if (loop_inputs) {

            if (!txindexFlag) {
                throw JSONRPCError(RPC_INVALID_PARAMETER,
                                   "One or more of the selected stats requires "
                                   "-txindex enabled");
            }

            Amount tx_total_in = Amount();
            for (const CTxIn &in : tx->vin) {
                CTransactionRef tx_in;
                uint256 hashBlock;
                bool isGenesisEnabled;
                if (!GetTransaction(config, in.prevout.GetTxId(), tx_in, true, hashBlock, isGenesisEnabled)) {
                    throw JSONRPCError(RPC_INTERNAL_ERROR,
                                       std::string("Unexpected internal error "
                                                   "(tx index seems corrupt)"));
                }

                CTxOut prevoutput = tx_in->vout[in.prevout.GetN()];

                tx_total_in += prevoutput.nValue;
                utxo_size_inc -= GetSerializeSize(prevoutput, SER_NETWORK,
                                                  PROTOCOL_VERSION) +
                                 PER_UTXO_OVERHEAD;
            }

            Amount txfee = tx_total_in - tx_total_out;
            assert(MoneyRange(txfee));
            if (do_medianfee) {
                fee_array.push_back(txfee);
            }
            maxfee = std::max(maxfee, txfee);
            minfee = std::min(minfee, txfee);
            totalfee += txfee;

            if(tx_size == 0)
                throw JSONRPCError(RPC_INTERNAL_ERROR,
                                   "Division by zero: tx_size");

            Amount feerate = txfee / tx_size;
            if (do_medianfeerate) {
                feerate_array.push_back(feerate);
            }
            maxfeerate = std::max(maxfeerate, feerate);
            minfeerate = std::min(minfeerate, feerate);
        }
    } while(!reader->EndOfStream());


    size_t numTx = pindex->GetBlockTxCount();
    UniValue ret_all(UniValue::VOBJ);
    ret_all.pushKV("avgfee",
                   ValueFromAmount((numTx > 1)
                                       ? totalfee / int((numTx - 1))
                                       : Amount()));
    ret_all.pushKV("avgfeerate",
                   ValueFromAmount((total_size > 0) ? totalfee / total_size
                                                    : Amount()));
    ret_all.pushKV("avgtxsize", (numTx > 1)
                                    ? total_size / (numTx - 1)
                                    : 0);
    ret_all.pushKV("blockhash", pindex->GetBlockHash().GetHex());
    ret_all.pushKV("height", (int64_t)pindex->GetHeight());
    ret_all.pushKV("ins", inputs);
    ret_all.pushKV("maxfee", ValueFromAmount(maxfee));
    ret_all.pushKV("maxfeerate", ValueFromAmount(maxfeerate));
    ret_all.pushKV("maxtxsize", maxtxsize);
    ret_all.pushKV("medianfee",
                   ValueFromAmount(CalculateTruncatedMedian(fee_array)));
    ret_all.pushKV("medianfeerate",
                   ValueFromAmount(CalculateTruncatedMedian(feerate_array)));
    ret_all.pushKV("mediantime", pindex->GetMedianTimePast());
    ret_all.pushKV("mediantxsize", CalculateTruncatedMedian(txsize_array));
    ret_all.pushKV(
        "minfee",
        ValueFromAmount((minfee == MAX_MONEY) ? Amount() : minfee));
    ret_all.pushKV("minfeerate",
                   ValueFromAmount((minfeerate == MAX_MONEY) ? Amount()
                                                             : minfeerate));
    ret_all.pushKV("mintxsize", mintxsize == blockMaxSize ? 0 : mintxsize);
    ret_all.pushKV("outs", outputs);
    ret_all.pushKV("subsidy", ValueFromAmount(GetBlockSubsidy(
                                  pindex->GetHeight(), Params().GetConsensus())));
    ret_all.pushKV("time", pindex->GetBlockTime());
    ret_all.pushKV("total_out", ValueFromAmount(total_out));
    ret_all.pushKV("total_size", total_size);
    ret_all.pushKV("totalfee", ValueFromAmount(totalfee));
    ret_all.pushKV("txs", (int64_t)pindex->GetBlockTxCount());
    ret_all.pushKV("utxo_increase", outputs - inputs);
    ret_all.pushKV("utxo_size_inc", utxo_size_inc);

    if (do_all) {
        return ret_all;
    }

    UniValue ret(UniValue::VOBJ);
    for (const std::string &stat : stats) {
        const UniValue &value = ret_all[stat];
        if (value.isNull()) {
            throw JSONRPCError(
                RPC_INVALID_PARAMETER,
                strprintf("Invalid selected statistic %s", stat));
        }
        ret.pushKV(stat, value);
    }
    return ret;
}

UniValue checkjournal(const Config &config, const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() != 0) {
        throw std::runtime_error("checkjournal\n"
                                 "\nChecks for consistency between the TX "
                                 "memory pool and the block assembly journal.\n"
                                 "\nResult:\n"
                                 "{\n"
                                 "  \"ok\": xx,                    (boolean) "
                                 "True if check passed, False otherwise\n"
                                 "  \"errors\": xxxxx,             (string) If "
                                 "check failed, a string listing the errors\n"
                                 "}\n"
                                 "\nExamples:\n" +
                                 HelpExampleCli("checkjournal", "") +
                                 HelpExampleRpc("checkjournal", ""));
    }

    std::string checkResult{mempool.CheckJournal()};

    UniValue result{UniValue::VOBJ};
    if (checkResult.empty()) {
        result.push_back(Pair("ok", true));
    } else {
        result.push_back(Pair("ok", false));
        result.push_back(Pair("errors", checkResult));
    }

    return result;
}

UniValue rebuildjournal(const Config &config, const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() != 0) {
        throw std::runtime_error(
            "rebuildjournal\n"
            "\nForces the block assembly journal and the TX mempool to be rebuilt to make them "
            "consistent with each other.\n"
            "\nResult:\n"
            "\nExamples:\n" +
            HelpExampleCli("rebuildjournal", "") +
            HelpExampleRpc("rebuildjournal", ""));
    }

    auto changeSet = mempool.RebuildMempool();
    changeSet->apply();

    return NullUniValue;
}

static UniValue getblockchainactivity(const Config &config,
                                      const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() != 0) {
        throw std::runtime_error(
            "getblockchainactivity\n"
            "\nReturn number of blocks and transactions being "
            "processed/waiting for processing at the moment\n"
            "\nResult:\n"
            "{\n"
            "  \"blocks\": xx,          (integer) Number of blocks\n"
            "  \"transactions\": xx,    (integer) Number of transactions\n"
            "}\n"
            "\nExamples:\n" +
            HelpExampleCli("getblockchainactivity", "") +
            HelpExampleRpc("getblockchainactivity", ""));
    }

    if (!g_connman) {
        throw JSONRPCError(
            RPC_CLIENT_P2P_DISABLED,
            "Error: Peer-to-peer functionality missing or disabled");
    }

    UniValue result{UniValue::VOBJ};

    result.push_back(Pair("blocks", GetProcessingBlocksCount()));
    static_assert(std::numeric_limits<size_t>::max() <=
                  std::numeric_limits<uint64_t>::max());
    result.push_back(
        Pair("transactions",
             static_cast<uint64_t>(
                 g_connman->getTxnValidator()->GetTransactionsInQueueCount())));

    return result;
}

static UniValue waitaftervalidatingblock(const Config &config,
                                         const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() != 2) {
        throw std::runtime_error(
            "WARNING: For testing purposes only! Can hang a node/create a fork."
            "\n\n"
            "waitaftervalidatingblock \"blockhash\" \"action\"\n"
            "\nMakes specific block to wait before validation completion\n"
            "\nReturn the information about our action"
            "\nResult\n"
            "  blockhash (string) blockhash we added or removed\n"
            "  action (string) add or remove\n"
            "\nExamples:\n" +
            HelpExampleCli("waitaftervalidatingblock",
                           "\"blockhash\" \"add\"") +
            HelpExampleRpc("waitaftervalidatingblock",
                           "\"blockhash\", \"add\""));
    }

    std::string strHash = request.params[0].get_str();
    if (strHash.size() != 64 || !IsHex(strHash)) {
        return JSONRPCError(RPC_PARSE_ERROR, "Wrong hexdecimal string");
    }

    std::string strAction = request.params[1].get_str();
    if (strAction != "add" && strAction != "remove") {
        return JSONRPCError(RPC_TYPE_ERROR, "Wrong action");
    }

    uint256 blockHash(uint256S(strHash));

    blockValidationStatus.waitAfterValidation(blockHash, strAction);

    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("blockhash", blockHash.GetHex()));
    ret.push_back(Pair("action", strAction));

    return ret;
}

static UniValue getcurrentlyvalidatingblocks(const Config &config,
                                             const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() != 0) {
        throw std::runtime_error(
            "getcurrentlyvalidatingblocks\n"
            "\nReturn the block hashes of blocks that are currently validating"
            "\nResult\n"
            "[ blockhashes ]     (array) hashes of blocks\n"
            "\nExamples:\n" +
            HelpExampleCli("getcurrentlyvalidatingblocks", "") +
            HelpExampleRpc("getcurrentlyvalidatingblocks", ""));
    }

    UniValue blockHashes(UniValue::VARR);
    for (uint256 hash : blockValidationStatus.getCurrentlyValidatingBlocks()) {
        blockHashes.push_back(hash.GetHex());
    }

    return blockHashes;
}

static UniValue getwaitingblocks(const Config &config,
                                 const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() != 0) {
        throw std::runtime_error(
            "getwaitingblocks\n"
            "\nReturn the block hashes of blocks that are currently waiting "
            "validation completion"
            "\nResult\n"
            "[ blockhashes ]     (array) hashes of blocks\n"
            "\nExamples:\n" +
            HelpExampleCli("getwaitingblocks", "") +
            HelpExampleRpc("getwaitingblocks", ""));
    }

    UniValue blockHashes(UniValue::VARR);
    for (uint256 hash :
         blockValidationStatus.getWaitingAfterValidationBlocks()) {
        blockHashes.push_back(hash.GetHex());
    }

    return blockHashes;
}

UniValue waitforptvcompletion(const Config &config, const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() != 0) {
        throw std::runtime_error(
            "waitforptvcompletion\n"
            "\nWaits until the txn validation queues are empty (including the orphan pool).\n"
            "\nResult:\n"
            "NullUniValue\n"
            "\nExamples:\n" +
            HelpExampleCli("waitforptvcompletion", "") +
            HelpExampleRpc("waitforptvcompletion", ""));
    }

    LogPrint(BCLog::TXNVAL,"waitforptvcompletion: before waitForEmptyQueue()\n");
    g_connman->getTxnValidator()->waitForEmptyQueue();
    LogPrint(BCLog::TXNVAL,"waitforptvcompletion: after waitForEmptyQueue()\n");
    return NullUniValue;
}

// clang-format off
static const CRPCCommand commands[] = {
    //  category            name                      actor (function)        okSafe argNames
    //  ------------------- ------------------------  ----------------------  ------ ----------
    { "blockchain",         "getblockchaininfo",      getblockchaininfo,      true,  {} },
    { "blockchain",         "getchaintxstats",        &getchaintxstats,       true,  {"nblocks", "blockhash"} },
    { "blockchain",         "getbestblockhash",       getbestblockhash,       true,  {} },
    { "blockchain",         "getblockcount",          getblockcount,          true,  {} },
    { "blockchain",         "getblock",               getblock,               true,  {"blockhash","verbosity|verbose"} },
    { "blockchain",         "getblockbyheight",       getblockbyheight,       true,  {"blockhash","verbosity|verbose"} },
    { "blockchain",         "getblockhash",           getblockhash,           true,  {"height"} },
    { "blockchain",         "getblockheader",         getblockheader,         true,  {"blockhash","verbosity|verbose"} },
    { "blockchain",         "getblockstats",          getblockstats,          true,  {"blockhash","stats"} },
    { "blockchain",         "getblockstatsbyheight",  getblockstatsbyheight,  true,  {"height","stats"} },
    { "blockchain",         "getchaintips",           getchaintips,           true,  {} },
    { "blockchain",         "getdifficulty",          getdifficulty,          true,  {} },
    { "blockchain",         "getmempoolancestors",    getmempoolancestors,    true,  {"txid","verbose"} },
    { "blockchain",         "getmempooldescendants",  getmempooldescendants,  true,  {"txid","verbose"} },
    { "blockchain",         "getmempoolentry",        getmempoolentry,        true,  {"txid"} },
    { "blockchain",         "getmempoolinfo",         getmempoolinfo,         true,  {} },
    { "blockchain",         "getrawmempool",          getrawmempool,          true,  {"verbose"} },
    { "blockchain",         "getrawnonfinalmempool",  getrawnonfinalmempool,  true,  {} },
    { "blockchain",         "gettxout",               gettxout,               true,  {"txid","n","include_mempool"} },
    { "blockchain",         "gettxouts",              gettxouts,              true,  {"txids_vouts","return_fields","include_mempool"} },
    { "blockchain",         "gettxoutsetinfo",        gettxoutsetinfo,        true,  {} },
    { "blockchain",         "pruneblockchain",        pruneblockchain,        true,  {"height"} },
    { "blockchain",         "verifychain",            verifychain,            true,  {"checklevel","nblocks"} },
    { "blockchain",         "preciousblock",          preciousblock,          true,  {"blockhash"} },
    { "blockchain",         "checkjournal",           checkjournal,           true,  {} },
    { "blockchain",         "rebuildjournal",         rebuildjournal,         true,  {} },

    /* Not shown in help */
    { "hidden",             "invalidateblock",        invalidateblock,        true,  {"blockhash"} },
    { "hidden",             "reconsiderblock",        reconsiderblock,        true,  {"blockhash"} },
    { "hidden",             "softrejectblock",        softrejectblock,        true,  {"blockhash","numblocks"} },
    { "hidden",             "acceptblock",            acceptblock,            true,  {"blockhash","numblocks"} },
    { "hidden",             "getsoftrejectedblocks",  getsoftrejectedblocks,  true,  {"onlymarked"} },
    { "hidden",             "waitfornewblock",        waitfornewblock,        true,  {"timeout"} },
    { "hidden",             "waitforblockheight",     waitforblockheight,     true,  {"height","timeout"} },
    { "hidden",             "getblockchainactivity",  getblockchainactivity,  true,  {} },
    { "hidden",             "getcurrentlyvalidatingblocks",     getcurrentlyvalidatingblocks,     true,  {} },
    { "hidden",             "waitaftervalidatingblock",         waitaftervalidatingblock,         true,  {"blockhash","action"} },
    { "hidden",             "getwaitingblocks",                 getwaitingblocks,            true,  {} },
    { "hidden",             "getorphaninfo",                    getorphaninfo, true, {} },
    { "hidden",             "waitforptvcompletion",             waitforptvcompletion, true, {} },
};
// clang-format on

void RegisterBlockchainRPCCommands(CRPCTable &t) {
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++) {
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
    }
}
