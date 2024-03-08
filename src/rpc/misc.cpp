// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "rpc/misc.h"
#include "base58.h"
#include "block_index_store.h"
#include "clientversion.h"
#include "config.h"
#include "dstencode.h"
#include "init.h"
#include "net/net.h"
#include "net/netbase.h"
#include "policy/policy.h"
#include "rpc/blockchain.h"
#include "rpc/server.h"
#include "timedata.h"
#include "txdb.h"
#include "util.h"
#include "utilstrencodings.h"
#include "validation.h"

#ifdef ENABLE_WALLET
#include "wallet/rpcwallet.h"
#include "wallet/wallet.h"
#include "wallet/walletdb.h"
#endif

#include "vmtouch.h"
#include <univalue.h>
#include <cstdint>

/**
 * @note Do not add or change anything in the information returned by this
 * method. `getinfo` exists for backwards-compatibility only. It combines
 * information from wildly different sources in the program, which is a mess,
 * and is thus planned to be deprecated eventually.
 *
 * Based on the source of the information, new information should be added to:
 * - `getblockchaininfo`,
 * - `getnetworkinfo` or
 * - `getwalletinfo`
 *
 * Or alternatively, create a specific query method for the information.
 **/
static UniValue getinfo(const Config &config, const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() != 0) {
        throw std::runtime_error(
            "getinfo\n"
            "\nDEPRECATED. Returns an object containing various state info.\n"
            "\nResult:\n"
            "{\n"
            "  \"version\": xxxxx,           (numeric) the server version\n"
            "  \"protocolversion\": xxxxx,   (numeric) the protocol version\n"
            "  \"walletversion\": xxxxx,     (numeric) the wallet version\n"
            "  \"balance\": xxxxxxx,         (numeric) the total bitcoin "
            "balance of the wallet\n"
            "  \"blocks\": xxxxxx,           (numeric) the current number of "
            "blocks processed in the server\n"
            "  \"timeoffset\": xxxxx,        (numeric) the time offset\n"
            "  \"connections\": xxxxx,       (numeric) the number of "
            "connections\n"
            "  \"proxy\": \"host:port\",       (string, optional) the proxy used "
            "by the server\n"
            "  \"difficulty\": xxxxxx,       (numeric) the current difficulty\n"
            "  \"testnet\": true|false,      (boolean) if the server is using "
            "testnet or not\n"
            "  \"stn\": true|false,          (boolean) if the server is using "
            "stn or not\n"
            "  \"keypoololdest\": xxxxxx,    (numeric) the timestamp (seconds "
            "since Unix epoch) of the oldest pre-generated key in the key "
            "pool\n"
            "  \"keypoolsize\": xxxx,        (numeric) how many new keys are "
            "pre-generated\n"
            "  \"paytxfee\": x.xxxx,         (numeric) the transaction fee set "
            "in " +
            CURRENCY_UNIT + "/kB\n"
                            "  \"relayfee\": x.xxxx,         (numeric) minimum "
                            "relay fee for non-free transactions in " +
            CURRENCY_UNIT +
            "/kB\n"
            "  \"errors\": \"...\",            (string) any error messages\n"
            "  \"maxblocksize\": xxxxx,      (numeric) The absolute maximum block "
            "size we will accept from any source\n"
            "  \"maxminedblocksize\": xxxxx  (numeric) The maximum block size "
            "we will mine\n"
            "  \"maxstackmemoryusagepolicy\": xxxxx, (numeric) Policy value of "
            "max stack memory usage\n"
            "  \"maxStackMemoryUsageConsensus\": xxxxx, (numeric) Consensus value of "
            "max stack memory usage\n"
            "}\n"
            "\nExamples:\n" +
            HelpExampleCli("getinfo", "") + HelpExampleRpc("getinfo", ""));
    }

#ifdef ENABLE_WALLET
    CWallet *const pwallet = GetWalletForJSONRPCRequest(request);

    LOCK2(cs_main, pwallet ? &pwallet->cs_wallet : nullptr);
#endif
    
    auto tip = chainActive.Tip();

    proxyType proxy;
    GetProxy(NET_IPV4, proxy);

    UniValue obj(UniValue::VOBJ);
    obj.push_back(Pair("version", CLIENT_VERSION));
    obj.push_back(Pair("protocolversion", PROTOCOL_VERSION));
#ifdef ENABLE_WALLET
    if (pwallet) {
        obj.push_back(Pair("walletversion", pwallet->GetVersion()));
        obj.push_back(Pair("balance", ValueFromAmount(pwallet->GetBalance())));
    }
#endif
    obj.push_back(Pair("initcomplete", GetAppInitCompleted()));
    obj.push_back(Pair("blocks", (int)(tip ? tip->GetHeight() : -1)));
    obj.push_back(Pair("timeoffset", GetTimeOffset()));
    if (g_connman) {
        obj.push_back(
            Pair("connections",
                 (int)g_connman->GetNodeCount(CConnman::CONNECTIONS_ALL)));
    }
    obj.push_back(Pair("proxy", (proxy.IsValid() ? proxy.proxy.ToStringIPPort()
                                                 : std::string())));
    obj.push_back(Pair("difficulty", double(GetDifficulty(tip))));
    obj.push_back(Pair("testnet",
                       config.GetChainParams().NetworkIDString() ==
                           CBaseChainParams::TESTNET));
    obj.push_back(Pair("stn",
                       config.GetChainParams().NetworkIDString() ==
                       CBaseChainParams::STN));
#ifdef ENABLE_WALLET
    if (pwallet) {
        obj.push_back(Pair("keypoololdest", pwallet->GetOldestKeyPoolTime()));
        obj.push_back(Pair("keypoolsize", (int)pwallet->GetKeyPoolSize()));
    }
    if (pwallet && pwallet->IsCrypted()) {
        obj.push_back(Pair("unlocked_until", pwallet->nRelockTime));
    }
    obj.push_back(Pair("paytxfee", ValueFromAmount(payTxFee.GetFeePerK())));
#endif
    obj.push_back(Pair("relayfee",
                       ValueFromAmount(config.GetMinFeePerKB().GetFeePerK())));
    obj.push_back(Pair("errors", GetWarnings("statusbar")));
    obj.push_back(Pair("maxblocksize", config.GetMaxBlockSize()));
    obj.push_back(Pair("maxminedblocksize", config.GetMaxGeneratedBlockSize()));
    obj.push_back(Pair("maxstackmemoryusagepolicy", 
                       config.GetMaxStackMemoryUsage(true, false)));
    obj.push_back(Pair("maxstackmemoryusageconsensus",
                       config.GetMaxStackMemoryUsage(true, true)));
    return obj;
}

#ifdef ENABLE_WALLET
class DescribeAddressVisitor : public boost::static_visitor<UniValue> {
public:
    CWallet *const pwallet;

    DescribeAddressVisitor(CWallet *_pwallet) : pwallet(_pwallet) {}

    UniValue operator()(const CNoDestination &dest) const {
        return UniValue(UniValue::VOBJ);
    }

    UniValue operator()(const CKeyID &keyID) const {
        UniValue obj(UniValue::VOBJ);
        CPubKey vchPubKey;
        obj.push_back(Pair("isscript", false));
        if (pwallet && pwallet->GetPubKey(keyID, vchPubKey)) {
            obj.push_back(Pair("pubkey", HexStr(vchPubKey)));
            obj.push_back(Pair("iscompressed", vchPubKey.IsCompressed()));
        }
        return obj;
    }

    UniValue operator()(const CScriptID &scriptID) const {
        UniValue obj(UniValue::VOBJ);
        CScript subscript;
        obj.push_back(Pair("isscript", true));
        if (pwallet && pwallet->GetCScript(scriptID, subscript)) {
            std::vector<CTxDestination> addresses;
            txnouttype whichType;
            int nRequired;
            // DescribeAddressVisitor is used by RPC call validateaddress, which only takes address as input. 
            // We have no block height available - treat all transactions as post-Genesis except P2SH to be able to spend them.
            const bool isGenesisEnabled = !IsP2SH(subscript);
            ExtractDestinations(subscript, isGenesisEnabled, whichType, addresses, nRequired);
            obj.push_back(Pair("script", GetTxnOutputType(whichType)));
            obj.push_back(
                Pair("hex", HexStr(subscript.begin(), subscript.end())));
            UniValue a(UniValue::VARR);
            for (const CTxDestination &addr : addresses) {
                a.push_back(EncodeDestination(addr));
            }
            obj.push_back(Pair("addresses", a));
            if (whichType == TX_MULTISIG) {
                obj.push_back(Pair("sigsrequired", nRequired));
            }
        }
        return obj;
    }
};
#endif

static UniValue validateaddress(const Config &config,
                                const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() != 1) {
        throw std::runtime_error(
            "validateaddress \"address\"\n"
            "\nReturn information about the given bitcoin address.\n"
            "\nArguments:\n"
            "1. \"address\"     (string, required) The bitcoin address to "
            "validate\n"
            "\nResult:\n"
            "{\n"
            "  \"isvalid\" : true|false,       (boolean) If the address is "
            "valid or not. If not, this is the only property returned.\n"
            "  \"address\" : \"address\", (string) The bitcoin address "
            "validated\n"
            "  \"scriptPubKey\" : \"hex\",       (string) The hex encoded "
            "scriptPubKey generated by the address\n"
            "  \"ismine\" : true|false,        (boolean) If the address is "
            "yours or not\n"
            "  \"iswatchonly\" : true|false,   (boolean) If the address is "
            "watchonly\n"
            "  \"isscript\" : true|false,      (boolean) If the key is a "
            "script\n"
            "  \"pubkey\" : \"publickeyhex\",    (string) The hex value of the "
            "raw public key\n"
            "  \"iscompressed\" : true|false,  (boolean) If the address is "
            "compressed\n"
            "  \"account\" : \"account\"         (string) DEPRECATED. The "
            "account associated with the address, \"\" is the default account\n"
            "  \"timestamp\" : timestamp,        (number, optional) The "
            "creation time of the key if available in seconds since epoch (Jan "
            "1 1970 GMT)\n"
            "  \"hdkeypath\" : \"keypath\"       (string, optional) The HD "
            "keypath if the key is HD and available\n"
            "  \"hdmasterkeyid\" : \"<hash160>\" (string, optional) The "
            "Hash160 of the HD master pubkey\n"
            "}\n"
            "\nExamples:\n" +
            HelpExampleCli("validateaddress",
                           "\"1PSSGeFHDnKNxiEyFrD1wcEaHr9hrQDDWc\"") +
            HelpExampleRpc("validateaddress",
                           "\"1PSSGeFHDnKNxiEyFrD1wcEaHr9hrQDDWc\""));
    }

#ifdef ENABLE_WALLET
    CWallet *const pwallet = GetWalletForJSONRPCRequest(request);

    LOCK2(cs_main, pwallet ? &pwallet->cs_wallet : nullptr);
#endif

    CTxDestination dest =
        DecodeDestination(request.params[0].get_str(), config.GetChainParams());
    bool isValid = IsValidDestination(dest);

    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("isvalid", isValid));
    if (isValid) {
        std::string currentAddress = EncodeDestination(dest);
        ret.push_back(Pair("address", currentAddress));

        CScript scriptPubKey = GetScriptForDestination(dest);
        ret.push_back(Pair("scriptPubKey",
                           HexStr(scriptPubKey.begin(), scriptPubKey.end())));

#ifdef ENABLE_WALLET
        isminetype mine = pwallet ? IsMine(*pwallet, dest) : ISMINE_NO;
        ret.push_back(Pair("ismine", (mine & ISMINE_SPENDABLE) ? true : false));
        ret.push_back(
            Pair("iswatchonly", (mine & ISMINE_WATCH_ONLY) ? true : false));
        UniValue detail =
            boost::apply_visitor(DescribeAddressVisitor(pwallet), dest);
        ret.pushKVs(detail);
        if (pwallet && pwallet->mapAddressBook.count(dest)) {
            ret.push_back(Pair("account", pwallet->mapAddressBook[dest].name));
        }
        if (pwallet) {
            const auto &meta = pwallet->mapKeyMetadata;
            const CKeyID *keyID = boost::get<CKeyID>(&dest);
            auto it = keyID ? meta.find(*keyID) : meta.end();
            if (it == meta.end()) {
                it = meta.find(CScriptID(scriptPubKey));
            }
            if (it != meta.end()) {
                ret.push_back(Pair("timestamp", it->second.nCreateTime));
                if (!it->second.hdKeypath.empty()) {
                    ret.push_back(Pair("hdkeypath", it->second.hdKeypath));
                    ret.push_back(Pair("hdmasterkeyid",
                                       it->second.hdMasterKeyID.GetHex()));
                }
            }
        }
#endif
    }
    return ret;
}

// Needed even with !ENABLE_WALLET, to pass (ignored) pointers around
class CWallet;

/**
 * Used by addmultisigaddress / createmultisig:
 */
CScript createmultisig_redeemScript(CWallet *const pwallet,
                                    const UniValue &params) {
    int nRequired = params[0].get_int();
    const UniValue &keys = params[1].get_array();

    // Gather public keys
    if (nRequired < 1) {
        throw std::runtime_error(
            "a multisignature address must require at least one key to redeem");
    }
    if ((int)keys.size() < nRequired) {
        throw std::runtime_error(
            strprintf("not enough keys supplied "
                      "(got %u keys, but need at least %d to redeem)",
                      keys.size(), nRequired));
    }
    if (keys.size() > 16) {
        throw std::runtime_error(
            "Number of addresses involved in the "
            "multisignature address creation > 16\nReduce the "
            "number");
    }
    std::vector<CPubKey> pubkeys;
    pubkeys.resize(keys.size());
    for (size_t i = 0; i < keys.size(); i++) {
        const std::string &ks = keys[i].get_str();
#ifdef ENABLE_WALLET
        // Case 1: Bitcoin address and we have full public key:
        if (pwallet) {
            CTxDestination dest = DecodeDestination(ks, pwallet->chainParams);
            if (IsValidDestination(dest)) {
                const CKeyID *keyID = boost::get<CKeyID>(&dest);
                if (!keyID) {
                    throw std::runtime_error(
                        strprintf("%s does not refer to a key", ks));
                }
                CPubKey vchPubKey;
                if (!pwallet->GetPubKey(*keyID, vchPubKey)) {
                    throw std::runtime_error(
                        strprintf("no full public key for address %s", ks));
                }
                if (!vchPubKey.IsFullyValid()) {
                    throw std::runtime_error(" Invalid public key: " + ks);
                }
                pubkeys[i] = vchPubKey;
                continue;
            }
        }
#endif
        // Case 2: hex public key
        if (IsHex(ks)) {
            CPubKey vchPubKey(ParseHex(ks));
            if (!vchPubKey.IsFullyValid()) {
                throw std::runtime_error(" Invalid public key: " + ks);
            }
            pubkeys[i] = vchPubKey;
        } else {
            throw std::runtime_error(" Invalid public key: " + ks);
        }
    }

    CScript result = GetScriptForMultisig(nRequired, pubkeys);
    if (result.size() > MAX_SCRIPT_ELEMENT_SIZE_BEFORE_GENESIS) {
        throw std::runtime_error(
            strprintf("redeemScript exceeds size limit: %d > %d", result.size(),
                      MAX_SCRIPT_ELEMENT_SIZE_BEFORE_GENESIS));
    }

    return result;
}

static UniValue createmultisig(const Config &config,
                               const JSONRPCRequest &request) {
#ifdef ENABLE_WALLET
    CWallet *const pwallet = GetWalletForJSONRPCRequest(request);
#else
    CWallet *const pwallet = nullptr;
#endif

    if (request.fHelp || request.params.size() < 2 ||
        request.params.size() > 2) {
        std::string msg =
            "createmultisig nrequired [\"key\",...]\n"
            "\nCreates a multi-signature address with n signature of m keys "
            "required.\n"
            "It returns a json object with the address and redeemScript.\n"

            "\nArguments:\n"
            "1. nrequired      (numeric, required) The number of required "
            "signatures out of the n keys or addresses.\n"
            "2. \"keys\"       (string, required) A json array of keys which "
            "are bitcoin addresses or hex-encoded public keys\n"
            "     [\n"
            "       \"key\"    (string) bitcoin address or hex-encoded public "
            "key\n"
            "       ,...\n"
            "     ]\n"

            "\nResult:\n"
            "{\n"
            "  \"address\":\"multisigaddress\",  (string) The value of the new "
            "multisig address.\n"
            "  \"redeemScript\":\"script\"       (string) The string value of "
            "the hex-encoded redemption script.\n"
            "}\n"

            "\nExamples:\n"
            "\nCreate a multisig address from 2 addresses\n" +
            HelpExampleCli("createmultisig",
                           "2 "
                           "\"[\\\"16sSauSf5pF2UkUwvKGq4qjNRzBZYqgEL5\\\","
                           "\\\"171sgjn4YtPu27adkKGrdDwzRTxnRkBfKV\\\"]\"") +
            "\nAs a json rpc call\n" +
            HelpExampleRpc("createmultisig",
                           "2, "
                           "[\"16sSauSf5pF2UkUwvKGq4qjNRzBZYqgEL5\","
                           "\"171sgjn4YtPu27adkKGrdDwzRTxnRkBfKV\"]");
        throw std::runtime_error(msg);
    }

    // Construct using pay-to-script-hash:
    CScript inner = createmultisig_redeemScript(pwallet, request.params);
    CScriptID innerID(inner);

    UniValue result(UniValue::VOBJ);
    result.push_back(Pair("address", EncodeDestination(innerID)));
    result.push_back(Pair("redeemScript", HexStr(inner.begin(), inner.end())));

    return result;
}

static UniValue verifymessage(const Config &config,
                              const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() != 3) {
        throw std::runtime_error(
            "verifymessage \"address\" \"signature\" \"message\"\n"
            "\nVerify a signed message\n"
            "\nArguments:\n"
            "1. \"address\"         (string, required) The bitcoin address to "
            "use for the signature.\n"
            "2. \"signature\"       (string, required) The signature provided "
            "by the signer in base 64 encoding (see signmessage).\n"
            "3. \"message\"         (string, required) The message that was "
            "signed.\n"
            "\nResult:\n"
            "true|false   (boolean) If the signature is verified or not.\n"
            "\nExamples:\n"
            "\nUnlock the wallet for 30 seconds\n" +
            HelpExampleCli("walletpassphrase", "\"mypassphrase\" 30") +
            "\nCreate the signature\n" +
            HelpExampleCli(
                "signmessage",
                "\"1D1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XX\" \"my message\"") +
            "\nVerify the signature\n" +
            HelpExampleCli("verifymessage", "\"1D1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4"
                                            "XX\" \"signature\" \"my "
                                            "message\"") +
            "\nAs json rpc\n" +
            HelpExampleRpc("verifymessage", "\"1D1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4"
                                            "XX\", \"signature\", \"my "
                                            "message\""));
    }

    std::string strAddress = request.params[0].get_str();
    std::string strSign = request.params[1].get_str();
    std::string strMessage = request.params[2].get_str();

    CTxDestination destination =
        DecodeDestination(strAddress, config.GetChainParams());
    if (!IsValidDestination(destination)) {
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid address");
    }

    const CKeyID *keyID = boost::get<CKeyID>(&destination);
    if (!keyID) {
        throw JSONRPCError(RPC_TYPE_ERROR, "Address does not refer to key");
    }

    bool fInvalid = false;
    std::vector<uint8_t> vchSig = DecodeBase64(strSign.c_str(), &fInvalid);

    if (fInvalid) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,
                           "Malformed base64 encoding");
    }

    CHashWriter ss(SER_GETHASH, 0);
    ss << strMessageMagic;
    ss << strMessage;

    CPubKey pubkey;
    if (!pubkey.RecoverCompact(ss.GetHash(), vchSig)) {
        return false;
    }

    return (pubkey.GetID() == *keyID);
}

static UniValue verifyscript(const Config& config, const JSONRPCRequest& request)
{
    if(request.fHelp)
    {
        throw std::runtime_error( R"(verifyscript <scripts> [<stopOnFirstInvalid> [<totalTimeout>]]

Verify a script in given transactions.

Script to be verified is defined by unlock script in n-th input of specified transaction and lock script in spent transaction output.

Script verification in general depends on node configuration and state:
  - Node configuration defines script related limits and policies.
  - Block height is needed to obtain values of script verification flags (e.g. BIPs, genesis...).
  - UTXO database and mempool are needed to get TXO providing the lock script.

Limits and policies specified in node configuration always apply and may affect script verification (e.g. maxscriptsizepolicy, maxstdtxvalidationduration ...).
Dependency on node state can be avoided by explicitly providing required data.

Arguments:
  1. scripts (array, required)
        JSON array specifying scripts that will be verified.
        [
          {
            # (required) Hex-string of transaction containing unlock script (input) to be verified
            tx: <string>,

            # (required) Input of the transaction providing the unlock script to be verified
            n: <integer>,

            # (optional) Bit field providing script verification flags.
            # If not specified, flags are defined by prevblockhash and txo.height.
            # Script flags are defined in source file script_flags.h.
            flags: <integer>,

            # (optional) If true, actual value of flags used to verify script is included in verification result object.
            reportflags: <boolean>,

            # (optional) Hash of parent of the block containing the transaction tx (default: current tip)
            # Used to obtain script verification flags. Only allowed if flags is not present.
            prevblockhash: <string>,

            # (optional) Data for transaction output spent by the n-th input.
            # By default it is obtained from current UTXO database or mempool using n-th input of transaction.
            txo: {
              # (required) Hex-string of the lock script
              lock: <string>,

              # (required) Value of transaction output (in satoshi)
              value: <integer>,

              # Height at which this transaction output was created (-1=mempool height)
              # Used to obtain script verification flags that depend on height of TXO.
              # If flags is present, this is optional and overrides the value in flags.
              # If flags is not present, this is required.
              height: <integer>
            }
          }, ...
        ]

  2. stopOnFirstInvalid (boolean, optional default=true)
        If true and an invalid script is encountered, subsequent scripts will not be verified.
        If false, all scripts are verified.

  3. totalTimeout (integer, optional default=100)
        Execution will stop if total script verification time exceeds this value (in ms).
        Note that actual timeout may be lower if node does not allow script verification to take this long.

Result:
  JSON array containing verification results.
  It has the same number of elements as <scripts> argument with each element providing verification result of the corresponding script.
  [
    {
      result: <string>,
      description: <string>  # (optional)
      flags: <integer> # (optional)
    }, ...
  ]
  Possible values for "result":
    "ok"      : Script verification succeeded.
    "error"   : Script verification failed. Script was determined to be invalid. More info may be provided in "description".
    "timeout" : Script verification was aborted because total allowed script verification time was exceeded or because verification of this script took longer than permitted in node configuration (maxstdtxvalidationduration).
    "skipped" : Script verification was skipped. This could happen because total allowed script verification time was exceeded or because previous script verification failed and stopOnFirstInvalid was specified.

Examples:
)" +
            HelpExampleCli("verifyscript", R"("[{\"tx\": \"<txhex>\", \"n\": 0}]" true 100)") +
            HelpExampleRpc("verifyscript", R"([{"tx": "<txhex>", "n": 0}], true, 100)")
        );
    }

    if(request.params.size() < 1)
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Missing required argument (scripts)!");
    }
    if(request.params.size() > 3)
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Too many arguments (>3)!");
    }

    // Parse stopOnFirstInvalid argument
    const bool stopOnFirstInvalid = [&request]{
        if(request.params.size() < 2)
        {
            return false;
        }
        return request.params[1].get_bool();
    }();

    // Parse totalTimeout argument
    const std::chrono::milliseconds totalTimeout = [&request]{
        if(request.params.size() < 3)
        {
            return std::chrono::milliseconds{100};
        }
        std::chrono::milliseconds tt{request.params[2].get_int()};
        if(tt<std::chrono::milliseconds::zero())
        {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid value for totalTimeout argument!");
        }
        return tt;
    }();

    // Timed cancellation source that will abort script verification if total allowed time is exceeded.
    // Timer is started now so that it also includes parsing scripts argument and getting TXO which could also take a while.
    const auto cancellation_source = task::CTimedCancellationSource::Make( totalTimeout );

    // Parse scripts argument
    struct ScriptToVerify
    {
        ScriptToVerify(CMutableTransaction&& tx, uint32_t n, CScript&& txo_lock, Amount txo_value, uint32_t flags, bool reportflags)
        : tx{std::move(tx)}
        , n{n}
        , txo_lock{std::move(txo_lock)}
        , txo_value{std::move(txo_value)}
        , flags{flags}
        , reportflags{reportflags}
        {}

        // Data needed by script verification
        CTransaction tx;
        uint32_t n;
        CScript txo_lock;
        Amount txo_value;
        uint32_t flags;
        bool reportflags;

        // Verification result
        mutable std::string result;
        mutable std::string result_desc;
    };
    const std::vector<ScriptToVerify> scripts = [&config](const UniValue& scripts_json){
        std::vector<ScriptToVerify> scripts_tmp;
        scripts_tmp.reserve(scripts_json.size());

        // Current tip is default value for prevblockhash parameter
        const CBlockIndex* tip = chainActive.Tip();

        // Coins view is used to find TXOs spent by transaction if txo parameter is not provided
        CoinsDBView tipView{*pcoinsTip};
        CCoinsViewMemPool viewMempool{tipView, mempool};
        CCoinsViewCache view{viewMempool};

        // Expected structure of object items in <script> JSON array
        const std::map<std::string, UniValueType> expected_type_script_json{
            {"tx",            UniValueType(UniValue::VSTR)},
            {"n",             UniValueType(UniValue::VNUM)},
            {"flags",         UniValueType(UniValue::VNUM)},
            {"reportflags",   UniValueType(UniValue::VBOOL)},
            {"prevblockhash", UniValueType(UniValue::VSTR)},
            {"txo",           UniValueType(UniValue::VOBJ)}
        };
        for(const auto& item: scripts_json.getValues())
        {
            RPCTypeCheckObj(item, expected_type_script_json, true, true);

            // Current item in array as string. Used to report errors.
            std::string itemstr = "scripts["+std::to_string(scripts_tmp.size())+"]";

            const auto& tx_hexstr_json = item["tx"];
            if(tx_hexstr_json.isNull())
            {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Missing "+itemstr+".tx!");
            }
            CMutableTransaction mtx;
            if(!DecodeHexTx(mtx, tx_hexstr_json.get_str()))
            {
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed for "+itemstr+".tx!");
            }

            const auto& n_json = item["n"];
            if(n_json.isNull())
            {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Missing "+itemstr+".n!");
            }
            int n = n_json.get_int();
            if(n<0 || static_cast<std::size_t>(n)>=mtx.vin.size())
            {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid value for n in "+itemstr+"!");
            }

            uint32_t flags;
            const auto& flags_json = item["flags"];
            const auto& prevblockhash_json = item["prevblockhash"];
            if(!flags_json.isNull())
            {
                flags = static_cast<uint32_t>(flags_json.get_int());
                if(!prevblockhash_json.isNull())
                {
                    throw JSONRPCError(RPC_INVALID_PARAMETER, "Both flags and prevblockhash specified in "+itemstr+"!");
                }
            }
            else
            {
                const CBlockIndex* pindexPrev;
                if(prevblockhash_json.isNull())
                {
                    pindexPrev = tip;
                }
                else
                {
                    auto prevblockhash = ParseHashV(prevblockhash_json, itemstr+".prevblockhash");

                    pindexPrev = mapBlockIndex.Get(prevblockhash);
                    if(pindexPrev==nullptr)
                    {
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Unknown block ("+prevblockhash.GetHex()+") specified by "+itemstr+".prevblockhash!");
                    }
                }

                // Use script verification flags corresponding to parent block
                flags = GetBlockScriptFlags(config, pindexPrev);
            }

            CScript txo_lock;
            Amount txo_value;
            std::optional<int32_t> txo_height;
            const auto& txo_json = item["txo"];
            if(!txo_json.isNull())
            {
                RPCTypeCheckObj(
                    txo_json,
                    {
                        {"lock",   UniValueType(UniValue::VSTR)},
                        {"value",  UniValueType(UniValue::VNUM)},
                        {"height", UniValueType(UniValue::VNUM)}
                    }, true, true);

                const auto& txo_lock_json = txo_json["lock"];
                if(txo_lock_json.isNull())
                {
                    throw JSONRPCError(RPC_INVALID_PARAMETER, "Missing "+itemstr+".txo.lock!");
                }
                std::vector<uint8_t> txo_lock_bin;
                const std::string& txo_lock_str = txo_lock_json.get_str();
                if(!txo_lock_str.empty())
                {
                    txo_lock_bin = ParseHexV(txo_lock_str, itemstr+".txo.lock");
                }
                txo_lock = CScript(txo_lock_bin.begin(), txo_lock_bin.end());

                const auto& txo_value_json = txo_json["value"];
                if(txo_value_json.isNull())
                {
                    throw JSONRPCError(RPC_INVALID_PARAMETER, "Missing "+itemstr+".txo.value!");
                }
                auto txo_value_int = txo_value_json.get_int64();
                if(txo_value_int<0)
                {
                    throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid value for "+itemstr+".txo.value!");
                }
                txo_value = Amount(txo_value_int);

                const auto& txo_height_json = txo_json["height"];
                if(txo_height_json.isNull())
                {
                    if(flags_json.isNull())
                    {
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Missing "+itemstr+".txo.height!");
                    }
                }
                else
                {
                    txo_height = txo_height_json.get_int();
                    if(*txo_height<-1)
                    {
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid value for "+itemstr+".txo.height!");
                    }
                    if(*txo_height<0)
                    {
                        txo_height = MEMPOOL_HEIGHT;
                    }
                }
            }
            else
            {
                // Read lock script from coinsdb
                std::optional<CoinWithScript> coin = view.GetCoinWithScript(mtx.vin[n].prevout);
                if(!coin.has_value())
                {
                    throw JSONRPCError(RPC_INVALID_PARAMETER, "Unable to find TXO spent by transaction "+itemstr+".tx!");
                }
                txo_lock = coin->GetTxOut().scriptPubKey;
                txo_value = coin->GetAmount();
                txo_height = coin->GetHeight();
            }

            if(txo_height.has_value())
            {
                if(*txo_height == MEMPOOL_HEIGHT)
                {
                    // When spending an output that was created in mempool, we assume that it will be mined in the next block.
                    *txo_height = tip->GetHeight() + 1;
                }

                // If txo.height was specified (or we got it from coinsdb),
                // it overrides per-input script verification flags.
                flags &= ~SCRIPT_UTXO_AFTER_GENESIS;
                if(IsGenesisEnabled(config, *txo_height))
                {
                    flags |= SCRIPT_UTXO_AFTER_GENESIS;
                }
            }

            scripts_tmp.emplace_back(std::move(mtx), n, std::move(txo_lock), txo_value, flags, item["reportflags"].getBool());
        }

        return scripts_tmp;
    }(request.params[0].get_array());


    // Verify all scripts
    bool failed = false;
    for(auto& scr: scripts)
    {
        if(failed && stopOnFirstInvalid)
        {
            scr.result = "skipped";
            scr.result_desc = "Verification of previous script failed.";
            continue;
        }

        if(cancellation_source->IsCanceled())
        {
            scr.result = "skipped";
            scr.result_desc = "Total script verification time ("+std::to_string(totalTimeout.count())+"ms) exceeded.";
            continue;
        }

        CScriptCheck script_check{
            config,
            false, // consensus = false
            scr.txo_lock,
            scr.txo_value,
            scr.tx,
            scr.n,
            scr.flags,
            false, // no cache
            PrecomputedTransactionData(scr.tx)
        };

        auto t0 = std::chrono::steady_clock::now();
        auto res = script_check( task::CCancellationToken::JoinToken(
            // Cancel if total allowed time is exceeded
            cancellation_source,
            // Cancel if it takes longer than longest allowed validation of standard transaction
            task::CTimedCancellationSource::Make(config.GetMaxStdTxnValidationDuration())
        ));

        if(!res.has_value())
        {
            scr.result = "timeout";
            scr.result_desc = "Verification of this script was aborted after " +
                std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-t0).count())+"ms.";
            if(cancellation_source->IsCanceled())
            {
                scr.result_desc += " Total script verification time ("+std::to_string(totalTimeout.count())+"ms) exceeded.";
            }
            failed = true;
            continue;
        }

        if(!*res)
        {
            scr.result = "error";
            scr.result_desc = ScriptErrorString(script_check.GetScriptError());
            failed = true;
            continue;
        }

        scr.result = "ok";
    }

    UniValue result_json{UniValue::VARR};
    for(auto& scr: scripts)
    {
        UniValue res_json{UniValue::VOBJ};
        res_json.push_back(Pair("result", scr.result));
        if(!scr.result_desc.empty())
        {
            res_json.push_back(Pair("description", scr.result_desc));
        }
        if(scr.reportflags)
        {
            res_json.push_back(Pair("flags", (int)scr.flags));
        }
        result_json.push_back(res_json);
    }
    return result_json;
}

static UniValue signmessagewithprivkey(const Config &config,
                                       const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() != 2) {
        throw std::runtime_error(
            "signmessagewithprivkey \"privkey\" \"message\"\n"
            "\nSign a message with the private key of an address\n"
            "\nArguments:\n"
            "1. \"privkey\"         (string, required) The private key to sign "
            "the message with.\n"
            "2. \"message\"         (string, required) The message to create a "
            "signature of.\n"
            "\nResult:\n"
            "\"signature\"          (string) The signature of the message "
            "encoded in base 64\n"
            "\nExamples:\n"
            "\nCreate the signature\n" +
            HelpExampleCli("signmessagewithprivkey",
                           "\"privkey\" \"my message\"") +
            "\nVerify the signature\n" +
            HelpExampleCli("verifymessage", "\"1D1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4"
                                            "XX\" \"signature\" \"my "
                                            "message\"") +
            "\nAs json rpc\n" + HelpExampleRpc("signmessagewithprivkey",
                                               "\"privkey\", \"my message\""));
    }

    std::string strPrivkey = request.params[0].get_str();
    std::string strMessage = request.params[1].get_str();

    CBitcoinSecret vchSecret;
    bool fGood = vchSecret.SetString(strPrivkey);
    if (!fGood) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid private key");
    }
    CKey key = vchSecret.GetKey();
    if (!key.IsValid()) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,
                           "Private key outside allowed range");
    }

    CHashWriter ss(SER_GETHASH, 0);
    ss << strMessageMagic;
    ss << strMessage;

    std::vector<uint8_t> vchSig;
    if (!key.SignCompact(ss.GetHash(), vchSig)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Sign failed");
    }

    return EncodeBase64(&vchSig[0], vchSig.size());
}

static UniValue clearinvalidtransactions(const Config &config,
                                         const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() != 0) {
        throw std::runtime_error(
            "clearinvalidtransactions\n\n"
            "Deletes stored invalid transactions.\n"
            "Result: number of bytes freed.");
    }
    return g_connman->getInvalidTxnPublisher().ClearStored();
}

static UniValue setmocktime(const Config &config,
                            const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() != 1) {
        throw std::runtime_error(
            "setmocktime timestamp\n"
            "\nSet the local time to given timestamp (-regtest only)\n"
            "\nArguments:\n"
            "1. timestamp  (integer, required) Unix seconds-since-epoch "
            "timestamp\n"
            "   Pass 0 to go back to using the system time.");
    }

    if (!config.GetChainParams().MineBlocksOnDemand()) {
        throw std::runtime_error(
            "setmocktime for regression testing (-regtest mode) only");
    }

    // For now, don't change mocktime if we're in the middle of validation, as
    // this could have an effect on mempool time-based eviction, as well as
    // IsInitialBlockDownload().
    // TODO: figure out the right way to synchronize around mocktime, and
    // ensure all callsites of GetTime() are accessing this safely.
    LOCK(cs_main);

    RPCTypeCheck(request.params, {UniValue::VNUM});
    SetMockTime(request.params[0].get_int64());

    return NullUniValue;
}

static UniValue RPCLockedMemoryInfo() {
    LockedPool::Stats stats = LockedPoolManager::Instance().stats();
    UniValue obj(UniValue::VOBJ);
    obj.push_back(Pair("used", uint64_t(stats.used)));
    obj.push_back(Pair("free", uint64_t(stats.free)));
    obj.push_back(Pair("total", uint64_t(stats.total)));
    obj.push_back(Pair("locked", uint64_t(stats.locked)));
    obj.push_back(Pair("chunks_used", uint64_t(stats.chunks_used)));
    obj.push_back(Pair("chunks_free", uint64_t(stats.chunks_free)));
    return obj;
}

static UniValue TouchedPagesInfo() {
    UniValue obj(UniValue::VOBJ);
    double percents = 0.0;
#ifndef WIN32
    VMTouch vm;
    try {
        auto path = GetDataDir() / "chainstate";
        std::string result = boost::filesystem::canonical(path).string();
        percents = vm.vmtouch_check(result);
    }   catch(const std::runtime_error& ex) {
        LogPrintf("Error while preloading chain state: %s\n", ex.what());
    }
#endif
    obj.push_back(Pair("chainStateCached", percents));
    return obj;
}

static UniValue getmemoryinfo(const Config &config,
                              const JSONRPCRequest &request) {
    /* Please, avoid using the word "pool" here in the RPC interface or help,
     * as users will undoubtedly confuse it with the other "memory pool"
     */
    if (request.fHelp || request.params.size() != 0) {
        throw std::runtime_error(
            "getmemoryinfo\n"
            "Returns an object containing information about memory usage.\n"
            "\nResult:\n"
            "{\n"
            "  \"locked\": {               (json object) Information about "
            "locked memory manager\n"
            "    \"used\": xxxxx,          (numeric) Number of bytes used\n"
            "    \"free\": xxxxx,          (numeric) Number of bytes available "
            "in current arenas\n"
            "    \"total\": xxxxxxx,       (numeric) Total number of bytes "
            "managed\n"
            "    \"locked\": xxxxxx,       (numeric) Amount of bytes that "
            "succeeded locking. If this number is smaller than total, locking "
            "pages failed at some point and key data could be swapped to "
            "disk.\n"
            "    \"chunks_used\": xxxxx,   (numeric) Number allocated chunks\n"
            "    \"chunks_free\": xxxxx,   (numeric) Number unused chunks\n"
            "  }\n"
            "}\n"
            "\nExamples:\n" +
            HelpExampleCli("getmemoryinfo", "") +
            HelpExampleRpc("getmemoryinfo", ""));
    }

    UniValue obj(UniValue::VOBJ);
    obj.push_back(Pair("locked", RPCLockedMemoryInfo()));
    obj.push_back(Pair("preloading", TouchedPagesInfo()));
    return obj;
}

static UniValue echo(const Config &config, const JSONRPCRequest &request) {
    if (request.fHelp) {
        throw std::runtime_error(
            "echo|echojson \"message\" ...\n"
            "\nSimply echo back the input arguments. This command is for "
            "testing.\n"
            "\nThe difference between echo and echojson is that echojson has "
            "argument conversion enabled in the client-side table in"
            "bitcoin-cli. There is no server-side difference.");
    }

    return request.params;
}

static UniValue activezmqnotifications(const Config &config, const JSONRPCRequest &request)
{
    if (request.fHelp || request.params.size() != 0)
    {
        throw std::runtime_error(
            "activezmqnotifications\n"
            "Get the active zmq notifications and their addresses\n"
            "\nResult:\n"
            "[ (array) active zmq notifications\n"
            "    {\n"
            "       \"notification\": \"xxxx\", (string) name of zmq notification\n"
            "       \"address\": \"xxxx\"       (string) address of zmq notification\n"
            "    }, ...\n"
            "]\n"
            "\nExamples:\n" +
            HelpExampleCli("activezmqnotifications", "") +
            HelpExampleRpc("activezmqnotifications", ""));
    }

    UniValue obj(UniValue::VARR);
#if ENABLE_ZMQ
    LOCK(cs_zmqNotificationInterface);
    if (pzmqNotificationInterface)
    {
        std::vector<ActiveZMQNotifier> arrNotifiers = pzmqNotificationInterface->ActiveZMQNotifiers();
        for (auto& n : arrNotifiers)
        {
            UniValue notifierData(UniValue::VOBJ);
            notifierData.push_back(Pair("notification", n.notifierName));
            notifierData.push_back(Pair("address", n.notifierAddress));
            obj.push_back(notifierData);

        }
    }
#endif
    return obj;
}

static UniValue getsettings(const Config &config, const JSONRPCRequest &request)
{

    if (request.fHelp || request.params.size() != 0)
    {
        throw std::runtime_error(
            "getsettings\n"
            "Returns node policy and consensus settings that are used when constructing"
            " a block or transaction.\n"
            "\nResult:\n"
            "{\n"
            "  \"excessiveblocksize\": xxxxx,            (numeric) The maximum block size "
            "in bytes we will accept from any source\n"
            "  \"blockmaxsize\": xxxxx,                  (numeric) The maximum block size "
            "in bytes we will mine\n"
            "  \"maxtxsizepolicy\": xxxxx,               (numeric) The maximum transaction "
            "size in bytes we relay and mine\n"
            "  \"datacarriersize\": xxxxx,               (numeric) The maximum size in bytes "
            "we consider acceptable for data carrier outputs.\n"

            "  \"maxscriptsizepolicy\": xxxxx,           (numeric) The maximum script size "
            "in bytes we're willing to relay/mine per script\n"
            "  \"maxopsperscriptpolicy\": xxxxx,         (numeric) The maximum number of "
            "non-push operations we're willing to relay/mine per script\n"
            "  \"maxscriptnumlengthpolicy\": xxxxx,      (numeric) The maximum allowed number "
            "length in bytes we're willing to relay/mine in scripts\n"
            "  \"maxpubkeyspermultisigpolicy\": xxxxx,   (numeric) The maximum allowed number "
            "of public keys we're willing to relay/mine in a single CHECK_MULTISIG(VERIFY) operation\n"
            "  \"maxtxsigopscountspolicy\": xxxxx,       (numeric) The maximum allowed number "
            "of signature operations we're willing to relay/mine in a single transaction\n"
            "  \"maxstackmemoryusagepolicy\": xxxxx,     (numeric) The maximum stack memory "
            "usage in bytes used for script verification we're willing to relay/mine in a single transaction\n"
            "  \"maxstackmemoryusageconsensus\": xxxxx,  (numeric) The maximum stack memory usage in bytes "
            "used for script verification we're willing to accept from any source\n"

            "  \"maxorphantxsize\": xxxxx,               (numeric) The maximum size in bytes of "
            "unconnectable transactions in memory\n"

            "  \"limitancestorcount\": xxxxx,            (numeric) Do not accept transactions "
            "if number of in-mempool ancestors is <n> or more.\n"
            "  \"limitcpfpgroupmemberscount\": xxxxx,    (numeric) Do not accept transactions "
            "if number of in-mempool low paying ancestors is <n> or more.\n"

            "  \"maxmempool\": xxxxx,                    (numeric) Keep the resident size of "
            "the transaction memory pool below <n> megabytes.\n"
            "  \"maxmempoolsizedisk\": xxxxx,            (numeric) Additional amount of mempool "
            "transactions to keep stored on disk below <n> megabytes.\n"
            "  \"mempoolmaxpercentcpfp\": xxxxx,         (numeric) Percentage of total mempool "
            "size (ram+disk) to allow for low paying transactions (0..100).\n"

            "  \"acceptnonstdoutputs\": xxxx,            (boolean) Relay and mine transactions "
            "that create or consume non-standard output\n"
            "  \"datacarrier\": xxxx,                    (boolean) Relay and mine data carrier transactions\n"
            "  \"minminingtxfee\": xxxxx,                 (numeric) Lowest fee rate (in BSV/kB) for "
            "transactions to be included in block creation\n"
            "  \"maxstdtxvalidationduration\": xxxxx,    (numeric) Time before terminating validation "
            "of standard transaction in milliseconds\n"
            "  \"maxnonstdtxvalidationduration\": xxxxx, (numeric) Time before terminating validation "
            "of non-standard transaction in milliseconds\n"

            "  \"maxtxchainvalidationbudget\": xxxxx,    (numeric) Additional validation time that can be carried over "
            "from previous transactions in the chain in milliseconds\n"
            "  \"validationclockcpu\": xxxxx,            (boolean) Prefer CPU time over wall time for validation.\n"

            "  \"minconsolidationfactor\": xxxxx         (numeric) Minimum ratio between scriptPubKey inputs and outputs, "
            "0 disables consolidation transactions\n"
            "  \"maxconsolidationinputscriptsize\": xxxx (numeric) Maximum scriptSig length of input in bytes\n"
            "  \"minconfconsolidationinput\": xxxxx      (numeric) Minimum number of confirmations for inputs spent\n"
            "  \"minconsolidationinputmaturity\": xxxxx  (numeric) Minimum number of confirmations for inputs spent "
            "(DEPRECATED: use minconfconsolidationinput instead)\n"
            "  \"acceptnonstdconsolidationinput\": xxxx  (boolean) Accept consolidation transactions that use non "
            "standard inputs\n"
            "}\n"
            "\nExamples:\n" +
            HelpExampleCli("getsettings", "") +
            HelpExampleRpc("getsettings", ""));
    }

    UniValue obj(UniValue::VOBJ);

    obj.push_back(Pair("excessiveblocksize", config.GetMaxBlockSize()));
    obj.push_back(Pair("blockmaxsize", config.GetMaxGeneratedBlockSize()));
    obj.push_back(Pair("maxtxsizepolicy", config.GetMaxTxSize(true, false)));
    obj.push_back(Pair("maxorphantxsize", config.GetMaxOrphanTxSize()));
    obj.push_back(Pair("datacarriersize", config.GetDataCarrierSize()));

    obj.push_back(Pair("maxscriptsizepolicy", config.GetMaxScriptSize(true, false)));
    obj.push_back(Pair("maxopsperscriptpolicy", config.GetMaxOpsPerScript(true, false)));
    obj.push_back(Pair("maxscriptnumlengthpolicy", config.GetMaxScriptNumLength(true, false)));
    obj.push_back(Pair("maxpubkeyspermultisigpolicy", config.GetMaxPubKeysPerMultiSig(true, false)));
    obj.push_back(Pair("maxtxsigopscountspolicy", config.GetMaxTxSigOpsCountPolicy(true)));
    obj.push_back(Pair("maxstackmemoryusagepolicy", config.GetMaxStackMemoryUsage(true, false)));
    obj.push_back(Pair("maxstackmemoryusageconsensus", config.GetMaxStackMemoryUsage(true, true)));

    obj.push_back(Pair("limitancestorcount", config.GetLimitAncestorCount()));
    obj.push_back(Pair("limitcpfpgroupmemberscount", config.GetLimitSecondaryMempoolAncestorCount()));

    obj.push_back(Pair("maxmempool", config.GetMaxMempool()));
    obj.push_back(Pair("maxmempoolsizedisk", config.GetMaxMempoolSizeDisk()));
    obj.push_back(Pair("mempoolmaxpercentcpfp", config.GetMempoolMaxPercentCPFP()));

    obj.push_back(Pair("acceptnonstdoutputs", config.GetAcceptNonStandardOutput(true)));
    obj.push_back(Pair("datacarrier", config.GetDataCarrier()));
    obj.push_back(Pair("minminingtxfee", ValueFromAmount(mempool.GetBlockMinTxFee().GetFeePerK())));
    obj.push_back(Pair("maxstdtxvalidationduration", config.GetMaxStdTxnValidationDuration().count()));
    obj.push_back(Pair("maxnonstdtxvalidationduration", config.GetMaxNonStdTxnValidationDuration().count()));

    obj.push_back(Pair("maxtxchainvalidationbudget", config.GetMaxTxnChainValidationBudget().count()));
    obj.push_back(Pair("validationclockcpu", config.GetValidationClockCPU()));


    obj.push_back(Pair("minconsolidationfactor",  config.GetMinConsolidationFactor()));
    obj.push_back(Pair("maxconsolidationinputscriptsize",  config.GetMaxConsolidationInputScriptSize()));
    obj.push_back(Pair("minconfconsolidationinput",  config.GetMinConfConsolidationInput()));
    obj.push_back(Pair("minconsolidationinputmaturity",  config.GetMinConfConsolidationInput()));
    obj.push_back(Pair("acceptnonstdconsolidationinput",  config.GetAcceptNonStdConsolidationInput()));

    return obj;
}

static UniValue dumpparameters(const Config &config, const JSONRPCRequest &request)
{

    if (request.fHelp || request.params.size() != 0)
    {
        throw std::runtime_error(
            "dumpparameters\n"
            "Dumps non-sensitive force set parameters and parameters set by switches and config file.\n"
            "Note: rpcuser, rpcpassword and rpcauth are excluded from the dump.\n"
            "\nResult:\n"
            "[ (array) parameters\n"
            "    parametername=value,\n"
            "    ...,\n"
            "]\n"
            "\nExamples:\n" +
            HelpExampleCli("dumpparameters", "") +
            HelpExampleRpc("dumpparameters", ""));
    }

    UniValue obj(UniValue::VARR);

    for(const auto& arg : gArgs.GetNonSensitiveParameters())
    {
        obj.push_back(arg);
    }

    return obj;
}
namespace 
{
    const std::map<std::string, uint32_t> mapFlagNames = {
        {"NONE", SCRIPT_VERIFY_NONE},
        {"P2SH", SCRIPT_VERIFY_P2SH},
        {"STRICTENC", SCRIPT_VERIFY_STRICTENC},
        {"DERSIG", SCRIPT_VERIFY_DERSIG},
        {"LOW_S", SCRIPT_VERIFY_LOW_S},
        {"SIGPUSHONLY", SCRIPT_VERIFY_SIGPUSHONLY},
        {"MINIMALDATA", SCRIPT_VERIFY_MINIMALDATA},
        {"NULLDUMMY", SCRIPT_VERIFY_NULLDUMMY},
        {"DISCOURAGE_UPGRADABLE_NOPS", SCRIPT_VERIFY_DISCOURAGE_UPGRADABLE_NOPS},
        {"CLEANSTACK", SCRIPT_VERIFY_CLEANSTACK},
        {"MINIMALIF", SCRIPT_VERIFY_MINIMALIF},
        {"NULLFAIL", SCRIPT_VERIFY_NULLFAIL},
        {"CHECKLOCKTIMEVERIFY", SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY},
        {"CHECKSEQUENCEVERIFY", SCRIPT_VERIFY_CHECKSEQUENCEVERIFY},
        {"COMPRESSED_PUBKEYTYPE", SCRIPT_VERIFY_COMPRESSED_PUBKEYTYPE},
        {"SIGHASH_FORKID", SCRIPT_ENABLE_SIGHASH_FORKID},
        {"GENESIS", SCRIPT_GENESIS},
        {"UTXO_AFTER_GENESIS", SCRIPT_UTXO_AFTER_GENESIS},
    };
}

std::optional<uint32_t> GetFlagNumber(const std::string& flagName, std::string& err)
{
    std::optional<uint32_t> flagNumber;
    auto findFlagIterator = mapFlagNames.find(flagName);
    if (findFlagIterator == mapFlagNames.end())
    {
        err = "Provided flag (" + flagName + ") is unknown.";
    }
    else {
        flagNumber = findFlagIterator->second;
    }
    return flagNumber;
}

// clang-format off
static const CRPCCommand commands[] = {
    //  category            name                      actor (function)        okSafeMode
    //  ------------------- ------------------------  ----------------------  ----------
    { "control",            "getinfo",                getinfo,                true,  {} }, /* uses wallet if enabled */
    { "control",            "getmemoryinfo",          getmemoryinfo,          true,  {} },
    { "control",            "dumpparameters",         dumpparameters,         true,  {} },
    { "control",            "getsettings",            getsettings,            true,  {} },
    { "control",            "activezmqnotifications", activezmqnotifications, true,  {} },
    { "util",               "validateaddress",        validateaddress,        true,  {"address"} }, /* uses wallet if enabled */
    { "util",               "createmultisig",         createmultisig,         true,  {"nrequired","keys"} },
    { "util",               "verifymessage",          verifymessage,          true,  {"address","signature","message"} },
    { "util",               "verifyscript",           verifyscript,           true,  {"scripts", "stopOnFirstInvalid", "totalTimeout"} },
    { "util",               "signmessagewithprivkey", signmessagewithprivkey, true,  {"privkey","message"} },

    { "util",               "clearinvalidtransactions",clearinvalidtransactions, true,  {} },

    /* Not shown in help */
    { "hidden",             "setmocktime",            setmocktime,            true,  {"timestamp"}},
    { "hidden",             "echo",                   echo,                   true,  {"arg0","arg1","arg2","arg3","arg4","arg5","arg6","arg7","arg8","arg9"}},
    { "hidden",             "echojson",               echo,                   true,  {"arg0","arg1","arg2","arg3","arg4","arg5","arg6","arg7","arg8","arg9"}},
};
// clang-format on

void RegisterMiscRPCCommands(CRPCTable &t) {
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++) {
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
    }
}
