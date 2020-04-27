// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2017 The Bitcoin developers
// Copyright (c) 2018-2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#ifndef BITCOIN_VALIDATION_H
#define BITCOIN_VALIDATION_H

#if defined(HAVE_CONFIG_H)
#include "config/bitcoin-config.h"
#endif

#include "amount.h"
#include "blockstreams.h"
#include "blockvalidation.h"
#include "chain.h"
#include "coins.h"
#include "consensus/consensus.h"
#include "fs.h"
#include "mining/journal_change_set.h"
#include "protocol.h" // For CMessageHeader::MessageMagic
#include "script/script_error.h"
#include "sync.h"
#include "streams.h"
#include "task.h"
#include "txn_double_spend_detector.h"
#include "txn_validation_result.h"
#include "versionbits.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

class CBlockIndex;
class CBlockTreeDB;
class CBloomFilter;
class CChainParams;
class CConnman;
class CInv;
class Config;
class CScriptCheck;
class CTxMemPool;
struct CTxnHandlers;
class CTxUndo;
class CValidationInterface;
class CValidationState;
struct ChainTxData;

struct PrecomputedTransactionData;
struct LockPoints;

namespace boost
{
    class thread_group;
}

namespace task
{
    class CCancellationToken;
}

#define MIN_TRANSACTION_SIZE                                                   \
    (::GetSerializeSize(CTransaction(), SER_NETWORK, PROTOCOL_VERSION))

/** Default for DEFAULT_WHITELISTRELAY. */
static const bool DEFAULT_WHITELISTRELAY = true;
/** Default for DEFAULT_WHITELISTFORCERELAY. */
static const bool DEFAULT_WHITELISTFORCERELAY = true;
/** Default for DEFAULT_REJECTMEMPOOLREQUEST. */
static const bool DEFAULT_REJECTMEMPOOLREQUEST = false;
/** Default for -minrelaytxfee, minimum relay fee for transactions */
static const Amount DEFAULT_MIN_RELAY_TX_FEE(250);
//! -maxtxfee default
static const Amount DEFAULT_TRANSACTION_MAXFEE(COIN / 10);
//! Discourage users to set fees higher than this amount (in satoshis) per kB
static const Amount HIGH_TX_FEE_PER_KB(COIN / 100);
/** -maxtxfee will warn if called with a higher fee than this amount (in
 * satoshis */
static const Amount HIGH_MAX_TX_FEE(100 * HIGH_TX_FEE_PER_KB);
/** Default for -limitancestorcount, max number of in-mempool ancestors */
static const uint64_t DEFAULT_ANCESTOR_LIMIT = 25;
/** Default for -limitdescendantcount, max number of in-mempool descendants */
static const uint64_t DEFAULT_DESCENDANT_LIMIT = 25;
/** Default for -limitancestorsize, maximum kilobytes of tx + all in-mempool
 * ancestors */
static const uint64_t DEFAULT_ANCESTOR_SIZE_LIMIT = DEFAULT_ANCESTOR_LIMIT * MAX_TX_SIZE_CONSENSUS_BEFORE_GENESIS;
/** Default for -limitdescendantsize, maximum kilobytes of in-mempool
 * descendants */
static const uint64_t DEFAULT_DESCENDANT_SIZE_LIMIT = DEFAULT_DESCENDANT_LIMIT * MAX_TX_SIZE_CONSENSUS_BEFORE_GENESIS;
/** Default for -mempoolexpiry, expiration time for mempool transactions in
 * hours */
static const unsigned int DEFAULT_MEMPOOL_EXPIRY = 336;
/** Default for -nonfinalmempoolexpiry, expiration time for non-final mempool transactions in hours */
static const unsigned int DEFAULT_NONFINAL_MEMPOOL_EXPIRY = 4 * 7 * 24;
/** The maximum size of a blk?????.dat file (since 0.8) */
static const unsigned int DEFAULT_PREFERRED_BLOCKFILE_SIZE = 0x8000000; // 128 MiB
/** The pre-allocation chunk size for blk?????.dat files (since 0.8) */
static const unsigned int BLOCKFILE_CHUNK_SIZE = 0x1000000; // 16 MiB
/** The pre-allocation chunk size for rev?????.dat files (since 0.8) */
static const unsigned int UNDOFILE_CHUNK_SIZE = 0x100000; // 1 MiB

/** Maximum number of script-checking threads allowed */
static const int MAX_SCRIPTCHECK_THREADS = 64;
/** -threadsperblock default (number of script-checking threads, 0 = auto) */
static const int DEFAULT_SCRIPTCHECK_THREADS = 0;
/** Number of blocks that can be requested at any given time from a single peer.
 */
static const int MAX_BLOCKS_IN_TRANSIT_PER_PEER = 16;
/**
 * Timeout in seconds during which a peer must stall block download progress
 * before being disconnected.
 */
static const unsigned int DEFAULT_BLOCK_STALLING_TIMEOUT = 10;
/**
 * Minimum rate (in KBytes/sec) we will allow a stalling peer to send to us at
 * before disconnecting them.
 */
static const unsigned int DEFAULT_MIN_BLOCK_STALLING_RATE = 100;
/**
 * Number of headers sent in one getheaders result. We rely on the assumption
 * that if a peer sends less than this number, we reached its tip. Changing this
 * value is a protocol upgrade.
 */
static const unsigned int MAX_HEADERS_RESULTS = 2000;
/**
 * Maximum depth of blocks we're willing to serve as compact blocks to peers
 * when requested. For older blocks, a regular BLOCK response will be sent.
 */
static const int MAX_CMPCTBLOCK_DEPTH = 5;
/**
 * Maximum depth of blocks we're willing to respond to GETBLOCKTXN requests for.
 */
static const int MAX_BLOCKTXN_DEPTH = 10;
/**
 * Size of the "block download window": how far ahead of our current height do
 * we fetch ? Larger windows tolerate larger download speed differences between
 * peer, but increase the potential degree of disordering of blocks on disk
 * (which make reindexing and in the future perhaps pruning harder). We'll
 * probably want to make this a per-peer adaptive value at some point.
 */
static const unsigned int DEFAULT_BLOCK_DOWNLOAD_WINDOW = 1024;
/** Time to wait (in seconds) between writing blocks/block index to disk. */
static const unsigned int DATABASE_WRITE_INTERVAL = 60 * 60;
/** Time to wait (in seconds) between flushing chainstate to disk. */
static const unsigned int DATABASE_FLUSH_INTERVAL = 24 * 60 * 60;
/** Maximum length of reject messages. */
static const unsigned int MAX_REJECT_MESSAGE_LENGTH = 111;
/** Average delay between local address broadcasts in seconds. */
static const unsigned int AVG_LOCAL_ADDRESS_BROADCAST_INTERVAL = 24 * 24 * 60;
/** Average delay between peer address broadcasts in seconds. */
static const unsigned int AVG_ADDRESS_BROADCAST_INTERVAL = 30;
/**
 * Average delay between trickled inventory transmissions in seconds.
 * Blocks and whitelisted receivers bypass this, outbound peers get half this
 * delay.
 */
static const unsigned int INVENTORY_BROADCAST_INTERVAL = 5;
/**
 * Maximum number of inventory items to send per transmission.
 * Limits the impact of low-fee transaction floods.
 */
static const unsigned int INVENTORY_BROADCAST_MAX_PER_MB = 7 * INVENTORY_BROADCAST_INTERVAL;
/** Average delay between feefilter broadcasts in seconds. */
static const unsigned int AVG_FEEFILTER_BROADCAST_INTERVAL = 10 * 60;
/** Maximum feefilter broadcast delay after significant change. */
static const unsigned int MAX_FEEFILTER_CHANGE_DELAY = 5 * 60;
/** Block download timeout base, expressed in millionths of the block interval
 * (i.e. 10 min) */
static const int64_t BLOCK_DOWNLOAD_TIMEOUT_BASE = 1000000;
/**
 * Additional block download timeout per parallel downloading peer (i.e. 5 min)
 */
static const int64_t BLOCK_DOWNLOAD_TIMEOUT_PER_PEER = 500000;

static const unsigned int DEFAULT_LIMITFREERELAY = 0;
static const bool DEFAULT_RELAYPRIORITY = true;
static const int64_t DEFAULT_MAX_TIP_AGE = 24 * 60 * 60;

/** Default for -permitbaremultisig */
static const bool DEFAULT_PERMIT_BAREMULTISIG = true;
static const bool DEFAULT_CHECKPOINTS_ENABLED = true;
static const bool DEFAULT_TXINDEX = false;
static const unsigned int DEFAULT_BANSCORE_THRESHOLD = 100;

/* Default settings for controlling P2P reading */
static const unsigned int DEFAULT_MIN_TIME_INTERVAL_CHECKSUM_MS = 500;
static const unsigned int DEFAULT_INVALID_CHECKSUM_FREQUENCY = 100;

/** Default for -persistmempool */
static const bool DEFAULT_PERSIST_MEMPOOL = true;
/** Default for using fee filter */
static const bool DEFAULT_FEEFILTER = true;

/**
 * Maximum number of headers to announce when relaying blocks with headers
 * message.
 */
static const unsigned int MAX_BLOCKS_TO_ANNOUNCE = 8;

/** Maximum number of unconnecting headers announcements before DoS score */
static const int MAX_UNCONNECTING_HEADERS = 10;

static const bool DEFAULT_PEERBLOOMFILTERS = true;

/** Default for -stopatheight */
static const int DEFAULT_STOPATHEIGHT = 0;

/** Default count of transaction script checker instances */
constexpr size_t DEFAULT_SCRIPT_CHECK_POOL_SIZE = 4;
/** Default maximum size of script batches processed by a single checker thread */
constexpr size_t DEFAULT_SCRIPT_CHECK_MAX_BATCH_SIZE = 128;

extern CScript COINBASE_FLAGS;
extern CCriticalSection cs_main;
extern CTxMemPool mempool;
extern uint64_t nLastBlockTx;
extern uint64_t nLastBlockSize;
extern const std::string strMessageMagic;
extern CWaitableCriticalSection csBestBlock;
extern CConditionVariable cvBlockChange;
extern std::atomic_bool fImporting;
extern bool fReindex;
extern bool fTxIndex;
extern bool fIsBareMultisigStd;
extern bool fRequireStandard;
extern bool fCheckBlockIndex;
extern bool fCheckpointsEnabled;
extern size_t nCoinCacheUsage;

/**
 * Absolute maximum transaction fee (in satoshis) used by wallet and mempool
 * (rejects high fee in sendrawtransaction)
 */
extern Amount maxTxFee;
/**
 * If the tip is older than this (in seconds), the node is considered to be in
 * initial block download.
 */
extern int64_t nMaxTipAge;

/**
 * Block hash whose ancestors we will assume to have valid scripts without
 * checking them.
 */
extern uint256 hashAssumeValid;

/**
 * Minimum work we will assume exists on some valid chain.
 */
extern arith_uint256 nMinimumChainWork;

/**
 * Best header we've seen so far (used for getheaders queries' starting points).
 */
extern CBlockIndex *pindexBestHeader;

/** Minimum disk space required - used in CheckDiskSpace() */
static const uint64_t nMinDiskSpace = 52428800;

/** Pruning-related variables and constants */
/** True if any block files have ever been pruned. */
extern bool fHavePruned;
/** True if we're running in -prune mode. */
extern bool fPruneMode;
/** Number of MiB of block files that we're trying to stay below. */
extern uint64_t nPruneTarget;
/** Block files containing a block-height within MIN_BLOCKS_TO_KEEP of
 * chainActive.Tip() will not be pruned. */
static const unsigned int MIN_BLOCKS_TO_KEEP = 288;

static const signed int DEFAULT_CHECKBLOCKS = 6;
static const unsigned int DEFAULT_CHECKLEVEL = 3;

// Flush modes to update on-disk chain state
enum FlushStateMode {
    FLUSH_STATE_NONE,
    FLUSH_STATE_IF_NEEDED,
    FLUSH_STATE_PERIODIC,
    FLUSH_STATE_ALWAYS
};

/**
 * Require that user allocate at least 550MB for block & undo files (blk???.dat
 * and rev???.dat)
 * At 1MB per block, 288 blocks = 288MB.
 * Add 15% for Undo data = 331MB
 * Add 20% for Orphan block rate = 397MB
 * We want the low water mark after pruning to be at least 397 MB and since we
 * prune in full block file chunks, we need the high water mark which triggers
 * the prune to be one 128MB block file + added 15% undo data = 147MB greater
 * for a total of 545MB. Setting the target to > than 550MB will make it likely
 * we can respect the target.
 */
static const uint64_t MIN_DISK_SPACE_FOR_BLOCK_FILES = 550 * 1024 * 1024;

/** get number of blocks that are currently being processed */
int GetProcessingBlocksCount();

/**
 * Minimum length of valid fork that trigger safe mode.
 */
static const int SAFE_MODE_MIN_VALID_FORK_LENGTH = 7;

/**
 * Maximum distance of valid fork tip from active tip.
 */
static const int SAFE_MODE_MAX_VALID_FORK_DISTANCE = 72;

/**
 * Maximum distance of forks last common block from current active tip
 * to still enter safe mode.
 */
static const int SAFE_MODE_MAX_FORK_DISTANCE = 288;

/**
 * Minimum number of blocks that fork should be ahead of active tip to
 * enter safe mode.
 */
static const int SAFE_MODE_MIN_POW_DIFFERENCE = 6;

/**
 * Method checks if block that is being added to block index causes
 * node to enter safe mode. 
 * Node enters "Safe mode" in two cases:
 *   1. When any fork tip which is demonstrating at least SAFE_MODE_MIN_POW_DIFFERENCE 
 *      blocks more proof of work than the current best chain, is detected regardless 
 *      of the header status (valid / invalid / unknown) but only if last common block
 *      is at most SAFE_MODE_MAX_FORK_DISTANCE blocks away from current best tip. 
 *   2. When a fork with successfully validated blocks is detected, which is at least 
 *      SAFE_MODE_MIN_VALID_FORK_LENGTH block long and whose tip is within 
 *      SAFE_MODE_MAX_VALID_FORK_DISTANCE blocks from the current best chain tip
 */
void CheckSafeModeParameters(const CBlockIndex* pindexNew);

/**
 * Method finds all chain tips (except active) and checks if any of them 
 * should trigger node to enter safe mode.
 */
void CheckSafeModeParametersForAllForksOnStartup();

/**
 * Invalidate all chains containing given block that should be already invalid. 
 * Set status of descendent blocks to "with failed parent".
 */
void InvalidateChain(const CBlockIndex* pindexNew);

/**
 * Minimum distance between recevied block and active tip required 
 * to perform TTOR order validation of a block.
 * This is a local policy and not a consensus rule.
 */
static const int MIN_TTOR_VALIDATION_DISTANCE = 100;

/** 
 * Search block for transaction that violates TTOR order. 
 * Returns false if TTOR is violated. 
 */
bool CheckBlockTTOROrder(const CBlock& block);

class BlockValidationOptions {
private:
    bool checkPoW : 1;
    bool checkMerkleRoot : 1;

    // If true; force block to be flagged as checked
    bool markChecked : 1;

public:
    BlockValidationOptions() : checkPoW{true}, checkMerkleRoot{true}, markChecked{false}
    {}

    BlockValidationOptions(bool checkPoWIn, bool checkMerkleRootIn, bool markCheckedIn = false)
        : checkPoW{checkPoWIn}, checkMerkleRoot{checkMerkleRootIn}, markChecked{markCheckedIn}
    {}

    bool shouldValidatePoW() const { return checkPoW; }
    bool shouldValidateMerkleRoot() const { return checkMerkleRoot; }
    bool shouldMarkChecked() const { return markChecked; }
};

/**
 * Keeping status of currently validating blocks and blocks that we wait after validation
 */
extern CBlockValidationStatus blockValidationStatus;

/**
 * Verify block without proof-of-work verification.
 *
 * If you want to *possibly* get feedback on whether pblock is valid, you must
 * install a CValidationInterface (see validationinterface.h) - this will have
 * its BlockChecked method called whenever *any* block completes validation.
 *
 * @param[in]   config  The global config.
 * @param[in]   pblock  The block we want to verify.
 * @return True if the block is valid.
 */
bool VerifyNewBlock(const Config &config,
                     const std::shared_ptr<const CBlock> pblock);

/**
 * Process an incoming block. This only returns after the best known valid
 * block is made active. Note that it does not, however, guarantee that the
 * specific block passed to it has been checked for validity!
 *
 * If you want to *possibly* get feedback on whether pblock is valid, you must
 * install a CValidationInterface (see validationinterface.h) - this will have
 * its BlockChecked method called whenever *any* block completes validation.
 *
 * Note that we guarantee that either the proof-of-work is valid on pblock, or
 * (and possibly also) BlockChecked will have been called.
 *
 * Call without cs_main held.
 *
 * @param[in]   config  The global config.
 * @param[in]   pblock  The block we want to process.
 * @param[in]   fForceProcessing Process this block even if unrequested; used
 * for non-network block sources and whitelisted peers.
 * @param[out]  fNewBlock A boolean which is set to indicate if the block was
 *                        first received via this call.
 * @return True if the block is accepted as a valid block.
 */
bool ProcessNewBlock(const Config &config,
                     const std::shared_ptr<const CBlock>& pblock,
                     bool fForceProcessing, bool *fNewBlock);

/**
 * Same as ProcessNewBlock but it doesn't activate best chain - it returns a
 * function that should be called asyncrhonously to activate the best chain.
 * Reason for this split is that ProcessNewBlock adds block to mapBlockIndex
 * so we execute that part synchronously as otherwise child blocks that were
 * sent right after the parent could be missing parent even though we've already
 * seen it.
 */
std::function<bool()> ProcessNewBlockWithAsyncBestChainActivation(
    task::CCancellationToken&& token,
    const Config& config,
    const std::shared_ptr<const CBlock>& pblock,
    bool fForceProcessing,
    bool* fNewBlock);

/**
 * Process incoming block headers.
 *
 * Call without cs_main held.
 *
 * @param[in]  config  The global config.
 * @param[in]  block   The block headers themselves.
 * @param[out] state   This may be set to an Error state if any error occurred
 *                     processing them.
 * @param[out] ppindex If set, the pointer will be set to point to the last new
 *                     block index object for the given headers.
 * @return True if block headers were accepted as valid.
 */
bool ProcessNewBlockHeaders(const Config &config,
                            const std::vector<CBlockHeader> &block,
                            CValidationState &state,
                            const CBlockIndex **ppindex = nullptr);

/** 
 * The size of the header for each block in a block file 
 */
unsigned int GetBlockFileBlockHeaderSize(uint64_t nBlockSize);

/**
 * Check whether enough disk space is available for an incoming block.
 */
bool CheckDiskSpace(uint64_t nAdditionalBytes = 0);

/**
 * Translation to a filesystem path.
 */
fs::path GetBlockPosFilename(const CDiskBlockPos &pos, const char *prefix);

/**
 * Import blocks from an external file.
 */
bool LoadExternalBlockFile(const Config &config, FILE *fileIn,
                           CDiskBlockPos *dbp = nullptr);

/** used for --reindex */
void ReindexAllBlockFiles(const Config &config, CBlockTreeDB *pblocktree, bool& fReindex);

/**
 * Initialize a new block tree database + block data on disk.
 */
bool InitBlockIndex(const Config &config);

/**
 * Load the block tree and coins database from disk.
 */
bool LoadBlockIndex(const CChainParams &chainparams);

/**
 * Update the chain tip based on database information.
 */
void LoadChainTip(const CChainParams &chainparams);

/**
 * Unload database information.
 */
void UnloadBlockIndex();

/**
 * Initialize script checking pool.
 */
void InitScriptCheckQueues(const Config& config, boost::thread_group& threadGroup);
//! Shutdown script checking pool.
void ShutdownScriptCheckQueues();

/**
 * Check whether we are doing an initial block download (synchronizing from disk
 * or network)
 */
bool IsInitialBlockDownload();

/**
 * Format a string that describes several potential problems detected by the
 * core.
 * strFor can have three values:
 * - "rpc": get critical warnings, which should put the client in safe mode if
 * non-empty
 * - "statusbar": get all warnings
 * This function only returns the highest priority warning of the set selected
 * by strFor.
 */
std::string GetWarnings(const std::string &strFor);

/**
 * Retrieve a transaction (from memory pool, or from disk, if possible).
 */
bool GetTransaction(const Config &config, const TxId &txid, CTransactionRef &tx,
    bool fAllowSlow, uint256 &hashBlock, bool& isGenesisEnabled);

/**
 * Find the best known block, and make it the active tip of the block chain.
 * If it fails, the tip is not updated.
 *
 * pblock is either nullptr or a pointer to a block that is already loaded
 * in memory (to avoid loading it from disk again). It also enables parallel
 * block validation if provided block is building on top of currently active
 * tip and a different call to ActivateBestChain is already in progress.
 *
 * Returns true if no fatal errors occurred. Returns false in case a fatal error
 * occurred (no disk space, database error, ...).
 *
 * NOTE: ActivateBestChain checks for tip candidates (better than current
 *      best chain tip so a potential candidate) but is not guaranteed to
 *      either change the tip or even consider the provided pblock. It may
 *      change the tip if a better candidate is found but that block is not
 *      necessarily the one that was provided by the caller of this function.
 *      Because of this state parameter may contain success or error but
 *      that value represents activation process in general and is not
 *      necessarily related to provided block in particular.
 */
bool ActivateBestChain(
    const task::CCancellationToken& token,
    const Config &config,
    CValidationState &state,
    const mining::CJournalChangeSetPtr& changeSet,
    std::shared_ptr<const CBlock> pblock = std::shared_ptr<const CBlock>());
Amount GetBlockSubsidy(int nHeight, const Consensus::Params &consensusParams);

/**
 * Determines whether block is a best chain candidate or not.
 *
 * Being a candidate means that you might be added to the tip or removed from
 * candidates later on - in any case it indicates that we should wait to see if
 * it lands in the best chain or not.
 */
bool IsBlockABestChainTipCandidate(CBlockIndex& index);

/**
 * Check whether there are any block index candidates that are older than
 * the provided time and still don't have SCRIPT validity status.
 */
bool AreOlderOrEqualUnvalidatedBlockIndexCandidates(
    const std::chrono::time_point<std::chrono::system_clock>& comparisonTime);

/**
 * Guess verification progress (as a fraction between 0.0=genesis and
 * 1.0=current tip).
 */
double GuessVerificationProgress(const ChainTxData &data, CBlockIndex *pindex);

/**
 * Unlink the specified files and mark associated block indices as pruned
 */
void UnlinkPrunedFiles(const std::set<int> &setFilesToPrune);

/** Create a new block index entry for a given block hash */
CBlockIndex *InsertBlockIndex(uint256 hash);
/**
 * Update the on-disk chain state.
 * The caches and indexes are flushed depending on the mode we're called with if
 * they're too large, if it's been a while since the last write, or always and
 * in all cases if we're in prune mode and are deleting files.
 */
bool FlushStateToDisk(
    const CChainParams &chainParams,
    CValidationState &state,
    FlushStateMode mode,
    int nManualPruneHeight = 0);
/** Flush all state, indexes and buffers to disk. */
void FlushStateToDisk();
/** Prune block files and flush state to disk. */
void PruneAndFlush();
/** Prune block files up to a given height */
void PruneBlockFilesManual(int nPruneUpToHeight);

/** Check if UAHF has activated. */
bool IsUAHFenabled(const Config &config, const CBlockIndex *pindexPrev);

/** Check if DAA HF has activated. */
bool IsDAAEnabled(const Config &config, const CBlockIndex *pindexPrev);

/** Check if Genesis has activated. */
bool IsGenesisEnabled(const Config &config, const CBlockIndex *pindexPrev);
/** Check if Genesis has activated.
 * Do not call this overload with height of coin. If the coin was created in mempool, 
 * this function will throw exception.
 */
bool IsGenesisEnabled(const Config& config, int nHeight);
/**  Check if Genesis has activated.
 * When a coins is present in mempool, it will have height MEMPOOL_HEIGHT. 
 * In this case, you should call this overload and specify the mempool height (chainActive.Height()+1) 
 *as parameter to correctly determine if genesis is enabled for this coin.
 */
bool IsGenesisEnabled(const Config& config, const Coin& coin, int mempoolHeight  );
int GetGenesisActivationHeight(const Config& config);

/**
 * A function used to produce a default value for a number of Low priority threads
 * (on the running hardware).
 *
 * If std::thread::hardware_concurrency < 8, then the value is 1, otherwise the returned value
 * shouldn't be greater than 25% of std::thread::hardware_concurrency.
 *
 * @param nTestingHCValue The argument used for testing purposes.
 * @return Returns a number of low priority threads supported by the platform.
 */
size_t GetNumLowPriorityValidationThrs(size_t nTestingHCValue=SIZE_MAX);

/**
 * A function used to produce a default value for a number of High priority threads
 * (on the running hardware).
 *
 * If std::thread::hardware_concurrency < 3, then the value is 1,
 * If std::thread::hardware_concurrency == 3, then the value is 2,
 * If std::thread::hardware_concurrency == 4, then the value is 3,
 * otherwise the returned value shouldn't be greater than 75% of std::thread::hardware_concurrency.
 *
 * @param nTestingHCValue The argument used for testing purposes.
 * @return Returns a number of high priority threads supported by the platform.
 */
size_t GetNumHighPriorityValidationThrs(size_t nTestingHCValue=SIZE_MAX);

/**
 * Limit mempool size.
 *
 * @param pool A reference to the mempool
 * @param changeSet A reference to the Jorunal ChangeSet
 * @param limit A size limit for txn to remove
 * @param age Time limit for txn to remove
 * @return A vector with all TxIds which were removed from the mempool
 */
std::vector<TxId> LimitMempoolSize(
    CTxMemPool &pool,
    const mining::CJournalChangeSetPtr& changeSet,
    size_t limit,
    unsigned long age);

/**
 * Submit transaction to the mempool.
 *
 * @param ptx A reference to the transaction
 * @param entry A valid entry point for the given transaction
 * @param fTxValidForFeeEstimation A flag to inform if txn is valid for fee estimations.
 * @param setAncestors  A set of ancestors
 * @param pool A reference to the mempool
 * @param state A reference to a state variable
 * @param changeSet A reference to the Jorunal ChangeSet
 * @param fLimitMempoolSize A flag to limit a mempool size
 * @param pnMempoolSize If not null store mempool size after txn is commited
 * @param pnDynamicMemoryUsage If not null store dynamic memory usage after txn is commited
 */
void CommitTxToMempool(
    const CTransactionRef &ptx,
    const CTxMemPoolEntry& entry,
    bool fTxValidForFeeEstimation,
    CTxMemPool::setEntries& setAncestors,
    CTxMemPool& pool,
    CValidationState& state,
    const mining::CJournalChangeSetPtr& changeSet,
    size_t* pnMempoolSize=nullptr,
    size_t* pnDynamicMemoryUsage=nullptr);

/**
 * The function performs essential checks which need to be fulfilled by a transaction
 * before submitting to the mempool.
 *
 * @param pTxInputData A reference to transaction's details
 * @param config A reference to a configuration
 * @param pool A reference to the mempool
 * @param dsDetector A reference to a double spend detector
 * @param fUseLimits A flag to check if timed cancellation source and coins cache limits should be used
 * @return A result of validation.
 */
CTxnValResult TxnValidation(
    const TxInputDataSPtr& pTxInputData,
    const Config &config,
    CTxMemPool &pool,
    TxnDoubleSpendDetectorSPtr dsDetector,
    bool fUseLimits);

/**
 * Handle an exception thrown during txn processing.
 *
 * @param sExceptionMsg A message related to the exception.
 * @param pTxInputData A reference to transaction's details.
 * @param txnValResult A result of txn validation.
 * @param pool A reference to the mempool
 * @param handlers Txn handlers.
 * @return A state of txn processing.
 */
CValidationState HandleTxnProcessingException(
    const std::string& sExceptionMsg,
    const TxInputDataSPtr& pTxInputData,
    const CTxnValResult& txnValResult,
    const CTxMemPool& pool,
    CTxnHandlers& handlers);

/**
 * Batch processing support for txns validation.
 *
 * @param pTxInputData A reference to transaction's details
 * @param config A reference to a configuration
 * @param pool A reference to the mempool
 * @param handlers Txn handlers
 * @param fUseLimits A flag to check if timed cancellation source and coins cache limits should be used
 * @return A vector of validation results
 */
std::pair<CTxnValResult, CTask::Status> TxnValidationProcessingTask(
    const TxInputDataSPtr& pTxInputData,
    const Config &config,
    CTxMemPool &pool,
    CTxnHandlers& handlers,
    bool fUseLimits,
    std::chrono::steady_clock::time_point end);

/**
 * Process validated txn. Submit txn to the mempool if it is valid.
 *
 * @param pool A reference to the mempool
 * @param txStatus A result of validation
 * @param handlers Txn handlers
 * @param fLimitMempoolSize A flag to limit a mempool size
 */
void ProcessValidatedTxn(
    CTxMemPool& pool,
    CTxnValResult& txStatus,
    CTxnHandlers& handlers,
    bool fLimitMempoolSize);

/**
 * Create a tx reject message.
 *
 * @param pTxInputData A reference to transaction's details
 * @param nRejectCode A reject code
 * @param sRejectReason A reject reason
 */
void CreateTxRejectMsgForP2PTxn(
    const TxInputDataSPtr& pTxInputData,
    unsigned int nRejectCode,
    const std::string& sRejectReason);

/** Convert CValidationState to a human-readable message for logging */
std::string FormatStateMessage(const CValidationState &state);

/**
 * Count ECDSA signature operations the old-fashioned (pre-0.6) way
 * @return number of sigops this transaction's outputs will produce when spent
 * @see CTransaction::FetchInputs
 */
uint64_t GetSigOpCountWithoutP2SH(const CTransaction &tx, bool isGenesisEnabled, bool& sigOpCountError);

/**
 * Count ECDSA signature operations in pay-to-script-hash inputs.
 *
 * @param[in] mapInputs Map of previous transactions that have outputs we're
 * spending
 * @return maximum number of sigops required to validate this transaction's
 * inputs
 * @see CTransaction::FetchInputs
 */
uint64_t GetP2SHSigOpCount(const Config &config, 
                           const CTransaction &tx,
                           const CCoinsViewCache &mapInputs, 
                           bool& sigOpCountError);

/**
 * Compute total signature operation of a transaction.
 * @param[in] tx     Transaction for which we are computing the cost
 * @param[in] inputs Map of previous transactions that have outputs we're
 * spending
 * @param[in] checkP2SH  check if it is P2SH and include signature operation of the redeem scripts
 * @return Total signature operation cost of tx
 */
uint64_t GetTransactionSigOpCount(const Config &config, 
                                  const CTransaction &tx,
                                  const CCoinsViewCache &inputs, 
                                  bool checkP2SH, 
                                  bool isGenesisEnabled, 
                                  bool& sigOpCountError);

/**
 * Check whether all inputs of this transaction are valid (no double spends,
 * scripts & sigs, amounts). This does not modify the UTXO set.
 *
 * If pvChecks is not nullptr, script checks are pushed onto it instead of being
 * performed inline. Any script checks which are not necessary (eg due to script
 * execution cache hits) are, obviously, not pushed onto pvChecks/run.
 *
 * Setting sigCacheStore/scriptCacheStore to false will remove elements from the
 * corresponding cache which are matched. This is useful for checking blocks
 * where we will likely never need the cache entry again.
 *
 * In case a task cancellation is triggered through token before result is
 * known the function returns a std::nullopt
 */
std::optional<bool> CheckInputs(
    const task::CCancellationToken& token,
    const Config& config,
    bool consensus,
    const CTransaction& tx,
    CValidationState& state,
    const CCoinsViewCache& view,
    bool fScriptChecks,
    const uint32_t flags,
    bool sigCacheStore,
    bool scriptCacheStore,
    const PrecomputedTransactionData& txdata,
    std::vector<CScriptCheck>* pvChecks = nullptr);

/** Apply the effects of this transaction on the UTXO set represented by view */
void UpdateCoins(const CTransaction &tx, CCoinsViewCache &inputs, int nHeight);
void UpdateCoins(const CTransaction &tx, CCoinsViewCache &inputs,
                 CTxUndo &txundo, int nHeight);

/** Transaction validation functions */

/** Context-independent validity checks for coinbase and non-coinbase transactions */
bool CheckRegularTransaction(const CTransaction &tx, CValidationState &state, uint64_t maxTxSigOpsCountConsensusBeforeGenesis, uint64_t maxTxSizeConsensus, bool isGenesisEnabled);
bool CheckCoinbase(const CTransaction &tx, CValidationState &state, uint64_t maxTxSigOpsCountConsensusBeforeGenesis, uint64_t maxTxSizeConsensus, bool isGenesisEnabled);

namespace Consensus {

/**
 * Check whether all inputs of this transaction are valid (no double spends and
 * amounts). This does not modify the UTXO set. This does not check scripts and
 * sigs. Preconditions: tx.IsCoinBase() is false.
 */
bool CheckTxInputs(const CTransaction &tx, CValidationState &state,
                   const CCoinsViewCache &inputs, int nSpendHeight);

} // namespace Consensus

/**
 * Test whether the given transaction is final for the given height and time.
 */
bool IsFinalTx(const CTransaction &tx, int nBlockHeight, int64_t nBlockTime);

/**
 * Test whether the LockPoints height and time are still valid on the current
 * chain.
 */
bool TestLockPointValidity(const LockPoints *lp);

/**
 * Check if transaction is final per BIP 68 sequence numbers and can be included
 * in a block. Consensus critical. Takes as input a list of heights at which
 * tx's inputs (in order) confirmed.
 */
bool SequenceLocks(const CTransaction &tx, int flags,
                   std::vector<int> *prevHeights, const CBlockIndex &block);

/**
 * Check if transaction will be BIP 68 final in the next block to be created.
 *
 * Simulates calling SequenceLocks() with data from the tip of the current
 * active chain. Optionally stores in LockPoints the resulting height and time
 * calculated and the hash of the block needed for calculation or skips the
 * calculation and uses the LockPoints passed in for evaluation. The LockPoints
 * should not be considered valid if CheckSequenceLocks returns false.
 *
 * See consensus/consensus.h for flag definitions.
 *
 * The caller of the method needs to hold the mempool's smtx.
 */
bool CheckSequenceLocks(
    const CTransaction &tx,
    const CTxMemPool &pool,
    const Config& config,
    int flags,
    LockPoints *lp = nullptr,
    bool useExistingLockPoints = false);

/**
 * Closure representing one script verification.
 * Note that this stores references to the spending transaction.
 */
class CScriptCheck {
private:
    CScript scriptPubKey;
    Amount amount;
    const CTransaction *ptxTo = 0;
    unsigned int nIn = 0;
    uint32_t nFlags = 0;
    bool cacheStore = false;
    ScriptError error = SCRIPT_ERR_UNKNOWN_ERROR;
    PrecomputedTransactionData txdata;
    std::reference_wrapper<const Config> config;
    bool consensus = false;

public:
    CScriptCheck(const Config &configIn, bool consensusIn, const CScript &scriptPubKeyIn, const Amount amountIn,
                 const CTransaction &txToIn, unsigned int nInIn,
                 uint32_t nFlagsIn, bool cacheIn,
                 const PrecomputedTransactionData& txdataIn)
        : scriptPubKey(scriptPubKeyIn), amount(amountIn), ptxTo(&txToIn),
          nIn(nInIn), nFlags(nFlagsIn), cacheStore(cacheIn),
          error(SCRIPT_ERR_UNKNOWN_ERROR), txdata(txdataIn), config(configIn), consensus(consensusIn) {}

    std::optional<bool> operator()(const task::CCancellationToken& token);

    ScriptError GetScriptError() const { return error; }
};

/** Functions for disk access for blocks */
bool ReadBlockFromDisk(CBlock &block, const CDiskBlockPos &pos,
                       const Config &config);
bool ReadBlockFromDisk(CBlock &block, const CBlockIndex *pindex,
                       const Config &config);
std::unique_ptr<CBlockStreamReader<CFileReader>> GetDiskBlockStreamReader(
    const CDiskBlockPos& pos, bool calculateDiskBlockMetadata=false);
std::unique_ptr<CForwardAsyncReadonlyStream> StreamBlockFromDisk(
    CBlockIndex& index,
    int networkVersion);
std::unique_ptr<CForwardReadonlyStream> StreamSyncBlockFromDisk(CBlockIndex& index);
void SetBlockIndexFileMetaDataIfNotSet(CBlockIndex& index, CDiskBlockMetaData metadata);
/** Functions for validating blocks and updating the block tree */

/**
 * Context-independent validity checks.
 *
 * Returns true if the provided block is valid (has valid header,
 * transactions are valid, block is a valid size, etc.)
 */
bool CheckBlock(
    const Config &Config, const CBlock &block, CValidationState &state, int blockHeight,
    BlockValidationOptions validationOptions = BlockValidationOptions());

/**
 * Context dependent validity checks for non coinbase transactions. This
 * doesn't check the validity of the transaction against the UTXO set, but
 * simply characteristic that are suceptible to change over time such as feature
 * activation/deactivation and CLTV.
 */
bool ContextualCheckTransaction(const Config &config, const CTransaction &tx,
                                CValidationState &state, int nHeight,
                                int64_t nLockTimeCutoff, bool fromBlock);

/**
 * This is a variant of ContextualCheckTransaction which computes the contextual
 * check for a transaction based on nChainActiveHeight and nMedianTimePast
 * of the active chain tip (including block flags).
 *
 * See consensus/consensus.h for flag definitions.
 *
 * @param config A reference to the configuration
 * @param tx A reference to the transaction
 * @param nChainActiveHeight The maximal height in the current active chain
 * @param nMedianTimePast A median time of the tip for the current active chain
 * @param state A reference to store a validation state
 * @param flags Flags assigned to the block
 */
bool ContextualCheckTransactionForCurrentBlock(
    const Config &config,
    const CTransaction &tx,
    int nChainActiveHeight,
    int nMedianTimePast,
    CValidationState &state,
    int flags = -1);

/**
 * Check a block is completely valid from start to finish (only works on top of
 * our current best block, with cs_main held)
 */
bool TestBlockValidity(
    const Config &config, CValidationState &state, const CBlock &block,
    CBlockIndex *pindexPrev,
    BlockValidationOptions validationOptions = BlockValidationOptions());

/**
 * When there are blocks in the active chain with missing data, rewind the
 * chainstate and remove them from the block index.
 */
bool RewindBlockIndex(const Config &config);

/**
 * RAII wrapper for VerifyDB: Verify consistency of the block and coin
 * databases.
 */
class CVerifyDB {
public:
    CVerifyDB();
    ~CVerifyDB();
    bool VerifyDB(const Config &config, CCoinsView *coinsview, int nCheckLevel,
                  int nCheckDepth, const task::CCancellationToken& shutdownToken);
};

/** Replay blocks that aren't fully applied to the database. */
bool ReplayBlocks(const Config &config, CCoinsView *view);

/** Find the last common block between the parameter chain and a locator. */
CBlockIndex *FindForkInGlobalIndex(const CChain &chain,
                                   const CBlockLocator &locator);

/**
 * Treats a block as if it were received before others with the same work,
 * making it the active chain tip if applicable. Successive calls to
 * PreciousBlock() will override the effects of earlier calls. The effects of
 * calls to PreciousBlock() are not retained across restarts.
 *
 * Returns true if the provided block index successfully became the chain tip.
 */
bool PreciousBlock(const Config &config, CValidationState &state,
                   CBlockIndex *pindex);

/** Mark a block as invalid. */
bool InvalidateBlock(const Config &config, CValidationState &state,
                     CBlockIndex *pindex);

/** Blocks that are in the Config::GetInvalidBlocks() will be marked as invalid.*/
void InvalidateBlocksFromConfig(const Config &config);

/** Remove invalidity status from a block and its descendants. */
bool ResetBlockFailureFlags(CBlockIndex *pindex);

/** The currently-connected chain of blocks (protected by cs_main). */
extern CChain chainActive;
extern CChainActiveSharedData chainActiveSharedData;

/** Global variable that points to the active CCoinsView (protected by cs_main)
 */
extern CCoinsViewCache *pcoinsTip;

/** Global variable that points to the active block tree (protected by cs_main)
 */
extern CBlockTreeDB *pblocktree;

/**
 * Return the MTP and spend height, which is one more than the inputs.GetBestBlock().
 * While checking, GetBestBlock() refers to the parent block. (protected by
 * cs_main)
 * This is also true for mempool checks.
 */
std::pair<int,int> GetSpendHeightAndMTP(const CCoinsViewCache &inputs);

/**
 * Reject codes greater or equal to this can be returned by AcceptToMemPool for
 * transactions, to signal internal conditions. They cannot and should not be
 * sent over the P2P network.
 */
static const unsigned int REJECT_INTERNAL = 0x100;
/** Too high fee. Can not be triggered by P2P transactions */
static const unsigned int REJECT_HIGHFEE = 0x100;
/** Transaction is already known (either in mempool or blockchain) */
static const unsigned int REJECT_ALREADY_KNOWN = 0x101;
/** Transaction conflicts with a transaction already known */
static const unsigned int REJECT_CONFLICT = 0x102;
/** No space for transaction */
static const unsigned int REJECT_MEMPOOL_FULL = 0x103;

/** Dump the mempool to disk. */
void DumpMempool();

/** Load the mempool from disk. */
bool LoadMempool(const Config &config, const task::CCancellationToken& shutdownToken);

/** AlertNotify */
void AlertNotify(const std::string &strMessage);

#endif // BITCOIN_VALIDATION_H
