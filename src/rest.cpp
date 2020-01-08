// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chain.h"
#include "config.h"
#include "httpserver.h"
#include "core_io.h"
#include "primitives/transaction.h"
#include "rpc/blockchain.h"
#include "rpc/http_protocol.h"
#include "rpc/jsonwriter.h"
#include "rpc/server.h"
#include "rpc/tojson.h"
#include "streams.h"
#include "sync.h"
#include "txdb.h"
#include "txmempool.h"
#include "utilstrencodings.h"
#include "validation.h"
#include "version.h"
#include <boost/algorithm/string.hpp>
#include <univalue.h>

// Allow a max of 15 outpoints to be queried at once.
static const size_t MAX_GETUTXOS_OUTPOINTS = 15;

namespace {

class CCoin {
private:
    CoinWithScript coin;

public:
    CCoin() = default;
    CCoin(CoinWithScript&& in) noexcept : coin{std::move(in)} {}

    int32_t GetHeight() const { return coin.GetHeight(); }
    const Amount& GetAmount() const { return coin.GetTxOut().nValue; }
    const CScript& GetScriptPubKey() const { return coin.GetTxOut().scriptPubKey; }

    template <typename Stream>
    inline void Serialize(Stream &s) const {
        uint32_t nTxVerDummy = 0;
        s << nTxVerDummy;
        s << coin.GetHeight();
        s << coin.GetTxOut();
    }
};

} // namespace

extern UniValue mempoolInfoToJSON(const Config& config);
extern void writeMempoolToJson(CJSONWriter& jWriter, bool fVerbose = false);

static bool RESTERR(HTTPRequest *req, enum HTTPStatusCode status,
                    std::string message) {
    req->WriteHeader("Content-Type", "text/plain");
    req->WriteReply(status, message + "\r\n");
    return false;
}

static enum RetFormat ParseDataFormat(std::string &param,
                                      const std::string &strReq) {
    const std::string::size_type pos = strReq.rfind('.');
    if (pos == std::string::npos) {
        param = strReq;
        return rf_names[0].rf;
    }

    param = strReq.substr(0, pos);
    const std::string suff(strReq, pos + 1);

    for (size_t i = 0; i < ARRAYLEN(rf_names); i++) {
        if (suff == rf_names[i].name) {
            return rf_names[i].rf;
        }
    }

    /* If no suffix is found, return original string.  */
    param = strReq;
    return rf_names[0].rf;
}

static std::string AvailableDataFormatsString() {
    std::string formats = "";
    for (size_t i = 0; i < ARRAYLEN(rf_names); i++) {
        if (strlen(rf_names[i].name) > 0) {
            formats.append(".");
            formats.append(rf_names[i].name);
            formats.append(", ");
        }
    }

    if (formats.length() > 0) {
        return formats.substr(0, formats.length() - 2);
    }

    return formats;
}

static bool ParseHashStr(const std::string &strReq, uint256 &v) {
    if (!IsHex(strReq) || (strReq.size() != 64)) {
        return false;
    }

    v.SetHex(strReq);
    return true;
}

static bool CheckWarmup(HTTPRequest *req) {
    std::string statusmessage;
    if (RPCIsInWarmup(&statusmessage)) {
        return RESTERR(req, HTTP_SERVICE_UNAVAILABLE,
                       "Service temporarily unavailable: " + statusmessage);
    }

    return true;
}

static bool rest_headers(Config &config, HTTPRequest *req,
                         const std::string &strURIPart) {
    if (!CheckWarmup(req)) {
        return false;
    }

    std::string param;
    const RetFormat rf = ParseDataFormat(param, strURIPart);
    std::vector<std::string> path;
    boost::split(path, param, boost::is_any_of("/"));

    if (path.size() != 2) {
        return RESTERR(req, HTTP_BAD_REQUEST, "No header count specified. Use "
                                              "/rest/headers/<count>/"
                                              "<hash>.<ext>.");
    }

    long count = strtol(path[0].c_str(), nullptr, 10);
    if (count < 1 || count > 2000) {
        return RESTERR(req, HTTP_BAD_REQUEST,
                       "Header count out of range: " + path[0]);
    }

    std::string hashStr = path[1];
    uint256 hash;
    if (!ParseHashStr(hashStr, hash)) {
        return RESTERR(req, HTTP_BAD_REQUEST, "Invalid hash: " + hashStr);
    }

    std::optional<uint256> lastBlockHash;
    int confirmations = -1;
    std::vector<const CBlockIndex*> headers;
    headers.reserve(count);
    {
        LOCK(cs_main);
        const CBlockIndex* tip = chainActive.Tip();
        BlockMap::const_iterator it = mapBlockIndex.find(hash);
        const CBlockIndex *pindex =
            (it != mapBlockIndex.end()) ? it->second : nullptr;
        if (!pindex)
            return RESTERR(req, HTTP_BAD_REQUEST, "Block not found: " + hashStr);
        
        confirmations = tip->nHeight - pindex->nHeight + 1;

        while (pindex != nullptr && chainActive.Contains(pindex)) {
            headers.push_back(pindex);
            if (headers.size() == size_t(count)) {
                break;
            }
            pindex = chainActive.Next(pindex);
        }
        // store blockhash of additional header if we are not on chain tip
        // because each header points to next blockhash
        if (pindex) {
            const CBlockIndex* pLast = chainActive.Next(pindex);
            if (pLast) {
                lastBlockHash = pLast->GetBlockHash();
            }
        }
    }

    CDataStream ssHeader(SER_NETWORK, PROTOCOL_VERSION);
    for (const CBlockIndex *pindex : headers) {
        ssHeader << pindex->GetBlockHeader();
    }

    switch (rf) {
        case RF_BINARY: {
            std::string binaryHeader = ssHeader.str();
            req->WriteHeader("Content-Type", "application/octet-stream");
            req->WriteReply(HTTP_OK, binaryHeader);
            return true;
        }

        case RF_HEX: {
            std::string strHex =
                HexStr(ssHeader.begin(), ssHeader.end()) + "\n";
            req->WriteHeader("Content-Type", "text/plain");
            req->WriteReply(HTTP_OK, strHex);
            return true;
        }
        case RF_JSON: {
            UniValue jsonHeaders(UniValue::VARR);
            for (size_t i = 0; i < headers.size(); i++) {
                std::optional<uint256> nextBlockHash;
                const CBlockIndex* pindex= headers[i];
                if (pindex != headers.back()) {
                    nextBlockHash = headers[i + 1]->GetBlockHash();
                } else {
                    nextBlockHash = lastBlockHash;
                }
                jsonHeaders.push_back(blockheaderToJSON(pindex, confirmations--, nextBlockHash));
            }
            std::string strJSON = jsonHeaders.write() + "\n";
            req->WriteHeader("Content-Type", "application/json");
            req->WriteReply(HTTP_OK, strJSON);
            return true;
        }
        default: {
            return RESTERR(req, HTTP_NOT_FOUND,
                           "output format not found (available: .bin, .hex)");
        }
    }

    // not reached
    // continue to process further HTTP reqs on this cxn
    return true;
}

static bool rest_block(const Config &config, HTTPRequest *req,
                       const std::string &strURIPart, bool showTxDetails) {
    if (!CheckWarmup(req)) {
        return false;
    }

    std::string hashStr;
    const RetFormat rf = ParseDataFormat(hashStr, strURIPart);

    uint256 hash;
    if (!ParseHashStr(hashStr, hash)) {
        return RESTERR(req, HTTP_BAD_REQUEST, "Invalid hash: " + hashStr);
    }

    int confirmations;
    std::optional<uint256> nextBlockHash;
    CBlockIndex* pblockindex = nullptr;
    {
        LOCK(cs_main);

        if (mapBlockIndex.count(hash) == 0) {
            return RESTERR(req, HTTP_NOT_FOUND, hashStr + " not found");
        }
        pblockindex = mapBlockIndex[hash];
        if (fHavePruned && !pblockindex->nStatus.hasData() &&
            pblockindex->nTx > 0) {
            return RESTERR(req, HTTP_NOT_FOUND,
                           hashStr + " not available (pruned data)");
        }
        confirmations = ComputeNextBlockAndDepthNL(chainActive.Tip(), pblockindex, nextBlockHash);
    }

    try
    {
        switch (rf) {
            /*
             * When Content-Length HTTP header is NOT set, libevent will automatically use chunked-encoding transfer.
             * When Content-Length HTTP header is set, no encoding is done by libevent,
             * but we still read and write response in chunks to avoid bringing whole data in memory.
            */
            case RF_BINARY: {
                writeBlockChunksAndUpdateMetadata(false, *req, *pblockindex, "", false, rf);
                break;
            }

            case RF_HEX: {
                writeBlockChunksAndUpdateMetadata(true, *req, *pblockindex, "", false, rf);
                break;
            }

            case RF_JSON: {
                writeBlockJsonChunksAndUpdateMetadata(config, *req, showTxDetails , *pblockindex, false, false, confirmations, nextBlockHash, "");
                break;
            }

            default: {
                return RESTERR(req, HTTP_NOT_FOUND,
                    "output format not found (available: " +
                    AvailableDataFormatsString() + ")");
            }
        }
    }
    catch (block_parse_error& ex)
    {
        return RESTERR(req, HTTP_NOT_FOUND, std::string(ex.what()));
    }

    return true;
}

static bool rest_block_extended(Config &config, HTTPRequest *req,
                                const std::string &strURIPart) {
    return rest_block(config, req, strURIPart, true);
}

static bool rest_block_notxdetails(Config &config, HTTPRequest *req,
                                   const std::string &strURIPart) {
    return rest_block(config, req, strURIPart, false);
}

static bool rest_chaininfo(Config &config, HTTPRequest *req,
                           const std::string &strURIPart) {
    if (!CheckWarmup(req)) {
        return false;
    }

    std::string param;
    const RetFormat rf = ParseDataFormat(param, strURIPart);

    switch (rf) {
        case RF_JSON: {
            JSONRPCRequest jsonRequest;
            jsonRequest.params = UniValue(UniValue::VARR);
            UniValue chainInfoObject = getblockchaininfo(config, jsonRequest);
            std::string strJSON = chainInfoObject.write() + "\n";
            req->WriteHeader("Content-Type", "application/json");
            req->WriteReply(HTTP_OK, strJSON);
            return true;
        }
        default: {
            return RESTERR(req, HTTP_NOT_FOUND,
                           "output format not found (available: json)");
        }
    }

    // not reached
    // continue to process further HTTP reqs on this cxn
    return true;
}

static bool rest_mempool_info(Config &config, HTTPRequest *req,
                              const std::string &strURIPart) {
    if (!CheckWarmup(req)) {
        return false;
    }

    std::string param;
    const RetFormat rf = ParseDataFormat(param, strURIPart);

    switch (rf) {
        case RF_JSON: {
            UniValue mempoolInfoObject = mempoolInfoToJSON(config);

            std::string strJSON = mempoolInfoObject.write() + "\n";
            req->WriteHeader("Content-Type", "application/json");
            req->WriteReply(HTTP_OK, strJSON);
            return true;
        }
        default: {
            return RESTERR(req, HTTP_NOT_FOUND,
                           "output format not found (available: json)");
        }
    }

    // not reached
    // continue to process further HTTP reqs on this cxn
    return true;
}

static bool rest_mempool_contents(Config &config, HTTPRequest *req,
                                  const std::string &strURIPart) {
    if (!CheckWarmup(req)) {
        return false;
    }

    std::string param;
    const RetFormat rf = ParseDataFormat(param, strURIPart);

    switch (rf) {
        case RF_JSON: {
            req->WriteHeader("Content-Type", "application/json");
            req->StartWritingChunks(HTTP_OK);

            CHttpTextWriter httpWriter(*req);
            CJSONWriter jWriter(httpWriter, false);

            writeMempoolToJson(jWriter, true);

            httpWriter.Flush();
            req->StopWritingChunks();
            return true;
        }
        default: {
            return RESTERR(req, HTTP_NOT_FOUND,
                           "output format not found (available: json)");
        }
    }

    // not reached
    // continue to process further HTTP reqs on this cxn
    return true;
}

static bool rest_tx(Config &config, HTTPRequest *req,
                    const std::string &strURIPart) {
    if (!CheckWarmup(req)) {
        return false;
    }

    std::string hashStr;
    const RetFormat rf = ParseDataFormat(hashStr, strURIPart);

    uint256 hash;
    if (!ParseHashStr(hashStr, hash)) {
        return RESTERR(req, HTTP_BAD_REQUEST, "Invalid hash: " + hashStr);
    }

    const TxId txid(hash);

    CTransactionRef tx;
    uint256 hashBlock = uint256();
    bool isGenesisEnabled;
    if (!GetTransaction(config, txid, tx, true, hashBlock, isGenesisEnabled)) {
        return RESTERR(req, HTTP_NOT_FOUND, hashStr + " not found");
    }

    CDataStream ssTx(SER_NETWORK, PROTOCOL_VERSION | RPCSerializationFlags());
    ssTx << tx;

    switch (rf) {
        case RF_BINARY: {
            std::string binaryTx = ssTx.str();
            req->WriteHeader("Content-Type", "application/octet-stream");
            req->WriteReply(HTTP_OK, binaryTx);
            return true;
        }

        case RF_HEX: {
            std::string strHex = HexStr(ssTx.begin(), ssTx.end()) + "\n";
            req->WriteHeader("Content-Type", "text/plain");
            req->WriteReply(HTTP_OK, strHex);
            return true;
        }

        case RF_JSON: {
            req->WriteHeader("Content-Type", "application/json");
            req->StartWritingChunks(HTTP_OK);
            CHttpTextWriter httpWriter(*req);
            CJSONWriter jWriter(httpWriter, false);
            TxToJSON(*tx, hashBlock, isGenesisEnabled, 0, jWriter);
            httpWriter.WriteLine();
            httpWriter.Flush();
            req->StopWritingChunks();
            return true;
        }

        default: {
            return RESTERR(req, HTTP_NOT_FOUND,
                           "output format not found (available: " +
                               AvailableDataFormatsString() + ")");
        }
    }

    // not reached
    // continue to process further HTTP reqs on this cxn
    return true;
}

static bool rest_getutxos(Config &config, HTTPRequest *req,
                          const std::string &strURIPart) {
    if (!CheckWarmup(req)) {
        return false;
    }

    std::string param;
    const RetFormat rf = ParseDataFormat(param, strURIPart);

    std::vector<std::string> uriParts;
    if (param.length() > 1) {
        std::string strUriParams = param.substr(1);
        boost::split(uriParts, strUriParams, boost::is_any_of("/"));
    }

    // throw exception in case of a empty request
    std::string strRequestMutable = req->ReadBody();
    if (strRequestMutable.length() == 0 && uriParts.size() == 0) {
        return RESTERR(req, HTTP_BAD_REQUEST, "Error: empty request");
    }

    bool fInputParsed = false;
    bool fCheckMemPool = false;
    std::vector<COutPoint> vOutPoints;

    // parse/deserialize input
    // input-format = output-format, rest/getutxos/bin requires binary input,
    // gives binary output, ...

    if (uriParts.size() > 0) {

        // inputs is sent over URI scheme
        // (/rest/getutxos/checkmempool/txid1-n/txid2-n/...)
        if (uriParts.size() > 0 && uriParts[0] == "checkmempool") {
            fCheckMemPool = true;
        }

        for (size_t i = (fCheckMemPool) ? 1 : 0; i < uriParts.size(); i++) {
            uint256 txid;
            int32_t nOutput;
            std::string strTxid = uriParts[i].substr(0, uriParts[i].find("-"));
            std::string strOutput =
                uriParts[i].substr(uriParts[i].find("-") + 1);

            if (!ParseInt32(strOutput, &nOutput) || !IsHex(strTxid)) {
                return RESTERR(req, HTTP_BAD_REQUEST, "Parse error");
            }

            txid.SetHex(strTxid);
            vOutPoints.push_back(COutPoint(txid, (uint32_t)nOutput));
        }

        if (vOutPoints.size() > 0) {
            fInputParsed = true;
        } else {
            return RESTERR(req, HTTP_BAD_REQUEST, "Error: empty request");
        }
    }

    switch (rf) {
        case RF_HEX: {
            // convert hex to bin, continue then with bin part
            std::vector<uint8_t> strRequestV = ParseHex(strRequestMutable);
            strRequestMutable.assign(strRequestV.begin(), strRequestV.end());
        }
        // FALLTHROUGH
        case RF_BINARY: {
            try {
                // deserialize only if user sent a request
                if (strRequestMutable.size() > 0) {
                    // don't allow sending input over URI and HTTP RAW DATA
                    if (fInputParsed) {
                        return RESTERR(req, HTTP_BAD_REQUEST,
                                       "Combination of URI scheme inputs and "
                                       "raw post data is not allowed");
                    }

                    CDataStream oss(SER_NETWORK, PROTOCOL_VERSION);
                    oss << strRequestMutable;
                    oss >> fCheckMemPool;
                    oss >> vOutPoints;
                }
            } catch (const std::ios_base::failure &e) {
                // abort in case of unreadable binary data
                return RESTERR(req, HTTP_BAD_REQUEST, "Parse error");
            }
            break;
        }

        case RF_JSON: {
            if (!fInputParsed) {
                return RESTERR(req, HTTP_BAD_REQUEST, "Error: empty request");
            }
            break;
        }
        default: {
            return RESTERR(req, HTTP_NOT_FOUND,
                           "output format not found (available: " +
                               AvailableDataFormatsString() + ")");
        }
    }

    // limit max outpoints
    if (vOutPoints.size() > MAX_GETUTXOS_OUTPOINTS) {
        return RESTERR(
            req, HTTP_BAD_REQUEST,
            strprintf("Error: max outpoints exceeded (max: %d, tried: %d)",
                      MAX_GETUTXOS_OUTPOINTS, vOutPoints.size()));
    }

    // check spentness and form a bitmap (as well as a JSON capable
    // human-readable string representation)
    std::vector<uint8_t> bitmap((vOutPoints.size() + 7) / 8);
    std::vector<CCoin> outs;
    outs.reserve(vOutPoints.size()); // reserve space for max possible amount of coins
    std::string bitmapStringRepresentation( vOutPoints.size(), '0' );

    auto handleUnspentCoin =
        [&outs, &bitmapStringRepresentation, &bitmap]
        (const CoinWithScript& coin, size_t idx)
        {
            outs.emplace_back( coin.MakeOwning() );
            // form a binary string representation (human-readable
            // for json output)
            bitmapStringRepresentation[ idx ] = '1';
            bitmap[idx / 8] |= (1 << (idx % 8));
        };

    if( fCheckMemPool )
    {
        mempool.OnUnspentCoinsWithScript(
            CoinsDBView{ *pcoinsTip },
            vOutPoints,
            handleUnspentCoin);
    }
    else
    {
        CoinsDBView view{ *pcoinsTip };
        std::size_t idx = 0;

        for(const auto& out : vOutPoints)
        {
            if (auto coin = view.GetCoinWithScript( out );
                coin.has_value() && !coin->IsSpent())
            {
                handleUnspentCoin( std::move( coin.value() ), idx );
            }

            ++idx;
        }
    }

    switch (rf) {
        case RF_BINARY: {
            // serialize data
            // use exact same output as mentioned in Bip64
            CDataStream ssGetUTXOResponse(SER_NETWORK, PROTOCOL_VERSION);
            ssGetUTXOResponse << chainActive.Height()
                              << chainActive.Tip()->GetBlockHash() << bitmap
                              << outs;
            std::string ssGetUTXOResponseString = ssGetUTXOResponse.str();

            req->WriteHeader("Content-Type", "application/octet-stream");
            req->WriteReply(HTTP_OK, ssGetUTXOResponseString);
            return true;
        }

        case RF_HEX: {
            CDataStream ssGetUTXOResponse(SER_NETWORK, PROTOCOL_VERSION);
            ssGetUTXOResponse << chainActive.Height()
                              << chainActive.Tip()->GetBlockHash() << bitmap
                              << outs;
            std::string strHex =
                HexStr(ssGetUTXOResponse.begin(), ssGetUTXOResponse.end()) +
                "\n";

            req->WriteHeader("Content-Type", "text/plain");
            req->WriteReply(HTTP_OK, strHex);
            return true;
        }

        case RF_JSON: {
            UniValue objGetUTXOResponse(UniValue::VOBJ);

            // pack in some essentials
            // use more or less the same output as mentioned in Bip64
            objGetUTXOResponse.push_back(
                Pair("chainHeight", chainActive.Height()));
            objGetUTXOResponse.push_back(Pair(
                "chaintipHash", chainActive.Tip()->GetBlockHash().GetHex()));
            objGetUTXOResponse.push_back(
                Pair("bitmap", bitmapStringRepresentation));

            UniValue utxos(UniValue::VARR);
            for (const CCoin &coin : outs) {
                UniValue utxo(UniValue::VOBJ);
                utxo.push_back(Pair("height", coin.GetHeight()));
                utxo.push_back(Pair("value", ValueFromAmount(coin.GetAmount())));

                // include the script in a json output
                UniValue o(UniValue::VOBJ);
                int32_t height = (coin.GetHeight() == MEMPOOL_HEIGHT) ? (chainActive.Height() + 1) : coin.GetHeight();
                ScriptPubKeyToUniv(coin.GetScriptPubKey(), true, IsGenesisEnabled(config, height), o);
                utxo.push_back(Pair("scriptPubKey", o));
                utxos.push_back(utxo);
            }
            objGetUTXOResponse.push_back(Pair("utxos", utxos));

            // return json string
            std::string strJSON = objGetUTXOResponse.write() + "\n";
            req->WriteHeader("Content-Type", "application/json");
            req->WriteReply(HTTP_OK, strJSON);
            return true;
        }
        default: {
            return RESTERR(req, HTTP_NOT_FOUND,
                           "output format not found (available: " +
                               AvailableDataFormatsString() + ")");
        }
    }

    // not reached
    // continue to process further HTTP reqs on this cxn
    return true;
}

static const struct {
    const char *prefix;
    bool (*handler)(Config &config, HTTPRequest *req,
                    const std::string &strReq);
} uri_prefixes[] = {
    {"/rest/tx/", rest_tx},
    {"/rest/block/notxdetails/", rest_block_notxdetails},
    {"/rest/block/", rest_block_extended},
    {"/rest/chaininfo", rest_chaininfo},
    {"/rest/mempool/info", rest_mempool_info},
    {"/rest/mempool/contents", rest_mempool_contents},
    {"/rest/headers/", rest_headers},
    {"/rest/getutxos", rest_getutxos},
};

bool StartREST() {
    for (size_t i = 0; i < ARRAYLEN(uri_prefixes); i++) {
        RegisterHTTPHandler(uri_prefixes[i].prefix, false,
                            uri_prefixes[i].handler);
    }

    return true;
}

void InterruptREST() {}

void StopREST() {
    for (size_t i = 0; i < ARRAYLEN(uri_prefixes); i++) {
        UnregisterHTTPHandler(uri_prefixes[i].prefix, false);
    }
}
