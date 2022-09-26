// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "file_sink.h"

#include <regex>
#include <sstream>
#include <iomanip>

using namespace InvalidTxnPublisher;

void CInvalidTxnFileSink::SaveTransaction(const InvalidTxnInfo& invalidTxnInfo, bool doWriteHex)
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

bool CInvalidTxnFileSink::ParseFilename(const std::string& fname, std::string& txid, int& ordNum1)
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
    LogPrintf("Problematic filename: %s\n", fname);
    return false;
}

void CInvalidTxnFileSink::FillFilesState()
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

bool CInvalidTxnFileSink::RemoveFile(const std::string fname, int64_t fsize)
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
        LogPrintf("Failed to delete a file: %s, error: %s\n", fname, ec.message());
        return false;
    }

    files.erase(fname);
    idCountMap.erase(txid);
    cumulativeFilesSize -= fsize;
    return true;
}

bool CInvalidTxnFileSink::ShrinkToSize(int64_t maximalSize)
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

void CInvalidTxnFileSink::Initialize()
{
    if(!boost::filesystem::exists(directory))
    {
        boost::filesystem::create_directory(directory);
    }
    FillFilesState();
    ShrinkToSize(maximumDiskUsed);
    isInitialized = true;
}

void CInvalidTxnFileSink::Publish(const InvalidTxnInfo& invalidTxnInfo)
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
            LogPrintf("Could not make enough room! Transaction not saved!\n");
        }
    }
}

int64_t CInvalidTxnFileSink::ClearStored()
{
    std::lock_guard<std::mutex> lock(guard);
    if(!isInitialized)
    {
        Initialize();
    }
    int64_t startingSize = cumulativeFilesSize;
    ShrinkToSize(0);
    return startingSize - cumulativeFilesSize;
}
