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
        const uint32_t vout = txOut["vout"].get_int();
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
      "reason": <string>)") );
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
      "reason": <string>)") );
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
      "reason": <string>)") );
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
      "blacklist": [ <string> ])") );
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

If removeAllEntries=true, all entries are unconditionally removed.
If removeAllEntries=false, only expired consensus entries are either removed or updated to policy-only, depending on their value of policyExpiresWithConsensus. Expired entries are consensus blacklist entries with stopEnforceAtHeight that is at least expirationHeightDelta blocks smaller than current block height.

Arguments: {
  removeAllEntries: <boolean>,
  expirationHeightDelta: <integer>
}

Result: {
  numRemovedEntries: <integer>
}
)" );
    }

    // Parse request parameters
    RPCTypeCheck(jsonRPCReq.params, { UniValue::VOBJ });
    const UniValue& params = jsonRPCReq.params[0];
    RPCTypeCheckObj(params, {
            {"removeAllEntries",      UniValueType(UniValue::VBOOL)},
            {"expirationHeightDelta", UniValueType(UniValue::VNUM )}
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

    auto& db = CFrozenTXODB::Instance();
    std::uint64_t numRemovedEntries = 0;
    if(removeAllEntries)
    {
        auto res = db.UnfreezeAll();
        numRemovedEntries += res.numUnfrozenPolicyOnly;
        numRemovedEntries += res.numUnfrozenConsensus;
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


// clang-format off
const CRPCCommand commands[] = {
  //  category      name                            actor (function)                okSafeMode
  //  ------------- -----------------------         ---------------------           ----------
    { "frozentxo", "addToPolicyBlacklist",         addToPolicyBlacklist,           false,  {"funds"} },
    { "hidden",    "addToConsensusBlacklist",      addToConsensusBlacklist,        false,  {"funds"} },
    { "frozentxo", "removeFromPolicyBlacklist",    removeFromPolicyBlacklist,      false,  {"funds"} },
    { "frozentxo", "queryBlacklist",               queryBlacklist,                 false,  {}        },
    { "frozentxo", "clearBlacklists",              clearBlacklists,                false,  {"removeAllEntries"} }
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
