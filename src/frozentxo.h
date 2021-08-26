// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#ifndef BITCOIN_FROZENTXO_H
#define BITCOIN_FROZENTXO_H

#include <cstdint>
#include <string>

class COutPoint;
class CTransaction;
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
    CFrozenTXOCheck(
        std::int32_t nHeight,
        const std::string& source,
        const uint256& previousActiveBlockHash,
        std::int64_t receivedTime,
        const uint256& blockHash);

    /**
     * Helper base class used by Check() method
     */
    struct TxGetter
    {
        struct TxData
        {
            TxData(const CTransaction& tx, std::int64_t receivedTime)
            : tx(tx)
            , receivedTime(receivedTime)
            {}

            const CTransaction& tx;
            const std::int64_t receivedTime;
        };

        virtual TxData GetTxData() = 0;
        virtual ~TxGetter() = default;
    };

    /**
     * Check whether output is frozen and if yes, log that a transaction was trying to spend it.
     *
     * Returns true if check passes and false if the output is frozen.
     *
     * @param outpoint This TXO is checked if it is frozen.
     * @param txGetter Object used to obtain reference to transaction trying to spend a frozen output.
     *                 For performance reasons, GetTxData() method is called only if it was determined that
     *                 the output is frozen.
     *                 Reference TxData::tx returned by call to GetTxData() must be valid until the next
     *                 call to GetTxData() or until Check() method returns.
     *                 Value TxData::receivedTime is the time when the tx was received and is used only
     *                 in case mReceivedTime is set to 0.
     */
    bool Check(const COutPoint& outpoint, TxGetter& txGetter);

    /**
     * Same as above, except that transaction reference and the time it was received are provided directly.
     */
    bool Check(const COutPoint& outpoint, const CTransaction& tx, std::int64_t receivedTime = 0);

    /**
     * Wrapper for CFrozenTXODB::Get_max_FrozenTXOData_enforceAtHeight_stop()
     */
    static std::int32_t Get_max_FrozenTXOData_enforceAtHeight_stop();

private:
    std::int32_t nHeight;
    std::string mSource;
    const uint256& mPreviousActiveBlockHash;
    std::int64_t mReceivedTime{0};

    // only used for block level validation
    const uint256* mBlockHash{nullptr};
};

#endif // BITCOIN_FROZENTXO_H
