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
#include "uint256.h"

#include <cassert>
#include <cstdint>
#include <unordered_map>
#include <mutex>
#include <optional>
#include <thread>

/**
 * A UTXO entry.
 *
 * Serialized format:
 * - VARINT((coinbase ? 1 : 0) | (height << 1))
 * - the non-spent CTxOut (via CTxOutCompressor)
 */
class Coin {
    //! Unspent transaction output.
    CTxOut out;

    //! Whether containing transaction was a coinbase and height at which the
    //! transaction was included into a block.
    // This variable is unsigned even though height is signed type consistently in the codebase.
    // The reason is that shifting on negative numbers causes undefined behavior.
    uint32_t nHeightAndIsCoinBase;

public:
    //! Empty constructor
    Coin() : nHeightAndIsCoinBase(0) {}

    //! Constructor from a CTxOut and height/coinbase information.
    Coin(CTxOut outIn, int32_t nHeightIn, bool IsCoinbase)
        : out(std::move(outIn)),
          nHeightAndIsCoinBase((static_cast<uint32_t>(nHeightIn) << 1) | IsCoinbase) {}

    int32_t GetHeight() const {
        return static_cast<int32_t>(nHeightAndIsCoinBase >> 1);
    }
    bool IsCoinBase() const { return nHeightAndIsCoinBase & 0x01; }
    bool IsSpent() const { return out.IsNull(); }

    CTxOut &GetTxOut() { return out; }
    const CTxOut &GetTxOut() const { return out; }

    void Clear() {
        out.SetNull();
        nHeightAndIsCoinBase = 0;
    }

    template <typename Stream> void Serialize(Stream &s) const {
        assert(!IsSpent());
        ::Serialize(s, VARINT(nHeightAndIsCoinBase));
        ::Serialize(s, CTxOutCompressor(REF(out)));
    }

    template <typename Stream> void Unserialize(Stream &s) {
        ::Unserialize(s, VARINT(nHeightAndIsCoinBase));
        ::Unserialize(s, REF(CTxOutCompressor(out)));
    }

    size_t DynamicMemoryUsage() const {
        return memusage::DynamicUsage(out.scriptPubKey);
    }
};

class SaltedOutpointHasher {
private:
    /** Salt */
    const uint64_t k0, k1;

public:
    SaltedOutpointHasher();

    /**
     * This *must* return size_t. With Boost 1.46 on 32-bit systems the
     * unordered_map will behave unpredictably if the custom hasher returns a
     * uint64_t, resulting in failures when syncing the chain (#4634).
     * Note: This information above might be outdated as the unordered map
     * container type has meanwhile been switched to the C++ standard library
     * implementation.
     */
    size_t operator()(const COutPoint &outpoint) const {
        return SipHashUint256Extra(k0, k1, outpoint.GetTxId(), outpoint.GetN());
    }
};

class CCoinsCacheEntry {
    // The actual cached data.
    Coin coin;

public:
    uint8_t flags;

    enum Flags : uint8_t {
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
    CCoinsCacheEntry(Coin&& coinIn, uint8_t flagsIn)
        : coin(std::move(coinIn))
        , flags(flagsIn)
    {}

    const Coin& GetCoin() const {return coin;}
    Coin MoveCoin() {return std::move(coin);}
    void Clear() {coin.Clear();}
    size_t DynamicMemoryUsage() const {return coin.DynamicMemoryUsage();}
};

typedef std::unordered_map<COutPoint, CCoinsCacheEntry, SaltedOutpointHasher>
    CCoinsMap;

/** Cursor for iterating over CoinsView state */
class CCoinsViewCursor {
public:
    CCoinsViewCursor(const uint256 &hashBlockIn) : hashBlock(hashBlockIn) {}
    virtual ~CCoinsViewCursor() {}

    virtual bool GetKey(COutPoint &key) const = 0;
    virtual bool GetValue(Coin &coin) const = 0;
    virtual unsigned int GetValueSize() const = 0;

    virtual bool Valid() const = 0;
    virtual void Next() = 0;

    //! Get best block at the time this cursor was created
    const uint256 &GetBestBlock() const { return hashBlock; }

private:
    uint256 hashBlock;
};

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
    virtual bool GetCoin(const COutPoint &outpoint, Coin &coin) const = 0;

    //! Just check whether we have data for a given outpoint.
    //! This may (but cannot always) return true for spent outputs.
    virtual bool HaveCoin(const COutPoint &outpoint) const = 0;

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
    bool GetCoin(const COutPoint &outpoint, Coin &coin) const override { return false; }
    bool HaveCoin(const COutPoint &outpoint) const override { return false; }
    uint256 GetBestBlock() const override { return {}; }

    void ReleaseLock() override {}
    void ReLock() override {}
};

class CoinsStore
{
public:
    template<typename T> struct UnitTestAccess;

    size_t DynamicMemoryUsage() const;

    size_t CachedCoinsCount() const { return cacheCoins.size(); }

    std::optional<std::reference_wrapper<const Coin>> FetchCoin(const COutPoint& outpoint) const;

    CCoinsMap MoveOutCoins()
    {
        cachedCoinsUsage = 0;
        auto map = std::move(cacheCoins);
        cacheCoins.clear();

        return map;
    }

    const Coin& AddCoin(const COutPoint& outpoint, Coin&& coin);
    void AddCoin(
        const COutPoint& outpoint,
        Coin&& coin,
        bool possible_overwrite,
        uint64_t genesisActivationHeight);
    bool SpendCoin(const COutPoint& outpoint, Coin* moveout);
    void Uncache(const std::vector<COutPoint>& vOutpoints);
    void BatchWrite(CCoinsMap& mapCoins);

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

    /**
     * Return a reference to a Coin in the cache, or a pruned one if not found.
     * This is more efficient than GetCoin. Modifications to other cache entries
     * are allowed while accessing the returned pointer.
     */
    const Coin &AccessCoin(const COutPoint &output) const;

    /**
     * Add a coin. Set potential_overwrite to true if a non-pruned version may
     * already exist.
     * genesisActivationHeight parameter is used to check if Genesis upgrade rules
     * are in effect for this coin. It is required to correctly determine if coin is unspendable.
     */
    void AddCoin(const COutPoint &outpoint, Coin&& coin,
                 bool potential_overwrite, int32_t genesisActivationHeight);

    /**
     * Spend a coin. Pass moveto in order to get the deleted data.
     * If no unspent output exists for the passed outpoint, this call has no
     * effect.
     */
    bool SpendCoin(const COutPoint &outpoint, Coin *moveto = nullptr);

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
     * Return priority of tx at height nHeight. Also calculate the sum of the
     * values of the inputs that are already in the chain. These are the inputs
     * that will age and increase priority as new blocks are added to the chain.
     */
    double GetPriority(const CTransaction &tx, int32_t nHeight,
                       Amount &inChainInputValue) const;

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
    // A non-locking fetch coin
    std::optional<std::reference_wrapper<const Coin>> FetchCoin(const COutPoint &outpoint) const;

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
