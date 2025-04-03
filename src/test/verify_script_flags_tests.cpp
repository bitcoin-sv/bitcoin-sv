// Copyright (c) 2025 BSV Blockchain Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.
#include "verify_script_flags.h"

#include <boost/test/unit_test.hpp>

#include "consensus/params.h"
#include "protocol_era.h"

BOOST_AUTO_TEST_SUITE(verify_script_flags_tests)

BOOST_AUTO_TEST_CASE(get_script_verify_flags)
{
    const bool require_standard{};
    const auto flags = GetScriptVerifyFlags(ProtocolEra::PreGenesis,
                                            require_standard);
    BOOST_CHECK_EQUAL(0x1'47df, flags);
}

BOOST_AUTO_TEST_CASE(get_block_script_flags)
{
    Consensus::Params params{};
    const int32_t height{};
    const auto flags = GetBlockScriptFlags(params,
                                           height,
                                           ProtocolEra::PreGenesis);
    BOOST_CHECK_EQUAL(0x1'460f, flags);
}

BOOST_AUTO_TEST_SUITE_END()
