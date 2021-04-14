// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include <double_spend/dstxn_serialiser.h>

#include <test/test_bitcoin.h>

#include <boost/test/unit_test.hpp>

#ifdef WIN32
#include <io.h>
#ifdef _MSC_VER
#pragma warning(disable : 4996)
#endif // _MSC_VER
#else
#include <unistd.h>
#endif // WIN32

namespace
{
    struct RegtestingSetup : public TestingSetup
    {
        RegtestingSetup() : TestingSetup(CBaseChainParams::REGTEST)
        {
        }
    };
}

BOOST_FIXTURE_TEST_SUITE(dstxn_serialiser, RegtestingSetup)

// Test creation of a txn serialiser and some simple serialisation
BOOST_AUTO_TEST_CASE(Serialise)
{
    BOOST_CHECK_NO_THROW(
        // Create serialiser
        DSTxnSerialiser txnSerialiser {};

        // Make a transaction
        CMutableTransaction txn {};
        txn.vin.resize(1);
        txn.vout.resize(1);
        CTransactionRef txnRef { MakeTransactionRef(txn) };
        size_t txnSize { txnRef->GetTotalSize() };

        fs::path txnFile {};
        {
            // Serialise it
            auto handle { txnSerialiser.Serialise(*txnRef) };
            txnFile = handle->GetFile();
            BOOST_CHECK(fs::exists(txnFile));
            BOOST_CHECK(fs::is_regular_file(txnFile));

            // Get file descriptor for serialised txn file
            auto fd { handle->OpenFile()};
            BOOST_CHECK(fd.Get() >= 0);

            // Check serialised file size
            BOOST_CHECK_EQUAL(handle->GetFileSize(), txnSize);
        }

        // Check file is deleted once handle goes out of scope
        BOOST_CHECK(! fs::exists(txnFile));
    );
}

// Test the file-descriptor wrapper
BOOST_AUTO_TEST_CASE(FileDescriptor)
{
    // Create a txn file we can test with
    DSTxnSerialiser txnSerialiser {};
    CMutableTransaction txn {};
    txn.vin.resize(1);
    txn.vout.resize(1);
    CTransactionRef txnRef { MakeTransactionRef(txn) };
    auto handle { txnSerialiser.Serialise(*txnRef) };
    fs::path txnFile { handle->GetFile() };
    BOOST_REQUIRE(fs::exists(txnFile));

    // Open the file and get a file-descriptor to it
    UniqueFileDescriptor fd1 { handle->OpenFile() };
    BOOST_CHECK(fd1.Get() >= 0);

    // Check descriptor is closed on destruction
    {
        int fd {-1};
        {
            // Open the file again
            UniqueFileDescriptor fd2 { handle->OpenFile() };
            fd = fd2.Get();
            BOOST_CHECK(fd >= 0);
            BOOST_CHECK(fd != fd1.Get());
        }
        // fd2 should have been closed, so if we close it again it should be an error
        #ifndef WIN32
        BOOST_CHECK_EQUAL(close(fd), -1);
        #endif
    }

    // Check Release function
    {
        UniqueFileDescriptor fd3 { handle->OpenFile() };
        int fd { fd3.Get() };
        BOOST_CHECK(fd >= 0);
        BOOST_CHECK_EQUAL(fd3.Release(), fd);
        BOOST_CHECK_EQUAL(close(fd), 0);
        BOOST_CHECK_EQUAL(fd3.Get(), -1);
    }

    // Check Reset function
    {
        UniqueFileDescriptor fd4 { handle->OpenFile() };
        BOOST_CHECK(fd4.Get() >= 0);
        fd4.Reset();
        BOOST_CHECK_EQUAL(fd4.Get(), -1);
    }
}

BOOST_AUTO_TEST_SUITE_END()

