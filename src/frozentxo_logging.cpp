// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "frozentxo_logging.h"

#include "logging.h"
#include "core_io.h"

#include "boost/date_time/posix_time/posix_time.hpp"




CFrozenTXOLogger::CFrozenTXOLogger() = default;
CFrozenTXOLogger::~CFrozenTXOLogger() = default;

CFrozenTXOLogger& CFrozenTXOLogger::Instance()
{
    static CFrozenTXOLogger logger;
    return logger;
}

void CFrozenTXOLogger::Init()
{
    auto& log = CFrozenTXOLogger::Instance();
    log.logger = std::unique_ptr<BCLog::Logger>(new BCLog::Logger("blacklist.log"));

    // Set required Logger options
    log.logger->fPrintToDebugLog = true; // always log to file
    log.logger->fPrintToConsole = false; // never log to console since all log entries are always also written to standard log file in addition to blacklist.log
    log.logger->fLogTimestamps = true; // timestamps must always be included in log entries
    log.logger->fLogTimeMicros = GetLogger().fLogTimeMicros; // use same precision for timestamps as in default logger

    // Open the log file
    log.logger->OpenDebugLog();
}

void CFrozenTXOLogger::Shutdown()
{
    auto& log = CFrozenTXOLogger::Instance();
    log.logger.reset();
}

namespace {

std::string le_to_string(const CFrozenTXOLogger::LogEntry_Rejected& le, bool log_frozenTXO=true)
{
    std::string s;
    s += " received_timestamp=";
    s += DateTimeFormatISO8601(le.receivedTime);
    s += " enforcement_level=";
    if(le.enforcementLevel==CFrozenTXODB::FrozenTXOData::Blacklist::PolicyOnly)
    {
        s += "policy";
    }
    else if(le.enforcementLevel==CFrozenTXODB::FrozenTXOData::Blacklist::Consensus)
    {
        s += "consensus";
    }
    else
    {
        s += "unknown";
    }
    s += " rejected_tx_hash="+le.rejectedTx.GetHash().ToString();
    s += " source='"+le.source+"'";
    if(log_frozenTXO)
    {
        s += " frozen_TXO={"+le.frozenTXO.GetTxId().ToString()+","+std::to_string(le.frozenTXO.GetN())+"}";
    }
    s += " previous_active_block_hash="+le.previousActiveBlockHash.ToString();
    return s;
}

}

void CFrozenTXOLogger::LogRejectedBlock(const LogEntry_Rejected& le, const uint256& rejectedBlockHash)
{
    std::string msg = "Block was rejected because it included a transaction, which tried to spend a frozen transaction output!";
    msg += le_to_string(le);
    msg += " rejected_block_hash="+rejectedBlockHash.ToString();
    msg += " rejected_tx_hex="+EncodeHexTx(le.rejectedTx);
    msg += '\n';

    this->logger->LogPrintStr(msg);

    // Also write entry to standard log
    GetLogger().LogPrintStr(msg);
}

void CFrozenTXOLogger::LogRejectedTransaction(const LogEntry_Rejected& le)
{
    std::string msg = "Transaction was rejected because it tried to spend a frozen transaction output!";
    msg += le_to_string(le);
    msg += " rejected_tx_hex="+EncodeHexTx(le.rejectedTx);
    msg += '\n';

    this->logger->LogPrintStr(msg);

    // Also write entry to standard log
    GetLogger().LogPrintStr(msg);
}

void CFrozenTXOLogger::LogRejectedBlockCTNotWhitelisted(const LogEntry_Rejected& le, std::optional<std::int32_t> whitelistEnforceAtHeight, const uint256& rejectedBlockHash, bool onlyWarning)
{
    std::string msg = "Block was rejected because it included a confiscation transaction";
    if(onlyWarning)
    {
        msg = "WARNING! Block included a confiscation transaction";
    }
    if(!whitelistEnforceAtHeight.has_value())
    {
        msg+=", which was not whitelisted!";
    }
    else
    {
        msg+=", which was whitelisted but not valid before height "+std::to_string(*whitelistEnforceAtHeight)+"!";
    }
    msg += le_to_string(le, false);
    msg += " rejected_block_hash="+rejectedBlockHash.ToString();
    msg += " rejected_tx_hex="+EncodeHexTx(le.rejectedTx);
    msg += '\n';

    this->logger->LogPrintStr(msg);
    GetLogger().LogPrintStr(msg);
}

void CFrozenTXOLogger::LogRejectedTransactionCTNotWhitelisted(const LogEntry_Rejected& le, std::optional<std::int32_t> whitelistEnforceAtHeight)
{
    std::string msg = "Confiscation transaction was rejected because";
    if(!whitelistEnforceAtHeight.has_value())
    {
        msg+=" it was not whitelisted!";
    }
    else
    {
        msg+=" it was whitelisted but not valid before height "+std::to_string(*whitelistEnforceAtHeight)+"!";
    }
    msg += le_to_string(le, false);
    msg += " rejected_tx_hex="+EncodeHexTx(le.rejectedTx);
    msg += '\n';

    this->logger->LogPrintStr(msg);
    GetLogger().LogPrintStr(msg);
}
