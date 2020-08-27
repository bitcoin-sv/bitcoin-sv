// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include <thread>

#include "consensus/consensus.h"
#include "primitives/transaction.h"
#include "thread_safe_queue.h"
#include "validation.h"
#include "core_io.h"
#include "net/net.h"
#include "prevector.h"

#include <ctime>
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

    std::string GetTxnIdHex() const
    {
        return
            std::holds_alternative<CTransactionRef>(mTransaction) ?
            std::get<CTransactionRef>(mTransaction)->GetId().GetHex() :
            std::get<InvalidTxnInfo::TxData>(mTransaction).txid.GetHex();
    };

    std::size_t GetTotalTransactionSize() const
    {
        auto tx = std::get_if<CTransactionRef>(&mTransaction);
        if (tx)
        {
            return (*tx)->GetTotalSize();
        }

        return 0;
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

    size_t DynamicMemoryUsage() const;

    void ToJson(CJSONWriter& writer, bool writeHex = true) const;

private:
    // transactions or informations about transaction (usually if the transaction itself is too big)
    std::variant<CTransactionRef, TxData> mTransaction;
    CValidationState mTxValidationState;
    // details about transaction origin
    std::variant<BlockDetails, TxDetails> mDetails;
    std::time_t mRejectionTime;

    void PutOrigin(CJSONWriter& writer) const;
    void PutTx(CJSONWriter& writer, bool writeHex) const;
    void PutState(CJSONWriter& writer) const;
    void PutRejectionTime(CJSONWriter& writer) const;
};

class CInvalidTxnSink;

// Class used for asynchronous publishing invalid transactions to different sinks, 
// implemented as singleton, thread safe
class CInvalidTxnPublisher
{
public:
    static constexpr int64_t DEFAULT_FILE_SINK_DISK_USAGE = 3 * ONE_GIGABYTE;
    static constexpr InvalidTxEvictionPolicy DEFAULT_FILE_SINK_EVICTION_POLICY = InvalidTxEvictionPolicy::IGNORE_NEW;
#if ENABLE_ZMQ
    static constexpr int64_t DEFAULT_ZMQ_SINK_MAX_MESSAGE_SIZE = 500 * ONE_MEGABYTE;
#endif
private:
    // Queue for transactions which should be written to the sinks,
    // maximal size of transactions in the queue at any time is one gigabyte
    CThreadSafeQueue<InvalidTxnInfo> txInfoQueue;

    // invalid transaction sinks (can be file or zmq)
    std::vector<std::shared_ptr<CInvalidTxnSink>> sinks;
    std::mutex sinksGuard;

    void AddFileSink(int64_t maxSize, InvalidTxEvictionPolicy evictionPolicy);
#if ENABLE_ZMQ
    void AddZMQSink(int64_t maxMessageSize);
#endif

    // worker thread which takes a transaction from the queue and sends it to all sinks
    std::thread dumpingThread;

    // starts the dumpingThread
    CInvalidTxnPublisher();
public:
    static CInvalidTxnPublisher& Get();
    ~CInvalidTxnPublisher();

    CInvalidTxnPublisher(CInvalidTxnPublisher&&) = delete;
    CInvalidTxnPublisher(const CInvalidTxnPublisher&&) = delete;
    CInvalidTxnPublisher& operator=(CInvalidTxnPublisher&&) = delete;
    CInvalidTxnPublisher& operator=(const CInvalidTxnPublisher&) = delete;

    // Creates sinks
    void Initialize(const Config& config);
    // Stops closes queue and stops dumpingThread
    void Stop();

    // Puts invalid transaction on the queue
    void Publish(InvalidTxnInfo&& InvalidTxnInfo);

};
