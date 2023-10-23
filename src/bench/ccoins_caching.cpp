// Copyright (c) 2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "bench.h"
#include "chainparams.h"
#include "coins.h"
#include "config.h"
#include "consensus/validation.h"
#include "primitives/transaction.h"
#include "test/test_bitcoin.h"
#include "wallet/crypter.h"
#include "uint256.h"
#include "validation.h"

#include <cstdint>
#include <limits>
#include <mutex>
#include <vector>

// FIXME: Dedup with SetupDummyInputs in test/transaction_tests.cpp.
//
// Helper: create two dummy transactions, each with
// two outputs.  The first has 11 and 50 CENT outputs
// paid to a TX_PUBKEY, the second 21 and 22 CENT outputs
// paid to a TX_PUBKEYHASH.
//
static std::vector<CMutableTransaction>
SetupDummyInputs(CBasicKeyStore &keystoreRet, CCoinsViewCache &coinsRet) {
    std::vector<CMutableTransaction> dummyTransactions;
    dummyTransactions.resize(2);

    // Add some keys to the keystore:
    CKey key[4];
    for (int i = 0; i < 4; i++) {
        key[i].MakeNewKey(i % 2);
        keystoreRet.AddKey(key[i]);
    }

    // Create some dummy input transactions
    dummyTransactions[0].vout.resize(2);
    dummyTransactions[0].vout[0].nValue = 11 * CENT;
    dummyTransactions[0].vout[0].scriptPubKey
        << ToByteVector(key[0].GetPubKey()) << OP_CHECKSIG;
    dummyTransactions[0].vout[1].nValue = 50 * CENT;
    dummyTransactions[0].vout[1].scriptPubKey
        << ToByteVector(key[1].GetPubKey()) << OP_CHECKSIG;
    AddCoins(coinsRet, CTransaction(dummyTransactions[0]), false, 0, 0);

    dummyTransactions[1].vout.resize(2);
    dummyTransactions[1].vout[0].nValue = 21 * CENT;
    dummyTransactions[1].vout[0].scriptPubKey =
        GetScriptForDestination(key[2].GetPubKey().GetID());
    dummyTransactions[1].vout[1].nValue = 22 * CENT;
    dummyTransactions[1].vout[1].scriptPubKey =
        GetScriptForDestination(key[3].GetPubKey().GetID());
    AddCoins(coinsRet, CTransaction(dummyTransactions[1]), false, 0, 0);

    return dummyTransactions;
}

// Microbenchmark for simple accesses to a CCoinsViewCache database. Note from
// laanwj, "replicating the actual usage patterns of the client is hard though,
// many times micro-benchmarks of the database showed completely different
// characteristics than e.g. reindex timings. But that's not a requirement of
// every benchmark."
// (https://github.com/bitcoin/bitcoin/issues/7883#issuecomment-224807484)
static void CCoinsCaching(benchmark::State &state) {
    CBasicKeyStore keystore;
    CCoinsViewEmpty coinsDummy;
    CCoinsViewCache coins(coinsDummy);
    std::vector<CMutableTransaction> dummyTransactions =
        SetupDummyInputs(keystore, coins);

    CMutableTransaction t1;
    t1.vin.resize(3);
    t1.vin[0].prevout = COutPoint(dummyTransactions[0].GetId(), 1);
    t1.vin[0].scriptSig << std::vector<uint8_t>(65, 0);
    t1.vin[1].prevout = COutPoint(dummyTransactions[1].GetId(), 0);
    t1.vin[1].scriptSig << std::vector<uint8_t>(65, 0)
                        << std::vector<uint8_t>(33, 4);
    t1.vin[2].prevout = COutPoint(dummyTransactions[1].GetId(), 1);
    t1.vin[2].scriptSig << std::vector<uint8_t>(65, 0)
                        << std::vector<uint8_t>(33, 4);
    t1.vout.resize(2);
    t1.vout[0].nValue = 90 * CENT;
    t1.vout[0].scriptPubKey << OP_1;

    // Benchmark.
    while (state.KeepRunning()) {
        CTransaction t(t1);
        bool success =
            AreInputsStandard(
                task::CCancellationSource::Make()->GetToken(),
                GlobalConfig::GetConfig(),
                t,
                coins,
                0).value();
        assert(success);
        Amount value = coins.GetValueIn(t);
        assert(value == (50 + 21 + 22) * CENT);
    }
}

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

    CCoinsMap& GetRawCacheCoins() { return TestAccessCoinsCache::GetRawCacheCoins(mShards[0].GetCache()); }
    size_t& GetCachedCoinsUsage() { return TestAccessCoinsCache::GetCachedCoinsUsage(mShards[0].GetCache()); }
    const std::vector<Shard>& GetShards() const { return mShards; }

    void BatchWrite(CCoinsMap& mapCoins, const uint256& hashBlockIn)
    {
        mShards[0].GetCache().BatchWrite(mapCoins);
        SetBestBlock(hashBlockIn);
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

    void DBCacheAllInputs(const std::vector<CTransactionRef>& txns, bool V1) const
    {
        // call new version of cached inputs
        if (!V1) {
            CoinsDB::DBCacheAllInputs(txns);
            return;
        }

        // old implementation is copied here
        // Sort inputs; leveldb seems to prefer it that way
        auto Sort = [](const COutPoint& out1, const COutPoint& out2)
        {
            if(out1.GetTxId() == out2.GetTxId())
            {
                return out1.GetN() < out2.GetN();
            }
            return out1.GetTxId() < out2.GetTxId();
        };

        std::vector<COutPoint> allInputs {};
        for(size_t i = 1; i < txns.size(); ++i)
        {
            for(const auto& in: txns[i]->vin)
            {
                allInputs.push_back(in.prevout);
            }
        }

        std::sort(allInputs.begin(), allInputs.end(), Sort);

        std::unique_lock lock { mCoinsViewCacheMtx };

        for(const auto& outpoint : allInputs)
        {
            auto coinFromCache = mCache.FetchCoin(outpoint);
            if (!coinFromCache.has_value() || (!coinFromCache->IsSpent() && !coinFromCache->HasScript()))
            {
                auto coinFromView = DBGetCoin(outpoint, std::numeric_limits<uint64_t>::max());
                if (coinFromView.has_value())
                {
                    if (coinFromCache.has_value())
                    {
                        if (hasSpaceForScript(coinFromView->GetScriptSize()))
                        {
                            mCache.ReplaceWithCoinWithScript(outpoint, std::move(coinFromView.value()));
                        }
                    }
                    else
                    {
                        if (hasSpaceForScript(coinFromView->GetScriptSize()))
                        {
                            mCache.AddCoin(outpoint, std::move(coinFromView.value()));
                        }
                        else
                        {
                            mCache.AddCoin(
                                outpoint,
                                CoinImpl {
                                    coinFromView->GetTxOut().nValue,
                                    coinFromView->GetScriptSize(),
                                    coinFromView->GetHeight(),
                                    coinFromView->IsCoinBase(),
                                    coinFromView->IsConfiscation()
                                }
                            );
                        }
                    }
                }
            }
        }
    }

protected:
    std::optional<CoinImpl> GetCoin(const COutPoint &outpoint, uint64_t maxScriptSize) const
    {
        mLatestRequestedScriptSize = (mOverrideSize.has_value() ? mOverrideSize.value() : maxScriptSize);
        mLatestGetCoin = CoinsDB::GetCoin(outpoint, mLatestRequestedScriptSize);

        return mLatestGetCoin->MakeNonOwning();
    }

    using CoinsDB::ReadLock;

private:
    mutable uint64_t mLatestRequestedScriptSize;
    mutable std::optional<CoinImpl> mLatestGetCoin;
    std::optional<uint64_t> mOverrideSize;

    friend class CTestCoinsView;
};

using CCoinsProviderTest = CoinsDB::UnitTestAccess<coins_tests_uid>;

static void CCoinsInsertion(benchmark::State &state, bool V1) {
    SelectBaseParams(CBaseChainParams::MAIN);
    constexpr uint16_t NumTxns {8};
    std::vector<CTransactionRef> txns;
    for(int i = 0; i < NumTxns; ++i)
    {
        CMutableTransaction txn {};
        txn.vin.resize(1);
        txn.vin[0].prevout = COutPoint{GetRandHash(), 0};
        txn.vin[0].scriptSig << OP_RETURN;
        txns.push_back(MakeTransactionRef(txn));
    }

    // Hash and height of a block that contains unspent transactions
    uint256 blockHash { GetRandHash() };

    // Create fresh coins DB
    CCoinsProviderTest provider {1024};

    while (state.KeepRunning())
    {
        TestCoinsSpanCache span { provider };
        span.SetBestBlock(blockHash);

        auto shardedTarget = [](uint16_t shardIndex,
                                CCoinsViewCache::Shard& shard,
                                const std::vector<CTransactionRef>& txns)
        {
            CoinWithScript coin;
            auto& tx = txns[shardIndex];
            for (auto& vin : tx->vin) {
                shard.SpendCoin(vin.prevout, &coin);
            }
            return true;
        };

        std::thread spawner([&]() {
            std::vector<std::thread> threads;
            for(int i = 0; i < NumTxns; ++i) {
                threads.emplace_back([&]() {
                    // Cache them all (Except the first in the list, which the function expects to be coinbase)
                    provider.DBCacheAllInputs(txns, V1);
                });
            }
            for (auto& thread : threads) {
                thread.join();
            }
        });

        auto results = span.RunSharded(NumTxns, shardedTarget, std::cref(txns));
        spawner.join();
    }
}

static void CCoinsInsertionV1(benchmark::State &state) {
    CCoinsInsertion(state, /* V1 */ true);
}

static void CCoinsInsertionV2(benchmark::State &state) {
    CCoinsInsertion(state, /* V1 */ false);
}

BENCHMARK(CCoinsCaching)
BENCHMARK(CCoinsInsertionV1)
BENCHMARK(CCoinsInsertionV2)
