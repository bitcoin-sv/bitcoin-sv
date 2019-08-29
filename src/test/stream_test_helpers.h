// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#ifndef BITCOIN_STREAM_TEST_HELPERS_H
#define BITCOIN_STREAM_TEST_HELPERS_H

#include "streams.h"
#include "primitives/block.h"
#include "consensus/merkle.h"
#include "pow.h"
#include "uint256.h"

#include <vector>
#include <chrono>
#include <type_traits>

template<typename T>
std::vector<uint8_t> Serialize(const T& serializable)
{
    std::vector<uint8_t> serializedData;
    CVectorWriter{
        SER_NETWORK,
        INIT_PROTO_VERSION,
        serializedData,
        0,
        serializable};

    return serializedData;
}

template<typename Serializer>
std::vector<uint8_t> StreamSerialize(
    Serializer& serializer,
    size_t maxChunkSize)
{
    std::vector<uint8_t> serializedData;
    auto runStart = std::chrono::steady_clock::now();

    do
    {
        using namespace std::chrono_literals;

        if ((std::chrono::steady_clock::now() - runStart) > 5s)
        {
            throw std::runtime_error("Test took too long");
        }

        auto chunk = serializer.Read(maxChunkSize);

        serializedData.insert(
            serializedData.end(),
            chunk.Begin(), chunk.Begin() + chunk.Size());
    }
    while(!serializer.EndOfStream());

    return serializedData;
}

inline std::vector<uint8_t> SerializeAsyncStream(
    CForwardAsyncReadonlyStream& serializer,
    size_t maxChunkSize)
{
    std::vector<uint8_t> serializedData;
    auto runStart = std::chrono::steady_clock::now();

    do
    {
        using namespace std::chrono_literals;

        if ((std::chrono::steady_clock::now() - runStart) > 5s)
        {
            throw std::runtime_error("Test took too long");
        }

        auto chunk = serializer.ReadAsync(maxChunkSize);

        serializedData.insert(
            serializedData.end(),
            chunk.Begin(), chunk.Begin() + chunk.Size());
    }
    while(!serializer.EndOfStream());

    return serializedData;
}

class CMemoryReader
{
public:
    CMemoryReader(const std::vector<uint8_t>& source)
        : mSourceBuffer{source}
    {/**/}

    size_t Read(char* pch, size_t maxSize)
    {
        size_t copiedSize =
            (
                mSourceBuffer.size() - mSourcePosition > maxSize
                ? maxSize
                : mSourceBuffer.size() - mSourcePosition
            );
        std::memcpy(
            pch,
            mSourceBuffer.data() + mSourcePosition,
            copiedSize);
        mSourcePosition += copiedSize;

        return copiedSize;
    }

    bool EndOfStream() const
    {
        return mSourceBuffer.size() == mSourcePosition;
    }

private:
    const std::vector<uint8_t>& mSourceBuffer;
    size_t mSourcePosition = 0u;
};


inline CBlock BuildRandomTestBlock()
{
    CBlock block;
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].scriptSig.resize(10);
    tx.vout.resize(1);
    tx.vout[0].nValue = Amount(42);

    block.vtx.resize(3);
    block.vtx[0] = MakeTransactionRef(tx); // txIn == 1, txOut == 1
    block.nVersion = 42;
    block.hashPrevBlock = InsecureRand256();
    block.nBits = 0x207fffff;

    tx.vin[0].prevout = COutPoint(InsecureRand256(), 0);
    block.vtx[1] = MakeTransactionRef(tx); // txIn == 1, txOut == 1

    tx.vin.resize(1000);
    for (size_t i = 0; i < tx.vin.size(); i++)
    {
        tx.vin[i].prevout = COutPoint(InsecureRand256(), 0);
    }
    block.vtx[2] = MakeTransactionRef(tx); // txIn == 1000, txOut == 1

    bool mutated;
    block.hashMerkleRoot = BlockMerkleRoot(block, &mutated);

    return block;
}

#endif // BITCOIN_STREAM_TEST_HELPERS_H