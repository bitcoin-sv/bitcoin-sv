// Copyright (c) 2024 BSV Blockchain Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "protocol_era.h"
#include "chainparams.h"
#include "coins.h"
#include "config.h"
#include "enum_cast.h"

#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>

#include <iostream>

// Enable printing of ProtocolEra
const enumTableT<ProtocolEra>& enumTable(ProtocolEra)
{   
    static enumTableT<ProtocolEra> table
    {   
        { ProtocolEra::Unknown,       "Unknown" },
        { ProtocolEra::PreGenesis,    "Genesis" },
        { ProtocolEra::PostGenesis,   "Genesis" },
        { ProtocolEra::PostChronicle, "Chronicle" }
    };
    return table;
}

std::ostream& operator<<(std::ostream& str, ProtocolEra era)
{
    str << enum_cast<std::string>(era);
    return str;
}

BOOST_FIXTURE_TEST_SUITE(protocol_eras, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(status_checks)
{
    // Setup config for testing
    const int32_t genesisHeight { testConfig.GetChainParams().GetConsensus().genesisHeight };
    const int32_t chronicleHeight { testConfig.GetChainParams().GetConsensus().chronicleHeight };
    BOOST_CHECK(testConfig.SetGenesisActivationHeight(genesisHeight));
    BOOST_CHECK(testConfig.SetChronicleActivationHeight(chronicleHeight));

    // Check basic height activation
    BOOST_CHECK_EQUAL(GetProtocolEra(testConfig, genesisHeight - 1), ProtocolEra::PreGenesis);
    BOOST_CHECK(! IsProtocolActive(GetProtocolEra(testConfig, genesisHeight - 1), ProtocolName::Genesis));
    BOOST_CHECK(! IsProtocolActive(GetProtocolEra(testConfig, genesisHeight - 1), ProtocolName::Chronicle));
    BOOST_CHECK_EQUAL(GetProtocolEra(testConfig, genesisHeight), ProtocolEra::PostGenesis);
    BOOST_CHECK(IsProtocolActive(GetProtocolEra(testConfig, genesisHeight), ProtocolName::Genesis));
    BOOST_CHECK(! IsProtocolActive(GetProtocolEra(testConfig, genesisHeight), ProtocolName::Chronicle));
    BOOST_CHECK_EQUAL(GetProtocolEra(testConfig, genesisHeight + 1), ProtocolEra::PostGenesis);
    BOOST_CHECK(IsProtocolActive(GetProtocolEra(testConfig, genesisHeight + 1), ProtocolName::Genesis));
    BOOST_CHECK(! IsProtocolActive(GetProtocolEra(testConfig, genesisHeight + 1), ProtocolName::Chronicle));

    BOOST_CHECK_EQUAL(GetProtocolEra(testConfig, chronicleHeight - 1), ProtocolEra::PostGenesis);
    BOOST_CHECK(! IsProtocolActive(GetProtocolEra(testConfig, chronicleHeight - 1), ProtocolName::Chronicle));
    BOOST_CHECK(IsProtocolActive(GetProtocolEra(testConfig, chronicleHeight - 1), ProtocolName::Genesis));
    BOOST_CHECK_EQUAL(GetProtocolEra(testConfig, chronicleHeight), ProtocolEra::PostChronicle);
    BOOST_CHECK(IsProtocolActive(GetProtocolEra(testConfig, chronicleHeight), ProtocolName::Chronicle));
    BOOST_CHECK(IsProtocolActive(GetProtocolEra(testConfig, chronicleHeight), ProtocolName::Genesis));
    BOOST_CHECK_EQUAL(GetProtocolEra(testConfig, chronicleHeight + 1), ProtocolEra::PostChronicle);
    BOOST_CHECK(IsProtocolActive(GetProtocolEra(testConfig, chronicleHeight + 1), ProtocolName::Chronicle));
    BOOST_CHECK(IsProtocolActive(GetProtocolEra(testConfig, chronicleHeight + 1), ProtocolName::Genesis));

    /* Test UTXO height activation */

    {
        // Test genesis activation for mined UTXO
        CoinWithScript coinBefore { CoinWithScript::MakeOwning(CTxOut{Amount{1}, {}}, genesisHeight - 1, false, false) };
        CoinWithScript coinOn { CoinWithScript::MakeOwning(CTxOut{Amount{1}, {}}, genesisHeight, false, false) };
        CoinWithScript coinAfter { CoinWithScript::MakeOwning(CTxOut{Amount{1}, {}}, genesisHeight + 1, false, false) };

        // Mempool height parameter is irrelevant for coin with non-MEMPOOL_HEIGHT
        BOOST_CHECK_EQUAL(GetProtocolEra(testConfig, coinBefore, genesisHeight), ProtocolEra::PreGenesis);
        BOOST_CHECK(! IsProtocolActive(GetProtocolEra(testConfig, coinBefore, genesisHeight), ProtocolName::Genesis));
        BOOST_CHECK(! IsProtocolActive(GetProtocolEra(testConfig, coinBefore, genesisHeight), ProtocolName::Chronicle));
        BOOST_CHECK_EQUAL(GetProtocolEra(testConfig, coinOn, genesisHeight), ProtocolEra::PostGenesis);
        BOOST_CHECK(IsProtocolActive(GetProtocolEra(testConfig, coinOn, genesisHeight), ProtocolName::Genesis));
        BOOST_CHECK(! IsProtocolActive(GetProtocolEra(testConfig, coinOn, genesisHeight), ProtocolName::Chronicle));
        BOOST_CHECK_EQUAL(GetProtocolEra(testConfig, coinAfter, genesisHeight), ProtocolEra::PostGenesis);
        BOOST_CHECK(IsProtocolActive(GetProtocolEra(testConfig, coinAfter, genesisHeight), ProtocolName::Genesis));
        BOOST_CHECK(! IsProtocolActive(GetProtocolEra(testConfig, coinAfter, genesisHeight), ProtocolName::Chronicle));
    }
    {
        // Test genesis activation for mempool UTXO
        CoinWithScript coinMempool { CoinWithScript::MakeOwning(CTxOut{Amount{1}, {}}, MEMPOOL_HEIGHT, false, false) };
        BOOST_CHECK_EQUAL(GetProtocolEra(testConfig, coinMempool, genesisHeight - 1), ProtocolEra::PreGenesis);
        BOOST_CHECK(! IsProtocolActive(GetProtocolEra(testConfig, coinMempool, genesisHeight - 1), ProtocolName::Genesis));
        BOOST_CHECK(! IsProtocolActive(GetProtocolEra(testConfig, coinMempool, genesisHeight - 1), ProtocolName::Chronicle));
        BOOST_CHECK_EQUAL(GetProtocolEra(testConfig, coinMempool, genesisHeight), ProtocolEra::PostGenesis);
        BOOST_CHECK(IsProtocolActive(GetProtocolEra(testConfig, coinMempool, genesisHeight), ProtocolName::Genesis));
        BOOST_CHECK(! IsProtocolActive(GetProtocolEra(testConfig, coinMempool, genesisHeight), ProtocolName::Chronicle));
        BOOST_CHECK_EQUAL(GetProtocolEra(testConfig, coinMempool, genesisHeight + 1), ProtocolEra::PostGenesis);
        BOOST_CHECK(IsProtocolActive(GetProtocolEra(testConfig, coinMempool, genesisHeight + 1), ProtocolName::Genesis));
        BOOST_CHECK(! IsProtocolActive(GetProtocolEra(testConfig, coinMempool, genesisHeight + 1), ProtocolName::Chronicle));
    }

    {
        // Test chronicle activation for mined UTXO
        CoinWithScript coinBefore { CoinWithScript::MakeOwning(CTxOut{Amount{1}, {}}, chronicleHeight - 1, false, false) };
        CoinWithScript coinOn { CoinWithScript::MakeOwning(CTxOut{Amount{1}, {}}, chronicleHeight, false, false) };
        CoinWithScript coinAfter { CoinWithScript::MakeOwning(CTxOut{Amount{1}, {}}, chronicleHeight + 1, false, false) };

        // Mempool height parameter is irrelevant for coin with non-MEMPOOL_HEIGHT
        BOOST_CHECK_EQUAL(GetProtocolEra(testConfig, coinBefore, chronicleHeight), ProtocolEra::PostGenesis);
        BOOST_CHECK(IsProtocolActive(GetProtocolEra(testConfig, coinBefore, chronicleHeight), ProtocolName::Genesis));
        BOOST_CHECK(! IsProtocolActive(GetProtocolEra(testConfig, coinBefore, chronicleHeight), ProtocolName::Chronicle));
        BOOST_CHECK_EQUAL(GetProtocolEra(testConfig, coinOn, chronicleHeight), ProtocolEra::PostChronicle);
        BOOST_CHECK(IsProtocolActive(GetProtocolEra(testConfig, coinOn, chronicleHeight), ProtocolName::Genesis));
        BOOST_CHECK(IsProtocolActive(GetProtocolEra(testConfig, coinOn, chronicleHeight), ProtocolName::Chronicle));
        BOOST_CHECK_EQUAL(GetProtocolEra(testConfig, coinAfter, chronicleHeight), ProtocolEra::PostChronicle);
        BOOST_CHECK(IsProtocolActive(GetProtocolEra(testConfig, coinAfter, chronicleHeight), ProtocolName::Genesis));
        BOOST_CHECK(IsProtocolActive(GetProtocolEra(testConfig, coinAfter, chronicleHeight), ProtocolName::Chronicle));
    }
    {
        // Test chronicle activation for mempool UTXO
        CoinWithScript coinMempool { CoinWithScript::MakeOwning(CTxOut{Amount{1}, {}}, MEMPOOL_HEIGHT, false, false) };
        BOOST_CHECK_EQUAL(GetProtocolEra(testConfig, coinMempool, chronicleHeight - 1), ProtocolEra::PostGenesis);
        BOOST_CHECK(IsProtocolActive(GetProtocolEra(testConfig, coinMempool, chronicleHeight - 1), ProtocolName::Genesis));
        BOOST_CHECK(! IsProtocolActive(GetProtocolEra(testConfig, coinMempool, chronicleHeight - 1), ProtocolName::Chronicle));
        BOOST_CHECK_EQUAL(GetProtocolEra(testConfig, coinMempool, chronicleHeight), ProtocolEra::PostChronicle);
        BOOST_CHECK(IsProtocolActive(GetProtocolEra(testConfig, coinMempool, chronicleHeight), ProtocolName::Genesis));
        BOOST_CHECK(IsProtocolActive(GetProtocolEra(testConfig, coinMempool, chronicleHeight), ProtocolName::Chronicle));
        BOOST_CHECK_EQUAL(GetProtocolEra(testConfig, coinMempool, chronicleHeight + 1), ProtocolEra::PostChronicle);
        BOOST_CHECK(IsProtocolActive(GetProtocolEra(testConfig, coinMempool, chronicleHeight + 1), ProtocolName::Genesis));
        BOOST_CHECK(IsProtocolActive(GetProtocolEra(testConfig, coinMempool, chronicleHeight + 1), ProtocolName::Chronicle));
    }
}

BOOST_AUTO_TEST_CASE(grace_period_tests)
{
    // Setup config for testing
    const int32_t genesisHeight { testConfig.GetChainParams().GetConsensus().genesisHeight };
    const int32_t chronicleHeight { testConfig.GetChainParams().GetConsensus().chronicleHeight };
    BOOST_CHECK(testConfig.SetGenesisActivationHeight(genesisHeight));
    BOOST_CHECK(testConfig.SetChronicleActivationHeight(chronicleHeight));

    // Check Genesis grace period detection
    BOOST_CHECK(! InProtocolGracePeriod(testConfig, ProtocolName::Genesis, genesisHeight - DEFAULT_GENESIS_GRACEFUL_ACTIVATION_PERIOD));
    BOOST_CHECK(InProtocolGracePeriod(testConfig, ProtocolName::Genesis, genesisHeight - DEFAULT_GENESIS_GRACEFUL_ACTIVATION_PERIOD + 1));
    BOOST_CHECK(InProtocolGracePeriod(testConfig, ProtocolName::Genesis, genesisHeight));
    BOOST_CHECK(InProtocolGracePeriod(testConfig, ProtocolName::Genesis, genesisHeight + DEFAULT_GENESIS_GRACEFUL_ACTIVATION_PERIOD - 1));
    BOOST_CHECK(! InProtocolGracePeriod(testConfig, ProtocolName::Genesis, genesisHeight + DEFAULT_GENESIS_GRACEFUL_ACTIVATION_PERIOD));

    // Check Chronicle grace period detection
    BOOST_CHECK(! InProtocolGracePeriod(testConfig, ProtocolName::Chronicle, chronicleHeight - DEFAULT_CHRONICLE_GRACEFUL_ACTIVATION_PERIOD));
    BOOST_CHECK(InProtocolGracePeriod(testConfig, ProtocolName::Chronicle, chronicleHeight - DEFAULT_CHRONICLE_GRACEFUL_ACTIVATION_PERIOD + 1));
    BOOST_CHECK(InProtocolGracePeriod(testConfig, ProtocolName::Chronicle, chronicleHeight));
    BOOST_CHECK(InProtocolGracePeriod(testConfig, ProtocolName::Chronicle, chronicleHeight + DEFAULT_CHRONICLE_GRACEFUL_ACTIVATION_PERIOD - 1));
    BOOST_CHECK(! InProtocolGracePeriod(testConfig, ProtocolName::Chronicle, chronicleHeight + DEFAULT_CHRONICLE_GRACEFUL_ACTIVATION_PERIOD));
}

BOOST_AUTO_TEST_CASE(inverse_protocol_eras)
{
    // Test for Genesis grace period
    BOOST_CHECK_EQUAL(GetInverseProtocolEra(ProtocolEra::PreGenesis, ProtocolName::Genesis), ProtocolEra::PostGenesis);
    BOOST_CHECK_EQUAL(GetInverseProtocolEra(ProtocolEra::PostGenesis, ProtocolName::Genesis), ProtocolEra::PreGenesis);
    BOOST_CHECK_THROW(GetInverseProtocolEra(ProtocolEra::PostChronicle, ProtocolName::Genesis), std::runtime_error);

    // Test for Chronicle grace period
    BOOST_CHECK_EQUAL(GetInverseProtocolEra(ProtocolEra::PostGenesis, ProtocolName::Chronicle), ProtocolEra::PostChronicle);
    BOOST_CHECK_EQUAL(GetInverseProtocolEra(ProtocolEra::PostChronicle, ProtocolName::Chronicle), ProtocolEra::PostGenesis);
    BOOST_CHECK_THROW(GetInverseProtocolEra(ProtocolEra::PreGenesis, ProtocolName::Chronicle), std::runtime_error);
}

BOOST_AUTO_TEST_SUITE_END()

