// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "rpc/client.h"
#include "rpc/client_config.h"
#include "rpc/client_utils.h"
#include "rpc/protocol.h"
#include "rpc/http_request.h"
#include "rpc/http_response.h"
#include "util.h"
#include "chainparamsbase.h"
#include "utilstrencodings.h"
#include "clientversion.h"
#include <set>
#include <univalue.h>
#include <event2/buffer.h>

// NOLINTNEXTLINE (cppcoreguidelines-pro-type-member-init)
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
// NOLINTNEXTLINE (cppcoreguidelines-avoid-c-arrays)
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
    {"sendrawtransactions", 0, "inputs"},
    {"fundrawtransaction", 1, "options"},
    {"gettxout", 1, "n"},
    {"gettxout", 2, "include_mempool"},
    {"gettxouts", 0, "txids_vouts"},
    {"gettxouts", 1, "return_fields"},
    {"gettxouts", 2, "include_mempool"},
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
    {"verifymerkleproof", 0, "proof"},
    {"softrejectblock", 1, "numblocks"},
    {"acceptblock", 1, "numblocks"},
    {"getsoftrejectedblocks", 0, "onlymarked"},
    {"verifyscript", 0, "scripts"},
    {"verifyscript", 1, "stopOnFirstInvalid"},
    {"verifyscript", 2, "totalTimeout"},
    {"getmerkleproof2",2,"includeFullTx"},
    {"addToPolicyBlacklist", 0, "funds"},
    {"addToConsensusBlacklist", 0, "funds"},
    {"removeFromPolicyBlacklist", 0, "funds"},
    {"clearBlacklists", 0, "removeAllEntries"},
    {"addToConfiscationTxidWhitelist", 0, "txs"},
    {"queryConfiscationTxidWhitelist", 0, "verbose"},
    {"rebuildminerids", 0, "fullrebuild"},
    {"revokeminerid", 0, "input"},
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
    {"createdatareftx", 0, "inputs"},
    {"setminerinfotxfundingoutpoint", 1, "n"},
};

class CRPCConvertTable {
private:
    std::set<std::pair<std::string, unsigned int>> members;
    std::set<std::pair<std::string, std::string>> membersByName;

public:
    CRPCConvertTable();

    bool contains(const std::string &method, unsigned int idx) {
        return (members.count(std::make_pair(method, idx)) > 0);
    }
    bool contains(const std::string &method, const std::string &name) {
        return (membersByName.count(std::make_pair(method, name)) > 0);
    }
};

// NOLINTNEXTLINE (cppcoreguidelines-pro-type-member-init)
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

// NOLINTNEXTLINE cppcoreguidelines-avoid-non-const-global-variables
static CRPCConvertTable rpcCvtTable; // NOLINTNEXTLINE (cert-err58-cpp)

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

        if (!rpcCvtTable.contains(strMethod, idx)) {
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
        size_t pos = s.find('=');
        if (pos == std::string::npos) {
            throw(std::runtime_error("No '=' in named argument '" + s +
                                     "', this needs to be present for every "
                                     "argument (even if it is empty)"));
        }

        std::string name = s.substr(0, pos);
        std::string value = s.substr(pos + 1);

        if (!rpcCvtTable.contains(strMethod, name)) {
            // insert string value directly
            params.pushKV(name, value);
        } else {
            // parse string as JSON, insert bool/number/object/etc. value
            params.pushKV(name, ParseNonRFCJSONValue(value));
        }
    }

    return params;
}

UniValue CallRPC(const std::string &strMethod, const UniValue &params)
{
    // Create config & response objects
    rpc::client::RPCClientConfig config { rpc::client::RPCClientConfig::CreateForBitcoind() };
    rpc::client::StringHTTPResponse response {};

    // Call RPC
    rpc::client::RPCClient client { config };
    rpc::client::HTTPRequest request { rpc::client::HTTPRequest::CreateJSONRPCRequest(config, strMethod, params) };
    client.SubmitRequest(request, &response);

    // Extract response
    UniValue valReply { UniValue::VSTR };
    if(!valReply.read(response.GetBody()))
    {
        throw std::runtime_error("couldn't parse reply from server");
    }
    const UniValue& reply { valReply.get_obj() };
    if(reply.empty())
    {
        throw std::runtime_error("expected reply to have result, error and id properties");
    }

    return reply;
}

//
// This function returns either one of EXIT_ codes when it's expected to stop
// the process or CONTINUE_EXECUTION when it's expected to continue further.
//
// NOLINTNEXTLINE (cppcoreguidelines-avoid-c-arrays)
int AppInitRPC(int argc, char *argv[], const std::string& usage_format, const std::function<std::string(void)>& help_message)
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

