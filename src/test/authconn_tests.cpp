// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "net/authconn.h"
#include "random.h"
#include "test/test_bitcoin.h"

#include <vector>
#include <boost/test/unit_test.hpp>

namespace {
    using namespace authconn;

    // This test case checks authconn key's correctness.
    CPubKey testAuthConnKeys(AuthConnKeys::PrivKeyStoredFormat keyStoredFormat, bool fCompressed) {
        // Instantiate the authconnkeys obj.
        AuthConnKeys authConnKeys(keyStoredFormat, fCompressed);
        CPubKey pubKey { authConnKeys.getPubKey() };
        BOOST_CHECK(pubKey.IsValid());
        BOOST_CHECK_EQUAL(pubKey.IsCompressed(), fCompressed);
        // Create and verify signature.
        uint256 rndMsgHash { GetRandHash() };
        std::vector<uint8_t> vSign {};
        BOOST_CHECK(authConnKeys.Sign(rndMsgHash, vSign));
        BOOST_CHECK(pubKey.Verify(rndMsgHash, vSign));

        return pubKey;
    }

    struct BasicTestingSetup2 : BasicTestingSetup {
        fs::path tmpAuthConnKeysFile = pathTemp / "authconnkeys.dat";

        // Check if the key data file is maintained correctly during key's creation process.
        void testAuthConnKeysAndDataFileExistence(AuthConnKeys::PrivKeyStoredFormat keyStoredFormat, bool fCompressed) {
            // The key-pair file doesn't exist at this stage.
            BOOST_CHECK(!fs::exists(tmpAuthConnKeysFile));
            CPubKey pubKey1 { testAuthConnKeys(keyStoredFormat, fCompressed) };
            BOOST_CHECK(fs::exists(tmpAuthConnKeysFile));
            CPubKey pubKey2 { testAuthConnKeys(keyStoredFormat, fCompressed) };
            // Once the data file is created it should be reused to instantiate
            // the same authconn key-pair during next instances run (reboot process).
            BOOST_CHECK(pubKey1 == pubKey2);
            // The key-pair file does exist.
            BOOST_CHECK(fs::exists(tmpAuthConnKeysFile));
        }
    };
}

BOOST_FIXTURE_TEST_SUITE(authconn_tests, BasicTestingSetup2)

BOOST_AUTO_TEST_CASE(authconnkeys_compressed_BIP32) {
    testAuthConnKeysAndDataFileExistence(AuthConnKeys::PrivKeyStoredFormat::BIP32, true);
}

BOOST_AUTO_TEST_CASE(authconnkeys_uncompressed_BIP32) {
    testAuthConnKeysAndDataFileExistence(AuthConnKeys::PrivKeyStoredFormat::BIP32, false);
}

BOOST_AUTO_TEST_CASE(authconnkeys_compressed_ECDSA) {
    testAuthConnKeysAndDataFileExistence(AuthConnKeys::PrivKeyStoredFormat::ECDSA, true);
}

BOOST_AUTO_TEST_CASE(authconnkeys_uncompressed_ECDSA) {
    testAuthConnKeysAndDataFileExistence(AuthConnKeys::PrivKeyStoredFormat::ECDSA, false);
}

BOOST_AUTO_TEST_SUITE_END()
