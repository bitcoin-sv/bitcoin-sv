// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "base58.h"
#include "chain.h"
#include "coins.h"
#include "config.h"
#include "consensus/validation.h"
#include "core_io.h"
#include "dstencode.h"
#include "keystore.h"
#include "merkleblock.h"
#include "mining/journal_builder.h"
#include "net/net.h"
#include "policy/policy.h"
#include "primitives/transaction.h"
#include "rpc/http_protocol.h"
#include "rpc/server.h"
#include "rpc/tojson.h"
#include "script/script_error.h"
#include "script/sign.h"
#include "script/standard.h"
#include "taskcancellation.h"
#include "txdb.h"
#include "txmempool.h"
#include "txn_validator.h"
#include "uint256.h"
#include "utilstrencodings.h"
#include "validation.h"
#include "merkletreestore.h"
#include "rpc/blockchain.h"
#ifdef ENABLE_WALLET
#include "wallet/rpcwallet.h"
#include "wallet/wallet.h"
#endif

#include <cstdint>
#include <univalue.h>

using namespace mining;

void getrawtransaction(const Config& config,
                       const JSONRPCRequest& request,
                       HTTPRequest* httpReq,
                       bool processedInBatch)
{
    if (request.fHelp || request.params.size() < 1 ||
        request.params.size() > 2) 
    {
        throw std::runtime_error(
            "getrawtransaction \"txid\" ( verbose )\n"

            "\nNOTE: By default this function only works for mempool "
            "transactions. If the -txindex option is\n"
            "enabled, it also works for blockchain transactions.\n"
            "DEPRECATED: for now, it also works for transactions with unspent "
            "outputs.\n"

            "\nReturn the raw transaction data.\n"
            "\nIf verbose is 'true', returns an Object with information about "
            "'txid'.\n"
            "If verbose is 'false' or omitted, returns a string that is "
            "serialized, hex-encoded data for 'txid'.\n"

            "\nArguments:\n"
            "1. \"txid\"      (string, required) The transaction id\n"
            "2. verbose       (bool, optional, default=false) If false, return "
            "a string, otherwise return a json object\n"

            "\nResult (if verbose is not set or set to false):\n"
            "\"data\"      (string) The serialized, hex-encoded data for "
            "'txid'\n"

            "\nResult (if verbose is set to true):\n"
            "{\n"
            "  \"hex\" : \"data\",       (string) The serialized, hex-encoded "
            "data for 'txid'\n"
            "  \"txid\" : \"id\",        (string) The transaction id (same as "
            "provided)\n"
            "  \"hash\" : \"id\",        (string) The transaction hash "
            "(differs from txid for witness transactions)\n"
            "  \"size\" : n,             (numeric) The serialized transaction "
            "size\n"
            "  \"version\" : n,          (numeric) The version\n"
            "  \"locktime\" : ttt,       (numeric) The lock time\n"
            "  \"vin\" : [               (array of json objects)\n"
            "     {\n"
            "       \"txid\": \"id\",    (string) The transaction id\n"
            "       \"vout\": n,         (numeric) \n"
            "       \"scriptSig\": {     (json object) The script\n"
            "         \"asm\": \"asm\",  (string) asm\n"
            "         \"hex\": \"hex\"   (string) hex\n"
            "       },\n"
            "       \"sequence\": n      (numeric) The script sequence number\n"
            "     }\n"
            "     ,...\n"
            "  ],\n"
            "  \"vout\" : [              (array of json objects)\n"
            "     {\n"
            "       \"value\" : x.xxx,            (numeric) The value in " +
            CURRENCY_UNIT +
            "\n"
            "       \"n\" : n,                    (numeric) index\n"
            "       \"scriptPubKey\" : {          (json object)\n"
            "         \"asm\" : \"asm\",          (string) the asm\n"
            "         \"hex\" : \"hex\",          (string) the hex\n"
            "         \"reqSigs\" : n,            (numeric) The required sigs\n"
            "         \"type\" : \"pubkeyhash\",  (string) The type, eg "
            "'pubkeyhash'\n"
            "         \"addresses\" : [           (json array of string)\n"
            "           \"address\"        (string) bitcoin address\n"
            "           ,...\n"
            "         ]\n"
            "       }\n"
            "     }\n"
            "     ,...\n"
            "  ],\n"
            "  \"blockhash\" : \"hash\",   (string) the block hash\n"
            "  \"confirmations\" : n,      (numeric) The confirmations\n"
            "  \"time\" : ttt,             (numeric) The transaction time in "
            "seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"blocktime\" : ttt,        (numeric) The block time in seconds "
            "since epoch (Jan 1 1970 GMT)\n"
            "  \"blockheight\" : n         (numeric) The block height\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("getrawtransaction", "\"mytxid\"") +
            HelpExampleCli("getrawtransaction", "\"mytxid\" true") +
            HelpExampleRpc("getrawtransaction", "\"mytxid\", true"));
    }

    if(httpReq == nullptr)
        return;

    CHttpTextWriter httpWriter(*httpReq);
    getrawtransaction(config, request, httpWriter, processedInBatch, [httpReq] {
        httpReq->WriteHeader("Content-Type", "application/json");
        httpReq->StartWritingChunks(HTTP_OK);
    });
    httpWriter.Flush();
    if (!processedInBatch)
    {
        httpReq->StopWritingChunks();
    }
}

void getrawtransaction(const Config& config,
                       const JSONRPCRequest& request,
                       CTextWriter& textWriter,
                       bool processedInBatch,
                       std::function<void()> httpCallback) 
{
    
    LOCK(cs_main);

    TxId txid = TxId(ParseHashV(request.params[0], "parameter 1"));

    // Accept either a bool (true) or a num (>=1) to indicate verbose output.
    bool fVerbose = false;
    if (request.params.size() > 1) 
    {
        if (request.params[1].isNum()) 
        {
            if (request.params[1].get_int() != 0) 
            {
                fVerbose = true;
            }
        } 
        else if (request.params[1].isBool()) 
        {
            if (request.params[1].isTrue()) 
            {
                fVerbose = true;
            }
        } 
        else 
        {
            throw JSONRPCError(
                RPC_TYPE_ERROR,
                "Invalid type provided. Verbose parameter must be a boolean.");
        }
    }

    CTransactionRef tx;
    uint256 hashBlock;
    bool isGenesisEnabled;
    if (!GetTransaction(config, txid, tx, true, hashBlock, isGenesisEnabled)) 
    {
        throw JSONRPCError(
            RPC_INVALID_ADDRESS_OR_KEY,
            std::string(fTxIndex ? "No such mempool or blockchain transaction"
                                 : "No such mempool transaction. Use -txindex "
                                   "to enable blockchain transaction queries") +
                ". Use gettransaction for wallet transactions.");
    }

    if (!processedInBatch) 
    {
        httpCallback();
    }

    if (!fVerbose) 
    {
        textWriter.Write("{\"result\": \"");
        EncodeHexTx(*tx, textWriter, RPCSerializationFlags());
        textWriter.Write("\", \"error\": " + NullUniValue.write() + ", \"id\": " + request.id.write() + "}");
        return;
    }

    textWriter.Write("{\"result\": ");

    CJSONWriter jWriter(textWriter, false);

    // Call into TxToJSON() in bitcoin-common to decode the transaction hex.
    //
    // Blockchain contextual information (confirmations and blocktime) is not
    // available to code in bitcoin-common, so we query them here and push the
    // data as JSON.

    if (!hashBlock.IsNull())
    {
        CBlockDetailsData blockData;
        auto mi = mapBlockIndex.find(hashBlock);
        if (mi != mapBlockIndex.end() && mi->second) 
        {
            const CBlockIndex* pindex = mi->second;
            if (chainActive.Contains(pindex)) 
            {
                blockData.confirmations = 1 + chainActive.Height() - pindex->nHeight;
                blockData.time = pindex->GetBlockTime();
                blockData.blockTime = pindex->GetBlockTime();
                blockData.blockHeight = pindex->nHeight;
            }
            else 
            {
                blockData.confirmations = 0;
            }
        }
        TxToJSON(*tx, hashBlock, isGenesisEnabled, RPCSerializationFlags(), jWriter, blockData);
    } 
    else 
    {
        TxToJSON(*tx, uint256(), isGenesisEnabled, RPCSerializationFlags(), jWriter);
    }

    textWriter.Write(", \"error\": " + NullUniValue.write() + ", \"id\": " + request.id.write() + "}");
}

/*
 * Returns a block index of a block that contains one of the transactions in setTxIds or
 * block index represented with "requestedBlockHash" parameter.
 * Note that this function assumes all transactions in setTxIds are in the same block unless requestedBlockHash was provided.
 * In this case exception is thrown if at least one transaction in setTxIds was not found in the related block.
 * verifyTxIds can be set to false to prevent loading the block, but this will not check if all provided transactions are in the block.
 */
static CBlockIndex* GetBlockIndex(const Config& config,
                                  const uint256& requestedBlockHash,
                                  const std::set<TxId>& setTxIds,
                                  bool verifyTxIds = true)
{
    CBlockIndex* pblockindex = nullptr;

    if (!requestedBlockHash.IsNull())
    {
        LOCK(cs_main);
        // Find requested block
        if (!mapBlockIndex.count(requestedBlockHash))
        {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
        }
        pblockindex = mapBlockIndex[requestedBlockHash];

        if (verifyTxIds)
        {
            // Check if all provided transactions are in the block
            CBlock block;
            bool allTxIdsFound = false;
            if (ReadBlockFromDisk(block, pblockindex, config))
            {
                auto numberOfTxIdsFound = decltype(setTxIds.size()){0};
                for (const auto &tx : block.vtx)
                {
                    if (setTxIds.find(tx->GetId()) != setTxIds.end())
                    {
                        ++numberOfTxIdsFound;
                    }
                    if (numberOfTxIdsFound == setTxIds.size())
                    {
                        // All txIds found, no need to check further
                        allTxIdsFound = true;
                        break;
                    }
                }
            }
            if (!allTxIdsFound)
            {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Transaction(s) not found in provided block");
            }
        }
    }
    else
    {
        CoinsDBView tipView{ *pcoinsTip };

        // Try to find a block containing at least one requested transaction with utxo
        for (const TxId& txid : setTxIds)
        {
            auto coin = tipView.GetCoinByTxId(txid);
            if (coin.has_value()) {
                pblockindex = chainActive[coin->GetHeight()];
                break;
            }
        }
    }

    //When hashBlock was not specified and none of requested transactions have unspent outputs
    //try to find the block from txindex
    if (pblockindex == nullptr)
    {
        CTransactionRef tx;
        uint256 foundBlockHash;
        bool isGenesisEnabledDummy; // not used
        if (!GetTransaction(config, *setTxIds.cbegin(), tx, false, foundBlockHash, isGenesisEnabledDummy) ||
            foundBlockHash.IsNull())
        {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,
                               "Transaction not yet in block or -txindex is not enabled");
        }

        LOCK(cs_main);
        if (!mapBlockIndex.count(foundBlockHash))
        {
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Transaction index corrupt");
        }
        pblockindex = mapBlockIndex[foundBlockHash];
    }
    return pblockindex;
}

/*
 * Returns a block file stream reader for a given block index
 */
static std::unique_ptr<CBlockStreamReader<CFileReader>>  GetBlockStream(CBlockIndex& pblockindex)
{
    auto stream = GetDiskBlockStreamReader(pblockindex.GetBlockPos());
    if (!stream)
    {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Can't read block from disk");
    }
    return stream;
}

static UniValue gettxoutproof(const Config &config,
                              const JSONRPCRequest &request) {
    if (request.fHelp ||
        (request.params.size() != 1 && request.params.size() != 2)) {
        throw std::runtime_error(
            "gettxoutproof [\"txid\",...] ( blockhash )\n"
            "\nReturns a hex-encoded proof that \"txid\" was included in a "
            "block.\n"
            "\nNOTE: By default this function only works sometimes. This is "
            "when there is an\n"
            "unspent output in the utxo for this transaction. To make it "
            "always work,\n"
            "you need to maintain a transaction index, using the -txindex "
            "command line option or\n"
            "specify the block in which the transaction is included manually "
            "(by blockhash).\n"
            "\nArguments:\n"
            "1. \"txids\"       (string) A json array of txids to filter\n"
            "    [\n"
            "      \"txid\"     (string) A transaction hash\n"
            "      ,...\n"
            "    ]\n"
            "2. \"blockhash\"   (string, optional) If specified, looks for "
            "txid in the block with this hash\n"
            "\nResult:\n"
            "\"data\"           (string) A string that is a serialized, "
            "hex-encoded data for the proof.\n");
    }

    std::set<TxId> setTxIds;

    UniValue txids = request.params[0].get_array();
    for (unsigned int idx = 0; idx < txids.size(); idx++) {
        const UniValue &utxid = txids[idx];
        if (utxid.get_str().length() != 64 || !IsHex(utxid.get_str())) {
            throw JSONRPCError(RPC_INVALID_PARAMETER,
                               std::string("Invalid txid ") + utxid.get_str());
        }

        TxId txid(uint256S(utxid.get_str()));
        if (setTxIds.count(txid)) {
            throw JSONRPCError(
                RPC_INVALID_PARAMETER,
                std::string("Invalid parameter, duplicated txid: ") +
                    utxid.get_str());
        }

        setTxIds.insert(txid);
    }

    uint256 requestedBlockHash;
    if (request.params.size() > 1)
    {
        requestedBlockHash = uint256S(request.params[1].get_str());
    }
    auto stream = GetBlockStream(*GetBlockIndex(config, requestedBlockHash, setTxIds));

    CMerkleBlock mb;

    try
    {
        mb = {*stream, setTxIds};
    }
    catch(const CMerkleBlock::CNotAllExpectedTransactionsFound& e)
    {
        throw
            JSONRPCError(
                RPC_INVALID_ADDRESS_OR_KEY,
                "Not all transactions found in specified or retrieved block");
    }

    CDataStream ssMB(SER_NETWORK, PROTOCOL_VERSION);
    ssMB << mb;
    std::string strHex = HexStr(ssMB.begin(), ssMB.end());
    return strHex;
}

static UniValue verifytxoutproof(const Config &config,
                                 const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() != 1) {
        throw std::runtime_error(
            "verifytxoutproof \"proof\"\n"
            "\nVerifies that a proof points to a transaction in a block, "
            "returning the transaction it commits to\n"
            "and throwing an RPC error if the block is not in our best chain\n"
            "\nArguments:\n"
            "1. \"proof\"    (string, required) The hex-encoded proof "
            "generated by gettxoutproof\n"
            "\nResult:\n"
            "[\"txid\"]      (array, strings) The txid(s) which the proof "
            "commits to, or empty array if the proof is invalid\n");
    }

    CDataStream ssMB(ParseHexV(request.params[0], "proof"), SER_NETWORK,
                     PROTOCOL_VERSION);
    CMerkleBlock merkleBlock;
    ssMB >> merkleBlock;

    UniValue res(UniValue::VARR);

    std::vector<uint256> vMatch;
    std::vector<unsigned int> vIndex;
    if (merkleBlock.txn.ExtractMatches(vMatch, vIndex) !=
        merkleBlock.header.hashMerkleRoot) {
        return res;
    }

    LOCK(cs_main);

    if (!mapBlockIndex.count(merkleBlock.header.GetHash()) ||
        !chainActive.Contains(mapBlockIndex[merkleBlock.header.GetHash()])) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,
                           "Block not found in chain");
    }

    for (const uint256 &hash : vMatch) {
        res.push_back(hash.GetHex());
    }

    return res;
}

static UniValue createrawtransaction(const Config &config,
                                     const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() < 2 ||
        request.params.size() > 3) {
        throw std::runtime_error(
            "createrawtransaction [{\"txid\":\"id\",\"vout\":n},...] "
            "{\"address\":amount,\"data\":\"hex\",...} ( locktime )\n"
            "\nCreate a transaction spending the given inputs and creating new "
            "outputs.\n"
            "Outputs can be addresses or data.\n"
            "Returns hex-encoded raw transaction.\n"
            "Note that the transaction's inputs are not signed, and\n"
            "it is not stored in the wallet or transmitted to the network.\n"

            "\nArguments:\n"
            "1. \"inputs\"                (array, required) A json array of "
            "json objects\n"
            "     [\n"
            "       {\n"
            "         \"txid\":\"id\",    (string, required) The transaction "
            "id\n"
            "         \"vout\":n,         (numeric, required) The output "
            "number\n"
            "         \"sequence\":n      (numeric, optional) The sequence "
            "number\n"
            "       } \n"
            "       ,...\n"
            "     ]\n"
            "2. \"outputs\"               (object, required) a json object "
            "with outputs\n"
            "    {\n"
            "      \"address\": x.xxx,    (numeric or string, required) The "
            "key is the bitcoin address, the numeric value (can be string) is "
            "the " +
            CURRENCY_UNIT +
            " amount\n"
            "      \"data\": \"hex\"      (string, required) The key is "
            "\"data\", the value is hex encoded data\n"
            "      ,...\n"
            "    }\n"
            "3. locktime                  (numeric, optional, default=0) Raw "
            "locktime. Non-0 value also locktime-activates inputs\n"
            "\nResult:\n"
            "\"transaction\"              (string) hex string of the "
            "transaction\n"

            "\nExamples:\n" +
            HelpExampleCli("createrawtransaction",
                           "\"[{\\\"txid\\\":\\\"myid\\\",\\\"vout\\\":0}]\" "
                           "\"{\\\"address\\\":0.01}\"") +
            HelpExampleCli("createrawtransaction",
                           "\"[{\\\"txid\\\":\\\"myid\\\",\\\"vout\\\":0}]\" "
                           "\"{\\\"data\\\":\\\"00010203\\\"}\"") +
            HelpExampleRpc("createrawtransaction",
                           "[{\"txid\":\"myid\",\"vout\":0}], "
                           "{\"address\":0.01}") +
            HelpExampleRpc("createrawtransaction",
                           "[{\"txid\":\"myid\",\"vout\":0}], "
                           "{\"data\":\"00010203\"}"));
    }

    RPCTypeCheck(request.params,
                 {UniValue::VARR, UniValue::VOBJ, UniValue::VNUM}, true);
    if (request.params[0].isNull() || request.params[1].isNull()) {
        throw JSONRPCError(
            RPC_INVALID_PARAMETER,
            "Invalid parameter, arguments 1 and 2 must be non-null");
    }

    UniValue inputs = request.params[0].get_array();
    UniValue sendTo = request.params[1].get_obj();

    CMutableTransaction rawTx;

    if (request.params.size() > 2 && !request.params[2].isNull()) {
        int64_t nLockTime = request.params[2].get_int64();
        if (nLockTime < 0 || nLockTime > std::numeric_limits<uint32_t>::max()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER,
                               "Invalid parameter, locktime out of range");
        }

        rawTx.nLockTime = nLockTime;
    }

    for (size_t idx = 0; idx < inputs.size(); idx++) {
        const UniValue &input = inputs[idx];
        const UniValue &o = input.get_obj();

        uint256 txid = ParseHashO(o, "txid");

        const UniValue &vout_v = find_value(o, "vout");
        if (!vout_v.isNum()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER,
                               "Invalid parameter, missing vout key");
        }

        int nOutput = vout_v.get_int();
        if (nOutput < 0) {
            throw JSONRPCError(RPC_INVALID_PARAMETER,
                               "Invalid parameter, vout must be positive");
        }

        uint32_t nSequence =
            (rawTx.nLockTime ? std::numeric_limits<uint32_t>::max() - 1
                             : std::numeric_limits<uint32_t>::max());

        // Set the sequence number if passed in the parameters object.
        const UniValue &sequenceObj = find_value(o, "sequence");
        if (sequenceObj.isNum()) {
            int64_t seqNr64 = sequenceObj.get_int64();
            if (seqNr64 < 0 || seqNr64 > std::numeric_limits<uint32_t>::max()) {
                throw JSONRPCError(
                    RPC_INVALID_PARAMETER,
                    "Invalid parameter, sequence number is out of range");
            }

            nSequence = uint32_t(seqNr64);
        }

        CTxIn in(COutPoint(txid, nOutput), CScript(), nSequence);
        rawTx.vin.push_back(in);
    }

    std::set<CTxDestination> destinations;
    std::vector<std::string> addrList = sendTo.getKeys();
    for (const std::string &name_ : addrList) {
        if (name_ == "data") {
            std::vector<uint8_t> data =
                ParseHexV(sendTo[name_].getValStr(), "Data");

            CTxOut out(Amount(0), CScript() << OP_FALSE << OP_RETURN << data);
            rawTx.vout.push_back(out);
        } else {
            CTxDestination destination =
                DecodeDestination(name_, config.GetChainParams());
            if (!IsValidDestination(destination)) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,
                                   std::string("Invalid Bitcoin address: ") +
                                       name_);
            }

            if (!destinations.insert(destination).second) {
                throw JSONRPCError(
                    RPC_INVALID_PARAMETER,
                    std::string("Invalid parameter, duplicated address: ") +
                        name_);
            }

            CScript scriptPubKey = GetScriptForDestination(destination);
            Amount nAmount = AmountFromValue(sendTo[name_]);

            CTxOut out(nAmount, scriptPubKey);
            rawTx.vout.push_back(out);
        }
    }

    return EncodeHexTx(CTransaction(rawTx));
}

void decoderawtransaction(const Config& config,
                          const JSONRPCRequest& request,
                          HTTPRequest* httpReq,
                          bool processedInBatch)
{
    if (request.fHelp || request.params.size() != 1) 
    {
        throw std::runtime_error(
            "decoderawtransaction \"hexstring\"\n"
            "\nReturn a JSON object representing the serialized, hex-encoded "
            "transaction.\n"

            "\nArguments:\n"
            "1. \"hexstring\"      (string, required) The transaction hex "
            "string\n"

            "\nResult:\n"
            "{\n"
            "  \"txid\" : \"id\",        (string) The transaction id\n"
            "  \"hash\" : \"id\",        (string) The transaction hash "
            "(differs from txid for witness transactions)\n"
            "  \"size\" : n,             (numeric) The transaction size\n"
            "  \"version\" : n,          (numeric) The version\n"
            "  \"locktime\" : ttt,       (numeric) The lock time\n"
            "  \"vin\" : [               (array of json objects)\n"
            "     {\n"
            "       \"txid\": \"id\",    (string) The transaction id\n"
            "       \"vout\": n,         (numeric) The output number\n"
            "       \"scriptSig\": {     (json object) The script\n"
            "         \"asm\": \"asm\",  (string) asm\n"
            "         \"hex\": \"hex\"   (string) hex\n"
            "       },\n"
            "       \"sequence\": n     (numeric) The script sequence number\n"
            "     }\n"
            "     ,...\n"
            "  ],\n"
            "  \"vout\" : [             (array of json objects)\n"
            "     {\n"
            "       \"value\" : x.xxx,            (numeric) The value in " +
            CURRENCY_UNIT +
            "\n"
            "       \"n\" : n,                    (numeric) index\n"
            "       \"scriptPubKey\" : {          (json object)\n"
            "         \"asm\" : \"asm\",          (string) the asm\n"
            "         \"hex\" : \"hex\",          (string) the hex\n"
            "         \"reqSigs\" : n,            (numeric) The required sigs\n"
            "         \"type\" : \"pubkeyhash\",  (string) The type, eg "
            "'pubkeyhash'\n"
            "         \"addresses\" : [           (json array of string)\n"
            "           \"12tvKAXCxZjSmdNbao16dKXC8tRWfcF5oc\"   (string) "
            "bitcoin address\n"
            "           ,...\n"
            "         ]\n"
            "       }\n"
            "     }\n"
            "     ,...\n"
            "  ],\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("decoderawtransaction", "\"hexstring\"") +
            HelpExampleRpc("decoderawtransaction", "\"hexstring\""));
    }
    
    if(httpReq == nullptr)
        return;

    CHttpTextWriter httpWriter(*httpReq);
    decoderawtransaction(
        config, request, httpWriter, processedInBatch, [httpReq] {
            httpReq->WriteHeader("Content-Type", "application/json");
            httpReq->StartWritingChunks(HTTP_OK);
        });
    httpWriter.Flush();
    if (!processedInBatch)
    {
        httpReq->StopWritingChunks();
    }
}

void decoderawtransaction(const Config& config,
                          const JSONRPCRequest& request,
                          CTextWriter& textWriter, 
                          bool processedInBatch,
                          std::function<void()> httpCallback) 
{

    LOCK(cs_main);
    RPCTypeCheck(request.params, {UniValue::VSTR});

    CMutableTransaction mtx;

    if (!DecodeHexTx(mtx, request.params[0].get_str())) 
    {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
    }
    
    if (!processedInBatch)
    {
        httpCallback();
    }
    textWriter.Write("{\"result\": ");

    CTransaction tx(std::move(mtx));
    //treat as after genesis if no output is P2SH
    const bool genesisEnabled =
        std::none_of(tx.vout.begin(), tx.vout.end(), [](const CTxOut& out) {
            return IsP2SH(out.scriptPubKey);
        });
    CJSONWriter jWriter(textWriter, false);
    TxToJSON(tx, uint256(), genesisEnabled, 0, jWriter);
    
    textWriter.Write(", \"error\": " + NullUniValue.write() + ", \"id\": " + request.id.write() + "}");
}

static UniValue decodescript(const Config &config,
                             const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() != 1) {
        throw std::runtime_error(
            "decodescript \"hexstring\"\n"
            "\nDecode a hex-encoded script.\n"
            "\nArguments:\n"
            "1. \"hexstring\"     (string) the hex encoded script\n"
            "\nResult:\n"
            "{\n"
            "  \"asm\":\"asm\",   (string) Script public key\n"
            "  \"hex\":\"hex\",   (string) hex encoded public key\n"
            "  \"type\":\"type\", (string) The output type\n"
            "  \"reqSigs\": n,    (numeric) The required signatures\n"
            "  \"addresses\": [   (json array of string)\n"
            "     \"address\"     (string) bitcoin address\n"
            "     ,...\n"
            "  ],\n"
            "  \"p2sh\",\"address\" (string) address of P2SH script wrapping "
            "this redeem script (not returned if the script is already a "
            "P2SH).\n"
            "}\n"
            "\nExamples:\n" +
            HelpExampleCli("decodescript", "\"hexstring\"") +
            HelpExampleRpc("decodescript", "\"hexstring\""));
    }

    RPCTypeCheck(request.params, {UniValue::VSTR});

    UniValue r(UniValue::VOBJ);
    CScript script;
    if (request.params[0].get_str().size() > 0) {
        std::vector<uint8_t> scriptData(
            ParseHexV(request.params[0], "argument"));
        script = CScript(scriptData.begin(), scriptData.end());
    } else {
        // Empty scripts are valid.
    }

    ScriptPubKeyToUniv(
        script, true,
        !IsP2SH(script), // treat all transactions as post-Genesis, except P2SH
        r);

    UniValue type;
    type = find_value(r, "type");

    if (type.isStr() && type.get_str() != "scripthash") {
        // P2SH cannot be wrapped in a P2SH. If this script is already a P2SH,
        // don't return the address for a P2SH of the P2SH.
        r.push_back(Pair("p2sh", EncodeDestination(CScriptID(script))));
    }

    return r;
}

/**
 * Pushes a JSON object for script verification or signing errors to vErrorsRet.
 */
static void TxInErrorToJSON(const CTxIn &txin, UniValue &vErrorsRet,
                            const std::string &strMessage) {
    UniValue entry(UniValue::VOBJ);
    entry.push_back(Pair("txid", txin.prevout.GetTxId().ToString()));
    entry.push_back(Pair("vout", uint64_t(txin.prevout.GetN())));
    entry.push_back(Pair("scriptSig",
                         HexStr(txin.scriptSig.begin(), txin.scriptSig.end())));
    entry.push_back(Pair("sequence", uint64_t(txin.nSequence)));
    entry.push_back(Pair("error", strMessage));
    vErrorsRet.push_back(entry);
}

static UniValue signrawtransaction(const Config &config,
                                   const JSONRPCRequest &request) {
#ifdef ENABLE_WALLET
    CWallet *const pwallet = GetWalletForJSONRPCRequest(request);
#endif

    if (request.fHelp || request.params.size() < 1 ||
        request.params.size() > 4) {
        throw std::runtime_error(
            "signrawtransaction \"hexstring\" ( "
            "[{\"txid\":\"id\",\"vout\":n,\"scriptPubKey\":\"hex\","
            "\"redeemScript\":\"hex\"},...] [\"privatekey1\",...] sighashtype "
            ")\n"
            "\nSign inputs for raw transaction (serialized, hex-encoded).\n"
            "The second optional argument (may be null) is an array of "
            "previous transaction outputs that\n"
            "this transaction depends on but may not yet be in the block "
            "chain.\n"
            "The third optional argument (may be null) is an array of "
            "base58-encoded private\n"
            "keys that, if given, will be the only keys used to sign the "
            "transaction.\n"
#ifdef ENABLE_WALLET
            + HelpRequiringPassphrase(pwallet) +
            "\n"
#endif

            "\nArguments:\n"
            "1. \"hexstring\"     (string, required) The transaction hex "
            "string\n"
            "2. \"prevtxs\"       (string, optional) An json array of previous "
            "dependent transaction outputs\n"
            "     [               (json array of json objects, or 'null' if "
            "none provided)\n"
            "       {\n"
            "         \"txid\":\"id\",             (string, required) The "
            "transaction id\n"
            "         \"vout\":n,                  (numeric, required) The "
            "output number\n"
            "         \"scriptPubKey\": \"hex\",   (string, required) script "
            "key\n"
            "         \"redeemScript\": \"hex\",   (string, required for P2SH "
            "or P2WSH) redeem script\n"
            "         \"amount\": value            (numeric, required) The "
            "amount spent\n"
            "       }\n"
            "       ,...\n"
            "    ]\n"
            "3. \"privkeys\"     (string, optional) A json array of "
            "base58-encoded private keys for signing\n"
            "    [                  (json array of strings, or 'null' if none "
            "provided)\n"
            "      \"privatekey\"   (string) private key in base58-encoding\n"
            "      ,...\n"
            "    ]\n"
            "4. \"sighashtype\"     (string, optional, default=ALL) The "
            "signature hash type. Must be one of\n"
            "       \"ALL\"\n"
            "       \"NONE\"\n"
            "       \"SINGLE\"\n"
            "       \"ALL|ANYONECANPAY\"\n"
            "       \"NONE|ANYONECANPAY\"\n"
            "       \"SINGLE|ANYONECANPAY\"\n"
            "       \"ALL|FORKID\"\n"
            "       \"NONE|FORKID\"\n"
            "       \"SINGLE|FORKID\"\n"
            "       \"ALL|FORKID|ANYONECANPAY\"\n"
            "       \"NONE|FORKID|ANYONECANPAY\"\n"
            "       \"SINGLE|FORKID|ANYONECANPAY\"\n"

            "\nResult:\n"
            "{\n"
            "  \"hex\" : \"value\",           (string) The hex-encoded raw "
            "transaction with signature(s)\n"
            "  \"complete\" : true|false,   (boolean) If the transaction has a "
            "complete set of signatures\n"
            "  \"errors\" : [                 (json array of objects) Script "
            "verification errors (if there are any)\n"
            "    {\n"
            "      \"txid\" : \"hash\",           (string) The hash of the "
            "referenced, previous transaction\n"
            "      \"vout\" : n,                (numeric) The index of the "
            "output to spent and used as input\n"
            "      \"scriptSig\" : \"hex\",       (string) The hex-encoded "
            "signature script\n"
            "      \"sequence\" : n,            (numeric) Script sequence "
            "number\n"
            "      \"error\" : \"text\"           (string) Verification or "
            "signing error related to the input\n"
            "    }\n"
            "    ,...\n"
            "  ]\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("signrawtransaction", "\"myhex\"") +
            HelpExampleRpc("signrawtransaction", "\"myhex\""));
    }

#ifdef ENABLE_WALLET
    LOCK2(cs_main, pwallet ? &pwallet->cs_wallet : nullptr);
#else
    LOCK(cs_main);
#endif
    RPCTypeCheck(
        request.params,
        {UniValue::VSTR, UniValue::VARR, UniValue::VARR, UniValue::VSTR}, true);

    std::vector<uint8_t> txData(ParseHexV(request.params[0], "argument 1"));
    CDataStream ssData(txData, SER_NETWORK, PROTOCOL_VERSION);
    std::vector<CMutableTransaction> txVariants;
    while (!ssData.empty()) {
        try {
            CMutableTransaction tx;
            ssData >> tx;
            txVariants.push_back(tx);
        } catch (const std::exception &) {
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
        }
    }

    if (txVariants.empty()) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Missing transaction");
    }

    // mergedTx will end up with all the signatures; it starts as a clone of the
    // rawtx:
    CMutableTransaction mergedTx(txVariants[0]);

    // Fetch previous transactions (inputs):
    CoinsDBView tipView{ *pcoinsTip };
    CCoinsViewMemPool viewMempool(tipView, mempool);
    CCoinsViewCache view(viewMempool);

    bool fGivenKeys = false;
    CBasicKeyStore tempKeystore;
    if (request.params.size() > 2 && !request.params[2].isNull()) {
        fGivenKeys = true;
        UniValue keys = request.params[2].get_array();
        for (size_t idx = 0; idx < keys.size(); idx++) {
            UniValue k = keys[idx];
            CBitcoinSecret vchSecret;
            bool fGood = vchSecret.SetString(k.get_str());
            if (!fGood) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,
                                   "Invalid private key");
            }

            CKey key = vchSecret.GetKey();
            if (!key.IsValid()) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,
                                   "Private key outside allowed range");
            }

            tempKeystore.AddKey(key);
        }
    }
#ifdef ENABLE_WALLET
    else if (pwallet) {
        EnsureWalletIsUnlocked(pwallet);
    }
#endif

    // Add previous txouts given in the RPC call:
    if (request.params.size() > 1 && !request.params[1].isNull()) {
        UniValue prevTxs = request.params[1].get_array();
        for (size_t idx = 0; idx < prevTxs.size(); idx++) {
            const UniValue &p = prevTxs[idx];
            if (!p.isObject()) {
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR,
                                   "expected object with "
                                   "{\"txid'\",\"vout\",\"scriptPubKey\"}");
            }

            UniValue prevOut = p.get_obj();

            RPCTypeCheckObj(prevOut,
                            {
                                {"txid", UniValueType(UniValue::VSTR)},
                                {"vout", UniValueType(UniValue::VNUM)},
                                {"scriptPubKey", UniValueType(UniValue::VSTR)},
                                // "amount" is also required but check is done
                                // below due to UniValue::VNUM erroneously
                                // not accepting quoted numerics
                                // (which are valid JSON)
                            });

            uint256 txid = ParseHashO(prevOut, "txid");

            int nOut = find_value(prevOut, "vout").get_int();
            if (nOut < 0) {
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR,
                                   "vout must be positive");
            }

            COutPoint out(txid, nOut);
            std::vector<uint8_t> pkData(ParseHexO(prevOut, "scriptPubKey"));
            CScript scriptPubKey(pkData.begin(), pkData.end());

            {
                if (auto coin = view.GetCoinWithScript(out);
                    coin.has_value() && !coin->IsSpent() &&
                    coin->GetTxOut().scriptPubKey != scriptPubKey) {
                    std::string err("Previous output scriptPubKey mismatch:\n");
                    err = err + ScriptToAsmStr(coin->GetTxOut().scriptPubKey) +
                          "\nvs:\n" + ScriptToAsmStr(scriptPubKey);
                    throw JSONRPCError(RPC_DESERIALIZATION_ERROR, err);
                }

                CTxOut txout;
                txout.scriptPubKey = scriptPubKey;
                txout.nValue = Amount(0);
                if (prevOut.exists("amount")) {
                    txout.nValue =
                        AmountFromValue(find_value(prevOut, "amount"));
                } else {
                    // amount param is required in replay-protected txs.
                    // Note that we must check for its presence here rather
                    // than use RPCTypeCheckObj() above, since UniValue::VNUM
                    // parser incorrectly parses numerics with quotes, eg
                    // "3.12" as a string when JSON allows it to also parse
                    // as numeric. And we have to accept numerics with quotes
                    // because our own dogfood (our rpc results) always
                    // produces decimal numbers that are quoted
                    // eg getbalance returns "3.14152" rather than 3.14152
                    throw JSONRPCError(RPC_INVALID_PARAMETER, "Missing amount");
                }

                // We do not have coin height here. We assume that the coin is about to
                // be mined using latest active rules.
                const auto genesisActivationHeight = config.GetGenesisActivationHeight();
                int32_t coinHeight = chainActive.Height() + 1;

                // except if we are trying to sign transactions that spends p2sh transaction, which
                // are non-standard (and therefore cannot be signed) after genesis upgrade
                if (coinHeight >= genesisActivationHeight &&
                    IsP2SH(txout.scriptPubKey)) {
                    coinHeight = genesisActivationHeight - 1;
                }

                view.AddCoin(out, CoinWithScript::MakeOwning(std::move(txout), coinHeight, false), true, genesisActivationHeight);
            }

            // If redeemScript given and not using the local wallet (private
            // keys given), add redeemScript to the tempKeystore so it can be
            // signed:
            if (fGivenKeys && IsP2SH(scriptPubKey)) {
                RPCTypeCheckObj(
                    prevOut,
                    {
                        {"txid", UniValueType(UniValue::VSTR)},
                        {"vout", UniValueType(UniValue::VNUM)},
                        {"scriptPubKey", UniValueType(UniValue::VSTR)},
                        {"redeemScript", UniValueType(UniValue::VSTR)},
                    });
                UniValue v = find_value(prevOut, "redeemScript");
                if (!v.isNull()) {
                    std::vector<uint8_t> rsData(ParseHexV(v, "redeemScript"));
                    CScript redeemScript(rsData.begin(), rsData.end());
                    tempKeystore.AddCScript(redeemScript);
                }
            }
        }
    }

#ifdef ENABLE_WALLET
    const CKeyStore &keystore =
        ((fGivenKeys || !pwallet) ? tempKeystore : *pwallet);
#else
    const CKeyStore &keystore = tempKeystore;
#endif

    SigHashType sigHashType = SigHashType().withForkId();
    if (request.params.size() > 3 && !request.params[3].isNull()) {
        static std::map<std::string, int> mapSigHashValues = {
            {"ALL", SIGHASH_ALL},
            {"ALL|ANYONECANPAY", SIGHASH_ALL | SIGHASH_ANYONECANPAY},
            {"ALL|FORKID", SIGHASH_ALL | SIGHASH_FORKID},
            {"ALL|FORKID|ANYONECANPAY",
             SIGHASH_ALL | SIGHASH_FORKID | SIGHASH_ANYONECANPAY},
            {"NONE", SIGHASH_NONE},
            {"NONE|ANYONECANPAY", SIGHASH_NONE | SIGHASH_ANYONECANPAY},
            {"NONE|FORKID", SIGHASH_NONE | SIGHASH_FORKID},
            {"NONE|FORKID|ANYONECANPAY",
             SIGHASH_NONE | SIGHASH_FORKID | SIGHASH_ANYONECANPAY},
            {"SINGLE", SIGHASH_SINGLE},
            {"SINGLE|ANYONECANPAY", SIGHASH_SINGLE | SIGHASH_ANYONECANPAY},
            {"SINGLE|FORKID", SIGHASH_SINGLE | SIGHASH_FORKID},
            {"SINGLE|FORKID|ANYONECANPAY",
             SIGHASH_SINGLE | SIGHASH_FORKID | SIGHASH_ANYONECANPAY},
        };
        std::string strHashType = request.params[3].get_str();
        if (!mapSigHashValues.count(strHashType)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid sighash param");
        }

        sigHashType = SigHashType(mapSigHashValues[strHashType]);
        if (!sigHashType.hasForkId()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER,
                               "Signature must use SIGHASH_FORKID");
        }
    }

    // Script verification errors.
    UniValue vErrors(UniValue::VARR);

    // Use CTransaction for the constant parts of the transaction to avoid
    // rehashing.
    const CTransaction txConst(mergedTx);

    bool genesisEnabled = IsGenesisEnabled(config, chainActive.Height() + 1);

    // Sign what we can:
    for (size_t i = 0; i < mergedTx.vin.size(); i++) {
        CTxIn &txin = mergedTx.vin[i];
        auto coin = view.GetCoinWithScript(txin.prevout);
        if (!coin.has_value() || coin->IsSpent())
        {
            TxInErrorToJSON(txin, vErrors, "Input not found or already spent");
            continue;
        }

        const CScript &prevPubKey = coin->GetTxOut().scriptPubKey;
        const Amount amount = coin->GetTxOut().nValue;

        bool utxoAfterGenesis = IsGenesisEnabled(config, coin.value(), chainActive.Height() + 1);

        SignatureData sigdata;
        // Only sign SIGHASH_SINGLE if there's a corresponding output:
        if ((sigHashType.getBaseType() != BaseSigHashType::SINGLE) ||
            (i < mergedTx.vout.size())) {
            ProduceSignature(config, true, MutableTransactionSignatureCreator(
                                 &keystore, &mergedTx, i, amount, sigHashType),
                             genesisEnabled, utxoAfterGenesis, prevPubKey, sigdata);
        }

        // ... and merge in other signatures:
        for (const CMutableTransaction &txv : txVariants) {
            if (txv.vin.size() > i) {
                sigdata = CombineSignatures(
                    config, 
                    true,
                    prevPubKey,
                    TransactionSignatureChecker(&txConst, i, amount), sigdata,
                    DataFromTransaction(txv, i),
                    utxoAfterGenesis);
            }
        }

        UpdateTransaction(mergedTx, i, sigdata);

        ScriptError serror = SCRIPT_ERR_OK;
        auto source = task::CCancellationSource::Make();
        auto res =
            VerifyScript(
                config,
                true,
                source->GetToken(),
                txin.scriptSig,
                prevPubKey,
                StandardScriptVerifyFlags(genesisEnabled, utxoAfterGenesis),
                TransactionSignatureChecker(&txConst, i, amount),
                &serror);
        if (!res.value())
        {
            TxInErrorToJSON(txin, vErrors, ScriptErrorString(serror));
        }
    }

    bool fComplete = vErrors.empty();

    UniValue result(UniValue::VOBJ);
    result.push_back(Pair("hex", EncodeHexTx(CTransaction(mergedTx))));
    result.push_back(Pair("complete", fComplete));
    if (!vErrors.empty()) {
        result.push_back(Pair("errors", vErrors));
    }

    return result;
}

static UniValue sendrawtransaction(const Config &config,
                                   const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() < 1 ||
        request.params.size() > 3) {
        throw std::runtime_error(
            "sendrawtransaction \"hexstring\" ( allowhighfees dontcheckfee )\n"
            "\nSubmits raw transaction (serialized, hex-encoded) to local node "
            "and network.\n"
            "\nAlso see createrawtransaction and signrawtransaction calls.\n"
            "\nArguments:\n"
            "1. \"hexstring\"    (string, required) The hex string of the raw "
            "transaction)\n"
            "2. allowhighfees    (boolean, optional, default=false) Allow high "
            "fees\n"
            "3. dontcheckfee     (boolean, optional, default=false) Don't check fee\n"
            "\nResult:\n"
            "\"hex\"             (string) The transaction hash in hex\n"
            "\nExamples:\n"
            "\nCreate a transaction\n" +
            HelpExampleCli("createrawtransaction",
                           "\"[{\\\"txid\\\" : "
                           "\\\"mytxid\\\",\\\"vout\\\":0}]\" "
                           "\"{\\\"myaddress\\\":0.01}\"") +
            "Sign the transaction, and get back the hex\n" +
            HelpExampleCli("signrawtransaction", "\"myhex\"") +
            "\nSend the transaction (signed hex)\n" +
            HelpExampleCli("sendrawtransaction", "\"signedhex\"") +
            "\nAs a json rpc call\n" +
            HelpExampleRpc("sendrawtransaction", "\"signedhex\""));
    }
    RPCTypeCheck(request.params,
                 {UniValue::VSTR, UniValue::VBOOL, UniValue::VBOOL});
    // parse hex string from parameter
    CMutableTransaction mtx;
    if (!DecodeHexTx(mtx, request.params[0].get_str())) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
    }

    CTransactionRef tx(MakeTransactionRef(std::move(mtx)));
    const TxId &txid = tx->GetId();

    Amount nMaxRawTxFee = maxTxFee;
    if (request.params.size() > 1 && request.params[1].get_bool()) {
        nMaxRawTxFee = Amount(0);
    }
    bool dontCheckFee = false;
    if (request.params.size() > 2 && request.params[2].get_bool()) {
        dontCheckFee = true;
    }

    if (!g_connman) {
        throw JSONRPCError(
            RPC_CLIENT_P2P_DISABLED,
            "Error: Peer-to-peer functionality missing or disabled");
    }
    // Make transaction's input data object.
    TxInputDataSPtr pTxInputData =
        std::make_shared<CTxInputData>(
            g_connman->GetTxIdTracker(),    // a pointer to the TxIdTracker
            std::move(tx),                  // a pointer to the tx
            TxSource::rpc,                  // tx source
            TxValidationPriority::normal,   // tx validation priority
            TxStorage::memory,              // tx storage
            GetTime(),                      // nAcceptTime
            nMaxRawTxFee);                 // nAbsurdFee
    // Check if transaction is already received through p2p interface,
    // and thus, couldn't be added to the TxIdTracker.
    if (!pTxInputData->IsTxIdStored()) {
        throw JSONRPCError(RPC_TRANSACTION_REJECTED,
                strprintf("%i: %s", REJECT_ALREADY_KNOWN, "txn-already-known"));
    }
    // Check if txn is present in one of the mempools.
    if (!mempool.Exists(txid) && !mempool.getNonFinalPool().exists(txid)) {
        // Mempool Journal ChangeSet
        CJournalChangeSetPtr changeSet {
            mempool.getJournalBuilder().getNewChangeSet(JournalUpdateReason::NEW_TXN)
        };
        // Prioritise transaction (if it was requested to prioritise)
        // - mempool prioritisation cleanup is done during destruction,
        //   if the prioritised txn was not accepted by the mempool
        // The mempool prioritisation is not executed on a null TxId
        // - no-op in terms of prioritise/clear operations
        CTxPrioritizer txPrioritizer(mempool, dontCheckFee ? txid : TxId());
        // Forward transaction to the validator and wait for results.
        const auto& txValidator = g_connman->getTxnValidator();
        const CValidationState& status {
            txValidator->processValidation(
                std::move(pTxInputData), // txn's input data
                changeSet, // an instance of the journal
                true) // fLimitMempoolSize
        };
        // Check if the transaction was accepted by the mempool.
        // Due to potential race-condition we have to explicitly call exists() instead of
        // checking a result from the status variable.
        if (!mempool.Exists(txid) && !mempool.getNonFinalPool().exists(txid)) {
            if (!status.IsValid()) {
                if (status.IsMissingInputs()) {
                        throw JSONRPCError(RPC_TRANSACTION_ERROR, "Missing inputs");
                } else if (status.IsInvalid()) {
                    throw JSONRPCError(RPC_TRANSACTION_REJECTED,
                                       strprintf("%i: %s", status.GetRejectCode(),
                                                 status.GetRejectReason()));
                } else {
                    throw JSONRPCError(RPC_TRANSACTION_ERROR, status.GetRejectReason());
                }
            }
        // At this stage we do reject a request which reached this point due to a race
        // condition so we can return correct error code to the caller.
        } else if (!status.IsValid()) {
            throw JSONRPCError(RPC_TRANSACTION_ALREADY_IN_CHAIN,
                               "Transaction already in the mempool");
        }
    } else {
        throw JSONRPCError(RPC_TRANSACTION_ALREADY_IN_CHAIN,
                           "Transaction already in the mempool");
    }

    CInv inv(MSG_TX, txid);
    TxMempoolInfo txinfo {};
    if(mempool.Exists(txid))
    {
        txinfo = mempool.Info(txid);
    }
    else if(mempool.getNonFinalPool().exists(txid))
    {
        txinfo = mempool.getNonFinalPool().getInfo(txid);
    }

    // It is possible that txn was added and removed from the mempool, because:
    // - block was mined
    // - the Validator's asynch mode removed the txn (and triggered reject msg)
    // - this txn is final version of timelocked txn and is still being validated
    if (!txinfo.IsNull()){
        g_connman->EnqueueTransaction({ inv, txinfo });
    }

    LogPrint(BCLog::TXNSRC, "got txn rpc: %s txnsrc user=%s\n",
        inv.hash.ToString(), request.authUser.c_str());

    return txid.GetHex();
}

/**
 * Pushes a JSON object for invalid transactions to JSON writer.
 */
static void InvalidTxnsToJSON(const CTxnValidator::InvalidTxnStateUMap& invalidTxns, CJSONWriter& writer)
{
    if (!invalidTxns.empty())
    {
        writer.writeBeginArray("invalid");
        for (const auto& elem: invalidTxns)
        {
            writer.writeBeginObject();
            writer.pushKV("txid", elem.first.ToString());
            if (elem.second.IsMissingInputs())
            {
                writer.pushKV("reject_code", REJECT_INVALID);
                writer.pushKV("reject_reason", "missing-inputs");
            } 
            else
            {
                writer.pushKV("reject_code", uint64_t(elem.second.GetRejectCode()));
                writer.pushKV("reject_reason", elem.second.GetRejectReason());
            }
            const std::set<CTransactionRef>& collidedWithTx = elem.second.GetCollidedWithTx();
            if (!collidedWithTx.empty())
            {
                writer.writeBeginArray("collidedWith");
                for (const CTransactionRef& tx : collidedWithTx)
                {
                    writer.writeBeginObject();
                    writer.pushKV("txid", tx->GetId().GetHex());
                    writer.pushKV("size", int64_t(tx->GetTotalSize()));
                    writer.pushK("hex");
                    writer.pushQuote();
                    EncodeHexTx(*tx, writer.getWriter(), 0);
                    writer.pushQuote();
                    writer.writeEndObject();
                }
                writer.writeEndArray();
            }
            writer.writeEndObject();
        }
        writer.writeEndArray();
    }
}

/**
 * Pushes insufficient fee txns to JSON writer.
 */
static void EvictedTxnsToJSON(const CTxnValidator::RemovedTxns& evictedTxns, CJSONWriter& writer)
{
    if (!evictedTxns.empty())
    {
        writer.writeBeginArray("evicted");
        for (const auto& elem: evictedTxns) {
            writer.pushV(elem.ToString());
        }
        writer.writeEndArray();
    }
}

/**
 * Pushes known txns to JSON writer.
 */
static void KnownTxnsToJSON(const std::vector<TxId>& knownTxns, CJSONWriter& writer)
{
    if (!knownTxns.empty())
    {
        writer.writeBeginArray("known");
        for (const auto& elem: knownTxns) {
            writer.pushV(elem.ToString());
        }
        writer.writeEndArray();
    }
}

void sendrawtransactions(const Config& config,
                         const JSONRPCRequest& request,
                         HTTPRequest* httpReq,
                         bool processedInBatch)
{
    if (request.fHelp || request.params.size() < 1 ||
        request.params.size() > 1) {
        throw std::runtime_error(
            "sendrawtransactions [{\"hex\": \"hexstring\", \"allowhighfees\": true|false, \"dontcheckfee\": true|false}, ...]\n"
            "\nSubmits raw transactions (serialized, hex-encoded) to local node "
            "and network.\n"
            "\nTo maximise performance, transaction chains should be provided in inheritance order\n"
            "(parent-child).\n"
            "\nAlso see sendrawtransaction, createrawtransaction and signrawtransaction calls.\n"
            "\nArguments:\n"
            "1. \"inputs\"      (array, required) "
            "A json array of json objects\n"
            "     [\n"
            "       {\n"
            "         \"hex\":\"hexstring\",          (string, required) "
            "The hex string of the raw transaction\n"
            "         \"allowhighfees\": true|false,  (boolean, optional, default=false) "
            "Allow high fees\n"
            "         \"dontcheckfee\": true|false    (boolean, optional, default=false) "
            "Don't check fee\n"
            "       } \n"
            "       ,...\n"
            "     ]\n"

            "\nResult:\n"
            "{\n"
            "  \"known\" : [                 (json array) "
            "Already known transactions detected during processing (if there are any)\n"
            "      \"txid\" : xxxxxx,        (string) "
            "The transaction id\n"
            "      ,...\n"
            "  ],\n"
            "  \"evicted\" : [               (json array) "
            "Transactions accepted by the mempool and then evicted due to insufficient fee (if there are any)\n"
            "      \"txid\" : xxxxx,         (string) "
            "The transaction id\n"
            "      ,...\n"
            "  ],\n"
            "  \"invalid\" : [               (json array of objects) "
            "Invalid transactions detected during validation (if there are any)\n"
            "    {\n"
            "      \"txid\" : xxxxxxxx,      (string) "
            "The transaction id\n"
            "      \"reject_code\" : x,      (numeric) "
            "The reject code set during validation\n"
            "      \"reject_reason\" : xxxxx (string) "
            "The reject reason set during validation\n"
            "      \"collidedWith\" : [      (json array of objects) This field is only present in case of "
            "doublespend transaction and contains transactions we collided with\n"
            "        {\n"
            "          \"txid\" : xxxxxxxx,  (string) The transaction id\n"
            "          \"size\" : xxxx,      (numeric) Transaction size in bytes\n"
            "          \"hex\"  : xxxxxxxx,  (string) Whole transaction in hex\n"
            "        }\n"
            "        ,...\n"
            "      ]\n"
            "    }\n"
            "    ,...\n"
            "  ]\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("sendrawtransactions",
                           "\"[{\\\"hex\\\":\\\"hexstring\\\"}]\"") +
            HelpExampleCli("sendrawtransactions",
                           "\"[{\\\"hex\\\":\\\"hexstring\\\", \\\"allowhighfees\\\":true}]\"") +
            HelpExampleCli("sendrawtransactions",
                           "\"[{\\\"hex\\\":\\\"hexstring\\\", \\\"allowhighfees\\\":true, \\\"dontcheckfee\\\":true}]\"") +
            HelpExampleRpc("sendrawtransactions",
                           "[{\"hex\":\"hexstring\"}]") +
            HelpExampleRpc("sendrawtransactions",
                           "[{\"hex\":\"hexstring\", \"allowhighfees\":true}]") +
            HelpExampleRpc("sendrawtransactions",
                           "[{\"hex\":\"hexstring\", \"allowhighfees\":true, \"dontcheckfee\":true}]"));
    }

    if(httpReq == nullptr)
        return;

    RPCTypeCheck(request.params, {UniValue::VARR});

    if (request.params[0].empty()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER,
            std::string("Invalid parameter: An empty json array of objects"));
    }
    // Check if inputs are present
    UniValue inputs = request.params[0].get_array();
    // A vector to store input transactions.
    TxInputDataSPtrVec vTxInputData {};
    vTxInputData.reserve(inputs.size());
    // A vector to store transactions that need to be prioritised.
    std::vector<TxId> vTxToPrioritise {};
    // A vector to store already known transactions.
    std::vector<TxId> vKnownTxns {};

    /**
     * Parse an input data
     * - read data from top to the bottom
     * - throw an exception in case of any error
     */
    for (size_t idx = 0; idx < inputs.size(); ++idx) {
        // Get json object.
        const UniValue &input = inputs[idx];
        const UniValue &o = input.get_obj();
        if (o.empty()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER,
                    std::string("Invalid parameter: An empty json object"));
        }
        // Read and decode transaction's data.
        const UniValue &txn_data = find_value(o, "hex");
        if (txn_data.isNull() || !txn_data.isStr()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER,
                    std::string("Invalid parameter: Missing the hex string of the raw transaction"));
        }
        CMutableTransaction mtx;
        if (!DecodeHexTx(mtx, txn_data.get_str())) {
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR,
                    "TX decode failed");
        }
        CTransactionRef tx(MakeTransactionRef(std::move(mtx)));
        const TxId& txid = tx->GetId();
        // Read allowhighfees.
        Amount nMaxRawTxFee = maxTxFee;
        const UniValue &allowhighfees = find_value(o, "allowhighfees");
        if (!allowhighfees.isNull()) {
            if (!allowhighfees.isBool()) {
                throw JSONRPCError(RPC_INVALID_PARAMETER,
                        std::string("allowhighfees: Invalid value"));
            } else if (allowhighfees.isTrue()) {
                nMaxRawTxFee = Amount(0);
            }
        }
        // Read dontcheckfee.
        bool fTxToPrioritise = false;
        bool fTxInMempools = mempool.Exists(txid) || mempool.getNonFinalPool().exists(txid);
        if (!fTxInMempools) {
            const UniValue &dontcheckfee = find_value(o, "dontcheckfee");
            if (!dontcheckfee.isNull()) {
                if (!dontcheckfee.isBool()) {
                    throw JSONRPCError(RPC_INVALID_PARAMETER,
                            std::string("dontcheckfee: Invalid value"));
                } else if (dontcheckfee.isTrue()) {
                    fTxToPrioritise = true;
                }
            }
        } else {
            vKnownTxns.emplace_back(txid);
            continue;
        }
        // Create an object with transaction's input data.
        TxInputDataSPtr pTxInputData =
            std::make_shared<CTxInputData>(
                g_connman->GetTxIdTracker(),    // a pointer to the TxIdTracker
                std::move(tx),                  // a pointer to the tx
                TxSource::rpc,                  // tx source
                TxValidationPriority::normal,   // tx validation priority
                TxStorage::memory,              // tx storage
                GetTime(),                      // fLimitFree
                nMaxRawTxFee);                 // nAbsurdFee
        // Check if transaction is already known
        // - received through p2p interface or present in the mempools
        if (!pTxInputData->IsTxIdStored() || fTxInMempools) {
            vKnownTxns.emplace_back(txid);
        // Move it to the vector of transactions awaiting to be processed
        } else {
            vTxInputData.emplace_back(std::move(pTxInputData));
            // Check if txn needs to be prioritised
            if (fTxToPrioritise) {
                vTxToPrioritise.emplace_back(txid);
            }
        }
    }

    /**
     * Run synchronous batch validation.
     */
    CTxnValidator::RejectedTxns rejectedTxns {};
    // Applay journal changeSet straight after processValidation call.
    {
        // Mempool Journal ChangeSet
        CJournalChangeSetPtr changeSet {
            mempool.getJournalBuilder().getNewChangeSet(JournalUpdateReason::NEW_TXN)
        };
        // Prioritise transactions (if any were requested to prioritise)
        // - mempool prioritisation cleanup is done during destruction
        //   for those txns which are not accepted by the mempool
        CTxPrioritizer txPrioritizer(mempool, std::move(vTxToPrioritise));
        // Run synch batch validation and wait for results.
        const auto& txValidator = g_connman->getTxnValidator();
        rejectedTxns =
            txValidator->processValidation(
                vTxInputData, // A vector of txns that need to be processed
                changeSet, // an instance of the journal
                true); // fLimitMempoolSize
    }

    /**
     * Enqueue INVs.
     */
    // Create a lookup table.
    std::unordered_set<TxId, std::hash<TxId>>
        usRemovedTxns(rejectedTxns.second.begin(), rejectedTxns.second.end());
    for (const TxInputDataSPtr& pTxInputData: vTxInputData) {
        const TxId& txid = pTxInputData->GetTxnPtr()->GetId();
        if (!rejectedTxns.first.count(txid) && !usRemovedTxns.count(txid)) {
            // Create an inv msg.
            CInv inv(MSG_TX, txid);
            TxMempoolInfo txinfo {};
            if(mempool.Exists(txid)) {
                txinfo = mempool.Info(txid);
            }
            else if(mempool.getNonFinalPool().exists(txid)) {
                txinfo = mempool.getNonFinalPool().getInfo(txid);
            }
            // It is possible that txn was added and removed from the mempool, because:
            // - a block was mined
            // - PTV's asynch mode removed txn(s)
            if (txinfo.GetTx() != nullptr){
                g_connman->EnqueueTransaction({ inv, txinfo });
            }
            LogPrint(BCLog::TXNSRC, "got txn rpc: %s txnsrc user=%s\n",
                inv.hash.ToString(), request.authUser.c_str());
        }
    }


    /**
     * Construct and return a result set, as a json object with rejected txids, which contains:
     *
     * 1. txid of a transaction which was detected as already known:
     *   - exists in the mempool
     *   - stored in ptv queues
     *   - stored as an orphan txn received through p2p interface
     * 2. txid of an invalid transaction, including validation state information:
     *   - reject code
     *   - reject reason
     * 3. txid of a transaction evicted from the mempool during processing:
     *   - txn which was accepted and then removed due to insufficient fee
     *
     * Accepted txids are not returned in the result set, as it could create false-positives,
     * for accepted txns, if:
     * - a block was mined
     * - PTV's asynch mode removed txn(s)
     * From the user's perspective, It could cause a misinterpretation.
     *
     * If the result set is empty, then all transactions are valid, and most likely,
     * present in the mempool.
     */
    // A result json object.
    if (!processedInBatch)
    {
        httpReq->WriteHeader("Content-Type", "application/json");
        httpReq->StartWritingChunks(HTTP_OK);
    }

    CHttpTextWriter httpWriter(*httpReq);
    CJSONWriter jWriter(httpWriter, false);

    jWriter.writeBeginObject();
    jWriter.pushKNoComma("result");
    jWriter.writeBeginObject();
    // Known txns array.
    KnownTxnsToJSON(vKnownTxns, jWriter);
    // Rejected txns array.
    InvalidTxnsToJSON(rejectedTxns.first, jWriter);
    // Evicted txns array.
    EvictedTxnsToJSON(rejectedTxns.second, jWriter);
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

static UniValue getmerkleproof(const Config& config,
    const JSONRPCRequest& request)
{
    if (request.fHelp ||
        (request.params.size() != 1 && request.params.size() != 2))
    {
        throw std::runtime_error(
            "getmerkleproof \"txid\" ( blockhash )\n"
            "\nReturns a Merkle proof for a transaction represented by txid in a list of Merkle"
            "\ntree hashes from which Merkle root can be calculated using the given txid. Calculated"
            "\n Merkle root can be used to prove that the transaction was included in a block.\n"
            "\nNOTE: This only works if transaction was already included in a block and the block"
            "\nwas found. When not specifying \"blockhash\", function will be able to find the block"
            "\nonly if there is an unspent output in the utxo for this transaction or transaction"
            "\nindex is maintained (using the -txindex command line option).\n"

            "\nArguments:\n"
            "1. \"txid\"      (string, required) The transaction id\n"
            "2. \"blockhash\" (string, optional) If specified, looks for txid in the block with "
            "this hash\n"
            "\nResult:\n"
            "{\n"
            "  \"flags\" : 2,                     (numeric) Flags is always 2 => \"txOrId\" is transaction ID and \"target\" is a block header\n"
            "  \"index\" : txIndex,               (numeric) Index of a transaction in a block/Merkle Tree (0 means coinbase)\n"
            "  \"txOrId\" : \"txid\",             (string) ID of the Tx in question\n"
            "  \"target\" : {blockheader},        (json) The block header, as returned by getBlockHeader(true) RPC (i.e. verbose = true)\n"
            "  \"nodes\" :                        (json array) Merkle Proof for transaction txOrId as array of nodes\n"
            "    [\"hash\", \"hash\", \"*\", ...] Each node is a hash in a Merkle Tree and \"*\" represents a copy of the calculated node\n"
            "}\n"
            "\nExamples:\n" +
            HelpExampleCli("getmerkleproof", "\"mytxid\"") +
            HelpExampleCli("getmerkleproof", "\"mytxid\" \"myblockhash\"") +
            HelpExampleRpc("getmerkleproof", "\"mytxid\", \"myblockhash\""));
    }

    TxId transactionId = TxId(ParseHashV(request.params[0], "hash"));
    std::set<TxId> setTxIds;
    setTxIds.insert(transactionId);

    uint256 requestedBlockHash;
    if (request.params.size() > 1)
    {
        requestedBlockHash = uint256S(request.params[1].get_str());
    }
    CBlockIndex* blockIndex = GetBlockIndex(config, requestedBlockHash, setTxIds, false);

    if (blockIndex == nullptr)
    {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Block for this transaction not found");
    }

    int32_t currentChainHeight = 0;
    {
        LOCK(cs_main);
        currentChainHeight = static_cast<int32_t>(chainActive.Height());
    }

    CMerkleTreeRef merkleTree = pMerkleTreeFactory->GetMerkleTree(config, *blockIndex, currentChainHeight);

    if (merkleTree == nullptr)
    {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Can't read block from disk");
    }

    CMerkleTree::MerkleProof proof = merkleTree->GetMerkleProof(transactionId, true);
    if (proof.merkleTreeHashes.size() == 0)
    {
        // The requested transaction was not found in the block/merkle tree
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Transaction(s) not found in provided block");
    }
    // Result in JSON format
    UniValue merkleProofArray(UniValue::VARR);
    for (const uint256& node : proof.merkleTreeHashes)
    {
        if (node.IsNull())
        {
            merkleProofArray.push_back("*");
        }
        else
        {
            merkleProofArray.push_back(node.GetHex());
        }
    }

    // CallbackData
    UniValue callbackDataObject(UniValue::VOBJ);
    callbackDataObject.pushKV("flags", 2);
    callbackDataObject.pushKV("index", static_cast<uint64_t>(proof.transactionIndex));
    callbackDataObject.pushKV("txOrId", transactionId.GetHex());
    int confirmations = 0;
    std::optional<uint256> nextBlockHash;
    {
        LOCK(cs_main);
        confirmations = ComputeNextBlockAndDepthNL(chainActive.Tip(), blockIndex, nextBlockHash);
    }

    callbackDataObject.pushKV("target", blockheaderToJSON(blockIndex, confirmations, nextBlockHash));
    callbackDataObject.pushKV("nodes", merkleProofArray);
    return callbackDataObject;
}

static UniValue verifymerkleproof(const Config& config,
    const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
    {
        throw std::runtime_error(
            "verifymerkleproof \"proof\"\n"
            "\nVerifies a given Merkle proof in JSON format and returns true if\n"
            "verification succeeded.\n"
            "\nArguments:\n"
            "1. \"proof\" (json, required) A json object containing Merkle proof for specified transaction. "
            "Json object from \"getmerkleproof\" result can be used:\n"
            "{\n"
            "  \"flags\" : 2,                     (numeric) Flags should always be 2 => \"txOrId\" is transaction ID and \"target\" is a block header\n"
            "  \"index\" : txIndex,               (numeric) Index of a transaction in a block/Merkle Tree (coinbase transaction for example is always at index 0)\n"
            "  \"txOrId\" : \"txid\",             (string) ID of the Tx to be verified\n"
            "  \"target\" : {blockheader},        (json) The block header, as returned by getblockheader RPC (verbose = true). Should at least contain \"merkleroot\" key and value\n"
            "  \"nodes\" :                        (json array) Merkle Proof for transaction txOrId as array of nodes\n"
            "    [\"hash\", \"hash\", \"*\", ...] Each node is a hash in a Merkle Tree or \"*\" to represent a duplicate of the calculated node\n"
            "}\n"
            "\nResult:\n"
            "true|false                           (boolean) If true, proof for \"txOrId\" was successfully verified, false otherwise\n"
            "\nExamples:\n" +
            HelpExampleCli("verifymerkleproof", "\"{\\\"flags\\\": 2, \\\"index\\\": 1, \\\"txOrId\\\": \\\"b4cc287e58f87cdae59417329f710f3ecd75a4ee1d2872b7248f50977c8493f3\\\", "
                "\\\"target\\\": {\\\"merkleroot\\\": \\\"abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890\\\"}, "
                "\\\"nodes\\\": [\\\"*\\\", \\\"b94d27b9934d3e08a52e52d7da7dabfac484efe37a5380ee9088f7ace2efcde9\\\"]}\"") +
            HelpExampleRpc("verifymerkleproof", "{\"flags\": 2, \"index\": 1, \"txOrId\": \"b4cc287e58f87cdae59417329f710f3ecd75a4ee1d2872b7248f50977c8493f3\", "
                "\"target\": {\"merkleroot\": \"abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890\"}, "
                "\"nodes\": [\"*\", \"b94d27b9934d3e08a52e52d7da7dabfac484efe37a5380ee9088f7ace2efcde9\"]}"));   
    }

    if (request.params[0].isNull())
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, argument 1 must be non-null");
    }
    RPCTypeCheck(request.params, { UniValue::VOBJ }, true);

    UniValue merkleProofObject = request.params[0].get_obj();
    UniValue flags = find_value(merkleProofObject, "flags");
    if (!flags.isNum())
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "\"flags\" must be a numeric value");
    }
    else if (flags.get_int() != 2)
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "verifymerkleproof only supports \"flags\" with value 2");
    }
    UniValue index = find_value(merkleProofObject, "index");
    if (!index.isNum())
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "\"index\" must be a numeric value");
    }
    else if (index.get_int() < 0)
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "\"index\" must be a positive value");
    }
    TxId txid = TxId(ParseHashO(merkleProofObject, "txOrId"));
    UniValue target = find_value(merkleProofObject, "target");
    if (!target.isObject())
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "\"target\" must be a block header Json object");
    }
    uint256 headerMerkleRoot = ParseHashO(target, "merkleroot");
    UniValue proofNodes = find_value(merkleProofObject, "nodes");
    if (!proofNodes.isArray())
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "\"nodes\" must be a Json array");
    }
    std::vector<uint256> merkleProof;
    for (const UniValue& proofNode : proofNodes.getValues())
    {
        if (!proofNode.isStr())
        {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "\"node\" must be a \"hash\" or \"*\"");
        }
        uint256 node;
        // "*" node is a zero unit256 which is considered as a duplicate in merkle root calculation
        if (proofNode.getValStr() != "*")
        {
            node = ParseHashV(proofNode, "node");
        }
        merkleProof.push_back(node);
    }

    uint256 calculatedMerkleRoot = ComputeMerkleRootFromBranch(txid, merkleProof, index.get_int());
    return(calculatedMerkleRoot == headerMerkleRoot);
}

// clang-format off
static const CRPCCommand commands[] = {
    //  category            name                      actor (function)        okSafeMode
    //  ------------------- ------------------------  ----------------------  ----------
    { "rawtransactions",    "getrawtransaction",      getrawtransaction,      true,  {"txid","verbose"} },
    { "rawtransactions",    "createrawtransaction",   createrawtransaction,   true,  {"inputs","outputs","locktime"} },
    { "rawtransactions",    "decoderawtransaction",   decoderawtransaction,   true,  {"hexstring"} },
    { "rawtransactions",    "decodescript",           decodescript,           true,  {"hexstring"} },
    { "rawtransactions",    "sendrawtransaction",     sendrawtransaction,     false, {"hexstring","allowhighfees","dontcheckfee"} },
    { "rawtransactions",    "sendrawtransactions",    sendrawtransactions,    false, {"inputs"} },
    { "rawtransactions",    "signrawtransaction",     signrawtransaction,     false, {"hexstring","prevtxs","privkeys","sighashtype"} }, /* uses wallet if enabled */

    { "blockchain",         "gettxoutproof",          gettxoutproof,          true,  {"txids", "blockhash"} },
    { "blockchain",         "verifytxoutproof",       verifytxoutproof,       true,  {"proof"} },
    { "blockchain",         "getmerkleproof",         getmerkleproof,         true,  {"txid", "blockhash"} },
    { "blockchain",         "verifymerkleproof",      verifymerkleproof,      true,  {"proof", "txid"} },
};
// clang-format on

void RegisterRawTransactionRPCCommands(CRPCTable &t) {
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++) {
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
    }
}
