// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_COINS_H
#define BITCOIN_COINS_H

#include "compressor.h"
#include "core_memusage.h"
#include "hash.h"
#include "memusage.h"
#include "serialize.h"
#include "txhasher.h"
#include "uint256.h"

#include <cassert>
#include <cstdint>
#include <unordered_map>
#include <mutex>
#include <optional>
#include <thread>

class CoinWithScript;

/**
 * Immutable coin type for use inside CCoinsProvider structure.
 *
 * Storage content:
 * It can either store coin data with or without scriptPubKey being present.
 *
 * Storage ownership:
 * It can store either its own CTxOut data or point to CTxOut data that is
 * owned by a different CoinImpl.
 *
 * NOTE: Only CoinImpl instances that contain the script and are not spent
 *       can be serialized.
 */
class CoinImpl
{
    /**
     * Unspent transaction output that is only set if coin instance is storage
     * owner - otherwise it's not set.
     */
    std::optional<CTxOut> storage;
    /**
     * Pointer to either local storage or to a different CoinImpl::storage.
     * In case the instance is not the owner it is expected that lifetime of
     * owning CoinImpl is longer than the non-owning instance.
     */
    const CTxOut* out;

    //! Whether containing transaction was a coinbase and height at which the
    //! transaction was included into a block.
    // This variable is unsigned even though height is signed type consistently in the codebase.
    // The reason is that shifting on negative numbers causes undefined behavior.
    uint32_t nHeightAndIsCoinBase{0};

    /**
     * Size of the storage.scriptPubKey that is available even if the script
     * itself is not loaded.
     */
    uint64_t mScriptSize{0};

public:
    CoinImpl() : storage{CTxOut{}}, out{&storage.value()} {}

    CoinImpl(Amount amount, uint64_t scriptSize, int32_t nHeightIn, bool IsCoinbase)
        : storage{CTxOut{amount, {}}}
        , out{&storage.value()}
        , nHeightAndIsCoinBase{(static_cast<uint32_t>(nHeightIn) << 1) | IsCoinbase}
        , mScriptSize{scriptSize}
    {}

    CoinImpl(CoinImpl&& other) noexcept
        : storage{std::move(other.storage)}
        , out{storage.has_value() ? &storage.value() : other.out}
        , nHeightAndIsCoinBase{other.nHeightAndIsCoinBase}
        , mScriptSize{other.mScriptSize}
    {
        other.Clear();
    }

    static CoinImpl FromCoinWithScript(CoinWithScript&& other) noexcept;

    CoinImpl& operator=(CoinImpl&& other) noexcept
    {
        storage = std::move(other.storage);
        out = (storage.has_value() ? &storage.value() : other.out);
        nHeightAndIsCoinBase = other.nHeightAndIsCoinBase;
        mScriptSize = other.mScriptSize;

        other.Clear();

        return *this;
    }

    CoinImpl MakeOwning() const
    {
        return {CTxOut{GetTxOut()}, GetScriptSize(), GetHeight(), IsCoinBase()};
    }

    CoinImpl MakeNonOwning() const
    {
        return {GetTxOut(), GetScriptSize(), GetHeight(), IsCoinBase()};
    }

    static CoinImpl MakeNonOwningWithScript(const CTxOut& outIn, int32_t nHeightIn, bool IsCoinbase)
    {
        return {outIn, outIn.scriptPubKey.size(), nHeightIn, IsCoinbase};
    }

    //! NOTE: serialization is only allowed if scriptPubKey is loaded!
    template <typename Stream> void Serialize(Stream &s) const {
        assert(!IsSpent());
        assert(HasScript());

        ::Serialize(s, VARINT(nHeightAndIsCoinBase));
        ::Serialize(s, CTxOutCompressor(REF(*out)));
    }

    template <typename Stream> void Unserialize(Stream &s)
    {
        assert(storage.has_value());

        ::Unserialize(s, VARINT(nHeightAndIsCoinBase));
        ::Unserialize(s, REF(CTxOutCompressor(storage.value())));
        mScriptSize = out->scriptPubKey.size();
    }

    int32_t GetHeight() const {
        return static_cast<int32_t>(nHeightAndIsCoinBase >> 1);
    }
    bool IsCoinBase() const { return nHeightAndIsCoinBase & 0x01; }
    uint64_t GetScriptSize() const { return mScriptSize; }

    bool IsSpent() const { return out->nValue == Amount{-1}; }
    bool HasScript() const { return mScriptSize == out->scriptPubKey.size(); }

    const CTxOut& GetTxOut() const { return *out; }

    size_t DynamicMemoryUsage() const {
        return (HasScript() && IsStorageOwner() ? memusage::DynamicUsage(out->scriptPubKey) : 0);
    }

    bool IsStorageOwner() const { return storage.has_value(); };

protected:
    CoinImpl(const CoinImpl& other)
        : storage{other.storage}
        , out{storage.has_value() ? &storage.value() : other.out}
        , nHeightAndIsCoinBase{other.nHeightAndIsCoinBase}
        , mScriptSize{other.mScriptSize}
    {}

    CoinImpl(CTxOut&& outIn, uint64_t scriptSize, int32_t nHeightIn, bool IsCoinbase)
        : storage{std::move(outIn)}
        , out{&storage.value()}
        , nHeightAndIsCoinBase{(static_cast<uint32_t>(nHeightIn) << 1) | IsCoinbase}
        , mScriptSize{scriptSize}
    {}

private:
    CoinImpl(const CTxOut& outIn, uint64_t scriptSize, int32_t nHeightIn, bool IsCoinbase)
        : out{&outIn}
        , nHeightAndIsCoinBase{(static_cast<uint32_t>(nHeightIn) << 1) | IsCoinbase}
        , mScriptSize{scriptSize}
    {}

    CoinImpl& operator=(const CoinImpl& other) = delete;

    void Clear()
    {
        storage = CTxOut{};
        out = &storage.value();
        nHeightAndIsCoinBase = 0;
    }
};

/**
 * A UTXO entry without scriptPubKey.
 */
class Coin
{
    //! Unspent transaction output amount
    Amount mAmount{ -1 };

    //! Whether containing transaction was a coinbase and height at which the
    //! transaction was included into a block.
    // This variable is unsigned even though height is signed type consistently in the codebase.
    // The reason is that shifting on negative numbers causes undefined behavior.
    uint32_t nHeightAndIsCoinBase{ 0 };

    uint64_t mScriptSize{ 0 };

public:
    Coin() = default;

    explicit Coin(const CoinImpl& coin)
        : mAmount{coin.GetTxOut().nValue}
        , nHeightAndIsCoinBase((static_cast<uint32_t>(coin.GetHeight()) << 1) | coin.IsCoinBase())
        , mScriptSize{coin.GetScriptSize()}
    {}

    int32_t GetHeight() const
    {
        return static_cast<int32_t>(nHeightAndIsCoinBase >> 1);
    }
    bool IsCoinBase() const { return nHeightAndIsCoinBase & 0x01; }
    bool IsSpent() const { return mAmount == Amount{-1}; }
    uint64_t GetScriptSize() const { return mScriptSize; }

    const Amount& GetAmount() const { return mAmount; }
};

/**
 * A UTXO entry.
 *
 * NOTE: CoinWithScript is not necessarily storage owner and in case we need to
 *       extend the lifetime of this coin past the storage owner's destruction
 *       (usually that is an object on which GetCoinWithScript() was called)
 *       we need to make a deep copy.
 *
 * This is a public API wrapper around CoinImpl so it can either own or point
 * to the CTxOut content.
 * It is always guaranteed to contain scriptPubKey data.
 * Class is used in code outside CCoinsProvider structure and can be retrieved
 * through provider views/spans.
 *
 * This class is used for access to CTxOut and serialization of Coin class as
 * it is guaranteed to contain loaded CTxOut script data.
 *
 * Serialized format:
 * - VARINT((coinbase ? 1 : 0) | (height << 1))
 * - the non-spent CTxOut (via CTxOutCompressor)
 */
class CoinWithScript : private CoinImpl
{
public:
    //! Empty constructor
    CoinWithScript() = default;

    CoinWithScript(const CoinWithScript&) = delete;
    CoinWithScript& operator=(const CoinWithScript&) = delete;

    CoinWithScript(CoinWithScript&& coin) noexcept
        : CoinImpl{std::move(coin)}
    {}

    CoinWithScript(CoinImpl&& coin) noexcept
        : CoinImpl{std::move(coin)}
    {}

    CoinWithScript MakeOwning() const
    {
        return {CTxOut(GetTxOut()), GetHeight(), IsCoinBase()};
    }

    static CoinWithScript MakeOwning(CTxOut&& outIn, int32_t nHeightIn, bool IsCoinbase)
    {
        return {std::move(outIn), nHeightIn, IsCoinbase};
    }

    CoinWithScript& operator=(CoinWithScript&& other)
    {
        static_cast<CoinImpl&>(*this) = std::move(other);

        return *this;
    }

    using
        CoinImpl::Serialize,
        CoinImpl::GetTxOut,
        CoinImpl::DynamicMemoryUsage,
        CoinImpl::GetHeight,
        CoinImpl::IsCoinBase,
        CoinImpl::IsSpent,
        CoinImpl::GetScriptSize,
        CoinImpl::IsStorageOwner;

    const Amount& GetAmount() const { return GetTxOut().nValue; }

private:
    //! Constructor from a CTxOut and height/coinbase information.
    CoinWithScript(CTxOut&& outIn, int32_t nHeightIn, bool IsCoinbase)
        : CoinImpl{std::move(outIn), outIn.scriptPubKey.size(), nHeightIn, IsCoinbase}
    {}

    CoinImpl ToCoinImpl() && { return std::move( *this ); }

    friend CoinImpl CoinImpl::FromCoinWithScript(CoinWithScript&& other) noexcept;
};

inline CoinImpl CoinImpl::FromCoinWithScript(CoinWithScript&& other) noexcept
{
    return std::move( other ).ToCoinImpl();
}

class CCoinsCacheEntry {
    // The actual cached data.
    CoinImpl coin;

public:
    uint8_t flags;

    enum Flags : uint8_t{
        // This cache entry is potentially different from the version in the
        // parent view.
        DIRTY = (1 << 0),
        // The parent view does not have this entry (or it is pruned).
        FRESH = (1 << 1),
        /* Note that FRESH is a performance optimization with which we can erase
           coins that are fully spent if we know we do not need to flush the
           changes to the parent cache. It is always safe to not mark FRESH if
           that condition is not guaranteed. */
    };

    CCoinsCacheEntry() : flags(0u) {}
    CCoinsCacheEntry(CoinImpl&& coinIn, uint8_t flagsIn)
        : coin(std::move(coinIn))
        , flags(flagsIn)
    {}

    const CoinImpl& GetCoinImpl() const { return coin; }
    Coin GetCoin() const { return Coin{coin}; }

    std::optional<CoinWithScript> GetCoinWithScript() const
    {
        if(coin.HasScript() || coin.IsSpent())
        {
            return coin.MakeNonOwning();
        }

        return {};
    }

    void Clear()
    {
        coin = {};
    }

    void ReplaceWithCoinWithScript(CoinImpl&& newCoin)
    {
        assert(!coin.HasScript());
        assert(newCoin.HasScript());
        coin = std::move(newCoin);
    }

    size_t DynamicMemoryUsage() const { return coin.DynamicMemoryUsage(); }
};

typedef std::unordered_map<COutPoint, CCoinsCacheEntry, SaltedOutpointHasher>
    CCoinsMap;

/**
 * UTXO coins view interface.
 *
 * Implementations of this interface provide basic functionality that is needed
 * by CCoinsViewCache.
 */
class ICoinsView {
protected:
    //! As we use CCoinsViews polymorphically, have protected destructor as we
    //! don't want to support polymorphic destruction.
    ~ICoinsView() = default;

    //! Retrieve the Coin (unspent transaction output) for a given outpoint.
    virtual std::optional<CoinImpl> GetCoin(const COutPoint &outpoint, uint64_t maxScriptSize) const = 0;

    //! Retrieve the block hash whose state this CCoinsProvider currently represents
    virtual uint256 GetBestBlock() const = 0;

    virtual void ReleaseLock() { assert(!"Should not be used!"); }
    virtual void ReLock() { assert(!"Should not be used!"); }

    friend class CCoinsViewCache;
};

/**
 * Coins view that never contains coins - dummy.
 */
class CCoinsViewEmpty : public ICoinsView
{
protected:
    std::optional<CoinImpl> GetCoin(const COutPoint &outpoint, uint64_t maxScriptSize) const override { return {}; }
    uint256 GetBestBlock() const override { return {}; }

    void ReleaseLock() override {}
    void ReLock() override {}
};

/**
 * Class for storing coins - either owning or non-owning.
 *
 * This is a helper class intended to be used by coin view/span/coins db classes.
 */
class CoinsStore
{
public:
    template<typename T> struct UnitTestAccess;

    size_t DynamicMemoryUsage() const;

    size_t CachedCoinsCount() const { return cacheCoins.size(); }

    std::optional<CoinImpl> FetchCoin(const COutPoint& outpoint) const;

    CCoinsMap MoveOutCoins()
    {
        cachedCoinsUsage = 0;
        auto map = std::move(cacheCoins);
        cacheCoins.clear();

        return map;
    }

    const CoinImpl& AddCoin(const COutPoint& outpoint, CoinImpl&& coin);
    void AddCoin(
        const COutPoint& outpoint,
        CoinWithScript&& coin,
        bool possible_overwrite,
        uint64_t genesisActivationHeight);
    bool SpendCoin(const COutPoint& outpoint);
    void Uncache(const std::vector<COutPoint>& vOutpoints);
    void BatchWrite(CCoinsMap& mapCoins);

    const CoinImpl& ReplaceWithCoinWithScript(const COutPoint& outpoint, CoinImpl&& newCoin)
    {
        auto it = cacheCoins.find(outpoint);
        assert(it != cacheCoins.end());

        it->second.ReplaceWithCoinWithScript(std::move(newCoin));
        cachedCoinsUsage += it->second.DynamicMemoryUsage();

        return it->second.GetCoinImpl();
    }

private:
    void AddEntry(const COutPoint& outpoint, CCoinsCacheEntry&& entryIn);
    void UpdateEntry(CCoinsMap::iterator itUs, CCoinsCacheEntry&& coinEntry);
    void EraseCoin(CCoinsMap::const_iterator itUs);

    mutable CCoinsMap cacheCoins;

    /* Cached dynamic memory usage for the inner Coin objects. */
    mutable size_t cachedCoinsUsage{0};
};

/**
 * A cached coins view that is expected to be used from only one thread.
 *
 * Cache of this class stores:
 * - non-owning coins pointing to owning coin with script in underlying view
 * - owning coins without script retrieved from the underlying view
 * - owning coins without script that were spent with through SpendCoin()
 * - owning coins with script added to this cache through AddCoin()
 */
class CCoinsViewCache
{
protected:
    /**
     * Make mutable so that we can "fill the cache" even from Get-methods
     * declared as "const".
     */
    mutable uint256 hashBlock;
    mutable CoinsStore mCache;

public:
    explicit CCoinsViewCache(const ICoinsView& view);

    CCoinsViewCache(const CCoinsViewCache&) = delete;
    CCoinsViewCache& operator=(const CCoinsViewCache&) = delete;
    CCoinsViewCache(CCoinsViewCache&&) = delete;
    CCoinsViewCache& operator=(CCoinsViewCache&&) = delete;

    //! Calculate the size of the cache (in bytes)
    size_t DynamicMemoryUsage() const;
    bool HaveCoin(const COutPoint &outpoint) const;
    uint256 GetBestBlock() const;
    void SetBestBlock(const uint256 &hashBlock);

    // If found return basic coin info without script loaded
    std::optional<Coin> GetCoin(const COutPoint& outpoint) const
    {
        auto coinData = GetCoin(outpoint, 0);
        if(coinData.has_value())
        {
            return Coin{coinData.value()};
        }

        return {};
    }

    // Return coin with script loaded
    //
    // It will return either:
    // * a non owning coin pointing to the coin stored in view hierarchy cache
    // * an owning coin if there is not enough space for coin in cache
    // * nothing if coin is not found
    //
    // Non owning coins must be released before view goes out of scope
    std::optional<CoinWithScript> GetCoinWithScript(const COutPoint& outpoint) const
    {
        auto coinData = GetCoin(outpoint, std::numeric_limits<size_t>::max());
        if(coinData.has_value())
        {
            assert(coinData->HasScript());

            return std::move(coinData.value());
        }

        return {};
    }

    /**
     * Add a coin. Set potential_overwrite to true if a non-pruned version may
     * already exist.
     * genesisActivationHeight parameter is used to check if Genesis upgrade rules
     * are in effect for this coin. It is required to correctly determine if coin is unspendable.
     *
     * NOTE: It is possible that a coin already exists in the underlying view
     *       therefore it is required by the caller that it has already called
     *       GetCoin() on the same view instance beforehand. This enables
     *       correct handling of overrides - checked more thoroughly if DEBUG
     *       macro is enabled during compilation (enable_debug flag).
     *       This is not performed inside the function as it causes a slow
     *       additional access to database for every non existent coin
     *       (something that should already be checked by external code).
     */
    void AddCoin(const COutPoint &outpoint, CoinWithScript&& coin,
                 bool potential_overwrite, int32_t genesisActivationHeight);

    /**
     * Spend a coin. Pass moveto in order to get the deleted data.
     * If no unspent output exists for the passed outpoint, this call has no
     * effect.
     */
    bool SpendCoin(const COutPoint &outpoint, CoinWithScript *moveto = nullptr);

    /**
     * Amount of bitcoins coming in to a transaction
     * Note that lightweight clients may not know anything besides the hash of
     * previous transactions, so may not be able to calculate this.
     *
     * @param[in] tx    transaction for which we are checking input total
     * @return  Sum of value of all inputs (scriptSigs)
     */
    Amount GetValueIn(const CTransaction &tx) const;

    //! Check whether all prevouts of the transaction are present in the UTXO
    //! set represented by this view
    bool HaveInputs(const CTransaction &tx) const;

    //! Same as HaveInputs but with addition of limiting cache size
    //! If result is std::nullopt
    std::optional<bool> HaveInputsLimited(const CTransaction &tx, size_t maxCachedCoinsUsage) const;

    /**
     * Detach provider - this function expects that view won't be used until
     * it is re-attached by calling TryReattach().
     *
     * This functionality is needed for parallel block validation so that it
     * can continue validating loosing blocks while still being able to commit
     * the changes for the block that won to the database.
     *
     * Function is dangerous to use since it removes coins cache stability so
     * the code between detach and re-attach should not access any coins.
     *
     * It is used in combination wit TryReattach()
     */
    void ForceDetach()
    {
        assert(mThreadId == std::this_thread::get_id());
        if(mView != &mViewEmpty)
        {
            GetBestBlock();
            mView = &mViewEmpty;
            const_cast<ICoinsView*>( mSourceView )->ReleaseLock();
        }
    }

    /**
     * Try to re-attach view to once again use it. It is expected that in case
     * best block hash hasn't changed the cached entries are still valid.
     *
     * Function returns true if re-attachment is possible (current best block
     * hasn't changed) and false otherwise. In case false is returned the class
     * should be destroyed without using any other functions.
     *
     * It is used in combination wit ForceDetach() - see for use
     */
    bool TryReattach()
    {
        assert(mThreadId == std::this_thread::get_id());
        if(mView == mSourceView)
        {
            return true;
        }

        const_cast<ICoinsView*>( mSourceView )->ReLock();

        if(mSourceView->GetBestBlock() != GetBestBlock())
        {
            return false;
        }

        mView = mSourceView;

        return true;
    }

private:
    /**
     * The function returns a CoinImpl object:
     * - If a non owning coin is in cache it is returned (non owning coin points
     *   to underlying view cache entry) and is guaranteed to have a loaded
     *   script
     * - If an owning coin is in cache:
     *   + If requiresScript is false an owning coin without script is returned
     *   + If requiresScript is true a GetCoin() is called on underlying view
     * - If coin is not in cache GetCoin() is called on underlying view
     *
     * Call to underlying view returns:
     * - Owning coin without script:
     *   + Owning coin without script is stored in cache
     *   + Another owning coin is returned
     * - Owning coin with script:
     *   + Owning coin without script is stored in cache
     *   + Owning coin with script is returned
     * - Non owning coin (guaranteed to contain script):
     *   + Non owning coin is stored in cache
     *   + Another non owning coin is returned
     */
    std::optional<CoinImpl> GetCoin(const COutPoint &outpoint, bool requiresScript) const;

    inline static CCoinsViewEmpty mViewEmpty;

protected:
    // variable is only used for asserts to make sure that users are not using
    // it in an unsupported way - from more than one thread
    std::thread::id mThreadId;

    const ICoinsView* mSourceView; // can't be null but must be a pointer for lazy binding
    const ICoinsView* mView;
};

//! Utility function to add all of a transaction's outputs to a cache.
// When check is false, this assumes that overwrites are only possible for
// coinbase transactions.
// When check is true, the underlying view may be queried to determine whether
// an addition is an overwrite.
// TODO: pass in a boolean to limit these possible overwrites to known
// (pre-BIP34) cases.
void AddCoins(CCoinsViewCache &cache, const CTransaction &tx, int32_t nHeight, int32_t genesisActivationHeight,
              bool check = false);

#endif // BITCOIN_COINS_H
