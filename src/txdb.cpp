// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "txdb.h"

#include "block_file_info.h"
#include "chainparams.h"
#include "config.h"
#include "disk_block_index.h"
#include "disk_tx_pos.h"
#include "hash.h"
#include "init.h"
#include "pow.h"
#include "random.h"
#include "uint256.h"
#include "util.h"
#include "ui_interface.h"
#include <boost/thread.hpp>
#include <string>
#include <vector>

static const char DB_COIN = 'C';
static const char DB_COINS = 'c';
static const char DB_BLOCK_FILES = 'f';
static const char DB_TXINDEX = 't';
static const char DB_BLOCK_INDEX = 'b';

static const char DB_BEST_BLOCK = 'B';
static const char DB_HEAD_BLOCKS = 'H';
static const char DB_FLAG = 'F';
static const char DB_REINDEX_FLAG = 'R';
static const char DB_LAST_BLOCK = 'l';

namespace {

struct CoinEntry {
    COutPoint *outpoint;
    char key;
    CoinEntry(const COutPoint *ptr)
        : outpoint(const_cast<COutPoint *>(ptr)), key(DB_COIN) {}

    template <typename Stream> void Serialize(Stream &s) const {
        s << key;
        s << outpoint->GetTxId();
        s << VARINT(outpoint->GetN());
    }

    template <typename Stream> void Unserialize(Stream &s) {
        s >> key;
        uint256 id;
        s >> id;
        uint32_t n = 0;
        s >> VARINT(n);
        *outpoint = COutPoint(id, n);
    }
};
} // namespace

namespace {

/**
 * Custom stream class that only unserializes the script if it is not larger than maxScriptSize.
 *
 * If the script is larger, it is not unserialized and the actualScriptSize is set to the actual size of script.
 * Otherwise value of actualScriptSize is left as is (i.e. empty).
 *
 * After unserialization, caller can use member actualScriptSIze to check if script was actually unserialized
 * and determine actual length of the script.
 *
 * This roundabout way of unserializing script and template trickery is needed to preserve compatibility with other
 * Unserialize() functions that unconditionally unserialize everything.
 *
 * @note This only works correctly when unserializing classes with at most one script because actual size of only
 *       one script can be provided. It is enforced (assert) within implementation of Unserialize() method.
 *
 * @see CScriptCompressor::NonSpecialScriptUnserializer<CDataStreamInput_NoScr> that provides actual Unserialize() method.
 */
template<class TBase>
struct CDataStreamInput_NoScr : TBase
{
    using Base = TBase;

    CDataStreamInput_NoScr(std::string_view buf, const std::vector<uint8_t>& key, std::size_t maxScriptSize, std::optional<std::size_t>& actualScriptSize)
    : Base(buf, key)
    , maxScriptSize(maxScriptSize)
    , actualScriptSize(actualScriptSize)
    , wasUnserializeScriptCalled(false)
    {}

    template<typename T>
    CDataStreamInput_NoScr& operator>>(T& obj)
    {
        ::Unserialize(*this, obj);
        return *this;
    }

    const std::size_t maxScriptSize;
    std::optional<std::size_t>& actualScriptSize;
    bool wasUnserializeScriptCalled;
};

} // anonymous namespace

/**
 * Implements unserialization of locking script only if it is not larger than maximum size specified
 * in CDataStreamInput_NoScr object.
 */
template<class TBase>
class CScriptCompressor::NonSpecialScriptUnserializer< CDataStreamInput_NoScr<TBase> >
{
public:
    using Stream = CDataStreamInput_NoScr<TBase>;

    static void Unserialize(CScriptCompressor* self, Stream& s, unsigned int nSize)
    {
        assert([](auto& s){
            bool result = !s.wasUnserializeScriptCalled;
            s.wasUnserializeScriptCalled = true;
            return result;
        }(s)); // Cannot unserialize more than one script using only one CDataStreamInput_NoScr object!
               // NOTE: Lambda is used because we want to remember within assert expression that Unserialize was called since
               //       this flag is only needed by assert expression. I.e.: No assert, no need to change the flag.
        if(nSize > s.maxScriptSize)
        {
            // Default initialize script if it is too large
            self->script = CScript();

            // Provide actual script size to the caller
            s.actualScriptSize = nSize;

            return;
        }

        // Unserialize script using Base of our custom stream class
        NonSpecialScriptUnserializer<typename Stream::Base>::Unserialize(self, s, nSize);
    }
};

std::optional<CoinImpl> CoinsDB::DBGetCoin(const COutPoint &outpoint, uint64_t maxScriptSize) const {
    try
    {
        std::optional<CoinImpl> coin{CoinImpl{}};
        // If script is not unserialized, this will be set to the actual size of the script.
        // Otherwise (i.e. if script is unserialized), value will remain unset.
        std::optional<std::size_t> actualScriptSize;
        bool res = db.Read<CDataStreamInput_NoScr>(CoinEntry(&outpoint), coin.value(), maxScriptSize, actualScriptSize);
        if( res )
        {
            if(actualScriptSize.has_value())
            {
                // Script was not unserialized
                return {
                    CoinImpl{
                        coin->GetTxOut().nValue,
                        *actualScriptSize,
                        coin->GetHeight(),
                        coin->IsCoinBase(),
                        coin->IsConfiscation()}};
            }

            return coin;
        }

        return {};
    } catch (const std::runtime_error &e) {
        uiInterface.ThreadSafeMessageBox(
            _("Error reading from database, shutting down."), "",
            CClientUIInterface::MSG_ERROR);
        LogPrintf("Error reading from database: %s\n", e.what());
        // Starting the shutdown sequence and returning false to the caller
        // would be interpreted as 'entry not found' (as opposed to unable
        // to read data), and could lead to invalid interpretation. Just
        // exit immediately, as we can't continue anyway, and all writes
        // should be atomic.
        abort();
    }
}

uint256 CoinsDB::DBGetBestBlock() const {
    uint256 hashBestChain;
    if (!db.Read(DB_BEST_BLOCK, hashBestChain)) return uint256();
    return hashBestChain;
}

std::vector<uint256> CoinsDB::GetHeadBlocks() const {
    std::vector<uint256> vhashHeadBlocks;
    if (!db.Read(DB_HEAD_BLOCKS, vhashHeadBlocks)) {
        return std::vector<uint256>();
    }
    return vhashHeadBlocks;
}

bool CoinsDB::DBBatchWrite(CCoinsMap &mapCoins, const uint256 &hashBlock) {
    CDBBatch batch(db);
    size_t count = 0;
    size_t changed = 0;
    size_t batch_size =
        (size_t)gArgs.GetArgAsBytes("-dbbatchsize", nDefaultDbBatchSize);
    int crash_simulate = gArgs.GetArg("-dbcrashratio", 0);
    assert(!hashBlock.IsNull());

    uint256 old_tip = DBGetBestBlock();
    if (old_tip.IsNull()) {
        // We may be in the middle of replaying.
        std::vector<uint256> old_heads = GetHeadBlocks();
        if (old_heads.size() == 2) {
            assert(old_heads[0] == hashBlock);
            old_tip = old_heads[1];
        }
    }

    // In the first batch, mark the database as being in the middle of a
    // transition from old_tip to hashBlock.
    // A vector is used for future extensibility, as we may want to support
    // interrupting after partial writes from multiple independent reorgs.
    batch.Erase(DB_BEST_BLOCK);
    batch.Write(DB_HEAD_BLOCKS, std::vector<uint256>{hashBlock, old_tip});

    for (CCoinsMap::iterator it = mapCoins.begin(); it != mapCoins.end();) {
        if (it->second.flags & CCoinsCacheEntry::DIRTY) {
            CoinEntry entry(&it->first);
            if (it->second.GetCoin().IsSpent()) {
                batch.Erase(entry);
            } else {
                auto coinWithScript = it->second.GetCoinWithScript();

                // coin entries that have DIRTY flag set and are not spent must
                // always contain the script
                assert(coinWithScript.has_value());

                batch.Write(entry, coinWithScript.value());
            }
            changed++;
        }
        count++;
        CCoinsMap::iterator itOld = it++;
        mapCoins.erase(itOld);
        if (batch.SizeEstimate() > batch_size) {
            LogPrint(BCLog::COINDB, "Writing partial batch of %.2f MiB\n",
                     batch.SizeEstimate() * (1.0 / 1048576.0));
            db.WriteBatch(batch);
            batch.Clear();
            if (crash_simulate) {
                static FastRandomContext rng;
                static int64_t crash_notbefore = gArgs.GetArg("-dbcrashnotbefore", 0);
                if (rng.randrange(crash_simulate) == 0 && GetSystemTimeInSeconds() > crash_notbefore) {
                    LogPrintf("Simulating a crash. Goodbye.\n");
                    _Exit(0);
                }
            }
        }
    }

    // In the last batch, mark the database as consistent with hashBlock again.
    batch.Erase(DB_HEAD_BLOCKS);
    batch.Write(DB_BEST_BLOCK, hashBlock);

    LogPrint(BCLog::COINDB, "Writing final batch of %.2f MiB\n",
             batch.SizeEstimate() * (1.0 / 1048576.0));
    bool ret = db.WriteBatch(batch);
    LogPrint(BCLog::COINDB, "Committed %u changed transaction outputs (out of "
                            "%u) to coin database...\n",
             (unsigned int)changed, (unsigned int)count);
    return ret;
}

size_t CoinsDB::EstimateSize() const {
    return db.EstimateSize(DB_COIN, char(DB_COIN + 1));
}

CBlockTreeDB::CBlockTreeDB(size_t nCacheSize, bool fMemory, bool fWipe)
    : CDBWrapper(GetDataDir() / "blocks" / "index", nCacheSize, fMemory,
                 fWipe) {}

bool CBlockTreeDB::ReadBlockFileInfo(int nFile, CBlockFileInfo &info) {
    return Read(std::make_pair(DB_BLOCK_FILES, nFile), info);
}

bool CBlockTreeDB::WriteReindexing(bool fReindexing) {
    if (fReindexing)
        return Write(DB_REINDEX_FLAG, '1');
    else
        return Erase(DB_REINDEX_FLAG);
}

bool CBlockTreeDB::ReadReindexing(bool &fReindexing) {
    fReindexing = Exists(DB_REINDEX_FLAG);
    return true;
}

bool CBlockTreeDB::ReadLastBlockFile(int &nFile) {
    return Read(DB_LAST_BLOCK, nFile);
}

CCoinsViewDBCursor *CoinsDB::Cursor() const {
    CCoinsViewDBCursor *i = new CCoinsViewDBCursor(
        const_cast<CDBWrapper &>(db).NewIterator(), GetBestBlock());
    /**
     * It seems that there are no "const iterators" for LevelDB. Since we only
     * need read operations on it, use a const-cast to get around that
     * restriction.
     */
    i->pcursor->Seek(DB_COIN);
    // Cache key of first record
    if (i->pcursor->Valid()) {
        CoinEntry entry(&i->keyTmp.second);
        i->pcursor->GetKey(entry);
        i->keyTmp.first = entry.key;
    } else {
        // Make sure Valid() and GetKey() return false
        i->keyTmp.first = 0;
    }
    return i;
}

// Same as CCoinsViewCursor::Cursor() with added Seek() to key txId
CCoinsViewDBCursor* CoinsDB::Cursor(const TxId &txId) const {
    CCoinsViewDBCursor* i = new CCoinsViewDBCursor(
        const_cast<CDBWrapper&>(db).NewIterator(), GetBestBlock());
    
    COutPoint op = COutPoint(txId, 0);
    CoinEntry key = CoinEntry(&op);

    i->pcursor->Seek(key);

    // Cache key of first record
    if (i->pcursor->Valid()) {
        CoinEntry entry(&i->keyTmp.second);
        i->pcursor->GetKey(entry);
        i->keyTmp.first = entry.key;
    }
    else {
        // Make sure Valid() and GetKey() return false
        i->keyTmp.first = 0;
    }
    return i;
}

bool CCoinsViewDBCursor::GetKey(COutPoint &key) const {
    // Return cached key
    if (keyTmp.first == DB_COIN) {
        key = keyTmp.second;
        return true;
    }
    return false;
}

std::optional<CoinImpl> CCoinsViewDBCursor::GetCoin(uint64_t maxScriptSize) const {
    std::optional<CoinImpl> coin{ CoinImpl{} };
    // If script is not unserialized, this will be set to the actual size of the script.
    // Otherwise (i.e. if script is unserialized), value will remain unset.
    std::optional<std::size_t> actualScriptSize;
    bool res = pcursor->GetValue<CDataStreamInput_NoScr>(coin.value(), maxScriptSize, actualScriptSize);
    if( res )
    {
        if(actualScriptSize.has_value())
        {
            // Script was not unserialized
            return {
                CoinImpl{
                    coin->GetTxOut().nValue,
                    *actualScriptSize,
                    coin->GetHeight(),
                    coin->IsCoinBase(),
                    coin->IsConfiscation()}};
        }

        return coin;
    }

    return {};
}

bool CCoinsViewDBCursor::GetValue(Coin &coin) const {
    if(auto tmp = GetCoin(0); tmp.has_value())
    {
        coin = Coin{tmp.value()};

        return true;
    }

    return false;
}

bool CCoinsViewDBCursor::GetValue(CoinWithScript &coin) const {
    if(auto tmp = GetCoin(std::numeric_limits<uint64_t>::max()); tmp.has_value())
    {
        coin = std::move(tmp.value());

        return true;
    }

    return false;
}

bool CCoinsViewDBCursor::Valid() const {
    return keyTmp.first == DB_COIN;
}

void CCoinsViewDBCursor::Next() {
    pcursor->Next();
    CoinEntry entry(&keyTmp.second);
    if (!pcursor->Valid() || !pcursor->GetKey(entry)) {
        // Invalidate cached key after last record so that Valid() and GetKey()
        // return false
        keyTmp.first = 0;
    } else {
        keyTmp.first = entry.key;
    }
}

bool CBlockTreeDB::WriteBatchSync(
    const std::vector<std::pair<int, const CBlockFileInfo *>> &fileInfo,
    int nLastFile, const std::vector<const CBlockIndex *> &blockinfo) {
    CDBBatch batch(*this);
    for (std::vector<std::pair<int, const CBlockFileInfo *>>::const_iterator
             it = fileInfo.begin();
         it != fileInfo.end(); it++) {
        batch.Write(std::make_pair(DB_BLOCK_FILES, it->first), *it->second);
    }
    batch.Write(DB_LAST_BLOCK, nLastFile);
    for (std::vector<const CBlockIndex *>::const_iterator it =
             blockinfo.begin();
         it != blockinfo.end(); it++) {
        batch.Write(std::make_pair(DB_BLOCK_INDEX, (*it)->GetBlockHash()),
                    CDiskBlockIndex(const_cast<CBlockIndex&>(**it)));
    }
    return WriteBatch(batch, true);
}

bool CBlockTreeDB::ReadTxIndex(const uint256 &txid, CDiskTxPos &pos) {
    return Read(std::make_pair(DB_TXINDEX, txid), pos);
}

bool CBlockTreeDB::WriteTxIndex(
    const std::vector<std::pair<uint256, CDiskTxPos>> &vect) {
    CDBBatch batch(*this);
    for (std::vector<std::pair<uint256, CDiskTxPos>>::const_iterator it =
             vect.begin();
         it != vect.end(); it++)
        batch.Write(std::make_pair(DB_TXINDEX, it->first), it->second);
    return WriteBatch(batch);
}

bool CBlockTreeDB::WriteFlag(const std::string &name, bool fValue) {
    return Write(std::make_pair(DB_FLAG, name), fValue ? '1' : '0');
}

bool CBlockTreeDB::ReadFlag(const std::string &name, bool &fValue) {
    char ch;
    if (!Read(std::make_pair(DB_FLAG, name), ch)) return false;
    fValue = ch == '1';
    return true;
}

std::unique_ptr<CDBIterator> CBlockTreeDB::GetIterator()
{
    return std::unique_ptr<CDBIterator>{ NewIterator() };
}

bool CoinsDB::IsOldDBFormat()
{
    std::unique_ptr<CDBIterator> pcursor(db.NewIterator());
    pcursor->Seek(std::make_pair(DB_COINS, uint256()));
    if (!pcursor->Valid())
    {
        return false;
    }
    return true;
}

CoinsDB::CoinsDB(
        uint64_t cacheSizeThreshold,
        size_t nCacheSize,
        CDBWrapper::MaxFiles maxFiles,
        bool fMemory,
        bool fWipe)
    : db{ GetDataDir() / "chainstate", nCacheSize, fMemory, fWipe, true, maxFiles }
    , mCacheSizeThreshold{cacheSizeThreshold}
{}

size_t CoinsDB::DynamicMemoryUsage() const {
    std::shared_lock lock { mCoinsViewCacheMtx };
    return mCache.DynamicMemoryUsage();
}

void CoinsDB::DBCacheAllInputs(const std::vector<CTransactionRef>& txns) const
{
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
    // FIXME: Consider using parallel sort when it beconmes available
    // NOTE: parallel sorting requires -ltbb in few GCC versions
    std::sort(allInputs.begin(), allInputs.end(), Sort);

    for(const auto& outpoint : allInputs)
    {
        GetCoin(outpoint, std::numeric_limits<uint64_t>::max());
    }
}

std::optional<CoinImpl> CoinsDB::GetCoin(const COutPoint &outpoint, uint64_t maxScriptSize) const {
    std::optional<CoinImpl> coinFromCache;
    size_t maxScriptLoadingSize = 0;

    // Scope guard must protect the scope until the end of the cache insertion
    // below the infinite while loop if it is instantiated inside that loop!
    std::optional<FetchingCoins::ScopeGuard> fetchCoinsGuard;

    while(true)
    {
        {
            std::shared_lock lock { mCoinsViewCacheMtx };

            coinFromCache = mCache.FetchCoin(outpoint);

            if (coinFromCache.has_value())
            {
                if (coinFromCache->IsSpent())
                {
                    return {};
                }
                else if (coinFromCache->HasScript())
                {
                    return coinFromCache;
                }
                else if(maxScriptSize < coinFromCache->GetScriptSize())
                {
                    // make a copy since we will swap the cached value on script load
                    // and we want the child view to re-request the coin from us
                    // at that point to preserve thread safety
                    return
                        CoinImpl{
                            coinFromCache->GetTxOut().nValue,
                            coinFromCache->GetScriptSize(),
                            coinFromCache->GetHeight(),
                            coinFromCache->IsCoinBase(),
                            coinFromCache->IsConfiscation()};
                }
            }

            // Scoped guard prevents race to mCoinsViewCacheMtx unique lock as reader threads
            // shouldn't get to it before the writer thread has the chance to write it into
            // the cache (the first thread that manages the insert)
            // We have to grab it under mCoinsViewCacheMtx being locked since
            // we need the guarantee that TryInsert won't accidentally cause
            // a race condition that would go to the database more than once
            // so the flow is:
            // T1: shared locks mCoinsViewCacheMtx
            // T1: doesn't find the coin and grabs the fetch guard
            // T2: shared locks mCoinsViewCacheMtx
            // T2: doesn't find the coin and can't obtain fetch guard
            // T1: releases shared lock of mCoinsViewCacheMtx
            // T2: still can't obtain fetch guard
            // T1: obtains exclusive lock of mCoinsViewCacheMtx after all
            //     other threads left this shared lock scope
            // T1: writes to cache or fails to fetch coin
            // T1: releases exclusive lock of mCoinsViewCacheMtx
            // T2: shared locks mCoinsViewCacheMtx
            // T2: is guaranteed to either get the coin (if it exists) from the
            //     cache above or obtain fetch coins guard and re-try DB fetch
            //     of coin that is guaranteed not to be in the cache
            fetchCoinsGuard = mFetchingCoins.TryInsert(outpoint);
            if (fetchCoinsGuard.has_value())
            {
                // it can happen that we'll get multiple requests and unnecessarily
                // load more scripts than needed but that should be rare enough
                maxScriptLoadingSize = getMaxScriptLoadingSize(maxScriptSize);
                break;
            }
        }

        // All but the first reader will end up here. Give the initial thread a
        // chance to load the coin before re-attempting to access it.
        //
        // The code would get here extremely rarely during parallel block
        // validation and almost simultaneous request of the same coin.
        //
        // The other, more likely scenario is chain validation when we validate
        // dependant transactions in parallel. Validation will look for the
        // transaction outputs of the ancestor transaction, that we will
        // normally not find, and in parallel in a separate task validation of
        // the dependant transaction will load inputs that we will _also_ not
        // find.
        //
        // Sleeping as little time as possible speeds up the more common
        // not-found case.
        //
        std::this_thread::yield();
    }

    // Only one thread can reach this point for each distinct outpoint – this
    // will perform a read from the backing view and remove the outpoint from
    // mFetchcingCoins when local variable “guard” goes out of scope so that
    // the rare potential other threads that are waiting for the same outpoint
    // may continue.

    auto coinFromView = DBGetCoin(outpoint, maxScriptLoadingSize);
    if (!coinFromView.has_value())
    {
        return {};
    }

    std::unique_lock lock { mCoinsViewCacheMtx };

    if (coinFromCache.has_value())
    {
        assert(coinFromView->HasScript());

        if (hasSpaceForScript(coinFromView.value().GetScriptSize()))
        {
            return mCache.ReplaceWithCoinWithScript(outpoint, std::move(coinFromView.value())).MakeNonOwning();
        }

        return coinFromView;
    }

    if (!hasSpaceForScript(coinFromView->GetScriptSize()))
    {
        mCache.AddCoin(
            outpoint,
            CoinImpl{
                coinFromView->GetTxOut().nValue,
                coinFromView->GetScriptSize(),
                coinFromView->GetHeight(),
                coinFromView->IsCoinBase(),
                coinFromView->IsConfiscation()});

        return coinFromView;
    }

    auto& cws = mCache.AddCoin(outpoint, std::move(coinFromView.value()));
    assert(cws.IsStorageOwner());

    return cws.MakeNonOwning();
}

bool CoinsDB::HaveCoinInCache(const COutPoint &outpoint) const {
    std::shared_lock lock { mCoinsViewCacheMtx };
    return mCache.FetchCoin(outpoint).has_value();
}

uint256 CoinsDB::GetBestBlock() const {
    std::shared_lock lock { mCoinsViewCacheMtx };
    if (hashBlock.IsNull()) {
        hashBlock = DBGetBestBlock();
    }
    return hashBlock;
}

bool CoinsDB::BatchWrite(
    const WPUSMutex::Lock& writeLock,
    const uint256& hashBlockIn,
    CCoinsMap&& mapCoins)
{
    assert( writeLock.GetLockType() == WPUSMutex::Lock::Type::write );
    std::unique_lock lock { mCoinsViewCacheMtx };

    if(hashBlockIn.IsNull())
    {
        assert(mapCoins.empty());
    }
    else
    {
        mCache.BatchWrite(mapCoins);
        hashBlock = hashBlockIn;
    }
    return true;
}

bool CoinsDB::Flush()
{
    WPUSMutex::Lock writeLock = mMutex.WriteLock();
    std::unique_lock lock { mCoinsViewCacheMtx };

    if(hashBlock.IsNull())
    {
        // nothing new was added
        return true;
    }

    auto coins = mCache.MoveOutCoins();

    return DBBatchWrite(coins, hashBlock);
}

void CoinsDB::Uncache(const std::vector<COutPoint>& vOutpoints)
{
    WPUSMutex::Lock writeLock = mMutex.WriteLock();
    std::unique_lock lock { mCoinsViewCacheMtx };
    mCache.Uncache(vOutpoints);
}

unsigned int CoinsDB::GetCacheSize() const {
    std::shared_lock lock { mCoinsViewCacheMtx };
    return mCache.CachedCoinsCount();
}

std::optional<Coin> CoinsDB::GetCoinByTxId(const TxId& txid) const
{
    constexpr int MAX_VIEW_ITERATIONS = 100;

    // wtih MAX_VIEW_ITERATIONS we are avoiding for loop to MAX_OUTPUTS_PER_TX (in millions after genesis)
    // performance testing indicates that after 100 look up by cursor becomes faster

    for (int n = 0; n < MAX_VIEW_ITERATIONS; n++) {
        auto alternate = GetCoin(COutPoint(txid, n), 0);
        if (alternate.has_value()) {
            return Coin{alternate.value()};
        }
    }

    // for large output indexes delegate search to db cursor/iterator by key prefix (txId)

    COutPoint key;
    std::optional<Coin> coin{ Coin{} };

    std::unique_ptr<CCoinsViewDBCursor> cursor{ Cursor(txid) };

    if (cursor->Valid())
    {
        cursor->GetKey(key);
    }
    while (cursor->Valid() && key.GetTxId() == txid)
    {
        if (!cursor->GetValue(coin.value()))
        {
            return {};
        }
        assert(!coin->IsSpent());
        return coin;
    }
    return {};
}

auto CoinsDBSpan::TryFlush() -> WriteState
{
    assert(mThreadId == std::this_thread::get_id());
    assert(mShards.size() == 1);

    if (!mDB.TryWriteLock( mView.mLock ))
    {
        return WriteState::invalidated;
    }

    auto revertToReadLock =
        [this](void*)
        {
             mDB.ReadLock( mView.mLock );
        };
    std::unique_ptr<WPUSMutex::Lock, decltype(revertToReadLock)> guard{&mView.mLock, revertToReadLock};

    return
        mDB.BatchWrite(mView.mLock, GetBestBlock(), mShards[0].GetCache().MoveOutCoins())
        ? WriteState::ok
        : WriteState::error;
}
