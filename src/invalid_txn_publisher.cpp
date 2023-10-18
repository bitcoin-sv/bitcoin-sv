// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "invalid_txn_publisher.h"
#include "core_io.h"
#include "util.h"
#include "memusage.h"

InvalidTxnInfo::InvalidTxnInfo(
    const CTransactionRef& tx,
    const uint256& hash,
    int64_t height,
    int64_t time,
    const CValidationState& state)
    : InvalidTxnInfo{
        tx,
        InvalidTxnInfo::BlockDetails{
            CScopedBlockOriginRegistry::GetOrigins(hash),
            hash,
            height,
            time},
        std::time(nullptr),
        state}
{}

size_t InvalidTxnInfo::DynamicMemoryUsage() const
{
    size_t totalSize = sizeof(InvalidTxnInfo);

    if(auto tx = std::get_if<CTransactionRef>(&mTransaction))
    {
        totalSize += memusage::DynamicUsage(*tx);
    }

    totalSize += mTxValidationState.GetRejectReason().capacity();
    totalSize += mTxValidationState.GetDebugMessage().capacity();

    if(auto det = std::get_if<BlockDetails>(&mDetails))
    {
        totalSize += memusage::DynamicUsage(det->origins);
        for(const auto& o: det->origins)
        {
            totalSize += o.source.capacity();
            totalSize += o.address.capacity();
        }
    }
    else if(auto origin = std::get_if<TxDetails>(&mDetails))
    {
        totalSize += origin->address.capacity();
    }

    totalSize +=
        std::accumulate(
            mCollidedWithTransaction.begin(),
            mCollidedWithTransaction.end(),
            0,
            [](int sum, const CollidedWith& item)
            {
                if(auto tx = std::get_if<CTransactionRef>(&item.mTransaction); tx)
                {
                    sum += memusage::DynamicUsage(*tx);
                }

                return sum;
            }
        );

    return totalSize;
}

void InvalidTxnInfo::PutOrigin(CJSONWriter& writer) const
{
    if(auto blockDetails = std::get_if<InvalidTxnInfo::BlockDetails>(&mDetails))
    {
        writer.pushKV("fromBlock", true);
        writer.writeBeginArray("origins");
        for(const auto& origin: blockDetails->origins)
        {
            writer.writeBeginObject();

            writer.pushKV("source", origin.source);
            if(!origin.address.empty())
            {
                writer.pushKV("address", origin.address);
                writer.pushKV("nodeId", origin.nodeId);
            }
            writer.writeEndObject();
        };
        writer.writeEndArray();
        writer.pushKV("blockhash", blockDetails->hash.GetHex());
        writer.pushKV("blocktime", blockDetails->time);
        writer.pushKV("blockheight", blockDetails->height);
    }
    else if (auto txDetails = std::get_if<InvalidTxnInfo::TxDetails>(&mDetails))
    {
        writer.pushKV("fromBlock", false);
        writer.pushKV("source", enum_cast(txDetails->src));
        if(!txDetails->address.empty())
        {
            writer.pushKV("address", txDetails->address);
            writer.pushKV("nodeId", txDetails->nodeId);
        }
    }
}

void InvalidTxnInfo::PutTx(
    CJSONWriter& writer,
    const std::variant<CTransactionRef, TxData>& transaction,
    bool writeHex) const
{
    if(auto tx = std::get_if<CTransactionRef>(&transaction))
    {
        if(*tx)
        {
            writer.pushKV("txid", (*tx)->GetId().GetHex());
            writer.pushKV("size", int64_t((*tx)->GetTotalSize()));
            if(writeHex)
            {
                writer.pushK("hex");
                writer.pushQuote();
                EncodeHexTx(**tx, writer.getWriter(), 0);
                writer.pushQuote();
            }
        }
    }
    else if(auto tx = std::get_if<InvalidTxnInfo::TxData>(&transaction))
    {
        writer.pushKV("txid", tx->txid.GetHex());
        writer.pushKV("size", tx->txSize);
    }
}

void InvalidTxnInfo::PutState(CJSONWriter& writer) const
{
    writer.pushKV("isInvalid", mTxValidationState.IsInvalid());
    writer.pushKV("isValidationError", mTxValidationState.IsError());
    writer.pushKV("isMissingInputs", mTxValidationState.IsMissingInputs());
    writer.pushKV("isDoubleSpendDetected", mTxValidationState.IsDoubleSpendDetected());
    writer.pushKV("isMempoolConflictDetected", mTxValidationState.IsMempoolConflictDetected());
    writer.pushKV("isNonFinal", mTxValidationState.IsNonFinal());
    writer.pushKV("isValidationTimeoutExceeded", mTxValidationState.IsValidationTimeoutExceeded());
    writer.pushKV("isStandardTx", mTxValidationState.IsStandardTx());
    writer.pushKV("rejectionCode", static_cast<int64_t>(mTxValidationState.GetRejectCode()));
    writer.pushKV("rejectionReason", mTxValidationState.GetRejectReason());
}


void InvalidTxnInfo::PutRejectionTime(CJSONWriter& writer) const
{
    //YYYY-MM-DDThh:mm:ssZ
    auto time = DateTimeStrFormat("%Y-%m-%dT%H:%M:%SZ", mRejectionTime);
    writer.pushKV("rejectionTime", time);
}

// writes invalidTxnInfo to the writer
void InvalidTxnInfo::ToJson(CJSONWriter& writer, bool writeHex) const
{
    writer.writeBeginObject();

    PutOrigin(writer);
    PutTx(writer, mTransaction, writeHex);
    PutState(writer);

    writer.writeBeginArray("collidedWith");
    for(auto& item : mCollidedWithTransaction)
    {
        writer.writeBeginObject();
        PutTx(writer, item.mTransaction, writeHex);
        writer.writeEndObject();
    }
    writer.writeEndArray();

    PutRejectionTime(writer);

    writer.writeEndObject();
}

InvalidTxnPublisher::InvalidTxnInfoWithTxn::InvalidTxnInfoWithTxn(
    const CTransactionRef& tx,
    const uint256& hash,
    int64_t height,
    int64_t time,
    const CValidationState& state)
    : InvalidTxnInfoWithTxn{
        tx,
        InvalidTxnInfo::BlockDetails{
            CScopedBlockOriginRegistry::GetOrigins(hash),
            hash,
            height,
            time},
        std::time(nullptr),
        state}
{}

CInvalidTxnPublisher::CInvalidTxnPublisher(
    std::vector<std::unique_ptr<InvalidTxnPublisher::CInvalidTxnSink>>&& sinks,
    PublishCallback&& callback,
    std::size_t maxQueueSize)
    : txInfoQueue(maxQueueSize, [](const InvalidTxnInfo& txInfo) { return txInfo.DynamicMemoryUsage(); })
    , mSinks{ std::move(sinks) }
    , mPublishCallback{ std::move(callback) }
{
    if( mSinks.empty() )
    {
        txInfoQueue.Close();
        return;
    }

    auto taskFunction = [this](){
        while(true)
        {
            auto txInfo = txInfoQueue.PopWait();

            if (!txInfo.has_value())
            {
                break;
            }
            std::string txid = txInfo->GetTxnIdHex();
            LogPrintf("Dumping invalid transaction %s\n", txid);

            for (auto& sink: mSinks)
            {
                sink->Publish(*txInfo);
            }
        }
    };

    dumpingThread =
        std::thread(
            [taskFunction](){TraceThread("invalidtxnpublisher", taskFunction);});
}


CInvalidTxnPublisher::~CInvalidTxnPublisher()
{
    if(!txInfoQueue.IsClosed())
    {
        txInfoQueue.Close(true);
    }

    if(dumpingThread.joinable())
    {
        dumpingThread.join();
    }

    mSinks.clear();
}

void CInvalidTxnPublisher::Publish(InvalidTxnPublisher::InvalidTxnInfoWithTxn&& invalidTxnInfo)
{
    if (mPublishCallback)
    {
        try
        {
            mPublishCallback(invalidTxnInfo);
        }
        catch(const std::exception& e)
        {
            LogPrintf(
                "Error CInvalidTxnPublisher::Publish threw an unexpected exception: %s\n",
                e.what());
        }
        catch(...)
        {
            LogPrintf("Error CInvalidTxnPublisher::Publish threw an unexpected exception!\n");
        }
    }

    if (txInfoQueue.IsClosed())
    {
        return;
    }

    InvalidTxnInfo info = invalidTxnInfo.GetInvalidTxnInfo();

    if (!txInfoQueue.PushNoWait(info))
    {
        for( auto& item : info.GetCollidedWithTruncationRange() )
        {
            if (!item.TruncateTransactionDetails())
            {
                continue;
            }

            if (txInfoQueue.PushNoWait(info))
            {
                return;
            }
        }

        // maybe we don't have space in the queue, try without actual transaction
        if (!info.TruncateTransactionDetails())
        {
            return;
        }
        txInfoQueue.PushNoWait(info);
    }
}

int64_t CInvalidTxnPublisher::ClearStored()
{
    int64_t clearedSize = 0;
    for(auto& sink: mSinks)
    {
        clearedSize += sink->ClearStored();
    }
    return clearedSize;
}

CScopedBlockOriginRegistry::CScopedBlockOriginRegistry(
    const uint256& hash,
    const std::string& source,
    const std::string& address,
    NodeId nodeId)
{
    std::lock_guard lock(mRegistryGuard);
    mRegistry.emplace_back(hash, InvalidTxnInfo::BlockOrigin{source, address, nodeId});
    mThisItem = std::prev(mRegistry.cend());
}

CScopedBlockOriginRegistry::~CScopedBlockOriginRegistry()
{
    std::lock_guard lock(mRegistryGuard);
    mRegistry.erase(mThisItem);
}

std::vector<InvalidTxnInfo::BlockOrigin> CScopedBlockOriginRegistry::GetOrigins(const uint256& blockHash)
{
    std::lock_guard lock(mRegistryGuard);
    std::vector<InvalidTxnInfo::BlockOrigin> origins;
    for(const auto& [hash, origin] : mRegistry)
    {
        if(blockHash == hash)
        {
            origins.push_back(origin);
        }
    }
    return origins;
}
