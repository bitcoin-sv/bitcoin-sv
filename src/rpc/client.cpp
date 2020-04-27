// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "rpc/client.h"
#include "rpc/protocol.h"
#include "util.h"
#include "support/events.h"
#include "chainparamsbase.h"
#include "utilstrencodings.h"
#include "clientversion.h"

#include <cstdint>
#include <set>

#include <boost/algorithm/string/case_conv.hpp> // for to_lower()
#include <univalue.h>

#include <event2/buffer.h>
#include <event2/keyvalq_struct.h>

class CRPCConvertParam {
public:
    std::string methodName; //!< method whose params want conversion
    int paramIdx;           //!< 0-based idx of param to convert
    std::string paramName;  //!< parameter name
};

/**
 * Specifiy a (method, idx, name) here if the argument is a non-string RPC
 * argument and needs to be converted from JSON.
 *
 * @note Parameter indexes start from 0.
 */
static const CRPCConvertParam vRPCConvertParams[] = {
    {"setmocktime", 0, "timestamp"},
    {"generate", 0, "nblocks"},
    {"generate", 1, "maxtries"},
    {"generatetoaddress", 0, "nblocks"},
    {"generatetoaddress", 2, "maxtries"},
    {"getnetworkhashps", 0, "nblocks"},
    {"getnetworkhashps", 1, "height"},
    {"sendtoaddress", 1, "amount"},
    {"sendtoaddress", 4, "subtractfeefromamount"},
    {"settxfee", 0, "amount"},
    {"getreceivedbyaddress", 1, "minconf"},
    {"getreceivedbyaccount", 1, "minconf"},
    {"listreceivedbyaddress", 0, "minconf"},
    {"listreceivedbyaddress", 1, "include_empty"},
    {"listreceivedbyaddress", 2, "include_watchonly"},
    {"listreceivedbyaccount", 0, "minconf"},
    {"listreceivedbyaccount", 1, "include_empty"},
    {"listreceivedbyaccount", 2, "include_watchonly"},
    {"getbalance", 1, "minconf"},
    {"getbalance", 2, "include_watchonly"},
    {"getblockhash", 0, "height"},
    {"waitforblockheight", 0, "height"},
    {"waitforblockheight", 1, "timeout"},
    {"waitforblock", 1, "timeout"},
    {"waitfornewblock", 0, "timeout"},
    {"move", 2, "amount"},
    {"move", 3, "minconf"},
    {"sendfrom", 2, "amount"},
    {"sendfrom", 3, "minconf"},
    {"listtransactions", 1, "count"},
    {"listtransactions", 2, "skip"},
    {"listtransactions", 3, "include_watchonly"},
    {"listaccounts", 0, "minconf"},
    {"listaccounts", 1, "include_watchonly"},
    {"walletpassphrase", 1, "timeout"},
    {"getblocktemplate", 0, "template_request"},
    {"listsinceblock", 1, "target_confirmations"},
    {"listsinceblock", 2, "include_watchonly"},
    {"sendmany", 1, "amounts"},
    {"sendmany", 2, "minconf"},
    {"sendmany", 4, "subtractfeefrom"},
    {"addmultisigaddress", 0, "nrequired"},
    {"addmultisigaddress", 1, "keys"},
    {"createmultisig", 0, "nrequired"},
    {"createmultisig", 1, "keys"},
    {"listunspent", 0, "minconf"},
    {"listunspent", 1, "maxconf"},
    {"listunspent", 2, "addresses"},
    {"getblockheader", 1, "verbose"},
    {"getchaintxstats", 0, "nblocks"},
    {"gettransaction", 1, "include_watchonly"},
    {"getrawtransaction", 1, "verbose"},
    {"createrawtransaction", 0, "inputs"},
    {"createrawtransaction", 1, "outputs"},
    {"createrawtransaction", 2, "locktime"},
    {"signrawtransaction", 1, "prevtxs"},
    {"signrawtransaction", 2, "privkeys"},
    {"sendrawtransaction", 1, "allowhighfees"},
    {"sendrawtransaction", 2, "dontcheckfee"},
    {"fundrawtransaction", 1, "options"},
    {"gettxout", 1, "n"},
    {"gettxout", 2, "include_mempool"},
    {"gettxoutproof", 0, "txids"},
    {"lockunspent", 0, "unlock"},
    {"lockunspent", 1, "transactions"},
    {"importprivkey", 2, "rescan"},
    {"importaddress", 2, "rescan"},
    {"importaddress", 3, "p2sh"},
    {"importpubkey", 2, "rescan"},
    {"importmulti", 0, "requests"},
    {"importmulti", 1, "options"},
    {"verifychain", 0, "checklevel"},
    {"verifychain", 1, "nblocks"},
    {"getblockstats", 1, "stats"},
    {"getblockstatsbyheight", 0, "height"},
    {"getblockstatsbyheight", 1, "stats"},
    {"pruneblockchain", 0, "height"},
    {"keypoolrefill", 0, "newsize"},
    {"getrawmempool", 0, "verbose"},
    {"prioritisetransaction", 1, "priority_delta"},
    {"prioritisetransaction", 2, "fee_delta"},
    {"setban", 2, "bantime"},
    {"setban", 3, "absolute"},
    {"setnetworkactive", 0, "state"},
    {"getmempoolancestors", 1, "verbose"},
    {"getmempooldescendants", 1, "verbose"},
    {"disconnectnode", 1, "nodeid"},
    {"getminingcandidate", 0, "coinbase"},
    {"getblockbyheight", 0, "height"},
    // Echo with conversion (For testing only)
    {"echojson", 0, "arg0"},
    {"echojson", 1, "arg1"},
    {"echojson", 2, "arg2"},
    {"echojson", 3, "arg3"},
    {"echojson", 4, "arg4"},
    {"echojson", 5, "arg5"},
    {"echojson", 6, "arg6"},
    {"echojson", 7, "arg7"},
    {"echojson", 8, "arg8"},
    {"echojson", 9, "arg9"},
};

class CRPCConvertTable {
private:
    std::set<std::pair<std::string, int>> members;
    std::set<std::pair<std::string, std::string>> membersByName;

public:
    CRPCConvertTable();

    bool convert(const std::string &method, int idx) {
        return (members.count(std::make_pair(method, idx)) > 0);
    }
    bool convert(const std::string &method, const std::string &name) {
        return (membersByName.count(std::make_pair(method, name)) > 0);
    }
};

CRPCConvertTable::CRPCConvertTable() {
    const unsigned int n_elem =
        (sizeof(vRPCConvertParams) / sizeof(vRPCConvertParams[0]));

    for (unsigned int i = 0; i < n_elem; i++) {
        members.insert(std::make_pair(vRPCConvertParams[i].methodName,
                                      vRPCConvertParams[i].paramIdx));
        membersByName.insert(std::make_pair(vRPCConvertParams[i].methodName,
                                            vRPCConvertParams[i].paramName));
    }
}

static CRPCConvertTable rpcCvtTable;

/**
 * Non-RFC4627 JSON parser, accepts internal values (such as numbers, true,
 * false, null) as well as objects and arrays.
 */
UniValue ParseNonRFCJSONValue(const std::string &strVal) {
    UniValue jVal;
    if (!jVal.read(std::string("[") + strVal + std::string("]")) ||
        !jVal.isArray() || jVal.size() != 1)
        throw std::runtime_error(std::string("Error parsing JSON:") + strVal);
    return jVal[0];
}

UniValue RPCConvertValues(const std::string &strMethod,
                          const std::vector<std::string> &strParams) {
    UniValue params(UniValue::VARR);

    for (unsigned int idx = 0; idx < strParams.size(); idx++) {
        const std::string &strVal = strParams[idx];

        if (!rpcCvtTable.convert(strMethod, idx)) {
            // insert string value directly
            params.push_back(strVal);
        } else {
            // parse string as JSON, insert bool/number/object/etc. value
            params.push_back(ParseNonRFCJSONValue(strVal));
        }
    }

    return params;
}

UniValue RPCConvertNamedValues(const std::string &strMethod,
                               const std::vector<std::string> &strParams) {
    UniValue params(UniValue::VOBJ);

    for (const std::string &s : strParams) {
        size_t pos = s.find("=");
        if (pos == std::string::npos) {
            throw(std::runtime_error("No '=' in named argument '" + s +
                                     "', this needs to be present for every "
                                     "argument (even if it is empty)"));
        }

        std::string name = s.substr(0, pos);
        std::string value = s.substr(pos + 1);

        if (!rpcCvtTable.convert(strMethod, name)) {
            // insert string value directly
            params.pushKV(name, value);
        } else {
            // parse string as JSON, insert bool/number/object/etc. value
            params.pushKV(name, ParseNonRFCJSONValue(value));
        }
    }

    return params;
}

const char *http_errorstring(int code)
{
    switch (code)
    {
#if LIBEVENT_VERSION_NUMBER >= 0x02010300
        case EVREQ_HTTP_TIMEOUT:
            return "timeout reached";
        case EVREQ_HTTP_EOF:
            return "EOF reached";
        case EVREQ_HTTP_INVALID_HEADER:
            return "error while reading header, or invalid header";
        case EVREQ_HTTP_BUFFER_ERROR:
            return "error encountered while reading or writing";
        case EVREQ_HTTP_REQUEST_CANCEL:
            return "request was canceled";
        case EVREQ_HTTP_DATA_TOO_LONG:
            return "response body is larger than allowed";
#endif
        default:
            return "unknown";
    }
}

#if LIBEVENT_VERSION_NUMBER >= 0x02010300
static void http_error_cb(enum evhttp_request_error err, void *ctx) {
    HTTPReply *reply = static_cast<HTTPReply *>(ctx);
    reply->error = err;
}
#endif

void http_request_done(struct evhttp_request *req, void *ctx)
{
    HTTPReply *reply = static_cast<HTTPReply *>(ctx);

    if (req == nullptr)
    {
        /**
         * If req is nullptr, it means an error occurred while connecting: the
         * error code will have been passed to http_error_cb.
         */
        reply->status = 0;
        return;
    }

    reply->status = evhttp_request_get_response_code(req);

    struct evbuffer *buf = evhttp_request_get_input_buffer(req);
    if (buf)
    {
        size_t size = evbuffer_get_length(buf);
        const char *data = (const char *)evbuffer_pullup(buf, size);
        if (data) reply->body = std::string(data, size);
        evbuffer_drain(buf, size);
    }
}

UniValue CallRPC(const std::string &strMethod, const UniValue &params)
{
    std::string host;
    // In preference order, we choose the following for the port:
    //     1. -rpcport
    //     2. port in -rpcconnect (ie following : in ipv4 or ]: in ipv6)
    //     3. default port for chain
    int port = BaseParams().RPCPort();
    SplitHostPort(gArgs.GetArg("-rpcconnect", DEFAULT_RPCCONNECT), port, host);
    port = gArgs.GetArg("-rpcport", port);

    // Obtain event base
    raii_event_base base = obtain_event_base();

    // Synchronously look up hostname
    raii_evhttp_connection evcon =
        obtain_evhttp_connection_base(base.get(), host, port);
    evhttp_connection_set_timeout(
        evcon.get(),
        gArgs.GetArg("-rpcclienttimeout", DEFAULT_HTTP_CLIENT_TIMEOUT));

    HTTPReply response;
    raii_evhttp_request req =
        obtain_evhttp_request(http_request_done, (void *)&response);
    if (req == nullptr) throw std::runtime_error("create http request failed");
#if LIBEVENT_VERSION_NUMBER >= 0x02010300
    evhttp_request_set_error_cb(req.get(), http_error_cb);
#endif

    // Get credentials
    std::string strRPCUserColonPass;
    if (gArgs.GetArg("-rpcpassword", "") == "")
    {
        // Try fall back to cookie-based authentication if no password is
        // provided
        if (!GetAuthCookie(&strRPCUserColonPass))
        {
            throw std::runtime_error(strprintf(
                _("Could not locate RPC credentials. No authentication cookie "
                  "could be found, and RPC password is not set.  See "
                  "-rpcpassword and -stdinrpcpass.  Configuration file: (%s)"),
                GetConfigFile(gArgs.GetArg("-conf", BITCOIN_CONF_FILENAME)).string().c_str()));
        }
    }
    else
    {
        strRPCUserColonPass = gArgs.GetArg("-rpcuser", "") + ":" + gArgs.GetArg("-rpcpassword", "");
    }

    struct evkeyvalq *output_headers = evhttp_request_get_output_headers(req.get());
    assert(output_headers);
    evhttp_add_header(output_headers, "Host", host.c_str());
    evhttp_add_header(output_headers, "Connection", "close");
    evhttp_add_header( output_headers, "Authorization", (std::string("Basic ") + EncodeBase64(strRPCUserColonPass)).c_str());

    // Attach request data
    std::string strRequest = JSONRPCRequestObj(strMethod, params, 1).write() + "\n";
    struct evbuffer *output_buffer = evhttp_request_get_output_buffer(req.get());
    assert(output_buffer);
    evbuffer_add(output_buffer, strRequest.data(), strRequest.size());

    // check if we should use a special wallet endpoint
    std::string endpoint = "/";
    std::string walletName = gArgs.GetArg("-rpcwallet", "");
    if (!walletName.empty())
    {
        char *encodedURI = evhttp_uriencode(walletName.c_str(), walletName.size(), false);
        if (encodedURI)
        {
            endpoint = "/wallet/" + std::string(encodedURI);
            free(encodedURI);
        }
        else
        {
            throw CConnectionFailed("uri-encode failed");
        }
    }
    int r = evhttp_make_request(evcon.get(), req.get(), EVHTTP_REQ_POST, endpoint.c_str());

    // ownership moved to evcon in above call
    req.release();
    if (r != 0)
    {
        throw CConnectionFailed("send http request failed");
    }

    event_base_dispatch(base.get());

    if (response.status == 0)
    {
        throw CConnectionFailed(strprintf(
            "couldn't connect to server: %s (code %d)\n(make sure server is "
            "running and you are connecting to the correct RPC port)",
            http_errorstring(response.error), response.error));
    }
    else if (response.status == HTTP_UNAUTHORIZED)
    {
        throw std::runtime_error("incorrect rpcuser or rpcpassword (authorization failed)");
    }
    else if (response.status >= 400 && response.status != HTTP_BAD_REQUEST &&
               response.status != HTTP_NOT_FOUND &&
               response.status != HTTP_INTERNAL_SERVER_ERROR)
    {
        throw std::runtime_error(strprintf("server returned HTTP error %d", response.status));
    }
    else if (response.body.empty())
    {
        throw std::runtime_error("no response from server");
    }

    // Parse reply
    UniValue valReply(UniValue::VSTR);
    if (!valReply.read(response.body))
    {
        throw std::runtime_error("couldn't parse reply from server");
    }
    const UniValue &reply = valReply.get_obj();
    if (reply.empty())
    {
        throw std::runtime_error("expected reply to have result, error and id properties");
    }

    return reply;
}

//
// This function returns either one of EXIT_ codes when it's expected to stop
// the process or CONTINUE_EXECUTION when it's expected to continue further.
//
int AppInitRPC(int argc, char *argv[], const std::string& usage_format, std::function<std::string(void)> help_message)
{
    try
    {
        gArgs.ParseParameters(argc, argv);
    }
    catch(const std::exception& e)
    {
       fprintf(stderr, "Error parsing program options: %s\n", e.what());
       return EXIT_FAILURE;
    }
    if (gArgs.IsArgSet("-?") || gArgs.IsArgSet("-h") || gArgs.IsArgSet("-help") || gArgs.IsArgSet("-version"))
    {
        std::string usage = strprintf(_("%s RPC client version"), _(PACKAGE_NAME)) + " " + FormatFullVersion() + "\n";

        if (!gArgs.IsArgSet("-version"))
            usage += usage_format + "\n" + help_message();

        fprintf(stdout, "%s", usage.c_str());
        if (argc < 2)
        {
            fprintf(stderr, "Error: too few parameters\n");
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    }
    if (!fs::is_directory(GetDataDir(false)))
    {
        fprintf(stderr, "Error: Specified data directory \"%s\" does not exist.\n", gArgs.GetArg("-datadir", "").c_str());
        return EXIT_FAILURE;
    }
    try
    {
        gArgs.ReadConfigFile(gArgs.GetArg("-conf", BITCOIN_CONF_FILENAME));
    }
    catch (const std::exception &e)
    {
        fprintf(stderr, "Error reading configuration file: %s\n", e.what());
        return EXIT_FAILURE;
    }
    // Check for -testnet or -regtest parameter (BaseParams() calls are only
    // valid after this clause)
    try
    {
        SelectBaseParams(ChainNameFromCommandLine());
    }
    catch (const std::exception &e)
    {
        fprintf(stderr, "Error: %s\n", e.what());
        return EXIT_FAILURE;
    }
    if (gArgs.GetBoolArg("-rpcssl", false))
    {
        fprintf(stderr, "Error: SSL mode for RPC (-rpcssl) is no longer supported.\n");
        return EXIT_FAILURE;
    }
    return CONTINUE_EXECUTION;
}

