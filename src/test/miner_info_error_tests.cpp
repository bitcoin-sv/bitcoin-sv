// Copyright (c) 2022 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#include <array>
#include <string>

#include <boost/test/unit_test.hpp>

#include "miner_id/miner_info_error.h"

#include "script/script.h"

using namespace std;

BOOST_AUTO_TEST_SUITE(miner_info_error_tests)

BOOST_AUTO_TEST_CASE(miner_info_error__op_insertion)
{
    const array<string, 27> expected{
        "miner info ref not found",
        "invalid instruction",
        "unsupported version",
        "invalid txid length",
        "invalid hash(modified merkle root || previous block hash) length",
        "invalid signature length",
        "txid not found",
        "script output not found",
        "doc parse error - ill-formed json",
        "doc parse error - missing fields",
        "doc parse error - invalid string type",
        "doc parse error - invalid number type",
        "doc parse error - unsupported version",
        "doc parse error - invalid height",
        "doc parse error - invalid minerId",
        "doc parse error - invalid prevMinerId",
        "doc parse error - invalid prevMinerId signature",
        "doc parse error - invalid revocationKey",
        "doc parse error - invalid prevRevocationKey",
        "doc parse error - invalid revocationMessageSig",
        "doc parse error - revocation msg fields",
        "doc parse error - revocation msg field",
        "doc parse error - revocation msg key",
        "doc parse error - revocation msg sig1 field missing",
        "doc parse error - revocation msg sig1 invalid value",
        "doc parse error - revocation msg sig2 field missing",
        "doc parse error - revocation msg sig2 invalid value",
    };

    const int size = static_cast<int>(miner_info_error::size);
    for(int i{}; i < size; ++i)
    {
        miner_info_error e{i};
        ostringstream oss;
        oss << e;
        const string& exp{expected[i]};
        BOOST_CHECK_EQUAL(exp, oss.str());
    }
}

BOOST_AUTO_TEST_SUITE_END()

