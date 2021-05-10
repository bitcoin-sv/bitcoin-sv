// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include <double_spend/dstxn_serialiser.h>

#include <clientversion.h>
#include <logging.h>
#include <streams.h>
#include <util.h>

#include <stdexcept>
#include <string>

#ifdef WIN32
#include <io.h>
#ifdef _MSC_VER
#pragma warning(disable : 4996)
#endif // _MSC_VER
#endif // WIN32

#include <fcntl.h>

namespace
{
    // Subdirectory of the main bitoind directory under which to store
    // serialised double-spend txns
    const char* DS_DIR_NAME { "dstxns" };
}

DSTxnSerialiser::TxnHandle::~TxnHandle()
{
    // Tidy up and delete our underlying file (if we have one)
    if(!mTxnFile.empty())
    {
        try
        {
            if(fs::remove(mTxnFile))
            {
                LogPrint(BCLog::DOUBLESPEND, "Deleted serialised txn file %s\n", mTxnFile.string());
            }
            else
            {
                LogPrint(BCLog::DOUBLESPEND, "Failed to delete serialised txn file %s\n", mTxnFile.string());
            }
        }
        catch(std::exception& e)
        {
            LogPrint(BCLog::DOUBLESPEND, "Error deleting serialised txn file %s : %s\n", mTxnFile.string(),
                e.what());
        }
    }
}

// Open the underlying file for reading and return a descriptor to it.
UniqueFileDescriptor DSTxnSerialiser::TxnHandle::OpenFile() const
{
    UniqueFileDescriptor fd { open(mTxnFile.string().c_str(), O_RDONLY) };
    if(fd.Get() < 0)
    {
        throw std::runtime_error("Failed to open serialised txn file for reading " + mTxnFile.string());
    }
    return fd;
}

// Get the size of our underlying file
size_t DSTxnSerialiser::TxnHandle::GetFileSize() const
{
    return fs::file_size(mTxnFile);
}

DSTxnSerialiser::DSTxnSerialiser()
: mTxnDir { GetDataDir() / DS_DIR_NAME }
{
    // Remove any remaining old data directory and re-create it
    RemoveDataDir();
    MakeDataDir();
}

DSTxnSerialiser::~DSTxnSerialiser()
{
    // Remove data directory
    RemoveDataDir();
}

// Serialise the given transaction to disk
DSTxnSerialiser::TxnHandleUPtr DSTxnSerialiser::Serialise(const CTransaction& txn)
{
    // Open file for txn
    fs::path txnFile { mTxnDir / txn.GetId().ToString() };
    FILE* filestr { fsbridge::fopen(txnFile, "wb") };
    if(!filestr)
    {   
        throw std::runtime_error("Failed to create serialised txn file " + txnFile.string());
    }

    // Create the handle now so that if there's an error serialising we'll still delete the file
    TxnHandleUPtr handle { std::make_unique<TxnHandle>(txnFile) };

    // Serialise txn
    CAutoFile file { filestr, SER_DISK, CLIENT_VERSION };
    file << txn;
    FileCommit(file.Get());

    return handle;
}

// Create our working data dir
void DSTxnSerialiser::MakeDataDir() const
{
    if(fs::create_directories(mTxnDir))
    {
        LogPrint(BCLog::DOUBLESPEND, "Created double-spend txns directory %s\n", mTxnDir.string());
    }
    else
    {
        throw std::runtime_error("Failed to create double-spend txns directory " + mTxnDir.string());
    }
}

// Remove our working data dir
void DSTxnSerialiser::RemoveDataDir() const
{
    try
    {
        if(fs::remove_all(mTxnDir) > 0)
        {
            LogPrint(BCLog::DOUBLESPEND, "Removed double-spend txns directory %s\n", mTxnDir.string());
        }
    }
    catch(std::exception& e)
    {
        LogPrint(BCLog::DOUBLESPEND, "Error removing double-spend txns directory %s : %s\n",
            mTxnDir.string(), e.what());
    }
}

