// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#include "mining/journal_change_set.h"
#include "validation.h"
#include "mempooltxdb.h"

#include "mempool_test_access.h"

#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>

#include <vector>

namespace {
    mining::CJournalChangeSetPtr nullChangeSet{nullptr};

    // large enough count to control integer rounding errors in fractions
    constexpr unsigned N_PRIMARY = 50;
    // fixed size transactions so we can correlate sizes and counts
    // large enough transactions so we are not dominated by index ram usage
    constexpr int TX_SIZE = 1000;
    const CFeeRate A_PRIMARY = CFeeRate(Amount(6000));
    const CFeeRate A_BLOCK_MIN_FEE = CFeeRate(Amount(1000));
    const CFeeRate A_SECONDARY = CFeeRate(Amount(100));

    // test representation of mempool entry
    struct Entry {
        TxId txId;
        bool forPrimary;
        size_t size;
        Entry(TxId txId, bool forPrimary, size_t size)
        : txId(txId), forPrimary(forPrimary), size(size) {}
    };

    using Predicate = std::function<bool(CTxMemPoolTestAccess&, const Entry&)>;

    // a collection of entries added to mempool
	struct Entries {
        CTxMemPool &pool;
        std::vector<Entry> entries;
        Entries(CTxMemPool& pool) : pool(pool) {}
        // return entries that satisfy the predicate by consulting the actual mempool entries
        Entries that(Predicate predicate) const {
            return filter(with(predicate));
        }
        // return entries that were submitted to primary mempool
        Entries for_primary() const {
            return filter([](const Entry& entry){return entry.forPrimary;});
        }
        // return entries that were submitted to secondary mempool
        Entries for_secondary() const {
            return filter([](const Entry& entry){return !entry.forPrimary;});
        }
        // number of entries
        size_t count() const { return entries.size(); }
        // number of bytes consumed by transactions of entries
        size_t size() const {
            return std::accumulate(entries.begin(), entries.end(), static_cast<size_t>(0),
                                   [](size_t size, const Entry& entry) {
                return size + entry.size;
                });
            }
	private:
        using Filter = std::function<bool(const Entry&)>;
        Entries filter(Filter cond) const {
            Entries ret(pool);
            std::copy_if(entries.cbegin(), entries.cend(), std::back_inserter(ret.entries), cond);
            return ret;
        }
        Filter with(Predicate predicate) const {
            return [this, predicate](const Entry& entry) {
                CTxMemPoolTestAccess testPoolAccess(pool);
                return predicate(testPoolAccess, entry);
            };
        }
    };

    // Predicates
    bool in_pool(CTxMemPoolTestAccess& pool, const Entry& entry) {
        auto it = pool.mapTx().find(entry.txId);
        return it != pool.mapTx().end();
    }

    bool in_memory(CTxMemPoolTestAccess& pool, const Entry& entry) {
        auto it = pool.mapTx().find(entry.txId);
        return (it != pool.mapTx().end()) && it->IsInMemory();
    }

    bool on_disk(CTxMemPoolTestAccess& pool, const Entry& entry) {
        auto it = pool.mapTx().find(entry.txId);
        return (it != pool.mapTx().end()) && !it->IsInMemory();
    }

    bool in_primary(CTxMemPoolTestAccess& pool, const Entry& entry) {
        auto it = pool.mapTx().find(entry.txId);
        return (it != pool.mapTx().end()) && it->IsInPrimaryMempool();
    }

    bool in_secondary(CTxMemPoolTestAccess& pool, const Entry& entry) {
        auto it = pool.mapTx().find(entry.txId);
        return (it != pool.mapTx().end()) && !it->IsInPrimaryMempool();
    }

    Predicate are(Predicate predicate) {
        return predicate;
    }

	Predicate are_not(Predicate predicate) {
        return [predicate](CTxMemPoolTestAccess& pool, const Entry& entry) {
            return !predicate(pool, entry);
        };
    }

	struct Demand {
        int howMany;
        CFeeRate fee;
        Demand(int howMany, CFeeRate fee) : howMany(howMany), fee(fee) {}
    };

    std::vector<CTxMemPoolEntry> GetABunchOfEntries(Demand demand)
    {
        static int unique = 0;
        std::vector<uint8_t> fluff;
        fluff.resize(TX_SIZE - 71, 42);    // subtract base transaction size
        TestMemPoolEntryHelper entry;
        std::vector<CTxMemPoolEntry> entries;
        for (int i = 0; i < demand.howMany; i++) {
            CMutableTransaction mtx;
            mtx.vin.resize(1);
            mtx.vin[0].prevout = COutPoint(uint256(), unique++);
            mtx.vin[0].scriptSig = CScript() << fluff << OP_DROP << 11 << OP_DROP << OP_11;
            mtx.vout.resize(1);
            mtx.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
            mtx.vout[0].nValue = Amount(33000LL);
            // add a bounded oscillating offset to the fee to fight eviction order assumptions
            Amount fee = (demand.fee.GetFee(TX_SIZE)
                          + ((50 * Amount(i%2 ? i : -i)) / demand.howMany));
            entries.emplace_back(entry.Fee(fee).Time(GetTime()).FromTx(mtx));
        }
        return entries;
    }

    // add requested kinds and number of transactions to the mempool
    Entries StuffMempool(CTxMemPool& pool, std::initializer_list<Demand> demand)
    {
        CTxMemPoolTestAccess testPoolAccess(pool);
        testPoolAccess.SetBlockMinTxFee(A_BLOCK_MIN_FEE);

        std::vector<std::vector<CTxMemPoolEntry>> all_txns;
        for (auto d: demand) {
            all_txns.push_back(GetABunchOfEntries(d));
        }
        Entries entries {pool};
        // intersperse transactions to prevent arrival order assumptions by the tests
        bool progress = true;
        while(progress) {
            progress = false;
            for(auto& txns : all_txns) {
                if (txns.empty()) {
                    continue;
                }
                progress = true;
                CTxMemPoolEntry txn = txns.back();
                txns.pop_back();
                const TxId txId = txn.GetTxId();
                pool.AddUnchecked(txId, txn, TxStorage::memory, nullChangeSet);
                bool isForPrimary = txn.GetFee() >= A_BLOCK_MIN_FEE.GetFee(txn.GetTxSize());
                entries.entries.push_back({txId, isForPrimary, txn.GetTxSize()});
            }
        }
        // test the basic assumptions how Entries interacts with the mempool
        BOOST_TEST(entries.for_primary().count() == entries.that(are(in_primary)).count());
        BOOST_TEST(entries.for_secondary().count() == entries.that(are(in_secondary)).count());
        BOOST_TEST(pool.DynamicMemoryUsage() > entries.size());
        BOOST_TEST(pool.GetDiskUsage() == 0U);
        BOOST_TEST(pool.SecondaryMempoolUsage() >= entries.for_secondary().size());
        return entries;
    }

	// synchronize with async mempooldb thread
	void sync(CTxMemPool& pool) {
        CTxMemPoolTestAccess testPoolAccess(pool);
        testPoolAccess.SyncWithMempoolTxDB();
    }
}

BOOST_FIXTURE_TEST_SUITE(mempool_limit_tests, TestingSetup)

BOOST_AUTO_TEST_CASE(PrimaryBelowLimitAllInRamSecondaryBelowLimit)
{
    CTxMemPool testPool;

    const auto entries = StuffMempool(testPool, {{N_PRIMARY, A_PRIMARY}, {N_PRIMARY/11, A_SECONDARY}});

    const auto poolTotal = testPool.DynamicMemoryUsage();
    const auto poolSecondary = testPool.SecondaryMempoolUsage();
    const auto limitTotal = poolTotal + 10000;
    const auto limitMemory = poolTotal;
    const auto limitDisk = limitTotal - limitMemory;

    LimitMempoolSize(testPool, nullChangeSet, MempoolSizeLimits(limitMemory, limitDisk, limitTotal/10, 1000000));
    sync(testPool);

    BOOST_TEST(entries.count() == entries.that(are(in_memory)).count());
    BOOST_TEST(entries.that(are(on_disk)).count() == 0U);
    BOOST_TEST(testPool.DynamicMemoryUsage() == poolTotal);
    BOOST_TEST(testPool.GetDiskUsage() == 0U);
    BOOST_TEST(testPool.SecondaryMempoolUsage() == poolSecondary);
}

BOOST_AUTO_TEST_CASE(PrimaryBelowLimitAllInRamSecondaryAboveLimit)
{
    CTxMemPool testPool;

    const auto entries = StuffMempool(testPool, {{N_PRIMARY, A_PRIMARY}, {N_PRIMARY, A_SECONDARY}});

    const auto poolTotal = testPool.DynamicMemoryUsage();
    const auto poolSecondary = testPool.SecondaryMempoolUsage();
    const auto limitTotal = poolTotal + 10000;
    const auto limitMemory = poolTotal;
    const auto limitDisk = limitTotal - limitMemory;

    LimitMempoolSize(testPool, nullChangeSet, MempoolSizeLimits(limitMemory, limitDisk, limitTotal/10, 1000000));
    sync(testPool);

    BOOST_TEST(entries.that(are(in_pool)).count() > entries.for_primary().count());
    BOOST_TEST(entries.that(are(in_pool)).count() <= entries.for_primary().count() + entries.for_secondary().count()/3);
    BOOST_TEST(entries.that(are(in_memory)).count() = entries.that(are(in_pool)).count());
    BOOST_TEST(entries.that(are(on_disk)).count() == 0U);
    BOOST_TEST(entries.for_secondary().that(are(in_memory)).count() >= N_PRIMARY / 10);
    BOOST_TEST(entries.for_secondary().that(are(in_memory)).count() <= 2 * N_PRIMARY / 10 + 3);
    BOOST_TEST(testPool.DynamicMemoryUsage() <= poolTotal - entries.for_secondary().that(are_not(in_pool)).size());
    BOOST_TEST(testPool.GetDiskUsage() == 0U);
    BOOST_TEST(testPool.SecondaryMempoolUsage() >= entries.for_secondary().that(are(in_pool)).size());
    BOOST_TEST(testPool.SecondaryMempoolUsage() < poolSecondary);
}



BOOST_AUTO_TEST_CASE(PrimaryBelowLimitAllOnDiskSecondaryBelowLimit)
{
    CTxMemPool testPool;

    const auto entries = StuffMempool(testPool, {{N_PRIMARY, A_PRIMARY}, {N_PRIMARY/11, A_SECONDARY}});

    const auto poolTotal = testPool.DynamicMemoryUsage();
    const auto poolSecondary = testPool.SecondaryMempoolUsage();
    const auto limitTotal = poolTotal + 10000;
    const auto limitMemory = 100;
    const auto limitDisk = limitTotal - limitMemory;

    LimitMempoolSize(testPool, nullChangeSet, MempoolSizeLimits(limitMemory, limitDisk, limitTotal/10, 1000000));
    sync(testPool);

    BOOST_TEST(entries.that(are(in_pool)).count() == entries.count());
    BOOST_TEST(entries.that(are(in_memory)).count() == 0U);
    BOOST_TEST(entries.that(are(on_disk)).count() == entries.count());
    BOOST_TEST(testPool.DynamicMemoryUsage() == poolTotal);
    BOOST_TEST(testPool.GetDiskUsage() >= entries.size());
    BOOST_TEST(testPool.SecondaryMempoolUsage() >= entries.for_secondary().size());
    BOOST_TEST(testPool.SecondaryMempoolUsage() <= poolSecondary);
}

BOOST_AUTO_TEST_CASE(PrimaryBelowLimitAllOnDiskSecondaryAboveLimit)
{
    CTxMemPool testPool;

    const auto entries = StuffMempool(testPool, {{N_PRIMARY, A_PRIMARY}, {N_PRIMARY, A_SECONDARY}});

    const auto poolTotal = testPool.DynamicMemoryUsage();
    const auto poolSecondary = testPool.SecondaryMempoolUsage();
    const auto limitTotal = poolTotal + 10000;
    const auto limitMemory = 100;
    const auto limitDisk = limitTotal - limitMemory;

    LimitMempoolSize(testPool, nullChangeSet, MempoolSizeLimits(limitMemory, limitDisk, limitTotal/10, 1000000));
    sync(testPool);

    BOOST_TEST(entries.that(are(in_pool)).count() > entries.for_primary().count());
    BOOST_TEST(entries.that(are(in_pool)).count() <= entries.for_primary().count() + entries.for_secondary().count()/3);
    BOOST_TEST(entries.that(are(in_memory)).count() == 0U);
    BOOST_TEST(entries.that(are(on_disk)).count() == entries.that(are(in_pool)).count());
    BOOST_TEST(entries.for_secondary().that(are(in_pool)).count() >= N_PRIMARY / 10);
    BOOST_TEST(entries.for_secondary().that(are(in_pool)).count() <= 3 * N_PRIMARY / 10);
    BOOST_TEST(testPool.DynamicMemoryUsage() <= poolTotal - entries.that(are_not(in_pool)).size());
    BOOST_TEST(testPool.GetDiskUsage() >= entries.that(are(in_pool)).size());
    BOOST_TEST(testPool.SecondaryMempoolUsage() < poolSecondary);
    BOOST_TEST(testPool.SecondaryMempoolUsage() >= entries.for_secondary().that(are(in_pool)).size());
}

BOOST_AUTO_TEST_CASE(PrimaryBelowLimitHalfOnDiskSecondaryBelowLimit)
{
    CTxMemPool testPool;

    const auto entries = StuffMempool(testPool, {{N_PRIMARY, A_PRIMARY}, {N_PRIMARY/11, A_SECONDARY}});

    const auto poolTotal = testPool.DynamicMemoryUsage();
    const auto poolSecondary = testPool.SecondaryMempoolUsage();
    const auto limitTotal = poolTotal + 10000;
    const auto limitMemory = poolTotal / 2;
    const auto limitDisk = limitTotal - limitMemory;

    LimitMempoolSize(testPool, nullChangeSet, MempoolSizeLimits(limitMemory, limitDisk, limitTotal/10, 1000000));
    sync(testPool);

    BOOST_TEST(entries.that(are(in_pool)).count() == entries.count());
    BOOST_TEST(entries.that(are(in_memory)).count() < entries.count() / 2);
    BOOST_TEST(entries.that(are(on_disk)).count() > entries.count() / 2);
    BOOST_TEST(entries.for_secondary().that(are(in_pool)).count() >= N_PRIMARY / 11);
    BOOST_TEST(entries.for_secondary().that(are(in_pool)).count() <= 2 * N_PRIMARY / 10);
    BOOST_TEST(testPool.DynamicMemoryUsage() == poolTotal);
    BOOST_TEST(testPool.GetDiskUsage() >= entries.that(are(in_pool)).size() / 2);
    BOOST_TEST(testPool.SecondaryMempoolUsage() == poolSecondary);
}

BOOST_AUTO_TEST_CASE(PrimaryBelowLimitHalfOnDiskSecondaryAboveLimit)
{
    CTxMemPool testPool;

    const auto entries = StuffMempool(testPool, {{N_PRIMARY, A_PRIMARY}, {N_PRIMARY, A_SECONDARY}});

    const auto poolTotal = testPool.DynamicMemoryUsage();
    const auto poolSecondary = testPool.SecondaryMempoolUsage();
    const auto limitTotal = poolTotal + 10000;
    const auto limitMemory = poolTotal / 3;
    const auto limitDisk = limitTotal - limitMemory;

    LimitMempoolSize(testPool, nullChangeSet, MempoolSizeLimits(limitMemory, limitDisk, limitTotal/10, 1000000));
    sync(testPool);


    BOOST_TEST(entries.that(are(in_pool)).count() < entries.count());
    BOOST_TEST(entries.that(are(in_pool)).count() >= 11 * entries.for_primary().count() / 10);
    BOOST_TEST(entries.that(are(on_disk)).count() <= entries.that(are(in_pool)).count());
    BOOST_TEST(entries.that(are(on_disk)).count() > entries.that(are(in_pool)).count() / 6);
    BOOST_TEST(entries.for_secondary().that(are(in_pool)).count() >= N_PRIMARY / 10);
    BOOST_TEST(entries.for_secondary().that(are(in_pool)).count() <= 3 * N_PRIMARY / 10);
    BOOST_TEST(testPool.DynamicMemoryUsage() <= poolTotal - entries.that(are_not(in_pool)).size());
    BOOST_TEST(testPool.GetDiskUsage() >= entries.that(are(on_disk)).size());
    BOOST_TEST(testPool.SecondaryMempoolUsage() < poolSecondary);
    BOOST_TEST(testPool.SecondaryMempoolUsage() >= entries.for_secondary().that(are(in_pool)).size());

}

BOOST_AUTO_TEST_CASE(PrimaryAtLimitAllInRam)
{
    CTxMemPool testPool;

    const auto entries = StuffMempool(testPool, {{N_PRIMARY, A_PRIMARY},});

    const auto poolTotal = testPool.DynamicMemoryUsage();
    const auto limitTotal = poolTotal;
    const auto limitMemory = limitTotal;
    const auto limitDisk = limitTotal - limitMemory;

    LimitMempoolSize(testPool, nullChangeSet, MempoolSizeLimits(limitMemory, limitDisk, limitTotal/10, 1000000));
    sync(testPool);

    BOOST_TEST(entries.that(are(in_memory)).count() == entries.count());
    BOOST_TEST(testPool.DynamicMemoryUsage() == limitTotal);
    BOOST_TEST(testPool.GetDiskUsage() == 0U);
    BOOST_TEST(testPool.SecondaryMempoolUsage() == 0U);
}

BOOST_AUTO_TEST_CASE(PrimaryAtLimitAllOnDisk)
{
    CTxMemPool testPool;

    const auto entries = StuffMempool(testPool, {{N_PRIMARY, A_PRIMARY},});

    const auto poolTotal = testPool.DynamicMemoryUsage();
    const auto limitTotal = poolTotal;
    const auto limitMemory = 0;
    const auto limitDisk = limitTotal - limitMemory;

    LimitMempoolSize(testPool, nullChangeSet, MempoolSizeLimits(limitMemory, limitDisk, limitTotal/10, 1000000));
    sync(testPool);

    BOOST_TEST(entries.that(are(on_disk)).count() == entries.count());
    BOOST_TEST(testPool.DynamicMemoryUsage() == limitTotal);
    BOOST_TEST(testPool.GetDiskUsage() >= entries.size());
    BOOST_TEST(testPool.SecondaryMempoolUsage() == 0U);
}

BOOST_AUTO_TEST_CASE(PrimaryAtLimitThirdOnDiskSecondaryGone)
{
    CTxMemPool testPool;

    const auto entries = StuffMempool(testPool, {{N_PRIMARY, A_PRIMARY}, {2, A_SECONDARY}});

    const auto poolTotal = testPool.DynamicMemoryUsage();
    const auto limitTotal = poolTotal - testPool.SecondaryMempoolUsage() + 1000;
    const auto limitMemory = (limitTotal * 2) / 3;
    const auto limitDisk = limitTotal - limitMemory;

    LimitMempoolSize(testPool, nullChangeSet, MempoolSizeLimits(limitMemory, limitDisk, limitTotal/10, 1000000));
    sync(testPool);

    BOOST_TEST(entries.that(are(in_pool)).count() < entries.count());
    BOOST_TEST(entries.that(are(in_pool)).count() == entries.for_primary().count());
    BOOST_TEST(entries.that(are(in_memory)).count() <= ((N_PRIMARY * 2) / 3));
    BOOST_TEST(entries.that(are(on_disk)).count() >= ((N_PRIMARY * 1) / 3));
    BOOST_TEST(testPool.DynamicMemoryUsage() <= poolTotal - entries.that(are_not(in_pool)).size());
    BOOST_TEST(testPool.GetDiskUsage() >= entries.that(are(on_disk)).size());
    BOOST_TEST(testPool.SecondaryMempoolUsage() == 0U);
}

BOOST_AUTO_TEST_CASE(PrimaryAboveLimitInRamSecondaryGone)
{
    CTxMemPool testPool;

    const auto entries = StuffMempool(testPool, {{N_PRIMARY, A_PRIMARY}, {2, A_SECONDARY}});

    const auto poolTotal = testPool.DynamicMemoryUsage();
    const auto limitTotal = (poolTotal * 2) / 3 + 10000;
    const auto limitMemory = limitTotal;
    const auto limitDisk = limitTotal - limitMemory;

    LimitMempoolSize(testPool, nullChangeSet, MempoolSizeLimits(limitMemory, limitDisk, limitTotal/10, 1000000));
    sync(testPool);

    BOOST_TEST(entries.that(are(in_pool)).count() >= (N_PRIMARY * 2) / 3);
    BOOST_TEST(entries.that(are(in_memory)).count() >= N_PRIMARY / 3);
    BOOST_TEST(entries.for_secondary().that(are(in_pool)).count() == 0U);
    BOOST_TEST(testPool.DynamicMemoryUsage() <= limitTotal);
    BOOST_TEST(testPool.DynamicMemoryUsage() > entries.that(are(in_pool)).size());
    BOOST_TEST(testPool.GetDiskUsage() == 0U);
    BOOST_TEST(testPool.SecondaryMempoolUsage() == 0U);
}

BOOST_AUTO_TEST_CASE(PrimaryAboveLimitAllOnDiskSecondaryGone)
{
    CTxMemPool testPool;

    const auto entries = StuffMempool(testPool, {{N_PRIMARY, A_PRIMARY}, {2, A_SECONDARY}});

    const auto poolTotal = testPool.DynamicMemoryUsage();
    const auto limitTotal = (poolTotal * 2) / 3 + 10000;
    const auto limitMemory = 0;
    const auto limitDisk = limitTotal - limitMemory;

    LimitMempoolSize(testPool, nullChangeSet, MempoolSizeLimits(limitMemory, limitDisk, limitTotal/10, 1000000));
    sync(testPool);

    BOOST_TEST(entries.that(are(in_pool)).count() >= (N_PRIMARY * 2) / 3);
    BOOST_TEST(entries.that(are(in_memory)).count() == 0U);
    BOOST_TEST(entries.for_secondary().that(are(in_pool)).count() == 0U);
    BOOST_TEST(testPool.DynamicMemoryUsage() <= limitTotal);
    BOOST_TEST(testPool.DynamicMemoryUsage() > entries.that(are(in_pool)).size());
    BOOST_TEST(testPool.GetDiskUsage() >= entries.that(are(on_disk)).size());
    BOOST_TEST(testPool.SecondaryMempoolUsage() == 0U);
}

BOOST_AUTO_TEST_CASE(PrimaryAboveLimitThirdOnDiskSecondaryGone)
{
    CTxMemPool testPool;

    const auto entries = StuffMempool(testPool, {{N_PRIMARY, A_PRIMARY}, {2, A_SECONDARY}});

    const auto poolTotal = testPool.DynamicMemoryUsage();
    const auto limitTotal = (poolTotal * 2) / 3 + 10000;
    const auto limitMemory = (limitTotal * 2) / 3;
    const auto limitDisk = limitTotal - limitMemory;

    LimitMempoolSize(testPool, nullChangeSet, MempoolSizeLimits(limitMemory, limitDisk, limitTotal/10, 1000000));
    sync(testPool);

    BOOST_TEST(entries.that(are(in_pool)).count() >= (N_PRIMARY * 2) / 3);
    BOOST_TEST(entries.that(are(in_memory)).count() < (N_PRIMARY * 4) / 9);
    BOOST_TEST(entries.for_secondary().that(are(in_pool)).count() == 0U);
    BOOST_TEST(testPool.DynamicMemoryUsage() <= limitTotal);
    BOOST_TEST(testPool.DynamicMemoryUsage() > entries.that(are(in_pool)).size());
    BOOST_TEST(testPool.GetDiskUsage() >= entries.that(are(on_disk)).size());
    BOOST_TEST(testPool.SecondaryMempoolUsage() == 0U);
}

BOOST_AUTO_TEST_SUITE_END()
