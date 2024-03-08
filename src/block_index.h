// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#ifndef BITCOIN_BLOCKINDEX_H
#define BITCOIN_BLOCKINDEX_H

#include "arith_uint256.h"
#include "consensus/params.h"
#include "dirty_block_index_store.h"
#include "disk_block_pos.h"
#include "streams.h"
#include "undo.h"
#include "utiltime.h"
#include <chrono>
#include <functional>
#include <optional>
#include <shared_mutex>
#include <unordered_map>
#include <vector>
#include <memory>

struct CBlockIndexWorkComparator;

template<typename Reader>
class CBlockStreamReader;

/**
 * Maximum amount of time that a block timestamp is allowed to exceed the
 * current network-adjusted time before the block will be accepted.
 */
// NOLINTNEXTLINE(bugprone-implicit-widening-of-multiplication-result)
static const int64_t MAX_FUTURE_BLOCK_TIME = 2 * 60 * 60;

/**
 * Timestamp window used as a grace period by code that compares external
 * timestamps (such as timestamps passed to RPCs, or wallet key creation times)
 * to block timestamps. This should be set at least as high as
 * MAX_FUTURE_BLOCK_TIME.
 */
static const int64_t TIMESTAMP_WINDOW = MAX_FUTURE_BLOCK_TIME;

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

std::string to_string(const enum BlockValidity&);

struct BlockStatus {
private:
    friend class CBlockIndex;
    friend class CDiskBlockIndex;

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

    // The block index contains data for soft rejection
    static const uint32_t HAS_SOFT_REJ_FLAG = 0x100;
    
    static const uint32_t HAS_DOUBLE_SPEND_FLAG = 0x200;

    static const uint32_t HAS_SOFT_CONSENSUS_FROZEN_FLAG = 0x400;

    // Mask used to check if the block failed.
    static const uint32_t INVALID_MASK = FAILED_FLAG | FAILED_PARENT_FLAG;

    [[nodiscard]] BlockStatus withData(bool hasData = true) const {
        return BlockStatus((status & ~HAS_DATA_FLAG) |
                           (hasData ? HAS_DATA_FLAG : 0));
    }

    [[nodiscard]] BlockStatus withUndo(bool hasUndo = true) const {
        return BlockStatus((status & ~HAS_UNDO_FLAG) |
                           (hasUndo ? HAS_UNDO_FLAG : 0));
    }

    [[nodiscard]] BlockStatus withDiskBlockMetaData(bool hasData = true) const
    {
        return BlockStatus((status & ~HAS_DISK_BLOCK_META_DATA_FLAG) |
                           (hasData ? HAS_DISK_BLOCK_META_DATA_FLAG : 0));
    }

public:
    template<typename T> struct UnitTestAccess;

    explicit BlockStatus() : status(0) {}

    BlockValidity getValidity() const {
        return BlockValidity(status & VALIDITY_MASK);
    }

    BlockStatus withValidity(BlockValidity validity) const {
        return BlockStatus((status & ~VALIDITY_MASK) | uint32_t(validity));
    }

    bool hasData() const { return status & HAS_DATA_FLAG; }

    bool hasUndo() const { return status & HAS_UNDO_FLAG; }

    bool hasFailed() const { return status & FAILED_FLAG; }
    BlockStatus withFailed(bool hasFailed = true) const {
        return BlockStatus((status & ~FAILED_FLAG) |
                           (hasFailed ? FAILED_FLAG : 0));
    }

    bool hasFailedParent() const { return status & FAILED_PARENT_FLAG; }
    BlockStatus withFailedParent(bool hasFailedParent = true) const {
        return BlockStatus((status & ~FAILED_PARENT_FLAG) |
                           (hasFailedParent ? FAILED_PARENT_FLAG : 0));
    }
    
    [[nodiscard]] bool hasDiskBlockMetaData() const
    {
        return status & HAS_DISK_BLOCK_META_DATA_FLAG;
    }
    
    bool hasDataForSoftRejection() const
    {
        return status & HAS_SOFT_REJ_FLAG;
    }
    [[nodiscard]] BlockStatus withDataForSoftRejection(bool hasData = true) const
    {
        return BlockStatus((status & ~HAS_SOFT_REJ_FLAG) |
                           (hasData ? HAS_SOFT_REJ_FLAG : 0));
    }

    bool hasDataForSoftConsensusFreeze() const
    {
        return status & HAS_SOFT_CONSENSUS_FROZEN_FLAG;
    }
    [[nodiscard]] BlockStatus withDataForSoftConsensusFreeze(bool hasData = true) const
    {
        return BlockStatus((status & ~HAS_SOFT_CONSENSUS_FROZEN_FLAG) |
                           (hasData ? HAS_SOFT_CONSENSUS_FROZEN_FLAG : 0));
    }

    [[nodiscard]] bool hasDoubleSpend() const
    {
        return status & HAS_DOUBLE_SPEND_FLAG;
    }

    BlockStatus withDoubleSpend(bool hasDoubleSpend = true) const 
    {
        return BlockStatus((status & ~HAS_DOUBLE_SPEND_FLAG) |
                           (hasDoubleSpend ? HAS_DOUBLE_SPEND_FLAG : 0));
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

    ADD_SERIALIZE_METHODS

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

//! Identifier of source from which we received the first instance of a block.
class CBlockSource
{
private:
    std::string mSource;

    CBlockSource(std::string&& source) : mSource( std::move(source) ) {}

public:
    static CBlockSource MakeUnknown() { return {"unknown"}; }
    static CBlockSource MakeLocal(const std::string& extra) { return {"local: " + extra}; }
    static CBlockSource MakeP2P(const std::string& address) { return {"p2p address: " + address}; }
    static CBlockSource MakeRPC() { return {"rpc"}; }

    const std::string& ToString() const { return mSource; }
};

/**
 * CBlockIndex holds information about block header as well as its context in the blockchain.
 * For holding information about the containing chain, pprev must always be set.
 * Only genesis block has pprev set to nullptr. With pprev, blocks can be connected into blockchain.
 * The blockchain is a tree shaped structure starting with the genesis block at the root,
 * with each block potentially having multiple candidates to be the next block.
 * A blockindex may have multiple pprev pointing to it, but at most one of them can be part of the currently active branch.
 *
 * CBlockIndex also contains information about file location of block.
 * The majority of CBlockIndex are immutable and therefore do not change once they are set.
 * Members that can change during the lifetime of CBlockIndex are data about disk location (nFile, nDataPos, nUndoPos), nSequenceId and nStatus.
 * All CBlockIndex objects (except TemporaryBlockIndex objects) are stored in a global variable mapBlockIndex.
 *
 * All public methods are thread-safe.
 * Mutable data in CBlockIndex is protected by a set of mutexes which are shared between all CBlockIndex instances.
 * This is a compromise between each CBlockIndex having own mutex and having just a single mutex for all objects.
 *
 * Mutex locks are held only within the implementation of CBlockIndex and are never passed to application code.
 * This eliminates the possibility of a dead-lock.
 * But it also means that all mutable public methods must be independent of each other and
 * object state between calling any two must be valid because it can be observed by another thread.
 * This is why there are also setters that atomically modify several members and methods that atomically do several independent things.
 *
 * NOTE: CBlockIndex can be soft consensus frozen during validation in which case
 *       the block can't be added to best chain as long as it doesn't receive enough
 *       descendant blocks to pass the freeze threshold.
 *       This is not reflected in CBlockIndex state and can only be queried by
 *       calling IsInExplicitSoftConsensusFreeze() or IsInSoftConsensusFreeze()
 */
class CBlockIndex { // NOLINT(cppcoreguidelines-special-member-functions)
public:
    template<typename T> struct UnitTestAccess;
    class TemporaryBlockIndex;

    struct BlockStreamAndMetaData
    {
        std::unique_ptr<CForwardAsyncReadonlyStream> stream;
        CDiskBlockMetaData metaData;
    };

    CBlockIndex(const CBlockIndex&) = delete;
    CBlockIndex& operator=(const CBlockIndex&) = delete;

    /**
     * Class used to guarantee that public constructors and other construction related methods
     * of this class can only be used from friends specified in this class.
     */
    class PrivateTag
    {
        // intentionally private
        explicit PrivateTag() = default;

        friend class BlockIndexStore;
        friend class BlockIndexStoreLoader;

    public:
        template<typename T> struct UnitTestAccess;
    };

private:

    /**
     * TODO: this member is a root of many problems regarding const-correctness and object construction
     * because object cannot be created without this member being set.
     * This should be implemented properly in the future.
    **/
    //! pointer to the hash of the block, if any.
    const uint256* phashBlock{ nullptr };

    //! pointer to the index of the predecessor of this block
    CBlockIndex* pprev{ nullptr };

    //! pointer to the index of some further predecessor of this block
    const CBlockIndex* pskip{ nullptr };

    //! height of the entry in the chain. The genesis block has height 0
    int32_t nHeight{ 0 };

    //! Which # file this block is stored in (blk?????.dat)
    int nFile{ 0 };

    //! Byte offset within blk?????.dat where this block's data is stored
    unsigned int nDataPos{ 0 };

    //! Byte offset within rev?????.dat where this block's undo data is stored
    unsigned int nUndoPos{ 0 };;

    //! (memory only) Total amount of work (expected number of hashes) in the
    //! chain up to and including this block
    arith_uint256 nChainWork;

    //! Number of transactions in this block.
    //! Note: in a potential headers-first mode, this number cannot be relied
    //! upon
    unsigned int nTx{ 0 };

    //! (memory only) Number of transactions in the chain up to and including
    //! this block.
    //! This value will be non-zero only if and only if transactions for this
    //! block and all its parents are available. Change to 64-bit type when
    //! necessary; won't happen before 2030
    unsigned int nChainTx{ 0 };

    //! Verification status of this block. See enum BlockStatus
    mutable BlockStatus nStatus;

    //! block header
    int32_t nVersion{ 0 };
    uint256 hashMerkleRoot;
    uint32_t nTime{ 0 };
    uint32_t nBits{ 0 };
    uint32_t nNonce{ 0 };

    //! (memory only) Sequential id assigned to distinguish order in which
    //! blocks are received.
    int32_t nSequenceId{ 0 };

    //! (memory only) block header metadata
    uint64_t nTimeReceived{};

    //! (memory only) Maximum nTime in the chain upto and including this block.
    unsigned int nTimeMax{ 0 };

    //! (memory only) Ignore this block and all of it descendants when checking criteria for the safe-mode.
    bool ignoreForSafeMode{ false };

    //! (memory only) Source from which we received the first instance of a block.
    CBlockSource mBlockSource{CBlockSource::MakeUnknown()};

    /**
     * If >=0, this block is considered soft consensus frozen. Value specifies number of descendants
     * in chain after this block that should also be considered consensus frozen.
     *
     * If <0, this block is not considered soft rejected (i.e. it is a normal block).
     *
     * NOTE: Value is memory only as persistence on disk is not needed since we
     *       can always recalculate the value during execution.
     */
    std::int32_t mSoftConsensusFreezeForNBlocks{ -1 };

    /**
     * Indicator whether current block is soft consensus frozen either due to
     * explicit freeze or implicit due to parent being frozen.
     *
     * It is calculated as:
     * std::max( mSoftConsensusFreezeForNBlocks, parent.mSoftConsensusFreezeForNBlocksCumulative - 1)
     *
     * For the next block in chain, value of this member is always equal to the value in parent minus one,
     * value of current block or is -1 if value in parent is already -1. This way soft rejection status is propagated
     * down the chain and a descendant block that is high enough will not be soft rejected anymore.
     *
     * This value is used in best chain selection algorithm. Chains whose tip is soft consensus frozen,
     * are not considered when selecting best chain.
     *
     * NOTE: Value is memory only as persistence on disk is not needed since we
     *       can always recalculate the value during execution.
     */
    std::int32_t mSoftConsensusFreezeForNBlocksCumulative{ -1 };

public:
    /**
     * Used after indexes are loaded from the database to update their chain
     * related data.
     * NOTE: This function must be called on indexes in sorted by ascending
     *        height order as it expects the parent data is already set
     *        correctly.
     *
     * Returns: true if index is linked to a chain and false otherwise.
     */
    bool PostLoadIndexConnect()
    {
        std::lock_guard lock { GetMutex() };

        BuildSkipNL();
        SetChainWorkNL();

        if (pprev)
        {
            UpdateSoftConsensusFreezeFromParentNL();
        }

        nTimeMax = (pprev ? std::max(pprev->nTimeMax, nTime) : nTime);

        // We can link the chain of blocks for which we've received transactions
        // at some point. Pruned nodes may have deleted the block.
        if (nTx > 0) {
            if (pprev)
            {
                if (pprev->nChainTx)
                {
                    nChainTx = pprev->nChainTx + nTx;
                } else {
                    nChainTx = 0;

                    return false;
                }
            } else {
                nChainTx = nTx;
            }
        }

        return true;
    }

    bool IsGenesis() const { return pprev == nullptr; }

    CBlockIndex* GetPrev() { return pprev; }
    const CBlockIndex* GetPrev() const { return pprev; }
    const CBlockIndex* GetSkip() const { return pskip; }

    /**
    * TODO: This method should become private.
    */
    CDiskBlockPos GetBlockPos() const
    {
        std::lock_guard lock { GetMutex() };

        return GetBlockPosNL();
    }

    CDiskBlockMetaData GetDiskBlockMetaData() const
    {
        std::lock_guard lock { GetMutex() };

        return mDiskBlockMetaData;
    }

    void SetSequenceId( int32_t id )
    {
        std::lock_guard lock { GetMutex() };

        nSequenceId = id;
    }

    int32_t GetSequenceId() const
    {
        std::lock_guard lock { GetMutex() };

        return nSequenceId;
    }

    unsigned int GetBlockTxCount() const
    {
        std::lock_guard lock { GetMutex() };

        return nTx;
    }

    void SetChainTxAndSequenceId(unsigned int chainTx, int32_t id)
    {
        std::lock_guard lock { GetMutex() };

        nChainTx = chainTx;
        nSequenceId = id;
    }

    void SetDiskBlockData(
        size_t transactionsCount,
        const CDiskBlockPos& pos,
        CDiskBlockMetaData metaData,
        const CBlockSource& source,
        DirtyBlockIndexStore& notifyDirty)
    {
        std::lock_guard lock { GetMutex() };

        nTx = transactionsCount;
        nChainTx = 0;
        nFile = pos.File();
        nDataPos = pos.Pos();
        nUndoPos = 0;
        nStatus = nStatus.withData();
        RaiseValidityNL(BlockValidity::TRANSACTIONS, notifyDirty);

        if (!metaData.diskDataHash.IsNull() && metaData.diskDataSize)
        {
            // NOLINTNEXTLINE(performance-move-const-arg)
            mDiskBlockMetaData = std::move(metaData);
            nStatus = nStatus.withDiskBlockMetaData();
        }

        mBlockSource = source;
    }

    void SetSoftConsensusFreezeFor(
        std::int32_t numberOfBlocks,
        DirtyBlockIndexStore& notifyDirty )
    {
        std::lock_guard lock{ GetMutex() };

        assert( numberOfBlocks >= 0 );
        assert( mSoftConsensusFreezeForNBlocks == -1 );

        mSoftConsensusFreezeForNBlocks = numberOfBlocks;
        mSoftConsensusFreezeForNBlocksCumulative =
            std::max(
                mSoftConsensusFreezeForNBlocksCumulative,
                mSoftConsensusFreezeForNBlocks );

        nStatus = nStatus.withDataForSoftConsensusFreeze();

        notifyDirty.Insert( *this );
    }

    void UpdateSoftConsensusFreezeFromParent()
    {
        std::lock_guard lock{ GetMutex() };

        UpdateSoftConsensusFreezeFromParentNL();
    }

    bool IsInExplicitSoftConsensusFreeze() const
    {
        std::lock_guard lock{ GetMutex() };

        return mSoftConsensusFreezeForNBlocks != -1;
    }

    bool IsInSoftConsensusFreeze() const
    {
        std::lock_guard lock{ GetMutex() };

        return mSoftConsensusFreezeForNBlocksCumulative != -1;
    }

    /**
     * Return true if this block is soft rejected.
     */
    bool IsSoftRejected() const
    {
        std::lock_guard lock { GetMutex() };
        return IsSoftRejectedNL();
    }

    /**
     * Return true if this block should be considered soft rejected because of its parent.
     *
     * @note Parent of this block must be known and its value of nSoftRejected must be set correctly.
     */
    bool ShouldBeConsideredSoftRejectedBecauseOfParent() const
    {
        std::lock_guard lock { GetMutex() };
        return ShouldBeConsideredSoftRejectedBecauseOfParentNL();
    }

    /**
     * Return number of blocks after this one that should also be considered soft rejected.
     *
     * If <0, this block is not soft rejected and does not affect descendant blocks.
     */
    std::int32_t GetSoftRejectedFor() const
    {
        std::lock_guard lock { GetMutex() };
        return nSoftRejected;
    }

    /**
     * Set number of blocks after this one, which should also be considered soft rejected.
     *
     * If numBlocks is -1, this block will not be considered soft rejected. Values lower than -1 must not be used.
     *
     * @note Can only be called on blocks that are not soft rejected because of its parent.
     *       This implies that parent of this block must be known and its value of nSoftRejected must be set correctly.
     * @note After calling this method, SetSoftRejectedFromParent() should be called on known descendants
     *       of this block on all chains. This should be done recursively up to and including numBlocks (or previous
     *       value of nSoftRejected, whichever is larger) higher than this block.
     *       This will ensure that soft rejection status is properly propagated to subsequent blocks.
     */
    void SetSoftRejectedFor(std::int32_t numBlocks, DirtyBlockIndexStore& notifyDirty)
    {
        std::lock_guard lock { GetMutex() };
        assert(numBlocks>=-1);
        assert(!ShouldBeConsideredSoftRejectedBecauseOfParentNL()); // this block must not be soft rejected because of its parent

        nSoftRejected = numBlocks;

        // Data only needs to be stored on disk if block is soft rejected because
        // absence of this data means that block is not considered soft rejected.
        nStatus = nStatus.withDataForSoftRejection( IsSoftRejectedNL() );

        notifyDirty.Insert( *this );
    }

    /**
     * Set soft rejection status from parent block.
     *
     * This method should be used to properly propagate soft rejection status on child blocks
     * (either on newly received block or when status in parent is changed).
     *
     * @note Parent of this block must be known and its value of nSoftRejected must be set correctly.
     */
    void SetSoftRejectedFromParent(DirtyBlockIndexStore& notifyDirty)
    {
        std::lock_guard lock { GetMutex() };
        SetSoftRejectedFromParentNL(notifyDirty);
    }
    
    bool HasDoubleSpend() const
    {
        std::lock_guard lock { GetMutex() };
        return nStatus.hasDoubleSpend();
    }

    void SetChainWork()
    {
        nChainWork =
            (pprev ? pprev->nChainWork : 0) +
            GetBlockProof(*this);
    }

    // Returns true if clear is successful, otherwise false
    bool ClearFileInfoIfFileNumberEquals(int fileNumber, DirtyBlockIndexStore& notifyDirty)
    {
        std::lock_guard lock { GetMutex() };
        if (nFile == fileNumber)
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

            notifyDirty.Insert( *this );

            return true;
        }

        return false;
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

    int32_t GetHeight() const { return nHeight; }

    const uint256& GetBlockHash() const { return *phashBlock; }

    int64_t GetBlockTime() const { return int64_t(nTime); }

    int64_t GetBlockTimeMax() const { return int64_t(nTimeMax); }
    
    // NOLINTNEXTLINE(*-narrowing-conversions)
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
        // NOLINTNEXTLINE(*-narrowing-conversions)
        std::nth_element(begin(block_times), begin(block_times) + n,
                         end(block_times));
        return block_times[n];
    }

    uint32_t GetNonce() const { return nNonce; }
    const uint256& GetMerkleRoot() const { return hashMerkleRoot; }

    uint32_t GetBits() const
    {
        return this->nBits;
    }

    int32_t GetVersion() const
    {
        return this->nVersion;
    }

    unsigned int GetChainTx() const
    {
        std::lock_guard lock { GetMutex() };
        return this->nChainTx;
    }

    const arith_uint256& GetChainWork() const
    {
        return this->nChainWork;
    }

    bool GetIgnoredForSafeMode() const
    {
        std::lock_guard lock { GetMutex() };
        return this->ignoreForSafeMode;
    }

    void SetIgnoredForSafeMode(bool doIgnore)
    {
        std::lock_guard lock { GetMutex() };
        this->ignoreForSafeMode = doIgnore;
    }

    BlockStatus getStatus() const {
        std::lock_guard lock { GetMutex() };
        return nStatus;
    }

    void ModifyStatusWithFailed(DirtyBlockIndexStore& notifyDirty) {
        std::lock_guard lock { GetMutex() };
        nStatus = nStatus.withFailed();

        notifyDirty.Insert( *this );
    }

    void ModifyStatusWithClearedFailedFlags(DirtyBlockIndexStore& notifyDirty) {
        std::lock_guard lock { GetMutex() };
        nStatus = nStatus.withClearedFailureFlags();

        notifyDirty.Insert( *this );
    }

    void ModifyStatusWithFailedParent(DirtyBlockIndexStore& notifyDirty) {
        std::lock_guard lock { GetMutex() };
        nStatus = nStatus.withFailedParent();

        notifyDirty.Insert( *this );
    }
    
    void ModifyStatusWithDoubleSpend(DirtyBlockIndexStore& notifyDirty)
    {
        std::lock_guard lock { GetMutex() };
        nStatus = nStatus.withDoubleSpend();
        notifyDirty.Insert( *this );
    }

    /**
     * Pretend that validation to SCRIPT level was instantanious. This is used
     * for precious blocks where we wish to treat a certain block as if it was
     * the first block with a certain amount of work.
     */
    void IgnoreValidationTime()
    {
        std::lock_guard lock{ GetMutex() };
        mValidationCompletionTime = SteadyClockTimePoint::min();
    }

    /**
     * Get tie breaker time for checking which of the blocks with same amount of
     * work was validated to SCRIPT level first.
     */
    auto GetValidationCompletionTime() const
    {
        std::lock_guard lock{ GetMutex() };
        return mValidationCompletionTime;
    }

    std::string ToString() const {
        return strprintf(
            "CBlockIndex(pprev=%p, nHeight=%d, merkle=%s, hashBlock=%s)", pprev,
            nHeight, hashMerkleRoot.ToString(), GetBlockHash().ToString());
    }

    bool IsValid(enum BlockValidity nUpTo = BlockValidity::TRANSACTIONS) const {
        std::lock_guard lock { GetMutex() };
        return IsValidNL(nUpTo);
    }

    bool RaiseValidity(enum BlockValidity nUpTo, DirtyBlockIndexStore& notifyDirty) {
        std::lock_guard lock { GetMutex() };
        return RaiseValidityNL(nUpTo, notifyDirty);
    }

    std::optional<int> GetFileNumber() const
    {
        std::lock_guard lock { GetMutex() };
        if (nStatus.hasData())
        {
            return nFile;
        }
        return std::nullopt;
    }

    //! Efficiently find an ancestor of this block.
    CBlockIndex *GetAncestor(int32_t height);
    const CBlockIndex *GetAncestor(int32_t height) const;

    const CBlockSource& GetBlockSource() const { return mBlockSource; }

    std::optional<CBlockUndo> GetBlockUndo() const;

    bool writeUndoToDisk(CValidationState &state, const CBlockUndo &blockundo,
                            bool fCheckForPruning, const Config &config, DirtyBlockIndexStore& notifyDirty);

    bool verifyUndoValidity() const;

    bool ReadBlockFromDisk(CBlock &block,
                            const Config &config) const;

    void SetBlockIndexFileMetaDataIfNotSet(
        CDiskBlockMetaData metadata, DirtyBlockIndexStore& notifyDirty) const;

    std::unique_ptr<CBlockStreamReader<CFileReader>> GetDiskBlockStreamReader(
                            bool calculateDiskBlockMetadata=false) const;

    // Same as above except that pos is obtained from pindex and some additional checks are performed
    std::unique_ptr<CBlockStreamReader<CFileReader>> GetDiskBlockStreamReader(
                            const Config &config, bool calculateDiskBlockMetadata=false) const;

    BlockStreamAndMetaData StreamBlockFromDisk(int networkVersion, DirtyBlockIndexStore& notifyDirty) const;

    std::unique_ptr<CForwardReadonlyStream> StreamSyncBlockFromDisk() const;

    std::unique_ptr<CForwardReadonlyStream> StreamSyncPartialBlockFromDisk(
                            uint64_t offset, uint64_t length) const;

    friend class CDiskBlockIndex;

    /**
     * NOTE: Constructor is not publicly available and can only be used by friends of PrivateTag class!
     *       Because it is public, it can also be used for emplace construction inside containers.
     */
    CBlockIndex(PrivateTag) noexcept {}

    /**
     * NOTE: Constructor is not publicly available and can only be used by friends of PrivateTag class!
     *       Because it is public, it can also be used for emplace construction inside containers.
     */
    CBlockIndex(
        const CBlockHeader& block,
        CBlockIndex* prev,
        DirtyBlockIndexStore& notifyDirty,
        PrivateTag) noexcept
        : CBlockIndex{ block, prev, notifyDirty }
    {}

    /**
     * NOTE: Method is not publicly available and can only be used by friends of PrivateTag class!
     *       Because it is only used during logical object construction, method name has a class name prefix.
     */
    void CBlockIndex_SetBlockHash(const uint256* blockhash, PrivateTag)
    {
        CBlockIndex_SetBlockHash(blockhash);
    }

    /**
     * NOTE: Method is not publicly available and can only be used by friends of PrivateTag class!
     *       Because it is only used during logical object construction, method name has a class name prefix.
     */
    void CBlockIndex_SetPrev(CBlockIndex* prev, PrivateTag)
    {
        CBlockIndex_SetPrev(prev);
    }

protected:
    mutable CDiskBlockMetaData mDiskBlockMetaData;

    /**
     * If >=0, this block is considered soft rejected. Value specifies number of descendants
     * in chain after this block that should also be considered soft rejected.
     *
     * If <0, this block is not considered soft rejected (i.e. it is a normal block).
     *
     * For the next block in chain, value of this member is always equal to the value in parent minus one,
     * or is -1 if value in parent is already -1. This way soft rejection status is propagated
     * down the chain and a descendant block that is high enough will not be soft rejected anymore.
     *
     * This value is used in best chain selection algorithm. Chains whose tip is soft rejected,
     * are not considered when selecting best chain.
     */
    std::int32_t nSoftRejected { -1 };

    using SteadyClockTimePoint =
        std::chrono::time_point<std::chrono::steady_clock>;
    // Time when the block validation has been completed to SCRIPT level.
    // This is a memmory only variable after reboot we can set it to
    // SteadyClockTimePoint::min() (best possible candidate value) since after
    // the validation we only care that best tip is valid and not which that
    // best tip is (it's a race condition during validation anyway).
    //
    // Set to maximum time by default to indicate that validation has not
    // yet been completed.
    SteadyClockTimePoint mValidationCompletionTime{ SteadyClockTimePoint::max() };

private:
    CBlockIndex(const CBlockHeader& block) noexcept
        : nVersion{ block.nVersion }
        , hashMerkleRoot{ block.hashMerkleRoot }
        , nTime{ block.nTime }
        , nBits{ block.nBits }
        , nNonce{ block.nNonce }
        // Default to block time if nTimeReceived is never set, which
        // in effect assumes that this block is honestly mined.
        // Note that nTimeReceived isn't written to disk, so blocks read from
        // disk will be assumed to be honestly mined.
        , nTimeReceived{ block.nTime }
    {}

    CBlockIndex(
        const CBlockHeader& block,
        CBlockIndex* prev,
        DirtyBlockIndexStore& notifyDirty ) noexcept
        : CBlockIndex{ block }
    {
        // nSequenceId remains 0 to blocks only when the full data is available,
        // to avoid miners withholding blocks but broadcasting headers, to get a
        // competitive advantage.

        if (prev)
        {
            pprev = prev;
            nHeight = pprev->nHeight + 1;
            BuildSkipNL();
            // Set soft rejection status from parent.
            // Note that if a parent of a block is not found in index, this must be a genesis block.
            // Since genesis block is never considered soft rejected, defaults set by CBlockIndex
            // constructor are OK.
            SetSoftRejectedFromParentBaseNL();

            UpdateSoftConsensusFreezeFromParentNL();
        }
        nTimeReceived = GetTime();
        nTimeMax = pprev ? std::max(pprev->nTimeMax, nTime) : nTime;
        SetChainWorkNL();

        // We want to trigger dirty marking even though we've just created a
        // new index that might never get block data as we might get a descendant
        // index with requested block data that would be written to database
        // and in that case if parent block index wouldn't exist in the database
        // we'd get an incomplete chain which is not supported and would fail
        // with an assert.
        RaiseValidityNL( BlockValidity::TREE, notifyDirty );
    }

    void CBlockIndex_SetBlockHash(const uint256* blockhash)
    {
        phashBlock = blockhash;
    }

    void CBlockIndex_SetPrev(CBlockIndex* prev)
    {
        pprev = prev;
    }

    bool PopulateBlockIndexBlockDiskMetaDataNL(FILE* file,
                            int networkVersion,
                            DirtyBlockIndexStore& notifyDirty) const;

    void SetBlockIndexFileMetaDataIfNotSetNL(
        CDiskBlockMetaData metadata, DirtyBlockIndexStore& notifyDirty) const;

    CDiskBlockPos GetUndoPosNL() const
    {
       if (nStatus.hasUndo()) {
            return { nFile, nUndoPos };
        }
        return {};
    }

    bool IsSoftRejectedNL() const
    {
        return nSoftRejected >= 0;
    }

    bool ShouldBeConsideredSoftRejectedBecauseOfParentNL() const
    {
        assert(pprev);
        return pprev->nSoftRejected > 0; // NOTE: Parent block makes this one soft rejected only if it affects one or more blocks after it.
                                         //       If this value is 0 or -1, this block is not soft rejected because of its parent.
    }
    
    CDiskBlockPos GetBlockPosNL() const
    {
        if (nStatus.hasData()) {
            return { nFile, nDataPos };
        }
        return {};
    }

    // Check whether this block index entry is valid up to the passed validity
    // level.
    bool IsValidNL(enum BlockValidity nUpTo = BlockValidity::TRANSACTIONS) const {
        return nStatus.isValid(nUpTo);
    }

    // Raise the validity level of this block index entry.
    // Returns true if the validity was changed and marks index as dirty.
    bool RaiseValidityNL(enum BlockValidity nUpTo, DirtyBlockIndexStore& notifyDirty)
    {
        notifyDirty.Insert( *this );

        // Only validity flags allowed.
        if (nStatus.isInvalid()) {
            return false;
        }

        if (nStatus.getValidity() >= nUpTo) {
            return false;
        }

        // Check if validity change requires setting mValidationCompletionTime
        if (nUpTo == BlockValidity::SCRIPTS &&
            mValidationCompletionTime == SteadyClockTimePoint::max() )
        {
            mValidationCompletionTime = std::chrono::steady_clock::now();
        }

        nStatus = nStatus.withValidity(nUpTo);

        return true;
    }

    void SetChainWorkNL()
    {
        nChainWork =
            (pprev ? pprev->nChainWork : 0) +
            GetBlockProof(*this);
    }

    void SetSoftRejectedFromParentBaseNL()
    {
        if(ShouldBeConsideredSoftRejectedBecauseOfParentNL())
        {
            // If previous block was marked soft rejected, this one is also soft rejected, but for one block less.
            nSoftRejected = pprev->nSoftRejected - 1;
            nStatus = nStatus.withDataForSoftRejection(true);
        }
        else
        {
            // This block is not soft rejected.
            nSoftRejected = -1;
            nStatus = nStatus.withDataForSoftRejection(false);
        }
    }


    void SetSoftRejectedFromParentNL(DirtyBlockIndexStore& notifyDirty)
    {
        SetSoftRejectedFromParentBaseNL();

        notifyDirty.Insert( *this );
    }    

    //! Build the skiplist pointer for this entry.
    void BuildSkipNL();

    void SetDiskBlockMetaData(const uint256& hash, size_t size, DirtyBlockIndexStore& notifyDirty) const
    {
        assert(!hash.IsNull());
        assert(size > 0);

        mDiskBlockMetaData = {hash, size};
        nStatus = nStatus.withDiskBlockMetaData();

        notifyDirty.Insert( *this );
    }

    std::mutex& GetMutex() const;

    void UpdateSoftConsensusFreezeFromParentNL()
    {
        assert( pprev ); // !IsGenesis

        if ( pprev->mSoftConsensusFreezeForNBlocksCumulative != -1 )
        {
            mSoftConsensusFreezeForNBlocksCumulative =
                std::max(
                    mSoftConsensusFreezeForNBlocks,
                    pprev->mSoftConsensusFreezeForNBlocksCumulative - 1);
        }
        else
        {
            mSoftConsensusFreezeForNBlocksCumulative =
                mSoftConsensusFreezeForNBlocks;
        }
    }
};

/**
 * TODO make const in future MRs once const correctness is achieved
 *
 * Class for creating temporary block index that can be used for various
 * checks if block is a valid candidate for potential later inclusion.
 * Class is also used in unit tests.
 */
// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions)
class CBlockIndex::TemporaryBlockIndex
{
public:
    // Creates a genesis like block without parent
    TemporaryBlockIndex( const CBlockHeader& block )
        : mDummyHash{ block.GetHash() }
        , mIndex{ block }
    {
        //pSkip is not set here intentionally: logic depends on pSkip to be nullptr at this point
        mIndex.phashBlock = &mDummyHash;

        mIndex.SetChainWorkNL(); // no need for lock as we control the dummy
    }

    // TODO make parent const in future MRs
    //
    // Creates a child block
    TemporaryBlockIndex(CBlockIndex& parent, const CBlockHeader& block )
        : mDummyHash{ block.GetHash() }
        , mIndex{ block }
    {
        //pSkip is not set here intentionally: logic depends on pSkip to be nullptr at this point
        mIndex.phashBlock = &mDummyHash;

        mIndex.pprev = &parent;
        mIndex.nHeight = parent.nHeight + 1;

        mIndex.SetChainWorkNL(); // no need for lock as we control the dummy
    }

    // We want to limit the scope of temporary as much as possible
    TemporaryBlockIndex(TemporaryBlockIndex&&) = delete;
    TemporaryBlockIndex& operator=(TemporaryBlockIndex&&) = delete;
    TemporaryBlockIndex(const TemporaryBlockIndex&) = delete;
    TemporaryBlockIndex& operator=(const TemporaryBlockIndex&) = delete;

    operator const CBlockIndex&() const { return mIndex; }
    operator const CBlockIndex*() const { return &mIndex; }
    const CBlockIndex* operator->() const { return &mIndex; }
    const CBlockIndex* get() const { return &mIndex; }

    // TODO delete this non-const batch in later MRs
    operator CBlockIndex&() { return mIndex; }
    operator CBlockIndex*() { return &mIndex; }
    CBlockIndex* operator->() { return &mIndex; }
    CBlockIndex* get() { return &mIndex; }

private:
    uint256 mDummyHash;
    CBlockIndex mIndex;
};

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

struct CBlockIndexWorkComparator {
    bool operator()(const CBlockIndex *pa, const CBlockIndex *pb) const {
        // First sort by most total work, ...
        auto paChainWork = pa->GetChainWork();
        auto pbChainWork = pb->GetChainWork();
        if (paChainWork > pbChainWork)
        {
            return false;
        }
        if (paChainWork < pbChainWork)
        {
            return true;
        }

        // ... then by when block was completely validated, ...
        auto paValidationCompletionTime = pa->GetValidationCompletionTime();
        auto pbValidationCompletionTime = pb->GetValidationCompletionTime();
        if (paValidationCompletionTime < pbValidationCompletionTime)
        {
            return false;
        }
        if (paValidationCompletionTime > pbValidationCompletionTime)
        {
            return true;
        }

        // ... then by earliest time received, ...
        auto paSeqId = pa->GetSequenceId();
        auto pbSeqId = pb->GetSequenceId();
        if (paSeqId < pbSeqId)
        {
            return false;
        }
        if (paSeqId > pbSeqId)
        {
            return true;
        }

        // Use pointer address as tie breaker (should only happen with blocks
        // loaded from disk, as those all have id 0 and validation time 0).
        if (pa < pb) {
            return false;
        }
        if (pa > pb) {
            return true;
        }

        // Identical blocks.
        return false;
    }
};

#endif
