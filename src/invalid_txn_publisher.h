// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include <thread>

#include "consensus/consensus.h"
#include "primitives/transaction.h"
#include "thread_safe_queue.h"
#include "validation.h"
#include "core_io.h"
#include "net/net_types.h"
#include "prevector.h"

#include <ctime>
#include <functional>
#include <list>
#include <set>
#include <string>
#include <variant>

enum class InvalidTxEvictionPolicy
{
    IGNORE_NEW,
    DELETE_OLD
};

class InvalidTxnInfo
{
public:
    struct TxData
    {
        int64_t txSize;
        uint256 txid;
    };

    struct BlockOrigin
    {
        std::string source;
        std::string address;
        NodeId nodeId;
    };

    struct BlockDetails
    {
        std::vector<BlockOrigin> origins;
        uint256 hash;
        int64_t height;
        int64_t time;
    };

    struct TxDetails
    {
        TxSource src;
        NodeId nodeId;
        std::string address;
    };

    class CollidedWith
    {
    public:
        CollidedWith(const CTransactionRef& transaction)
            : mTransaction{ transaction }
        {}

        bool TruncateTransactionDetails()
        {
            // maybe we don't have space in the queue, try without actual transaction
            if (!std::holds_alternative<CTransactionRef>(mTransaction))
            {
                return false; // we are already without transaction
            }

            const auto& tx = std::get<CTransactionRef>(mTransaction);
            mTransaction = InvalidTxnInfo::TxData{tx->GetTotalSize(), tx->GetId()};

            return true;
        }

    private:
        std::variant<CTransactionRef, TxData> mTransaction;

        friend class InvalidTxnInfo;
    };

    InvalidTxnInfo(
        const CTransactionRef& tx,
        const std::variant<BlockDetails, TxDetails>& details,
        int64_t rejectionTime,
        const CValidationState& state)
        : mTransaction{ tx }
        , mTxValidationState{ state }
        , mCollidedWithTransaction(
            state.GetCollidedWithTx().begin(),
            state.GetCollidedWithTx().end() )
        , mDetails{ details }
        , mRejectionTime{ rejectionTime }
    {
        mTxValidationState.ClearCollidedWithTx();
    }

    InvalidTxnInfo(
        const CTransactionRef& tx,
        const uint256& hash,
        int64_t height,
        int64_t time,
        const CValidationState& state);

    InvalidTxnInfo(
        const CTransactionRef& tx,
        const CBlockIndex* blockIndex,
        const CValidationState& state)
        : InvalidTxnInfo{
            tx,
            blockIndex->GetBlockHash(),
            blockIndex->GetHeight(),
            blockIndex->GetBlockTime(),
            state}
    {}

    std::string GetTxnIdHex() const
    {
        return
            std::holds_alternative<CTransactionRef>(mTransaction) ?
            std::get<CTransactionRef>(mTransaction)->GetId().GetHex() :
            std::get<InvalidTxnInfo::TxData>(mTransaction).txid.GetHex();
    };

    std::size_t GetTotalTransactionSize() const
    {
        std::size_t totalSize = 0;
        auto tx = std::get_if<CTransactionRef>(&mTransaction);
        if (tx)
        {
            totalSize = (*tx)->GetTotalSize();
        }

        for(const auto& item : mCollidedWithTransaction)
        {
            auto tx = std::get_if<CTransactionRef>(&item.mTransaction);
            if (tx)
            {
                totalSize += (*tx)->GetTotalSize();
            }
        }

        return totalSize;
    }

    bool TruncateTransactionDetails()
    {
        // maybe we don't have space in the queue, try without actual transaction
        if (!std::holds_alternative<CTransactionRef>(mTransaction))
        {
            return false; // we are already without transaction
        }

        const auto& tx = std::get<CTransactionRef>(mTransaction);
        mTransaction = InvalidTxnInfo::TxData{tx->GetTotalSize(), tx->GetId()};

        return true;
    }

    class CollidedWithTruncationRange
    {
    public:
        std::vector<CollidedWith>::reverse_iterator begin()
        {
            return mCollidedWithTransaction.rbegin();
        }

        std::vector<CollidedWith>::reverse_iterator end()
        {
            return mCollidedWithTransaction.rend();
        }

    private:
        CollidedWithTruncationRange(std::vector<CollidedWith>& collidedWithTransaction)
            : mCollidedWithTransaction{ collidedWithTransaction }
        {}

        // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
        std::vector<CollidedWith>& mCollidedWithTransaction;

        friend class InvalidTxnInfo;
    };

    CollidedWithTruncationRange GetCollidedWithTruncationRange()
    {
        return { mCollidedWithTransaction };
    }

    size_t DynamicMemoryUsage() const;

    void ToJson(CJSONWriter& writer, bool writeHex = true) const;

private:
    // transactions or informations about transaction (usually if the transaction itself is too big)
    std::variant<CTransactionRef, TxData> mTransaction;
    CValidationState mTxValidationState;
    // NOTE: If a collision was not detected this variable will be set to an
    //       empty (nullptr) CTransactionRef
    std::vector<CollidedWith> mCollidedWithTransaction;
    // details about transaction origin
    std::variant<BlockDetails, TxDetails> mDetails;
    std::time_t mRejectionTime;

    void PutOrigin(CJSONWriter& writer) const;
    void PutTx(
        CJSONWriter& writer,
        const std::variant<CTransactionRef, TxData>& transaction,
        bool writeHex) const;
    void PutState(CJSONWriter& writer) const;
    void PutRejectionTime(CJSONWriter& writer) const;
};

namespace InvalidTxnPublisher
{
    /**
     * Class that is explicitly convertible to InvalidTxnInfo with guarantee that
     * transaction and collided with transactions references are present.
     */
    class InvalidTxnInfoWithTxn
    {
    public:
        InvalidTxnInfoWithTxn(
            const CTransactionRef& tx,
            const std::variant<InvalidTxnInfo::BlockDetails, InvalidTxnInfo::TxDetails>& details,
            int64_t rejectionTime,
            const CValidationState& state)
            : mTransaction{ tx }
            , mTxValidationState{ state }
            , mDetails{ details }
            , mRejectionTime{ rejectionTime }
        {}

        InvalidTxnInfoWithTxn(
            const CTransactionRef& tx,
            const uint256& hash,
            int64_t height,
            int64_t time,
            const CValidationState& state);

        InvalidTxnInfoWithTxn(
            const CTransactionRef& tx,
            const CBlockIndex* blockIndex,
            const CValidationState& state)
            : InvalidTxnInfoWithTxn{
                tx,
                blockIndex->GetBlockHash(),
                blockIndex->GetHeight(),
                blockIndex->GetBlockTime(),
                state}
        {}

        InvalidTxnInfo GetInvalidTxnInfo() const
        {
            return { mTransaction, mDetails, mRejectionTime, mTxValidationState };
        }

        const CTransactionRef& GetTransaction() const
        {
            return mTransaction;
        }

        const std::set<CTransactionRef>& GetCollidedWithTransactions() const
        {
            return mTxValidationState.GetCollidedWithTx();
        }

        const CValidationState& GetValidationState() const
        {
            return mTxValidationState;
        }

        const std::variant<InvalidTxnInfo::BlockDetails, InvalidTxnInfo::TxDetails>& GetDetails() const
        {
            return mDetails;
        }

    private:
        CTransactionRef mTransaction;
        CValidationState mTxValidationState;
        std::variant<InvalidTxnInfo::BlockDetails, InvalidTxnInfo::TxDetails> mDetails;
        std::time_t mRejectionTime;
    };

    // NOLINTNEXTLINE(cppcoreguidelines-special-member-functions)
    class CInvalidTxnSink
    {
    protected:

        static int64_t EstimateMessageSize(const InvalidTxnInfo& invalidTxnInfo, bool writeTxHex)
        {
            constexpr int64_t APPROXIMATE_SIZE_NO_HEX = 500; // roughly size of the json file without transaction hex
            if (writeTxHex)
            {
                // NOLINTNEXTLINE(*-narrowing-conversions)
                return invalidTxnInfo.GetTotalTransactionSize() * 2 + APPROXIMATE_SIZE_NO_HEX;
            }
            return APPROXIMATE_SIZE_NO_HEX;
        }

    public:

        virtual ~CInvalidTxnSink() = default;
        virtual void Publish(const InvalidTxnInfo& invalidTxnInfo) = 0;
        virtual int64_t ClearStored() { return 0;};
    };
}

// Class used for asynchronous publishing invalid transactions to different sinks, 
// thread safe
// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions)
class CInvalidTxnPublisher
{
public:
    static constexpr int64_t DEFAULT_FILE_SINK_DISK_USAGE = 3 * ONE_GIGABYTE;
    static constexpr InvalidTxEvictionPolicy DEFAULT_FILE_SINK_EVICTION_POLICY = InvalidTxEvictionPolicy::IGNORE_NEW;
#if ENABLE_ZMQ
    static constexpr int64_t DEFAULT_ZMQ_SINK_MAX_MESSAGE_SIZE = 500 * ONE_MEGABYTE;
#endif

    using PublishCallback =
        std::function<void(const InvalidTxnPublisher::InvalidTxnInfoWithTxn&)>;

private:
    // Queue for transactions which should be written to the sinks,
    // maximal size of transactions in the queue at any time is one gigabyte
    CThreadSafeQueue<InvalidTxnInfo> txInfoQueue;

    // invalid transaction sinks (can be file or zmq)
    std::vector<std::unique_ptr<InvalidTxnPublisher::CInvalidTxnSink>> mSinks;

    // worker thread which takes a transaction from the queue and sends it to all sinks
    std::thread dumpingThread;

    PublishCallback mPublishCallback;

public:
    /**
     * @param sinks to which info messages will be dumped either CTransactionRef
     *        data or InvalidTxnInfo::TxData (depending on the remaining queue
     *        size).
     * @param callback that is guaranteed to be called with CTransactionRef data
     *        before info is submitted to sinks. Callback is called from the
     *        thread that calls Publish() function. Exceptions thrown from the
     *        callback function are logged and ignored as they are not expected.
     * @param maxQueueSize is cumulative size of queued InvalidTxnInfo. If size
     *        is exceeded transaction data is truncated and if that doesn't make
     *        it compact enough the info is silently discarded.
     *
     * Starts the dumpingThread.
     */
    CInvalidTxnPublisher(
        std::vector<std::unique_ptr<InvalidTxnPublisher::CInvalidTxnSink>>&& sinks,
        PublishCallback&& callback,
        std::size_t maxQueueSize = ONE_GIGABYTE);

    ~CInvalidTxnPublisher();

    CInvalidTxnPublisher(CInvalidTxnPublisher&&) = delete;
    CInvalidTxnPublisher(const CInvalidTxnPublisher&&) = delete;
    CInvalidTxnPublisher& operator=(CInvalidTxnPublisher&&) = delete;
    CInvalidTxnPublisher& operator=(const CInvalidTxnPublisher&) = delete;

    // Puts invalid transaction on the queue
    void Publish(InvalidTxnPublisher::InvalidTxnInfoWithTxn&& InvalidTxnInfo);

    // Removes locally stored invalid transactions
    int64_t ClearStored();
};

// Utility class that registers block origin in the constructor and unregisters in the destructor.
// Usually, at places where we validate transactions we don't have information
// how we got block which contains these transactions. So when we are starting to validate
// block we are registering its origin and when we are finished with validation we are unregistering.
class CScopedBlockOriginRegistry
{
    using BlockOriginRegistry =
        std::list<std::tuple<uint256, InvalidTxnInfo::BlockOrigin>>;
    BlockOriginRegistry::const_iterator mThisItem;

    // registering origin of the block (from which peer, rpc)
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
    inline static BlockOriginRegistry mRegistry;
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
    inline static std::mutex mRegistryGuard;

public:
    CScopedBlockOriginRegistry(const uint256& hash,
                               const std::string& source,
                               const std::string& address = "",
                               NodeId nodeId = 0);

    ~CScopedBlockOriginRegistry();

    CScopedBlockOriginRegistry(CScopedBlockOriginRegistry&&) = delete;
    CScopedBlockOriginRegistry(const CScopedBlockOriginRegistry&) = delete;
    CScopedBlockOriginRegistry& operator=(CScopedBlockOriginRegistry&&) = delete;
    CScopedBlockOriginRegistry& operator=(const CScopedBlockOriginRegistry&) = delete;

    static std::vector<InvalidTxnInfo::BlockOrigin> GetOrigins(const uint256& blockHash);
};

// Utility class that takes informations about transaction in the constructor and if evaluation failed publishes
// it from the destructor. Useful in function with multiple exits
class CScopedInvalidTxSenderBlock
{
    CInvalidTxnPublisher* publisher;
    InvalidTxnInfo::BlockDetails blockDetails;
    const CTransactionRef transaction;
    const CValidationState& validationState;

public:
    CScopedInvalidTxSenderBlock(CInvalidTxnPublisher* dump,
                                CTransactionRef tx,
                                const CBlockIndex* blockIndex,
                                const CValidationState& state)
        :publisher( dump )
        ,blockDetails(
            blockIndex ? InvalidTxnInfo::BlockDetails{{},
                                                      blockIndex->GetBlockHash(),
                                                      blockIndex->GetHeight(),
                                                      blockIndex->GetBlockTime()}
                       : InvalidTxnInfo::BlockDetails{})
        ,transaction( std::move(tx) )
        ,validationState(state)
    {}

    ~CScopedInvalidTxSenderBlock()
    {
        if (validationState.IsValid() || !publisher)
        {
            return;
        }

        blockDetails.origins = CScopedBlockOriginRegistry::GetOrigins(blockDetails.hash);
        publisher->Publish( {transaction, blockDetails, std::time(nullptr), validationState} );
    }

    CScopedInvalidTxSenderBlock(CScopedInvalidTxSenderBlock&&) = delete;
    CScopedInvalidTxSenderBlock(const CScopedInvalidTxSenderBlock&) = delete;
    CScopedInvalidTxSenderBlock& operator=(CScopedInvalidTxSenderBlock&&) = delete;
    CScopedInvalidTxSenderBlock& operator=(const CScopedInvalidTxSenderBlock&) = delete;
};
