// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#include "config.h"
#include "mempooltxdb.h"
#include "mining/journal_change_set.h"

#include "mempool_test_access.h"

#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>

#include <vector>

namespace {
    mining::CJournalChangeSetPtr nullChangeSet{nullptr};

    std::vector<CTxMemPoolEntry> GetABunchOfEntries(int howMany, bool expired=false)
    {
        TestMemPoolEntryHelper entry;
        if (!expired) {
            entry.Time(GetTime());
        }

        std::vector<CTxMemPoolEntry> result;
        for (int i = 0; i < howMany; i++) {
            CMutableTransaction mtx;
            mtx.vin.resize(1);
            mtx.vin[0].scriptSig = CScript() << OP_11;
            mtx.vout.resize(1);
            mtx.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
            mtx.vout[0].nValue = Amount(33000LL + i);
            result.emplace_back(entry.FromTx(mtx));
        }
        return result;
    }

    struct Validator
    {
        explicit Validator(CTxMemPool& pool_)
            : pool(pool_)
        {}

        CValidationState operator()(const TxInputDataSPtr& txInputData,
                                    const mining::CJournalChangeSetPtr& changeSet,
                                    bool limitMempoolSize)
        {
            auto entry = std::make_shared<CTxMemPoolEntry>(
                helper.Time(txInputData->GetAcceptTime())
                .FromTx(*txInputData->GetTxnPtr()));
            pool.AddUnchecked(entry->GetTxId(), *entry,
                              txInputData->GetTxStorage(),
                              changeSet);
            return CValidationState();
        }

    private:
        CTxMemPool& pool;
        TestMemPoolEntryHelper helper;
    };

    bool LoadMempool(CTxMemPoolTestAccess& poolAccess, Config& testConfig)
    {
        auto token = task::CCancellationSource::Make()->GetToken();
        auto validator = Validator(poolAccess.mempool);
        const auto validate =
            [&validator](const TxInputDataSPtr& txInputData,
                         const mining::CJournalChangeSetPtr& changeSet,
                         bool limitMempoolSize) -> CValidationState
            {
                return validator(txInputData, changeSet, limitMempoolSize);
            };
        return poolAccess.LoadMempool(testConfig, token, validate);
    }

    uint64_t PrepareMempoolDat(const std::vector<CTxMemPoolEntry>& entries,
                               const uint64_t version, const bool save)
    {
        uint64_t count = 0;
        CTxMemPool testPool;
        CTxMemPoolTestAccess testPoolAccess(testPool);

        // Add transactions:
        testPool.InitMempoolTxDB();
        for (auto& entry : entries) {
            testPool.AddUnchecked(entry.GetTxId(), entry, TxStorage::memory, nullChangeSet);
        }
        BOOST_CHECK_EQUAL(testPool.Size(), entries.size());
        BOOST_CHECK_EQUAL(testPool.GetDiskTxCount(), 0);

        if (save)
        {
            // Save half of the transactions to disk.
            size_t size = 0;
            count = entries.size() / 2;
            for (uint64_t i = 0; i < count; ++i)
            {
                size += entries[i].GetTxSize();
            }
            testPool.SaveTxsToDisk(size);
            testPoolAccess.SyncWithMempoolTxDB();
            BOOST_CHECK_EQUAL(testPool.GetDiskTxCount(), count);
        }

        // Dump the mempool and forget about it.
        testPoolAccess.DumpMempool(version);
        BOOST_CHECK_EQUAL(testPool.GetDiskTxCount(), count);
        return count;
    }

    void LoadMempoolDat(const std::vector<CTxMemPoolEntry>& entries, Config& testConfig,
                        const uint64_t count, const bool expired)
    {
        CTxMemPool testPool;
        CTxMemPoolTestAccess testPoolAccess(testPool);

        testPool.SuspendSanityCheck();
        testPool.InitMempoolTxDB();
        BOOST_CHECK(LoadMempool(testPoolAccess, testConfig));
        testPool.ResumeSanityCheck();

        BOOST_CHECK(testPoolAccess.CheckMempoolTxDB());
        if (!expired)
        {
            BOOST_CHECK_EQUAL(testPool.Size(), entries.size());
            BOOST_CHECK_EQUAL(testPool.GetDiskTxCount(), count);
            for (const auto& entry : entries)
            {
                BOOST_CHECK(testPool.Exists(entry.GetTxId()));
            }
        }
        else
        {
            BOOST_CHECK_EQUAL(testPool.Size(), 0);
            BOOST_CHECK_EQUAL(testPool.GetDiskTxCount(), 0);
            for (const auto& entry : entries)
            {
                BOOST_CHECK(!testPool.Exists(entry.GetTxId()));
            }
        }
    }
}

BOOST_FIXTURE_TEST_SUITE(mempool_dump_load_tests, TestingSetup)
BOOST_AUTO_TEST_CASE(DumpLoadFormat1)
{
    gArgs.ForceSetBoolArg("-persistmempool", true);
    const auto entries = GetABunchOfEntries(6);
    const auto count = PrepareMempoolDat(entries, 1, false);
    BOOST_CHECK_EQUAL(count, 0);
    LoadMempoolDat(entries, testConfig, count, false);
}

BOOST_AUTO_TEST_CASE(DumpLoadFormat1Empty)
{
    gArgs.ForceSetBoolArg("-persistmempool", true);
    const auto entries = GetABunchOfEntries(0);
    const auto count = PrepareMempoolDat(entries, 1, false);
    BOOST_CHECK_EQUAL(count, 0);
    LoadMempoolDat(entries, testConfig, count, false);
}

BOOST_AUTO_TEST_CASE(DumpLoadFormat1WithOnDiskTxs)
{
    gArgs.ForceSetBoolArg("-persistmempool", true);
    const auto entries = GetABunchOfEntries(6);
    const auto count = PrepareMempoolDat(entries, 1, true);
    BOOST_CHECK_EQUAL(count, entries.size() / 2);
    LoadMempoolDat(entries, testConfig, 0, false);
}

BOOST_AUTO_TEST_CASE(DumpLoadFormat1Expired)
{
    gArgs.ForceSetBoolArg("-persistmempool", true);
    const auto entries = GetABunchOfEntries(6, true);
    const auto count = PrepareMempoolDat(entries, 1, false);
    BOOST_CHECK_EQUAL(count, 0);
    LoadMempoolDat(entries, testConfig, 0, true);
}

BOOST_AUTO_TEST_CASE(DumpLoadFormat2)
{
    gArgs.ForceSetBoolArg("-persistmempool", true);
    const auto entries = GetABunchOfEntries(6);
    const auto count = PrepareMempoolDat(entries, 2, false);
    BOOST_CHECK_EQUAL(count, 0);
    LoadMempoolDat(entries, testConfig, count, false);
}

BOOST_AUTO_TEST_CASE(DumpLoadFormat2Empty)
{
    gArgs.ForceSetBoolArg("-persistmempool", true);
    const auto entries = GetABunchOfEntries(0);
    const auto count = PrepareMempoolDat(entries, 2, false);
    BOOST_CHECK_EQUAL(count, 0);
    LoadMempoolDat(entries, testConfig, count, false);
}

BOOST_AUTO_TEST_CASE(DumpLoadFormat2WithOnDiskTxs)
{
    gArgs.ForceSetBoolArg("-persistmempool", true);
    const auto entries = GetABunchOfEntries(6);
    const auto count = PrepareMempoolDat(entries, 2, true);
    BOOST_CHECK_EQUAL(count, entries.size() / 2);
    LoadMempoolDat(entries, testConfig, count, false);
}

BOOST_AUTO_TEST_CASE(DumpLoadFormat2Expired)
{
    gArgs.ForceSetBoolArg("-persistmempool", true);
    const auto entries = GetABunchOfEntries(6, true);
    const auto count = PrepareMempoolDat(entries, 2, false);
    BOOST_CHECK_EQUAL(count, 0);
    LoadMempoolDat(entries, testConfig, 0, true);
}

BOOST_AUTO_TEST_CASE(DumpLoadFormat2WithOnDiskTxsExpired)
{
    gArgs.ForceSetBoolArg("-persistmempool", true);
    const auto entries = GetABunchOfEntries(6, true);
    const auto count = PrepareMempoolDat(entries, 2, true);
    BOOST_CHECK_EQUAL(count, entries.size() / 2);
    LoadMempoolDat(entries, testConfig, 0, true);
}
BOOST_AUTO_TEST_SUITE_END()
