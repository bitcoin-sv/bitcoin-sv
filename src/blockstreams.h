// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#ifndef BITCOIN_BLOCKSTREAMS_H
#define BITCOIN_BLOCKSTREAMS_H

#include "primitives/block.h"
#include "consensus/consensus.h"
#include "streams.h"
#include "chain.h"
#include "hash.h"
#include "util.h"

#include <memory>
#include <vector>

/**
 * Helper class for encapsulating version and type pair transfer to
 * serializers/unserializers
 */
struct CStreamVersionAndType
{
    const int version;
    const int type;
};

/**
 * Stream class for on the fly userialization of CBlock and retrieval of
 * CBlockHeader and CTransaction instances.
 * CBlockHeader can be accessed at any time while CTransaction instances are
 * dropped immediately after the next call to ReadTransaction which reads the
 * next block transaction.
 */
template<typename Reader>
class CBlockStreamReader
{
public:
    /**
     * @param disk_block_pos Which block is being read. Used for logging.
     */
    CBlockStreamReader(Reader&& reader, const CStreamVersionAndType& version, bool calculateDiskBlockMetadata=false, const CDiskBlockPos& disk_block_pos=CDiskBlockPos())
        : mCalculateDiskBlockMetadata(calculateDiskBlockMetadata)
        , mReader{std::move(reader)}
        , mDeserializationStream{this, version.type, version.version}
        , disk_block_pos(disk_block_pos)
    {
        mDeserializationStream >> mBlockHeader;
        mRemainingTransactionsCounter = ReadCompactSize(mDeserializationStream);
    }

    CBlockStreamReader(CBlockStreamReader&&) = delete;
    CBlockStreamReader& operator=(CBlockStreamReader&&) = delete;
    CBlockStreamReader(const CBlockStreamReader&) = delete;
    CBlockStreamReader& operator=(const CBlockStreamReader&) = delete;

    const CBlockHeader& GetBlockHeader() const
    {
        return mBlockHeader;
    }

    /**
     * Returns reference to last read transaction and transfers the ownership
     * of CTransaction object to the caller
     * 
     * It will return null if ReadTransaction()/ReadTransaction_NoThrow() was never called before or if this
     * method is called more than once after the last call to ReadTransaction()/ReadTransaction_NoThrow().
     */
    CTransactionRef GetLastTransactionRef()
    {
        return CTransactionRef(std::move(mTransaction));
    }

    size_t GetRemainingTransactionsCount() const
    {
        return mRemainingTransactionsCounter;
    }

    const CTransaction& ReadTransaction()
    {
        if (EndOfStream())
        {
            throw std::runtime_error("End of stream!");
        }

        mDeserializationStream >> mTransaction;
        --mRemainingTransactionsCounter;

        // After reading the last transactions, we can finalize the hash.
        if (EndOfStream() && mCalculateDiskBlockMetadata) {
            hasher.Finalize(reinterpret_cast<uint8_t*>(&diskBlockMetaData.diskDataHash));
        }

        return *mTransaction;
    }

    /**
     * Same as ReadTransaction() except that nullptr is returned instead of throwing an exception
     * if reading or deserializing transaction from stream fails.
     */
    const CTransaction* ReadTransaction_NoThrow() noexcept
    {
        try
        {
            return &this->ReadTransaction();
        }
        catch(const std::exception &e)
        {
            error("%s: Deserialize or I/O error - %s at %s", __func__,
                e.what(), disk_block_pos.ToString());
        }
        return nullptr;
    }

    bool EndOfStream() const
    {
        return mRemainingTransactionsCounter == 0;
    }

    /**
     * INTERNAL USE ONLY!
     * Function is used by mDeserializationStream through serialize.h functions
     * so it is not feasible to move it to private region.
     */
    void read(char* pch, size_t size)
    {
        auto read = mReader.Read(pch, size);

        if(read != size)
        {
            throw std::runtime_error("Unexpected end of stream!");

        }

        if (mCalculateDiskBlockMetadata) 
        {
            updateDiskBlockMetadata(reinterpret_cast<uint8_t*>(pch), size);
        }
    }

    const CDiskBlockMetaData& getDiskBlockMetadata()
    {
        if (EndOfStream() && mCalculateDiskBlockMetadata)
        {
            return diskBlockMetaData;
        }

        throw std::runtime_error("Cannot access disk block metadata while block is still being read!");
    }

private:
    CHash256 hasher;
    CDiskBlockMetaData diskBlockMetaData;
    bool mCalculateDiskBlockMetadata;

    void updateDiskBlockMetadata(uint8_t* begin, size_t chunkSize)
    {
        hasher.Write(begin, chunkSize);
        diskBlockMetaData.diskDataSize += chunkSize;
    }

    Reader mReader;
    OverrideStream<CBlockStreamReader> mDeserializationStream;

    size_t mRemainingTransactionsCounter;
    CBlockHeader mBlockHeader;
    std::unique_ptr<const CTransaction> mTransaction;
    CDiskBlockPos disk_block_pos;
};

/**
 * Stream class for on the fly userialization and re-serialization
 * of CBlock classes for which we don't know the final data length.
 * Class doesn't hold the entire CBlock in memory but rather streams
 * the data bit by bit.
 */
template<typename Reader>
class CBlockStream : public CForwardReadonlyStream
{
public:
    CBlockStream(
        Reader&& reader,
        const CStreamVersionAndType& inputVersionAndType,
        const CStreamVersionAndType& outputVersionAndType)
        : mBlockReader{std::move(reader), inputVersionAndType}
        , mOutputVersionAndType{outputVersionAndType}
    {
        CVectorWriter writer{
            mOutputVersionAndType.type,
            mOutputVersionAndType.version,
            mBuffer,
            0,
            mBlockReader.GetBlockHeader()};
        WriteCompactSize(writer, mBlockReader.GetRemainingTransactionsCount());
    }

    CBlockStream(CBlockStream&&) = delete;
    CBlockStream& operator=(CBlockStream&&) = delete;
    CBlockStream(const CBlockStream&) = delete;
    CBlockStream& operator=(const CBlockStream&) = delete;

    bool EndOfStream() const override
    {
        return
            mBlockReader.EndOfStream() &&
            (mBuffer.size() - mLastChunkSize == 0);
    }

    CSpan Read(size_t maxSize) override
    {
        if (!EndOfStream())
        {
            if (mLastChunkSize)
            {
                mBuffer.erase(
                    mBuffer.begin(),
                    std::next(mBuffer.begin(), mLastChunkSize));
            }

            if (mBuffer.size() < maxSize && !mBlockReader.EndOfStream())
            {
                CVectorWriter{
                    mOutputVersionAndType.type,
                    mOutputVersionAndType.version,
                    mBuffer,
                    mBuffer.size(),
                    mBlockReader.ReadTransaction()};
            }

            mLastChunkSize = std::min(mBuffer.size(), maxSize);

            if (mLastChunkSize)
            {
                return {mBuffer.data(), mLastChunkSize};
            }
        }

        return {};
    }

private:
    CBlockStreamReader<Reader> mBlockReader;
    CStreamVersionAndType mOutputVersionAndType;
    std::vector<uint8_t> mBuffer;
    size_t mLastChunkSize = 0u;
};

#endif // BITCOIN_BLOCKSTREAMS_H
