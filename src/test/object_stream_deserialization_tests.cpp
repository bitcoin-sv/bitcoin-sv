// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "test/test_bitcoin.h"
#include "blockstreams.h"
#include "serialize.h"
#include "stream_test_helpers.h"

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(object_stream_deserialization_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(block)
{
    CBlock block{BuildRandomTestBlock()};
    std::vector<uint8_t> serializedData{Serialize(block)};

    CBlockStreamReader<CMemoryReader> stream{
        serializedData,
        {SER_NETWORK, INIT_PROTO_VERSION}};

    BOOST_REQUIRE_EQUAL(
        stream.GetRemainingTransactionsCount(),
        block.vtx.size());

    size_t itemCounter = 0;
    do
    {
        // read transaction for counting but ignore the result as we are not
        // interested in the content
        [[maybe_unused]]
        const CTransaction& transaction = stream.ReadTransaction();

        ++itemCounter;
    } while(!stream.EndOfStream());

    BOOST_REQUIRE_EQUAL(itemCounter, 3U);
}

BOOST_AUTO_TEST_SUITE_END()
