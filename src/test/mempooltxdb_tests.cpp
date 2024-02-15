#include "mining/journal_change_set.h"
#include "mempooltxdb.h"

#include "mempool_test_access.h"

#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

#include <algorithm>
#include <array>
#include <future>
#include <random>
#include <unordered_map>
#include <vector>


namespace {
    mining::CJournalChangeSetPtr nullChangeSet{nullptr};

    std::vector<CTxMemPoolEntry> GetABunchOfEntries(int howMany)
    {
        TestMemPoolEntryHelper entry;
        std::vector<CTxMemPoolEntry> result;
        for (int i = 0; i < howMany; i++) {
            CMutableTransaction mtx;
            mtx.vin.resize(1);
            mtx.vin[0].scriptSig = CScript() << OP_11;
            mtx.vout.resize(1);
            mtx.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
            mtx.vout[0].nValue = Amount{33000LL + i};
            result.emplace_back(entry.Fee(Amount{10000}).FromTx(mtx));
        }
        return result;
    }

    uint64_t totalSize(const std::vector<CTxMemPoolEntry>& entries)
    {
        uint64_t total = 0;
        for (const auto& e : entries)
        {
            total += e.GetTxSize();
        }
        return total;
    }
}

CTxMemPool& globalMempool()
{
    extern CTxMemPool mempool;
    return mempool;
}



BOOST_FIXTURE_TEST_SUITE(mempooltxdb_tests, TestingSetup)
BOOST_AUTO_TEST_CASE(WriteToTxDB)
{
    const auto entries = GetABunchOfEntries(11);

    CMempoolTxDB txdb{GetDataDir() / "test-txdb", 10000, true, true};
    BOOST_CHECK_EQUAL(txdb.GetDiskUsage(), 0U);
    BOOST_CHECK_EQUAL(txdb.GetTxCount(), 0U);

    // Write the entries to the database.
    for (const auto& e : entries)
    {
        BOOST_CHECK(txdb.AddTransactions({e.GetSharedTx()}));
    }
    BOOST_CHECK_EQUAL(txdb.GetDiskUsage(), totalSize(entries));
    BOOST_CHECK_EQUAL(txdb.GetTxCount(), entries.size());

    // Check that all transactions are in the database.
    for (const auto& e : entries)
    {
        CTransactionRef _;
        BOOST_CHECK(txdb.GetTransaction(e.GetTxId(), _));
    }
}

BOOST_AUTO_TEST_CASE(DoubleWriteToTxDB)
{
    const auto entries = GetABunchOfEntries(13);

    CMempoolTxDB txdb{GetDataDir() / "test-txdb", 10000, true, true};
    BOOST_CHECK_EQUAL(txdb.GetDiskUsage(), 0U);
    BOOST_CHECK_EQUAL(txdb.GetTxCount(), 0U);

    // Write the entries to the database.
    for (const auto& e : entries)
    {
        BOOST_CHECK(txdb.AddTransactions({e.GetSharedTx()}));
    }
    BOOST_CHECK_EQUAL(txdb.GetDiskUsage(), totalSize(entries));
    BOOST_CHECK_EQUAL(txdb.GetTxCount(), entries.size());

    // Check that all transactions are in the database.
    for (const auto& e : entries)
    {
        CTransactionRef _;
        BOOST_CHECK(txdb.GetTransaction(e.GetTxId(), _));
    }

    // Write and check again.
    for (const auto& e : entries)
    {
        BOOST_CHECK(txdb.AddTransactions({e.GetSharedTx()}));
    }
    BOOST_WARN_EQUAL(txdb.GetDiskUsage(), totalSize(entries));
    BOOST_WARN_EQUAL(txdb.GetTxCount(), entries.size());
    BOOST_CHECK_GE(txdb.GetDiskUsage(), totalSize(entries));
    BOOST_CHECK_GE(txdb.GetTxCount(), entries.size());
    for (const auto& e : entries)
    {
        CTransactionRef _;
        BOOST_CHECK(txdb.GetTransaction(e.GetTxId(), _));
    }
}

BOOST_AUTO_TEST_CASE(DeleteFromTxDB)
{
    const auto entries = GetABunchOfEntries(17);

    CMempoolTxDB txdb{GetDataDir() / "test-txdb", 10000, true, true};
    BOOST_CHECK_EQUAL(txdb.GetDiskUsage(), 0U);
    BOOST_CHECK_EQUAL(txdb.GetTxCount(), 0U);

    // Write the entries to the database.
    for (const auto& e : entries)
    {
        BOOST_CHECK(txdb.AddTransactions({e.GetSharedTx()}));
    }
    BOOST_CHECK_EQUAL(txdb.GetDiskUsage(), totalSize(entries));
    BOOST_CHECK_EQUAL(txdb.GetTxCount(), entries.size());

    // Remove transactions from the database one by one.
    for (const auto& e : entries)
    {
        BOOST_CHECK(txdb.RemoveTransactions({{e.GetTxId(), e.GetTxSize()}}));
    }
    BOOST_CHECK_EQUAL(txdb.GetDiskUsage(), 0U);
    BOOST_CHECK_EQUAL(txdb.GetTxCount(), 0U);
    for (const auto& e : entries)
    {
        CTransactionRef _;
        BOOST_CHECK(!txdb.GetTransaction(e.GetTxId(), _));
    }
}

BOOST_AUTO_TEST_CASE(BatchDeleteFromTxDB)
{
    const auto entries = GetABunchOfEntries(19);

    CMempoolTxDB txdb{GetDataDir() / "test-txdb", 10000, true, true};
    BOOST_CHECK_EQUAL(txdb.GetDiskUsage(), 0U);
    BOOST_CHECK_EQUAL(txdb.GetTxCount(), 0U);

    // Write the entries to the database.
    std::vector<CMempoolTxDB::TxData> txdata;
    for (const auto& e : entries)
    {
        txdata.emplace_back(e.GetTxId(), e.GetTxSize());
        BOOST_CHECK(txdb.AddTransactions({e.GetSharedTx()}));
    }
    BOOST_CHECK_EQUAL(txdb.GetDiskUsage(), totalSize(entries));
    BOOST_CHECK_EQUAL(txdb.GetTxCount(), entries.size());

    // Remove all transactions from the database at once.
    BOOST_CHECK(txdb.RemoveTransactions(txdata));
    BOOST_CHECK_EQUAL(txdb.GetDiskUsage(), 0U);
    BOOST_CHECK_EQUAL(txdb.GetTxCount(), 0U);
    for (const auto& e : entries)
    {
        CTransactionRef _;
        BOOST_CHECK(!txdb.GetTransaction(e.GetTxId(), _));
    }
}

BOOST_AUTO_TEST_CASE(BadDeleteFromTxDB)
{
    CMempoolTxDB txdb{GetDataDir() / "test-txdb", 10000, true, true};
    BOOST_CHECK_EQUAL(txdb.GetDiskUsage(), 0U);
    BOOST_CHECK_EQUAL(txdb.GetTxCount(), 0U);

    // Remove nonexistent transactions.
    const auto e = GetABunchOfEntries(3);
    BOOST_CHECK(txdb.RemoveTransactions({
                {e[0].GetTxId(), e[0].GetTxSize()},
                {e[1].GetTxId(), e[1].GetTxSize()},
                {e[2].GetTxId(), e[2].GetTxSize()}
            }));
    BOOST_WARN_EQUAL(txdb.GetDiskUsage(), 0U);
    BOOST_WARN_EQUAL(txdb.GetTxCount(), 0U);
}

BOOST_AUTO_TEST_CASE(ClearTxDB)
{
    const auto entries = GetABunchOfEntries(23);

    CMempoolTxDB txdb{GetDataDir() / "test-txdb", 10000, true, true};
    BOOST_CHECK_EQUAL(txdb.GetDiskUsage(), 0U);
    BOOST_CHECK_EQUAL(txdb.GetTxCount(), 0U);

    // Write the entries to the database.
    for (const auto& e : entries)
    {
        BOOST_CHECK(txdb.AddTransactions({e.GetSharedTx()}));
    }
    BOOST_CHECK_EQUAL(txdb.GetDiskUsage(), totalSize(entries));
    BOOST_CHECK_EQUAL(txdb.GetTxCount(), entries.size());

    // Clear the database and check that it's empty.
    txdb.ClearDatabase();
    BOOST_CHECK_EQUAL(txdb.GetDiskUsage(), 0U);
    BOOST_CHECK_EQUAL(txdb.GetTxCount(), 0U);
    for (const auto& e : entries)
    {
        CTransactionRef _;
        BOOST_CHECK(!txdb.GetTransaction(e.GetTxId(), _));
    }
}

BOOST_AUTO_TEST_CASE(GetContentsOfTxDB)
{
    const auto entries = GetABunchOfEntries(29);

    CMempoolTxDB txdb{GetDataDir() / "test-txdb", 10000, true, true};
    BOOST_CHECK_EQUAL(txdb.GetDiskUsage(), 0U);
    BOOST_CHECK_EQUAL(txdb.GetTxCount(), 0U);

    // Write the entries to the database.
    for (const auto& e : entries)
    {
        BOOST_CHECK(txdb.AddTransactions({e.GetSharedTx()}));
    }
    BOOST_CHECK_EQUAL(txdb.GetDiskUsage(), totalSize(entries));
    BOOST_CHECK_EQUAL(txdb.GetTxCount(), entries.size());

    // Check that all transactions are in the database and only the ones we wrote.
    auto keys = txdb.GetKeys();
    BOOST_CHECK_EQUAL(keys.size(), 29U);
    for (const auto& e : entries)
    {
        auto iter = keys.find(e.GetTxId());
        BOOST_WARN(iter != keys.end());
        if (iter != keys.end())
        {
            keys.erase(iter);
        }
    }
    // We should have removed all the keys in the loop.
    BOOST_CHECK_EQUAL(keys.size(), 0U);
}

namespace  {
    class DeterministicUUIDGenerator
    {
    private:
        boost::mt19937 random_generator;
        using uuid_random_generator_type = boost::uuids::basic_random_generator<boost::mt19937>;
        uuid_random_generator_type uuid_random_generator;

    public:
        using result_type = uuid_random_generator_type::result_type;

        DeterministicUUIDGenerator()
            : random_generator(insecure_rand())
            , uuid_random_generator(random_generator)
        {}

        result_type operator()()
        {
            return uuid_random_generator();
        }
    };
}

BOOST_AUTO_TEST_CASE(GetSetXrefKey)
{
    DeterministicUUIDGenerator gen;
    const auto uuid = gen();
    auto xref = decltype(uuid)();
    BOOST_CHECK_NE(to_string(uuid), to_string(xref));

    CMempoolTxDB txdb{GetDataDir() / "test-txdb", 10000, true, true};
    BOOST_CHECK(!txdb.GetXrefKey(xref));
    BOOST_CHECK(txdb.SetXrefKey(uuid));
    BOOST_CHECK(txdb.GetXrefKey(xref));
    BOOST_CHECK_EQUAL(to_string(uuid), to_string(xref));
}

BOOST_AUTO_TEST_CASE(RemoveXrefKey)
{
    DeterministicUUIDGenerator gen;
    const auto uuid = gen();
    auto xref = decltype(uuid)();

    CMempoolTxDB txdb{GetDataDir() / "test-txdb", 10000, true, true};
    BOOST_CHECK(!txdb.GetXrefKey(xref));
    BOOST_CHECK(txdb.SetXrefKey(uuid));
    BOOST_CHECK(txdb.GetXrefKey(xref));
    BOOST_CHECK(txdb.RemoveXrefKey());
    BOOST_CHECK(!txdb.GetXrefKey(xref));
}

BOOST_AUTO_TEST_CASE(AutoRemoveXrefKey)
{
    DeterministicUUIDGenerator gen;
    const auto uuid = gen();
    auto xref = decltype(uuid)();
    const auto entries = GetABunchOfEntries(1);
    const auto& e = entries[0];

    CMempoolTxDB txdb{GetDataDir() / "test-txdb", 10000, true, true};
    BOOST_CHECK(!txdb.GetXrefKey(xref));
    BOOST_CHECK(txdb.SetXrefKey(uuid));
    BOOST_CHECK(txdb.GetXrefKey(xref));
    txdb.AddTransactions({e.GetSharedTx()});
    BOOST_CHECK(!txdb.GetXrefKey(xref));

    BOOST_CHECK(txdb.SetXrefKey(uuid));
    BOOST_CHECK(txdb.GetXrefKey(xref));
    txdb.RemoveTransactions({{e.GetTxId(), e.GetTxSize()}});
    BOOST_CHECK(!txdb.GetXrefKey(xref));
    BOOST_CHECK_EQUAL(txdb.GetDiskUsage(), 0U);
}

BOOST_AUTO_TEST_CASE(BatchWriteWrite)
{
    const auto entries = GetABunchOfEntries(1);
    const auto& entry = entries[0];

    CMempoolTxDB txdb{GetDataDir() / "test-txdb", 10000, true, true};
    BOOST_CHECK_EQUAL(txdb.GetDiskUsage(), 0U);
    BOOST_CHECK_EQUAL(txdb.GetTxCount(), 0U);

    int counter = 0;
    const auto update = [&entry, &counter](const TxId& txid) {
        BOOST_CHECK_EQUAL(txid.ToString(), entry.GetTxId().ToString());
        ++counter;
    };

    CMempoolTxDB::Batch batch;
    batch.Add(entry.GetSharedTx(), update);
    batch.Add(entry.GetSharedTx(), update);
    BOOST_CHECK(txdb.Commit(batch));
    BOOST_CHECK_EQUAL(txdb.GetDiskUsage(), entry.GetTxSize());
    BOOST_CHECK_EQUAL(txdb.GetTxCount(), 1U);
    BOOST_CHECK_EQUAL(counter, 1);
}

BOOST_AUTO_TEST_CASE(BatchWriteRemove)
{
    const auto entries = GetABunchOfEntries(1);
    const auto& entry = entries[0];

    CMempoolTxDB txdb{GetDataDir() / "test-txdb", 10000, true, true};
    BOOST_CHECK_EQUAL(txdb.GetDiskUsage(), 0U);
    BOOST_CHECK_EQUAL(txdb.GetTxCount(), 0U);

    int counter = 0;
    const auto update = [&entry, &counter](const TxId& txid) {
        BOOST_CHECK_EQUAL(txid.ToString(), entry.GetTxId().ToString());
        ++counter;
    };

    CMempoolTxDB::Batch batch;
    batch.Add(entry.GetSharedTx(), update);
    batch.Remove(entry.GetTxId(), entry.GetTxSize());
    BOOST_CHECK(txdb.Commit(batch));
    BOOST_CHECK_EQUAL(txdb.GetDiskUsage(), 0U);
    BOOST_CHECK_EQUAL(txdb.GetTxCount(), 0U);
    BOOST_CHECK_EQUAL(counter, 0);
}

BOOST_AUTO_TEST_CASE(BatchWriteRemoveWrite)
{
    const auto entries = GetABunchOfEntries(1);
    const auto& entry = entries[0];

    CMempoolTxDB txdb{GetDataDir() / "test-txdb", 10000, true, true};
    BOOST_CHECK_EQUAL(txdb.GetDiskUsage(), 0U);
    BOOST_CHECK_EQUAL(txdb.GetTxCount(), 0U);

    int counter = 0;
    const auto update = [&entry, &counter](const TxId& txid) {
        BOOST_CHECK_EQUAL(txid.ToString(), entry.GetTxId().ToString());
        ++counter;
    };

    CMempoolTxDB::Batch batch;
    batch.Add(entry.GetSharedTx(), update);
    batch.Remove(entry.GetTxId(), entry.GetTxSize());
    batch.Add(entry.GetSharedTx(), update);
    BOOST_CHECK(txdb.Commit(batch));
    BOOST_CHECK_EQUAL(txdb.GetDiskUsage(), entry.GetTxSize());
    BOOST_CHECK_EQUAL(txdb.GetTxCount(), 1U);
    BOOST_CHECK_EQUAL(counter, 1);
}

BOOST_AUTO_TEST_CASE(Write_BatchRemoveWrite)
{
    const auto entries = GetABunchOfEntries(1);
    const auto& entry = entries[0];

    CMempoolTxDB txdb{GetDataDir() / "test-txdb", 10000, true, true};
    BOOST_CHECK_EQUAL(txdb.GetDiskUsage(), 0U);
    BOOST_CHECK_EQUAL(txdb.GetTxCount(), 0U);

    BOOST_CHECK(txdb.AddTransactions({entry.GetSharedTx()}));
    BOOST_CHECK_EQUAL(txdb.GetDiskUsage(), entry.GetTxSize());
    BOOST_CHECK_EQUAL(txdb.GetTxCount(), 1U);

    int counter = 0;
    const auto update = [&entry, &counter](const TxId& txid) {
        BOOST_CHECK_EQUAL(txid.ToString(), entry.GetTxId().ToString());
        ++counter;
    };

    CMempoolTxDB::Batch batch;
    batch.Remove(entry.GetTxId(), entry.GetTxSize());
    batch.Add(entry.GetSharedTx(), update);
    BOOST_CHECK(txdb.Commit(batch));
    BOOST_CHECK_EQUAL(txdb.GetDiskUsage(), entry.GetTxSize());
    BOOST_CHECK_EQUAL(txdb.GetTxCount(), 1U);
    BOOST_CHECK_EQUAL(counter, 0);
}

BOOST_AUTO_TEST_CASE(AsyncWriteToTxDB)
{
    const auto entries = GetABunchOfEntries(11);

    CAsyncMempoolTxDB txdb{GetDataDir() / "test-txdb", 10000, true};
    BOOST_CHECK_EQUAL(txdb.GetDiskUsage(), 0U);
    BOOST_CHECK_EQUAL(txdb.GetTxCount(), 0U);

    // Write the entries to the database.
    for (const auto& e : entries)
    {
        txdb.Add(CTestTxMemPoolEntry::GetTxWrapper(e));
    }
    txdb.Sync();
    BOOST_CHECK_EQUAL(txdb.GetDiskUsage(), totalSize(entries));
    BOOST_CHECK_EQUAL(txdb.GetTxCount(), entries.size());

    // Check that all transactions are in the databas.
    const auto innerdb = txdb.GetDatabase();
    for (const auto& e : entries)
    {
        CTransactionRef _;
        BOOST_CHECK(innerdb->GetTransaction(e.GetTxId(), _));
    }
}

BOOST_AUTO_TEST_CASE(AsyncDeleteFromTxDB)
{
    const auto entries = GetABunchOfEntries(13);

    CAsyncMempoolTxDB txdb{GetDataDir() / "test-txdb", 10000, true};
    BOOST_CHECK_EQUAL(txdb.GetDiskUsage(), 0U);
    BOOST_CHECK_EQUAL(txdb.GetTxCount(), 0U);

    // Write the entries to the database.
    std::vector<CMempoolTxDB::TxData> txdata;
    for (const auto& e : entries)
    {
        txdata.emplace_back(e.GetTxId(), e.GetTxSize());
        txdb.Add(CTestTxMemPoolEntry::GetTxWrapper(e));
    }

    // Remove all transactions from the database.
    for (auto& td : txdata)
    {
        txdb.Remove(std::move(td));
    }
    txdb.Sync();
    BOOST_CHECK_EQUAL(txdb.GetDiskUsage(), 0U);
    BOOST_CHECK_EQUAL(txdb.GetTxCount(), 0U);
    const auto innerdb = txdb.GetDatabase();
    for (const auto& e : entries)
    {
        CTransactionRef _;
        BOOST_CHECK(!innerdb->GetTransaction(e.GetTxId(), _));
    }
}

BOOST_AUTO_TEST_CASE(AsyncClearDB)
{
    const auto entries = GetABunchOfEntries(17);

    CAsyncMempoolTxDB txdb{GetDataDir() / "test-txdb", 10000, true};
    BOOST_CHECK_EQUAL(txdb.GetDiskUsage(), 0U);
    BOOST_CHECK_EQUAL(txdb.GetTxCount(), 0U);

    // Write the entries to the database.
    for (const auto& e : entries)
    {
        txdb.Add(CTestTxMemPoolEntry::GetTxWrapper(e));
    }

    txdb.Clear();
    BOOST_CHECK_EQUAL(txdb.GetDiskUsage(), 0U);
    BOOST_CHECK_EQUAL(txdb.GetTxCount(), 0U);

    const auto innerdb = txdb.GetDatabase();
    for (const auto& e : entries)
    {
        CTransactionRef _;
        BOOST_CHECK(!innerdb->GetTransaction(e.GetTxId(), _));
    }
}

BOOST_AUTO_TEST_CASE(AsyncMultiWriteCoalesce)
{
    const auto entries = GetABunchOfEntries(1223);

    CAsyncMempoolTxDB txdb{GetDataDir() / "test-txdb", 10000, true};
    BOOST_CHECK_EQUAL(txdb.GetDiskUsage(), 0U);
    BOOST_CHECK_EQUAL(txdb.GetTxCount(), 0U);

    for (const auto& e : entries)
    {
        txdb.Add({CTestTxMemPoolEntry::GetTxWrapper(e)});
    }

    txdb.Sync();
    BOOST_CHECK_EQUAL(txdb.GetTxCount(), entries.size());
    BOOST_CHECK_LT(txdb.GetWriteCount(), entries.size());
    BOOST_TEST_MESSAGE("AsyncMultiWriteCoalesce: " << txdb.GetWriteCount()
                       << " batch writes for " << entries.size() << " adds");

    const auto innerdb = txdb.GetDatabase();
    for (const auto& e : entries)
    {
        CTransactionRef _;
        BOOST_CHECK(innerdb->GetTransaction(e.GetTxId(), _));
    }
}

BOOST_AUTO_TEST_CASE(AsyncMultiWriteRemoveCoalesce)
{
    std::mt19937 generator(insecure_rand());

    auto entries = GetABunchOfEntries(541);
    const auto middle = entries.begin() + entries.size() / 2;

    CAsyncMempoolTxDB txdb{GetDataDir() / "test-txdb", 10000, true};
    BOOST_CHECK_EQUAL(txdb.GetDiskUsage(), 0U);
    BOOST_CHECK_EQUAL(txdb.GetTxCount(), 0U);

    for (auto it = entries.begin(); it != middle; ++it)
    {
        txdb.Add({CTestTxMemPoolEntry::GetTxWrapper(*it)});
    }
    std::shuffle(entries.begin(), middle, generator);
    for (auto it = entries.begin(); it != middle; ++it)
    {
        txdb.Remove({it->GetTxId(), it->GetTxSize()});
    }
    txdb.Sync();

    for (auto it = middle; it != entries.end(); ++it)
    {
        txdb.Add({CTestTxMemPoolEntry::GetTxWrapper(*it)});
    }
    std::shuffle(middle, entries.end(), generator);
    for (auto it = middle; it != entries.end(); ++it)
    {
        txdb.Remove({it->GetTxId(), it->GetTxSize()});
    }
    txdb.Sync();

    BOOST_CHECK_EQUAL(txdb.GetTxCount(), 0U);
    BOOST_CHECK_LT(txdb.GetWriteCount(), 2 * entries.size());
    BOOST_TEST_MESSAGE("AsyncMultiWriteRemoveCoalesce: " << txdb.GetWriteCount()
                       << " batch writes for " << entries.size() << " adds"
                       << " and " << entries.size() << " deletes");

    const auto innerdb = txdb.GetDatabase();
    for (const auto& e : entries)
    {
        CTransactionRef _;
        BOOST_CHECK(!innerdb->GetTransaction(e.GetTxId(), _));
    }
}

BOOST_AUTO_TEST_CASE(AsyncGetSetXrefKey)
{
    DeterministicUUIDGenerator gen;
    const auto uuid = gen();
    auto xref = decltype(uuid)();
    BOOST_CHECK_NE(to_string(uuid), to_string(xref));

    CAsyncMempoolTxDB txdb{GetDataDir() / "test-txdb", 10000, true};
    BOOST_CHECK(!txdb.GetXrefKey(xref));
    BOOST_CHECK(txdb.SetXrefKey(uuid));
    BOOST_CHECK(txdb.GetXrefKey(xref));
    BOOST_CHECK_EQUAL(to_string(uuid), to_string(xref));
}

BOOST_AUTO_TEST_CASE(AsyncRemoveXrefKey)
{
    DeterministicUUIDGenerator gen;
    const auto uuid = gen();
    auto xref = decltype(uuid)();

    CAsyncMempoolTxDB txdb{GetDataDir() / "test-txdb", 10000, true};
    BOOST_CHECK(!txdb.GetXrefKey(xref));
    BOOST_CHECK(txdb.SetXrefKey(uuid));
    BOOST_CHECK(txdb.GetXrefKey(xref));
    BOOST_CHECK(txdb.RemoveXrefKey());
    BOOST_CHECK(!txdb.GetXrefKey(xref));
}

BOOST_AUTO_TEST_CASE(AsyncAutoRemoveXrefKey)
{
    DeterministicUUIDGenerator gen;
    const auto uuid = gen();
    auto xref = decltype(uuid)();
    auto entries = GetABunchOfEntries(1);
    auto& e = entries[0];

    CAsyncMempoolTxDB txdb{GetDataDir() / "test-txdb", 10000, true};
    BOOST_CHECK(!txdb.GetXrefKey(xref));
    BOOST_CHECK(txdb.SetXrefKey(uuid));
    BOOST_CHECK(txdb.GetXrefKey(xref));
    txdb.Add({CTestTxMemPoolEntry::GetTxWrapper(e)});
    BOOST_CHECK(!txdb.GetXrefKey(xref));

    BOOST_CHECK(txdb.SetXrefKey(uuid));
    BOOST_CHECK(txdb.GetXrefKey(xref));
    txdb.Remove({e.GetTxId(), e.GetTxSize()});
    BOOST_CHECK(!txdb.GetXrefKey(xref));
    BOOST_CHECK_EQUAL(txdb.GetDiskUsage(), 0U);
}

BOOST_AUTO_TEST_CASE(SaveOnFullMempool)
{
    TestMemPoolEntryHelper entry;
    // Parent transaction with three children, and three grand-children:
    CMutableTransaction txParent;
    txParent.vin.resize(1);
    txParent.vin[0].scriptSig = CScript() << OP_11;
    txParent.vout.resize(3);
    for (int i = 0; i < 3; i++) {
        txParent.vout[i].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
        txParent.vout[i].nValue = Amount(33000LL);
    }
    CMutableTransaction txChild[3];
    for (int i = 0; i < 3; i++) {
        txChild[i].vin.resize(1);
        txChild[i].vin[0].scriptSig = CScript() << OP_11;
        txChild[i].vin[0].prevout = COutPoint(txParent.GetId(), i);
        txChild[i].vout.resize(1);
        txChild[i].vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
        txChild[i].vout[0].nValue = Amount(11000LL);
    }
    CMutableTransaction txGrandChild[3];
    for (int i = 0; i < 3; i++) {
        txGrandChild[i].vin.resize(1);
        txGrandChild[i].vin[0].scriptSig = CScript() << OP_11;
        txGrandChild[i].vin[0].prevout = COutPoint(txChild[i].GetId(), 0);
        txGrandChild[i].vout.resize(1);
        txGrandChild[i].vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
        txGrandChild[i].vout[0].nValue = Amount(11000LL);
    }

    CTxMemPool testPool;
    CTxMemPoolTestAccess testPoolAccess(testPool);

    // Nothing in pool, remove should do nothing:
    BOOST_CHECK_EQUAL(testPool.Size(), 0U);
    testPool.SaveTxsToDisk(10000);
    testPoolAccess.SyncWithMempoolTxDB();
    BOOST_CHECK_EQUAL(testPool.GetDiskUsage(), 0U);
    BOOST_CHECK_EQUAL(testPool.GetDiskTxCount(), 0U);
    BOOST_CHECK_EQUAL(testPool.Size(), 0U);

    // Add transactions:
    testPool.AddUnchecked(txParent.GetId(), entry.FromTx(txParent), TxStorage::memory, nullChangeSet);
    for (int i = 0; i < 3; i++) {
        testPool.AddUnchecked(txChild[i].GetId(), entry.FromTx(txChild[i]), TxStorage::memory, nullChangeSet);
        testPool.AddUnchecked(txGrandChild[i].GetId(), entry.FromTx(txGrandChild[i]), TxStorage::memory, nullChangeSet);
    }

    // Saving transactions to disk doesn't change the mempool size:
    const auto poolSize = testPool.Size();
    testPool.SaveTxsToDisk(10000);
    testPoolAccess.SyncWithMempoolTxDB();
    BOOST_CHECK_EQUAL(testPool.Size(), poolSize);

    // But it does store something to disk:
    const auto diskUsage = testPool.GetDiskUsage();
    const auto txCount = testPool.GetDiskTxCount();
    BOOST_CHECK_GT(diskUsage, 0U);
    BOOST_CHECK_GT(txCount, 0U);
    BOOST_CHECK(testPoolAccess.CheckMempoolTxDB());

    // Check that all transactions have been saved to disk:
    uint64_t sizeTxsAdded = 0;
    uint64_t countTxsAdded = 0;
    for (const auto& entry : testPoolAccess.mapTx().get<entry_time>())
    {
        BOOST_CHECK(!entry.IsInMemory());
        sizeTxsAdded += entry.GetTxSize();
        ++countTxsAdded;
    }
    BOOST_CHECK_EQUAL(diskUsage, sizeTxsAdded);
    BOOST_CHECK_EQUAL(txCount, countTxsAdded);
    BOOST_CHECK(testPoolAccess.CheckMempoolTxDB());
}

BOOST_AUTO_TEST_CASE(RemoveFromDiskOnMempoolTrim)
{
    const auto entries = GetABunchOfEntries(6);

    CTxMemPool testPool;
    CTxMemPoolTestAccess testPoolAccess(testPool);

    // Add transactions:
    for (auto& entry : entries) {
        testPool.AddUnchecked(entry.GetTxId(), entry, TxStorage::memory, nullChangeSet);
    }

    // Saving transactions to disk doesn't change the mempool size:
    const auto poolSize = testPool.Size();
    BOOST_CHECK_EQUAL(poolSize, entries.size());
    testPool.SaveTxsToDisk(10000);
    testPoolAccess.SyncWithMempoolTxDB();
    BOOST_CHECK_EQUAL(testPool.Size(), poolSize);

    // But it does store something to disk:
    BOOST_CHECK_GT(testPool.GetDiskUsage(), 0U);
    BOOST_CHECK_GT(testPool.GetDiskTxCount(), 0U);
    BOOST_CHECK(testPoolAccess.CheckMempoolTxDB());

    // Trimming the mempool size should also remove transactions from disk:
    testPool.TrimToSize(0, nullChangeSet);
    testPoolAccess.SyncWithMempoolTxDB();
    BOOST_CHECK_EQUAL(testPool.Size(), 0U);
    BOOST_CHECK_EQUAL(testPool.GetDiskUsage(), 0U);
    BOOST_CHECK_EQUAL(testPool.GetDiskTxCount(), 0U);
    BOOST_CHECK(testPoolAccess.CheckMempoolTxDB());
}

BOOST_AUTO_TEST_CASE(RemoveFromDiskOnMempoolTrimDoesNotConfuseJBA)
{
    CTxMemPool& testPool = globalMempool();
    CTxMemPoolTestAccess testPoolAccess(testPool);

    auto [totalSize_entries, count_entries] = ([&testPool] () {
        auto entries = GetABunchOfEntries(6);

        // Add transactions:
        for (auto& entry : entries) {
            testPool.AddUnchecked(entry.GetTxId(), entry, TxStorage::memory, nullChangeSet);
        }

        return std::make_pair(totalSize(entries), entries.size());
        })();

    const auto poolSize = testPool.Size();
    BOOST_CHECK_EQUAL(poolSize, count_entries);

    auto jba = mining::g_miningFactory->GetAssembler();
    CBlockIndex* bla;

    // Get a block template
    auto template1 = jba->CreateNewBlock(CScript{}, bla);
    // wait for JBA to process all transactions in mempool
    const auto maxWaits = 100;
    auto waits = 0;
    while (template1->GetBlockRef()->GetTransactionCount() < count_entries + 1)
    {
        MilliSleep(waits);
        template1 = jba->CreateNewBlock(CScript{}, bla);
        if ( ++waits >= maxWaits)
        {
            BOOST_CHECK_GE(waits, maxWaits);
            break;
        }
    }
    auto& vtx1 = template1->GetBlockRef()->vtx;
    // remove coinbase
    vtx1.erase(vtx1.begin());

    // Check that mempool and JBA also hold the same shared pointers as the block template
    for(const auto& tx: vtx1) {
        BOOST_CHECK_EQUAL(tx.use_count(), 3);
    }

    auto moveToDisk = true; // set to false to test the test.

    // force writeout of everything
    testPool.SaveTxsToDisk(moveToDisk * totalSize_entries);
    testPoolAccess.SyncWithMempoolTxDB();
    BOOST_CHECK_EQUAL(testPool.Size(), poolSize);

    // But it does store something to disk:
    BOOST_CHECK_EQUAL(testPool.GetDiskUsage(), moveToDisk * totalSize_entries);
    BOOST_CHECK_EQUAL(testPool.GetDiskTxCount(), moveToDisk * count_entries);
    BOOST_CHECK(testPoolAccess.CheckMempoolTxDB());

    // Check that the mempool dropped it's shared pointer in the wrapper
    for(const auto& tx: vtx1) {
        BOOST_CHECK_EQUAL(tx.use_count(), 3 - moveToDisk);
    }

    // force JBA to create new journal by removing one entry
    {
        auto tx = vtx1.begin();
        testPoolAccess.RemoveRecursive(**tx, nullChangeSet);
        // forget the erased entry
        count_entries -= 1;
        totalSize_entries -= (*tx)->GetTotalSize();
        vtx1.erase(tx);
    }

    // Get another block template
    auto template2 = jba->CreateNewBlock(CScript{}, bla);
    // wait for JBA to notice the journal reset and to re-process all transactions
    waits = 0;
    while (template2->GetBlockRef() == template1->GetBlockRef() ||
           template2->GetBlockRef()->GetTransactionCount() < count_entries) {
        MilliSleep(waits);
        template2 = jba->CreateNewBlock(CScript{}, bla);
        if ( ++waits >= maxWaits)
        {
            BOOST_CHECK_GE(waits, maxWaits);
            break;
        }
    }

    auto& vtx2 = template2->GetBlockRef()->vtx;
    // remove coinbase
    vtx2.erase(vtx2.begin());

    // Check that the block template refcount went up the only shared pointers
    for(const auto& tx: vtx1) {
        BOOST_CHECK_EQUAL(tx.use_count(), 4 - moveToDisk);
    }

    // Check that the other block template has the same refcounts
    for(const auto& tx: vtx2) {
        BOOST_CHECK_EQUAL(tx.use_count(), 4 - moveToDisk);
    }

    BOOST_CHECK_EQUAL(vtx1.size(), vtx2.size());
    // check that both blocks share all the memory used by transactions
    auto set1 = std::set<CTransactionRef>(vtx1.cbegin(), vtx1.cend());
    for(auto tx: vtx2) {
        auto erased = set1.erase(tx);
        BOOST_CHECK(erased == 1);
    }
    BOOST_CHECK(set1.empty());
}

BOOST_AUTO_TEST_CASE(CheckMempoolTxDB)
{
    constexpr unsigned numberOfEntries = 6;
    const auto entries = GetABunchOfEntries(numberOfEntries);

    CTxMemPool testPool;
    CTxMemPoolTestAccess testPoolAccess(testPool);
    testPoolAccess.OpenMempoolTxDB();

    // Add transactions to the database that are not in the mempool.
    for (const auto& entry : entries)
    {
        // Create a copy of the transaction wrapper because Add() marks them as saved.
        testPoolAccess.mempoolTxDB()->Add(std::make_shared<CTransactionWrapper>(entry.GetSharedTx(), nullptr));
    }
    testPoolAccess.SyncWithMempoolTxDB();
    BOOST_CHECK_EQUAL(testPool.Size(), 0U);
    BOOST_CHECK_GT(testPool.GetDiskUsage(), 0U);
    BOOST_CHECK_GT(testPool.GetDiskTxCount(), 0U);
    BOOST_CHECK(!testPoolAccess.CheckMempoolTxDB());

    // Clearing the database should put everything right again.
    testPoolAccess.mempoolTxDB()->Clear();
    BOOST_CHECK_EQUAL(testPool.GetDiskUsage(), 0U);
    BOOST_CHECK_EQUAL(testPool.GetDiskTxCount(), 0U);
    BOOST_CHECK(testPoolAccess.CheckMempoolTxDB());

    // Add transactions to the mempool and mark them saved without writing to disk.
    for (auto& entry : entries)
    {
        testPool.AddUnchecked(entry.GetTxId(), entry, TxStorage::memory, nullChangeSet);
        auto it = testPoolAccess.mapTx().find(entry.GetTxId());
        BOOST_REQUIRE(it != testPoolAccess.mapTx().end());
        CTestTxMemPoolEntry::GetTxWrapper(*it)->ResetTransaction();
        BOOST_CHECK(entry.IsInMemory());
        BOOST_CHECK(!it->IsInMemory());
    }
    testPoolAccess.SyncWithMempoolTxDB();
    BOOST_CHECK_EQUAL(testPool.Size(), numberOfEntries);
    BOOST_CHECK_EQUAL(testPool.GetDiskUsage(), 0U);
    BOOST_CHECK_EQUAL(testPool.GetDiskTxCount(), 0U);
    BOOST_CHECK(!testPoolAccess.CheckMempoolTxDB());

    // Clearing the mempool should put everything right again.
    testPool.Clear();
    BOOST_CHECK_EQUAL(testPool.Size(), 0U);
    BOOST_CHECK_EQUAL(testPool.GetDiskUsage(), 0U);
    BOOST_CHECK_EQUAL(testPool.GetDiskTxCount(), 0U);
    BOOST_CHECK(testPoolAccess.CheckMempoolTxDB());
}


namespace {
    CTransactionWrapperRef MakeTxWrapper(std::shared_ptr<CMempoolTxDBReader> txdb)
    {
        const auto entries = GetABunchOfEntries(1);
        return std::make_shared<CTransactionWrapper>(entries[0].GetSharedTx(), txdb);
    }

    struct FakeMempoolTxDB : CMempoolTxDBReader
    {
        virtual bool GetTransaction(const uint256 &txid, CTransactionRef &tx) override
        {
            if (const auto it = database.find(txid); it != database.end())
            {
                // Always return a copy of the transaction in the database to
                // simulate actual reading from disk.
                tx = std::make_shared<CTransaction>(*it->second);;
                return true;
            }
            tx = nullptr;
            return false;
        }

        virtual bool TransactionExists(const uint256 &txid) override
        {
            return database.count(txid) > 0;
        }

        void QuoteSaveToDiskUnquote(const CTransactionWrapperRef& wrapper)
        {
            if (wrapper->IsInMemory())
            {
                // Create a separate copy of the transaction to avoid ownership
                // mixups with the wrapper.
                auto tx = std::make_shared<CTransaction>(*wrapper->GetTx());
                database.emplace(tx->GetId(), tx);
                wrapper->ResetTransaction();
            }
        }

        std::unordered_map<uint256, CTransactionRef> database;
    };

    void MultiCheck(const CTransactionRef& tx, const CTransactionWrapperRef& wrapper)
    {
        std::array<std::future<CTransactionRef>, 100> threads;
        for (auto& f : threads)
        {
            f = std::async(std::launch::async,
                           [&](){
                               return wrapper->GetTx();
                           });
        }

        const auto firsttx = threads[0].get();
        BOOST_CHECK(firsttx != nullptr);
        for (auto& f : threads)
        {
            if (f.valid())
            {
                BOOST_CHECK(f.get() == firsttx);
            }
        }
        if (tx)
        {
            BOOST_CHECK(firsttx == tx);
        }
    }
}

BOOST_AUTO_TEST_CASE(TxWrapper_UniqueOwned)
{
    const auto wrapper = MakeTxWrapper(nullptr);

    // Make sure the same wrapper always returns the same pointer when it's in-memory
    const auto tx = wrapper->GetTx();
    MultiCheck(tx, wrapper);
    BOOST_CHECK(wrapper->GetTx() == tx);
}

BOOST_AUTO_TEST_CASE(TxWrapper_UniqueOwnedWeak)
{
    const auto txdb = std::make_shared<FakeMempoolTxDB>();
    const auto wrapper = MakeTxWrapper(txdb);

    // Make sure the same wrapper always returns the same pointer when a tx is
    // kept in memory even when it's "saved to disk".
    const auto tx = wrapper->GetTx();
    txdb->QuoteSaveToDiskUnquote(wrapper);
    BOOST_CHECK(!wrapper->IsInMemory());
    BOOST_CHECK(txdb->TransactionExists(wrapper->GetId()));
    CTransactionRef anothertx;
    BOOST_CHECK(txdb->GetTransaction(wrapper->GetId(), anothertx));
    BOOST_CHECK(anothertx != tx);

    MultiCheck(tx, wrapper);
    BOOST_CHECK(wrapper->GetTx() == tx);
}

BOOST_AUTO_TEST_CASE(TxWrapper_EventuallyUniqueWeak)
{
    const auto txdb = std::make_shared<FakeMempoolTxDB>();
    const auto wrapper = MakeTxWrapper(txdb);

    // Make sure the same wrapper always returns the same pointer when it's been
    // read from the txdb once.
    auto tx = wrapper->GetTx();
    txdb->QuoteSaveToDiskUnquote(wrapper);
    BOOST_CHECK(!wrapper->IsInMemory());
    BOOST_CHECK(txdb->TransactionExists(wrapper->GetId()));

    CTransactionRef savedtx;
    BOOST_CHECK(txdb->GetTransaction(wrapper->GetId(), savedtx));
    BOOST_CHECK(savedtx != nullptr);
    BOOST_CHECK(savedtx != tx);
    BOOST_CHECK(savedtx->GetId() == wrapper->GetId());

    // At this point, we stil have a live copy of the pointer, so the wrapper
    // should be able to return it.
    BOOST_CHECK(wrapper->GetTx() == tx);

    // Throw away all live tx pointers, only the one in the database exists and
    // it's different than the weak reference in the wrapper.
    savedtx.reset();
    tx.reset();

    // The wrapper should read from the database exactly once.
    MultiCheck(nullptr, wrapper);
    BOOST_CHECK(wrapper->GetTx() != nullptr);
}

BOOST_AUTO_TEST_SUITE_END()
