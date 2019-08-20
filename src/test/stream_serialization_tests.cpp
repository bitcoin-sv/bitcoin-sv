// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "test/test_bitcoin.h"
#include "blockstreams.h"
#include "serialize.h"
#include "stream_test_helpers.h"

#include <boost/test/unit_test.hpp>

#include <thread>

BOOST_FIXTURE_TEST_SUITE(stream_serialization_tests, BasicTestingSetup)

namespace
{
    template<typename Reader = CMemoryReader>
    void CompareSerializeWithStreamingSerialization(
        const CBlock& serializable,
        size_t maxChunkSize = 5u)
    {
        std::vector<uint8_t> expectedSerializedData{Serialize(serializable)};

        CBlockStream<Reader> stream{
            expectedSerializedData,
            {SER_NETWORK, INIT_PROTO_VERSION},
            {SER_NETWORK, INIT_PROTO_VERSION}};
        std::vector<uint8_t> serializedData{
            StreamSerialize(stream, maxChunkSize)};

        BOOST_REQUIRE_EQUAL_COLLECTIONS(
            serializedData.begin(), serializedData.end(),
            expectedSerializedData.begin(), expectedSerializedData.end());
    }
} // ns

BOOST_AUTO_TEST_CASE(empty_block)
{
    CompareSerializeWithStreamingSerialization(CBlock{});
}

BOOST_AUTO_TEST_CASE(block)
{
    CompareSerializeWithStreamingSerialization(BuildRandomTestBlock());
}

BOOST_AUTO_TEST_CASE(read_big_chunks)
{
    CompareSerializeWithStreamingSerialization(BuildRandomTestBlock(), 9999999u);
}

BOOST_AUTO_TEST_CASE(exception)
{
    struct CTestExceptionReader
    {
        size_t Read(char* pch, size_t maxSize)
        {
            throw std::exception{};
        }

        bool EndOfStream() const
        {
            return false;
        }
    };

    auto runner =
        []
        {
            using namespace std::literals::chrono_literals;

            CBlockStream<CTestExceptionReader> stream{
                {},
                {SER_NETWORK, INIT_PROTO_VERSION},
                {SER_NETWORK, INIT_PROTO_VERSION}};

            auto start = std::chrono::steady_clock::now();

            // try reading from stream for max 5 seconds and in the meantime
            // exception from the other thread should be thrown
            do
            {
                auto chunk = stream.Read(5);

                BOOST_REQUIRE_EQUAL(chunk.Begin(), nullptr);
                BOOST_REQUIRE_EQUAL(chunk.Size(), 0);
                BOOST_REQUIRE_EQUAL(stream.EndOfStream(), false);
            }
            while(std::chrono::steady_clock::now() - start < 5s);
        };

    BOOST_REQUIRE_THROW(runner(), std::exception);
}

BOOST_AUTO_TEST_CASE(known_size_input)
{
    std::vector<uint8_t> expectedSerializedData{
        Serialize(BuildRandomTestBlock())};

    CFixedSizeStream stream{
        expectedSerializedData.size(),
        CMemoryReader{expectedSerializedData}};
    std::vector<uint8_t> serializedData{
        StreamSerialize(stream, 5u)};

    BOOST_REQUIRE_EQUAL_COLLECTIONS(
        serializedData.begin(), serializedData.end(),
        expectedSerializedData.begin(), expectedSerializedData.end());
}

BOOST_AUTO_TEST_SUITE_END()
