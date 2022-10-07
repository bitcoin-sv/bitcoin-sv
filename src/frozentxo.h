// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#ifndef BITCOIN_FROZENTXO_H
#define BITCOIN_FROZENTXO_H

#include <cstdint>
#include <string>

class CBlockIndex;
class COutPoint;
class CTransaction;
struct TxId;
class uint256;

/**
 * Class for checking whether a standalone transaction or transaction that is
 * part of a block is frozen or not and logs to special frozen txo log in case
 * it is frozen.
 */
class CFrozenTXOCheck
{
public:
    // For transactions validation
    // NOTE: previousActiveBlockHash must remain stable until this class instance is destroyed
    //       nHeight is the block height used to check if TXO is frozen. Typically height of best chain tip + 1 should
    //       be specified since this is the height of the first block in which transaction can be included.
    CFrozenTXOCheck(
        std::int32_t nHeight,
        const std::string& source,
        const uint256& previousActiveBlockHash,
        std::int64_t receivedTime = 0);

    // For block validation
    // NOTE: previousActiveBlockHash must remain stable until this class instance is destroyed
    // NOTE: blockHash must remain stable until this class instance is destroyed
    //       nHeight is the block height used to check if TXO is frozen. Typically this is the height of block that is currently being validated.
    CFrozenTXOCheck( const CBlockIndex& blockIndex );

    /**
     * Check whether output is frozen and if yes, set value of effectiveBlacklist argument.
     *
     * Returns true if check passes and false if the output is frozen.
     *
     * @param outpoint This TXO is checked if it is frozen.
     * @param effectiveBlacklist Blacklist on which TXO was frozen. Only set if TXO is frozen, otherwise this value is left unchanged.
     *                           If set, value is intended to be passed to LogAttemptToSpendFrozenTXO() member function.
     *
     * NOTE: Function is a no-op if mBlockIndex->IsInExplicitSoftConsensusFreeze() returns true.
     */
    bool Check(const COutPoint& outpoint, std::uint8_t& effectiveBlacklist) const;

    /**
     * Same as above and if output is frozen, log that a transaction was trying to spend it.
     *
     * @param outpoint This TXO is checked if it is frozen.
     * @param tx Transaction trying to spend TXO 'outpoint'
     * @param receivedTime Time when the tx was received. If mReceivedTime!=0, this value is ignored and mReceivedTime is use instead.
     */
    bool Check(const COutPoint& outpoint, const CTransaction& tx, std::int64_t receivedTime = 0) const;

    /**
     * Add an entry to blacklist log file that a transaction was trying to spend a frozen output.
     *
     * @param outpoint Frozen TXO
     * @param tx Transaction trying to spend TXO 'outpoint'
     * @param effectiveBlacklist Blacklist on which TXO was frozen as returned by Check() member function
     * @param receivedTime Time when the tx was received. If mReceivedTime!=0, this value is ignored and mReceivedTime is use instead.
     */
    void LogAttemptToSpendFrozenTXO(const COutPoint& outpoint, const CTransaction& tx, std::uint8_t effectiveBlacklist, std::int64_t receivedTime) const;

    bool IsCheckOnBlock() const { return mBlockIndex != nullptr; }

    /**
     * Wrapper for CFrozenTXODB::Get_max_FrozenTXOData_enforceAtHeight_stop()
     */
    static std::int32_t Get_max_FrozenTXOData_enforceAtHeight_stop();


    /**
     * Wrapper for CFrozenTXODB::IsConfiscationTx()
     */
    static bool IsConfiscationTx(const CTransaction& tx);

    /**
     * Wrapper for CFrozenTXODB::ValidateConfiscationTxContents()
     */
    static bool ValidateConfiscationTxContents(const CTransaction& confiscation_tx);

    /**
     * Check whether confiscation transaction tx is whitelisted and can be spent at height nHeight.
     *
     * Adds a log entry if not.
     *
     * @param tx Confiscation transaction
     * @param receivedTime Time when the tx was received. If mReceivedTime!=0, this value is ignored and mReceivedTime is use instead.
     *
     * @return true iff transaction is whitelisted and can be spent at height nHeight.
     */
    bool CheckConfiscationTxWhitelisted(const CTransaction& tx, std::int64_t receivedTime = 0) const;

    /**
     * Used to disable enforcing checks if confiscation transaction is whitelisted and spends only consensus
     * frozen TXOs when validating a block
     *
     * Afterwards method CheckConfiscationTxWhitelisted() always succeeds.
     * The check is still performed and if it fails, a warning is logged.
     *
     * This setting has no effect when validating transaction that is not in block.
     *
     * This is intended to be used when database may not contain current state (consensus frozen TXO, whitelisted)
     * and block containing confiscation transaction is deep enough in active chain so that confiscation
     * transaction can be assumed to have been valid at the time the block was mined.
     *
     * A typical example is during initial block download when node does has not yet have current data for
     * frozen TXOs and whitelisted transactions or because because confiscation transactions are so old that
     * this data may not even exist anymore.
     */
    void DisableEnforcingConfiscationTransactionChecks()
    {
        disableEnforcingConfiscationTransactionChecks = true;
    }

    /**
     * Wrapper for CFrozenTXODB::Get_max_WhitelistedTxData_enforceAtHeight()
     */
    static std::int32_t Get_max_WhitelistedTxData_enforceAtHeight();

private:
    std::int32_t nHeight;
    std::string mSource;
    const uint256& mPreviousActiveBlockHash;
    std::int64_t mReceivedTime{0};

    // only used for block level validation
    const CBlockIndex* mBlockIndex{ nullptr };
    bool disableEnforcingConfiscationTransactionChecks = false;
};

#endif // BITCOIN_FROZENTXO_H
