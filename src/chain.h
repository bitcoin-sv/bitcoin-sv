// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CHAIN_H
#define BITCOIN_CHAIN_H

#include "arith_uint256.h"
#include "consensus/params.h"
#include "pow.h"
#include "primitives/block.h"
#include "tinyformat.h"
#include "uint256.h"
#include "logging.h"

#include <atomic>
#include <chrono>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

/**
 * Maximum amount of time that a block timestamp is allowed to exceed the
 * current network-adjusted time before the block will be accepted.
 */
static const int64_t MAX_FUTURE_BLOCK_TIME = 2 * 60 * 60;

/**
 * Timestamp window used as a grace period by code that compares external
 * timestamps (such as timestamps passed to RPCs, or wallet key creation times)
 * to block timestamps. This should be set at least as high as
 * MAX_FUTURE_BLOCK_TIME.
 */
static const int64_t TIMESTAMP_WINDOW = MAX_FUTURE_BLOCK_TIME;

class CBlockFileInfo {
public:
    //!< number of blocks stored in file
    unsigned int nBlocks;
    //!< lowest height of block in file
    unsigned int nHeightFirst;
    //!< highest height of block in file
    unsigned int nHeightLast;
    //!< earliest time of block in file
    uint64_t nTimeFirst;
    //!< latest time of block in file
    uint64_t nTimeLast;
    //!< number of used bytes of block file
    uint64_t nSize;
    //!< number of used bytes in the undo file
    uint64_t nUndoSize;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) 
    {
        // Legacy 32 bit sizes used for reading and writing. 
        // When writing size larger or equal than max 32 bit value, 
        // max 32 bit value (0xFFFFFFFF) is written in 32 bit field 
        // and actual size is written in separate 64 bit field. 
        // When reading, separate 64 bit value should be read when 32 bit value
        // is max (0xFFFFFFFF).
        unsigned int nSizeLegacy; 
        unsigned int nUndoSizeLegacy;
        if (nSize >= std::numeric_limits<uint32_t>::max())
        {
            nSizeLegacy = std::numeric_limits<uint32_t>::max();
        }
        else
        {
            nSizeLegacy = static_cast<uint32_t>(nSize);
        }
        if (nUndoSize >= std::numeric_limits<uint32_t>::max())
        {
            nUndoSizeLegacy = std::numeric_limits<uint32_t>::max();
        }
        else
        {
            nUndoSizeLegacy = static_cast<uint32_t>(nUndoSize);
        }
        READWRITE(VARINT(nBlocks));
        READWRITE(VARINT(nSizeLegacy));
        READWRITE(VARINT(nUndoSizeLegacy));
        READWRITE(VARINT(nHeightFirst));
        READWRITE(VARINT(nHeightLast));
        READWRITE(VARINT(nTimeFirst));
        READWRITE(VARINT(nTimeLast));
        if (nSizeLegacy == std::numeric_limits<uint32_t>::max())
        {
            READWRITE(VARINT(nSize));
        }
        else
        {
            nSize = nSizeLegacy;
        }
        if (nUndoSize == std::numeric_limits<uint32_t>::max())
        {
            READWRITE(VARINT(nUndoSize));
        }
        else
        {
            nUndoSize = nUndoSizeLegacy;
        }
    }

    void SetNull() {
        nBlocks = 0;
        nHeightFirst = 0;
        nHeightLast = 0;
        nTimeFirst = 0;
        nTimeLast = 0;
        nSize = 0;
        nUndoSize = 0;
    }

    CBlockFileInfo() { SetNull(); }

    std::string ToString() const;

    /** update statistics (does not update nSize) */
    void AddBlock(unsigned int nHeightIn, uint64_t nTimeIn) {
        if (nBlocks == 0 || nHeightFirst > nHeightIn) {
            nHeightFirst = nHeightIn;
        }
        if (nBlocks == 0 || nTimeFirst > nTimeIn) {
            nTimeFirst = nTimeIn;
        }
        nBlocks++;
        if (nHeightIn > nHeightLast) {
            nHeightLast = nHeightIn;
        }
        if (nTimeIn > nTimeLast) {
            nTimeLast = nTimeIn;
        }
    }
};

struct CDiskBlockPos {
    int nFile;
    unsigned int nPos;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        READWRITE(VARINT(nFile));
        READWRITE(VARINT(nPos));
    }

    CDiskBlockPos() { SetNull(); }

    CDiskBlockPos(int nFileIn, unsigned int nPosIn) {
        nFile = nFileIn;
        nPos = nPosIn;
    }

    friend bool operator==(const CDiskBlockPos &a, const CDiskBlockPos &b) {
        return (a.nFile == b.nFile && a.nPos == b.nPos);
    }

    friend bool operator!=(const CDiskBlockPos &a, const CDiskBlockPos &b) {
        return !(a == b);
    }

    void SetNull() {
        nFile = -1;
        nPos = 0;
    }
    bool IsNull() const { return (nFile == -1); }

    std::string ToString() const {
        return strprintf("CBlockDiskPos(nFile=%i, nPos=%i)", nFile, nPos);
    }
};

enum class BlockValidity : uint32_t {
    /**
     * Unused.
     */
    UNKNOWN = 0,

    /**
     * Parsed, version ok, hash satisfies claimed PoW, 1 <= vtx count <= max,
     * timestamp not in future.
     */
    HEADER = 1,

    /**
     * All parent headers found, difficulty matches, timestamp >= median
     * previous, checkpoint. Implies all parents are also at least TREE.
     */
    TREE = 2,

    /**
     * Only first tx is coinbase, 2 <= coinbase input script length <= 100,
     * transactions valid, no duplicate txids, sigops, size, merkle root.
     * Implies all parents are at least TREE but not necessarily TRANSACTIONS.
     * When all parent blocks also have TRANSACTIONS, CBlockIndex::nChainTx will
     * be set.
     */
    TRANSACTIONS = 3,

    /**
     * Outputs do not overspend inputs, no double spends, coinbase output ok, no
     * immature coinbase spends, BIP30.
     * Implies all parents are also at least CHAIN.
     */
    CHAIN = 4,

    /**
     * Scripts & signatures ok. Implies all parents are also at least SCRIPTS.
     */
    SCRIPTS = 5,
};

struct BlockStatus {
private:
    uint32_t status;

    explicit BlockStatus(uint32_t nStatusIn) : status(nStatusIn) {}

    static const uint32_t VALIDITY_MASK = 0x07;

    // Full block available in blk*.dat
    static const uint32_t HAS_DATA_FLAG = 0x08;
    // Undo data available in rev*.dat
    static const uint32_t HAS_UNDO_FLAG = 0x10;

    // The block is invalid.
    static const uint32_t FAILED_FLAG = 0x20;
    // The block has an invalid parent.
    static const uint32_t FAILED_PARENT_FLAG = 0x40;

    // The block disk file hash and content size are set.
    static const uint32_t HAS_DISK_BLOCK_META_DATA_FLAG = 0x80;

    // Mask used to check if the block failed.
    static const uint32_t INVALID_MASK = FAILED_FLAG | FAILED_PARENT_FLAG;

public:
    explicit BlockStatus() : status(0) {}

    BlockValidity getValidity() const {
        return BlockValidity(status & VALIDITY_MASK);
    }

    BlockStatus withValidity(BlockValidity validity) const {
        return BlockStatus((status & ~VALIDITY_MASK) | uint32_t(validity));
    }

    bool hasData() const { return status & HAS_DATA_FLAG; }
    BlockStatus withData(bool hasData = true) const {
        return BlockStatus((status & ~HAS_DATA_FLAG) |
                           (hasData ? HAS_DATA_FLAG : 0));
    }

    bool hasUndo() const { return status & HAS_UNDO_FLAG; }
    BlockStatus withUndo(bool hasUndo = true) const {
        return BlockStatus((status & ~HAS_UNDO_FLAG) |
                           (hasUndo ? HAS_UNDO_FLAG : 0));
    }

    bool hasFailed() const { return status & FAILED_FLAG; }
    BlockStatus withFailed(bool hasFailed = true) const {
        return BlockStatus((status & ~FAILED_FLAG) |
                           (hasFailed ? FAILED_FLAG : 0));
    }

    bool hasDiskBlockMetaData() const
    {
        return status & HAS_DISK_BLOCK_META_DATA_FLAG;
    }
    BlockStatus withDiskBlockMetaData(bool hasData = true) const
    {
        return BlockStatus((status & ~HAS_DISK_BLOCK_META_DATA_FLAG) |
                           (hasData ? HAS_DISK_BLOCK_META_DATA_FLAG : 0));
    }

    bool hasFailedParent() const { return status & FAILED_PARENT_FLAG; }
    BlockStatus withFailedParent(bool hasFailedParent = true) const {
        return BlockStatus((status & ~FAILED_PARENT_FLAG) |
                           (hasFailedParent ? FAILED_PARENT_FLAG : 0));
    }

    /**
     * Check whether this block index entry is valid up to the passed validity
     * level.
     */
    bool isValid(enum BlockValidity nUpTo = BlockValidity::TRANSACTIONS) const {
        if (isInvalid()) {
            return false;
        }

        return getValidity() >= nUpTo;
    }

    bool isInvalid() const { return status & INVALID_MASK; }
    BlockStatus withClearedFailureFlags() const {
        return BlockStatus(status & ~INVALID_MASK);
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        READWRITE(VARINT(status));
    }
};

/**
 * Structure for storing hash of the block data on disk and its size.
 */
struct CDiskBlockMetaData
{
    uint256 diskDataHash;
    uint64_t diskDataSize = 0;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action)
    {
        READWRITE(diskDataHash);
        READWRITE(diskDataSize);
    }
};

arith_uint256 GetBlockProof(const CBlockIndex &block);

/**
 * The block chain is a tree shaped structure starting with the genesis block at
 * the root, with each block potentially having multiple candidates to be the
 * next block. A blockindex may have multiple pprev pointing to it, but at most
 * one of them can be part of the currently active branch.
 */
class CBlockIndex {
public:
    //! pointer to the hash of the block, if any. Memory is owned by this
    //! CBlockIndex
    const uint256 *phashBlock;

    //! pointer to the index of the predecessor of this block
    CBlockIndex *pprev;

    //! pointer to the index of some further predecessor of this block
    CBlockIndex *pskip;

    //! height of the entry in the chain. The genesis block has height 0
    int nHeight;

    //! Which # file this block is stored in (blk?????.dat)
    int nFile;

    //! Byte offset within blk?????.dat where this block's data is stored
    unsigned int nDataPos;

    //! Byte offset within rev?????.dat where this block's undo data is stored
    unsigned int nUndoPos;

    //! (memory only) Total amount of work (expected number of hashes) in the
    //! chain up to and including this block
    arith_uint256 nChainWork;

    //! Number of transactions in this block.
    //! Note: in a potential headers-first mode, this number cannot be relied
    //! upon
    unsigned int nTx;

    //! (memory only) Number of transactions in the chain up to and including
    //! this block.
    //! This value will be non-zero only if and only if transactions for this
    //! block and all its parents are available. Change to 64-bit type when
    //! necessary; won't happen before 2030
    unsigned int nChainTx;

    //! Verification status of this block. See enum BlockStatus
    BlockStatus nStatus;

    //! block header
    int32_t nVersion;
    uint256 hashMerkleRoot;
    uint32_t nTime;
    uint32_t nBits;
    uint32_t nNonce;

    //! (memory only) Sequential id assigned to distinguish order in which
    //! blocks are received.
    int32_t nSequenceId;

    //! (memory only) block header metadata
    uint64_t nTimeReceived;

    //! (memory only) Maximum nTime in the chain upto and including this block.
    unsigned int nTimeMax;

    void SetNull() {
        phashBlock = nullptr;
        pprev = nullptr;
        pskip = nullptr;
        nHeight = 0;
        nFile = 0;
        nDataPos = 0;
        nUndoPos = 0;
        nChainWork = arith_uint256();
        nTx = 0;
        nChainTx = 0;
        nStatus = BlockStatus();
        nSequenceId = 0;
        nTimeMax = 0;

        nVersion = 0;
        hashMerkleRoot = uint256();
        nTime = 0;
        nTimeReceived = 0;
        nBits = 0;
        nNonce = 0;
        mDiskBlockMetaData = {};

        // set to maximum time by default to indicate that validation has not
        // yet been completed
        mValidationCompletionTime = SteadyClockTimePoint::max();
    }

    CBlockIndex() { SetNull(); }

    CBlockIndex(const CBlockHeader &block) {
        SetNull();

        nVersion = block.nVersion;
        hashMerkleRoot = block.hashMerkleRoot;
        nTime = block.nTime;
        // Default to block time if nTimeReceived is never set, which
        // in effect assumes that this block is honestly mined.
        // Note that nTimeReceived isn't written to disk, so blocks read from
        // disk will be assumed to be honestly mined.
        nTimeReceived = block.nTime;
        nBits = block.nBits;
        nNonce = block.nNonce;
    }

    void LoadFromPersistentData(const CBlockIndex& other, CBlockIndex* previous)
    {
        pprev = previous;
        nHeight = other.nHeight;
        nFile = other.nFile;
        nDataPos = other.nDataPos;
        nUndoPos = other.nUndoPos;
        nVersion = other.nVersion;
        hashMerkleRoot = other.hashMerkleRoot;
        nTime = other.nTime;
        nBits = other.nBits;
        nNonce = other.nNonce;
        nStatus = other.nStatus;
        nTx = other.nTx;
        mDiskBlockMetaData = other.mDiskBlockMetaData;
        mValidationCompletionTime = other.mValidationCompletionTime;
    }

    CDiskBlockPos GetBlockPos() const {
        CDiskBlockPos ret;
        if (nStatus.hasData()) {
            ret.nFile = nFile;
            ret.nPos = nDataPos;
        }
        return ret;
    }

    CDiskBlockPos GetUndoPos() const {
        CDiskBlockPos ret;
        if (nStatus.hasUndo()) {
            ret.nFile = nFile;
            ret.nPos = nUndoPos;
        }
        return ret;
    }

    CDiskBlockMetaData GetDiskBlockMetaData() const {return mDiskBlockMetaData;}
    void SetDiskBlockMetaData(const uint256& hash, size_t size)
    {
        assert(!hash.IsNull());
        assert(size > 0);

        mDiskBlockMetaData = {hash, size};
        nStatus = nStatus.withDiskBlockMetaData();
    }

    void SetDiskBlockData(
        size_t transactionsCount,
        const CDiskBlockPos& pos,
        CDiskBlockMetaData metaData)
    {
        nTx = transactionsCount;
        nChainTx = 0;
        nFile = pos.nFile;
        nDataPos = pos.nPos;
        nUndoPos = 0;
        nStatus = nStatus.withData();
        RaiseValidity(BlockValidity::TRANSACTIONS);

        if (!metaData.diskDataHash.IsNull() && metaData.diskDataSize)
        {
            mDiskBlockMetaData = std::move(metaData);
            nStatus = nStatus.withDiskBlockMetaData();
        }
    }

    void SetChainWork()
    {
        nChainWork =
            (pprev ? pprev->nChainWork : 0) +
            GetBlockProof(*this);
    }

    void ClearFileInfo()
    {
        nStatus =
            nStatus
                .withData(false)
                .withUndo(false)
                .withDiskBlockMetaData(false);
        nFile = 0;
        nDataPos = 0;
        nUndoPos = 0;
        mDiskBlockMetaData = {};
    }

    CBlockHeader GetBlockHeader() const {
        CBlockHeader block;
        block.nVersion = nVersion;
        if (pprev) {
            block.hashPrevBlock = pprev->GetBlockHash();
        }
        block.hashMerkleRoot = hashMerkleRoot;
        block.nTime = nTime;
        block.nBits = nBits;
        block.nNonce = nNonce;
        return block;
    }

    uint256 GetBlockHash() const { return *phashBlock; }

    int64_t GetBlockTime() const { return int64_t(nTime); }

    int64_t GetBlockTimeMax() const { return int64_t(nTimeMax); }

    int64_t GetHeaderReceivedTime() const { return nTimeReceived; }

    int64_t GetReceivedTimeDiff() const {
        return GetHeaderReceivedTime() - GetBlockTime();
    }

    enum { nMedianTimeSpan = 11 };

    int64_t GetMedianTimePast() const
    {
        std::vector<int64_t> block_times;

        const CBlockIndex* pindex = this;
        for(int i{}; i < nMedianTimeSpan && pindex; i++, pindex = pindex->pprev)
        {
            block_times.push_back(pindex->GetBlockTime());
        }

        const auto n{block_times.size() / 2};
        std::nth_element(begin(block_times), begin(block_times) + n,
                         end(block_times));
        return block_times[n];
    }

    /**
     * Pretend that validation to SCRIPT level was instantanious. This is used
     * for precious blocks where we wish to treat a certain block as if it was
     * the first block with a certain amount of work.
     */
    void IgnoreValidationTime()
    {
        mValidationCompletionTime = SteadyClockTimePoint::min();
    }

    /**
     * Get tie breaker time for checking which of the blocks with same amount of
     * work was validated to SCRIPT level first.
     */
    auto GetValidationCompletionTime() const
    {
        return mValidationCompletionTime;
    }

    std::string ToString() const {
        return strprintf(
            "CBlockIndex(pprev=%p, nHeight=%d, merkle=%s, hashBlock=%s)", pprev,
            nHeight, hashMerkleRoot.ToString(), GetBlockHash().ToString());
    }

    //! Check whether this block index entry is valid up to the passed validity
    //! level.
    bool IsValid(enum BlockValidity nUpTo = BlockValidity::TRANSACTIONS) const {
        return nStatus.isValid(nUpTo);
    }

    //! Raise the validity level of this block index entry.
    //! Returns true if the validity was changed.
    bool RaiseValidity(enum BlockValidity nUpTo) {
        // Only validity flags allowed.
        if (nStatus.isInvalid()) {
            return false;
        }

        if (nStatus.getValidity() >= nUpTo) {
            return false;
        }

        if (ValidityChangeRequiresValidationTimeSetting(nUpTo))
        {
            mValidationCompletionTime = std::chrono::steady_clock::now();
        }

        nStatus = nStatus.withValidity(nUpTo);
        return true;
    }

    //! Build the skiplist pointer for this entry.
    void BuildSkip();

    //! Efficiently find an ancestor of this block.
    CBlockIndex *GetAncestor(int height);
    const CBlockIndex *GetAncestor(int height) const;

protected:
    CDiskBlockMetaData mDiskBlockMetaData;

    using SteadyClockTimePoint =
        std::chrono::time_point<std::chrono::steady_clock>;
    // Time when the block validation has been completed to SCRIPT level.
    // This is a memmory only variable after reboot we can set it to
    // SteadyClockTimePoint::min() (best possible candidate value) since after
    // the validation we only care that best tip is valid and not which that
    // best tip is (it's a race condition during validation anyway).
    SteadyClockTimePoint mValidationCompletionTime;

private:
    bool ValidityChangeRequiresValidationTimeSetting(BlockValidity nUpTo) const
    {
        return
            nUpTo == BlockValidity::SCRIPTS
            && mValidationCompletionTime == SteadyClockTimePoint::max();
    }
};

/**
 * Maintain a map of CBlockIndex for all known headers.
 */
struct BlockHasher {
    size_t operator()(const uint256 &hash) const { return hash.GetCheapHash(); }
};

typedef std::unordered_map<uint256, CBlockIndex *, BlockHasher> BlockMap;
extern BlockMap mapBlockIndex;

/**
 * Return the time it would take to redo the work difference between from and
 * to, assuming the current hashrate corresponds to the difficulty at tip, in
 * seconds.
 */
int64_t GetBlockProofEquivalentTime(const CBlockIndex &to,
                                    const CBlockIndex &from,
                                    const CBlockIndex &tip,
                                    const Consensus::Params &);
/**
 * Find the forking point between two chain tips.
 */
const CBlockIndex *LastCommonAncestor(const CBlockIndex *pa,
                                      const CBlockIndex *pb);

/** Used to marshal pointers into hashes for db storage. */
class CDiskBlockIndex : public CBlockIndex {
public:
    uint256 hashPrev;

    CDiskBlockIndex() { hashPrev = uint256(); }

    explicit CDiskBlockIndex(const CBlockIndex *pindex) : CBlockIndex(*pindex) {
        hashPrev = (pprev ? pprev->GetBlockHash() : uint256());
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        int nVersion = s.GetVersion();
        if (!(s.GetType() & SER_GETHASH)) {
            READWRITE(VARINT(nVersion));
        }

        READWRITE(VARINT(nHeight));
        READWRITE(nStatus);
        READWRITE(VARINT(nTx));
        if (nStatus.hasData() || nStatus.hasUndo()) {
            READWRITE(VARINT(nFile));
        }
        if (nStatus.hasData()) {
            READWRITE(VARINT(nDataPos));
        }
        if (nStatus.hasUndo()) {
            READWRITE(VARINT(nUndoPos));
        }
        if(nStatus.getValidity() == BlockValidity::SCRIPTS)
        {
            mValidationCompletionTime =
                CBlockIndex::SteadyClockTimePoint::min();
        }

        // block header
        READWRITE(this->nVersion);
        READWRITE(hashPrev);
        READWRITE(hashMerkleRoot);
        READWRITE(nTime);
        READWRITE(nBits);
        READWRITE(nNonce);
        if (nStatus.hasDiskBlockMetaData())
        {
            try {
                READWRITE(mDiskBlockMetaData);
            } catch (std::ios_base::failure &) {
                nStatus = nStatus.withDiskBlockMetaData(false);
                LogPrintf("Can not read metadata from block %s. Probably upgrading from downgraded version. \n", GetBlockHash().ToString());
            }
        }
    }

    uint256 GetBlockHash() const {
        CBlockHeader block;
        block.nVersion = nVersion;
        block.hashPrevBlock = hashPrev;
        block.hashMerkleRoot = hashMerkleRoot;
        block.nTime = nTime;
        block.nBits = nBits;
        block.nNonce = nNonce;
        return block.GetHash();
    }

    std::string ToString() const {
        std::string str = "CDiskBlockIndex(";
        str += CBlockIndex::ToString();
        str += strprintf("\n                hashBlock=%s, hashPrev=%s)",
                         GetBlockHash().ToString(), hashPrev.ToString());
        return str;
    }
};

/**
 * An in-memory indexed chain of blocks.
 */
class CChain {
private:
    std::vector<CBlockIndex *> vChain;
    std::atomic<CBlockIndex*> mChainTip = nullptr;

public:
    /**
     * Returns the index entry for the genesis block of this chain, or nullptr
     * if none.
     */
    CBlockIndex *Genesis() const {
        return vChain.size() > 0 ? vChain[0] : nullptr;
    }

    /**
     * Returns the index entry for the tip of this chain, or nullptr if none.
     */
    CBlockIndex* Tip() const { return mChainTip; }

    /**
     * Returns the index entry at a particular height in this chain, or nullptr
     * if no such height exists.
     */
    CBlockIndex *operator[](int nHeight) const {
        if (nHeight < 0 || nHeight >= (int)vChain.size()) {
            return nullptr;
        }
        return vChain[nHeight];
    }

    /** Compare two chains efficiently. */
    friend bool operator==(const CChain &a, const CChain &b) {
        return a.vChain.size() == b.vChain.size() &&
               a.vChain[a.vChain.size() - 1] == b.vChain[b.vChain.size() - 1];
    }

    /** Efficiently check whether a block is present in this chain. */
    bool Contains(const CBlockIndex *pindex) const {
        return (*this)[pindex->nHeight] == pindex;
    }

    /**
     * Find the successor of a block in this chain, or nullptr if the given
     * index is not found or is the tip.
     */
    CBlockIndex *Next(const CBlockIndex *pindex) const {
        if (!Contains(pindex)) {
            return nullptr;
        }

        return (*this)[pindex->nHeight + 1];
    }

    /**
     * Return the maximal height in the chain or -1 if tip is not set.
     */
    int Height() const
    {
        const CBlockIndex* tip = mChainTip;
        return tip ? tip->nHeight : -1;
    }

    /** Set/initialize a chain with a given tip. */
    void SetTip(CBlockIndex *pindex);

    /**
     * Return a CBlockLocator that refers to a block in this chain (by default
     * the tip).
     */
    CBlockLocator GetLocator(const CBlockIndex *pindex = nullptr) const;

    /**
     * Find the last common block between this chain and a block index entry.
     */
    const CBlockIndex *FindFork(const CBlockIndex *pindex) const;

    /**
     * Find the earliest block with timestamp equal or greater than the given.
     */
    CBlockIndex *FindEarliestAtLeast(int64_t nTime) const;
};

/**
 * class CChainActiveSharedData.
 *
 * TODO: This class becomes redundant once CChain offers mt support.
 * For the time being, it is needed to share activeHeight & activeTipBlockHash
 * between different threads without a need to hold cs_main.
 */
class CChainActiveSharedData {
    std::atomic_int mChainActiveHeight {};
    uint256 mChainActiveTipBlockHash { uint256() };
    mutable std::shared_mutex mMainMtx {};

public:
    void SetChainActiveHeight(int height);
    int GetChainActiveHeight() const;

    void SetChainActiveTipBlockHash(uint256 blockHash);
    uint256 GetChainActiveTipBlockHash() const;
};

#endif // BITCOIN_CHAIN_H
