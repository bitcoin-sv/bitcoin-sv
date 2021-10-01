// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "http_protocol.h"
#include "block_index_store.h"
#include "validation.h"
#include "rpc/server.h"
#include "safe_mode.h"
#include "jsonwriter.h"
#include "logging.h"

UniValue ignoresafemodeforblock(const Config &config, const JSONRPCRequest &request) 
{
    if (request.fHelp || request.params.size() != 1) {
        throw std::runtime_error(
            "ignoresafemodeforblock \"blockhash\"\n"
            "\nSpecified block, and all its descendants, will be ignored for safe mode activation.\n"
            "\nArguments:\n"
            "1. \"blockhash\"   (string, required) the hash of "
            "the block which we want to ignore.\n"
            "\nResult:\n"
            "\nExamples:\n" +
            HelpExampleCli("ignoresafemodeforblock", "\"blockhash\"") +
            HelpExampleRpc("ignoresafemodeforblock", "\"blockhash\""));
    }

    std::string strHash = request.params[0].get_str();
    uint256 hash{uint256S(strHash)};

    {
        LOCK(cs_main);
        
        auto* pblockindex = mapBlockIndex.Get(hash);
        
        if (!pblockindex)
        {
            throw JSONRPCError(RPC_INVALID_PARAMS, "Unknown block!\n");
        }

        if(chainActive.Contains(pblockindex))
        {
            throw JSONRPCError(RPC_INVALID_PARAMS, "Can not ignore a block on the main chain!\n");//TODO: add better error description!
        }
        
        pblockindex->SetIgnoredForSafeMode(true);
        CheckSafeModeParameters(config, nullptr);
    }


    return NullUniValue;
}

UniValue reconsidersafemodeforblock(const Config &config, const JSONRPCRequest &request) 
{
    if (request.fHelp || request.params.size() != 1) {
        throw std::runtime_error(
            "reconsidersafemodeforblock \"blockhash\"\n"
            "\nSpecified block, and all its ancestors, will be considered for safe mode activation. \n"
            "\nArguments:\n"
            "1. \"blockhash\"   (string, required) the hash of "
            "the block for which we want \n"
            "\nResult:\n"
            "\nExamples:\n" +
            HelpExampleCli("reconsidersafemodeforblock", "\"blockhash\"") +
            HelpExampleRpc("reconsidersafemodeforblock", "\"blockhash\""));
    }

    std::string strHash = request.params[0].get_str();
    uint256 hash{uint256S(strHash)};

    {
        LOCK(cs_main);
        CBlockIndex* pblockindex = mapBlockIndex.Get(hash);

        if (!pblockindex)
        {
            throw JSONRPCError(RPC_INVALID_PARAMS, "Unknown block!\n");
        }

        if(chainActive.Contains(pblockindex))
        {
            throw JSONRPCError(RPC_INVALID_PARAMS, "Can not reconsider a block on the main chain!\n");
        }

        while(!chainActive.Contains(pblockindex))
        {
            pblockindex->SetIgnoredForSafeMode(false);
            pblockindex = pblockindex->GetPrev();
        }

        CheckSafeModeParameters(config, nullptr);
    }

    return NullUniValue;
}

void  getsafemodeinfo(const Config& config,
                   const JSONRPCRequest& request,
                   HTTPRequest* httpReq,
                   bool processedInBatch) 
{
    if (request.fHelp || request.params.size() > 0) {
        throw std::runtime_error(
            "getsafemodeinfo\n"
            "\nReturns safe mode status.\n"
            "\nArguments:\n"
            "\nResult:"
            "\n{"
            "\n  \"safemodeenabled\": <true/false>,"
            "\n  \"activetip\": {"
            "\n    \"hash\": \"<block_hash>\","
            "\n    \"height\": <height>,"
            "\n    \"blocktime\": \"<time UTC>\","
            "\n    \"firstseentime\": \"<time UTC>\","
            "\n    \"status\": \"active\""
            "\n  },"
            "\n  \"timeutc\": \"<time_of_the_message>\","
            "\n  \"reorg\": {"
            "\n    \"happened\": <true/false>,"
            "\n    \"numberofdisconnectedblocks\": <number>,"
            "\n    \"oldtip\": {"
            "\n      \"hash\": \"<block_hash>\","
            "\n      \"height\": <height>,"
            "\n      \"blocktime\": \"<time UTC>\","
            "\n      \"firstseentime\": \"<time UTC>\","
            "\n      \"status\": \"<block_header_status>\""
            "\n    }"
            "\n  },"
            "\n  \"forks\": ["
            "\n    {"
            "\n      \"forkfirstblock\": {"
            "\n        \"hash\": \"<block_hash>\","
            "\n        \"height\": <height>,"
            "\n        \"blocktime\": \"<time UTC>\","
            "\n        \"firstseentime\": \"<time UTC>\","
            "\n        \"status\": \"<block_header_status>\""
            "\n      },"
            "\n      \"tips\": ["
            "\n        {"
            "\n          \"hash\": \"<block_hash>\","
            "\n          \"height\": <height>,"
            "\n          \"blocktime\": \"<time UTC>\","
            "\n          \"firstseentime\": \"<time UTC>\","
            "\n          \"status\": \"<block_header_status>\""
            "\n        },"
            "\n        ..."
            "\n      ],"
            "\n      \"lastcommonblock\": {"
            "\n        \"hash\": \"<block_hash>\","
            "\n        \"height\": <height>,"
            "\n        \"blocktime\": \"<time UTC>\","
            "\n        \"firstseentime\": \"<time UTC>\","
            "\n        \"status\": \"active\""
            "\n      },"
            "\n      \"activechainfirstblock\": {"
            "\n        \"hash\": \"<block_hash>\","
            "\n        \"height\": <height>,"
            "\n        \"blocktime\": \"<time UTC>\","
            "\n        \"firstseentime\": \"<time UTC>\","
            "\n        \"status\": \"active\""
            "\n      },"
            "\n    },"
            "\n         ..."
            "\n  ]"
            "\n}"
            "\n\n"
            "\nExamples:\n" +
            HelpExampleCli("getsafemodeinfo", "") +
            HelpExampleRpc("getsafemodeinfo", ""));
    }

    if (!processedInBatch)
    {
        httpReq->WriteHeader("Content-Type", "application/json");
        httpReq->StartWritingChunks(HTTP_OK);
    }

    {
        CHttpTextWriter httpWriter{*httpReq};
        CJSONWriter jWriter(httpWriter, false);
        
        jWriter.writeBeginObject();
        jWriter.pushKNoComma("result");
        LOCK(cs_main);
        SafeModeGetStatus(jWriter);
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

// clang-format off
static const CRPCCommand commands[] = {
    //  category            name                   actor (function)            okSafeMode
    //  ------------ ----------------------------- --------------------------  ----------
    { "safemode",    "ignoresafemodeforblock",     ignoresafemodeforblock,     true,  {"blockhash"} },
    { "safemode",    "reconsidersafemodeforblock", reconsidersafemodeforblock, true,  {"blockhash"} },
    { "safemode",    "getsafemodeinfo",            getsafemodeinfo,            true,  {} },
};
// clang-format on

void RegisterSafeModeRPCCommands(CRPCTable &t) {
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++) {
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
    }
}