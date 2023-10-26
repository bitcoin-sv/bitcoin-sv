// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_TXDB_H
#define BITCOIN_TXDB_H

#include "chain.h"
#include "coins.h"
#include "dbwrapper.h"
#include "write_preferring_upgradable_mutex.h"

#include <map>
#include <shared_mutex>
#include <string>
#include <utility>
#include <vector>
#include <optional>

#include "boost/thread/shared_mutex.hpp"

class CBlockFileInfo;
class CBlockIndex;
struct CDiskTxPos;
class uint256;

//! No need to periodic flush if at least this much space still available.
static constexpr int MAX_BLOCK_COINSDB_USAGE = 10;
//! -dbcache default (MiB)
static const int64_t nDefaultDbCache = 450;
//! -dbbatchsize default (bytes)
static const int64_t nDefaultDbBatchSize = 16 << 20;
//! max. -dbcache (MiB)
static const int64_t nMaxDbCache = sizeof(void *) > 4 ? 16384 : 1024;
//! min. -dbcache (MiB)
static const int64_t nMinDbCache = 4;
//! Max memory allocated to block tree DB specific cache, if no -txindex (MiB)
static const int64_t nMaxBlockDBCache = 2;
//! Max memory allocated to block tree DB specific cache, if -txindex (MiB)
// Unlike for the UTXO database, for the txindex scenario the leveldb cache make
// a meaningful difference:
// https://github.com/bitcoin/bitcoin/pull/8273#issuecomment-229601991
static const int64_t nMaxBlockDBAndTxIndexCache = 1024;
//! Max memory allocated to coin DB specific cache (MiB)
static const int64_t nMaxCoinsDBCache = 8;

/** Iterate over coins in DB */
class CCoinsViewDBCursor {
public:
    ~CCoinsViewDBCursor() {}

    bool GetKey(COutPoint &key) const;
    bool GetValue(Coin &coin) const;
    bool GetValue(CoinWithScript &coin) const;

    bool Valid() const;
    void Next();

    //! Get best block at the time this cursor was created
    const uint256 &GetBestBlock() const { return hashBlock; }

private:
    CCoinsViewDBCursor(CDBIterator *pcursorIn, const uint256 &hashBlockIn)
        : hashBlock(hashBlockIn), pcursor(pcursorIn) {}
    std::optional<CoinImpl> GetCoin(uint64_t maxScriptSize) const;
    uint256 hashBlock;
    std::unique_ptr<CDBIterator> pcursor;
    std::pair<char, COutPoint> keyTmp;

    friend class CoinsDB;
};

/**
 * CCoinsProvider backed by the coin database (chainstate/)
 * and adds a memory cache of de-serialized coins.
 *
 * Provider is intended to be used from multiple threads but only one of the
 * threads is allowed to write to it - the rest of the threads must release their
 * read locks and try again later.
 *
 * Cache is limited for loading new coins (they can still be flushed from child
 * providers down without caring for the threshold limit) so once the cache is
 * full only coins without script are stored in it while coins with script are
 * re-requested from base on every call to GetCoin() that requires a script.
 */
class CoinsDB {
private:
    friend class CoinsDBView;
    friend class CoinsDBSpan;

    /**
     * Make mutable so that we can "fill the cache" even from Get-methods
     * declared as "const".
     */
    mutable uint256 hashBlock;
    mutable CoinsStore mCache;

public:
    template<typename T> struct UnitTestAccess;

    using MaxFiles = CDBWrapper::MaxFiles;

    /**
     * @param[in] cacheSizeThreshold
     *                        Maximum amount of coins that can be stored in
     *                        cache after being loaded from the database.
     *                        Added coins and coins without scripts do not count
     *                        to this limit and may exceed it.
     * @param[in] nCacheSize  Underlying database cache size
     * @param[in] fMemory     If true, use leveldb's memory environment.
     * @param[in] fWipe       If true, remove all existing data.
     */
    CoinsDB(
        uint64_t cacheSizeThreshold,
        size_t nCacheSize,
        MaxFiles maxFiles,
        bool fMemory = false,
        bool fWipe = false);

    CoinsDB(const CoinsDB&) = delete;
    CoinsDB& operator=(const CoinsDB&) = delete;
    CoinsDB(CoinsDB&&) = delete;
    CoinsDB& operator=(CoinsDB&&) = delete;

    /**
     * Check if we have the given utxo already loaded in this cache.
     */
    bool HaveCoinInCache(const COutPoint &outpoint) const;

    //! Calculate the size of the cache (in number of transaction outputs)
    unsigned int GetCacheSize() const;

    //! Calculate the size of the cache (in bytes)
    size_t DynamicMemoryUsage() const;

    //! Returns true if database is in an older format.
    bool IsOldDBFormat();

    CCoinsViewDBCursor* Cursor() const;

    size_t EstimateSize() const;

    /**
     * Push the modifications applied to this cache to its base.
     * Failure to call this method before destruction will cause the changes to
     * be forgotten. If false is returned, the state of this cache (and its
     * backing view) will be undefined.
     */
    bool Flush();

    /**
     * Removes UTXOs with the given outpoints from the cache.
     */
    void Uncache(const std::vector<COutPoint>& vOutpoints);

private:
    uint256 GetBestBlock() const;

    //! Do a bulk modification (multiple Coin changes + BestBlock change).
    //! The passed mapCoins can be modified.
    bool BatchWrite(
        const WPUSMutex::Lock& writeLock,
        const uint256& hashBlock,
        CCoinsMap&& mapCoins);

    //! Get a cursor to iterate over coins by txId. Cursor is positioned at the first key in the source that is at or past target.
    //! If coin with txId is not found then cursor is at position at first record after txId - source is sorted by txId
    CCoinsViewDBCursor* Cursor(const TxId &txId) const;

    //! Get any unspent output with a given txid.
    std::optional<Coin> GetCoinByTxId(const TxId &txid) const;

    void ReadLock(WPUSMutex::Lock& lockHandle) const
    {
        mMutex.ReadLock( lockHandle );
    }

    bool TryWriteLock(WPUSMutex::Lock& lockHandle)
    {
        return mMutex.TryWriteLock( lockHandle );
    }

    std::optional<CoinImpl> GetCoin(const COutPoint &outpoint, uint64_t maxScriptSize) const;
    std::optional<CoinImpl> DBGetCoin(const COutPoint &outpoint, uint64_t maxScriptSize) const;
    uint256 DBGetBestBlock() const;
    std::vector<uint256> GetHeadBlocks() const;
    bool DBBatchWrite(CCoinsMap &mapCoins, const uint256 &hashBlock);

    // Read all inputs from the DB and cache
    void DBCacheAllInputs(const std::vector<CTransactionRef>& txns) const;

    /**
     * A mutex that guarantees that coins from cache will not be removed and
     * more importantly loaded coin scripts will not be removed until all read
     * locks of this mutex are released and a write lock is held.
     */
    mutable WPUSMutex mMutex;

    CDBWrapper db;

    /**
     * Return the larger script loading size - either the requested size or the
     * remaining size of the remaining available cache of current class instance.
     */
    uint64_t getMaxScriptLoadingSize(uint64_t requestedMaxScriptSize) const
    {
        if(mCacheSizeThreshold > mCache.DynamicMemoryUsage())
        {
            return std::max(requestedMaxScriptSize, mCacheSizeThreshold - mCache.DynamicMemoryUsage());
        }

        return requestedMaxScriptSize;
    }

    //! Returns whether we still have space to store a script of certain size
    bool hasSpaceForScript(uint64_t scriptSize) const
    {
        return mCacheSizeThreshold >= (mCache.DynamicMemoryUsage() + scriptSize);
    }

    uint64_t mCacheSizeThreshold;

    // A mutex to support a thread safe access to cache.
    // boost::shared_mutex used rather than std::shared_mutex as it implements a
    // completely fair locking algorithm to guarantee against reader or writer 
    // starvation.
    mutable boost::shared_mutex mCoinsViewCacheMtx {};

    /**
     * Contains outpoints that are currently being loaded from base view by
     * FetchCoin(). This prevents simultaneous loads of the same coin by
     * multiple threads and enables us not to hold the locks while loading from
     * base view, which can be slow if it is backed by disk.
     */
    class FetchingCoins
    {
    public:
        class ScopeGuard
        {
        public:
            ScopeGuard( FetchingCoins& fc, std::set<COutPoint>::iterator it ) noexcept
                : mFc{ &fc }
                , mIt{ it }
            {}
            ~ScopeGuard() noexcept
            {
                if(mFc)
                {
                    std::lock_guard lock{ mFc->mMutex };
                    mFc->mCoins.erase( mIt );
                }
            }

            ScopeGuard(const ScopeGuard&) = delete;
            ScopeGuard& operator=(const ScopeGuard&) = delete;
            ScopeGuard(ScopeGuard&&) = default;
            ScopeGuard& operator=(ScopeGuard&&) = default;

        private:
            // helper to get default move construction/assignment in this class
            struct NullDeleter{void operator()(FetchingCoins*){}};

            // unique_ptr is used instead of std::optional<ref_wrapper> because
            // move from ref_wrapper actually makes a copy so we'd still need to
            // write custom move constructor/assignment in such case which is
            // error prone and thus we try to avoid it
            std::unique_ptr<FetchingCoins, NullDeleter> mFc;
            std::set<COutPoint>::iterator mIt;
        };

        std::optional<ScopeGuard> TryInsert(const COutPoint& outpoint)
        {
            std::lock_guard lock{ mMutex };

            if (auto [it, success] = mCoins.insert(outpoint); success)
            {
                return std::optional<ScopeGuard>{std::in_place, *this, it};
            }

            return std::nullopt;
        }

    private:
        mutable std::mutex mMutex;
        mutable std::set<COutPoint> mCoins;
    };
    mutable FetchingCoins mFetchingCoins;
};

/**
 * View for read-only querying of coins providers.
 *
 * Class automatically obtains CoinsDB read lock on construction and
 * releases it on destruction.
 */
class CoinsDBView : public ICoinsView
{
public:
    friend class CoinsViewLockedMemPoolNL;
    friend class CCoinsViewMemPool;

    CoinsDBView(const CoinsDB& db)
        : mDB{db}
    {
        mDB.ReadLock( mLock );
    }

    void CacheAllCoins(const std::vector<CTransactionRef>& txns) const override
    {
        mDB.DBCacheAllInputs(txns);
    }

    // If found return basic coin info without script loaded
    std::optional<Coin> GetCoin(const COutPoint& outpoint) const
    {
        auto coinData = mDB.GetCoin(outpoint, 0);
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
        auto coinData = mDB.GetCoin(outpoint, std::numeric_limits<size_t>::max());
        if(coinData.has_value())
        {
            assert(coinData->HasScript());

            return std::move(coinData.value());
        }

        return {};
    }
    uint256 GetBestBlock() const override { return mDB.GetBestBlock(); }
    std::vector<uint256> GetHeadBlocks() const { return mDB.GetHeadBlocks(); }

    std::optional<Coin> GetCoinByTxId(const TxId &txid) const { return mDB.GetCoinByTxId(txid); }

private:
    // The caller of GetCoin needs to hold mempool.smtx.
    std::optional<CoinImpl> GetCoin(const COutPoint &outpoint, uint64_t maxScriptSize) const override
    {
        return mDB.GetCoin(outpoint, maxScriptSize);
    }

    const CoinsDB& mDB;

    // This variable enforces read only access to mDB
    WPUSMutex::Lock mLock;

    void ReleaseLock() override
    {
        mLock = {};
    }

    void ReLock() override
    {
        assert(mLock.GetLockType() == WPUSMutex::Lock::Type::unlocked);

        mDB.ReadLock( mLock );
    }

    friend class CoinsDBSpan;
};

/**
 * Same as CCoinsViewCache but with additional functionality for pushing the
 * changes to the underlying CoinsDB.
 *
 * It holds read lock and on TryFlush() it tries to obtain write lock.
 * * If it's the first instance that tries to obtain it it waits for all the read
 *   locks to be released before locking, flushing and re-obtaining read lock.
 * * If it's not the first instance that tries to obtain it, it immediately
 *   returns with holding a read lock and it is expected that the owner gracefully
 *   releases the instance and re-tries the task at a later point in time if needed.
 */
class CoinsDBSpan : public CCoinsViewCache
{
public:
    template<typename T> struct UnitTestAccess;

    enum class WriteState
    {
        ok,
        error,
        invalidated // span can no longer be used
    };

    explicit CoinsDBSpan(CoinsDB& db)
        // CCoinsViewCache constructor doesn't use mView (only stores its
        // address) so we can provide it in an uninitialized state.
        : CCoinsViewCache{ mView }
        , mDB{ db }
        , mView{ mDB }
    {}

    /**
     * Push the modifications applied to this cache to its base.
     * Failure to call this method before destruction will cause the changes to
     * be forgotten.
     *
     * Return:
     * * If WriteState::error is returned, the state of
     *   this cache (and its backing coins database) will be undefined.
     * * If WriteState::invalidated is returned, the span is expected to
     *   gracefully release the read lock otherwise a dead lock will occur.
     */
    WriteState TryFlush();

    std::vector<uint256> GetHeadBlocks() const
    {
        assert(mThreadId == std::this_thread::get_id());
        return mDB.GetHeadBlocks();
    }

private:
    CoinsDB& mDB;
    CoinsDBView mView;
};

/** Access to the block database (blocks/index/) */
class CBlockTreeDB : public CDBWrapper {
public:
    CBlockTreeDB(size_t nCacheSize, bool fMemory = false, bool fWipe = false);

private:
    CBlockTreeDB(const CBlockTreeDB &);
    void operator=(const CBlockTreeDB &);

public:
    bool WriteBatchSync(
        const std::vector<std::pair<int, const CBlockFileInfo *>> &fileInfo,
        int nLastFile, const std::vector<const CBlockIndex *> &blockinfo);
    bool ReadBlockFileInfo(int nFile, CBlockFileInfo &fileinfo);
    bool ReadLastBlockFile(int &nFile);
    bool WriteReindexing(bool fReindex);
    bool ReadReindexing(bool &fReindex);
    bool ReadTxIndex(const uint256 &txid, CDiskTxPos &pos);
    bool WriteTxIndex(const std::vector<std::pair<uint256, CDiskTxPos>> &list);
    bool WriteFlag(const std::string &name, bool fValue);
    bool ReadFlag(const std::string &name, bool &fValue);

    std::unique_ptr<CDBIterator> GetIterator();
};

#endif // BITCOIN_TXDB_H
