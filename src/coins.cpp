// Copyright (c) 2012-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "coins.h"

#include "consensus/consensus.h"
#include "memusage.h"
#include "random.h"

#include <cassert>
#include <config.h>

SaltedOutpointHasher::SaltedOutpointHasher()
    : k0(GetRand(std::numeric_limits<uint64_t>::max())),
      k1(GetRand(std::numeric_limits<uint64_t>::max())) {}

CCoinsViewCache::CCoinsViewCache(const ICoinsView& view)
    : mThreadId{std::this_thread::get_id()}
    , mSourceView{&view}
    , mView{mSourceView}
{}

size_t CCoinsViewCache::DynamicMemoryUsage() const {
    assert(mThreadId == std::this_thread::get_id());
    return mCache.DynamicMemoryUsage();
}

std::optional<std::reference_wrapper<const Coin>>
CCoinsViewCache::FetchCoin(const COutPoint &outpoint) const
{
    if (auto it = mCache.FetchCoin(outpoint); it.has_value())
    {
        return it;
    }

    Coin tmp;
    if (!mView->GetCoin(outpoint, tmp)) {
        return {};
    }

    return mCache.AddCoin(outpoint, std::move(tmp));
}

void CCoinsViewCache::AddCoin(const COutPoint &outpoint, Coin&& coin,
                              bool possible_overwrite,
                              int32_t genesisActivationHeight) {
    assert(mThreadId == std::this_thread::get_id());
    assert(!coin.IsSpent());
    if (coin.GetTxOut().scriptPubKey.IsUnspendable( coin.GetHeight() >= genesisActivationHeight)) {
        return;
    }

    mCache.AddCoin(outpoint, std::move(coin), possible_overwrite, genesisActivationHeight);
}

void AddCoins(CCoinsViewCache &cache, const CTransaction &tx, int32_t nHeight, int32_t genesisActivationHeight,
              bool check) {
    bool fCoinbase = tx.IsCoinBase();
    const TxId txid = tx.GetId();
    for (size_t i = 0; i < tx.vout.size(); ++i) {
        const COutPoint outpoint(txid, i);
        bool overwrite = check ? cache.HaveCoin(outpoint) : fCoinbase;
        // Always set the possible_overwrite flag to AddCoin for coinbase txn,
        // in order to correctly deal with the pre-BIP30 occurrences of
        // duplicate coinbase transactions.
        cache.AddCoin(outpoint, Coin(tx.vout[i], nHeight, fCoinbase),
                      overwrite, genesisActivationHeight);
    }
}

bool CCoinsViewCache::SpendCoin(const COutPoint &outpoint, Coin *moveout) {
    assert(mThreadId == std::this_thread::get_id());
    auto it = FetchCoin(outpoint);
    if (!it.has_value()) {
        return false;
    }

    mCache.SpendCoin(outpoint, moveout);

    return true;
}

static const Coin coinEmpty;

const Coin& CCoinsViewCache::AccessCoin(const COutPoint &outpoint) const {
    assert(mThreadId == std::this_thread::get_id());
    auto coin = FetchCoin(outpoint);
    if (!coin.has_value()) {
        return coinEmpty;
    }
    return coin.value();
}

bool CCoinsViewCache::HaveCoin(const COutPoint &outpoint) const {
    assert(mThreadId == std::this_thread::get_id());
    auto coin = FetchCoin(outpoint);
    return coin.has_value() && !coin->get().IsSpent();
}

uint256 CCoinsViewCache::GetBestBlock() const {
    assert(mThreadId == std::this_thread::get_id());
    if (hashBlock.IsNull()) {
        hashBlock = mView->GetBestBlock();
    }
    return hashBlock;
}

void CCoinsViewCache::SetBestBlock(const uint256 &hashBlockIn) {
    assert(mThreadId == std::this_thread::get_id());
    hashBlock = hashBlockIn;
}

Amount CCoinsViewCache::GetValueIn(const CTransaction &tx) const {
    assert(mThreadId == std::this_thread::get_id());
    if (tx.IsCoinBase()) {
        return Amount(0);
    }

    Amount nResult(0);

    for (const auto& input: tx.vin) {
        auto coin = mCache.FetchCoin(input.prevout);
        assert(coin.has_value());
        assert(!coin->get().IsSpent());

        nResult += coin->get().GetTxOut().nValue;
    }

    return nResult;
}

bool CCoinsViewCache::HaveInputs(const CTransaction &tx) const {
    assert(mThreadId == std::this_thread::get_id());
    if (tx.IsCoinBase()) {
        return true;
    }

    for (const auto& input: tx.vin) {
        if (!HaveCoin(input.prevout)) {
            return false;
        }
    }

    return true;
}

std::optional<bool> CCoinsViewCache::HaveInputsLimited(
    const CTransaction &tx,
    size_t maxCachedCoinsUsage) const
{
    assert(mThreadId == std::this_thread::get_id());
    if (tx.IsCoinBase()) {
        return true;
    }

    for (const auto& input: tx.vin) {
        if (!HaveCoin(input.prevout)) {
            return false;
        }

        if(maxCachedCoinsUsage > 0 && mCache.DynamicMemoryUsage() >= maxCachedCoinsUsage)
        {
            return {};
        }
    }

    return true;
}

double CCoinsViewCache::GetPriority(const CTransaction &tx, int32_t nHeight,
                                    Amount &inChainInputValue) const {
    assert(mThreadId == std::this_thread::get_id());
    inChainInputValue = Amount(0);
    if (tx.IsCoinBase()) {
        return 0.0;
    }
    double dResult = 0.0;

    for (const CTxIn &txin : tx.vin) {
        const Coin &coin = AccessCoin(txin.prevout);
        if (coin.IsSpent()) {
            continue;
        }
        if (int64_t(coin.GetHeight()) <= nHeight) {
            dResult += double(coin.GetTxOut().nValue.GetSatoshis()) *
                       (nHeight - coin.GetHeight());
            inChainInputValue += coin.GetTxOut().nValue;
        }
    }

    return tx.ComputePriority(dResult);
}

size_t CoinsStore::DynamicMemoryUsage() const
{
    return memusage::DynamicUsage(cacheCoins) + cachedCoinsUsage;
}

std::optional<std::reference_wrapper<const Coin>> CoinsStore::FetchCoin(const COutPoint& outpoint) const
{
    if (auto it = cacheCoins.find(outpoint); it != cacheCoins.end())
    {
        return it->second.GetCoin();
    }

    return {};
}

const Coin& CoinsStore::AddCoin(const COutPoint& outpoint, Coin&& coin)
{
    CCoinsMap::iterator it =
        cacheCoins
            .emplace(std::piecewise_construct, std::forward_as_tuple(outpoint),
                     std::forward_as_tuple(std::move(coin), CCoinsCacheEntry::Flags(0)))
            .first;
    if (it->second.GetCoin().IsSpent()) {
        // The parent only has an empty entry for this outpoint; we can consider
        // our version as fresh.
        it->second.flags = CCoinsCacheEntry::FRESH;
    }
    cachedCoinsUsage += it->second.DynamicMemoryUsage();

    return it->second.GetCoin();
}

void CoinsStore::AddCoin(
    const COutPoint& outpoint,
    Coin&& coin,
    bool possible_overwrite,
    uint64_t genesisActivationHeight)
{
    auto [it, inserted] =
        cacheCoins.emplace(std::piecewise_construct,
                           std::forward_as_tuple(outpoint), std::tuple<>());
    bool fresh = false;
    if (!possible_overwrite) {
        if (!it->second.GetCoin().IsSpent()) {
            throw std::logic_error(
                "Adding new coin that replaces non-pruned entry");
        }
        fresh = !(it->second.flags & CCoinsCacheEntry::DIRTY);
    }
    if (!inserted) {
        cachedCoinsUsage -= it->second.DynamicMemoryUsage();
    }
    it->second =
        CCoinsCacheEntry{
            std::move(coin),
            static_cast<uint8_t>(it->second.flags | CCoinsCacheEntry::DIRTY | (fresh ? CCoinsCacheEntry::FRESH : 0u))
        };
    cachedCoinsUsage += it->second.DynamicMemoryUsage();
}

void CoinsStore::AddEntry(const COutPoint& outpoint, CCoinsCacheEntry&& entryIn)
{
    uint8_t flags = CCoinsCacheEntry::DIRTY;

    if (entryIn.flags & CCoinsCacheEntry::FRESH)
    {
        // We can mark it FRESH in the parent if it was FRESH in the
        // child. Otherwise it might have just been flushed from the
        // parent's cache and already exist in the grandparent
        flags |= CCoinsCacheEntry::FRESH;
    }

    CCoinsCacheEntry &entry = cacheCoins[outpoint];
    entry = std::move(entryIn);
    cachedCoinsUsage += entry.DynamicMemoryUsage();
    entry.flags = flags;
}

bool CoinsStore::SpendCoin(const COutPoint& outpoint, Coin* moveout)
{
    CCoinsMap::iterator it = cacheCoins.find(outpoint);
    if (it == cacheCoins.end()) {
        return false;
    }
    cachedCoinsUsage -= it->second.DynamicMemoryUsage();
    if (moveout) {
        *moveout = it->second.MoveCoin();
    }
    if (it->second.flags & CCoinsCacheEntry::FRESH) {
        cacheCoins.erase(it);
    } else {
        it->second.flags |= CCoinsCacheEntry::DIRTY;
        it->second.Clear();
    }
    return true;
}

void CoinsStore::UpdateEntry(CCoinsMap::iterator itUs, CCoinsCacheEntry&& coinEntry)
{
    cachedCoinsUsage -= itUs->second.DynamicMemoryUsage();
    uint8_t flags = itUs->second.flags;
    itUs->second = std::move(coinEntry);
    cachedCoinsUsage += itUs->second.DynamicMemoryUsage();
    itUs->second.flags = flags | CCoinsCacheEntry::DIRTY;
    // NOTE: It is possible the child has a FRESH flag here in
    // the event the entry we found in the parent is pruned. But
    // we must not copy that FRESH flag to the parent as that
    // pruned state likely still needs to be communicated to the
    // grandparent.
}

void CoinsStore::EraseCoin(CCoinsMap::const_iterator itUs)
{
    cachedCoinsUsage -= itUs->second.DynamicMemoryUsage();
    cacheCoins.erase(itUs);
}

void CoinsStore::Uncache(const std::vector<COutPoint>& vOutpoints)
{
    for (const COutPoint &outpoint : vOutpoints) {
        CCoinsMap::iterator it = cacheCoins.find(outpoint);
        if (it != cacheCoins.end() && it->second.flags == 0) {
            EraseCoin(it);
        }
    }
}

void CoinsStore::BatchWrite(CCoinsMap& mapCoins)
{
    for (CCoinsMap::iterator it = mapCoins.begin(); it != mapCoins.end();) {
        // Ignore non-dirty entries (optimization).
        if (it->second.flags & CCoinsCacheEntry::DIRTY) {
            auto itUs = cacheCoins.find(it->first);
            if (itUs == cacheCoins.end()) {
                // The parent cache does not have an entry, while the child does
                // We can ignore it if it's both FRESH and pruned in the child
                if (!(it->second.flags & CCoinsCacheEntry::FRESH &&
                      it->second.GetCoin().IsSpent())) {
                    AddEntry(it->first, std::move(it->second));
                }
            } else {
                auto& coinEntry = itUs->second;
                // Assert that the child cache entry was not marked FRESH if the
                // parent cache entry has unspent outputs. If this ever happens,
                // it means the FRESH flag was misapplied and there is a logic
                // error in the calling code.
                if ((it->second.flags & CCoinsCacheEntry::FRESH) &&
                    !coinEntry.GetCoin().IsSpent())
                    throw std::logic_error("FRESH flag misapplied to cache "
                                           "entry for base transaction with "
                                           "spendable outputs");

                // Found the entry in the parent cache
                if ((coinEntry.flags & CCoinsCacheEntry::FRESH) &&
                    it->second.GetCoin().IsSpent()) {
                    // The grandparent does not have an entry, and the child is
                    // modified and being pruned. This means we can just delete
                    // it from the parent.
                    EraseCoin(itUs);
                } else {
                    // A normal modification.
                    UpdateEntry(
                        itUs,
                        std::move(it->second));
                }
            }
        }
        CCoinsMap::iterator itOld = it++;
        mapCoins.erase(itOld);
    }
}
