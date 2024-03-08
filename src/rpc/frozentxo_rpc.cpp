// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "rpc/server.h"
#include "univalue.h"
#include "frozentxo_db.h"
#include "validation.h"
#include "mining/journal_builder.h"
#include "http_protocol.h"

#include <vector>




namespace {

/*
internal DTO structure for RPC frozen txo implementation
*/
struct FrozenFund
{
    COutPoint frozenTXO;
    CFrozenTXODB::FrozenTXOData::EnforceAtHeightType enforceAtHeight;
    bool policyExpiresWithConsensus=false;
    std::string reason;
};

void setVarFromUniValue_Helper(CFrozenTXODB::FrozenTXOData::EnforceAtHeightType& var, const UniValue& uv)
{
    const auto& arr = uv.get_array();
    var.clear();
    for (const UniValue& i : arr.getValues())
    {
        RPCTypeCheckObj(
            i,
            {
                {"start",       UniValueType(UniValue::VNUM )},
                {"stop",        UniValueType(UniValue::VNUM )},
            }, true, true);
        if(!i.exists("start"))
        {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Missing start");
        }

        if(i.exists("stop"))
        {
            var.push_back({i["start"].get_int(), i["stop"].get_int()});
        }
        else
        {
            var.push_back({i["start"].get_int()});
        }
    }
}
void setVarFromUniValue_Helper(bool& var, const UniValue& uv) { var = uv.get_bool(); }

template<class TVar>
void setVar(const UniValue& fund, const std::string& name, TVar& var, bool cond)
{
    if (fund.exists(name))
    {
        if (!cond)
        {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Misused "+name);
        }

        setVarFromUniValue_Helper(var, fund[name]);
    }
    else
    {
        if (cond)
        {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Missing "+ name);
        }
        // var is intentionally not set here because it is assumed to already contain the default value
    }
}

std::vector<FrozenFund> ParseFundsFromRequest(const JSONRPCRequest& request, bool isConsensusBlacklist)
{
    RPCTypeCheck(request.params, { UniValue::VOBJ });

    const UniValue& params = request.params[0];

    RPCTypeCheckObj(
        params,
        {
            {"funds", UniValueType(UniValue::VARR)}
        }, false, true);

    const UniValue& funds = params["funds"].get_array();

    std::vector<FrozenFund> result;

    for (const UniValue& fund : funds.getValues())
    {
        FrozenFund ff;

        RPCTypeCheckObj(
            fund,
            {
                {"txOut",                      UniValueType(UniValue::VOBJ )},
                {"enforceAtHeight",            UniValueType(UniValue::VARR )},
                {"policyExpiresWithConsensus", UniValueType(UniValue::VBOOL)}
            }, true, true);

        if (!fund.exists("txOut"))
        {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Missing txOut");
        }
        const UniValue& txOut = fund["txOut"].get_obj();

        RPCTypeCheckObj(
            txOut,
            {
                {"txId", UniValueType(UniValue::VSTR)},
                {"vout", UniValueType(UniValue::VNUM)}
            }, false, true);

        const std::string& txId = txOut["txId"].getValStr();
        const uint32_t vout = [&]{
            auto vout_tmp = txOut["vout"].get_int64();
            if(vout_tmp<0)
            {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Negative vout");
            };
            if(vout_tmp>std::numeric_limits<uint32_t>::max())
            {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Too large vout");
            };
            return vout_tmp;
        }();
        ff.frozenTXO = COutPoint(uint256S(txId), vout);

        setVar(fund, "enforceAtHeight",            ff.enforceAtHeight,            isConsensusBlacklist);
        setVar(fund, "policyExpiresWithConsensus", ff.policyExpiresWithConsensus, isConsensusBlacklist);

        result.push_back(ff);
    }

    return result;
}

bool FundImported(CFrozenTXODB::FreezeTXOResult freezeResult, bool isConsensusBlackList, std::string& reason)
{
    if(freezeResult == CFrozenTXODB::FreezeTXOResult::OK)
    {
        return true;
    }

    if (isConsensusBlackList)
    {
        if (freezeResult == CFrozenTXODB::FreezeTXOResult::OK_UPDATED_TO_CONSENSUS_BLACKLIST)
        {
            reason = "updated from policy to consensus";
            return true;
        }
        if (freezeResult == CFrozenTXODB::FreezeTXOResult::OK_UPDATED)
        {
            reason = "updated consensus enforcement parameters";
            return true;
        }
        if (freezeResult == CFrozenTXODB::FreezeTXOResult::OK_ALREADY_FROZEN)
        {
            reason = "already in consensus";
            return false;
        }
    }
    else
    { 
        if (freezeResult == CFrozenTXODB::FreezeTXOResult::OK_ALREADY_FROZEN)
        {
            reason = "already in policy";
            return false;
        }
        if (freezeResult == CFrozenTXODB::FreezeTXOResult::ERROR_ALREADY_IN_CONSENSUS_BLACKLIST)
        {
            reason = "already in consensus";
            return false;
        }
    }

    reason = "unknown reason";
    return false;
}

std::vector<FrozenFund> ImportFundsToDB(const std::vector<FrozenFund>& funds, bool isConsensusBlacklist)
{
    auto& db = CFrozenTXODB::Instance();
    std::vector<FrozenFund> notImportedFunds;
    for (const auto &fund : funds)
    {
        auto freezeResult = isConsensusBlacklist ? db.FreezeTXOConsensus(fund.frozenTXO, fund.enforceAtHeight, fund.policyExpiresWithConsensus)
            : db.FreezeTXOPolicyOnly(fund.frozenTXO);
        std::string reason;
        if (!FundImported(freezeResult, isConsensusBlacklist, reason))
        {
            auto ff = fund;
            ff.reason = reason;
            notImportedFunds.push_back(ff);
        }
    }
    db.Sync();
    return notImportedFunds;
}

bool FundRemoved(CFrozenTXODB::UnfreezeTXOResult unfreezeResult, std::string& reason)
{
    if(unfreezeResult == CFrozenTXODB::UnfreezeTXOResult::OK)
    {
        return true;
    }

    if (unfreezeResult == CFrozenTXODB::UnfreezeTXOResult::ERROR_TXO_NOT_FROZEN)
    {
        reason = "not found";
        return false;
    }

    if (unfreezeResult == CFrozenTXODB::UnfreezeTXOResult::ERROR_TXO_IS_IN_CONSENSUS_BLACKLIST)
    {
        reason = "in consensus";
        return false;
    }

    reason = "unknown reason";
    return false;
}

std::vector<FrozenFund> RemovePolicyFundsFromDB(const std::vector<FrozenFund>& funds)
{
    auto& db = CFrozenTXODB::Instance();
    std::vector<FrozenFund> notRemovedFunds;
    for (const auto& fund : funds)
    {
        auto unfreezeResult = db.UnfreezeTXOPolicyOnly(fund.frozenTXO);
        std::string reason;
        if (!FundRemoved(unfreezeResult, reason))
        {
            auto ff = fund;
            ff.reason = reason;
            notRemovedFunds.push_back(ff);
        }
    }
    db.Sync();
    return notRemovedFunds;
}

void RemoveFundsFromQueues()
{
    // cs_main lock prevents transaction validators from running in parallel
    // to this task as otherwise it might happen that:
    // - transaction passes the frozen input UTXO check
    // - frozen transaction children are removed from mempool by this code
    //   and keeps the mempool lock locked
    // - validator waits for mempool lock
    // - this code completes
    // - validator adds the child of a frozen parent to the mempool
    LOCK(cs_main);

    mining::CJournalChangeSetPtr changeSet {
        mempool.getJournalBuilder().getNewChangeSet(
            mining::JournalUpdateReason::REMOVE_TXN) };

    mempool.RemoveFrozen(changeSet);
}

void RemoveInvalidCTXsFromMempool()
{
    // cs_main lock is needed for the same reasons as in RemoveFundsFromQueues()
    LOCK(cs_main);

    mining::CJournalChangeSetPtr changeSet {
        mempool.getJournalBuilder().getNewChangeSet(
            mining::JournalUpdateReason::REMOVE_TXN) };

    mempool.RemoveInvalidCTXs(changeSet);
}

UniValue PrepareResult(std::vector<FrozenFund>& notProcessedFunds)
{
    UniValue result(UniValue::VOBJ);
    UniValue notProcessed(UniValue::VARR);

    for (const auto& fundObj : notProcessedFunds)
    {
        UniValue fund(UniValue::VOBJ);
        UniValue txOut(UniValue::VOBJ);

        txOut.pushKV("txId", fundObj.frozenTXO.GetTxId().ToString());
        txOut.pushKV("vout", static_cast<uint64_t>(fundObj.frozenTXO.GetN()));
        fund.pushKV("txOut", txOut);
        fund.pushKV("reason", fundObj.reason);

        notProcessed.push_back(fund);
    }
    result.pushKV("notProcessed", notProcessed);

    return result;
}

std::string GenerateHelpStringForFunds(const std::string& fund_str, const std::string& additional_members_help)
{
    return R"({
  ")"+fund_str+R"(": [
    {
      "txOut": {
        "txId": <hex string>,
        "vout": <integer>
      })" + additional_members_help + R"(
    }
  ]
})";
}




UniValue addToPolicyBlacklist(const Config& config, const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
    {
        throw std::runtime_error( R"(addToPolicyBlacklist (funds)

Adds transaction outputs to policy-only blacklist.

Arguments: )" + GenerateHelpStringForFunds("funds", "") + R"(

Result: )" + GenerateHelpStringForFunds("notProcessed", R"(,
      "reason": <string>)") + R"(

Examples:
)"
        + HelpExampleCli("addToPolicyBlacklist", R"('{"funds":[{"txOut":{"txId":"<hex string>", "vout":<integer>}}]}')")
        + HelpExampleRpc("addToPolicyBlacklist", R"({"funds":[{"txOut":{"txId":"<hex string>", "vout":<integer>}}]})") );
    }

    std::vector<FrozenFund> funds = ParseFundsFromRequest(request, false);
    std::vector<FrozenFund> notImportedFunds = ImportFundsToDB(funds, false);
    RemoveFundsFromQueues();

    return PrepareResult(notImportedFunds);
}

UniValue addToConsensusBlacklist(const Config& config, const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
    {
        throw std::runtime_error( R"(addToConsensusBlacklist (funds)

Adds or updates transaction outputs on consensus blacklist.

Block heights at which the transaction output is considered consensus frozen are specified by half-open intervals [start, stop) in array 'enforceAtHeight'.
Option 'policyExpiresWithConsensus' specifies whether the transaction output is still considered policy frozen at heights after the highest interval in 'enforceAtHeight' (true = no longer considered policy frozen, false = still considered policy frozen).

Arguments: )" + GenerateHelpStringForFunds("funds", R"(,
      "enforceAtHeight": [{
        "start": <integer>,
        "stop": <integer>
      }],
      "policyExpiresWithConsensus": <boolean>)") + R"(

Result: )" + GenerateHelpStringForFunds("notProcessed", R"(,
      "reason": <string>)") + R"(

Examples:
)"
        + HelpExampleCli("addToConsensusBlacklist", R"('{"funds":[{"txOut":{"txId":"<hex string>", "vout":<integer>}, "enforceAtHeight":[{"start":<integer>, "stop":<integer>}], "policyExpiresWithConsensus":false}]}')")
        + HelpExampleRpc("addToConsensusBlacklist", R"({"funds":[{"txOut":{"txId":"<hex string>", "vout":<integer>}, "enforceAtHeight":[{"start":<integer>, "stop":<integer>}], "policyExpiresWithConsensus":false}]})") );
    }

    std::vector<FrozenFund> funds = ParseFundsFromRequest(request, true);
    std::vector<FrozenFund> notImportedFundsConsensus = ImportFundsToDB(funds, true);
    RemoveFundsFromQueues();
   
    return PrepareResult(notImportedFundsConsensus);
}

UniValue removeFromPolicyBlacklist(const Config& config, const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
    {
        throw std::runtime_error( R"(removeFromPolicyBlacklist (funds)

Removes transaction outputs from policy blacklist.

Arguments: )" + GenerateHelpStringForFunds("funds", "") + R"(

Result: )" + GenerateHelpStringForFunds("notProcessed", R"(,
      "reason": <string>)") + R"(

Examples:
)"
        + HelpExampleCli("removeFromPolicyBlacklist", R"('{"funds":[{"txOut":{"txId":"<hex string>", "vout":<integer>}}]}')")
        + HelpExampleRpc("removeFromPolicyBlacklist", R"({"funds":[{"txOut":{"txId":"<hex string>", "vout":<integer>}}]})") );
    }

    std::vector<FrozenFund> funds = ParseFundsFromRequest(request, false);
    std::vector<FrozenFund> notRemovedFunds = RemovePolicyFundsFromDB(funds);
    return PrepareResult(notRemovedFunds);
}

void queryBlacklist(const Config& config, const JSONRPCRequest& jsonRPCReq, HTTPRequest* httpReq, bool processedInBatch)
{
    if(jsonRPCReq.fHelp || jsonRPCReq.params.size()>0)
    {
        throw std::runtime_error( R"(queryBlacklist

Returns an array of currently frozen transaction outputs and blacklist membership.

Arguments: None

Result: )" + GenerateHelpStringForFunds("funds", R"(,
      "enforceAtHeight": [{
        "start": <integer>,
        "stop": <integer>
      }],
      "policyExpiresWithConsensus": <boolean>,
      "blacklist": [ <string> ])") + R"(

Examples:
)"
        + HelpExampleCli("queryBlacklist", "")
        + HelpExampleRpc("queryBlacklist", "") );
    }

    if(httpReq == nullptr)
        return;

    if (!processedInBatch) {
        httpReq->WriteHeader("Content-Type", "application/json");
        httpReq->StartWritingChunks(HTTP_OK);
    }

    // Since there may be many frozen TXOs, reply is streamed to client.

    // First send the reply header
    httpReq->WriteReplyChunk("{\"result\": {\"funds\": [");

    auto& db = CFrozenTXODB::Instance();

    // Query all frozen TXOs
    bool first=true;
    for(auto it=db.QueryAllFrozenTXOs(); it.Valid(); it.Next())
    {
        auto txo = it.GetFrozenTXO();

        UniValue txOut(UniValue::VOBJ);
        txOut.pushKV("txId", txo.first.GetTxId().ToString());
        txOut.pushKV("vout", static_cast<std::uint64_t>(txo.first.GetN()));

        UniValue blacklist(UniValue::VARR);
        if(txo.second.blacklist == CFrozenTXODB::FrozenTXOData::Blacklist::PolicyOnly)
        {
            blacklist.push_back("policy");
        }
        else if(txo.second.blacklist == CFrozenTXODB::FrozenTXOData::Blacklist::Consensus)
        {
            blacklist.push_back("policy");
            blacklist.push_back("consensus");
        }

        UniValue fund(UniValue::VOBJ);
        fund.pushKV("txOut", txOut);
        if(txo.second.blacklist == CFrozenTXODB::FrozenTXOData::Blacklist::Consensus)
        {
            UniValue enforceAtHeight(UniValue::VARR);
            for(auto& i: txo.second.enforceAtHeight)
            {
                UniValue interval(UniValue::VOBJ);
                interval.pushKV("start", i.start);
                interval.pushKV("stop", i.stop);
                enforceAtHeight.push_back(interval);
            }
            fund.pushKV("enforceAtHeight", enforceAtHeight);
            fund.pushKV("policyExpiresWithConsensus", txo.second.policyExpiresWithConsensus);
        }
        fund.pushKV("blacklist", blacklist);

        // Send frozen TXO (one "txOut" element in "funds" array) to client
        if(first)
        {
            first=false;
        }
        else
        {
            httpReq->WriteReplyChunk(",");
        }
        httpReq->WriteReplyChunk(fund.write());
    }

    // Send the reply footer
    httpReq->WriteReplyChunk("]}, \"error\": " + NullUniValue.write() + ", \"id\": " + jsonRPCReq.id.write() + "}");

    if (!processedInBatch) {
        httpReq->StopWritingChunks();
    }
}

UniValue clearBlacklists(const Config& config, const JSONRPCRequest& jsonRPCReq)
{
    if(jsonRPCReq.fHelp || jsonRPCReq.params.size()!=1)
    {
        throw std::runtime_error( R"(clearBlacklists (removeAllEntries)

Clears all blacklists and returns number of entries for frozen transaction outputs that were removed from database.

If removeAllEntries=true and keepExistingPolicyEntries=false, all entries are unconditionally removed. This includes both frozen transaction outputs and whitelisted confiscation transactions.
If removeAllEntries=true and keepExistingPolicyEntries=true, all consensus frozen TXO entries and whitelisted confiscation transactions are unconditionally removed. PolicyOnly frozen entries are not affected.
If removeAllEntries=false, only expired consensus entries are either removed or updated to policy-only, depending on their value of policyExpiresWithConsensus. Expired entries are consensus blacklist entries with stopEnforceAtHeight that is at least expirationHeightDelta blocks smaller than current block height. Whitelisted confiscation transactions and confiscated TXOs are not affected.

Arguments: {
  removeAllEntries: <boolean>,
  keepExistingPolicyEntries: <boolean>,  # optional (default=false), only allowed if removeAllEntries=true
  expirationHeightDelta: <integer>
}

Result: {
  numRemovedEntries: <integer>
}

Examples:
)"
        + HelpExampleCli("clearBlacklists", R"('{"removeAllEntries":true, "keepExistingPolicyEntries":false}')")
        + HelpExampleCli("clearBlacklists", R"('{"removeAllEntries":false, "expirationHeightDelta":1000}')")
        + HelpExampleRpc("clearBlacklists", R"({"removeAllEntries":true, "keepExistingPolicyEntries":false})")
        + HelpExampleRpc("clearBlacklists", R"({"removeAllEntries":false, "expirationHeightDelta":1000})") );
    }

    // Parse request parameters
    RPCTypeCheck(jsonRPCReq.params, { UniValue::VOBJ });
    const UniValue& params = jsonRPCReq.params[0];
    RPCTypeCheckObj(params, {
            {"removeAllEntries",          UniValueType(UniValue::VBOOL)},
            {"keepExistingPolicyEntries", UniValueType(UniValue::VBOOL)},
            {"expirationHeightDelta",     UniValueType(UniValue::VNUM )}
        }, true, true);
    if (!params.exists("removeAllEntries"))
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Missing removeAllEntries");
    }
    const bool removeAllEntries = params["removeAllEntries"].get_bool();
    const std::int32_t expirationHeightDelta = [&]{
        if (!params.exists("expirationHeightDelta"))
        {
            if(removeAllEntries)
            {
                return 0;
            }
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Missing expirationHeightDelta");
        }

        if(removeAllEntries)
        {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Misused expirationHeightDelta");
        }

        std::int32_t ehd = params["expirationHeightDelta"].get_int();
        if(ehd<0)
        {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid value for expirationHeightDelta! Must be non-negative integer.");
        }

        return ehd;
    }();
    const bool keepExistingPolicyEntries = [&]{
        if(!params.exists("keepExistingPolicyEntries"))
        {
            return false;
        }

        if(!removeAllEntries)
        {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Misused keepExistingPolicyEntries");
        }

        return params["keepExistingPolicyEntries"].get_bool();
    }();

    auto& db = CFrozenTXODB::Instance();
    std::uint64_t numRemovedEntries = 0;
    if(removeAllEntries)
    {
        auto res = db.UnfreezeAll(keepExistingPolicyEntries);
        numRemovedEntries += res.numUnfrozenPolicyOnly;
        numRemovedEntries += res.numUnfrozenConsensus;
        numRemovedEntries += res.numUnwhitelistedTxs;
        if(res.numUnfrozenConsensus>0 || res.numUnwhitelistedTxs>0)
        {
            // If any consensus frozen TXO were removed or if any confiscation transactions were un-whitelisted,
            // mempool might contain confiscation transactions that are not valid anymore and must be removed.
            RemoveInvalidCTXsFromMempool();
        }
    }
    else
    {
        const std::int32_t expired_height = chainActive.Tip()->GetHeight() - expirationHeightDelta; // TXOs expire only after they have been considered unfrozen for given number of blocks
        auto res = db.CleanExpiredRecords(expired_height); // NOTE: method can handle negative values for height
        numRemovedEntries += res.numConsensusRemoved;
    }

    UniValue result(UniValue::VOBJ);
    result.pushKV("numRemovedEntries", numRemovedEntries);
    return result;
}

std::string GenerateHelpStringForConfiscationTxids(const std::string& txs_str, bool includeTxId, bool includeEnforceAtHeight, bool includeInputs, bool includeHex, const std::string& additional_members_help)
{
    return R"({
  ")"+txs_str+R"(": [
    {
      "confiscationTx": {)" + (includeTxId ? R"(
        "txId": <hex string>)" : "") + ((includeTxId && includeEnforceAtHeight) ? "," : "")
                                     + (includeEnforceAtHeight ? R"(
        "enforceAtHeight" : <integer>)" : "") + (includeInputs ? R"(, 
        "inputs": [
          {
            "txOut": {
              "txId": <hex string>,
              "vout": <integer>
            }
          },...
        ])" : "") + (includeHex ? R"(,
        "hex" : <tx_hex_string>)" : "") + R"(
      })" + additional_members_help + R"(
    },...
  ]
})";
}

UniValue addToConfiscationTxidWhitelist(const Config& config, const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
    {
        throw std::runtime_error( R"(addToConfiscationTxidWhitelist (txs)

Add confiscation transactions to the whitelist.

Arguments: )" + GenerateHelpStringForConfiscationTxids("confiscationTxs", false, true, false, true,  "") + R"(

Result: )" + GenerateHelpStringForConfiscationTxids("notProcessed", true, false, false, false, R"(,
      "reason": <string>)") + R"(

Examples:
)"
        + HelpExampleCli("addToConfiscationTxidWhitelist", R"('{"confiscationTxs":[{"confiscationTx":{"enforceAtHeight":<integer>, "hex":"<tx_hex string>"}}]}')")
        + HelpExampleRpc("addToConfiscationTxidWhitelist", R"({"confiscationTxs":[{"confiscationTx":{"enforceAtHeight":<integer>, "hex":"<tx_hex string>"}}]})"));
    }

    //
    // Check request argument and parse ids of whitelisted confiscation transactions
    //
    struct WLCTX
    {
        std::int32_t enforceAtHeight;
        CTransaction confiscationTx;

        WLCTX(std::int32_t enforceAtHeight, const std::string& confiscationTxHex)
        : enforceAtHeight(enforceAtHeight)
        , confiscationTx([&]{
            try
            {
                // Parse hex string of confiscation transaction
                CDataStream stream(ParseHex(confiscationTxHex), SER_NETWORK, PROTOCOL_VERSION);
                return CTransaction(deserialize, stream);
            }
            catch(...)
            {
                // Store null transaction if it cannot be parsed from hex string
                return CTransaction();
            }
          }())
        {
        }
    };
    std::vector<WLCTX> wlctxs;

    RPCTypeCheck(request.params, { UniValue::VOBJ });

    const UniValue& par = request.params[0];

    RPCTypeCheckObj(
        par,
        {
            {"confiscationTxs", UniValueType(UniValue::VARR)}
        }, false, true);

    const UniValue& confiscationTxs_json = par["confiscationTxs"].get_array();

    for (const UniValue& confiscationTxsElement_json : confiscationTxs_json.getValues())
    {
        RPCTypeCheckObj(
            confiscationTxsElement_json,
            {
                {"confiscationTx", UniValueType(UniValue::VOBJ )}
            }, false, true);

        const UniValue& confiscationTx_json = confiscationTxsElement_json["confiscationTx"].get_obj();

        RPCTypeCheckObj(
            confiscationTx_json,
            {
                {"enforceAtHeight", UniValueType(UniValue::VNUM)},
                {"hex",             UniValueType(UniValue::VSTR)}
            }, false, true);

        WLCTX wlctx(
            confiscationTx_json["enforceAtHeight"].get_int(),
            confiscationTx_json["hex"].getValStr() );
        if(wlctx.enforceAtHeight<0)
        {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Negative enforceAtHeight");
        }
        wlctxs.push_back(wlctx);
    }

    //
    // Whitelist specified confiscation transactions
    //
    auto& db = CFrozenTXODB::Instance();

    UniValue notProcessed_json(UniValue::VARR);
    for (const auto &wlctx : wlctxs)
    {
        std::string reason;
        if(wlctx.confiscationTx.IsNull())
        {
            reason = "invalid transaction hex string";
        }
        else
        {
            auto res = db.WhitelistTx(wlctx.enforceAtHeight, wlctx.confiscationTx);
            if(res==CFrozenTXODB::WhitelistTxResult::OK || res==CFrozenTXODB::WhitelistTxResult::OK_ALREADY_WHITELISTED_AT_LOWER_HEIGHT || res==CFrozenTXODB::WhitelistTxResult::OK_UPDATED)
            {
                // Confiscation transaction was successfully white listed
                continue;
            }

            if (res == CFrozenTXODB::WhitelistTxResult::ERROR_NOT_VALID)
            {
                reason = "confiscation transaction is not valid";
            }
            else if (res == CFrozenTXODB::WhitelistTxResult::ERROR_TXO_NOT_CONSENSUS_FROZEN)
            {
                reason = "confiscated TXO is not consensus frozen";
            }
        }

        UniValue npctx_json(UniValue::VOBJ);
        UniValue txid_json(UniValue::VOBJ);
        txid_json.pushKV("txId", wlctx.confiscationTx.GetId().ToString());
        npctx_json.pushKV("confiscationTx", txid_json);
        npctx_json.pushKV("reason", reason);

        notProcessed_json.push_back(npctx_json);
    }
    db.Sync();

    UniValue result_json(UniValue::VOBJ);
    result_json.pushKV("notProcessed", notProcessed_json);

    return result_json;
}

UniValue clearConfiscationWhitelist(const Config& config, const JSONRPCRequest& jsonRPCReq)
{
    if(jsonRPCReq.fHelp || jsonRPCReq.params.size()>0)
    {
        throw std::runtime_error( R"(clearConfiscationWhitelist

Remove all confiscation transactions from whitelist and move previously confiscated TXOs back to a consensus frozen state according to their consensus freeze intervals.

Arguments: None
Result:
{
  numFrozenBackToConsensus: <integer>,
  numUnwhitelistedTxs: <integer>
}

Examples:
)"
        + HelpExampleCli("clearConfiscationWhitelist", "")
        + HelpExampleRpc("clearConfiscationWhitelist", "") );
    }

    auto& db = CFrozenTXODB::Instance();

    auto res = db.ClearWhitelist();
    RemoveInvalidCTXsFromMempool();

    UniValue result(UniValue::VOBJ);
    result.pushKV("numFrozenBackToConsensus", static_cast<std::uint64_t>(res.numFrozenBackToConsensus));
    result.pushKV("numUnwhitelistedTxs", static_cast<std::uint64_t>(res.numUnwhitelistedTxs));

    return result;
}

void queryConfiscationTxidWhitelist(const Config& config, const JSONRPCRequest& jsonRPCReq, HTTPRequest* httpReq, bool processedInBatch)
{
    if(jsonRPCReq.fHelp || jsonRPCReq.params.size()>1)
    {
        throw std::runtime_error( R"(queryConfiscationTxidWhitelist (verbose)

Returns an array with ids of currently whitelisted confiscation transactions.

Arguments:
    verbose (boolean, optional, default=false):
        If True, inputs of confiscation transaction are also included in the result.
        If False, inputs field is not present in the result.

Result (for verbose = false):
)" + GenerateHelpStringForConfiscationTxids("confiscationTxs", true, true, false, false, "" ) + R"(

Result (for verbose = true):
)" + GenerateHelpStringForConfiscationTxids("confiscationTxs", true, true, true,  false, "" ) + R"(

Examples:
)"
        + HelpExampleCli("queryConfiscationTxidWhitelist", "true")
        + HelpExampleRpc("queryConfiscationTxidWhitelist", "true") );
    }

    if(httpReq == nullptr)
        return;

    const bool verbose = [&]{
        if(jsonRPCReq.params.size() > 0)
        {
            return jsonRPCReq.params[0].get_bool();
        }
        return false;
    }();

    if (!processedInBatch) {
        httpReq->WriteHeader("Content-Type", "application/json");
        httpReq->StartWritingChunks(HTTP_OK);
    }

    // Since there may be many ids, reply is streamed to client.

    // First send the reply header
    httpReq->WriteReplyChunk("{\"result\": {\"confiscationTxs\": [");

    auto& db = CFrozenTXODB::Instance();

    // Query all whitelisted confiscation transactions
    bool first=true;
    for(auto it=db.QueryAllWhitelistedTxs(); it.Valid(); it.Next())
    {
        auto wltx = it.GetWhitelistedTx();

        UniValue confiscationTx_json(UniValue::VOBJ);
        confiscationTx_json.pushKV("txId", wltx.first.ToString());
        confiscationTx_json.pushKV("enforceAtHeight", static_cast<std::int64_t>(wltx.second.enforceAtHeight));
        if(verbose)
        {
            UniValue inputs(UniValue::VARR);
            for(auto& txo: wltx.second.confiscatedTXOs)
            {
                UniValue txOut(UniValue::VOBJ);
                txOut.pushKV("txId", txo.GetTxId().ToString());
                txOut.pushKV("vout", static_cast<std::uint64_t>(txo.GetN()));

                UniValue input(UniValue::VOBJ);
                input.pushKV("txOut", txOut);

                inputs.push_back(input);
            }
            confiscationTx_json.pushKV("inputs", inputs);
        }

        UniValue confiscationTxsElement_json(UniValue::VOBJ);
        confiscationTxsElement_json.pushKV("confiscationTx", confiscationTx_json);

        // Send txid (one "confiscationTx" element in "confiscationTxs" array) to client
        if(first)
        {
            first=false;
        }
        else
        {
            httpReq->WriteReplyChunk(",");
        }
        httpReq->WriteReplyChunk(confiscationTxsElement_json.write());
    }

    // Send the reply footer
    httpReq->WriteReplyChunk("]}, \"error\": " + NullUniValue.write() + ", \"id\": " + jsonRPCReq.id.write() + "}");

    if (!processedInBatch) {
        httpReq->StopWritingChunks();
    }
}


// clang-format off
const CRPCCommand commands[] = {
  //  category      name                            actor (function)                okSafeMode
  //  ------------- -----------------------         ---------------------           ----------
    { "frozentxo", "addToPolicyBlacklist",          addToPolicyBlacklist,           true ,  {"funds"} },
    { "frozentxo", "addToConsensusBlacklist",       addToConsensusBlacklist,        true ,  {"funds"} },
    { "frozentxo", "removeFromPolicyBlacklist",     removeFromPolicyBlacklist,      true ,  {"funds"} },
    { "frozentxo", "queryBlacklist",                queryBlacklist,                 true ,  {}        },
    { "frozentxo", "clearBlacklists",               clearBlacklists,                true ,  {"removeAllEntries"} },
    { "frozentxo", "addToConfiscationTxidWhitelist",addToConfiscationTxidWhitelist, true ,  {"txs"}   },
    { "frozentxo", "clearConfiscationWhitelist",    clearConfiscationWhitelist,     true ,  {}        },
    { "frozentxo", "queryConfiscationTxidWhitelist",queryConfiscationTxidWhitelist, true ,  {"verbose"} }
};
// clang-format on

} // anonymous namespace




void RegisterFrozenTransactionRPCCommands(CRPCTable& t)
{
    for (auto& vc: commands)
    {
        t.appendCommand(vc.name, &vc);
    }
}
