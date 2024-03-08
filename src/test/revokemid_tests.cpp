// Copyright (c) 2022 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "miner_id/revokemid.h"
#include "key.h"

#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>

namespace
{
    // Make revokemid message with random keys
    RevokeMid MakeRevokeMid(bool makeWithSig = false)
    {
        // Create some keys
        CKey revocationKey {};
        revocationKey.MakeNewKey(true);
        CKey minerIdKey {};
        minerIdKey.MakeNewKey(true);
        CKey minerIdToRevoke {};
        minerIdToRevoke.MakeNewKey(true);

        if(makeWithSig)
        {
            // Hex encode revocation message
            const std::string hex { HexStr(minerIdToRevoke.GetPubKey()) };
            std::vector<uint8_t> encodedRevocationMessage {};
            transform_hex(hex, back_inserter(encodedRevocationMessage));
            assert(encodedRevocationMessage.size() == 33);

            // Hash revocation message
            uint8_t hashRevocationMessageBytes[CSHA256::OUTPUT_SIZE] {};
            CSHA256().Write(reinterpret_cast<const uint8_t*>(encodedRevocationMessage.data()), encodedRevocationMessage.size()).Finalize(hashRevocationMessageBytes);
            const uint256 hashRevocationMessage { std::vector<uint8_t> {std::begin(hashRevocationMessageBytes), std::end(hashRevocationMessageBytes)} };

            // Create signatures over hash of revocation message
            std::vector<uint8_t> sig1 {};
            std::vector<uint8_t> sig2 {};
            revocationKey.Sign(hashRevocationMessage, sig1);
            minerIdKey.Sign(hashRevocationMessage, sig2);

            // Create revokemid msg
            return { revocationKey.GetPubKey(), minerIdKey.GetPubKey(), minerIdToRevoke.GetPubKey(), sig1, sig2 };
        }
        else
        {
            // Create revokemid msg
            return { revocationKey, minerIdKey, minerIdToRevoke.GetPubKey() };
        }
    }

    // For ID only
    struct revokemid_tests;
}

// RevokeMid class inspection
template<>
struct RevokeMid::UnitTestAccess<revokemid_tests>
{
    static void MakeBadRevokeKeySig(RevokeMid& msg)
    {
        msg.mEncodedRevocationMessageSig[msg.mEncodedRevocationMessageSig.size() - 5] += 1;

        // Serialise/deserialise to put bad signature in msg object
        CDataStream ss { SER_NETWORK, 0 };
        ss << msg;
        ss >> msg;
    }

    static void MakeBadMinerIdKeySig(RevokeMid& msg)
    {
        msg.mEncodedRevocationMessageSig[5] += 1;

        // Serialise/deserialise to put bad signature in msg object
        CDataStream ss { SER_NETWORK, 0 };
        ss << msg;
        ss >> msg;
    }
};
using UnitTestAccess = RevokeMid::UnitTestAccess<revokemid_tests>;

BOOST_FIXTURE_TEST_SUITE(revokemid, BasicTestingSetup)

// Default construction
BOOST_AUTO_TEST_CASE(default_construction)
{
    RevokeMid msg {};

    // Check all fields null
    BOOST_CHECK_EQUAL(msg.GetVersion(), 0U);
    BOOST_CHECK(! msg.GetRevocationKey().IsValid());
    BOOST_CHECK(! msg.GetMinerId().IsValid());
    BOOST_CHECK(! msg.GetRevocationMessage().IsValid());
    BOOST_CHECK(msg.GetSig1().empty());
    BOOST_CHECK(msg.GetSig2().empty());
}

// Construct from real keys
BOOST_AUTO_TEST_CASE(key_construction)
{
    auto Check = [](bool makeWithSig) {
        // Create revokemid msg
        RevokeMid msg { MakeRevokeMid(makeWithSig) };

        // Check field sizes and contents
        BOOST_CHECK(msg.GetRevocationKey().IsValid());
        BOOST_CHECK_EQUAL(msg.GetEncodedRevocationKey().size(), 33U);
        BOOST_CHECK(msg.GetMinerId().IsValid());
        BOOST_CHECK_EQUAL(msg.GetEncodedMinerId().size(), 33U);
        BOOST_CHECK(msg.GetRevocationMessage().IsValid());
        BOOST_CHECK_EQUAL(msg.GetEncodedRevocationMessage().size(), 33U);

        size_t sig1Size { msg.GetSig1().size() };
        size_t sig2Size { msg.GetSig2().size() };
        BOOST_CHECK_EQUAL(msg.GetEncodedRevocationMessageSig().size(), sig1Size + sig2Size + 2);

        // Check signatures verify
        BOOST_CHECK(msg.VerifySignatures());

        // Check bad signature fails to verify
        msg = MakeRevokeMid(makeWithSig);
        UnitTestAccess::MakeBadRevokeKeySig(msg);
        BOOST_CHECK(! msg.VerifySignatures());
        msg = MakeRevokeMid(makeWithSig);
        UnitTestAccess::MakeBadMinerIdKeySig(msg);
        BOOST_CHECK(! msg.VerifySignatures());
    };

    Check(false);
    Check(true);
}

// Serialisation/deserialisation
BOOST_AUTO_TEST_CASE(serialisation)
{
    auto Check = [](bool makeWithSig) {
        // Create revokemid msg
        RevokeMid msg { MakeRevokeMid() };

        CDataStream ss { SER_NETWORK, 0 };
        ss << msg;
        RevokeMid deserialised {};
        ss >> deserialised;
        BOOST_CHECK_EQUAL(msg, deserialised);
    };

    Check(false);
    Check(true);
}

BOOST_AUTO_TEST_SUITE_END()

