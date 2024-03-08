// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#ifndef BITCOIN_FROZENTXO_LOGGING_H
#define BITCOIN_FROZENTXO_LOGGING_H

#include "frozentxo_db.h"
#include "uint256.h"
#include "primitives/transaction.h"

#include <memory>
#include <string>
#include <optional>

namespace BCLog{class Logger;}


/**
 * Logger used to log events related to frozen transaction outputs
 */
class CFrozenTXOLogger
{
private:
    std::unique_ptr<BCLog::Logger> logger;

    /**
     * Constructor initializes the logger by opening the log file
     *
     * Afterwards log events can be added by calling corresponding method.
     *
     * @note Constructor depends on default logger (GetLogger()), which must be initialized.
     */
    CFrozenTXOLogger();
    CFrozenTXOLogger(CFrozenTXOLogger&&) = delete;
    ~CFrozenTXOLogger();

public:
    /**
     * Initialize the Logger
     *
     * Afterwards, Instance() method can be called to use the log.
     *
     * Method is not thread-safe and can only be called if log has not yet been initialized.
     * Typically it is only called during program initialization.
     */
    static void Init();

    /**
     * Access to a single object of this class in application
     *
     * It is automatically constructed when first called and destroyed before program exits.
     *
     * @note This method should not be called after main() function has finished since the
     *       object may already be destroyed.
     */
    static CFrozenTXOLogger& Instance();

    /**
     * Close the log file
     *
     * Afterwards calling any method (other than destructor) on this object results in
     * undefined behaviour (most likely crash).
     *
     * Normally log file is closed in destructor. This method is only needed when log file
     * must be closed before that (e.g. in unit tests).
     */
    static void Shutdown();

    /**
     * Common log entry data for rejected entity (block/transaction)
     */
    struct LogEntry_Rejected
    {
        /**
         * Timestamp (in POSIX time) when the rejected entity was received
         */
        std::int64_t receivedTime;

        /**
         * Level of enforcement for frozen transaction output (member frozenTXO)
         */
        CFrozenTXODB::FrozenTXOData::Blacklist enforcementLevel;

        /**
         * Reference to rejected transaction
         */
        const CTransaction& rejectedTx;

        /**
         * Source of rejected entity
         *
         * Can be peer id or IP address
         */
        std::string source;

        /**
         * Frozen transaction output that caused the rejection of transaction (member rejectedTx)
         */
        COutPoint frozenTXO;

        /**
         * Hash of the previous active block
         */
        uint256 previousActiveBlockHash;
    };

    /**
     * Add log entry to indicate that a whole block was rejected because it included a transaction,
     * which tried to spend a frozen transaction output.
     *
     * @param le Common log entry data
     * @param rejectedBlockHash Hash of the block that was rejected
     */
    void LogRejectedBlock(const LogEntry_Rejected& le, const uint256& rejectedBlockHash);

    /**
     * Add log entry to indicate that a transaction was rejected because it tried to spend
     * a frozen transaction output.
     *
     * @param le Common log entry data
     */
    void LogRejectedTransaction(const LogEntry_Rejected& le);

    /**
     * Add log entry to indicate that a whole block was rejected because it included a confiscation transaction,
     * which was not whitelisted or not valid at this height.
     *
     * @param le Common log entry data. Member frozenTXO is not applicable and is omitted from log entry.
     * @param whitelistEnforceAtHeight If this does not contain a value, transaction was not whitelisted at all.
     *                                 If it does, transaction was whitelisted, but is only valid from this height on.
     * @param rejectedBlockHash Hash of the block that was rejected
     * @param onlyWarning If true, log entry contains text with warning without mentioning that the block was rejected.
     */
    void LogRejectedBlockCTNotWhitelisted(const LogEntry_Rejected& le, std::optional<std::int32_t> whitelistEnforceAtHeight, const uint256& rejectedBlockHash, bool onlyWarning);

    /**
     * Add log entry to indicate that a confiscation transaction was rejected because it was not whitelisted
     * or not valid at this height.
     *
     * @param le Common log entry data
     * @param whitelistEnforceAtHeight Same as in LogRejectedBlockCTNotWhitelisted() method.
     */
    void LogRejectedTransactionCTNotWhitelisted(const LogEntry_Rejected& le, std::optional<std::int32_t> whitelistEnforceAtHeight);
};




#endif // BITCOIN_FROZENTXO_LOGGING_H
