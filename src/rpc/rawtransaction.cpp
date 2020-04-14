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
#include "init.h"
#include "keystore.h"
#include "merkleblock.h"
#include "mining/journal_builder.h"
#include "net.h"
#include "policy/policy.h"
#include "primitives/transaction.h"
#include "rpc/server.h"
#include "rpc/tojson.h"
#include "script/script.h"
#include "script/script_error.h"
#include "script/sign.h"
#include "script/standard.h"
#include "taskcancellation.h"
#include "txmempool.h"
#include "txn_validator.h"
#include "uint256.h"
#include "utilstrencodings.h"
#include "validation.h"
#ifdef ENABLE_WALLET
#include "wallet/rpcwallet.h"
#include "wallet/wallet.h"
#endif

#include <cstdint>

#include <univalue.h>

using namespace mining;

void getrawtransaction(const Config& config,
                       const JSONRPCRequest& request,
                       HTTPRequest& httpReq,
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

    CHttpTextWriter httpWriter(httpReq);
    getrawtransaction(config, request, httpWriter, processedInBatch, [&httpReq] {httpReq.WriteHeader("Content-Type", "application/json");  httpReq.StartWritingChunks(HTTP_OK); });
    httpWriter.Flush();
    if (!processedInBatch)
    {
        httpReq.StopWritingChunks();
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
    TxId oneTxId;
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
        oneTxId = txid;
    }

    LOCK(cs_main);

    CBlockIndex *pblockindex = nullptr;

    uint256 hashBlock;
    if (request.params.size() > 1) {
        hashBlock = uint256S(request.params[1].get_str());
        if (!mapBlockIndex.count(hashBlock))
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
        pblockindex = mapBlockIndex[hashBlock];
    } else {
        // Loop through txids and try to find which block they're in. Exit loop
        // once a block is found.
        for (const auto &txid : setTxIds) {
            const Coin &coin = AccessByTxid(*pcoinsTip, txid);
            if (!coin.IsSpent()) {
                pblockindex = chainActive[coin.GetHeight()];
                break;
            }
        }
    }

    if (pblockindex == nullptr) {
        CTransactionRef tx;
        bool isGenesisEnabledDummy; // not used
        if (!GetTransaction(config, oneTxId, tx, false, hashBlock, isGenesisEnabledDummy) ||
            hashBlock.IsNull()) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,
                               "Transaction not yet in block");
        }

        if (!mapBlockIndex.count(hashBlock)) {
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Transaction index corrupt");
        }

        pblockindex = mapBlockIndex[hashBlock];
    }

    auto stream =
        GetDiskBlockStreamReader(pblockindex->GetBlockPos());
    if (!stream) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Can't read block from disk");
    }

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
                           "\"[{\\\"txid\\\":\\\"myid\\\",\\\"vout\\\":0}]\", "
                           "\"{\\\"address\\\":0.01}\"") +
            HelpExampleRpc("createrawtransaction",
                           "\"[{\\\"txid\\\":\\\"myid\\\",\\\"vout\\\":0}]\", "
                           "\"{\\\"data\\\":\\\"00010203\\\"}\""));
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
                          HTTPRequest& httpReq,
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

    CHttpTextWriter httpWriter(httpReq);
    decoderawtransaction(config, request, httpWriter, processedInBatch, [&httpReq] {httpReq.WriteHeader("Content-Type", "application/json");  httpReq.StartWritingChunks(HTTP_OK);});
    httpWriter.Flush();
    if (!processedInBatch)
    {
        httpReq.StopWritingChunks();
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
    bool genesisEnabled = std::none_of(mtx.vout.begin(), mtx.vout.end(), [](const CTxOut& out) { return out.scriptPubKey.IsPayToScriptHash(); });
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

    ScriptPubKeyToUniv(script,
        true, 
        script.IsPayToScriptHash() ? false : true,  // treat all transactions as post-Genesis, except P2SH 
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
    CCoinsView viewDummy;
    CCoinsViewCache view(&viewDummy);
    {
        std::shared_lock lock(mempool.smtx);
        CCoinsViewCache &viewChain = *pcoinsTip;
        CCoinsViewMemPool viewMempool(&viewChain, mempool);
        // Temporarily switch cache backend to db+mempool view.
        view.SetBackend(viewMempool);

        for (const CTxIn &txin : mergedTx.vin) {
            // Load entries from viewChain into view; can fail.
            view.AccessCoin(txin.prevout);
        }

        // Switch back to avoid locking mempool for too long.
        view.SetBackend(viewDummy);
    }

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
                const Coin &coin = view.AccessCoin(out);
                if (!coin.IsSpent() &&
                    coin.GetTxOut().scriptPubKey != scriptPubKey) {
                    std::string err("Previous output scriptPubKey mismatch:\n");
                    err = err + ScriptToAsmStr(coin.GetTxOut().scriptPubKey) +
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
                uint32_t coinHeight = static_cast<uint32_t>(chainActive.Height() + 1);

                // except if we are trying to sign transactions that spends p2sh transaction, which
                // are non-standard (and therefore cannot be signed) after genesis upgrade
                if( coinHeight >= genesisActivationHeight && txout.scriptPubKey.IsPayToScriptHash()){
                    coinHeight = genesisActivationHeight - 1;
                }

                view.AddCoin(out, Coin(txout, coinHeight, false), true, genesisActivationHeight);
            }

            // If redeemScript given and not using the local wallet (private
            // keys given), add redeemScript to the tempKeystore so it can be
            // signed:
            if (fGivenKeys && scriptPubKey.IsPayToScriptHash()) {
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
        const Coin &coin = view.AccessCoin(txin.prevout);
        if (coin.IsSpent()) {
            TxInErrorToJSON(txin, vErrors, "Input not found or already spent");
            continue;
        }

        const CScript &prevPubKey = coin.GetTxOut().scriptPubKey;
        const Amount amount = coin.GetTxOut().nValue;

        bool utxoAfterGenesis = IsGenesisEnabled(config, coin, chainActive.Height() + 1); 

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
    const uint256 &txid = tx->GetId();

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
    if (!mempool.Exists(txid) && !mempool.getNonFinalPool().exists(txid)) {
        if (dontCheckFee) {
            mempool.PrioritiseTransaction(tx->GetId(), tx->GetId().ToString(),
                                          0.0, MAX_MONEY);
        }
        // Mempool Journal ChangeSet
        CJournalChangeSetPtr changeSet {
            mempool.getJournalBuilder()->getNewChangeSet(JournalUpdateReason::NEW_TXN)
        };
        // Forward transaction to the validator and wait for results.
        // To support backward compatibility (of this interface) we need
        // to wait until the transaction is processed.
        // At this stage any information about validation failure (or mempool rejects)
        // are put into the log file.
        const auto& txValidator = g_connman->getTxnValidator();
        const CValidationState& status {
            txValidator->processValidation(
                            std::make_shared<CTxInputData>(
                                                TxSource::rpc, // tx source
                                                TxValidationPriority::normal, // tx validation priority
                                                std::move(tx), // a pointer to the tx
                                                GetTime(),     // nAcceptTime
                                                false,         // fLimitFree
                                                nMaxRawTxFee), // nAbsurdFee
                            changeSet, // an instance of the journal
                            true) // fLimitMempoolSize
        };
        // Check if the transaction was accepted by the mempool.
        // Due to potential race-condition we have to explicitly call exists() instead of
        // checking a result from the status variable.
        if (!mempool.Exists(txid) && !mempool.getNonFinalPool().exists(txid)) {
            if (!status.IsValid()) {
                if (dontCheckFee) {
                    mempool.clearPrioritisation(txid);
                }
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

    // It is possible that we relay txn which was added and removed from the mempool, because:
    // - block was mined
    // - the Validator's asynch mode removed the txn (and triggered reject msg)
    g_connman->EnqueueTransaction( {inv, txinfo} );

    LogPrint(BCLog::TXNSRC, "got txn rpc: %s txnsrc user=%s\n",
        inv.hash.ToString(), request.authUser.c_str());

    return txid.GetHex();
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
    { "rawtransactions",    "signrawtransaction",     signrawtransaction,     false, {"hexstring","prevtxs","privkeys","sighashtype"} }, /* uses wallet if enabled */

    { "blockchain",         "gettxoutproof",          gettxoutproof,          true,  {"txids", "blockhash"} },
    { "blockchain",         "verifytxoutproof",       verifytxoutproof,       true,  {"proof"} },
};
// clang-format on

void RegisterRawTransactionRPCCommands(CRPCTable &t) {
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++) {
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
    }
}
