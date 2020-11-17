// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include <map>
#include <mutex>
#include <string>
#include <boost/filesystem.hpp>

#include "invalid_txn_publisher.h"
#include "util.h"

namespace InvalidTxnPublisher
{
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
        void SaveTransaction(const InvalidTxnInfo& invalidTxnInfo, bool doWriteHex);

        // Checks file name and extract relevant data
        static bool ParseFilename(const std::string& fname, std::string& txid, int& ordNum1);

        // Enumerates files on the disk to find cumulative files size and to count how many times each transaction is seen
        void FillFilesState();

        // Deletes a file and updates file relevant data
        bool RemoveFile(const std::string fname, int64_t fsize);

        // Deletes files, until cumulative files size drops below maximalSize
        bool ShrinkToSize(int64_t maximalSize);

        // Initialize state (executes lazy, right before first transaction is written)
        void Initialize();

    public:
        CInvalidTxnFileSink(int64_t maxDiskUsed, InvalidTxEvictionPolicy policy)
            : maximumDiskUsed(maxDiskUsed)
            , directory(GetDataDir() / "invalidtxs")
            , evictionPolicy(policy)
        {
        }

        void Publish(const InvalidTxnInfo& invalidTxnInfo) override;

        int64_t ClearStored() override;
    };
}