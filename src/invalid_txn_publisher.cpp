// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "invalid_txn_publisher.h"
#include "core_io.h"
#include "util.h"
#include "config.h"
#include "memusage.h"

#include <iomanip>
#include <sstream>
#include <regex>

#include "validationinterface.h"

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

    return totalSize;
}

void InvalidTxnInfo::PutOrigin(CJSONWriter& writer) const
{
    if(auto blockDetails = std::get_if<InvalidTxnInfo::BlockDetails>(&mDetails))
    {
        writer.pushKV("fromBlock", true);
        writer.writeBeginArray("origins");
        const InvalidTxnInfo::BlockOrigin* lastElement = &blockDetails->origins.back();
        for(const auto& origin: blockDetails->origins)
        {
            writer.writeBeginObject();

            writer.pushKV("source", origin.source);
            if(!origin.address.empty())
            {
                writer.pushKV("address", origin.address);
                writer.pushKV("nodeId", origin.nodeId, false);
            }
            writer.writeEndObject(&origin != lastElement);
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

void InvalidTxnInfo::PutTx(CJSONWriter& writer, bool writeHex) const
{
    if(auto tx = std::get_if<CTransactionRef>(&mTransaction))
    {
        writer.pushKV("txid", (*tx)->GetId().GetHex());
        writer.pushKV("size", int64_t((*tx)->GetTotalSize()));
        if(writeHex)
        {
            writer.pushK("hex");
            writer.pushQuote(true, false);
            EncodeHexTx(**tx, writer.getWriter(), 0);
            writer.pushQuote(false);
        }
    }
    else if(auto tx = std::get_if<InvalidTxnInfo::TxData>(&mTransaction))
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
    writer.pushKV("isCorruptionPossible", mTxValidationState.CorruptionPossible());
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
    writer.pushKV("rejectionTime", time, false);
}

// writes invalidTxnInfo to the writer
void InvalidTxnInfo::ToJson(CJSONWriter& writer, bool writeHex) const
{
    writer.writeBeginObject();

    PutOrigin(writer);
    PutTx(writer, writeHex);
    PutState(writer);
    PutRejectionTime(writer);

    writer.writeEndObject(false);
}

class CInvalidTxnSink
{
protected:

    static int64_t EstimateMessageSize(const InvalidTxnInfo& invalidTxnInfo, bool writeTxHex)
    {
        constexpr int64_t APPROXIMATE_SIZE_NO_HEX = 500; // roughly size of the json file without transaction hex
        if (writeTxHex)
        {
            return invalidTxnInfo.GetTotalTransactionSize() * 2 + APPROXIMATE_SIZE_NO_HEX;
        }
        return APPROXIMATE_SIZE_NO_HEX;
    }

public:

    virtual ~CInvalidTxnSink() = default;
    virtual void Publish(const InvalidTxnInfo& invalidTxnInfo) = 0;
    virtual int64_t ClearStored() { return 0;};
};

// Writes invalid transactions to disk. Each transaction to separate file (YYYY-MM-DD_HH-MM-SS_<transaction id>_<ord number>.json). 
// Keeps track of the on the overall disk usage. If the transaction is found to be invalid more than once its hex is written only in the first file.
// When disk usage limit is reached, we can delete old or ignore new transactions, depending on the policy which can be set from outside.
class CInvalidTxnFileSink : public CInvalidTxnSink
{
    bool isInitialized = false;
    int64_t maximumDiskUsed = CInvalidTxnPublisher::DEFAULT_FILE_SINK_DISK_USAGE; // maximal cumulative size of the files written to the disk
    boost::filesystem::path directory;
    InvalidTxEvictionPolicy evictionPolicy;

    std::mutex guard;
    std::map<std::string, int64_t> files;               // mapping filename to file size (taking advantage that std::map is sorted to evict first or last added file, depending on policy)
    int64_t cumulativeFilesSize = 0;                    // current size of all files written to the disk
    std::map<std::string, int> idCountMap;              // how many times we have seen any transaction (key: hex of transaction id, value: how many times it is seen)

    // Writes transaction to disk
    void SaveTransaction(const InvalidTxnInfo& invalidTxnInfo, bool doWriteHex)
    {
        std::string txid = invalidTxnInfo.GetTxnIdHex();
        std::stringstream fname;
        std::time_t t = std::time(nullptr);
        fname << std::put_time(std::gmtime(&t), "%Y-%m-%d_%H-%M-%S");
        fname << "_" << txid;
        fname << "_" << idCountMap[txid];
        fname << ".json";

        auto path = directory / fname.str();

        {
            CFileTextWriter textWriter(path.string());
            CJSONWriter jw(textWriter, true);
            invalidTxnInfo.ToJson(jw, doWriteHex);
            textWriter.Flush();
            auto err = textWriter.GetError();
            if(!err.empty())
            {
                LogPrintf("Error occurred while dumping invalid transaction to the file: " + err);
            }
        }

        auto size = boost::filesystem::file_size(path);
        cumulativeFilesSize += size;
        files[fname.str()] = size;
        idCountMap[txid] += 1;
    }

    // Checks file name and extract relevant data
    static bool ParseFilename(const std::string& fname, std::string& txid, int& ordNum1)
    {
        // YYYY-MM-DD_HH-MM-SS_<transaction id>_<ord number>.json
        static const std::regex fileRegex(
            R"(^\d{4}-\d{2}-\d{2}_\d{2}-\d{2}-\d{2}_((?:\d|[a-f]){64})_(\d+)\.json$)");

        std::smatch match;
        if (std::regex_match(fname, match, fileRegex) && match.size() > 1) {
            txid = match.str(1);
            ordNum1 = std::stoi(match.str(2));
            return true;
        }
        LogPrintf("Problematic filename: %s", fname);
        return false;
    }

    // Enumerates files on the disk to find cumulative files size and to count how many times each transaction is seen
    void FillFilesState()
    {
        boost::filesystem::directory_iterator end_itr;

        for (boost::filesystem::directory_iterator itr(directory); itr != end_itr; ++itr)
        {
            if (!boost::filesystem::is_regular_file(itr->path()))
            {
                continue;
            }

            std::string current_file = itr->path().string();
            auto filename = itr->path().filename().string();

            std::string txid;
            int count = 0;

            if (!ParseFilename(filename, txid, count))
            {
                continue;
            }

            auto fileSize = boost::filesystem::file_size(itr->path());
            files[filename] = fileSize;
            cumulativeFilesSize += fileSize;

            idCountMap[txid] = std::max(idCountMap[txid], count+1);
        }
    }

    // Deletes a file and updates file relevant data
    bool RemoveFile(const std::string fname, int64_t fsize)
    {
        std::string txid; int count = 0;
        if (!ParseFilename(fname, txid, count))
        {
            return false;
        }

        boost::system::error_code ec = {};
        boost::filesystem::remove(directory / fname, ec);
        if (ec)
        {
            LogPrintf("Failed to delete a file: %s, error: %s", fname, ec.message());
            return false;
        }

        files.erase(fname);
        idCountMap.erase(txid);
        cumulativeFilesSize -= fsize;
        return true;
    }

    // Deletes files, until cumulative files size drops below maximalSize
    bool ShrinkToSize(int64_t maximalSize)
    {
        switch(evictionPolicy)
        {
            case InvalidTxEvictionPolicy::DELETE_OLD:
            {
                while(files.begin() != files.end() && cumulativeFilesSize > maximalSize)
                {
                    auto it = files.begin();
                    RemoveFile(it->first, it->second);
                }
                break;
            }
            case InvalidTxEvictionPolicy::IGNORE_NEW:
            {
                while(files.rbegin() != files.rend() && cumulativeFilesSize > maximalSize)
                {
                    auto it = files.rbegin();
                    RemoveFile(it->first, it->second);
                }
                break;
            }
        }
        return cumulativeFilesSize <= maximalSize;
    }

    // Initialize state (executes lazy, right before first transaction is written)
    void Initialize()
    {
        if(!boost::filesystem::exists(directory))
        {
            boost::filesystem::create_directory(directory);
        }
        FillFilesState();
        ShrinkToSize(maximumDiskUsed);
        isInitialized = true;
    }

public:
    CInvalidTxnFileSink(int64_t maxDiskUsed, InvalidTxEvictionPolicy policy)
        : maximumDiskUsed(maxDiskUsed)
        , directory(GetDataDir() / "invalidtxs")
        , evictionPolicy(policy)
    {
    }

    void Publish(const InvalidTxnInfo& invalidTxnInfo) override
    {
        std::lock_guard<std::mutex> lock(guard);

        if(!isInitialized) // initialize lazily
        {
            Initialize();
        }

        std::string txid = invalidTxnInfo.GetTxnIdHex();

        bool doWriteHex = (idCountMap[txid] == 0); // write hex only if we never before received this transaction
        int64_t estimatedTxSize = EstimateMessageSize(invalidTxnInfo, doWriteHex);

        if(estimatedTxSize > maximumDiskUsed) // transaction is bigger than maximal cumulative size: try to write it without hex
        {
            doWriteHex = false;
            estimatedTxSize = EstimateMessageSize(invalidTxnInfo, doWriteHex);
        }

        int64_t missingSpace = cumulativeFilesSize + estimatedTxSize - maximumDiskUsed;
        if(missingSpace <= 0) // we have enough space
        {
            SaveTransaction(invalidTxnInfo, doWriteHex);
        }
        else if (evictionPolicy == InvalidTxEvictionPolicy::DELETE_OLD) // not enough space, make room
        {
            int64_t maxSize = std::max(maximumDiskUsed - estimatedTxSize, int64_t(0));
            if(ShrinkToSize(maxSize))
            {
                SaveTransaction(invalidTxnInfo, doWriteHex);
            }
            else
            {
                LogPrintf("Could not make enough room! Transaction not saved!");
            }
        }
    }

    int64_t ClearStored() override
    {
        std::lock_guard<std::mutex> lock(guard);
        int64_t startingSize = cumulativeFilesSize;
        if(!isInitialized)
        {
            Initialize();
        }
        ShrinkToSize(0);
        return cumulativeFilesSize - startingSize;
    }
};

#if ENABLE_ZMQ
class CInvalidTxnZmqSink : public CInvalidTxnSink
{
    int64_t maxMessageSize;
public:
    CInvalidTxnZmqSink(int64_t maxMsgSize)
        :maxMessageSize(maxMsgSize)
    {}

    void Publish(const InvalidTxnInfo& invalidTxInfo) override
    {
        auto messageSize = EstimateMessageSize(invalidTxInfo, true);
        CStringWriter tw;
        CJSONWriter jw(tw, false);
        invalidTxInfo.ToJson(jw, messageSize <= maxMessageSize);
        std::string jsonString = tw.MoveOutString();
        GetMainSignals().InvalidTxMessage(jsonString);
    }
};
#endif

CInvalidTxnPublisher::CInvalidTxnPublisher(const Config& config)
    :txInfoQueue(ONE_GIGABYTE, [](const InvalidTxnInfo& txInfo) { return txInfo.DynamicMemoryUsage(); })
{
    auto sinkNames = config.GetInvalidTxSinks();
    if (sinkNames.empty())
    {
        txInfoQueue.Close();
        return;
    }

    if (sinkNames.find( "FILE" ) != sinkNames.end())
    {
        AddFileSink(config.GetInvalidTxFileSinkMaxDiskUsage(),
                    config.GetInvalidTxFileSinkEvictionPolicy());
    }
#if ENABLE_ZMQ
    if (sinkNames.find( "ZMQ" ) != sinkNames.end())
    {
        AddZMQSink(config.GetInvalidTxZMQMaxMessageSize());
    }
#endif

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

            for (auto& sink: sinks)
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

    sinks.clear();
}

void CInvalidTxnPublisher::Publish(InvalidTxnInfo&& invalidTxnInfo)
{
    if (txInfoQueue.IsClosed())
    {
        return;
    }

    if (!txInfoQueue.PushNoWait(invalidTxnInfo))
    {
        if (!invalidTxnInfo.TruncateTransactionDetails())
        {
            return; // we are already without transaction
        }

        txInfoQueue.PushNoWait(invalidTxnInfo);
    }
}

void CInvalidTxnPublisher::AddFileSink(int64_t maxSize, InvalidTxEvictionPolicy evictionPolicy)
{
    sinks.emplace_back(new CInvalidTxnFileSink(maxSize, evictionPolicy));
}

int64_t CInvalidTxnPublisher::ClearStored()
{
    int64_t clearedSize = 0;
    for(auto& sink: sinks)
    {
        clearedSize += sink->ClearStored();
    }
    return clearedSize;
}

#if ENABLE_ZMQ
void CInvalidTxnPublisher::AddZMQSink(int64_t maxMessageSize)
{
    sinks.emplace_back(new CInvalidTxnZmqSink(maxMessageSize));
}
#endif


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
