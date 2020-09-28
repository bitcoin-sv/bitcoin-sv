// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_TXDB_H
#define BITCOIN_TXDB_H

#include "chain.h"
#include "coins.h"
#include "dbwrapper.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

class CBlockIndex;
class CCoinsViewDBCursor;
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

struct CDiskTxPos : public CDiskBlockPos {
    uint64_t nTxOffset; // after header

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        READWRITE(*(CDiskBlockPos *)this);

        // Legacy 32 bit sizes used for reading and writing.
        // When writing size larger or equal than max 32 bit value,
        // max 32 bit value (0xFFFFFFFF) is written in 32 bit field
        // and actual size is written in separate 64 bit field.
        // When reading, separate 64 bit value should be read when 32 bit value
        // is max (0xFFFFFFFF).
        unsigned int offset =
              nTxOffset >= std::numeric_limits<unsigned int>::max()
            ? std::numeric_limits<unsigned int>::max()
            : static_cast<unsigned int>(nTxOffset);
        READWRITE(VARINT(offset));

        if (offset == std::numeric_limits<unsigned int>::max())
        {
            READWRITE(VARINT(nTxOffset));
        }
        else
        {
            nTxOffset = offset;
        }
    }

    CDiskTxPos(const CDiskBlockPos &blockIn, uint64_t nTxOffsetIn)
        : CDiskBlockPos(blockIn.nFile, blockIn.nPos), nTxOffset(nTxOffsetIn) {}

    CDiskTxPos() { SetNull(); }

    void SetNull() {
        CDiskBlockPos::SetNull();
        nTxOffset = 0;
    }
};

/** Specialization of CCoinsViewCursor to iterate over a CCoinsViewDB */
class CCoinsViewDBCursor : public CCoinsViewCursor {
public:
    ~CCoinsViewDBCursor() {}

    bool GetKey(COutPoint &key) const override;
    bool GetValue(Coin &coin) const override;
    unsigned int GetValueSize() const override;

    bool Valid() const override;
    void Next() override;

private:
    CCoinsViewDBCursor(CDBIterator *pcursorIn, const uint256 &hashBlockIn)
        : CCoinsViewCursor(hashBlockIn), pcursor(pcursorIn) {}
    std::unique_ptr<CDBIterator> pcursor;
    std::pair<char, COutPoint> keyTmp;

    friend class CoinsDB;
};

/**
 * CCoinsProvider backed by the coin database (chainstate/)
 * and adds a memory cache of de-serialized coins.
 *
 * Cache is limited for loading new coins (they can still be flushed from child
 * providers down without caring for the threshold limit) so once the cache is
 * full only coins without script are stored in it while coins with script are
 * re-requested from base on every call to GetCoin() that requires a script.
 */
class CoinsDB : public CCoinsView {
private:
    /**
     * Make mutable so that we can "fill the cache" even from Get-methods
     * declared as "const".
     */
    mutable uint256 hashBlock;
    mutable CoinsStore mCache;

public:
    template<typename T> struct UnitTestAccess;

    CoinsDB(
        size_t nCacheSize,
        bool fMemory = false,
        bool fWipe = false);

    CoinsDB(const CoinsDB&) = delete;
    CoinsDB& operator=(const CoinsDB&) = delete;
    CoinsDB(CoinsDB&&) = delete;
    CoinsDB& operator=(CoinsDB&&) = delete;

    /**
     * Check if we have the given utxo already loaded in this cache.
     * The semantics are the same as HaveCoin(), but no calls to the backing
     * CCoinsView are made.
     */
    bool HaveCoinInCache(const COutPoint &outpoint) const;

    //! Calculate the size of the cache (in number of transaction outputs)
    unsigned int GetCacheSize() const;

    size_t DynamicMemoryUsage() const;

    //! Returns true if database is in an older format.
    bool IsOldDBFormat();

    // Standard CCoinsView methods
    bool GetCoin(const COutPoint &outpoint, Coin &coin) const override;
    bool HaveCoin(const COutPoint &outpoint) const override;
    uint256 GetBestBlock() const override;
    bool BatchWrite(CCoinsMap& mapCoins, const uint256& hashBlock) override;

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

    //! Get any unspent output with a given txid.
    Coin GetCoinByTxId(const TxId &txid) const;

    std::vector<uint256> GetHeadBlocks() const override;
    CCoinsViewCursor *Cursor() const override;
    CCoinsViewCursor* Cursor(const TxId &txId) const override;
    size_t EstimateSize() const override;

private:
    std::optional<std::reference_wrapper<const Coin>> FetchCoin(const COutPoint &outpoint) const;
    bool DBGetCoin(const COutPoint &outpoint, Coin &coin) const;
    uint256 DBGetBestBlock() const;
    bool DBBatchWrite(CCoinsMap &mapCoins, const uint256 &hashBlock);

    CDBWrapper db;

    /* A mutex to support a thread safe access. */
    mutable std::mutex mCoinsViewCacheMtx {};

    /**
     * Contains outpoints that are currently being loaded from base view by
     * FetchCoin(). This prevents simultaneous loads of the same coin by
     * multiple threads and enables us not to hold the locks while loading from
     * base view, which can be slow if it is backed by disk.
     */
    mutable std::set<COutPoint> mFetchingCoins;
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
    bool LoadBlockIndexGuts(
        std::function<CBlockIndex *(const uint256 &)> insertBlockIndex);
};

#endif // BITCOIN_TXDB_H
