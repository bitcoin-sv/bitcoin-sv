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
#include <optional>
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

/**
 * Class for testing provider internals that are otherwise not exposed through
 * public API of views/spans
 */
template <>
struct CoinsDB::UnitTestAccess<coins_tests_uid> : public CoinsDB
{
public:
    UnitTestAccess( std::size_t cacheSize )
        : CoinsDB{ cacheSize, 0, CoinsDB::MaxFiles::Default(), false, false }
    {}

    const std::optional<CoinImpl>& GetLatestCoin() const { return mLatestGetCoin; }
    uint64_t GetLatestRequestedScriptSize() const { return mLatestRequestedScriptSize; }

    void SizeOverride(std::optional<uint64_t> override) { mOverrideSize = override; }

    CCoinsMap& GetRawCacheCoins()
    {
        return TestAccessCoinsCache::GetRawCacheCoins(mCache);
    }

protected:
    std::optional<CoinImpl> GetCoin(const COutPoint &outpoint, uint64_t maxScriptSize) const
    {
        mLatestRequestedScriptSize = (mOverrideSize.has_value() ? mOverrideSize.value() : maxScriptSize);
        mLatestGetCoin = CoinsDB::GetCoin(outpoint, mLatestRequestedScriptSize);

        return mLatestGetCoin->MakeNonOwning();
    }

    void ReadLock(WPUSMutex::Lock& lockHandle)
    {
        return CoinsDB::ReadLock(lockHandle);
    }

private:
    mutable uint64_t mLatestRequestedScriptSize;
    mutable std::optional<CoinImpl> mLatestGetCoin;
    std::optional<uint64_t> mOverrideSize;

    friend class CTestCoinsView;
};
using CCoinsProviderTest = CoinsDB::UnitTestAccess<coins_tests_uid>;

class CTestCoinsView
{
public:
    CTestCoinsView(CCoinsProviderTest& providerIn)
        : provider{providerIn}
    {
        provider.ReadLock( mLock );
    }

    std::optional<Coin> GetCoin(const COutPoint& outpoint) const
    {
        auto coinData = provider.GetCoin(outpoint, 0);
        if(coinData.has_value())
        {
            return Coin{coinData.value()};
        }

        return {};
    }
    std::optional<CoinWithScript> GetCoinWithScript(const COutPoint& outpoint) const
    {
        auto coinData = provider.GetCoin(outpoint, std::numeric_limits<size_t>::max());
        if(coinData.has_value())
        {
            assert(coinData->HasScript());

            return std::move(coinData.value());
        }

        return {};
    }

private:
    CCoinsProviderTest& provider;
    WPUSMutex::Lock mLock;
};

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

CoinWithScript DataStreamToCoinWithScript(CDataStream& stream)
{
    CoinImpl coin;
    stream >> coin;

    return coin;
}

} // namespace

BOOST_FIXTURE_TEST_SUITE(coins_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(coin_serialization) {
    // Good example
    CDataStream ss1(
        ParseHex("97f23c835800816115944e077fe7c803cfa57f29b36bf87c1d35"),
        SER_DISK, CLIENT_VERSION);
    CoinWithScript c1 = DataStreamToCoinWithScript(ss1);
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
    CoinWithScript c2 = DataStreamToCoinWithScript(ss2);
    BOOST_CHECK_EQUAL(c2.IsCoinBase(), true);
    BOOST_CHECK_EQUAL(c2.GetHeight(), 120891);
    BOOST_CHECK_EQUAL(c2.GetTxOut().nValue, Amount(110397LL));
    BOOST_CHECK_EQUAL(HexStr(c2.GetTxOut().scriptPubKey),
                      HexStr(GetScriptForDestination(CKeyID(uint160(ParseHex(
                          "8c988f1a4a4de2161e0f50aac7f17e7f9555caa4"))))));

    // Smallest possible example
    CDataStream ss3(ParseHex("000006"), SER_DISK, CLIENT_VERSION);
    CoinWithScript c3 = DataStreamToCoinWithScript(ss3);
    BOOST_CHECK_EQUAL(c3.IsCoinBase(), false);
    BOOST_CHECK_EQUAL(c3.GetHeight(), 0);
    BOOST_CHECK_EQUAL(c3.GetTxOut().nValue, Amount(0));
    BOOST_CHECK_EQUAL(c3.GetTxOut().scriptPubKey.size(), 0);

    // scriptPubKey that ends beyond the end of the stream
    CDataStream ss4(ParseHex("000007"), SER_DISK, CLIENT_VERSION);
    try {
        CoinWithScript c4 = DataStreamToCoinWithScript(ss4);
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
        CoinWithScript c5 = DataStreamToCoinWithScript(ss5);
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
    coin = {CoinImpl{}, static_cast<uint8_t>(flags)};
    assert(coin.GetCoin().IsSpent());
    if (value != PRUNED) {
        CTxOut out;
        out.nValue = value;
        coin =
            CCoinsCacheEntry(
                CoinImpl::FromCoinWithScript(
                    CoinWithScript::MakeOwning(std::move(out), 1, false) ),
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
        value = it->second.GetCoin().GetAmount();

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
    CoinsDB base{ std::numeric_limits<size_t>::max(), 0, CoinsDB::MaxFiles::Default(), false, false };

public:
    std::unique_ptr<CCoinsViewCacheTest> cache;
};

void CheckAccessCoin(const Amount base_value, const Amount cache_value,
                     const Amount expected_value, char cache_flags,
                     char expected_flags) {
    SingleEntryCacheTest test(base_value, cache_value, cache_flags);
    test.cache->GetCoin(OUTPOINT);
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
        test.cache->GetCoin( OUTPOINT ); // make sure that coin is preloaded if it already exists
        test.cache->AddCoin(OUTPOINT, CoinWithScript::MakeOwning(std::move(output), 1, coinbase),
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
    {
        // Adding coins behaves differently in this case depending on whether
        // coin was already present beforehand or not.
        // New coin is added if coin is absent beforehand, otherwise adding it
        // is treated as an error.
        CheckAddCoinBase(ABSENT, ABSENT, VALUE3, VALUE3, NO_ENTRY, DIRTY | FRESH, false);
        CheckAddCoinBase(PRUNED, ABSENT, VALUE3, VALUE3, NO_ENTRY, DIRTY | FRESH, false);
        CheckAddCoinBase(VALUE1, ABSENT, VALUE3, FAIL, NO_ENTRY, NO_ENTRY, false);
    }
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

BOOST_FIXTURE_TEST_CASE(coin_get_lazy, TestingSetup) {
    // First delete pcoinsTip as we don't want to cause a dead lock in this
    // test since we'll be instantiating a pcoinsTip alternative
    pcoinsTip.reset();

    /* Check method CCoinsViewDB::GetCoin_NoLargeScript
     * Method should unserialize contents of the script only if size of script is not larger than specified.
     * Otherwise member coin.out.scriptPubKey should be default initialized.
     * In addition, method must always provide actual size of script, whether it was unserialized or not.
     */

    // Id of an unspent transaction
    auto txId = uint256S("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");

    // Hash and height of a block that contains unspent transaction (txId)
    auto blockHash = uint256S("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    std::uint32_t blockHeight = 1;

    // Size (in bytes) of small locking script
    std::size_t script_small_size = 0;

    // Size (in bytes) of big locking script
    std::size_t script_big_size = 0;

    //
    // Add sample UTXOs to database
    //
    CCoinsProviderTest provider{ script_small_size }; // small cache

    {
        CoinsDBSpan span{provider};
        span.SetBestBlock(blockHash);

        {
            // Output 0 - small locking script
            CScript scr(OP_RETURN);
            CTxOut txo(Amount(123), scr);
            script_small_size = txo.scriptPubKey.size(); // remember size of small script for later
            span.AddCoin(COutPoint(txId, 0), CoinWithScript::MakeOwning(std::move(txo), blockHeight, false), false, 0); // UTXO is not coinbase
        }

        {
            // Output 1 - big locking script
            CTxOut txo;
            txo.nValue = Amount(456);
            txo.scriptPubKey = CScript(std::vector<uint8_t>(1024*1024, 0xde));
            script_big_size = txo.scriptPubKey.size(); // remember size of big script for later
            span.AddCoin(COutPoint(txId, 1), CoinWithScript::MakeOwning(std::move(txo), blockHeight, false), false, 0); // UTXO is not coinbase
        }

        // And flush them to provider
        BOOST_TEST((span.TryFlush() == CoinsDBSpan::WriteState::ok));
    }

    // Flush sample UTXOs to DB
    BOOST_TEST(provider.Flush());

    //
    // Check that reading UTXOs from DB using GetCoinWithScript() method always also gets script
    //
    {
        // Get output 0 from DB
        auto c0 = CTestCoinsView{ provider }.GetCoinWithScript(COutPoint(txId, 0));
        BOOST_TEST(c0.has_value());
        BOOST_TEST(c0->GetTxOut().nValue == Amount(123)); // check that we got the correct output
        BOOST_TEST(c0->GetTxOut().scriptPubKey.size()==script_small_size);
    }

    {
        // Get output 1 from DB
        auto c1 = CTestCoinsView{ provider }.GetCoinWithScript(COutPoint(txId, 1));
        BOOST_TEST(c1.has_value());
        BOOST_TEST(c1->GetTxOut().nValue == Amount(456)); // check that we got the correct output
        BOOST_TEST(c1->GetTxOut().scriptPubKey.size()==script_big_size);
    }

    //
    // Check that reading UTXOs from DB using GetCoin() only gets script if it is not larger than specified
    //
    {
        // Get output 0 from DB with script regardless of its size
        provider.SizeOverride(script_small_size);
        auto c0 = CTestCoinsView{ provider }.GetCoin(COutPoint(txId, 0));
        BOOST_TEST(c0.has_value());
        BOOST_TEST(c0->GetAmount() == Amount(123)); // check that we got the correct output
        BOOST_TEST(provider.GetLatestRequestedScriptSize()==script_small_size);
        BOOST_TEST(provider.GetLatestCoin()->GetTxOut().scriptPubKey.size()==script_small_size);
        BOOST_TEST(provider.GetLatestCoin()->GetScriptSize()==script_small_size);
    }

    {
        // Get output 0 from DB without script (even if it is a very small one)
        provider.SizeOverride( {} );
        auto c0 = CTestCoinsView{ provider }.GetCoin(COutPoint(txId, 0));
        BOOST_TEST(c0.has_value());
        BOOST_TEST(c0->GetAmount() == Amount(123)); // check that we got the correct output
        BOOST_TEST(provider.GetLatestRequestedScriptSize()==0);
        BOOST_TEST(provider.GetLatestCoin()->GetTxOut().scriptPubKey.size()==0);
        BOOST_TEST(provider.GetLatestCoin()->GetScriptSize()==script_small_size);
    }

    {
        // Get output 1 from DB with script regardless of its size
        provider.SizeOverride(script_big_size);
        auto c1 = CTestCoinsView{ provider }.GetCoin(COutPoint(txId, 1));
        BOOST_TEST(c1.has_value());
        BOOST_TEST(c1->GetAmount() == Amount(456)); // check that we got the correct output
        BOOST_TEST(provider.GetLatestRequestedScriptSize()==script_big_size);
        BOOST_TEST(provider.GetLatestCoin()->GetTxOut().scriptPubKey.size()==script_big_size);
        BOOST_TEST(provider.GetLatestCoin()->GetScriptSize()==script_big_size);
    }

    {
        // Get output 1 from DB without script
        provider.SizeOverride( {} );
        auto c1 = CTestCoinsView{ provider }.GetCoin(COutPoint(txId, 1));
        BOOST_TEST(c1.has_value());
        BOOST_TEST(c1->GetAmount() == Amount(456)); // check that we got the correct output
        BOOST_TEST(provider.GetLatestRequestedScriptSize()==0);
        BOOST_TEST(provider.GetLatestCoin()->GetTxOut().scriptPubKey.size()==0);
        BOOST_TEST(provider.GetLatestCoin()->GetScriptSize()==script_big_size);
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

// Test that coins caching works as expected when script cache is disabled
BOOST_FIXTURE_TEST_CASE(no_coins_caching, TestingSetup)
{
    // First delete pcoinsTip as we don't want to cause a dead lock in this
    // test since we'll be instantiating a pcoinsTip alternative
    pcoinsTip.reset();

    // Id of an unspent transaction
    auto txId = uint256S("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");

    // Hash and height of a block that contains unspent transaction (txId)
    auto block_1_hash = uint256S("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    auto block_2_hash = uint256S("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    std::uint32_t blockHeight = 1;

    CScript script_template =
        []
        {
            // make sure that copies of this script don't end up smaller than
            // expected as constructor from vector allocates more space than
            // minimally needed
            CScript tmp{std::vector<uint8_t>(1024*1024, 0xde)};
            return CScript{tmp};
        }();

    // Dynamic memory usage of the scripts
    std::uint32_t script_memory_usage = memusage::DynamicUsage(script_template);
    BOOST_TEST(script_memory_usage != 0);

    std::uint32_t coins_count = 5;

    //
    // Add sample UTXOs to database
    //
    CCoinsProviderTest primary{ 0 }; // no cache

    // Each platform has its own default allocation policy for standard containers
    // so even though the expected memory usage should be 0 that is not necessarily
    // the case
    auto defaultDynamicMemoryUsage = primary.DynamicMemoryUsage();

    {
        TestCoinsSpanCache secondary{primary};

        // both caches are empty since we haven't added any coins
        BOOST_TEST(primary.DynamicMemoryUsage() == defaultDynamicMemoryUsage);
        BOOST_TEST(secondary.DynamicMemoryUsage() == defaultDynamicMemoryUsage);

        for(std::uint32_t i = 0; i < coins_count; ++i)
        {
            CTxOut txo;
            txo.nValue = Amount(123);
            txo.scriptPubKey = script_template;
            secondary.AddCoin(
                COutPoint{txId, i},
                CoinWithScript::MakeOwning(std::move(txo), blockHeight, false),
                false,
                0); // UTXO is not coinbase

            BOOST_TEST(primary.DynamicMemoryUsage() == defaultDynamicMemoryUsage);
            BOOST_TEST(
                secondary.DynamicMemoryUsage() ==
                (memusage::DynamicUsage(secondary.GetRawCacheCoins()) + (script_memory_usage * (i + 1))));
        }

        // And flush them to primary cache
        secondary.SetBestBlock(block_1_hash);
        BOOST_TEST((secondary.TryFlush() == CoinsDBSpan::WriteState::ok));

        // After flush dynamic memory usage can only be seen in primary cache
        BOOST_TEST(
            primary.DynamicMemoryUsage() ==
            (memusage::DynamicUsage(primary.GetRawCacheCoins()) + (script_memory_usage * coins_count)));
        BOOST_TEST(secondary.DynamicMemoryUsage() == defaultDynamicMemoryUsage);
    }

    // Flush sample UTXOs to DB
    primary.Flush();

    // After flush of primary cache to database the primary cache is empty
    BOOST_TEST(primary.DynamicMemoryUsage() == defaultDynamicMemoryUsage);

    auto first_coin_outpoint = COutPoint{txId, 0};

    //
    // Read UTXOs from database without secondary cache
    //
    {
        auto memory_usage_before_coin_load = primary.DynamicMemoryUsage();

        CoinsDBView view{primary};
        auto coin = view.GetCoin(first_coin_outpoint);

        // Cache contains only coin without script
        BOOST_TEST(coin.has_value());
        BOOST_TEST(primary.DynamicMemoryUsage() == memusage::DynamicUsage(primary.GetRawCacheCoins()));
        BOOST_TEST(memory_usage_before_coin_load < primary.DynamicMemoryUsage());

        auto memory_usage_before_coin_with_script_load = primary.DynamicMemoryUsage();
        auto coin_with_script = view.GetCoinWithScript(first_coin_outpoint);

        // Cache contains only coin without script
        BOOST_TEST(coin_with_script.has_value());
        BOOST_TEST(coin_with_script->IsStorageOwner());
        BOOST_TEST(coin_with_script->IsSpent() == false);
        BOOST_TEST(coin_with_script->GetTxOut().scriptPubKey == script_template);
        BOOST_TEST(primary.DynamicMemoryUsage() == memusage::DynamicUsage(primary.GetRawCacheCoins()));
        BOOST_TEST(memory_usage_before_coin_with_script_load == primary.DynamicMemoryUsage());
    }

    //
    // Read UTXOs from database with secondary cache
    //
    {
        auto provider_memory_usage_before_coin_load = primary.DynamicMemoryUsage();
        TestCoinsSpanCache secondary{primary};
        auto memory_usage_before_coin_load = secondary.DynamicMemoryUsage();
        auto coin = secondary.GetCoin(first_coin_outpoint);

        // Cache contains only coin without script
        BOOST_TEST(coin.has_value());
        BOOST_TEST(secondary.DynamicMemoryUsage() == memusage::DynamicUsage(secondary.GetRawCacheCoins()));
        BOOST_TEST(memory_usage_before_coin_load < secondary.DynamicMemoryUsage());

        auto memory_usage_before_coin_with_script_load = secondary.DynamicMemoryUsage();
        auto coin_with_script = secondary.GetCoinWithScript(first_coin_outpoint);

        // Cache contains only coin without script
        BOOST_TEST(coin_with_script.has_value());
        BOOST_TEST(coin_with_script->IsStorageOwner());
        BOOST_TEST(coin_with_script->IsSpent() == false);
        BOOST_TEST(coin_with_script->GetTxOut().scriptPubKey == script_template);
        BOOST_TEST(secondary.DynamicMemoryUsage() == memusage::DynamicUsage(secondary.GetRawCacheCoins()));
        BOOST_TEST(memory_usage_before_coin_with_script_load == secondary.DynamicMemoryUsage());
        BOOST_TEST(provider_memory_usage_before_coin_load == primary.DynamicMemoryUsage());

        secondary.SpendCoin(first_coin_outpoint);

        // Spending coin doesn't affect cache as the script was not in it
        BOOST_TEST(memory_usage_before_coin_with_script_load == secondary.DynamicMemoryUsage());
        BOOST_TEST(provider_memory_usage_before_coin_load == primary.DynamicMemoryUsage());

        // Reading spent coin from secondary cache returns a spent coin
        coin = secondary.GetCoin(first_coin_outpoint);
        BOOST_TEST(coin.has_value());
        BOOST_TEST(coin->IsSpent() == true);
        auto coin_with_script_2 = secondary.GetCoinWithScript(first_coin_outpoint);
        BOOST_TEST(coin_with_script_2.has_value());
        BOOST_TEST(!coin_with_script_2->IsStorageOwner());
        BOOST_TEST(coin_with_script_2->IsSpent() == true);

        secondary.SetBestBlock(block_2_hash);
        BOOST_TEST((secondary.TryFlush() == CoinsDBSpan::WriteState::ok));

        // Flush spent coin to primary cache doesn't affect cache as the script was not in it
        BOOST_TEST(provider_memory_usage_before_coin_load == primary.DynamicMemoryUsage());
    }

    //
    // Reading spent coin from primary cache shouldn't return any coins
    //
    {
        CoinsDBView view{primary};
        auto coin = view.GetCoin(first_coin_outpoint);
        BOOST_TEST(coin.has_value() == false);
        auto coin_with_script = view.GetCoinWithScript(first_coin_outpoint);
        BOOST_TEST(coin_with_script.has_value() == false);
    }
}



// Test that coins caching works as expected when script cache is disabled
BOOST_FIXTURE_TEST_CASE(coins_caching, TestingSetup)
{
    // First delete pcoinsTip as we don't want to cause a dead lock in this
    // test since we'll be instantiating a pcoinsTip alternative
    pcoinsTip.reset();

    // Id of an unspent transaction
    auto txId = uint256S("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");

    // Hash and height of a block that contains unspent transaction (txId)
    auto block_1_hash = uint256S("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    auto block_2_hash = uint256S("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    std::uint32_t blockHeight = 1;

    CScript script_template =
        []
        {
            // make sure that copies of this script don't end up smaller than
            // expected as constructor from vector allocates more space than
            // minimally needed
            CScript tmp{std::vector<uint8_t>(1024*1024, 0xde)};
            return CScript{tmp};
        }();

    // Dynamic memory usage of the scripts
    std::uint32_t script_memory_usage = memusage::DynamicUsage(script_template);
    BOOST_TEST(script_memory_usage != 0);

    std::uint32_t coins_count = 5;

    //
    // Add sample UTXOs to database
    //

    // cache a bit larger than two scripts since some of the cache is also used
    // up by coins themselves without scripts and we want the third script to
    // no longer fit into cache in this test
    CCoinsProviderTest primary{ static_cast<std::uint32_t>(script_memory_usage * 2.5) };

    // Each platform has its own default allocation policy for standard containers
    // so even though the expected memory usage should be 0 that is not necessarily
    // the case
    auto defaultDynamicMemoryUsage = primary.DynamicMemoryUsage();

    {
        TestCoinsSpanCache secondary{primary};

        // both caches are empty since we haven't added any coins
        BOOST_TEST(primary.DynamicMemoryUsage() == defaultDynamicMemoryUsage);
        BOOST_TEST(secondary.DynamicMemoryUsage() == defaultDynamicMemoryUsage);

        for(std::uint32_t i = 0; i < coins_count; ++i)
        {
            CTxOut txo;
            txo.nValue = Amount(123);
            txo.scriptPubKey = script_template;
            auto size = memusage::DynamicUsage(txo.scriptPubKey);
            secondary.AddCoin(
                COutPoint{txId, i},
                CoinWithScript::MakeOwning(std::move(txo), blockHeight, false),
                false,
                0); // UTXO is not coinbase

            BOOST_TEST(primary.DynamicMemoryUsage() == defaultDynamicMemoryUsage);
            assert(script_memory_usage == size);
            BOOST_TEST(
                secondary.DynamicMemoryUsage() ==
                (memusage::DynamicUsage(secondary.GetRawCacheCoins()) + (script_memory_usage * (i + 1))));
        }

        // And flush them to primary cache
        secondary.SetBestBlock(block_1_hash);
        BOOST_TEST((secondary.TryFlush() == CoinsDBSpan::WriteState::ok));

        // After flush dynamic memory usage can only be seen in primary cache
        BOOST_TEST(
            primary.DynamicMemoryUsage() ==
            (memusage::DynamicUsage(primary.GetRawCacheCoins()) + (script_memory_usage * coins_count)));
        BOOST_TEST(secondary.DynamicMemoryUsage() == defaultDynamicMemoryUsage);
    }

    // Flush sample UTXOs to DB
    primary.Flush();

    // After flush of primary cache to database the primary cache is empty
    BOOST_TEST(primary.DynamicMemoryUsage() == defaultDynamicMemoryUsage);

    auto first_coin_outpoint = COutPoint{txId, 0};

    //
    // Read UTXOs from database without secondary cache
    //
    {
        auto memory_usage_before_coin_load = primary.DynamicMemoryUsage();

        CoinsDBView view{primary};
        auto coin = view.GetCoin(first_coin_outpoint);

        // Cache contains coin with script even though no script was requested
        // as it had enough space to store it
        BOOST_TEST(coin.has_value());
        BOOST_TEST(
            primary.DynamicMemoryUsage() ==
            (memusage::DynamicUsage(primary.GetRawCacheCoins()) + script_memory_usage));
        BOOST_TEST(memory_usage_before_coin_load < primary.DynamicMemoryUsage());

        auto memory_usage_before_coin_with_script_load = primary.DynamicMemoryUsage();
        auto coin_with_script = view.GetCoinWithScript(first_coin_outpoint);

        // Cache contains coin with script
        BOOST_TEST(coin_with_script.has_value());
        BOOST_TEST(!coin_with_script->IsStorageOwner());
        BOOST_TEST(coin_with_script->IsSpent() == false);
        BOOST_TEST(coin_with_script->GetTxOut().scriptPubKey == script_template);
        BOOST_TEST(
            primary.DynamicMemoryUsage() ==
            (memusage::DynamicUsage(primary.GetRawCacheCoins()) + script_memory_usage));
        BOOST_TEST(memory_usage_before_coin_with_script_load == primary.DynamicMemoryUsage());

        // Cache contains two coins with script
        auto coin_with_script_2 = view.GetCoinWithScript(COutPoint{txId, 1});
        BOOST_TEST(coin_with_script_2.has_value());
        BOOST_TEST(!coin_with_script_2->IsStorageOwner());
        BOOST_TEST(coin_with_script_2->IsSpent() == false);
        BOOST_TEST(coin_with_script_2->GetTxOut().scriptPubKey == script_template);
        BOOST_TEST(
            primary.DynamicMemoryUsage() ==
            (memusage::DynamicUsage(primary.GetRawCacheCoins()) + script_memory_usage * 2));

        // Three was no more space for the third script
        auto coin_with_script_3 = view.GetCoinWithScript(COutPoint{txId, 2});
        BOOST_TEST(coin_with_script_3.has_value());
        BOOST_TEST(coin_with_script_3->IsStorageOwner());
        BOOST_TEST(coin_with_script_3->IsSpent() == false);
        BOOST_TEST(coin_with_script_3->GetTxOut().scriptPubKey == script_template);
        BOOST_TEST(
            primary.DynamicMemoryUsage() ==
            (memusage::DynamicUsage(primary.GetRawCacheCoins()) + script_memory_usage * 2));
    }

    //
    // Uncache and read another coin into cache
    //
    {
        auto memory_usage_before_uncache = primary.DynamicMemoryUsage();
        primary.Uncache({COutPoint{txId, 1}});
        BOOST_TEST(memory_usage_before_uncache > primary.DynamicMemoryUsage());

        CoinsDBView view{primary};

        // Cache contains two coins with script
        auto coin_with_script_2 = view.GetCoinWithScript(COutPoint{txId, 3});
        BOOST_TEST(coin_with_script_2.has_value());
        BOOST_TEST(!coin_with_script_2->IsStorageOwner());
        BOOST_TEST(coin_with_script_2->IsSpent() == false);
        BOOST_TEST(coin_with_script_2->GetTxOut().scriptPubKey == script_template);
        BOOST_TEST(
            primary.DynamicMemoryUsage() ==
            (memusage::DynamicUsage(primary.GetRawCacheCoins()) + script_memory_usage * 2));

        // Three was no more space for the third script
        auto coin_with_script_3 = view.GetCoinWithScript(COutPoint{txId, 4});
        BOOST_TEST(coin_with_script_3.has_value());
        BOOST_TEST(coin_with_script_3->IsStorageOwner());
        BOOST_TEST(coin_with_script_3->IsSpent() == false);
        BOOST_TEST(coin_with_script_3->GetTxOut().scriptPubKey == script_template);
        BOOST_TEST(
            primary.DynamicMemoryUsage() ==
            (memusage::DynamicUsage(primary.GetRawCacheCoins()) + script_memory_usage * 2));
    }

    //
    // Read UTXOs from database with secondary cache
    //
    {
        auto provider_memory_usage_before_coin_load = primary.DynamicMemoryUsage();
        TestCoinsSpanCache secondary{primary};
        auto memory_usage_before_coin_load = secondary.DynamicMemoryUsage();
        auto coin = secondary.GetCoin(first_coin_outpoint);

        // Secondary cache contains only coin without script while primary cache
        // now contains coin with script so we expect it to grow
        BOOST_TEST(coin.has_value());
        BOOST_TEST(
            secondary.DynamicMemoryUsage() ==
            memusage::DynamicUsage(secondary.GetRawCacheCoins()));
        BOOST_TEST(memory_usage_before_coin_load < secondary.DynamicMemoryUsage());

        auto memory_usage_before_coin_with_script_load = secondary.DynamicMemoryUsage();
        auto coin_with_script = secondary.GetCoinWithScript(first_coin_outpoint);

        // Cache doesn't change as there was enough space to load the script to
        // cache while asking only for the coin
        BOOST_TEST(coin_with_script.has_value());
        BOOST_TEST(!coin_with_script->IsStorageOwner());
        BOOST_TEST(coin_with_script->IsSpent() == false);
        BOOST_TEST(memory_usage_before_coin_with_script_load == secondary.DynamicMemoryUsage());
        BOOST_TEST(provider_memory_usage_before_coin_load == primary.DynamicMemoryUsage());

        secondary.SpendCoin(first_coin_outpoint);

        // Spending coin doesn't affect secondary cache as the script was not in
        // it as it was in the primary cache. Primary cache doesn't change as
        // the change wasn't flushed to it yet.
        BOOST_TEST(memory_usage_before_coin_with_script_load == secondary.DynamicMemoryUsage());
        BOOST_TEST(provider_memory_usage_before_coin_load == primary.DynamicMemoryUsage());

        // Reading spent coin from secondary cache returns a spent coin
        coin = secondary.GetCoin(first_coin_outpoint);
        BOOST_TEST(coin.has_value());
        BOOST_TEST(coin->IsSpent() == true);
        auto coin_with_script_2 = secondary.GetCoinWithScript(first_coin_outpoint);
        BOOST_TEST(coin_with_script_2.has_value());
        BOOST_TEST(!coin_with_script_2->IsStorageOwner());
        BOOST_TEST(coin_with_script_2->IsSpent() == true);

        secondary.SetBestBlock(block_2_hash);
        BOOST_TEST((secondary.TryFlush() == CoinsDBSpan::WriteState::ok));

        // Flush spent coin to primary cache shrinks the primary cache as script
        // is no longer present in the coin
        BOOST_TEST(provider_memory_usage_before_coin_load > primary.DynamicMemoryUsage());
    }

    //
    // Reading spent coin from primary cache shouldn't return any coins
    //
    {
        CoinsDBView view{primary};
        auto coin = view.GetCoin(first_coin_outpoint);
        BOOST_TEST(coin.has_value() == false);
        auto coin_with_script = view.GetCoinWithScript(first_coin_outpoint);
        BOOST_TEST(coin_with_script.has_value() == false);
    }
}

BOOST_AUTO_TEST_SUITE_END()
