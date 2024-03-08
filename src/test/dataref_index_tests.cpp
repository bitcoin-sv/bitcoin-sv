// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "config.h"
#include "miner_id/coinbase_doc.h"
#include "miner_id/dataref_index.h"

#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>

namespace
{
    // Create a dummy dataRef txn (doesn't have to be correct/valid) and a
    // CoinbaseDocument::DataRef that references it
    std::pair<CTransactionRef, CoinbaseDocument::DataRef> CreateDataRefTxn()
    {
        CMutableTransaction mtxn {};
        mtxn.vin.resize(1);
        mtxn.vout.resize(1);
        mtxn.vout[0].scriptPubKey = CScript() << OP_TRUE;
        mtxn.vin[0].prevout = COutPoint { InsecureRand256(), 0 };
        CTransactionRef txn { MakeTransactionRef(mtxn) };

        CoinbaseDocument::DataRef dataRef { {"brfcId1" }, txn->GetId(), 0 , ""};

        return std::make_pair(txn, dataRef);
    }

    // Space taken up by each of our txns in the DB
    constexpr uint64_t SizeofOneTxnInDB { 93 };

    // For ID only
    class dataref_index_tests_id;
}

// DataRefTxnDB class inspection

using DataDB = miner::detail::DataDB;
using DatarefDB = DataRefTxnDB::LockingAccess;
using DBDataref = DataDB::DBDataref;
using DBMinerInfo = DataDB::DBMinerInfo;

template<>
struct DatarefDB::UnitTestAccess<dataref_index_tests_id>
{
    static DataDB & GetDetail(DatarefDB& db)
    {
        return db.GetDetail();
    }
};

using UnitTestAccess = DatarefDB::UnitTestAccess<dataref_index_tests_id>;

BOOST_AUTO_TEST_SUITE(dataref_index)

// Test basic construction/destruction
BOOST_FIXTURE_TEST_CASE(Creation, BasicTestingSetup)
{
    {
        // Creation with no existing DB
        DataRefTxnDB dbx { GlobalConfig::GetConfig() };
        DatarefDB db = dbx.CreateLockingAccess();
        DataDB & db_detail = UnitTestAccess::GetDetail(db);
        BOOST_CHECK_EQUAL(db_detail.mDiskUsage, 0U);
        BOOST_CHECK_EQUAL(db_detail.GetAllDatarefEntries().size(), 0U);
        BOOST_CHECK_EQUAL(db_detail.GetAllMinerInfoEntries().size(), 0U);
    }
    {
        // Creation from existing empty DB
        DataRefTxnDB dbx { GlobalConfig::GetConfig() };
        DatarefDB db = dbx.CreateLockingAccess();
        DataDB & db_detail = UnitTestAccess::GetDetail(db);
        BOOST_CHECK_EQUAL(db_detail.mDiskUsage, 0U);
        BOOST_CHECK_EQUAL(db_detail.GetAllDatarefEntries().size(), 0U);
        BOOST_CHECK_EQUAL(db_detail.GetAllMinerInfoEntries().size(), 0U);
    }
}

// Test transaction storage and retrieval
BOOST_FIXTURE_TEST_CASE(StorageRetrieval, BasicTestingSetup)
{
    // Create coinbase doucument with a dataRef txn
    const auto& [ dataRefTxn, dataRef ] { CreateDataRefTxn() };
    uint256 blockId { InsecureRand256()};

    {
        DataRefTxnDB dbx { GlobalConfig::GetConfig() };
        DatarefDB db = dbx.CreateLockingAccess();
        DataDB & db_detail = UnitTestAccess::GetDetail(db);
        BOOST_CHECK(! db.DataRefTxnExists(dataRefTxn->GetId()));

        // Add txn to the database
        CoinbaseDocument cbDoc {"", "0.2", 1, "PrevMinerId", "PrevMinerIdSig", "MinerId", {}, {std::nullopt} };
        std::vector<CoinbaseDocument::DataRef> datarefs;
        datarefs.push_back(dataRef);
        cbDoc.SetDataRefs(datarefs);

        MerkleProof proof {};
        DBDataref entry1 {dataRefTxn, blockId, proof};
        db_detail.AddEntry(entry1, dataRefTxn->GetId());
        BOOST_CHECK(db.DataRefTxnExists(dataRefTxn->GetId()));
        BOOST_CHECK_EQUAL(db_detail.mDiskUsage, SizeofOneTxnInDB);
        BOOST_CHECK_EQUAL(db_detail.GetAllDatarefEntries().size(), 1U);
        auto dbentry { db.GetDataRefEntry(dataRefTxn->GetId()) };
        BOOST_CHECK (dbentry);
        BOOST_CHECK_EQUAL(dbentry->blockId.ToString(), blockId.ToString());
        BOOST_CHECK_EQUAL(dbentry->txn->GetId().ToString(), dataRefTxn->GetId().ToString());

        // Fetch newly added txn
        CTransactionRef readTxn { db.GetDataRefTxn(dataRefTxn->GetId()) };
        BOOST_CHECK_EQUAL(readTxn->GetId().ToString(), dataRefTxn->GetId().ToString());
    }
    {
        // Check we can still fetch from a new instance of the DB
        DataRefTxnDB dbx { GlobalConfig::GetConfig() };
        DatarefDB db = dbx.CreateLockingAccess();
        DataDB & db_detail = UnitTestAccess::GetDetail(db);
        BOOST_CHECK(db.DataRefTxnExists(dataRefTxn->GetId()));
        BOOST_CHECK_EQUAL(db_detail.mDiskUsage, SizeofOneTxnInDB);
        BOOST_CHECK_EQUAL(db_detail.GetAllDatarefEntries().size(), 1U);
        auto dbentry { db.GetDataRefEntry(dataRefTxn->GetId()) };
        BOOST_CHECK (dbentry);
        BOOST_CHECK_EQUAL(dbentry->blockId.ToString(), blockId.ToString());
        BOOST_CHECK_EQUAL(dbentry->txn->GetId().ToString(), dataRefTxn->GetId().ToString());
        CTransactionRef readTxn { db.GetDataRefTxn(dataRefTxn->GetId()) };

        BOOST_CHECK_EQUAL(readTxn->GetId().ToString(), dataRefTxn->GetId().ToString());
    }
    {
        // Check we can delete from the DB
        DataRefTxnDB dbx { GlobalConfig::GetConfig() };
        DatarefDB db = dbx.CreateLockingAccess();
        DataDB & db_detail = UnitTestAccess::GetDetail(db);
        BOOST_CHECK(db.DataRefTxnExists(dataRefTxn->GetId()));
        BOOST_CHECK_NO_THROW(db.DeleteDatarefTxn(dataRefTxn->GetId()));
        BOOST_CHECK_EQUAL(db_detail.mDiskUsage, 0U);
        BOOST_CHECK_EQUAL(db_detail.GetAllDatarefEntries().size(), 0U);
    }
}

BOOST_AUTO_TEST_SUITE_END()

