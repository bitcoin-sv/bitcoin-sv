// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2019 The Bitcoin Core developers
// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#include "rpc/blockchain.h"

#include "amount.h"
#include "blockfileinfostore.h"
#include "chain.h"
#include "chainparams.h"
#include "checkpoints.h"
#include "coins.h"
#include "config.h"
#include "consensus/validation.h"
#include "core_io.h"
#include "hash.h"
#include "mining/journal_builder.h"
#include "policy/policy.h"
#include "primitives/transaction.h"
#include "rpc/server.h"
#include "rpc/tojson.h"
#include "streams.h"
#include "sync.h"
#include "taskcancellation.h"
#include "txmempool.h"
#include "txn_validator.h"
#include "util.h"
#include "utilstrencodings.h"
#include "validation.h"
#include "init.h"

#include <boost/algorithm/string/case_conv.hpp> // for boost::to_upper
#include <boost/thread/thread.hpp>              // boost::thread::interrupt

#include <condition_variable>
#include <cstdint>
#include <mutex>

struct CUpdatedBlock {
    uint256 hash;
    int height;
};

static std::mutex cs_blockchange;
static std::condition_variable cond_blockchange;
static CUpdatedBlock latestblock;

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

    return GetDifficultyFromBits(blockindex->nBits);
}

UniValue blockheaderToJSON(const CBlockIndex *blockindex) {
    UniValue result(UniValue::VOBJ);
    result.push_back(Pair("hash", blockindex->GetBlockHash().GetHex()));
    int confirmations = -1;
    const CBlockIndex *pnext = nullptr;
    {
        LOCK(cs_main);

        // Only report confirmations if the block is on the main chain
        if (chainActive.Contains(blockindex)) {
            confirmations = chainActive.Height() - blockindex->nHeight + 1;
            pnext = chainActive.Next(blockindex);
        }
    }
    result.push_back(Pair("confirmations", confirmations));
    result.push_back(Pair("height", blockindex->nHeight));
    result.push_back(Pair("version", blockindex->nVersion));
    result.push_back(
        Pair("versionHex", strprintf("%08x", blockindex->nVersion)));
    result.push_back(Pair("merkleroot", blockindex->hashMerkleRoot.GetHex()));
    if (blockindex->nTx > 0) {
        result.push_back(Pair("num_tx", uint64_t(blockindex->nTx)));
    }
    result.push_back(Pair("time", int64_t(blockindex->nTime)));
    result.push_back(
        Pair("mediantime", int64_t(blockindex->GetMedianTimePast())));
    result.push_back(Pair("nonce", uint64_t(blockindex->nNonce)));
    result.push_back(Pair("bits", strprintf("%08x", blockindex->nBits)));
    result.push_back(Pair("difficulty", GetDifficulty(blockindex)));
    result.push_back(Pair("chainwork", blockindex->nChainWork.GetHex()));

    if (blockindex->pprev) {
        result.push_back(Pair("previousblockhash",
                              blockindex->pprev->GetBlockHash().GetHex()));
    }

    if (pnext) {
        result.push_back(Pair("nextblockhash", pnext->GetBlockHash().GetHex()));
    }
    return result;
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

    LOCK(cs_main);
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

    LOCK(cs_main);
    return chainActive.Tip()->GetBlockHash().GetHex();
}

void RPCNotifyBlockChange(bool ibd, const CBlockIndex *pindex) {
    if (pindex) {
        std::lock_guard<std::mutex> lock(cs_blockchange);
        latestblock.hash = pindex->GetBlockHash();
        latestblock.height = pindex->nHeight;
    }
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

    CUpdatedBlock block;
    {
        std::unique_lock<std::mutex> lock(cs_blockchange);
        block = latestblock;
        if (timeout) {
            cond_blockchange.wait_for(
                lock, std::chrono::milliseconds(timeout), [&block] {
                    return latestblock.height != block.height ||
                           latestblock.hash != block.hash || !IsRPCRunning();
                });
        } else {
            cond_blockchange.wait(lock, [&block] {
                return latestblock.height != block.height ||
                       latestblock.hash != block.hash || !IsRPCRunning();
            });
        }
        block = latestblock;
    }
    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("hash", block.hash.GetHex()));
    ret.push_back(Pair("height", block.height));
    return ret;
}

UniValue waitforblock(const Config &config, const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() < 1 ||
        request.params.size() > 2) {
        throw std::runtime_error(
            "waitforblock <blockhash> (timeout)\n"
            "\nWaits for a specific new block and returns useful info about "
            "it.\n"
            "\nReturns the current block on timeout or exit.\n"
            "\nArguments:\n"
            "1. \"blockhash\" (required, string) Block hash to wait for.\n"
            "2. timeout       (int, optional, default=0) Time in milliseconds "
            "to wait for a response. 0 indicates no timeout.\n"
            "\nResult:\n"
            "{                           (json object)\n"
            "  \"hash\" : {       (string) The blockhash\n"
            "  \"height\" : {     (int) Block height\n"
            "}\n"
            "\nExamples:\n" +
            HelpExampleCli("waitforblock", "\"0000000000079f8ef3d2c688c244eb7a4"
                                           "570b24c9ed7b4a8c619eb02596f8862\", "
                                           "1000") +
            HelpExampleRpc("waitforblock", "\"0000000000079f8ef3d2c688c244eb7a4"
                                           "570b24c9ed7b4a8c619eb02596f8862\", "
                                           "1000"));
    }

    int timeout = 0;

    uint256 hash = uint256S(request.params[0].get_str());

    if (request.params.size() > 1) {
        timeout = request.params[1].get_int();
    }

    CUpdatedBlock block;
    {
        std::unique_lock<std::mutex> lock(cs_blockchange);
        if (timeout) {
            cond_blockchange.wait_for(
                lock, std::chrono::milliseconds(timeout), [&hash] {
                    return latestblock.hash == hash || !IsRPCRunning();
                });
        } else {
            cond_blockchange.wait(lock, [&hash] {
                return latestblock.hash == hash || !IsRPCRunning();
            });
        }
        block = latestblock;
    }

    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("hash", block.hash.GetHex()));
    ret.push_back(Pair("height", block.height));
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

    int height = request.params[0].get_int();

    if (request.params.size() > 1) {
        timeout = request.params[1].get_int();
    }

    CUpdatedBlock block;
    {
        std::unique_lock<std::mutex> lock(cs_blockchange);
        if (timeout) {
            cond_blockchange.wait_for(
                lock, std::chrono::milliseconds(timeout), [&height] {
                    return latestblock.height >= height || !IsRPCRunning();
                });
        } else {
            cond_blockchange.wait(lock, [&height] {
                return latestblock.height >= height || !IsRPCRunning();
            });
        }
        block = latestblock;
    }
    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("hash", block.hash.GetHex()));
    ret.push_back(Pair("height", block.height));
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

    LOCK(cs_main);
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
           "    \"startingpriority\" : n, (numeric) DEPRECATED. Priority when "
           "transaction entered pool\n"
           "    \"currentpriority\" : n,  (numeric) DEPRECATED. Transaction "
           "priority now\n"
           "    \"descendantcount\" : n,  (numeric) number of in-mempool "
           "descendant transactions (including this one)\n"
           "    \"descendantsize\" : n,   (numeric) virtual transaction size "
           "of in-mempool descendants (including this one)\n"
           "    \"descendantfees\" : n,   (numeric) modified fees (see above) "
           "of in-mempool descendants (including this one)\n"
           "    \"ancestorcount\" : n,    (numeric) number of in-mempool "
           "ancestor transactions (including this one)\n"
           "    \"ancestorsize\" : n,     (numeric) virtual transaction size "
           "of in-mempool ancestors (including this one)\n"
           "    \"ancestorfees\" : n,     (numeric) modified fees (see above) "
           "of in-mempool ancestors (including this one)\n"
           "    \"depends\" : [           (array) unconfirmed transactions "
           "used as inputs for this transaction\n"
           "        \"transactionid\",    (string) parent transaction id\n"
           "       ... ]\n";
}

void entryToJSONNL(UniValue &info, const CTxMemPoolEntry &e) {
    info.push_back(Pair("size", (int)e.GetTxSize()));
    info.push_back(Pair("fee", ValueFromAmount(e.GetFee())));
    info.push_back(Pair("modifiedfee", ValueFromAmount(e.GetModifiedFee())));
    info.push_back(Pair("time", e.GetTime()));
    info.push_back(Pair("height", (int)e.GetHeight()));
    info.push_back(Pair("startingpriority", e.GetPriority(e.GetHeight())));
    info.push_back(
        Pair("currentpriority", e.GetPriority(chainActive.Height())));
    info.push_back(Pair("descendantcount", e.GetCountWithDescendants()));
    info.push_back(Pair("descendantsize", e.GetSizeWithDescendants()));
    info.push_back(
        Pair("descendantfees", e.GetModFeesWithDescendants().GetSatoshis()));
    info.push_back(Pair("ancestorcount", e.GetCountWithAncestors()));
    info.push_back(Pair("ancestorsize", e.GetSizeWithAncestors()));
    info.push_back(
        Pair("ancestorfees", e.GetModFeesWithAncestors().GetSatoshis()));
    const CTransaction &tx = e.GetTx();
    std::set<std::string> setDepends;
    for (const CTxIn &txin : tx.vin) {
        if (mempool.ExistsNL(txin.prevout.GetTxId())) {
            setDepends.insert(txin.prevout.GetTxId().ToString());
        }
    }

    UniValue depends(UniValue::VARR);
    for (const std::string &dep : setDepends) {
        depends.push_back(dep);
    }

    info.push_back(Pair("depends", depends));
}

UniValue mempoolToJSON(bool fVerbose = false) {
    if (fVerbose) {
        std::shared_lock lock(mempool.smtx);
        UniValue o(UniValue::VOBJ);
        for (const CTxMemPoolEntry &e : mempool.mapTx) {
            const uint256 &txid = e.GetTx().GetId();
            UniValue info(UniValue::VOBJ);
            entryToJSONNL(info, e);
            o.push_back(Pair(txid.ToString(), info));
        }
        return o;
    } else {
        std::vector<uint256> vtxids;
        mempool.QueryHashes(vtxids);

        UniValue a(UniValue::VARR);
        for (const uint256 &txid : vtxids) {
            a.push_back(txid.ToString());
        }

        return a;
    }
}

UniValue getrawmempool(const Config &config, const JSONRPCRequest &request) {
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

    bool fVerbose = false;
    if (request.params.size() > 0) {
        fVerbose = request.params[0].get_bool();
    }

    return mempoolToJSON(fVerbose);
}

UniValue getrawnonfinalmempool(const Config &config,
                               const JSONRPCRequest &request) {
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

    UniValue arr{UniValue::VARR};
    for (const uint256 &txid : mempool.getNonFinalPool().getTxnIDs()) {
        arr.push_back(txid.ToString());
    }

    return arr;
}

UniValue getmempoolancestors(const Config &config,
                             const JSONRPCRequest &request) {
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

    bool fVerbose = false;
    if (request.params.size() > 1) {
        fVerbose = request.params[1].get_bool();
    }

    uint256 hash = ParseHashV(request.params[0], "parameter 1");

    std::shared_lock lock(mempool.smtx);

    CTxMemPool::txiter txIter = mempool.mapTx.find(hash);
    if (txIter == mempool.mapTx.end()) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,
                           "Transaction not in mempool");
    }
    CTxMemPool::setEntries setAncestors;
    uint64_t noLimit = std::numeric_limits<uint64_t>::max();
    std::string dummy;
    mempool.CalculateMemPoolAncestorsNL(*txIter, setAncestors, noLimit, noLimit,
                                        noLimit, noLimit, dummy, false);
    if (!fVerbose) {
        UniValue o(UniValue::VARR);
        for (CTxMemPool::txiter ancestorIt : setAncestors) {
            o.push_back(ancestorIt->GetTx().GetId().ToString());
        }
        return o;
    } else {
        UniValue o(UniValue::VOBJ);
        for (CTxMemPool::txiter ancestorIt : setAncestors) {
            const CTxMemPoolEntry &e = *ancestorIt;
            const uint256 &_hash = e.GetTx().GetId();
            UniValue info(UniValue::VOBJ);
            entryToJSONNL(info, e);
            o.push_back(Pair(_hash.ToString(), info));
        }
        return o;
    }
}

UniValue getmempooldescendants(const Config &config,
                               const JSONRPCRequest &request) {
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

    bool fVerbose = false;
    if (request.params.size() > 1) fVerbose = request.params[1].get_bool();

    uint256 hash = ParseHashV(request.params[0], "parameter 1");

    std::shared_lock lock(mempool.smtx);

    // Check if tx is present in the mempool
    CTxMemPool::txiter txIter = mempool.mapTx.find(hash);
    if (txIter == mempool.mapTx.end()) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,
                           "Transaction not in mempool");
    }
    CTxMemPool::setEntries setDescendants;
    // Calculate descendants
    mempool.CalculateDescendantsNL(txIter, setDescendants);
    // Exclude the given tx from the output
    setDescendants.erase(txIter);
    if (!fVerbose) {
        UniValue o(UniValue::VARR);
        for (CTxMemPool::txiter descendantIt : setDescendants) {
            o.push_back(descendantIt->GetTx().GetId().ToString());
        }
        return o;
    } else {
        UniValue o(UniValue::VOBJ);
        for (CTxMemPool::txiter descendantIt : setDescendants) {
            const CTxMemPoolEntry &e = *descendantIt;
            const uint256 &_hash = e.GetTx().GetId();
            UniValue info(UniValue::VOBJ);
            entryToJSONNL(info, e);
            o.push_back(Pair(_hash.ToString(), info));
        }
        return o;
    }
}

UniValue getmempoolentry(const Config &config, const JSONRPCRequest &request) {
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

    uint256 hash = ParseHashV(request.params[0], "parameter 1");

    std::shared_lock lock(mempool.smtx);

    CTxMemPool::txiter txIter = mempool.mapTx.find(hash);
    if (txIter == mempool.mapTx.end()) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,
                           "Transaction not in mempool");
    }
    const CTxMemPoolEntry &e = *txIter;
    UniValue info(UniValue::VOBJ);
    entryToJSONNL(info, e);
    return info;
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

    int nHeight = request.params[0].get_int();
    if (nHeight < 0 || nHeight > chainActive.Height()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Block height out of range");
    }

    CBlockIndex *pblockindex = chainActive[nHeight];
    return pblockindex->GetBlockHash().GetHex();
}

UniValue getblockheader(const Config &config, const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() < 1 ||
        request.params.size() > 2) {
        throw std::runtime_error(
            "getblockheader \"hash\" ( verbose )\n"
            "\nIf verbose is false, returns a string that is serialized, "
            "hex-encoded data for blockheader 'hash'.\n"
            "If verbose is true, returns an Object with information about "
            "blockheader <hash>.\n"
            "\nArguments:\n"
            "1. \"hash\"          (string, required) The block hash\n"
            "2. verbose           (boolean, optional, default=true) true for a "
            "json object, false for the hex encoded data\n"
            "\nResult (for verbose = true):\n"
            "{\n"
            "  \"hash\" : \"hash\",     (string) the block hash (same as "
            "provided)\n"
            "  \"confirmations\" : n,   (numeric) The number of confirmations, "
            "or -1 if the block is not on the main chain\n"
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
            "}\n"
            "\nResult (for verbose=false):\n"
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

    LOCK(cs_main);

    std::string strHash = request.params[0].get_str();
    uint256 hash(uint256S(strHash));

    bool fVerbose = true;
    if (request.params.size() > 1) {
        fVerbose = request.params[1].get_bool();
    }

    if (mapBlockIndex.count(hash) == 0) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
    }

    CBlockIndex *pblockindex = mapBlockIndex[hash];

    if (!fVerbose) {
        CDataStream ssBlock(SER_NETWORK, PROTOCOL_VERSION);
        ssBlock << pblockindex->GetBlockHeader();
        std::string strHex = HexStr(ssBlock.begin(), ssBlock.end());
        return strHex;
    }

    return blockheaderToJSON(pblockindex);
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

void getblock(const Config &config, const JSONRPCRequest &jsonRPCReq,
              HTTPRequest &httpReq, bool processedInBatch) {

    if (jsonRPCReq.fHelp || jsonRPCReq.params.size() < 1 ||
        jsonRPCReq.params.size() > 2) {
        throw std::runtime_error(
            "getblock \"blockhash\" ( verbosity ) \n"
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

    LOCK(cs_main);

    std::string strHash = jsonRPCReq.params[0].get_str();
    uint256 hash(uint256S(strHash));

    if (mapBlockIndex.count(hash) == 0) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
    }

    CBlockIndex *pblockindex = mapBlockIndex[hash];

    getblockdata(pblockindex, config, jsonRPCReq, httpReq, processedInBatch);
}

void getblockbyheight(const Config &config, const JSONRPCRequest &jsonRPCReq,
                      HTTPRequest &httpReq, bool processedInBatch) {

    if (jsonRPCReq.fHelp || jsonRPCReq.params.size() < 1 ||
        jsonRPCReq.params.size() > 2) {
        throw std::runtime_error(
            "getblockbyheight height ( verbosity ) \n"
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

    LOCK(cs_main);

    int nHeight = jsonRPCReq.params[0].get_int();
    if (nHeight < 0 || nHeight > chainActive.Height()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Block height out of range");
    }

    CBlockIndex *pblockindex = chainActive.operator[](nHeight);

    getblockdata(pblockindex, config, jsonRPCReq, httpReq, processedInBatch);
}

void getblockdata(CBlockIndex *pblockindex, const Config &config,
                  const JSONRPCRequest &jsonRPCReq, HTTPRequest &httpReq,
                  bool processedInBatch) {

    // previously, false and true were accepted for verbosity 0 and 1
    // respectively. this code maintains backward compatibility.
    GetBlockVerbosity verbosity = GetBlockVerbosity::DECODE_HEADER;

    if (jsonRPCReq.params.size() > 1) {
        parseGetBlockVerbosity(jsonRPCReq.params[1], verbosity);
    }

    if (fHavePruned && !pblockindex->nStatus.hasData() &&
        pblockindex->nTx > 0) {
        throw JSONRPCError(RPC_MISC_ERROR, "Block not available (pruned data)");
    }

    auto stream = StreamSyncBlockFromDisk(*pblockindex);
    if (!stream) {
        // Block not found on disk. This could be because we have the block
        // header in our index but don't have the block (for example if a
        // non-whitelisted node sends us an unrequested long chain of valid
        // blocks, we add the headers to our index, but don't accept the block).
        throw JSONRPCError(RPC_MISC_ERROR, "Block not found on disk");
    }

    if (!processedInBatch) {
        httpReq.WriteHeader("Content-Type", "application/json");
        httpReq.StartWritingChunks(HTTP_OK);
    }

    if (verbosity == GetBlockVerbosity::RAW_BLOCK) {
        httpReq.WriteReplyChunk("{\"result\": \"");
        writeBlockChunksAndUpdateMetadata(true, httpReq, *stream,
            *pblockindex);
        httpReq.WriteReplyChunk("\", \"error\": " + NullUniValue.write() +
            ", \"id\": " + jsonRPCReq.id.write() + "}");
    } else if (verbosity == GetBlockVerbosity::DECODE_HEADER) {
        httpReq.WriteReplyChunk("{\"result\":");
        writeBlockJsonChunksAndUpdateMetadata(config, httpReq, false,
            *pblockindex, false);
        httpReq.WriteReplyChunk(", \"error\": " + NullUniValue.write() +
            ", \"id\": " + jsonRPCReq.id.write() + "}");
    } else if (verbosity == GetBlockVerbosity::DECODE_TRANSACTIONS) {
        httpReq.WriteReplyChunk("{\"result\":");
        writeBlockJsonChunksAndUpdateMetadata(config, httpReq, true,
            *pblockindex, false);
        httpReq.WriteReplyChunk(", \"error\": " + NullUniValue.write() +
            ", \"id\": " + jsonRPCReq.id.write() + "}");
    } else if (verbosity == GetBlockVerbosity::DECODE_HEADER_AND_COINBASE) {
        httpReq.WriteReplyChunk("{\"result\":");
        writeBlockJsonChunksAndUpdateMetadata(config, httpReq, true,
            *pblockindex, true);
        httpReq.WriteReplyChunk(", \"error\": " + NullUniValue.write() +
            ", \"id\": " + jsonRPCReq.id.write() + "}");
    }

    if (!processedInBatch) {
        httpReq.StopWritingChunks();
    }
}

void writeBlockChunksAndUpdateMetadata(bool isHexEncoded, HTTPRequest &req,
                                       CForwardReadonlyStream &stream,
                                       CBlockIndex &blockIndex) {

    CHash256 hasher;
    CDiskBlockMetaData metadata;

    bool hasDiskBlockMetaData;
    {
        LOCK(cs_main);
        hasDiskBlockMetaData = blockIndex.nStatus.hasDiskBlockMetaData();
    }

    do {
        auto chunk = stream.Read(4096);
        auto begin = reinterpret_cast<const char *>(chunk.Begin());
        if (!isHexEncoded) {
            req.WriteReplyChunk({begin, chunk.Size()});
        } else {
            req.WriteReplyChunk(HexStr(begin, begin + chunk.Size()));
        }

        if (!hasDiskBlockMetaData) {
            hasher.Write(chunk.Begin(), chunk.Size());
            metadata.diskDataSize += chunk.Size();
        }
    } while (!stream.EndOfStream());

    if (!hasDiskBlockMetaData) {
        hasher.Finalize(reinterpret_cast<uint8_t *>(&metadata.diskDataHash));
        SetBlockIndexFileMetaDataIfNotSet(blockIndex, metadata);
    }
}

void writeBlockJsonChunksAndUpdateMetadata(const Config &config,
                                           HTTPRequest &req, bool showTxDetails,
                                           CBlockIndex &blockIndex,
                                           bool showOnlyCoinbase) 
{

    bool hasDiskBlockMetaData;
    {
        LOCK(cs_main);
        hasDiskBlockMetaData = blockIndex.nStatus.hasDiskBlockMetaData();
    }

    auto reader = GetDiskBlockStreamReader(blockIndex.GetBlockPos(), !hasDiskBlockMetaData);
    if (!reader) 
    {
        assert(!"cannot load block from disk");
    }

    std::string delimiter;

    req.WriteReplyChunk("{\"tx\":[");
    do
    {
        const CTransaction& transaction = reader->ReadTransaction();
        if (showTxDetails)
        {
            req.WriteReplyChunk(delimiter);

            CHttpTextWriter httpWriter(req);
            CJSONWriter jWritter(httpWriter, false);
            TxToJSON(transaction, uint256(), IsGenesisEnabled(config, blockIndex.nHeight), RPCSerializationFlags(), jWritter);
            delimiter = ",";
        }
        else
        {
            std::string strJSON = delimiter + UniValue(transaction.GetId().GetHex()).write();
            req.WriteReplyChunk(strJSON);
            delimiter = ",";
        }
    } while(!reader->EndOfStream() && !showOnlyCoinbase);

    CBlockHeader header = reader->GetBlockHeader();

    // set metadata so it is available when setting header in the next step
    if (!hasDiskBlockMetaData && reader->EndOfStream())
    {
        CDiskBlockMetaData metadata = reader->getDiskBlockMetadata();
        SetBlockIndexFileMetaDataIfNotSet(blockIndex, metadata);
    }

    req.WriteReplyChunk("]," + headerBlockToJSON(config, header, &blockIndex) + "}");
}

std::string headerBlockToJSON(const Config &config,
                              const CBlockHeader &blockHeader,
                              const CBlockIndex *blockindex) {

    UniValue result(UniValue::VOBJ);

    result.push_back(Pair("hash", blockindex->GetBlockHash().GetHex()));
    int confirmations = -1;
    const CBlockIndex *pnext = nullptr;
    {
        LOCK(cs_main);

        // Only report confirmations if the block is on the main chain
        if (chainActive.Contains(blockindex)) {
            confirmations = chainActive.Height() - blockindex->nHeight + 1;
            pnext = chainActive.Next(blockindex);
        }
    }
    result.push_back(Pair("confirmations", confirmations));
    if (blockindex->nStatus.hasDiskBlockMetaData()) {
        result.push_back(
            Pair("size", blockindex->GetDiskBlockMetaData().diskDataSize));
    }
    result.push_back(Pair("height", blockindex->nHeight));
    result.push_back(Pair("version", blockHeader.nVersion));
    result.push_back(
        Pair("versionHex", strprintf("%08x", blockHeader.nVersion)));
    result.push_back(Pair("merkleroot", blockHeader.hashMerkleRoot.GetHex()));
    result.push_back(Pair("num_tx", uint64_t(blockindex->nTx)));
    result.push_back(Pair("time", blockHeader.GetBlockTime()));
    result.push_back(
        Pair("mediantime", int64_t(blockindex->GetMedianTimePast())));
    result.push_back(Pair("nonce", uint64_t(blockHeader.nNonce)));
    result.push_back(Pair("bits", strprintf("%08x", blockHeader.nBits)));
    result.push_back(Pair("difficulty", GetDifficulty(blockindex)));
    result.push_back(Pair("chainwork", blockindex->nChainWork.GetHex()));

    if (blockindex->pprev) {
        result.push_back(Pair("previousblockhash",
                              blockindex->pprev->GetBlockHash().GetHex()));
    }

    if (pnext) {
        result.push_back(Pair("nextblockhash", pnext->GetBlockHash().GetHex()));
    }

    std::string headerJSON = result.write();
    return headerJSON.substr(1, headerJSON.size() - 2);
}

struct CCoinsStats {
    int nHeight;
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
                       const std::map<uint32_t, Coin> &outputs) {
    assert(!outputs.empty());
    ss << hash;
    ss << VARINT(outputs.begin()->second.GetHeight() * 2 +
                 outputs.begin()->second.IsCoinBase());
    stats.nTransactions++;
    for (const auto output : outputs) {
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
static bool GetUTXOStats(CCoinsView *view, CCoinsStats &stats) {
    std::unique_ptr<CCoinsViewCursor> pcursor(view->Cursor());

    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    stats.hashBlock = pcursor->GetBestBlock();
    {
        LOCK(cs_main);
        stats.nHeight = mapBlockIndex.find(stats.hashBlock)->second->nHeight;
    }
    ss << stats.hashBlock;
    uint256 prevkey;
    std::map<uint32_t, Coin> outputs;
    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        COutPoint key;
        Coin coin;
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
    stats.nDiskSize = view->EstimateSize();
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

    LOCK(cs_main);

    int heightParam = request.params[0].get_int();
    if (heightParam < 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Negative block height.");
    }

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
        heightParam = pindex->nHeight;
    }

    unsigned int height = (unsigned int)heightParam;
    unsigned int chainHeight = (unsigned int)chainActive.Height();
    if (chainHeight < config.GetChainParams().PruneAfterHeight()) {
        throw JSONRPCError(RPC_MISC_ERROR,
                           "Blockchain is too short for pruning.");
    } else if (height > chainHeight) {
        throw JSONRPCError(
            RPC_INVALID_PARAMETER,
            "Blockchain is shorter than the attempted prune height.");
    } else if (height > chainHeight - MIN_BLOCKS_TO_KEEP) {
        LogPrint(BCLog::RPC, "Attempt to prune blocks close to the tip. "
                             "Retaining the minimum number of blocks.");
        height = chainHeight - MIN_BLOCKS_TO_KEEP;
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
    if (GetUTXOStats(pcoinsTip, stats)) {
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
            "}\n"

            "\nExamples:\n"
            "\nGet unspent transactions\n" +
            HelpExampleCli("listunspent", "") + "\nView the details\n" +
            HelpExampleCli("gettxout", "\"txid\" 1") +
            "\nAs a json rpc call\n" +
            HelpExampleRpc("gettxout", "\"txid\", 1"));
    }

    LOCK(cs_main);

    UniValue ret(UniValue::VOBJ);

    std::string strHash = request.params[0].get_str();
    uint256 hash(uint256S(strHash));
    int n = request.params[1].get_int();
    COutPoint out(hash, n);
    bool fMempool = true;
    if (request.params.size() > 2) {
        fMempool = request.params[2].get_bool();
    }

    Coin coin;
    if (fMempool) {
        std::shared_lock lock(mempool.smtx);
        CCoinsViewMemPool view(pcoinsTip, mempool);
        if (!view.GetCoin(out, coin) || mempool.IsSpentNL(out)) {
            // TODO: this should be done by the CCoinsViewMemPool
            return NullUniValue;
        }
    } else {
        if (!pcoinsTip->GetCoin(out, coin)) {
            return NullUniValue;
        }
    }

    BlockMap::iterator it = mapBlockIndex.find(pcoinsTip->GetBestBlock());
    CBlockIndex *pindex = it->second;
    ret.push_back(Pair("bestblock", pindex->GetBlockHash().GetHex()));
    if (coin.GetHeight() == MEMPOOL_HEIGHT) {
        ret.push_back(Pair("confirmations", 0));
    } else {
        ret.push_back(Pair("confirmations",
                           int64_t(pindex->nHeight - coin.GetHeight() + 1)));
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

    return ret;
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

    LOCK(cs_main);

    if (request.params.size() > 0) {
        nCheckLevel = request.params[0].get_int();
    }
    if (request.params.size() > 1) {
        nCheckDepth = request.params[1].get_int();
    }

    return CVerifyDB().VerifyDB(config, pcoinsTip, nCheckLevel, nCheckDepth, task::CCancellationSource::Make()->GetToken());
}

/** Implementation of IsSuperMajority with better feedback */
static UniValue SoftForkMajorityDesc(int version, CBlockIndex *pindex,
                                     const Consensus::Params &consensusParams) {
    UniValue rv(UniValue::VOBJ);
    bool activated = false;
    switch (version) {
        case 2:
            activated = pindex->nHeight >= consensusParams.BIP34Height;
            break;
        case 3:
            activated = pindex->nHeight >= consensusParams.BIP66Height;
            break;
        case 4:
            activated = pindex->nHeight >= consensusParams.BIP65Height;
            break;
        case 5:
            activated = pindex->nHeight >= consensusParams.CSVHeight;
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

    LOCK(cs_main);

    UniValue obj(UniValue::VOBJ);
    obj.push_back(Pair("chain", config.GetChainParams().NetworkIDString()));
    obj.push_back(Pair("blocks", int(chainActive.Height())));
    obj.push_back(
        Pair("headers", pindexBestHeader ? pindexBestHeader->nHeight : -1));
    obj.push_back(
        Pair("bestblockhash", chainActive.Tip()->GetBlockHash().GetHex()));
    obj.push_back(Pair("difficulty", double(GetDifficulty(chainActive.Tip()))));
    obj.push_back(
        Pair("mediantime", int64_t(chainActive.Tip()->GetMedianTimePast())));
    obj.push_back(
        Pair("verificationprogress",
             GuessVerificationProgress(config.GetChainParams().TxData(),
                                       chainActive.Tip())));
    obj.push_back(Pair("chainwork", chainActive.Tip()->nChainWork.GetHex()));
    obj.push_back(Pair("pruned", fPruneMode));

    const Consensus::Params &consensusParams =
        config.GetChainParams().GetConsensus();
    CBlockIndex *tip = chainActive.Tip();
    UniValue softforks(UniValue::VARR);
    softforks.push_back(SoftForkDesc("bip34", 2, tip, consensusParams));
    softforks.push_back(SoftForkDesc("bip66", 3, tip, consensusParams));
    softforks.push_back(SoftForkDesc("bip65", 4, tip, consensusParams));
    // version 5 is introduced only for this RPC (we will never receive block
    // with version 5)
    softforks.push_back(SoftForkDesc("csv", 5, tip, consensusParams));
    obj.push_back(Pair("softforks", softforks));

    if (fPruneMode) {
        CBlockIndex *block = chainActive.Tip();
        while (block && block->pprev && block->pprev->nStatus.hasData()) {
            block = block->pprev;
        }

        obj.push_back(Pair("pruneheight", block->nHeight));
    }
    return obj;
}

/** Comparison function for sorting the getchaintips heads.  */
struct CompareBlocksByHeight {
    bool operator()(const CBlockIndex *a, const CBlockIndex *b) const {
        // Make sure that unequal blocks with the same height do not compare
        // equal. Use the pointers themselves to make a distinction.
        if (a->nHeight != b->nHeight) {
            return (a->nHeight > b->nHeight);
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

    for (const std::pair<const uint256, CBlockIndex *> &item : mapBlockIndex) {
        if (!chainActive.Contains(item.second)) {
            setOrphans.insert(item.second);
            setPrevs.insert(item.second->pprev);
        }
    }

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
        obj.push_back(Pair("height", block->nHeight));
        obj.push_back(Pair("hash", block->phashBlock->GetHex()));

        const int branchLen =
            block->nHeight - chainActive.FindFork(block)->nHeight;
        obj.push_back(Pair("branchlen", branchLen));

        std::string status;
        if (chainActive.Contains(block)) {
            // This block is part of the currently active chain.
            status = "active";
        } else if (block->nStatus.isInvalid()) {
            // This block or one of its ancestors is invalid.
            status = "invalid";
        } else if (block->nChainTx == 0) {
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

UniValue mempoolInfoToJSON() {
    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("size", (int64_t)mempool.Size()));
    ret.push_back(Pair(
        "journalsize",
        (int64_t)mempool.getJournalBuilder()->getCurrentJournal()->size()));
    ret.push_back(
        Pair("nonfinalsize", (int64_t)mempool.getNonFinalPool().getNumTxns()));
    ret.push_back(Pair("bytes", (int64_t)mempool.GetTotalTxSize()));
    ret.push_back(Pair("usage", (int64_t)mempool.DynamicMemoryUsage()));
    ret.push_back(
        Pair("nonfinalusage",
             (int64_t)mempool.getNonFinalPool().estimateMemoryUsage()));
    size_t maxmempool =
        gArgs.GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * 1000000;
    ret.push_back(Pair("maxmempool", (int64_t)maxmempool));
    ret.push_back(
        Pair("mempoolminfee",
             ValueFromAmount(mempool.GetMinFee(maxmempool).GetFeePerK())));

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
            "  \"usage\": xxxxx,              (numeric) Total memory usage for "
            "the mempool\n"
            "  \"nonfinalusage\": xxxxx,      (numeric) Total memory usage for "
            "the non-final mempool\n"
            "  \"maxmempool\": xxxxx,         (numeric) Maximum memory usage "
            "for the mempool\n"
            "  \"mempoolminfee\": xxxxx       (numeric) Minimum fee for tx to "
            "be accepted\n"
            "}\n"
            "\nExamples:\n" +
            HelpExampleCli("getmempoolinfo", "") +
            HelpExampleRpc("getmempoolinfo", ""));
    }

    return mempoolInfoToJSON();
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
    CBlockIndex *pblockindex;

    {
        LOCK(cs_main);
        if (mapBlockIndex.count(hash) == 0) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
        }

        pblockindex = mapBlockIndex[hash];
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
        LOCK(cs_main);
        if (mapBlockIndex.count(hash) == 0) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
        }

        CBlockIndex *pblockindex = mapBlockIndex[hash];
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
        LOCK(cs_main);
        if (mapBlockIndex.count(hash) == 0) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
        }

        CBlockIndex *pblockindex = mapBlockIndex[hash];
        ResetBlockFailureFlags(pblockindex);
    }

    // state is used to report errors, not block related invalidity
    // (see description of ActivateBestChain)
    CValidationState state;
    mining::CJournalChangeSetPtr changeSet{
        mempool.getJournalBuilder()->getNewChangeSet(
            mining::JournalUpdateReason::REORG)};
    auto source = task::CCancellationSource::Make();
    ActivateBestChain(task::CCancellationToken::JoinToken(source->GetToken(), GetShutdownToken()), config, state, changeSet);

    if (!state.IsValid()) {
        throw JSONRPCError(RPC_DATABASE_ERROR, state.GetRejectReason());
    }

    return NullUniValue;
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

    {
        LOCK(cs_main);
        if (havehash) {
            auto it = mapBlockIndex.find(hash);
            if (it == mapBlockIndex.end()) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,
                                   "Block not found");
            }
            pindex = it->second;
            if (!chainActive.Contains(pindex)) {
                throw JSONRPCError(RPC_INVALID_PARAMETER,
                                   "Block is not in main chain");
            }
        } else {
            pindex = chainActive.Tip();
        }
    }

    assert(pindex != nullptr);

    if (request.params[0].isNull()) {
        blockcount = std::max(0, std::min(blockcount, pindex->nHeight - 1));
    } else {
        blockcount = request.params[0].get_int();

        if (blockcount < 0 ||
            (blockcount > 0 && blockcount >= pindex->nHeight)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid block count: "
                                                      "should be between 0 and "
                                                      "the block's height - 1");
        }
    }

    const CBlockIndex *pindexPast =
        pindex->GetAncestor(pindex->nHeight - blockcount);
    int nTimeDiff =
        pindex->GetMedianTimePast() - pindexPast->GetMedianTimePast();
    int nTxDiff = pindex->nChainTx - pindexPast->nChainTx;

    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("time", int64_t(pindex->nTime)));
    ret.push_back(Pair("txcount", int64_t(pindex->nChainTx)));
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
                           "000000000000000001618b0a11306363725fbb8dbecbb0201c2b4064cda00790 '[\"minfeerate\",\"avgfeerate\"]'") +
            HelpExampleRpc("getblockstats",
                           "000000000000000001618b0a11306363725fbb8dbecbb0201c2b4064cda00790 '[\"minfeerate\",\"avgfeerate\"]'"));
    }

    LOCK(cs_main);

    CBlockIndex *pindex;
    const std::string strHash = request.params[0].get_str();
    const uint256 hash(uint256S(strHash));
    pindex = mapBlockIndex[hash];
    if (!pindex) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
    }
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
                           "620538 '[\"minfeerate\",\"avgfeerate\"]'") +
            HelpExampleRpc("getblockstatsbyheight",
                           "630538 '[\"minfeerate\",\"avgfeerate\"]'"));
    }

    LOCK(cs_main);

    CBlockIndex *pindex;
    const int height = request.params[0].get_int();
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

    if (fHavePruned && !pindex->nStatus.hasData() &&
        pindex->nTx > 0) {
        throw JSONRPCError(RPC_MISC_ERROR, "Block not available (pruned data)");
    }

    auto stream = StreamSyncBlockFromDisk(*pindex);
    if (!stream) {
        // Block not found on disk. This could be because we have the block
        // header in our index but don't have the block (for example if a
        // non-whitelisted node sends us an unrequested long chain of valid
        // blocks, we add the headers to our index, but don't accept the block).
        throw JSONRPCError(RPC_MISC_ERROR, "Block not found on disk");
    }

    auto reader = GetDiskBlockStreamReader(pindex->GetBlockPos(), false);
    if (!reader)
    {
        assert(!"cannot load block from disk");
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

            Amount feerate = txfee / tx_size;
            if (do_medianfeerate) {
                feerate_array.push_back(feerate);
            }
            maxfeerate = std::max(maxfeerate, feerate);
            minfeerate = std::min(minfeerate, feerate);
        }
    } while(!reader->EndOfStream());


    size_t numTx = pindex->nTx;
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
    ret_all.pushKV("height", (int64_t)pindex->nHeight);
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
                                  pindex->nHeight, Params().GetConsensus())));
    ret_all.pushKV("time", pindex->GetBlockTime());
    ret_all.pushKV("total_out", ValueFromAmount(total_out));
    ret_all.pushKV("total_size", total_size);
    ret_all.pushKV("totalfee", ValueFromAmount(totalfee));
    ret_all.pushKV("txs", (int64_t)pindex->nTx);
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
            "\nForces the block assembly journal to be rebuilt to make it "
            "consistent with the TX mempool.\n"
            "\nResult:\n"
            "\nExamples:\n" +
            HelpExampleCli("rebuildjournal", "") +
            HelpExampleRpc("rebuildjournal", ""));
    }

    mempool.RebuildJournal();
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
                           "\"blockhash\" \"add\""));
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
    { "blockchain",         "getblockheader",         getblockheader,         true,  {"blockhash","verbose"} },
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
    { "blockchain",         "gettxoutsetinfo",        gettxoutsetinfo,        true,  {} },
    { "blockchain",         "pruneblockchain",        pruneblockchain,        true,  {"height"} },
    { "blockchain",         "verifychain",            verifychain,            true,  {"checklevel","nblocks"} },
    { "blockchain",         "preciousblock",          preciousblock,          true,  {"blockhash"} },
    { "blockchain",         "checkjournal",           checkjournal,           true,  {} },
    { "blockchain",         "rebuildjournal",         rebuildjournal,         true,  {} },

    /* Not shown in help */
    { "hidden",             "invalidateblock",        invalidateblock,        true,  {"blockhash"} },
    { "hidden",             "reconsiderblock",        reconsiderblock,        true,  {"blockhash"} },
    { "hidden",             "waitfornewblock",        waitfornewblock,        true,  {"timeout"} },
    { "hidden",             "waitforblock",           waitforblock,           true,  {"blockhash","timeout"} },
    { "hidden",             "waitforblockheight",     waitforblockheight,     true,  {"height","timeout"} },
    { "hidden",             "getblockchainactivity",  getblockchainactivity,  true,  {} },
    { "hidden",             "getcurrentlyvalidatingblocks",     getcurrentlyvalidatingblocks,     true,  {} },
    { "hidden",             "waitaftervalidatingblock",         waitaftervalidatingblock,         true,  {"blockhash","action"} },
    { "hidden",             "getwaitingblocks",                 getwaitingblocks,            true,  {} }
};
// clang-format on

void RegisterBlockchainRPCCommands(CRPCTable &t) {
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++) {
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
    }
}