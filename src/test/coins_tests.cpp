// Copyright (c) 2014-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "coins.h"
#include "consensus/validation.h"
#include "script/standard.h"
#include "test/test_bitcoin.h"
#include "testutil.h"
#include "uint256.h"
#include "undo.h"
#include "utilstrencodings.h"
#include "validation.h"
#include "chainparams.h"

#include <chrono>
#include <map>
#include <vector>

#include <boost/test/unit_test.hpp>
#include <config.h>

namespace{ class coins_tests_uid; } // only used as unique identifier

template <>
struct CoinsStore::UnitTestAccess<coins_tests_uid>
{
    UnitTestAccess() = delete;

    static CCoinsMap& GetRawCacheCoins(CoinsStore& cache) { return cache.cacheCoins; }
    static size_t& GetCachedCoinsUsage(CoinsStore& cache) { return cache.cachedCoinsUsage; }
};
using TestAccessCoinsCache = CoinsStore::UnitTestAccess<coins_tests_uid>;

template <>
struct CoinsDBSpan::UnitTestAccess<coins_tests_uid> : public CoinsDBSpan
{
    using CoinsDBSpan::CoinsDBSpan;

    CCoinsMap& GetRawCacheCoins() { return TestAccessCoinsCache::GetRawCacheCoins(mCache); }
    size_t& GetCachedCoinsUsage() { return TestAccessCoinsCache::GetCachedCoinsUsage(mCache); }

    void BatchWrite(CCoinsMap& mapCoins, const uint256& hashBlockIn)
    {
        mCache.BatchWrite(mapCoins);
        hashBlock = hashBlockIn;
    }
};
using TestCoinsSpanCache = CoinsDBSpan::UnitTestAccess<coins_tests_uid>;

namespace {

class CCoinsViewCacheTest : public TestCoinsSpanCache {
public:
    CCoinsViewCacheTest( CoinsDB& db ) : TestCoinsSpanCache(db) {}

    void SelfTest() {
        // Manually recompute the dynamic usage of the whole data, and compare
        // it.
        size_t ret = memusage::DynamicUsage(GetRawCacheCoins());
        size_t count = 0;
        for (const auto& entry : GetRawCacheCoins())
        {
            ret += entry.second.DynamicMemoryUsage();
            count++;
        }
        BOOST_CHECK_EQUAL(GetRawCacheCoins().size(), count);
        BOOST_CHECK_EQUAL(DynamicMemoryUsage(), ret);
    }
};

} // namespace

BOOST_FIXTURE_TEST_SUITE(coins_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(coin_serialization) {
    // Good example
    CDataStream ss1(
        ParseHex("97f23c835800816115944e077fe7c803cfa57f29b36bf87c1d35"),
        SER_DISK, CLIENT_VERSION);
    Coin c1;
    ss1 >> c1;
    BOOST_CHECK_EQUAL(c1.IsCoinBase(), false);
    BOOST_CHECK_EQUAL(c1.GetHeight(), 203998U);
    BOOST_CHECK_EQUAL(c1.GetTxOut().nValue, Amount(60000000000LL));
    BOOST_CHECK_EQUAL(HexStr(c1.GetTxOut().scriptPubKey),
                      HexStr(GetScriptForDestination(CKeyID(uint160(ParseHex(
                          "816115944e077fe7c803cfa57f29b36bf87c1d35"))))));

    // Good example
    CDataStream ss2(
        ParseHex("8ddf77bbd123008c988f1a4a4de2161e0f50aac7f17e7f9555caa4"),
        SER_DISK, CLIENT_VERSION);
    Coin c2;
    ss2 >> c2;
    BOOST_CHECK_EQUAL(c2.IsCoinBase(), true);
    BOOST_CHECK_EQUAL(c2.GetHeight(), 120891);
    BOOST_CHECK_EQUAL(c2.GetTxOut().nValue, Amount(110397LL));
    BOOST_CHECK_EQUAL(HexStr(c2.GetTxOut().scriptPubKey),
                      HexStr(GetScriptForDestination(CKeyID(uint160(ParseHex(
                          "8c988f1a4a4de2161e0f50aac7f17e7f9555caa4"))))));

    // Smallest possible example
    CDataStream ss3(ParseHex("000006"), SER_DISK, CLIENT_VERSION);
    Coin c3;
    ss3 >> c3;
    BOOST_CHECK_EQUAL(c3.IsCoinBase(), false);
    BOOST_CHECK_EQUAL(c3.GetHeight(), 0);
    BOOST_CHECK_EQUAL(c3.GetTxOut().nValue, Amount(0));
    BOOST_CHECK_EQUAL(c3.GetTxOut().scriptPubKey.size(), 0);

    // scriptPubKey that ends beyond the end of the stream
    CDataStream ss4(ParseHex("000007"), SER_DISK, CLIENT_VERSION);
    try {
        Coin c4;
        ss4 >> c4;
        BOOST_CHECK_MESSAGE(false, "We should have thrown");
    } catch (const std::ios_base::failure &e) {
    }

    // Very large scriptPubKey (3*10^9 bytes) past the end of the stream
    CDataStream tmp(SER_DISK, CLIENT_VERSION);
    uint64_t x = 3000000000ULL;
    tmp << VARINT(x);
    BOOST_CHECK_EQUAL(HexStr(tmp.begin(), tmp.end()), "8a95c0bb00");
    CDataStream ss5(ParseHex("00008a95c0bb00"), SER_DISK, CLIENT_VERSION);
    try {
        Coin c5;
        ss5 >> c5;
        BOOST_CHECK_MESSAGE(false, "We should have thrown");
    } catch (const std::ios_base::failure &e) {
    }
}

static const COutPoint OUTPOINT;
static const Amount PRUNED(-1);
static const Amount ABSENT(-2);
static const Amount FAIL(-3);
static const Amount VALUE1(100);
static const Amount VALUE2(200);
static const Amount VALUE3(300);
static const char DIRTY = CCoinsCacheEntry::DIRTY;
static const char FRESH = CCoinsCacheEntry::FRESH;
static const char NO_ENTRY = -1;

static const auto FLAGS = {char(0), FRESH, DIRTY, char(DIRTY | FRESH)};
static const auto CLEAN_FLAGS = {char(0), FRESH};
static const auto ABSENT_FLAGS = {NO_ENTRY};

static void SetCoinValue(const Amount value, CCoinsCacheEntry &coin, char flags) {
    assert(value != ABSENT);
    coin = {Coin{}, static_cast<uint8_t>(flags)};
    assert(coin.GetCoin().IsSpent());
    if (value != PRUNED) {
        CTxOut out;
        out.nValue = value;
        coin =
            CCoinsCacheEntry(
                Coin{std::move(out), 1, false},
                static_cast<uint8_t>(flags));
        assert(!coin.GetCoin().IsSpent());
    }
}

size_t InsertCoinMapEntry(CCoinsMap &map, const Amount value, char flags) {
    if (value == ABSENT) {
        assert(flags == NO_ENTRY);
        return 0;
    }
    assert(flags != NO_ENTRY);
    CCoinsCacheEntry entry;
    SetCoinValue(value, entry, flags);
    auto inserted = map.emplace(OUTPOINT, std::move(entry));
    assert(inserted.second);
    return inserted.first->second.DynamicMemoryUsage();
}


void GetCoinMapEntry(const CCoinsMap &map, Amount &value, char &flags) {
    auto it = map.find(OUTPOINT);
    if (it == map.end()) {
        value = ABSENT;
        flags = NO_ENTRY;
    } else {
        value = it->second.GetCoin().GetTxOut().nValue;

        if (it->second.GetCoin().IsSpent()) {
            assert(value == PRUNED);
        }

        flags = it->second.flags;
        assert(flags != NO_ENTRY);
    }
}

void WriteCoinViewEntry(TestCoinsSpanCache& span, const Amount value, char flags) {
    CCoinsMap map;
    InsertCoinMapEntry(map, value, flags);
    span.BatchWrite(map, uint256S("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"));
}

class SingleEntryCacheTest {
public:
    SingleEntryCacheTest(const Amount base_value, const Amount cache_value,
                         char cache_flags){
        {
            TestCoinsSpanCache span{base};
            WriteCoinViewEntry(span, base_value,
                               base_value == ABSENT ? NO_ENTRY : DIRTY);
            span.TryFlush();
        }
        cache = std::make_unique<CCoinsViewCacheTest>(base);
        cache->GetCachedCoinsUsage() +=
            InsertCoinMapEntry(cache->GetRawCacheCoins(), cache_value, cache_flags);
    }

private:
    CoinsDB base{ 0, false, false };

public:
    std::unique_ptr<CCoinsViewCacheTest> cache;
};

void CheckAccessCoin(const Amount base_value, const Amount cache_value,
                     const Amount expected_value, char cache_flags,
                     char expected_flags) {
    SingleEntryCacheTest test(base_value, cache_value, cache_flags);
    test.cache->AccessCoin(OUTPOINT);
    test.cache->SelfTest();

    Amount result_value;
    char result_flags;
    GetCoinMapEntry(test.cache->GetRawCacheCoins(), result_value, result_flags);
    BOOST_CHECK_EQUAL(result_value, expected_value);
    BOOST_CHECK_EQUAL(result_flags, expected_flags);
}

BOOST_AUTO_TEST_CASE(coin_access) {
    /* Check AccessCoin behavior, requesting a coin from a cache view layered on
     * top of a base view, and checking the resulting entry in the cache after
     * the access.
     *
     *               Base    Cache   Result  Cache        Result
     *               Value   Value   Value   Flags        Flags
     */
    CheckAccessCoin(ABSENT, ABSENT, ABSENT, NO_ENTRY, NO_ENTRY);
    CheckAccessCoin(ABSENT, PRUNED, PRUNED, 0, 0);
    CheckAccessCoin(ABSENT, PRUNED, PRUNED, FRESH, FRESH);
    CheckAccessCoin(ABSENT, PRUNED, PRUNED, DIRTY, DIRTY);
    CheckAccessCoin(ABSENT, PRUNED, PRUNED, DIRTY | FRESH, DIRTY | FRESH);
    CheckAccessCoin(ABSENT, VALUE2, VALUE2, 0, 0);
    CheckAccessCoin(ABSENT, VALUE2, VALUE2, FRESH, FRESH);
    CheckAccessCoin(ABSENT, VALUE2, VALUE2, DIRTY, DIRTY);
    CheckAccessCoin(ABSENT, VALUE2, VALUE2, DIRTY | FRESH, DIRTY | FRESH);
    CheckAccessCoin(PRUNED, ABSENT, ABSENT, NO_ENTRY, NO_ENTRY);
    CheckAccessCoin(PRUNED, PRUNED, PRUNED, 0, 0);
    CheckAccessCoin(PRUNED, PRUNED, PRUNED, FRESH, FRESH);
    CheckAccessCoin(PRUNED, PRUNED, PRUNED, DIRTY, DIRTY);
    CheckAccessCoin(PRUNED, PRUNED, PRUNED, DIRTY | FRESH, DIRTY | FRESH);
    CheckAccessCoin(PRUNED, VALUE2, VALUE2, 0, 0);
    CheckAccessCoin(PRUNED, VALUE2, VALUE2, FRESH, FRESH);
    CheckAccessCoin(PRUNED, VALUE2, VALUE2, DIRTY, DIRTY);
    CheckAccessCoin(PRUNED, VALUE2, VALUE2, DIRTY | FRESH, DIRTY | FRESH);
    CheckAccessCoin(VALUE1, ABSENT, VALUE1, NO_ENTRY, 0);
    CheckAccessCoin(VALUE1, PRUNED, PRUNED, 0, 0);
    CheckAccessCoin(VALUE1, PRUNED, PRUNED, FRESH, FRESH);
    CheckAccessCoin(VALUE1, PRUNED, PRUNED, DIRTY, DIRTY);
    CheckAccessCoin(VALUE1, PRUNED, PRUNED, DIRTY | FRESH, DIRTY | FRESH);
    CheckAccessCoin(VALUE1, VALUE2, VALUE2, 0, 0);
    CheckAccessCoin(VALUE1, VALUE2, VALUE2, FRESH, FRESH);
    CheckAccessCoin(VALUE1, VALUE2, VALUE2, DIRTY, DIRTY);
    CheckAccessCoin(VALUE1, VALUE2, VALUE2, DIRTY | FRESH, DIRTY | FRESH);
}

void CheckSpendCoin(Amount base_value, Amount cache_value,
                    Amount expected_value, char cache_flags,
                    char expected_flags) {
    SingleEntryCacheTest test(base_value, cache_value, cache_flags);
    test.cache->SpendCoin(OUTPOINT);
    test.cache->SelfTest();

    Amount result_value;
    char result_flags;
    GetCoinMapEntry(test.cache->GetRawCacheCoins(), result_value, result_flags);
    BOOST_CHECK_EQUAL(result_value, expected_value);
    BOOST_CHECK_EQUAL(result_flags, expected_flags);
};

BOOST_AUTO_TEST_CASE(coin_spend) {
    /**
     * Check SpendCoin behavior, requesting a coin from a cache view layered on
     * top of a base view, spending, and then checking the resulting entry in
     * the cache after the modification.
     *
     *              Base    Cache   Result  Cache        Result
     *              Value   Value   Value   Flags        Flags
     */
    CheckSpendCoin(ABSENT, ABSENT, ABSENT, NO_ENTRY, NO_ENTRY);
    CheckSpendCoin(ABSENT, PRUNED, PRUNED, 0, DIRTY);
    CheckSpendCoin(ABSENT, PRUNED, ABSENT, FRESH, NO_ENTRY);
    CheckSpendCoin(ABSENT, PRUNED, PRUNED, DIRTY, DIRTY);
    CheckSpendCoin(ABSENT, PRUNED, ABSENT, DIRTY | FRESH, NO_ENTRY);
    CheckSpendCoin(ABSENT, VALUE2, PRUNED, 0, DIRTY);
    CheckSpendCoin(ABSENT, VALUE2, ABSENT, FRESH, NO_ENTRY);
    CheckSpendCoin(ABSENT, VALUE2, PRUNED, DIRTY, DIRTY);
    CheckSpendCoin(ABSENT, VALUE2, ABSENT, DIRTY | FRESH, NO_ENTRY);
    CheckSpendCoin(PRUNED, ABSENT, ABSENT, NO_ENTRY, NO_ENTRY);
    CheckSpendCoin(PRUNED, PRUNED, PRUNED, 0, DIRTY);
    CheckSpendCoin(PRUNED, PRUNED, ABSENT, FRESH, NO_ENTRY);
    CheckSpendCoin(PRUNED, PRUNED, PRUNED, DIRTY, DIRTY);
    CheckSpendCoin(PRUNED, PRUNED, ABSENT, DIRTY | FRESH, NO_ENTRY);
    CheckSpendCoin(PRUNED, VALUE2, PRUNED, 0, DIRTY);
    CheckSpendCoin(PRUNED, VALUE2, ABSENT, FRESH, NO_ENTRY);
    CheckSpendCoin(PRUNED, VALUE2, PRUNED, DIRTY, DIRTY);
    CheckSpendCoin(PRUNED, VALUE2, ABSENT, DIRTY | FRESH, NO_ENTRY);
    CheckSpendCoin(VALUE1, ABSENT, PRUNED, NO_ENTRY, DIRTY);
    CheckSpendCoin(VALUE1, PRUNED, PRUNED, 0, DIRTY);
    CheckSpendCoin(VALUE1, PRUNED, ABSENT, FRESH, NO_ENTRY);
    CheckSpendCoin(VALUE1, PRUNED, PRUNED, DIRTY, DIRTY);
    CheckSpendCoin(VALUE1, PRUNED, ABSENT, DIRTY | FRESH, NO_ENTRY);
    CheckSpendCoin(VALUE1, VALUE2, PRUNED, 0, DIRTY);
    CheckSpendCoin(VALUE1, VALUE2, ABSENT, FRESH, NO_ENTRY);
    CheckSpendCoin(VALUE1, VALUE2, PRUNED, DIRTY, DIRTY);
    CheckSpendCoin(VALUE1, VALUE2, ABSENT, DIRTY | FRESH, NO_ENTRY);
}

void CheckAddCoinBase(Amount base_value, Amount cache_value,
                      Amount modify_value, Amount expected_value,
                      char cache_flags, char expected_flags, bool coinbase) {
    SingleEntryCacheTest test(base_value, cache_value, cache_flags);

    Amount result_value;
    char result_flags;
    try {
        CTxOut output;
        output.nValue = modify_value;
        test.cache->AddCoin(OUTPOINT, Coin(std::move(output), 1, coinbase),
                           coinbase, GlobalConfig::GetConfig().GetGenesisActivationHeight());
        test.cache->SelfTest();
        GetCoinMapEntry(test.cache->GetRawCacheCoins(), result_value, result_flags);
    } catch (std::logic_error &e) {
        result_value = FAIL;
        result_flags = NO_ENTRY;
    }

    BOOST_CHECK_EQUAL(result_value, expected_value);
    BOOST_CHECK_EQUAL(result_flags, expected_flags);
}

// Simple wrapper for CheckAddCoinBase function above that loops through
// different possible base_values, making sure each one gives the same results.
// This wrapper lets the coin_add test below be shorter and less repetitive,
// while still verifying that the CoinsViewCache::AddCoin implementation ignores
// base values.
template <typename... Args> void CheckAddCoin(Args &&... args) {
    for (Amount base_value : {ABSENT, PRUNED, VALUE1}) {
        CheckAddCoinBase(base_value, std::forward<Args>(args)...);
    }
}

BOOST_AUTO_TEST_CASE(coin_add) {
    /**
     * Check AddCoin behavior, requesting a new coin from a cache view, writing
     * a modification to the coin, and then checking the resulting entry in the
     * cache after the modification. Verify behavior with the with the AddCoin
     * potential_overwrite argument set to false, and to true.
     *
     * Cache   Write   Result  Cache        Result       potential_overwrite
     * Value   Value   Value   Flags        Flags
     */
    CheckAddCoin(ABSENT, VALUE3, VALUE3, NO_ENTRY, DIRTY | FRESH, false);
    CheckAddCoin(ABSENT, VALUE3, VALUE3, NO_ENTRY, DIRTY, true);
    CheckAddCoin(PRUNED, VALUE3, VALUE3, 0, DIRTY | FRESH, false);
    CheckAddCoin(PRUNED, VALUE3, VALUE3, 0, DIRTY, true);
    CheckAddCoin(PRUNED, VALUE3, VALUE3, FRESH, DIRTY | FRESH, false);
    CheckAddCoin(PRUNED, VALUE3, VALUE3, FRESH, DIRTY | FRESH, true);
    CheckAddCoin(PRUNED, VALUE3, VALUE3, DIRTY, DIRTY, false);
    CheckAddCoin(PRUNED, VALUE3, VALUE3, DIRTY, DIRTY, true);
    CheckAddCoin(PRUNED, VALUE3, VALUE3, DIRTY | FRESH, DIRTY | FRESH, false);
    CheckAddCoin(PRUNED, VALUE3, VALUE3, DIRTY | FRESH, DIRTY | FRESH, true);
    CheckAddCoin(VALUE2, VALUE3, FAIL, 0, NO_ENTRY, false);
    CheckAddCoin(VALUE2, VALUE3, VALUE3, 0, DIRTY, true);
    CheckAddCoin(VALUE2, VALUE3, FAIL, FRESH, NO_ENTRY, false);
    CheckAddCoin(VALUE2, VALUE3, VALUE3, FRESH, DIRTY | FRESH, true);
    CheckAddCoin(VALUE2, VALUE3, FAIL, DIRTY, NO_ENTRY, false);
    CheckAddCoin(VALUE2, VALUE3, VALUE3, DIRTY, DIRTY, true);
    CheckAddCoin(VALUE2, VALUE3, FAIL, DIRTY | FRESH, NO_ENTRY, false);
    CheckAddCoin(VALUE2, VALUE3, VALUE3, DIRTY | FRESH, DIRTY | FRESH, true);
}

void CheckWriteCoin(Amount parent_value, Amount child_value,
                    Amount expected_value, char parent_flags, char child_flags,
                    char expected_flags) {
    SingleEntryCacheTest test(ABSENT, parent_value, parent_flags);

    Amount result_value;
    char result_flags;
    try {
        WriteCoinViewEntry(*test.cache, child_value, child_flags);
        test.cache->SelfTest();
        GetCoinMapEntry(test.cache->GetRawCacheCoins(), result_value, result_flags);
    } catch (std::logic_error &e) {
        result_value = FAIL;
        result_flags = NO_ENTRY;
    }

    BOOST_CHECK_EQUAL(result_value, expected_value);
    BOOST_CHECK_EQUAL(result_flags, expected_flags);
}

BOOST_AUTO_TEST_CASE(coin_write) {
    /* Check BatchWrite behavior, flushing one entry from a child cache to a
     * parent cache, and checking the resulting entry in the parent cache
     * after the write.
     *
     *              Parent  Child   Result  Parent       Child        Result
     *              Value   Value   Value   Flags        Flags        Flags
     */
    CheckWriteCoin(ABSENT, ABSENT, ABSENT, NO_ENTRY, NO_ENTRY, NO_ENTRY);
    CheckWriteCoin(ABSENT, PRUNED, PRUNED, NO_ENTRY, DIRTY, DIRTY);
    CheckWriteCoin(ABSENT, PRUNED, ABSENT, NO_ENTRY, DIRTY | FRESH, NO_ENTRY);
    CheckWriteCoin(ABSENT, VALUE2, VALUE2, NO_ENTRY, DIRTY, DIRTY);
    CheckWriteCoin(ABSENT, VALUE2, VALUE2, NO_ENTRY, DIRTY | FRESH,
                   DIRTY | FRESH);
    CheckWriteCoin(PRUNED, ABSENT, PRUNED, 0, NO_ENTRY, 0);
    CheckWriteCoin(PRUNED, ABSENT, PRUNED, FRESH, NO_ENTRY, FRESH);
    CheckWriteCoin(PRUNED, ABSENT, PRUNED, DIRTY, NO_ENTRY, DIRTY);
    CheckWriteCoin(PRUNED, ABSENT, PRUNED, DIRTY | FRESH, NO_ENTRY,
                   DIRTY | FRESH);
    CheckWriteCoin(PRUNED, PRUNED, PRUNED, 0, DIRTY, DIRTY);
    CheckWriteCoin(PRUNED, PRUNED, PRUNED, 0, DIRTY | FRESH, DIRTY);
    CheckWriteCoin(PRUNED, PRUNED, ABSENT, FRESH, DIRTY, NO_ENTRY);
    CheckWriteCoin(PRUNED, PRUNED, ABSENT, FRESH, DIRTY | FRESH, NO_ENTRY);
    CheckWriteCoin(PRUNED, PRUNED, PRUNED, DIRTY, DIRTY, DIRTY);
    CheckWriteCoin(PRUNED, PRUNED, PRUNED, DIRTY, DIRTY | FRESH, DIRTY);
    CheckWriteCoin(PRUNED, PRUNED, ABSENT, DIRTY | FRESH, DIRTY, NO_ENTRY);
    CheckWriteCoin(PRUNED, PRUNED, ABSENT, DIRTY | FRESH, DIRTY | FRESH,
                   NO_ENTRY);
    CheckWriteCoin(PRUNED, VALUE2, VALUE2, 0, DIRTY, DIRTY);
    CheckWriteCoin(PRUNED, VALUE2, VALUE2, 0, DIRTY | FRESH, DIRTY);
    CheckWriteCoin(PRUNED, VALUE2, VALUE2, FRESH, DIRTY, DIRTY | FRESH);
    CheckWriteCoin(PRUNED, VALUE2, VALUE2, FRESH, DIRTY | FRESH, DIRTY | FRESH);
    CheckWriteCoin(PRUNED, VALUE2, VALUE2, DIRTY, DIRTY, DIRTY);
    CheckWriteCoin(PRUNED, VALUE2, VALUE2, DIRTY, DIRTY | FRESH, DIRTY);
    CheckWriteCoin(PRUNED, VALUE2, VALUE2, DIRTY | FRESH, DIRTY, DIRTY | FRESH);
    CheckWriteCoin(PRUNED, VALUE2, VALUE2, DIRTY | FRESH, DIRTY | FRESH,
                   DIRTY | FRESH);
    CheckWriteCoin(VALUE1, ABSENT, VALUE1, 0, NO_ENTRY, 0);
    CheckWriteCoin(VALUE1, ABSENT, VALUE1, FRESH, NO_ENTRY, FRESH);
    CheckWriteCoin(VALUE1, ABSENT, VALUE1, DIRTY, NO_ENTRY, DIRTY);
    CheckWriteCoin(VALUE1, ABSENT, VALUE1, DIRTY | FRESH, NO_ENTRY,
                   DIRTY | FRESH);
    CheckWriteCoin(VALUE1, PRUNED, PRUNED, 0, DIRTY, DIRTY);
    CheckWriteCoin(VALUE1, PRUNED, FAIL, 0, DIRTY | FRESH, NO_ENTRY);
    CheckWriteCoin(VALUE1, PRUNED, ABSENT, FRESH, DIRTY, NO_ENTRY);
    CheckWriteCoin(VALUE1, PRUNED, FAIL, FRESH, DIRTY | FRESH, NO_ENTRY);
    CheckWriteCoin(VALUE1, PRUNED, PRUNED, DIRTY, DIRTY, DIRTY);
    CheckWriteCoin(VALUE1, PRUNED, FAIL, DIRTY, DIRTY | FRESH, NO_ENTRY);
    CheckWriteCoin(VALUE1, PRUNED, ABSENT, DIRTY | FRESH, DIRTY, NO_ENTRY);
    CheckWriteCoin(VALUE1, PRUNED, FAIL, DIRTY | FRESH, DIRTY | FRESH,
                   NO_ENTRY);
    CheckWriteCoin(VALUE1, VALUE2, VALUE2, 0, DIRTY, DIRTY);
    CheckWriteCoin(VALUE1, VALUE2, FAIL, 0, DIRTY | FRESH, NO_ENTRY);
    CheckWriteCoin(VALUE1, VALUE2, VALUE2, FRESH, DIRTY, DIRTY | FRESH);
    CheckWriteCoin(VALUE1, VALUE2, FAIL, FRESH, DIRTY | FRESH, NO_ENTRY);
    CheckWriteCoin(VALUE1, VALUE2, VALUE2, DIRTY, DIRTY, DIRTY);
    CheckWriteCoin(VALUE1, VALUE2, FAIL, DIRTY, DIRTY | FRESH, NO_ENTRY);
    CheckWriteCoin(VALUE1, VALUE2, VALUE2, DIRTY | FRESH, DIRTY, DIRTY | FRESH);
    CheckWriteCoin(VALUE1, VALUE2, FAIL, DIRTY | FRESH, DIRTY | FRESH,
                   NO_ENTRY);

    // The checks above omit cases where the child flags are not DIRTY, since
    // they would be too repetitive (the parent cache is never updated in these
    // cases). The loop below covers these cases and makes sure the parent cache
    // is always left unchanged.
    for (Amount parent_value : {ABSENT, PRUNED, VALUE1}) {
        for (Amount child_value : {ABSENT, PRUNED, VALUE2}) {
            for (char parent_flags :
                 parent_value == ABSENT ? ABSENT_FLAGS : FLAGS) {
                for (char child_flags :
                     child_value == ABSENT ? ABSENT_FLAGS : CLEAN_FLAGS) {
                    CheckWriteCoin(parent_value, child_value, parent_value,
                                   parent_flags, child_flags, parent_flags);
                }
            }
        }
    }
}

BOOST_FIXTURE_TEST_CASE(coins_provider_locks, TestingSetup)
{
    using namespace std::literals::chrono_literals;

    auto test_try_flush =
        [](std::atomic<int>& step)
        {
            // initialize first
            CoinsDBSpan span{*pcoinsTip};
            step = 1;

            // wait for others to finish initialization
            while(step.load() == 1);

            // one thread should succeed, the rest should fail
            return (span.TryFlush() == CoinsDBSpan::WriteState::ok);
        };
    auto test_read_lock =
        [](std::atomic<int>& step)
        {
            // initialize first
            CoinsDBView view{ *pcoinsTip };
            CCoinsViewCache provider{ view };
            step = 1;

            // wait for others to finish initialization
            while(step.load() == 1);
        };

    // TryFlush can be performed only if there are no other view locks
    {
        std::atomic<int> one_step{0};
        auto one = std::async(std::launch::async, test_read_lock, std::reference_wrapper{one_step});
        std::atomic<int> two_step{0};
        auto two = std::async(std::launch::async, test_try_flush, std::reference_wrapper{two_step});

        // wait for all to initialize
        BOOST_TEST(wait_for([&]{ return one_step.load() == 1 && two_step.load() == 1; }, 200ms));

        two_step = 2;

        // make sure that TryFlush keeps waiting for the read lock to be dropped
        BOOST_TEST((two.wait_for(500ms) == std::future_status::timeout));

        one_step = 2;

        BOOST_TEST(two.get());
        one.wait();
    }

    // TryFlush from second location fails
    {
        std::atomic<int> one_step{0};
        auto one = std::async(std::launch::async, test_try_flush, std::reference_wrapper{one_step});
        std::atomic<int> two_step{0};
        auto two = std::async(std::launch::async, test_try_flush, std::reference_wrapper{two_step});

        // wait for all to initialize
        BOOST_TEST(wait_for([&]{ return one_step.load() == 1 && two_step.load() == 1; }, 200ms));

        one_step = 2;
        two_step = 2;

        BOOST_TEST(one.get() != two.get());
    }

    auto test_flush =
        [](std::atomic<int>& step)
        {
            bool result = pcoinsTip->Flush();

            step = 1;

            return result;
        };

    // MT span creation waits until other view locks are present
    {
        std::atomic<int> one_step{0};
        auto one = std::async(std::launch::async, test_read_lock, std::reference_wrapper{one_step});

        // wait for all to initialize
        BOOST_TEST(wait_for([&]{ return one_step.load() == 1; }, 200ms));

        std::atomic<int> two_step{0};
        auto two = std::async(std::launch::async, test_flush, std::reference_wrapper{two_step});

        // make sure that Flush keeps waiting for the read lock to be dropped
        BOOST_TEST((one.wait_for(500ms) == std::future_status::timeout));

        // MT span still hasn't been flushed
        BOOST_TEST(two_step == 0);

        one_step = 2;
        one.wait();
        BOOST_TEST(two.get());
    }

    // TryFlush fails if MT span creation is pending
    {
        std::atomic<int> one_step{0};
        auto one = std::async(std::launch::async, test_try_flush, std::reference_wrapper{one_step});

        // wait for all to initialize
        BOOST_TEST(wait_for([&]{ return one_step.load() == 1; }, 200ms));

        std::atomic<int> two_step{0};
        auto two = std::async(std::launch::async, test_flush, std::reference_wrapper{two_step});

        // make sure that Flush keeps waiting for the read lock held inside
        // test_try_flush to be dropped
        BOOST_TEST((two.wait_for(500ms) == std::future_status::timeout));

        ++one_step;

        // make sure that TryFlush immediately fails since an exclusive write
        // lock is pending
        BOOST_TEST((one.wait_for(500ms) == std::future_status::ready));
        BOOST_TEST((two.wait_for(500ms) == std::future_status::ready));

        BOOST_TEST(one.get() == false);
        BOOST_TEST(two.get());
    }
}

BOOST_AUTO_TEST_SUITE_END()
