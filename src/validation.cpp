// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2017-2018 The Bitcoin developers
// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "validation.h"

#include "arith_uint256.h"
#include "async_file_reader.h"
#include "blockstreams.h"
#include "chainparams.h"
#include "checkpoints.h"
#include "checkqueuepool.h"
#include "config.h"
#include "consensus/consensus.h"
#include "consensus/merkle.h"
#include "consensus/validation.h"
#include "fs.h"
#include "hash.h"
#include "init.h"
#include "mining/journal_builder.h"
#include "net.h"
#include "net_processing.h"
#include "netmessagemaker.h"
#include "policy/fees.h"
#include "policy/policy.h"
#include "pow.h"
#include "primitives/block.h"
#include "primitives/transaction.h"
#include "random.h"
#include "script/script.h"
#include "script/scriptcache.h"
#include "script/sigcache.h"
#include "script/standard.h"
#include "taskcancellation.h"
#include "timedata.h"
#include "tinyformat.h"
#include "txdb.h"
#include "txmempool.h"
#include "txn_validator.h"
#include "ui_interface.h"
#include "undo.h"
#include "util.h"
#include "utilmoneystr.h"
#include "utilstrencodings.h"
#include "validationinterface.h"
#include "versionbits.h"
#include "warnings.h"
#include "blockfileinfostore.h"

#include <atomic>
#include <sstream>

#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/math/distributions/poisson.hpp>
#include <boost/range/adaptor/reversed.hpp>
#include <boost/thread.hpp>

#if defined(NDEBUG)
#error "Bitcoin cannot be compiled without assertions."
#endif

using namespace mining;

/**
 * Global state
 */
CCriticalSection cs_main;

BlockMap mapBlockIndex;
CChain chainActive;
CChainActiveSharedData chainActiveSharedData;
CBlockIndex *pindexBestHeader = nullptr;
CWaitableCriticalSection csBestBlock;
CConditionVariable cvBlockChange;
std::atomic_bool fImporting(false);
bool fReindex = false;
bool fTxIndex = false;
bool fHavePruned = false;
bool fPruneMode = false;
bool fIsBareMultisigStd = DEFAULT_PERMIT_BAREMULTISIG;
bool fRequireStandard = true;
bool fCheckBlockIndex = false;
bool fCheckpointsEnabled = DEFAULT_CHECKPOINTS_ENABLED;
size_t nCoinCacheUsage = 5000 * 300;
uint64_t nPruneTarget = 0;
int64_t nMaxTipAge = DEFAULT_MAX_TIP_AGE;

uint256 hashAssumeValid;
arith_uint256 nMinimumChainWork;
CBlockValidationStatus blockValidationStatus;

Amount maxTxFee = DEFAULT_TRANSACTION_MAXFEE;

CTxMemPool mempool;

static void CheckBlockIndex(const Consensus::Params &consensusParams);

/** Constant stuff for coinbase transactions we create: */
CScript COINBASE_FLAGS;

const std::string strMessageMagic = "Bitcoin Signed Message:\n";

// Internal stuff
namespace {

/**
 * Exception used for indicating validation termination (not an error).
 */
class CBlockValidationCancellation : public std::exception
{
public:
    const char* what() const noexcept override
    {
        return "CBlockValidationCancellation";
    };
};

struct CBlockIndexWorkComparator {
    bool operator()(const CBlockIndex *pa, const CBlockIndex *pb) const {
        // First sort by most total work, ...
        if (pa->nChainWork > pb->nChainWork) {
            return false;
        }
        if (pa->nChainWork < pb->nChainWork) {
            return true;
        }

        // ... then by when block was completely validated, ...
        if (pa->GetValidationCompletionTime() < pb->GetValidationCompletionTime()) {
            return false;
        }
        if (pa->GetValidationCompletionTime() > pb->GetValidationCompletionTime()) {
            return true;
        }

        // ... then by earliest time received, ...
        if (pa->nSequenceId < pb->nSequenceId) {
            return false;
        }
        if (pa->nSequenceId > pb->nSequenceId) {
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

/**
 * Class for counting the amount of blocks being processed.
 */
class CBlockProcessing
{
public:
    CBlockProcessing() = delete;

    /** Get a scope guard that adds one block to the count. */
    static std::shared_ptr<std::atomic<int>> GetCountGuard()
    {
        ++mCount;
        return {&mCount, decrement};
    }

    /** Get the number of blocks being processed/waiting for processing. */
    static int Count() {return mCount;}

private:
    static void decrement(std::atomic<int>* counter)
    {
        --(*counter);
    }

    inline static std::atomic<int> mCount{0};
};

CBlockIndex *pindexBestInvalid;

/**
 * The set of all CBlockIndex entries with BLOCK_VALID_TRANSACTIONS (for itself
 * and all ancestors) and as good as our current tip or better. Entries may be
 * failed, though, and pruning nodes may be missing the data for the block.
 */
std::set<CBlockIndex *, CBlockIndexWorkComparator> setBlockIndexCandidates;
/**
 * All pairs A->B, where A (or one of its ancestors) misses transactions, but B
 * has transactions. Pruned nodes may have entries where B is missing data.
 */
std::multimap<CBlockIndex *, CBlockIndex *> mapBlocksUnlinked;





/**
 * Global flag to indicate we should check to see if there are block/undo files
 * that should be deleted. Set on startup or if we allocate more file space when
 * we're in prune mode.
 */
bool fCheckForPruning = false;

/**
 * Every received block is assigned a unique and increasing identifier, so we
 * know which one to give priority in case of a fork.
 */
CCriticalSection cs_nBlockSequenceId;
/** Blocks loaded from disk are assigned id 0, so start the counter at 1. */
int32_t nBlockSequenceId = 1;
/** Decreasing counter (used by subsequent preciousblock calls). */
int32_t nBlockReverseSequenceId = -1;
/** chainwork for the last block that preciousblock has been applied to. */
arith_uint256 nLastPreciousChainwork = 0;

/** Dirty block index entries. */
std::set<CBlockIndex *> setDirtyBlockIndex;

} // namespace

CBlockIndex *FindForkInGlobalIndex(const CChain &chain,
                                   const CBlockLocator &locator) {
    // Find the first block the caller has in the main chain
    for (const uint256 &hash : locator.vHave) {
        BlockMap::iterator mi = mapBlockIndex.find(hash);
        if (mi != mapBlockIndex.end()) {
            CBlockIndex *pindex = (*mi).second;
            if (chain.Contains(pindex)) {
                return pindex;
            }
            if (pindex->GetAncestor(chain.Height()) == chain.Tip()) {
                return chain.Tip();
            }
        }
    }
    return chain.Genesis();
}

CCoinsViewCache *pcoinsTip = nullptr;
CBlockTreeDB *pblocktree = nullptr;

static uint32_t GetBlockScriptFlags(const Config &config,
                                    const CBlockIndex *pChainTip);

/**
 * Test whether the given transaction is final for the given height and time.
 */
bool IsFinalTx(const CTransaction &tx, int nBlockHeight, int64_t nBlockTime)
{
    if (tx.nLockTime == 0) {
        return true;
    }

    int64_t lockTime = tx.nLockTime;
    int64_t lockTimeLimit =
        (lockTime < LOCKTIME_THRESHOLD) ? nBlockHeight : nBlockTime;
    if (lockTime < lockTimeLimit) {
        return true;
    }

    for (const auto &txin : tx.vin) {
        if (txin.nSequence != CTxIn::SEQUENCE_FINAL) {
            return false;
        }
    }

    return true;
}

/**
 * Calculates the block height and previous block's median time past at
 * which the transaction will be considered final in the context of BIP 68.
 * Also removes from the vector of input heights any entries which did not
 * correspond to sequence locked inputs as they do not affect the calculation.
 */
static std::pair<int, int64_t>
CalculateSequenceLocks(const CTransaction &tx, int flags,
                       std::vector<int> *prevHeights,
                       const CBlockIndex &block) {
    assert(prevHeights->size() == tx.vin.size());

    // Will be set to the equivalent height- and time-based nLockTime
    // values that would be necessary to satisfy all relative lock-
    // time constraints given our view of block chain history.
    // The semantics of nLockTime are the last invalid height/time, so
    // use -1 to have the effect of any height or time being valid.
    int nMinHeight = -1;
    int64_t nMinTime = -1;

    // tx.nVersion is signed integer so requires cast to unsigned otherwise
    // we would be doing a signed comparison and half the range of nVersion
    // wouldn't support BIP 68.
    bool fEnforceBIP68 = static_cast<uint32_t>(tx.nVersion) >= 2 &&
                         flags & LOCKTIME_VERIFY_SEQUENCE;

    // Do not enforce sequence numbers as a relative lock time
    // unless we have been instructed to
    if (!fEnforceBIP68) {
        return std::make_pair(nMinHeight, nMinTime);
    }

    for (size_t txinIndex = 0; txinIndex < tx.vin.size(); txinIndex++) {
        const CTxIn &txin = tx.vin[txinIndex];

        // Sequence numbers with the most significant bit set are not
        // treated as relative lock-times, nor are they given any
        // consensus-enforced meaning at this point.
        if (txin.nSequence & CTxIn::SEQUENCE_LOCKTIME_DISABLE_FLAG) {
            // The height of this input is not relevant for sequence locks
            (*prevHeights)[txinIndex] = 0;
            continue;
        }

        int nCoinHeight = (*prevHeights)[txinIndex];

        if (txin.nSequence & CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG) {
            int64_t nCoinTime = block.GetAncestor(std::max(nCoinHeight - 1, 0))
                                    ->GetMedianTimePast();
            // NOTE: Subtract 1 to maintain nLockTime semantics.
            // BIP 68 relative lock times have the semantics of calculating the
            // first block or time at which the transaction would be valid. When
            // calculating the effective block time or height for the entire
            // transaction, we switch to using the semantics of nLockTime which
            // is the last invalid block time or height. Thus we subtract 1 from
            // the calculated time or height.

            // Time-based relative lock-times are measured from the smallest
            // allowed timestamp of the block containing the txout being spent,
            // which is the median time past of the block prior.
            nMinTime = std::max(
                nMinTime,
                nCoinTime +
                    (int64_t)((txin.nSequence & CTxIn::SEQUENCE_LOCKTIME_MASK)
                              << CTxIn::SEQUENCE_LOCKTIME_GRANULARITY) -
                    1);
        } else {
            nMinHeight = std::max(
                nMinHeight,
                nCoinHeight +
                    (int)(txin.nSequence & CTxIn::SEQUENCE_LOCKTIME_MASK) - 1);
        }
    }

    return std::make_pair(nMinHeight, nMinTime);
}

static bool EvaluateSequenceLocks(const CBlockIndex &block,
                                  std::pair<int, int64_t> lockPair) {
    assert(block.pprev);
    int64_t nBlockTime = block.pprev->GetMedianTimePast();
    if (lockPair.first >= block.nHeight || lockPair.second >= nBlockTime) {
        return false;
    }

    return true;
}

bool SequenceLocks(const CTransaction &tx, int flags,
                   std::vector<int> *prevHeights, const CBlockIndex &block) {
    return EvaluateSequenceLocks(
        block, CalculateSequenceLocks(tx, flags, prevHeights, block));
}

bool TestLockPointValidity(const LockPoints *lp) {
    AssertLockHeld(cs_main);
    assert(lp);
    // If there are relative lock times then the maxInputBlock will be set
    // If there are no relative lock times, the LockPoints don't depend on the
    // chain
    if (lp->maxInputBlock) {
        // Check whether chainActive is an extension of the block at which the
        // LockPoints
        // calculation was valid.  If not LockPoints are no longer valid
        if (!chainActive.Contains(lp->maxInputBlock)) {
            return false;
        }
    }

    // LockPoints still valid
    return true;
}

bool CheckSequenceLocks(
    const CTransaction &tx,
    const CTxMemPool &pool,
    const Config& config,
    int flags,
    LockPoints *lp,
    bool useExistingLockPoints) {

    // cs_main is held by TxnValidator during TxnValidation call.
    // pool.smtx is held by TxnValidation method. It is required for viewMemPool::GetCoin()
    CBlockIndex *tip = chainActive.Tip();

    // Post-genesis we don't care about the old sequence lock calculations
    if(IsGenesisEnabled(config, tip->nHeight))
    {
        return true;
    }

    CBlockIndex index;
    index.pprev = tip;
    // CheckSequenceLocks() uses chainActive.Height()+1 to evaluate height based
    // locks because when SequenceLocks() is called within ConnectBlock(), the
    // height of the block *being* evaluated is what is used. Thus if we want to
    // know if a transaction can be part of the *next* block, we need to use one
    // more than chainActive.Height()
    index.nHeight = tip->nHeight + 1;

    std::pair<int, int64_t> lockPair;
    if (useExistingLockPoints) {
        assert(lp);
        lockPair.first = lp->height;
        lockPair.second = lp->time;
    } else {
        // pcoinsTip contains the UTXO set for chainActive.Tip()
        CCoinsViewMemPool viewMemPool(pcoinsTip, pool);
        std::vector<int> prevheights;
        prevheights.resize(tx.vin.size());
        for (size_t txinIndex = 0; txinIndex < tx.vin.size(); txinIndex++) {
            const CTxIn &txin = tx.vin[txinIndex];
            Coin coin;
            if (!viewMemPool.GetCoin(txin.prevout, coin)) {
                return error("%s: Missing input", __func__);
            }
            if (coin.GetHeight() == MEMPOOL_HEIGHT) {
                // Assume all mempool transaction confirm in the next block
                prevheights[txinIndex] = tip->nHeight + 1;
            } else {
                prevheights[txinIndex] = coin.GetHeight();
            }
        }
        lockPair = CalculateSequenceLocks(tx, flags, &prevheights, index);
        if (lp) {
            lp->height = lockPair.first;
            lp->time = lockPair.second;
            // Also store the hash of the block with the highest height of all
            // the blocks which have sequence locked prevouts. This hash needs
            // to still be on the chain for these LockPoint calculations to be
            // valid.
            // Note: It is impossible to correctly calculate a maxInputBlock if
            // any of the sequence locked inputs depend on unconfirmed txs,
            // except in the special case where the relative lock time/height is
            // 0, which is equivalent to no sequence lock. Since we assume input
            // height of tip+1 for mempool txs and test the resulting lockPair
            // from CalculateSequenceLocks against tip+1. We know
            // EvaluateSequenceLocks will fail if there was a non-zero sequence
            // lock on a mempool input, so we can use the return value of
            // CheckSequenceLocks to indicate the LockPoints validity
            int maxInputHeight = 0;
            for (int height : prevheights) {
                // Can ignore mempool inputs since we'll fail if they had
                // non-zero locks
                if (height != tip->nHeight + 1) {
                    maxInputHeight = std::max(maxInputHeight, height);
                }
            }
            lp->maxInputBlock = tip->GetAncestor(maxInputHeight);
        }
    }
    return EvaluateSequenceLocks(index, lockPair);
}

uint64_t GetSigOpCountWithoutP2SH(const CTransaction &tx, bool isGenesisEnabled, bool& sigOpCountError) 
{
    sigOpCountError = false;
    uint64_t nSigOps = 0;
    for (const auto &txin : tx.vin) 
    {
        // After Genesis, this should return 0, since only push data is allowed in input scripts:
        nSigOps += txin.scriptSig.GetSigOpCount(false, isGenesisEnabled, sigOpCountError);
        if (sigOpCountError)
        {
            return nSigOps;
        }
    }

    for (const auto &txout : tx.vout) 
    {
        nSigOps += txout.scriptPubKey.GetSigOpCount(false, isGenesisEnabled, sigOpCountError);
        if (sigOpCountError)
        {
            return nSigOps;
        }
    }
    return nSigOps;
}

uint64_t GetP2SHSigOpCount(const Config &config,
                           const CTransaction &tx,
                           const CCoinsViewCache &inputs,
                           bool& sigOpCountError) {
    sigOpCountError = false;
    if (tx.IsCoinBase()) {
        return 0;
    }

    uint64_t nSigOps = 0;
    for (auto &i : tx.vin) {
        const Coin &coin = inputs.AccessCoin(i.prevout);
        assert(!coin.IsSpent());

        bool genesisEnabled = true;
        if (coin.GetHeight() != MEMPOOL_HEIGHT){
            genesisEnabled = IsGenesisEnabled(config, coin.GetHeight());
        } else {
            // When this function is called from CTxnValidator, we could be spending UTXOS from mempool
            // CTxnValidator already takes cs_main lock when starting validation in it's main thread
            AssertLockHeld(cs_main);

            // TODO: in releases after genesis upgrade this part could be removed
            genesisEnabled = IsGenesisEnabled(config, chainActive.Height() + 1);
        }

        if (genesisEnabled) {
            continue;
        }
        const CTxOut &prevout = coin.GetTxOut();
        if (prevout.scriptPubKey.IsPayToScriptHash()) {
            nSigOps += prevout.scriptPubKey.GetSigOpCount(i.scriptSig, genesisEnabled, sigOpCountError);
            if (sigOpCountError) {
                return nSigOps;
            }
        }
    }
    return nSigOps;
}

uint64_t GetTransactionSigOpCount(const Config &config, 
                                  const CTransaction &tx,
                                  const CCoinsViewCache &inputs, 
                                  bool checkP2SH,
                                  bool isGenesisEnabled, 
                                  bool& sigOpCountError) {
    sigOpCountError = false;
    uint64_t nSigOps = GetSigOpCountWithoutP2SH(tx, isGenesisEnabled, sigOpCountError);
    if (sigOpCountError) {
        return nSigOps;
    }

    if (tx.IsCoinBase()) {
        return nSigOps;
    }

    if (checkP2SH) {
        nSigOps += GetP2SHSigOpCount(config, tx, inputs, sigOpCountError);
    }

    return nSigOps;
}

static bool CheckTransactionCommon(const CTransaction& tx,
                                   CValidationState& state,
                                   uint64_t maxTxSigOpsCountConsensusBeforeGenesis,
                                   uint64_t maxTxSizeConsensus,
                                   bool isGenesisEnabled) {
    // Basic checks that don't depend on any context
    if (tx.vin.empty()) {
        return state.DoS(10, false, REJECT_INVALID, "bad-txns-vin-empty");
    }

    if (tx.vout.empty()) {
        return state.DoS(10, false, REJECT_INVALID, "bad-txns-vout-empty");
    }

    // Size limit
    if (::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION) > maxTxSizeConsensus) {
        return state.DoS(100, false, REJECT_INVALID, "bad-txns-oversize");
    }

    // Check for negative or overflow output values
    Amount nValueOut(0);
    for (const auto &txout : tx.vout) {
        if (txout.nValue < Amount(0)) {
            return state.DoS(100, false, REJECT_INVALID,
                             "bad-txns-vout-negative");
        }

        if (txout.nValue > MAX_MONEY) {
            return state.DoS(100, false, REJECT_INVALID,
                             "bad-txns-vout-toolarge");
        }

        nValueOut += txout.nValue;
        if (!MoneyRange(nValueOut)) {
            return state.DoS(100, false, REJECT_INVALID,
                             "bad-txns-txouttotal-toolarge");
        }
    }

    // No need to count sigops after Genesis, because sigops are unlimited
    if (!isGenesisEnabled) {
        bool sigOpCountError;
        uint64_t nSigOpCount = GetSigOpCountWithoutP2SH(tx, isGenesisEnabled, sigOpCountError);
        if (sigOpCountError || nSigOpCount > maxTxSigOpsCountConsensusBeforeGenesis) {
            return state.DoS(100, false, REJECT_INVALID, "bad-txn-sigops");
        }
    }

    return true;
}

bool CheckCoinbase(const CTransaction& tx, CValidationState& state, uint64_t maxTxSigOpsCountConsensusBeforeGenesis, uint64_t maxTxSizeConsensus, bool isGenesisEnabled)
{
    if (!tx.IsCoinBase()) {
        return state.DoS(100, false, REJECT_INVALID, "bad-cb-missing", false,
                         "first tx is not coinbase");
    }

    if (!CheckTransactionCommon(tx, state, maxTxSigOpsCountConsensusBeforeGenesis, maxTxSizeConsensus, isGenesisEnabled)) {
        // CheckTransactionCommon fill in the state.
        return false;
    }

    if (tx.vin[0].scriptSig.size() < 2 || tx.vin[0].scriptSig.size() > 100) {
        return state.DoS(100, false, REJECT_INVALID, "bad-cb-length");
    }

    return true;
}

bool CheckRegularTransaction(const CTransaction &tx, CValidationState &state, uint64_t maxTxSigOpsCountConsensusBeforeGenesis, uint64_t maxTxSizeConsensus, bool isGenesisEnabled) {
    if (tx.IsCoinBase()) {
        return state.DoS(100, false, REJECT_INVALID, "bad-tx-coinbase");
    }
    
    if (!CheckTransactionCommon(tx, state, maxTxSigOpsCountConsensusBeforeGenesis, maxTxSizeConsensus, isGenesisEnabled)) {
        // CheckTransactionCommon fill in the state.
        return false;
    }

    if (isGenesisEnabled)
    {
        bool hasP2SHOutput = std::any_of(tx.vout.begin(), tx.vout.end(), 
            [](const CTxOut& o){ 
                return o.scriptPubKey.IsPayToScriptHash(); 
            }
        );
        
        if(hasP2SHOutput)
        {
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-vout-p2sh");
        }

    }

    static SaltedOutpointHasher hasher {};
    std::unordered_set<COutPoint, SaltedOutpointHasher> inOutPoints { 1, hasher };
    for (const auto &txin : tx.vin) {
        if (txin.prevout.IsNull()) {
            return state.DoS(10, false, REJECT_INVALID,
                             "bad-txns-prevout-null");
        }

        if (!inOutPoints.insert(txin.prevout).second) {
            return state.DoS(100, false, REJECT_INVALID,
                             "bad-txns-inputs-duplicate");
        }
    }

    return true;
}

/** Convert CValidationState to a human-readable message for logging */
std::string FormatStateMessage(const CValidationState &state) {
    return strprintf(
        "%s%s (code %i)", state.GetRejectReason(),
        state.GetDebugMessage().empty() ? "" : ", " + state.GetDebugMessage(),
        state.GetRejectCode());
}

static bool IsUAHFenabled(const Config &config, int nHeight) {
    return nHeight >= config.GetChainParams().GetConsensus().uahfHeight;
}

bool IsUAHFenabled(const Config &config, const CBlockIndex *pindexPrev) {
    if (pindexPrev == nullptr) {
        return false;
    }

    return IsUAHFenabled(config, pindexPrev->nHeight);
}

static bool IsDAAEnabled(const Config &config, int nHeight) {
    return nHeight >= config.GetChainParams().GetConsensus().daaHeight;
}

bool IsDAAEnabled(const Config &config, const CBlockIndex *pindexPrev) {
    if (pindexPrev == nullptr) {
        return false;
    }

    return IsDAAEnabled(config, pindexPrev->nHeight);
}

bool IsGenesisEnabled(const Config &config, int nHeight) {
    if (nHeight == MEMPOOL_HEIGHT) {
        throw std::runtime_error("A coin with height == MEMPOOL_HEIGHT was passed "
            "to IsGenesisEnabled() overload that does not handle this case. "
            "Use the overload that takes Coin as parameter");
    }

    return (uint64_t)nHeight >= config.GetGenesisActivationHeight();
}

bool IsGenesisEnabled(const Config& config, const Coin& coin, int mempoolHeight) {
    auto height = coin.GetHeight();
    if (height == MEMPOOL_HEIGHT) {
        return (uint32_t)mempoolHeight >= config.GetGenesisActivationHeight();
    }
    return height >= config.GetGenesisActivationHeight();
}

bool IsGenesisEnabled(const Config &config, const CBlockIndex* pindexPrev) {
    if (pindexPrev == nullptr) {
        return false;
    }

    // Genesis is enabled on the currently processed block, not on the current tip.
    return IsGenesisEnabled(config, pindexPrev->nHeight + 1);
}

// Used to avoid mempool polluting consensus critical paths if CCoinsViewMempool
// were somehow broken and returning the wrong scriptPubKeys.
//
// The function is only called by TxnValidation.
// TxnValidation is called by the Validator which holds cs_main lock during a call.
// view is constructed as local variable (by TxnValidation), populated and then disconnected from backing view,
// so that it can not be shared by other threads.
// Mt support is present in CCoinsViewCache class.
static std::optional<bool> CheckInputsFromMempoolAndCache(
    const task::CCancellationToken& token,
    const Config& config,
    const CTransaction& tx,
    CValidationState& state,
    const CCoinsViewCache* pcoinsTip,
    const CCoinsViewCache& view,
    CTxMemPool& pool,
    const uint32_t flags,
    bool cacheSigStore,
    PrecomputedTransactionData& txdata) {

    // Take the mempool lock to enforce that the mempool doesn't change
    // between when we check the view and when we actually call through to CheckInputs
    std::shared_lock lock(pool.smtx);

    assert(!tx.IsCoinBase());
    for (const CTxIn &txin : tx.vin) {
        const Coin &coin = view.AccessCoin(txin.prevout);
        // At this point we haven't actually checked if the coins are all
        // available (or shouldn't assume we have, since CheckInputs does). So
        // we just return failure if the inputs are not available here, and then
        // only have to check equivalence for available inputs.
        if (coin.IsSpent()) {
            return false;
        }
        const CTransactionRef &txFrom = pool.GetNL(txin.prevout.GetTxId());
        if (txFrom) {
            assert(txFrom->GetHash() == txin.prevout.GetTxId());
            assert(txFrom->vout.size() > txin.prevout.GetN());
            assert(txFrom->vout[txin.prevout.GetN()] == coin.GetTxOut());
        } else {
            const Coin &coinFromDisk = pcoinsTip->AccessCoin(txin.prevout);
            assert(!coinFromDisk.IsSpent());
            assert(coinFromDisk.GetTxOut() == coin.GetTxOut());
        }
    }

    // For Consenus parameter false is used because we already use policy rules in first CheckInputs call
    // from TxnValidation function that is called before this one, and if that call succeeds then we 
    // can use policy rules again but with different flags now
    return CheckInputs(
                token,
                config,
                false,          
                tx,
                state,
                view,
                true,           /* fScriptChecks */
                flags,
                cacheSigStore,  /* sigCacheStore */
                true,           /* scriptCacheStore */
                txdata);
}

static bool CheckTxOutputs(
    const CTransaction &tx,
    const CCoinsViewCache* pcoinsTip,
    const CCoinsViewCache& view,
    std::vector<COutPoint> &vCoinsToUncache) {
    if (!pcoinsTip) {
        throw std::runtime_error("UTXO cache is not initialized.");
    }
    const TxId txid = tx.GetId();
    // Do we already have it?
    for (size_t out = 0; out < tx.vout.size(); out++) {
        COutPoint outpoint(txid, out);
        bool had_coin_in_cache = pcoinsTip->HaveCoinInCache(outpoint);
        // Check if outpoint present in the mempool
        if (view.HaveCoin(outpoint)) {
            // Check if outpoint available as a UTXO tx.
            if (!had_coin_in_cache) {
                vCoinsToUncache.push_back(outpoint);
            }
            return false;
        }
    }
    return true;
}

static bool CheckTxInputExists(
    const CTransaction &tx,
    const CCoinsViewCache* pcoinsTip,
    const CCoinsViewCache& view,
    CValidationState &state,
    std::vector<COutPoint> &vCoinsToUncache) {
    // Do all inputs exist?
    for (const CTxIn& txin : tx.vin) {
        // Check if txin.prevout available as a UTXO tx.
        if (!pcoinsTip->HaveCoinInCache(txin.prevout)) {
            vCoinsToUncache.push_back(txin.prevout);
        }
        // Check if txin.prevout is not present in the mempool
        if (!view.HaveCoin(txin.prevout)) {
            state.SetMissingInputs();
            return state.Invalid();
        }
    }
    return true;
}

static bool IsAbsurdlyHighFeeSetForTxn(
    const Amount& nFees,
    const Amount& nAbsurdFee) {
    // Check a condition for txn's absurdly hight fee
    return !(nAbsurdFee != Amount(0) && nFees > nAbsurdFee);
}

static bool CheckTxSpendsCoinbase(
    const CTransaction &tx,
    const CCoinsViewCache& view) {
    // Keep track of transactions that spend a coinbase, which we re-scan
    // during reorgs to ensure COINBASE_MATURITY is still met.
    for (const CTxIn &txin : tx.vin) {
        const Coin &coin = view.AccessCoin(txin.prevout);
        if (coin.IsCoinBase()) {
            return true;
        }
    }
    return false;
}

static Amount GetMempoolRejectFee(
    const CTxMemPool &pool,
    unsigned int nTxSize) {
    // Get mempool reject fee
    return pool.GetMinFee(
                gArgs.GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) *
                1000000)
            .GetFee(nTxSize);
}

static bool CheckMempoolMinFee(
    const Amount& nModifiedFees,
    const Amount& nMempoolRejectFee) {
    // Check mempool minimal fee requirement.
    if (nMempoolRejectFee > Amount(0) &&
        nModifiedFees < nMempoolRejectFee) {
        return false;
    }
    return true;
}

static bool CheckTxRelayPriority(
    const Amount& nModifiedFees,
    const CFeeRate& minRelayTxFee,
    const CTxMemPoolEntry& pMempoolEntry,
    unsigned int nTxSize) {
    // Check txn relay priority
    if (gArgs.GetBoolArg("-relaypriority", DEFAULT_RELAYPRIORITY) &&
        nModifiedFees < minRelayTxFee.GetFee(nTxSize) &&
        !AllowFree(pMempoolEntry.GetPriority(chainActive.Height() + 1))) {
        // Require that free transactions have sufficient priority to be
        // mined in the next block.
        return false;
    }
    return true;
}

static bool CheckLimitFreeTx(
    bool fLimitFree,
    const Amount& nModifiedFees,
    const CFeeRate& minRelayTxFee,
    unsigned int nTxSize) {
    // Continuously rate-limit free (really, very-low-fee) transactions.
    // This mitigates 'penny-flooding' -- sending thousands of free
    // transactions just to be annoying or make others' transactions take
    // longer to confirm.
    if (!(fLimitFree && nModifiedFees < minRelayTxFee.GetFee(nTxSize))) {
        return true;
    }
    static CCriticalSection csFreeLimiter;
    static double dFreeCount;
    static int64_t nLastTime;
    int64_t nNow = GetTime();

    LOCK(csFreeLimiter);

    // Use an exponentially decaying ~10-minute window:
    dFreeCount *= pow(1.0 - 1.0 / 600.0, double(nNow - nLastTime));
    nLastTime = nNow;
    // -limitfreerelay unit is thousand-bytes-per-minute
    // At default rate it would take over a month to fill 1GB
    if (dFreeCount + nTxSize >=
        gArgs.GetArg("-limitfreerelay", DEFAULT_LIMITFREERELAY) * 10 *
            1000) {
        return false;
    }

    LogPrint(BCLog::MEMPOOL, "Rate limit dFreeCount: %g => %g\n",
             dFreeCount, dFreeCount + nTxSize);
    dFreeCount += nTxSize;
    return true;
}

static bool CalculateMempoolAncestors(
    const CTxMemPool &pool,
    const CTxMemPoolEntry& pMempoolEntry,
    CTxMemPool::setEntries& setAncestors,
    std::string& errString) {
    // Calculate in-mempool ancestors, up to a limit.
    size_t nLimitAncestors = GlobalConfig::GetConfig().GetLimitAncestorCount();
    size_t nLimitAncestorSize = GlobalConfig::GetConfig().GetLimitAncestorSize();
    size_t nLimitDescendants = GlobalConfig::GetConfig().GetLimitDescendantCount();
    size_t nLimitDescendantSize = GlobalConfig::GetConfig().GetLimitDescendantSize();
    if (!pool.CalculateMemPoolAncestors(pMempoolEntry,
                setAncestors,
                nLimitAncestors,
                nLimitAncestorSize,
                nLimitDescendants,
                nLimitDescendantSize,
                errString)) {
        return false;
    }
    return true;
}

static uint32_t GetScriptVerifyFlags(const Config &config, bool genesisEnabled) {
    // Check inputs based on the set of flags we activate.
    uint32_t scriptVerifyFlags = StandardScriptVerifyFlags(genesisEnabled, false);
    if (!config.GetChainParams().RequireStandard()) {
        scriptVerifyFlags =
            SCRIPT_ENABLE_SIGHASH_FORKID |
            gArgs.GetArg("-promiscuousmempoolflags", scriptVerifyFlags);
    }
    // Make sure whatever we need to activate is actually activated.
    return scriptVerifyFlags;
}

size_t GetNumLowPriorityValidationThrs(size_t nTestingHCValue) {
    size_t numHardwareThrs {0};
	// nTestingHCValue used by UTs
	if (nTestingHCValue == SIZE_MAX) {
		numHardwareThrs = std::thread::hardware_concurrency();
	} else {
		numHardwareThrs = nTestingHCValue;
	}
    // Calculate a number of low priority threads
    if (numHardwareThrs < 4) {
        return 1;
    }
    return static_cast<size_t>(numHardwareThrs * 0.25);
}

size_t GetNumHighPriorityValidationThrs(size_t nTestingHCValue) {
	size_t numHardwareThrs {0};
	// nTestingHCValue used by UTs
	if (nTestingHCValue == SIZE_MAX) {
		numHardwareThrs = std::thread::hardware_concurrency();
	} else {
		numHardwareThrs = nTestingHCValue;
	}
	// Calculate number of high priority threads
	if (!numHardwareThrs || numHardwareThrs == 1) {
		return 1;
	}
    return numHardwareThrs - GetNumLowPriorityValidationThrs(numHardwareThrs);
}

std::vector<TxId> LimitMempoolSize(
    CTxMemPool &pool,
    const CJournalChangeSetPtr& changeSet,
    size_t limit,
    unsigned long age) {

    int expired = pool.Expire(GetTime() - age, changeSet);
    if (expired != 0) {
        LogPrint(BCLog::MEMPOOL,
                 "Expired %i transactions from the memory pool\n", expired);
    }

    std::vector<COutPoint> vNoSpendsRemaining;
    std::vector<TxId> vRemovedTxIds = pool.TrimToSize(
                                                limit,
                                                changeSet,
                                                &vNoSpendsRemaining);
    pcoinsTip->Uncache(vNoSpendsRemaining);
    return vRemovedTxIds;
}

void CommitTxToMempool(
    const CTransactionRef &ptx,
    const CTxMemPoolEntry& pMempoolEntry,
    CTxMemPool::setEntries& setAncestors,
    CTxMemPool& pool,
    CValidationState& state,
    const CJournalChangeSetPtr& changeSet,
    bool fLimitMempoolSize,
    size_t* pnMempoolSize,
    size_t* pnDynamicMemoryUsage) {

    const CTransaction &tx = *ptx;
    const TxId txid = tx.GetId();

    // Post-genesis, non-final txns have their own mempool
    if(state.IsNonFinal() || pool.getNonFinalPool().finalisesExistingTransaction(ptx))
    {
        // Post-genesis, non-final txns have their own mempool
        TxMempoolInfo info { pMempoolEntry };
        pool.getNonFinalPool().addOrUpdateTransaction(info, state);
        return;
    }

    // Store transaction in the mempool.
    pool.AddUnchecked(
            txid,
            pMempoolEntry,
            setAncestors,
            changeSet,
            pnMempoolSize,
            pnDynamicMemoryUsage);
    // Check if the mempool size needs to be limited.
    if (fLimitMempoolSize) {
        // Trim mempool and check if tx was trimmed.
        LimitMempoolSize(
            pool,
            changeSet,
            gArgs.GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * 1000000,
            gArgs.GetArg("-mempoolexpiry", DEFAULT_MEMPOOL_EXPIRY) * 60 * 60);
        if (!pool.Exists(txid)) {
            state.DoS(0, false, REJECT_INSUFFICIENTFEE,
                     "mempool full");
            return;
        }
    }
}

// Does the given non-final txn spend another non-final txn?
static bool DoesNonFinalSpendNonFinal(const CTransaction& txn)
{
    for(const CTxIn& txin : txn.vin)
    {
        if(mempool.getNonFinalPool().exists(txin.prevout.GetTxId()))
        {
            return true;
        }
    }

    return false;
}

static bool IsGenesisGracefulPeriod(const Config& config, int spendHeight)
{
    uint64_t uSpendHeight = static_cast<uint64_t>(spendHeight);
    if (((config.GetGenesisActivationHeight() - config.GetGenesisGracefulPeriod()) < uSpendHeight) &&
        ((config.GetGenesisActivationHeight() + config.GetGenesisGracefulPeriod()) > uSpendHeight))
    {
        return true;
    }
    return false;
}

CTxnValResult TxnValidation(
    const TxInputDataSPtr& pTxInputData,
    const Config& config,
    CTxMemPool& pool,
    TxnDoubleSpendDetectorSPtr dsDetector,
    bool fUseLimits) {

    using Result = CTxnValResult;

    const CTransactionRef& ptx = pTxInputData->mpTx;
    const CTransaction &tx = *ptx;
    const TxId txid = tx.GetId();
    const bool fLimitFree = pTxInputData->mfLimitFree;
    const int64_t nAcceptTime = pTxInputData->mnAcceptTime;
    const Amount nAbsurdFee = pTxInputData->mnAbsurdFee;

    CValidationState state;
    std::vector<COutPoint> vCoinsToUncache {};

    // First check against consensus limits. If this check fails, then banscore will be increased. 
    // We re-test the transaction with policy rules later in this method (without banning if rules are violated)
    bool isGenesisEnabled = IsGenesisEnabled(config, chainActive.Height() + 1);
    uint64_t maxTxSigOpsCountConsensusBeforeGenesis = config.GetMaxTxSigOpsCountConsensusBeforeGenesis();
    uint64_t maxTxSizeConsensus = config.GetMaxTxSize(isGenesisEnabled, true);
    // Coinbase is only valid in a block, not as a loose transaction.
    if (!CheckRegularTransaction(tx, state, maxTxSigOpsCountConsensusBeforeGenesis, maxTxSizeConsensus, isGenesisEnabled)) 
    {
        // We will re-check the transaction if we are in Genesis gracefull period, to check if genesis rules would 
        // allow this script transaction to be accepted. If it is valid under Genesis rules, we only reject it
        // without adding banscore
        bool isGenesisGracefulPeriod = IsGenesisGracefulPeriod(config, chainActive.Height() + 1);
        if (isGenesisGracefulPeriod)
        {
            uint64_t maxTxSizeGraceful = config.GetMaxTxSize(!isGenesisEnabled, true);

            CValidationState genesisState;
            if (CheckRegularTransaction(tx, genesisState, maxTxSigOpsCountConsensusBeforeGenesis, maxTxSizeGraceful, !isGenesisEnabled))
            {
                genesisState.DoS(0, false, REJECT_INVALID, "flexible-" + state.GetRejectReason());
                return Result{ genesisState, pTxInputData };
            }
            else
            {
                genesisState.DoS(state.GetNDoS(), false, REJECT_INVALID, state.GetRejectReason());
                return Result{ genesisState, pTxInputData };
            }
        }
        else
        {
            // Not in Genesis grace period, so return original failure reason
            return Result{state, pTxInputData};
        }
    }

    // Rather not work on nonstandard transactions (unless -testnet/-regtest)
    // We determine if a transaction is standard or not based on assumption that
    // it will be mined in the next block. We accept the fact that it might get mined
    // into a later block and thus can become non standard transaction. 
    // Example: Transaction containing output with "OP_RETURN" and 0 value 
    //          is not dust under old rules, but it is dust under new rules,
    //          but we will mine it nevertheless. Anyone can collect such
    //          coin by providing OP_1 unlock script
    std::string reason;
    bool fStandard = IsStandardTx(config, tx, chainActive.Height() + 1, reason);
    if (fStandard) {
        state.SetStandardTx();
    }
    // Set txn validation timeout if required.
    auto source =
        fUseLimits ?
            task::CTimedCancellationSource::Make(
                (TxValidationPriority::high == pTxInputData->mTxValidationPriority ||
                 TxValidationPriority::normal == pTxInputData->mTxValidationPriority)
                    ? config.GetMaxStdTxnValidationDuration() : config.GetMaxNonStdTxnValidationDuration())
            : task::CCancellationSource::Make();

    bool acceptNonStandardOutput = config.GetAcceptNonStandardOutput(isGenesisEnabled);
    if(!fStandard)
    {
        if (!acceptNonStandardOutput ||
            (isGenesisEnabled && fRequireStandard && reason != "scriptpubkey"))
        {
            state.DoS(0, false, REJECT_NONSTANDARD,
                      reason);
            return Result{state, pTxInputData};
        }
    }

    // Only accept nLockTime-using transactions that can be mined in the next
    // block; we don't want our mempool filled up with transactions that can't
    // be mined yet.
    CValidationState ctxState;
    bool isFinal = true;
    unsigned int lockTimeFlags;
    {
        const CBlockIndex* tip = chainActive.Tip();
        int height { tip->nHeight };
        lockTimeFlags = StandardNonFinalVerifyFlags(IsGenesisEnabled(config, height));
        ContextualCheckTransactionForCurrentBlock(config, tx, height, tip->GetMedianTimePast(),
            ctxState, lockTimeFlags);
        if(ctxState.IsNonFinal() || ctxState.IsInvalid()) {
            if(ctxState.IsInvalid()) {
                // We copy the state from a dummy to ensure we don't increase the
                // ban score of peer for transaction that could be valid in the future.
                state.DoS(0, false, REJECT_NONSTANDARD,
                          ctxState.GetRejectReason(),
                          ctxState.CorruptionPossible(),
                          ctxState.GetDebugMessage());
                return Result{state, pTxInputData};
            }

            // Copy non-final status to return state
            state.SetNonFinal();
            isFinal = false;

            // No point doing further validation on non-final txn if we not going to be able to store it
            if(mempool.getNonFinalPool().getMaxMemory() == 0) {
                state.DoS(0, false, REJECT_MEMPOOL_FULL, "non-final-pool-full");
                return Result{state, pTxInputData};
            }

            // Currently we don't allow chains of non-final txns
            if(DoesNonFinalSpendNonFinal(tx)) {
                state.DoS(0, false, REJECT_NONSTANDARD, "too-long-non-final-chain",
                    false, "Attempt to spend non-final transaction");
                return Result{state, pTxInputData};
            }
        }
    }

    // Is it already in the memory pool?
    if (pool.Exists(txid)) {
        state.Invalid(false, REJECT_ALREADY_KNOWN, "txn-already-in-mempool");
        return Result{state, pTxInputData};
    }
    // Check for conflicts with in-memory transactions
    if (pool.CheckTxConflicts(ptx, isFinal)) {
        state.SetMempoolConflictDetected();
        // Disable replacement feature for good
        state.Invalid(false, REJECT_CONFLICT, "txn-mempool-conflict");
        return Result{state, pTxInputData};
    }

    CCoinsView dummy;
    CCoinsViewCache view(&dummy);
    Amount nValueIn(0);
    LockPoints lp;
    {
        std::shared_lock lock(pool.smtx);
        // Combine db & mempool views together.
        CCoinsViewMemPool viewMemPool(pcoinsTip, pool);
        // Temporarily switch cache backend to db+mempool view
        view.SetBackend(viewMemPool);
        // Do we already have it?
        if(!CheckTxOutputs(tx, pcoinsTip, view, vCoinsToUncache)) {
           state.Invalid(false, REJECT_ALREADY_KNOWN,
                        "txn-already-known");
           return Result{state, pTxInputData, vCoinsToUncache};
        }
        // Do all inputs exist?
        if(!CheckTxInputExists(tx, pcoinsTip, view, state,
                               vCoinsToUncache)) {
           return Result{state, pTxInputData, vCoinsToUncache};
        }
        // Are the actual inputs available?
        if (auto have = view.HaveInputsLimited(tx, fUseLimits ? config.GetMaxCoinsViewCacheSize() : 0);
            !have.has_value())
        {
            state.Invalid(false, REJECT_INVALID,
                         "bad-txns-inputs-too-large");
            return Result{state, pTxInputData, vCoinsToUncache};
        }
        else if (!have.value())
        {
            state.Invalid(false, REJECT_DUPLICATE,
                         "bad-txns-inputs-spent");
            return Result{state, pTxInputData, vCoinsToUncache};
        }
        // Bring the best block into scope.
        view.GetBestBlock();
        // Calculate txn's value-in
        nValueIn = view.GetValueIn(tx);
        // We have all inputs cached now, so switch back to dummy, so we
        // don't need to keep lock on mempool.
        view.SetBackend(dummy);
        // Only accept BIP68 sequence locked transactions that can be mined
        // in the next block; we don't want our mempool filled up with
        // transactions that can't be mined yet. Must keep pool.cs for this
        // unless we change CheckSequenceLocks to take a CoinsViewCache
        // instead of create its own.
        if (!CheckSequenceLocks(tx, pool, config, lockTimeFlags, &lp)) {
            state.DoS(0, false, REJECT_NONSTANDARD,
                     "non-BIP68-final");
            return Result{state, pTxInputData, vCoinsToUncache};
        }
    }

    // Check for non-standard pay-to-script-hash in inputs
    if (!acceptNonStandardOutput)
    {
        auto res =
            AreInputsStandard(source->GetToken(), config, tx, view, chainActive.Height() + 1);

        if (!res.has_value())
        {
            state.SetValidationTimeoutExceeded();
            state.DoS(0, false, REJECT_NONSTANDARD,
                     "too-long-validation-time",
                      false);
            return Result{state, pTxInputData, vCoinsToUncache};
        }
        else if (!res.value())
        {
            state.Invalid(false, REJECT_NONSTANDARD,
                         "bad-txns-nonstandard-inputs");
            return Result{state, pTxInputData, vCoinsToUncache};
        }
    }
    bool sigOpCountError;
    const uint64_t nSigOpsCount{
                GetTransactionSigOpCount(config, tx, view, true, isGenesisEnabled, sigOpCountError)
    };
    // Check that the transaction doesn't have an excessive number of
    // sigops, making it impossible to mine. We consider this an invalid rather
    // than merely non-standard transaction.
    if (sigOpCountError || nSigOpsCount > config.GetMaxTxSigOpsCountPolicy(IsGenesisEnabled(config, chainActive.Height() + 1))) {
        state.DoS(0, false, REJECT_NONSTANDARD,
                 "bad-txns-too-many-sigops",
                  false,
                  strprintf("%d", nSigOpsCount));
        return Result{state, pTxInputData, vCoinsToUncache};
    }

    Amount nFees = nValueIn - tx.GetValueOut();
    if (!IsAbsurdlyHighFeeSetForTxn(nFees, nAbsurdFee)) {
        state.Invalid(false, REJECT_HIGHFEE,
                     "absurdly-high-fee",
                      strprintf("%d > %d", nFees, nAbsurdFee));
        return Result{state, pTxInputData, vCoinsToUncache};
    }
    // nModifiedFees includes any fee deltas from PrioritiseTransaction
    Amount nModifiedFees = nFees;
    double nPriorityDummy = 0;
    pool.ApplyDeltas(txid, nPriorityDummy, nModifiedFees);

    Amount inChainInputValue;
    double dPriority =
        view.GetPriority(tx, chainActive.Height(), inChainInputValue);
    // Keep track of transactions that spend a coinbase, which we re-scan
    // during reorgs to ensure COINBASE_MATURITY is still met.
    const bool fSpendsCoinbase = CheckTxSpendsCoinbase(tx, view);
    // Calculate tx's size.
    const unsigned int nTxSize = ptx->GetTotalSize();
    // Check mempool minimal fee requirement.
    const Amount& nMempoolRejectFee = GetMempoolRejectFee(pool, nTxSize);
    if(!CheckMempoolMinFee(nModifiedFees, nMempoolRejectFee)) {
        state.DoS(0, false, REJECT_INSUFFICIENTFEE,
                 "mempool min fee not met",
                  false,
                  strprintf("%d < %d", nFees, nMempoolRejectFee));
        return Result{state, pTxInputData, vCoinsToUncache};
    }
    //
    // Create an entry point for the transaction (a basic unit in the mempool).
    //
    // chainActive.Height() can never be negative when adding transactions to the mempool,
    // since active chain contains at least genesis block.
    // We can therefore use std::max to convert height to unsigned integer.
    unsigned int uiChainActiveHeight = std::max(chainActive.Height(), 0);
    std::shared_ptr<CTxMemPoolEntry> pMempoolEntry {
        std::make_shared<CTxMemPoolEntry>(
            ptx,
            nFees,
            nAcceptTime,
            dPriority,
            uiChainActiveHeight,
            inChainInputValue,
            fSpendsCoinbase,
            nSigOpsCount,
            lp) };
    // Check tx's priority based on relaypriority flag and relay fee.
    CFeeRate minRelayTxFee = config.GetMinFeePerKB();
    if (!CheckTxRelayPriority(nModifiedFees, minRelayTxFee, *pMempoolEntry, nTxSize)) {
        // Require that free transactions have sufficient priority to be
        // mined in the next block.
        state.DoS(0, false, REJECT_INSUFFICIENTFEE,
                 "insufficient priority");
        return Result{state, pTxInputData, vCoinsToUncache};
    }
    // Continuously rate-limit free (really, very-low-fee) transactions.
    // This mitigates 'penny-flooding' -- sending thousands of free
    // transactions just to be annoying or make others' transactions take
    // longer to confirm.
    if (!CheckLimitFreeTx(fLimitFree, nModifiedFees, minRelayTxFee, nTxSize)){
        state.DoS(0, false, REJECT_INSUFFICIENTFEE,
                 "rate limited free transaction");
        return Result{state, pTxInputData, vCoinsToUncache};
    }
    // Calculate in-mempool ancestors, up to a limit.
    CTxMemPool::setEntries setAncestors;
    std::string errString;
    if (!CalculateMempoolAncestors(pool, *pMempoolEntry, setAncestors, errString)) {
        state.DoS(0, false, REJECT_NONSTANDARD,
                 "too-long-mempool-chain",
                  false,
                  errString);
        return Result{state, pTxInputData, vCoinsToUncache};
    }

    // We are getting flags as they would be if the utxos are before genesis. 
    // "CheckInputs" is adding specific flags for each input based on its height in the main chain
    uint32_t scriptVerifyFlags = GetScriptVerifyFlags(config, IsGenesisEnabled(config, chainActive.Height() + 1));
    // Check against previous transactions. This is done last to help
    // prevent CPU exhaustion denial-of-service attacks.
    PrecomputedTransactionData txdata(tx);
    auto res =
        CheckInputs(
            source->GetToken(),
            config,
            false,
            tx,
            state,
            view,
            true,      /* fScriptChecks */
            scriptVerifyFlags,
            true,      /* sigCacheStore */
            false,     /* scriptCacheStore */
            txdata);

    if (!res.has_value())
    {
        state.SetValidationTimeoutExceeded();
        state.DoS(0, false, REJECT_NONSTANDARD,
                 "too-long-validation-time",
                  false,
                  errString);
        return Result{state, pTxInputData, vCoinsToUncache};
    }
    else if (!res.value())
    {
        // State filled in by CheckInputs.
        return Result{state, pTxInputData, vCoinsToUncache};
    }
    // Check again against the current block tip's script verification flags
    // to cache our script execution flags. This is, of course, useless if
    // the next block has different script flags from the previous one, but
    // because the cache tracks script flags for us it will auto-invalidate
    // and we'll just have a few blocks of extra misses on soft-fork
    // activation.
    //
    // This is also useful in case of bugs in the standard flags that cause
    // transactions to pass as valid when they're actually invalid. For
    // instance the STRICTENC flag was incorrectly allowing certain CHECKSIG
    // NOT scripts to pass, even though they were invalid.
    //
    // There is a similar check in CreateNewBlock() to prevent creating
    // invalid blocks (using TestBlockValidity), however allowing such
    // transactions into the mempool can be exploited as a DoS attack.
    uint32_t currentBlockScriptVerifyFlags =
        GetBlockScriptFlags(config, chainActive.Tip());
    res =
        CheckInputsFromMempoolAndCache(
            source->GetToken(),
            config,
            tx,
            state,
            pcoinsTip,
            view,
            pool,
            currentBlockScriptVerifyFlags,
            true,
            txdata);
    if (!res.has_value())
    {
        state.SetValidationTimeoutExceeded();
        state.DoS(0, false, REJECT_NONSTANDARD,
                 "too-long-validation-time",
                  false,
                  errString);
        return Result{state, pTxInputData, vCoinsToUncache};
    }
    else if (!res.value())
    {
        // If we're using promiscuousmempoolflags, we may hit this normally.
        // Check if current block has some flags that scriptVerifyFlags does
        // not before printing an ominous warning.
        if (!(~scriptVerifyFlags & currentBlockScriptVerifyFlags)) {
            error(
                "%s: BUG! PLEASE REPORT THIS! ConnectInputs failed against "
                "MANDATORY but not STANDARD flags %s, %s",
                __func__, txid.ToString(), FormatStateMessage(state));
            return Result{state, pTxInputData, vCoinsToUncache};
        }
        res =
            CheckInputs(
                source->GetToken(),
                config,
                false,
                tx,
                state,
                view,
                true,      /* fScriptChecks */
                MANDATORY_SCRIPT_VERIFY_FLAGS,
                true,      /* sigCacheStore */
                false,     /* scriptCacheStore */
                txdata);
        if (!res.has_value())
        {
            state.SetValidationTimeoutExceeded();
            state.DoS(0, false, REJECT_NONSTANDARD,
                     "too-long-validation-time",
                      false,
                      errString);
            return Result{state, pTxInputData, vCoinsToUncache};
        }
        if (!res.value())
        {
            error(
                "%s: ConnectInputs failed against MANDATORY but not "
                "STANDARD flags due to promiscuous mempool %s, %s",
                __func__, txid.ToString(), FormatStateMessage(state));
            return Result{state, pTxInputData, vCoinsToUncache};
        }
        LogPrintf("Warning: -promiscuousmempool flags set to not include "
                  "currently enforced soft forks, this may break mining or "
                  "otherwise cause instability!\n");
        // Clear any invalid state due to promiscuousmempool flags usage.
        state = CValidationState();
    }
    // Check a mempool conflict and a double spend attempt
    if(!dsDetector->insertTxnInputs(pTxInputData, pool, state, isFinal)) {
        if (state.IsMempoolConflictDetected()) {
            state.Invalid(false, REJECT_CONFLICT,
                        "txn-mempool-conflict");
        } else if (state.IsDoubleSpendDetected()) {
            state.Invalid(false, REJECT_DUPLICATE,
                        "txn-double-spend-detected");
        }
        return Result{state, pTxInputData, vCoinsToUncache};
    }
    // Check if txn is valid for fee estimation.
    //
    // This transaction should only count for fee estimation if
    // the node is not behind and it is not dependent on any other
    // transactions in the Mempool.
    // Transaction is validated successfully. Return valid results.
    return Result{state,
                  pTxInputData,
                  vCoinsToUncache,
                  std::move(pMempoolEntry),
                  std::move(setAncestors)};
}

CValidationState HandleTxnProcessingException(
    const std::string& sExceptionMsg,
    const TxInputDataSPtr& pTxInputData,
    const CTxnValResult& txnValResult,
    const CTxMemPool& pool,
    CTxnHandlers& handlers) {

    const CTransactionRef& ptx = pTxInputData->mpTx;
    const CTransaction &tx = *ptx;
    // Clean-up steps.
    if (!txnValResult.mCoinsToUncache.empty() && !pool.Exists(tx.GetId())) {
        pcoinsTip->Uncache(txnValResult.mCoinsToUncache);
    }
    handlers.mpTxnDoubleSpendDetector->removeTxnInputs(tx);
    // Construct validation result and a logging message.
    CValidationState state;
    // Do not ban the node. The problem is inside txn processing.
    state.DoS(0, false, REJECT_INVALID, sExceptionMsg);
    std::string sTxnStateMsg = FormatStateMessage(state);
    if (txnValResult.mState.GetRejectCode()) {
        sTxnStateMsg += FormatStateMessage(txnValResult.mState);
    }
    LogPrint(BCLog::TXNVAL, "%s: %s txn= %s: %s\n",
             __func__,
             enum_cast<std::string>(pTxInputData->mTxSource),
             tx.GetId().ToString(),
             sTxnStateMsg);
    return state;
}

std::pair<CTxnValResult, CTask::Status> TxnValidationProcessingTask(
    const TxInputDataSPtr& txInputData,
    const Config& config,
    CTxMemPool& pool,
    CTxnHandlers& handlers,
    bool fUseLimits,
    std::chrono::steady_clock::time_point end_time_point) {
    // Check if time to trigger validation elapsed (skip this check if end_time_point == 0).
    if (!(std::chrono::steady_clock::time_point(std::chrono::milliseconds(0)) == end_time_point) &&
        !(std::chrono::steady_clock::now() < end_time_point)) {
        return {{CValidationState(), txInputData}, CTask::Status::Canceled};
    }
    CTxnValResult result {};
    try
    {
        // Execute validation for the given txn
        result =
            TxnValidation(
                txInputData,
                config,
                pool,
                handlers.mpTxnDoubleSpendDetector,
                fUseLimits);
        // Process validated results
        ProcessValidatedTxn(pool, result, handlers, false);
    } catch (const std::exception& e) {
        return { { HandleTxnProcessingException("An exception thrown in txn processing: " + std::string(e.what()),
                      txInputData,
                      result,
                      pool,
                      handlers), txInputData },
                   CTask::Status::Faulted };
    } catch (...) {
        return { { HandleTxnProcessingException("Unexpected exception in txn processing",
                      txInputData,
                      result,
                      pool,
                      handlers), txInputData },
                   CTask::Status::Faulted };
    }
    // Forward results to the next processing stage
    return {result, CTask::Status::RanToCompletion};
}

static void HandleInvalidP2POrphanTxn(
    const CTxnValResult& txStatus,
    CTxnHandlers& handlers);

static void HandleInvalidP2PNonOrphanTxn(
    const CTxnValResult& txStatus,
    CTxnHandlers& handlers);

static void HandleInvalidStateForP2PNonOrphanTxn(
    const CNodePtr& pNode,
    const CTxnValResult& txStatus,
    int nDoS,
    CTxnHandlers& handlers);

static void PostValidationStepsForP2PTxn(
    const CTxnValResult& txStatus,
    CTxMemPool& pool,
    CTxnHandlers& handlers);

static void PostValidationStepsForFinalisedTxn(
    const CTxnValResult& txStatus,
    CTxMemPool& pool,
    CTxnHandlers& handlers);

static void LogTxnInvalidStatus(const CTxnValResult& txStatus) {
    const bool fOrphanTxn = txStatus.mTxInputData->mfOrphan;
    const CTransactionRef& ptx = txStatus.mTxInputData->mpTx;
    const CTransaction &tx = *ptx;
    const CValidationState& state = txStatus.mState;
    const TxSource source = txStatus.mTxInputData->mTxSource;
    std::string sTxnStatusMsg;
    if (state.IsMissingInputs()) {
        sTxnStatusMsg = "detected orphan";
    } else if (fOrphanTxn && !state.IsMissingInputs()) {
        sTxnStatusMsg = "invalid orphan " + FormatStateMessage(state);
    } else if (!fOrphanTxn) {
        sTxnStatusMsg = "rejected " + FormatStateMessage(state);
    }
    LogPrint(BCLog::TXNVAL,
            "%s: %s txn= %s %s\n",
             enum_cast<std::string>(source),
             state.IsStandardTx() ? "standard" : "nonstandard",
             tx.GetId().ToString(),
             sTxnStatusMsg);
}

static void LogTxnCommitStatus(
    const CTxnValResult& txStatus,
    const size_t& nMempoolSize,
    const size_t& nDynamicMemoryUsage) {

    const bool fOrphanTxn = txStatus.mTxInputData->mfOrphan;
    const CTransactionRef& ptx = txStatus.mTxInputData->mpTx;
    const CTransaction &tx = *ptx;
    const CValidationState& state = txStatus.mState;
    const CNodePtr& pNode = txStatus.mTxInputData->mpNode.lock();
    const TxSource source = txStatus.mTxInputData->mTxSource;
    const std::string csPeerId {
        TxSource::p2p == source ? (pNode ? std::to_string(pNode->GetId()) : "-1")  : ""
    };
    std::string sTxnStatusMsg;
    if (state.IsValid()) {
        if (!fOrphanTxn) {
            sTxnStatusMsg = "accepted";
        } else {
            sTxnStatusMsg = "accepted orphan";
        }
    } else {
        if (!fOrphanTxn) {
            sTxnStatusMsg = "rejected ";
        } else {
            sTxnStatusMsg = "rejected orphan ";
        }
        sTxnStatusMsg += FormatStateMessage(state);
    }
    LogPrint(state.IsValid() ? BCLog::MEMPOOL : BCLog::MEMPOOLREJ,
            "%s: %s txn= %s %s (poolsz %u txn, %u kB) %s\n",
             enum_cast<std::string>(source),
             state.IsStandardTx() ? "standard" : "nonstandard",
             tx.GetId().ToString(),
             sTxnStatusMsg,
             nMempoolSize,
             nDynamicMemoryUsage / 1000,
             TxSource::p2p == source ? "peer=" + csPeerId  : "");
}

void ProcessValidatedTxn(
    CTxMemPool& pool,
    CTxnValResult& txStatus,
    CTxnHandlers& handlers,
    bool fLimitMempoolSize) {

    TxSource source {
        txStatus.mTxInputData->mTxSource
    };
    CValidationState& state = txStatus.mState;
    const CTransactionRef& ptx = txStatus.mTxInputData->mpTx;
    const CTransaction &tx = *ptx;
    /**
     * 1. Txn validation has failed
     *    - Handle an invalid state for p2p txn
     *    - Log txn invalid status
     *    - Uncache coins
     *    - Remove double spends
     * 2. Txn validation has succeeded
     *    - Submit txn to the mempool
     *    - Execute post validation steps for p2p txn
     *    - Log commit status
     *    - Remove double spends
     */
    // Txn validation has failed.
    if (!state.IsValid()) {
        // Handle an invalid state for p2p txn.
        if (TxSource::p2p == source) {
            const bool fOrphanTxn = txStatus.mTxInputData->mfOrphan;
            if (fOrphanTxn) {
                HandleInvalidP2POrphanTxn(txStatus, handlers);
            } else {
                HandleInvalidP2PNonOrphanTxn(txStatus, handlers);
            }
        } else if (handlers.mpOrphanTxns && state.IsMissingInputs()) {
            handlers.mpOrphanTxns->addTxn(txStatus.mTxInputData);
        }
        // Logging txn status
        LogTxnInvalidStatus(txStatus);
    }
    // Txn validation has succeeded.
    else {
        /**
         * Send transaction to the mempool
         */
        size_t nMempoolSize {};
        size_t nDynamicMemoryUsage {};
        // Check if required log categories are enabled
        bool fMempoolLogs = LogAcceptCategory(BCLog::MEMPOOL) || LogAcceptCategory(BCLog::MEMPOOLREJ);
        // Commit transaction
        CommitTxToMempool(
            ptx,
            *(txStatus.mpEntry),
            txStatus.mSetAncestors,
            pool,
            state,
            handlers.mJournalChangeSet,
            fLimitMempoolSize,
            fMempoolLogs ? &nMempoolSize : nullptr,
            fMempoolLogs ? &nDynamicMemoryUsage : nullptr);
        // Check txn's commit status and do all required actions.
        if (TxSource::p2p == source) {
            PostValidationStepsForP2PTxn(txStatus, pool, handlers);
        }
        else if(TxSource::finalised == source) {
            PostValidationStepsForFinalisedTxn(txStatus, pool, handlers);
        }
        // Logging txn commit status
        LogTxnCommitStatus(txStatus, nMempoolSize, nDynamicMemoryUsage);
    }
    // If txn validation or commit has failed then:
    // - uncache coins
    // If txn is accepted by the mempool and orphan handler is present then:
    // - collect txn's outpoints
    // - remove txn from the orphan queue
    if (!state.IsValid()) {
        // coins to uncache
        pcoinsTip->Uncache(txStatus.mCoinsToUncache);
    } else if (handlers.mpOrphanTxns) {
        // At this stage we want to collect outpoints of successfully submitted txn.
        // There might be other related txns being validated at the same time.
        handlers.mpOrphanTxns->collectTxnOutpoints(tx);
        // Remove tx if it was queued as an orphan txn.
        handlers.mpOrphanTxns->eraseTxn(tx.GetId());
    }
    // Remove txn's inputs from the double spend detector as the last step.
    // This needs to be done in all cases:
    // - txn validation has failed
    // - txn committed to the mempool or rejected
    handlers.mpTxnDoubleSpendDetector->removeTxnInputs(tx);
}

static void AskForMissingParents(
    const CNodePtr& pNode,
    const CTransaction &tx) {
    for (const CTxIn &txin : tx.vin) {
        // FIXME: MSG_TX should use a TxHash, not a TxId.
        CInv inv(MSG_TX, txin.prevout.GetTxId());
        pNode->AddInventoryKnown(inv);
        // Check if txn is already known.
        if (!IsTxnKnown(inv)) {
            pNode->AskFor(inv);
        }
    }
}

static void HandleOrphanAndRejectedP2PTxns(
    const CNodePtr& pNode,
    const CTxnValResult& txStatus,
    CTxnHandlers& handlers) {

    const CTransactionRef& ptx = txStatus.mTxInputData->mpTx;
    const CTransaction &tx = *ptx;
    // It may be the case that the orphans parents have all been rejected.
    bool fRejectedParents = false;
    for (const CTxIn &txin : tx.vin) {
        if (handlers.mpTxnRecentRejects->isRejected(txin.prevout.GetTxId())) {
            fRejectedParents = true;
            break;
        }
    }
    if (!fRejectedParents) {
        // Add txn to the orphan queue if it is not there.
        if (!handlers.mpOrphanTxns->checkTxnExists(tx.GetId())) {
            AskForMissingParents(pNode, tx);
            handlers.mpOrphanTxns->addTxn(txStatus.mTxInputData);
        }
        // DoS prevention: do not allow mpOrphanTxns to grow unbounded
        // Multiplying and dividing by ONE_MEGABYTE, because users provide value in MB, internally we use B
        uint64_t nMaxOrphanTxnsSize {
            static_cast<uint64_t>(
                    std::max(gArgs.GetArg("-maxorphantxsize",
                                        COrphanTxns::DEFAULT_MAX_ORPHAN_TRANSACTIONS_SIZE / ONE_MEGABYTE),
                             (int64_t)0) * ONE_MEGABYTE)
        };
        unsigned int nEvicted = handlers.mpOrphanTxns->limitTxnsSize(nMaxOrphanTxnsSize);
        if (nEvicted > 0) {
            LogPrint(BCLog::MEMPOOL,
                    "%s: mapOrphan overflow, removed %u tx\n",
                     enum_cast<std::string>(TxSource::p2p),
                     nEvicted);
        }
    } else {
        // We will continue to reject this tx since it has rejected
        // parents so avoid re-requesting it from other peers.
        handlers.mpTxnRecentRejects->insert(tx.GetId());
        LogPrint(BCLog::MEMPOOL,
                "%s: not keeping orphan with rejected parents txn= %s txnsrc peer=%d \n",
                 enum_cast<std::string>(TxSource::p2p),
                 tx.GetId().ToString(),
                 pNode->GetId());
    }
}

void CreateTxRejectMsgForP2PTxn(
    const TxInputDataSPtr& pTxInputData,
    unsigned int nRejectCode,
    const std::string& sRejectReason) {

    const CNodePtr& pNode = pTxInputData->mpNode.lock();
    // Never send validation's internal codes over P2P.
    if (pNode && nRejectCode > 0 && nRejectCode < REJECT_INTERNAL) {
        const CNetMsgMaker msgMaker(pNode->GetSendVersion());
        // Push tx reject message
        g_connman->PushMessage(
                        pNode,
                        msgMaker.Make(
                            NetMsgType::REJECT, std::string(NetMsgType::TX),
                            uint8_t(nRejectCode),
                            sRejectReason.substr(0, MAX_REJECT_MESSAGE_LENGTH),
                            pTxInputData->mpTx->GetId()));
    }
}

static void HandleInvalidP2POrphanTxn(
    const CTxnValResult& txStatus,
    CTxnHandlers& handlers) {

    const CNodePtr& pNode = txStatus.mTxInputData->mpNode.lock();
    if (!pNode) {
        LogPrint(BCLog::TXNVAL, "An invalid reference: Node doesn't exist");
        return;
    }
    const CTransactionRef& ptx = txStatus.mTxInputData->mpTx;
    const CTransaction &tx = *ptx;
    const CValidationState& state = txStatus.mState;
    // Check if the given p2p txn is considered as fully processed (validated)
    const bool fTxProcessingCompleted =
        (TxValidationPriority::low == txStatus.mTxInputData->mTxValidationPriority ||
         !state.IsValidationTimeoutExceeded());
    // Handle invalid orphan txn for which all inputs are known
    if (!state.IsMissingInputs()) {
        int nDoS = 0;
        if (state.IsInvalid(nDoS) && nDoS > 0) {
            // Punish peer that gave us an invalid orphan tx
            Misbehaving(pNode->GetId(), nDoS, "invalid-orphan-tx");
            // Remove all orphan txns queued from the punished peer
            handlers.mpOrphanTxns->eraseTxnsFromPeer(pNode->GetId());
        } else if (fTxProcessingCompleted) {
            // Erase an invalid orphan as we don't want to reprocess it again.
            handlers.mpOrphanTxns->eraseTxn(tx.GetId());
        }
        // Create and send a reject message when all the following conditions are met:
        // a) the txn is fully processed
        // b) a non-internal reject code was returned from txn validation.
        if (fTxProcessingCompleted) {
            // Has inputs but not accepted to mempool
            // Probably non-standard or insufficient fee/priority
            if (!state.CorruptionPossible()) {
                // Do not use rejection cache for witness
                // transactions or witness-stripped transactions, as
                // they can have been malleated. See
                // https://github.com/bitcoin/bitcoin/issues/8279
                // for details.
                handlers.mpTxnRecentRejects->insert(tx.GetId());
            }
            CreateTxRejectMsgForP2PTxn(
                txStatus.mTxInputData,
                state.GetRejectCode(),
                state.GetRejectReason());
        }
    }
    // No-operation defined for a known orphan with missing inputs.
}

static void HandleInvalidP2PNonOrphanTxn(
    const CTxnValResult& txStatus,
    CTxnHandlers& handlers) {

    const CNodePtr& pNode = txStatus.mTxInputData->mpNode.lock();
    if (!pNode) {
        LogPrint(BCLog::TXNVAL, "An invalid reference: Node doesn't exist");
        return;
    }
    const CValidationState& state = txStatus.mState;
    // Handle txn with missing inputs
    if (state.IsMissingInputs()) {
        HandleOrphanAndRejectedP2PTxns(pNode, txStatus, handlers);
    // Handle an invalid state
    } else {
        int nDoS = 0;
        if (state.IsInvalid(nDoS)) {
            // Handle invalid state
            HandleInvalidStateForP2PNonOrphanTxn(pNode, txStatus, nDoS, handlers);
        }
    }
    // No-operation defined for a known orphan with missing inputs.
}

static void HandleInvalidStateForP2PNonOrphanTxn(
    const CNodePtr& pNode,
    const CTxnValResult& txStatus,
    int nDoS,
    CTxnHandlers& handlers) {

    const CTransactionRef& ptx = txStatus.mTxInputData->mpTx;
    const CTransaction &tx = *ptx;
    const CValidationState& state = txStatus.mState;
    // Check if the given p2p txn is considered as fully processed (validated).
    if (TxValidationPriority::low == txStatus.mTxInputData->mTxValidationPriority ||
        !state.IsValidationTimeoutExceeded()) {
        // Check corruption flag
        if (!state.CorruptionPossible()) {
            // Do not use rejection cache for witness transactions or
            // witness-stripped transactions, as they can have been
            // malleated. See https://github.com/bitcoin/bitcoin/issues/8279
            // for details.
            handlers.mpTxnRecentRejects->insert(tx.GetId());
            if (RecursiveDynamicUsage(tx) < 100000) {
                handlers.mpOrphanTxns->addToCompactExtraTxns(ptx);
            }
        }
        bool fWhiteListForceRelay {
                gArgs.GetBoolArg("-whitelistforcerelay",
                                  DEFAULT_WHITELISTFORCERELAY)
        };
        if (pNode->fWhitelisted && fWhiteListForceRelay) {
            auto nodeId = pNode->GetId();
            // Always relay transactions received from whitelisted peers,
            // even if they were already in the mempool or rejected from it
            // due to policy, allowing the node to function as a gateway for
            // nodes hidden behind it.
            //
            // Never relay transactions that we would assign a non-zero DoS
            // score for, as we expect peers to do the same with us in that
            // case.
            if (nDoS == 0) {
                LogPrint(BCLog::TXNVAL,
                        "%s: Force relaying tx %s from whitelisted peer=%d\n",
                         enum_cast<std::string>(TxSource::p2p),
                         tx.GetId().ToString(),
                         nodeId);
                RelayTransaction(tx, *g_connman);
             } else {
                LogPrint(BCLog::TXNVAL,
                        "%s: Not relaying invalid txn %s from whitelisted peer=%d (%s)\n",
                         enum_cast<std::string>(TxSource::p2p),
                         tx.GetId().ToString(),
                         nodeId,
                         FormatStateMessage(state));
            }
        }
        // Create and send a reject message when all the following conditions are met:
        // a) txn is fully processed
        // b) a non-internal reject code was returned from txn validation.
        CreateTxRejectMsgForP2PTxn(
            txStatus.mTxInputData,
            state.GetRejectCode(),
            state.GetRejectReason());
    }
    if (nDoS > 0) {
        // Punish peer that gave us an invalid tx
        Misbehaving(pNode->GetId(), nDoS, state.GetRejectReason());
    }
}

static void PostValidationStepsForP2PTxn(
    const CTxnValResult& txStatus,
    CTxMemPool& pool,
    CTxnHandlers& handlers) {

    const CNodePtr& pNode = txStatus.mTxInputData->mpNode.lock();
    if (!pNode) {
        LogPrint(BCLog::TXNVAL, "An invalid reference: Node doesn't exist");
        return;
    }
    const CTransactionRef& ptx = txStatus.mTxInputData->mpTx;
    const CValidationState& state = txStatus.mState;
    // Post processing step for successfully commited txns (non-orphans & orphans)
    if (state.IsValid()) {
        // Finalising txns have another round of validation before making it into the
        // mempool, hold off relaying them until that has completed.
        if(pool.Exists(ptx->GetId()) || pool.getNonFinalPool().exists(ptx->GetId())) {
            pool.CheckMempool(pcoinsTip, handlers.mJournalChangeSet);
            RelayTransaction(*ptx, *g_connman);
        }
        pNode->nLastTXTime = GetTime();
    }
    else {
        // For P2P txns the Validator executes LimitMempoolSize when a batch of txns is
        // fully processed (validation is finished and all valid txns were commited)
        // so the else condition can not be interpreted if limit mempool size flag
        // is set on transaction level. As a consequence AddToCompactExtraTransactions is not
        // being called for txns added and then removed from the mempool.

        // Create and send a reject message when all the following conditions are met:
        // a) the txn is fully processed
        // b) a non-internal reject code was returned from txn validation.
        if (TxValidationPriority::low == txStatus.mTxInputData->mTxValidationPriority ||
            !state.IsValidationTimeoutExceeded()) {
            CreateTxRejectMsgForP2PTxn(
                txStatus.mTxInputData,
                state.GetRejectCode(),
                state.GetRejectReason());
        }
    }
}

static void PostValidationStepsForFinalisedTxn(
    const CTxnValResult& txStatus,
    CTxMemPool& pool,
    CTxnHandlers& handlers)
{
    const CTransactionRef& ptx { txStatus.mTxInputData->mpTx };
    const CValidationState& state { txStatus.mState };

    if(state.IsValid())
    {
        pool.CheckMempool(pcoinsTip, handlers.mJournalChangeSet);
        RelayTransaction(*ptx, *g_connman);
    }
}

/**
 * Make mempool consistent after a reorg, by re-adding or recursively erasing
 * disconnected block transactions from the mempool, and also removing any other
 * transactions from the mempool that are no longer valid given the new
 * tip/height.
 *
 * Note: we assume that disconnectpool only contains transactions that are NOT
 * confirmed in the current chain nor already in the mempool (otherwise,
 * in-mempool descendants of such transactions would be removed).
 *
 * Passing fAddToMempool=false will skip trying to add the transactions back,
 * and instead just erase from the mempool as needed.
 */
static void UpdateMempoolForReorg(const Config &config,
                                  DisconnectedBlockTransactions &disconnectpool,
                                  bool fAddToMempool,
                                  const CJournalChangeSetPtr& changeSet) {
    AssertLockHeld(cs_main);
    TxInputDataSPtrVec vTxInputData {};
    // disconnectpool's insertion_order index sorts the entries from oldest to
    // newest, but the oldest entry will be the last tx from the latest mined
    // block that was disconnected.
    // Iterate disconnectpool in reverse, so that we add transactions back to
    // the mempool starting with the earliest transaction that had been
    // previously seen in a block.
    auto it = disconnectpool.queuedTx.get<insertion_order>().rbegin();
    while (it != disconnectpool.queuedTx.get<insertion_order>().rend()) {
        bool fRemoveRecursive { !fAddToMempool || (*it)->IsCoinBase() };
        if (fRemoveRecursive) {
            // If the transaction doesn't make it in to the mempool, remove any
            // transactions that depend on it (which would now be orphans).
            mempool.RemoveRecursive(**it, changeSet, MemPoolRemovalReason::REORG);
        } else {
            vTxInputData.emplace_back(
                    std::make_shared<CTxInputData>(
                                        TxSource::reorg,  // tx source
                                        TxValidationPriority::normal,  // tx validation priority
                                        *it,              // a pointer to the tx
                                        GetTime(),        // nAcceptTime
                                        false));          // fLimitFree
        }
        ++it;
    }
    disconnectpool.queuedTx.clear();
    // Validate a set of transactions
    g_connman->getTxnValidator()->processValidation(vTxInputData, changeSet, true);
    // Mempool related updates
    std::vector<uint256> vHashUpdate {};
    for (const auto& txInputData : vTxInputData) {
        auto const& txid = txInputData->mpTx->GetId();
        if (mempool.Exists(txid)) {
            // A set of transaction hashes from a disconnected block re-added to the mempool.
            vHashUpdate.emplace_back(txid);
        } else {
            // If the transaction doesn't make it in to the mempool, remove any
            // transactions that depend on it (which would now be orphans).
            mempool.RemoveRecursive(*(txInputData->mpTx), changeSet, MemPoolRemovalReason::REORG);
        }
    }
    // Validator/addUnchecked all assume that new mempool entries have
    // no in-mempool children, which is generally not true when adding
    // previously-confirmed transactions back to the mempool.
    // UpdateTransactionsFromBlock finds descendants of any transactions in the
    // disconnectpool that were added back and cleans up the mempool state.
    LogPrint(BCLog::MEMPOOL, "Update transactions from block\n");
    mempool.UpdateTransactionsFromBlock(vHashUpdate);
    // We also need to remove any now-immature transactions
    LogPrint(BCLog::MEMPOOL, "Removing any now-immature transactions\n");
    int height { chainActive.Height() };
    mempool.
        RemoveForReorg(
            config,
            pcoinsTip,
            changeSet,
            height,
            chainActive.Tip()->GetMedianTimePast(),
            StandardNonFinalVerifyFlags(IsGenesisEnabled(config, height)));
}

/**
 * Return transaction in txOut, and if it was found inside a block, its hash is
 * placed in hashBlock and info about if this is post-Genesis transactions is placed into isGenesisEnabled
 */
bool GetTransaction(const Config &config, const TxId &txid,
                    CTransactionRef &txOut,
                    bool fAllowSlow,
                    uint256 &hashBlock,
                    bool& isGenesisEnabled
                    ) {
    CBlockIndex *pindexSlow = nullptr;
    isGenesisEnabled = true;

    LOCK(cs_main);

    CTransactionRef ptx = mempool.Get(txid);
    if (ptx) {
        txOut = ptx;
        isGenesisEnabled = IsGenesisEnabled(config, chainActive.Height() + 1); // assume that the transaction from mempool will be mined in next block
        return true;
    }

    if (fTxIndex) {
        CDiskTxPos postx;
        if (pblocktree->ReadTxIndex(txid, postx)) {
            CAutoFile file(CDiskFiles::OpenBlockFile(postx, true), SER_DISK,
                           CLIENT_VERSION);
            if (file.IsNull()) {
                return error("%s: OpenBlockFile failed", __func__);
            }
            CBlockHeader header;
            try {
                file >> header;
#if defined(WIN32)
                _fseeki64(file.Get(), postx.nTxOffset, SEEK_CUR);
#else
                fseek(file.Get(), postx.nTxOffset, SEEK_CUR);
#endif
                file >> txOut;
            } catch (const std::exception &e) {
                return error("%s: Deserialize or I/O error - %s", __func__,
                             e.what());
            }
            hashBlock = header.GetHash();
            if (txOut->GetId() != txid) {
                return error("%s: txid mismatch", __func__);
            }
            auto foundBlockIndex = mapBlockIndex.find(hashBlock);
            if (foundBlockIndex == mapBlockIndex.end() || foundBlockIndex->second == nullptr)
            {
                return error("%s: mapBlockIndex mismatch  ", __func__);
            }
            isGenesisEnabled = IsGenesisEnabled(config, foundBlockIndex->second->nHeight);
            return true;
        }
    }

    // use coin database to locate block that contains transaction, and scan it
    if (fAllowSlow) {
        const Coin &coin = AccessByTxid(*pcoinsTip, txid);
        if (!coin.IsSpent()) {
            pindexSlow = chainActive[coin.GetHeight()];
        }
    }

    if (pindexSlow) {
        CBlock block;
        if (ReadBlockFromDisk(block, pindexSlow, config)) {
            for (const auto &tx : block.vtx) {
                if (tx->GetId() == txid) {
                    txOut = tx;
                    hashBlock = pindexSlow->GetBlockHash();
                    isGenesisEnabled = IsGenesisEnabled(config, pindexSlow->nHeight);
                    return true;
                }
            }
        }
    }

    return false;
}

//////////////////////////////////////////////////////////////////////////////
//
// CBlock and CBlockIndex
//

/**
 * Returns size of the header for each block in a block file based on block size.
 * This method replaces BLOCKFILE_BLOCK_HEADER_SIZE because we need 64 bit number 
 * to store block size for blocks equal or larger than 32 bit max number.
 */
unsigned int GetBlockFileBlockHeaderSize(uint64_t nBlockSize)
{
    if (nBlockSize >= std::numeric_limits<unsigned int>::max())
    {
        return 16; // 4 bytes disk magic + 4 bytes uint32_t max + 8 bytes block size
    }
    else
    {
        return 8; // 4 bytes disk magic + 4 bytes block size 
    }
}

/** 
 * Write index header. If size larger thant 32 bit max than write 32 bit max and 64 bit size.
 * 32 bit max (0xFFFFFFFF) indicates that there is 64 bit size value following.
 */
void WriteIndexHeader(CAutoFile& fileout,
                      const CMessageHeader::MessageMagic& messageStart,
                      uint64_t nSize)
{
    if (nSize >= std::numeric_limits<unsigned int>::max())
    {
        fileout << FLATDATA(messageStart) << std::numeric_limits<uint32_t>::max() << nSize;
    }
    else
    {
        fileout << FLATDATA(messageStart) << static_cast<uint32_t>(nSize);
    }
}

static bool WriteBlockToDisk(
    const CBlock &block,
    CDiskBlockPos &pos,
    const CMessageHeader::MessageMagic &messageStart,
    CDiskBlockMetaData& metaData)
{
    // Open history file to append
    CAutoFile fileout(CDiskFiles::OpenBlockFile(pos), SER_DISK, CLIENT_VERSION);
    if (fileout.IsNull()) {
        return error("WriteBlockToDisk: OpenBlockFile failed");
    }

    // Write index header.
    WriteIndexHeader(fileout, messageStart, GetSerializeSize(fileout, block));    
    
    // Write block
    long fileOutPos = ftell(fileout.Get());
    if (fileOutPos < 0) {
        return error("WriteBlockToDisk: ftell failed");
    }

    pos.nPos = (unsigned int)fileOutPos;

    std::vector<uint8_t> data;
    CVectorWriter{SER_DISK, CLIENT_VERSION, data, 0, block};
    metaData = { Hash(data.begin(), data.end()), data.size() };

    fileout.write(reinterpret_cast<const char*>(data.data()), data.size());

    return true;
}

bool ReadBlockFromDisk(CBlock &block, const CDiskBlockPos &pos,
                       const Config &config) {
    block.SetNull();

    // Open history file to read
    CAutoFile filein(CDiskFiles::OpenBlockFile(pos, true), SER_DISK, CLIENT_VERSION);
    if (filein.IsNull()) {
        return error("ReadBlockFromDisk: OpenBlockFile failed for %s",
                     pos.ToString());
    }

    // Read block
    try {
        filein >> block;
    } catch (const std::exception &e) {
        return error("%s: Deserialize or I/O error - %s at %s", __func__,
                     e.what(), pos.ToString());
    }

    // Check the header
    if (!CheckProofOfWork(block.GetHash(), block.nBits, config)) {
        return error("ReadBlockFromDisk: Errors in block header at %s",
                     pos.ToString());
    }

    return true;
}

void SetBlockIndexFileMetaDataIfNotSet(CBlockIndex& index, CDiskBlockMetaData metadata)
{
    LOCK(cs_main);
    if (!index.nStatus.hasDiskBlockMetaData()) {
        LogPrintf("Setting block index file metadata for block %s\n", index.GetBlockHash().ToString());
        index.SetDiskBlockMetaData(std::move(metadata.diskDataHash), metadata.diskDataSize);
        setDirtyBlockIndex.insert(&index);
    }
}

bool ReadBlockFromDisk(CBlock &block, const CBlockIndex *pindex,
                       const Config &config) {
    if (!ReadBlockFromDisk(block, pindex->GetBlockPos(), config)) {
        return false;
    }

    if (block.GetHash() != pindex->GetBlockHash()) {
        return error("ReadBlockFromDisk(CBlock&, CBlockIndex*): GetHash() "
                     "doesn't match index for %s at %s",
                     pindex->ToString(), pindex->GetBlockPos().ToString());
    }

    return true;
}

std::unique_ptr<CBlockStreamReader<CFileReader>> GetDiskBlockStreamReader(
    const CDiskBlockPos& pos, bool calculateDiskBlockMetadata)
{
    std::unique_ptr<FILE, CCloseFile> file{
        CDiskFiles::OpenBlockFile(pos, true)};

    if (!file)
    {
        return {}; // could not open a stream
    }

    return
        std::make_unique<CBlockStreamReader<CFileReader>>(
            std::move(file),
            CStreamVersionAndType{SER_DISK, CLIENT_VERSION},
            calculateDiskBlockMetadata);
}

static bool PopulateBlockIndexBlockDiskMetaData(
    FILE* file,
    CBlockIndex& index,
    int networkVersion)
{
    AssertLockHeld(cs_main);

    CBlockStream stream{
        CNonOwningFileReader{file},
        CStreamVersionAndType{SER_DISK, CLIENT_VERSION},
        CStreamVersionAndType{SER_NETWORK, networkVersion}};
    CHash256 hasher;
    uint256 hash;
    size_t size = 0;

    do
    {
        auto chunk = stream.Read(4096);
        hasher.Write(chunk.Begin(), chunk.Size());
        size += chunk.Size();
    } while(!stream.EndOfStream());

    hasher.Finalize(reinterpret_cast<uint8_t*>(&hash));

    SetBlockIndexFileMetaDataIfNotSet(index, CDiskBlockMetaData{hash, size});

    if(fseek(file, index.GetBlockPos().nPos, SEEK_SET) != 0)
    {
        // this should never happen but for some odd reason we aren't
        // able to rewind the file pointer back to the beginning
        return false;
    }

    return true;
}

std::unique_ptr<CForwardAsyncReadonlyStream> StreamBlockFromDisk(
    CBlockIndex& index,
    int networkVersion)
{
    AssertLockHeld(cs_main);

    std::unique_ptr<FILE, CCloseFile> file{
        CDiskFiles::OpenBlockFile(index.GetBlockPos(), true)};

    if (!file)
    {
        return {}; // could not open a stream
    }

    if (!index.nStatus.hasDiskBlockMetaData())
    {
        if (!PopulateBlockIndexBlockDiskMetaData(file.get(), index, networkVersion))
        {
            return {};
        }
    }

    assert(index.GetDiskBlockMetaData().diskDataSize > 0);
    assert(!index.GetDiskBlockMetaData().diskDataHash.IsNull());

    // We expect that block data on disk is in same format as data sent over the
    // network. If this would change in the future then CBlockStream would need
    // to be used to change the resulting fromat.
    return
        std::make_unique<CFixedSizeStream<CAsyncFileReader>>(
            index.GetDiskBlockMetaData().diskDataSize,
            CAsyncFileReader{std::move(file)});
}


std::unique_ptr<CForwardReadonlyStream> StreamSyncBlockFromDisk(CBlockIndex& index)
{
    AssertLockHeld(cs_main);

    std::unique_ptr<FILE, CCloseFile> file{
        CDiskFiles::OpenBlockFile(index.GetBlockPos(), true)};

    if (!file)
    {
        return {}; // could not open a stream
    }

    if (index.nStatus.hasDiskBlockMetaData())
    {
        return
            std::make_unique<CSyncFixedSizeStream<CFileReader>>(
                index.GetDiskBlockMetaData().diskDataSize,
                CFileReader{std::move(file)});
    }

    return 
        std::make_unique<CBlockStream<CFileReader>>(
            CFileReader{std::move(file)},
            CStreamVersionAndType{SER_DISK, CLIENT_VERSION},
            CStreamVersionAndType{SER_NETWORK, PROTOCOL_VERSION});
}

Amount GetBlockSubsidy(int nHeight, const Consensus::Params &consensusParams) {
    int halvings = nHeight / consensusParams.nSubsidyHalvingInterval;
    // Force block reward to zero when right shift is undefined.
    if (halvings >= 64) {
        return Amount(0);
    }

    Amount nSubsidy = 50 * COIN;
    // Subsidy is cut in half every 210,000 blocks which will occur
    // approximately every 4 years.
    return Amount(nSubsidy.GetSatoshis() >> halvings);
}

bool IsInitialBlockDownload() {
    // Once this function has returned false, it must remain false.
    static std::atomic<bool> latchToFalse{false};
    // Optimization: pre-test latch before taking the lock.
    if (latchToFalse.load(std::memory_order_relaxed)) {
        return false;
    }

    LOCK(cs_main);
    if (latchToFalse.load(std::memory_order_relaxed)) {
        return false;
    }
    if (fImporting || fReindex) {
        return true;
    }
    if (chainActive.Tip() == nullptr) {
        return true;
    }
    if (chainActive.Tip()->nChainWork < nMinimumChainWork) {
        return true;
    }
    if (chainActive.Tip()->GetBlockTime() < (GetTime() - nMaxTipAge)) {
        return true;
    }
    LogPrintf("Leaving InitialBlockDownload (latching to false)\n");
    latchToFalse.store(true, std::memory_order_relaxed);
    return false;
}

CBlockIndex *pindexBestForkTip = nullptr, *pindexBestForkBase = nullptr;

void AlertNotify(const std::string &strMessage) {
    uiInterface.NotifyAlertChanged();
    std::string strCmd = gArgs.GetArg("-alertnotify", "");
    if (strCmd.empty()) {
        return;
    }

    // Alert text should be plain ascii coming from a trusted source, but to be
    // safe we first strip anything not in safeChars, then add single quotes
    // around the whole string before passing it to the shell:
    std::string singleQuote("'");
    std::string safeStatus = SanitizeString(strMessage);
    safeStatus = singleQuote + safeStatus + singleQuote;
    boost::replace_all(strCmd, "%s", safeStatus);

    boost::thread t(runCommand, strCmd); // thread runs free
}

// collection of current forks that trigger safe mode (key: fork tip, value: fork base)
std::map<const CBlockIndex*, const CBlockIndex*> safeModeForks;
static CCriticalSection cs_safeModeLevelForks;

/**
 * Finds fork base of a given fork tip. 
 * Returns nullptr if pindexForkTip is not connected to main chain
 */
const CBlockIndex* FindForkBase(const CBlockIndex* pindexForkTip)
{
    AssertLockHeld(cs_main);

    const CBlockIndex* pindexWalk = pindexForkTip;
    while (pindexWalk && !chainActive.Contains(pindexWalk))
    {
        pindexWalk = pindexWalk->pprev;
    }
    return pindexWalk;
}

/**
 * Finds first invalid block from pindexForkTip. Returns nullptr if none was found.
 */
const CBlockIndex* FindInvalidBlockOnFork(const CBlockIndex* pindexForkTip)
{
    AssertLockHeld(cs_main);

    const CBlockIndex* pindexWalk = pindexForkTip;
    while (pindexWalk && !chainActive.Contains(pindexWalk))
    {
        if (pindexWalk->nStatus.isInvalid())
        {
            return pindexWalk;
        }
        pindexWalk = pindexWalk->pprev;
    }
    return nullptr;
}

/**
 * Method tries to find invalid block from fork tip to active chain. If invalid
 * block is found, it sets status withFailedParent to all of its descendants.
 */
void CheckForkForInvalidBlocks(CBlockIndex* pindexForkTip)
{
    AssertLockHeld(cs_main);

    const CBlockIndex* pindexInvalidBlock = FindInvalidBlockOnFork(pindexForkTip);
    if (pindexInvalidBlock)
    {
        // if we found invalid block than invalidate whole chain
        CBlockIndex* pindexWalk = pindexForkTip;
        while (pindexWalk != pindexInvalidBlock)
        {
            pindexWalk->nStatus = pindexWalk->nStatus.withFailedParent();
            setDirtyBlockIndex.insert(pindexWalk);
            setBlockIndexCandidates.erase(pindexWalk);
            pindexWalk = pindexWalk->pprev;
        }
    }
}

/**
 * Method checks if fork should cause node to enter safe mode.
 *
 * @param[in]      pindexForkTip  Tip of the fork that we are validating.
 * @param[in]      pindexForkBase Base of the fork (point where this fork split from active chain). 
 *                                It must be (indirect) parent of pindexForkTip or nullptr if
 *                                pindexForkTip is not connected to main chain.
 * @return Returns level of safe mode that this fork triggers:
 *         NONE    - fork is not triggering safe mode or we called this with pindexForkTip 
 *         UNKNOWN - fork trigger safe mode but we don't know yet if this fork is valid or invalid;
 *                   we probably only have block headers
 *         INVALID - fork that triggers safe mode is marked as invalid but we should still warn 
 *                   because we could be using old version of node
 *         VALID   - fork that triggers safe mode is valid
 */
SafeModeLevel ShouldForkTriggerSafeMode(const CBlockIndex* pindexForkTip, const CBlockIndex* pindexForkBase)
{
    AssertLockHeld(cs_main);

    if (!pindexForkTip || !pindexForkBase)
    {
        return SafeModeLevel::NONE;
    }

    if (chainActive.Contains(pindexForkTip))
    {
        return SafeModeLevel::NONE;
    }
    else if (pindexForkTip->nStatus.isValid() && pindexForkTip->nChainTx > 0 &&
             pindexForkTip->nChainWork - pindexForkBase->nChainWork > (GetBlockProof(*chainActive.Tip()) * SAFE_MODE_MIN_VALID_FORK_LENGTH) &&
             chainActive.Tip()->nHeight - pindexForkBase->nHeight <= SAFE_MODE_MAX_VALID_FORK_DISTANCE)
    {
        return SafeModeLevel::VALID;
    }
    else if (chainActive.Tip()->nHeight - pindexForkBase->nHeight <= SAFE_MODE_MAX_FORK_DISTANCE &&
             chainActive.Tip()->nChainWork + (GetBlockProof(*chainActive.Tip()) * SAFE_MODE_MIN_POW_DIFFERENCE) <= pindexForkTip->nChainWork)
    {
        if (pindexForkTip->nStatus.isInvalid())
        {
            return SafeModeLevel::INVALID;
        }
        else if (pindexForkTip->nStatus.isValid() && pindexForkTip->nChainTx > 0)
        {
            return SafeModeLevel::VALID;
        }
        else
        {
            return SafeModeLevel::UNKNOWN;
        }
    }
    return SafeModeLevel::NONE;
}

void NotifySafeModeLevelChange(SafeModeLevel safeModeLevel, const CBlockIndex* pindexForkTip)
{
    AssertLockHeld(cs_safeModeLevelForks);

    if (!pindexForkTip)
        return;

    if (safeModeForks.find(pindexForkTip) == safeModeForks.end())
        return;

    switch (safeModeLevel)
    {
    case SafeModeLevel::NONE:
        break;
    case SafeModeLevel::UNKNOWN:
        LogPrintf("%s: Warning: Found chain at least ~6 blocks "
                  "longer than our best chain.\nStill waiting for "
                  "block data.\n",
                  __func__);
        break;
    case SafeModeLevel::INVALID:
        LogPrintf("%s: Warning: Found invalid chain at least ~6 blocks "
                  "longer than our best chain.\nChain state database "
                  "corruption likely.\n",
                  __func__);
        break;
    case SafeModeLevel::VALID:
        const CBlockIndex* pindexForkBase = safeModeForks[pindexForkTip];
        std::string warning =
            std::string("'Warning: Large-work fork detected, forking after "
                        "block ") +
                        pindexForkBase->phashBlock->ToString() + std::string("'");
        AlertNotify(warning);
        LogPrintf("%s: Warning: Large valid fork found\n  forking the "
                  "chain at height %d (%s)\n  lasting to height %d "
                  "(%s).\nChain state database corruption likely.\n",
                  __func__, pindexForkBase->nHeight,
                  pindexForkBase->phashBlock->ToString(),
                  pindexForkTip->nHeight,
                  pindexForkTip->phashBlock->ToString());
        break;
    }
}

void CheckSafeModeParameters(const CBlockIndex* pindexNew)
{
    AssertLockHeld(cs_main);

    if (!pindexNew)
        return;
    if (!pindexNew->pprev)
        return;

    LOCK(cs_safeModeLevelForks);

    // safe mode level to be set
    SafeModeLevel safeModeLevel = SafeModeLevel::NONE;
    // fork triggering safe mode level change
    const CBlockIndex* pindexForkTip = nullptr;

    if (chainActive.Tip() == pindexNew->pprev || chainActive.Contains(pindexNew))
    {
        // When we are extending or updating active chain only check if we have to exit safe mode
        if (GetSafeModeLevel() != SafeModeLevel::UNKNOWN)
        {
            // Check all forks if they still meet conditions for safe mode
            for (auto it = safeModeForks.cbegin(); it != safeModeForks.cend();)
            {
                SafeModeLevel safeModeLevelForks = ShouldForkTriggerSafeMode(it->first, it->second);
                if (safeModeLevelForks == SafeModeLevel::NONE)
                {
                    it = safeModeForks.erase(it);
                }
                else
                {
                    // check if this fork rises safe mode level
                    if (safeModeLevelForks > safeModeLevel)
                    {
                        safeModeLevel = safeModeLevelForks;
                        pindexForkTip = it->first;
                    }
                    ++it;
                }
            }
        }
    }
    else
    {
        const CBlockIndex* pindexForkBase = pindexNew;
        if (safeModeForks.find(pindexNew->pprev) != safeModeForks.end())
        {
            // Check if we are extending existing fork that caused safe mode
            pindexForkBase = safeModeForks[pindexNew->pprev];
            safeModeForks.erase(pindexNew->pprev);
        }
        else if (safeModeForks.find(pindexNew) != safeModeForks.end())
        {
            // Check if we are updating block status of existing fork tip (e.g. unknown -> valid)
            pindexForkBase = safeModeForks[pindexNew];
            safeModeForks.erase(pindexNew);
        }
        else
        {
            // Find base of new fork
            pindexForkBase = FindForkBase(pindexNew);
        }
        // Check if fork is in bounds to cause safe mode
        SafeModeLevel safeModeLevelFork = ShouldForkTriggerSafeMode(pindexNew, pindexForkBase);
        if (safeModeLevelFork != SafeModeLevel::NONE)
        {
            // Add fork to collection of forks that cause safe mode
            safeModeForks[pindexNew] = pindexForkBase;
        }
        if (safeModeLevelFork > safeModeLevel)
        {
            safeModeLevel = safeModeLevelFork;
            pindexForkTip = pindexNew;
        }
    }

    // If we have any forks trigger safe mode
    if (GetSafeModeLevel() != safeModeLevel)
    {
        SetSafeModeLevel(safeModeLevel);
        NotifySafeModeLevelChange(safeModeLevel, pindexForkTip);
    }
}

/**
 * This method is called on node startup. It has two tasks:
 *  1. Restore global safe mode state
 *  2. Validate that all header only fork tips have correct tip status
 */
void CheckSafeModeParametersForAllForksOnStartup()
{
    LOCK(cs_main);

    std::set<CBlockIndex*> setTipCandidates;
    std::set<CBlockIndex*> setPrevs;

    int64_t nStart = GetTimeMillis();

    for (const std::pair<const uint256, CBlockIndex*>& item : mapBlockIndex)
    {
        if (!chainActive.Contains(item.second))
        {
            setTipCandidates.insert(item.second);
            setPrevs.insert(item.second->pprev);
        }
    }

    std::set<CBlockIndex*> setTips;
    std::set_difference(setTipCandidates.begin(), setTipCandidates.end(),
                        setPrevs.begin(), setPrevs.end(),
                        std::inserter(setTips, setTips.begin()));

    for (auto& tip :setTips)
    {
        // This is needed because older versions of node did not correctly 
        // mark descendants of an invalid block on forks.
        if (!tip->nStatus.isInvalid() && tip->nChainTx == 0)
        {
            // if tip is valid headers only check fork if it has invalid block
            CheckForkForInvalidBlocks(tip);
        }
        // Restore global safe mode state,
        CheckSafeModeParameters(tip);
    }
    LogPrintf("%s: global safe mode state restored to level %d in %dms\n", 
              __func__, static_cast<int>(GetSafeModeLevel()),
              GetTimeMillis() - nStart);
}

static void InvalidChainFound(CBlockIndex *pindexNew) {
    if (!pindexBestInvalid ||
        pindexNew->nChainWork > pindexBestInvalid->nChainWork) {
        pindexBestInvalid = pindexNew;
    }

    LogPrintf(
        "%s: invalid block=%s  height=%d  log2_work=%.8g  date=%s\n", __func__,
        pindexNew->GetBlockHash().ToString(), pindexNew->nHeight,
        log(pindexNew->nChainWork.getdouble()) / log(2.0),
        DateTimeStrFormat("%Y-%m-%d %H:%M:%S", pindexNew->GetBlockTime()));
    CBlockIndex *tip = chainActive.Tip();
    assert(tip);
    LogPrintf("%s:  current best=%s  height=%d  log2_work=%.8g  date=%s\n",
              __func__, tip->GetBlockHash().ToString(), chainActive.Height(),
              log(tip->nChainWork.getdouble()) / log(2.0),
              DateTimeStrFormat("%Y-%m-%d %H:%M:%S", tip->GetBlockTime()));
    CheckSafeModeParameters(pindexNew);
}

static void InvalidBlockFound(CBlockIndex *pindex,
                              const CValidationState &state) {
    if (!state.CorruptionPossible()) {
        pindex->nStatus = pindex->nStatus.withFailed();
        setDirtyBlockIndex.insert(pindex);
        setBlockIndexCandidates.erase(pindex);
        InvalidChainFound(pindex);
    }
}

void UpdateCoins(const CTransaction &tx, CCoinsViewCache &inputs,
                 CTxUndo &txundo, int nHeight) {
    // Mark inputs spent.
    if (!tx.IsCoinBase()) {
        txundo.vprevout.reserve(tx.vin.size());
        for (const CTxIn &txin : tx.vin) {
            txundo.vprevout.emplace_back();
            bool is_spent =
                inputs.SpendCoin(txin.prevout, &txundo.vprevout.back());
            assert(is_spent);
        }
    }

    // Add outputs.
    AddCoins(inputs, tx, nHeight, GlobalConfig::GetConfig().GetGenesisActivationHeight());
}

void UpdateCoins(const CTransaction &tx, CCoinsViewCache &inputs, int nHeight) {
    CTxUndo txundo;
    UpdateCoins(tx, inputs, txundo, nHeight);
}

std::optional<bool> CScriptCheck::operator()(const task::CCancellationToken& token)
{
    const CScript &scriptSig = ptxTo->vin[nIn].scriptSig;
    return
        VerifyScript(
            config,
            consensus,
            token,
            scriptSig,
            scriptPubKey,
            nFlags,
            CachingTransactionSignatureChecker(
                ptxTo, nIn, amount, cacheStore, txdata),
            &error);
}

std::pair<int,int> GetSpendHeightAndMTP(const CCoinsViewCache &inputs) {
    CBlockIndex *pindexPrev = mapBlockIndex.find(inputs.GetBestBlock())->second;
    return { pindexPrev->nHeight + 1, pindexPrev->GetMedianTimePast() };
}

namespace Consensus {
bool CheckTxInputs(const CTransaction &tx, CValidationState &state,
                   const CCoinsViewCache &inputs, int nSpendHeight) {
    // This doesn't trigger the DoS code on purpose; if it did, it would make it
    // easier for an attacker to attempt to split the network.
    if (!inputs.HaveInputs(tx)) {
        return state.Invalid(false, 0, "", "Inputs unavailable");
    }

    Amount nValueIn(0);
    Amount nFees(0);
    for (const auto &in : tx.vin) {
        const COutPoint &prevout = in.prevout;
        const Coin &coin = inputs.AccessCoin(prevout);
        assert(!coin.IsSpent());

        // If prev is coinbase, check that it's matured
        if (coin.IsCoinBase()) {
            if (nSpendHeight - coin.GetHeight() < COINBASE_MATURITY) {
                return state.Invalid(
                    false, REJECT_INVALID,
                    "bad-txns-premature-spend-of-coinbase",
                    strprintf("tried to spend coinbase at depth %d",
                              nSpendHeight - coin.GetHeight()));
            }
        }

        // Check for negative or overflow input values
        nValueIn += coin.GetTxOut().nValue;
        if (!MoneyRange(coin.GetTxOut().nValue) || !MoneyRange(nValueIn)) {
            return state.DoS(100, false, REJECT_INVALID,
                             "bad-txns-inputvalues-outofrange");
        }
    }

    if (nValueIn < tx.GetValueOut()) {
        return state.DoS(100, false, REJECT_INVALID, "bad-txns-in-belowout",
                         false, strprintf("value in (%s) < value out (%s)",
                                          FormatMoney(nValueIn),
                                          FormatMoney(tx.GetValueOut())));
    }

    // Tally transaction fees
    Amount nTxFee = nValueIn - tx.GetValueOut();
    if (nTxFee < Amount(0)) {
        return state.DoS(100, false, REJECT_INVALID, "bad-txns-fee-negative");
    }
    nFees += nTxFee;
    if (!MoneyRange(nFees)) {
        return state.DoS(100, false, REJECT_INVALID, "bad-txns-fee-outofrange");
    }

    return true;
}
} // namespace Consensus

static int GetInputScriptBlockHeight(int coinHeight) {
    if (coinHeight == MEMPOOL_HEIGHT) {
        // When spending an output that was created in mempool, we assume that it will be mined in the next block.
        return chainActive.Height() + 1;
    }

    return coinHeight;
}

std::optional<bool> CheckInputs(
    const task::CCancellationToken& token,
    const Config& config,
    bool consensus,
    const CTransaction& tx,
    CValidationState& state,
    const CCoinsViewCache& inputs,
    bool fScriptChecks,
    const uint32_t flags,
    bool sigCacheStore,
    bool scriptCacheStore,
    const PrecomputedTransactionData& txdata,
    std::vector<CScriptCheck>* pvChecks)
{
    assert(!tx.IsCoinBase());

    const auto [ spendHeight, mtp ] = GetSpendHeightAndMTP(inputs);
    (void)mtp;  // Silence unused variable warning
    if (!Consensus::CheckTxInputs(tx, state, inputs, spendHeight)) 
    {
        return false;
    }

    if (pvChecks) 
    {
        pvChecks->reserve(tx.vin.size());
    }

    // The first loop above does all the inexpensive checks. Only if ALL inputs
    // pass do we perform expensive ECDSA signature checks. Helps prevent CPU
    // exhaustion attacks.

    // Skip script verification when connecting blocks under the assumedvalid
    // block. Assuming the assumedvalid block is valid this is safe because
    // block merkle hashes are still computed and checked, of course, if an
    // assumed valid block is invalid due to false scriptSigs this optimization
    // would allow an invalid chain to be accepted.
    if (!fScriptChecks) 
    {
        return true;
    }

    // First check if script executions have been cached with the same flags.
    // Note that this assumes that the inputs provided are correct (ie that the
    // transaction hash which is in tx's prevouts properly commits to the
    // scriptPubKey in the inputs view of that transaction).
    uint256 hashCacheEntry = GetScriptCacheKey(tx, flags);
    if (IsKeyInScriptCache(hashCacheEntry, !scriptCacheStore)) 
    {
        return true;
    }

    for (size_t i = 0; i < tx.vin.size(); i++) 
    {
        const COutPoint &prevout = tx.vin[i].prevout;
        const Coin &coin = inputs.AccessCoin(prevout);
        assert(!coin.IsSpent());

        // We very carefully only pass in things to CScriptCheck which are
        // clearly committed to by tx' witness hash. This provides a sanity
        // check that our caching is not introducing consensus failures through
        // additional data in, eg, the coins being spent being checked as a part
        // of CScriptCheck.
        const CScript &scriptPubKey = coin.GetTxOut().scriptPubKey;
        const Amount amount = coin.GetTxOut().nValue;

        uint32_t perInputScriptFlags = 0;
        int inputScriptBlockHeight = GetInputScriptBlockHeight(coin.GetHeight());
        bool isGenesisEnabled = IsGenesisEnabled(config, inputScriptBlockHeight);
        if (isGenesisEnabled)
        {
            perInputScriptFlags = SCRIPT_UTXO_AFTER_GENESIS;
        }

        // ScriptExecutionCache does NOT contain per-input flags. That's why we clear the
        // cache when we are about to cross genesis activation line (see function FinalizeGenesisCrossing).
        // Verify signature
        CScriptCheck check(config, consensus, scriptPubKey, amount, tx, i, flags | perInputScriptFlags, sigCacheStore,
                           txdata);
        if (pvChecks) 
        {
            pvChecks->push_back(std::move(check));
        }
        else if (auto res = check(token); !res.has_value())
        {
            return {};
        }
        else if (!res.value())
        {
            bool genesisGracefulPeriod = IsGenesisGracefulPeriod(config, spendHeight);
            const bool hasNonMandatoryFlags = ((flags | perInputScriptFlags) & STANDARD_NOT_MANDATORY_VERIFY_FLAGS) != 0;

            if (hasNonMandatoryFlags)
            {
                // Check whether the failure was caused by a non-mandatory
                // script verification check, such as non-standard DER encodings
                // or non-null dummy arguments; if so, don't trigger DoS
                // protection to avoid splitting the network between upgraded
                // and non-upgraded nodes.
                // FIXME: CORE-257 has to check if genesis check is necessary also in check2
                uint32_t flags2Check = flags | perInputScriptFlags;
                // Consensus flag is set to true, because we check policy rules in check1. If we would test policy rules 
                // again and fail because the transaction exceeds our policy limits, the node would get banned and this is not ok
                CScriptCheck check2(
                    config, true,
                    scriptPubKey, amount, tx, i,
                    ((flags2Check) & ~STANDARD_NOT_MANDATORY_VERIFY_FLAGS),
                    sigCacheStore, txdata);
                if (auto res2 = check2(token); !res2.has_value())
                {
                    return {};
                }
                else if (res2.value())
                {
                    return state.Invalid(false, REJECT_NONSTANDARD,
                            strprintf("non-mandatory-script-verify-flag (%s)", ScriptErrorString(check.GetScriptError())));
                } 
                else if (genesisGracefulPeriod)
                {
                    uint32_t flags3Check = flags2Check ^ SCRIPT_UTXO_AFTER_GENESIS;

                    CScriptCheck check3(
                        config, true,
                        scriptPubKey, amount, tx, i,
                        ((flags3Check) & ~STANDARD_NOT_MANDATORY_VERIFY_FLAGS),
                        sigCacheStore, txdata);

                    if (auto res3 = check3(token); !res3.has_value())
                    {
                        return {};
                    }
                    else if (res3.value()) 
                    {
                        return state.Invalid(false, REJECT_NONSTANDARD,
                            strprintf("genesis-script-verify-flag-failed (%s)", ScriptErrorString(check.GetScriptError())));
                    }
                }
            }

            // Failures of other flags indicate a transaction that is invalid in
            // new blocks, e.g. a invalid P2SH. We DoS ban such nodes as they
            // are not following the protocol. That said during an upgrade
            // careful thought should be taken as to the correct behavior - we
            // may want to continue peering with non-upgraded nodes even after
            // soft-fork super-majority signaling has occurred.
            return state.DoS(
                100, false, REJECT_INVALID,
                strprintf("mandatory-script-verify-flag-failed (%s)",
                          ScriptErrorString(check.GetScriptError())));
        }
    }

    if (scriptCacheStore && !pvChecks) 
    {
        // We executed all of the provided scripts, and were told to cache the
        // result. Do so now.
        AddKeyInScriptCache(hashCacheEntry);
    }

    return true;
}

namespace {

bool UndoWriteToDisk(const CBlockUndo &blockundo, CDiskBlockPos &pos,
                     const uint256 &hashBlock,
                     const CMessageHeader::MessageMagic &messageStart) {
    // Open history file to append
    CAutoFile fileout(CDiskFiles::OpenUndoFile(pos), SER_DISK, CLIENT_VERSION);
    if (fileout.IsNull()) {
        return error("%s: OpenUndoFile failed", __func__);
    }

    // Write index header. 
    WriteIndexHeader(fileout, messageStart, GetSerializeSize(fileout, blockundo));

    // Write undo data
    long fileOutPos = ftell(fileout.Get());
    if (fileOutPos < 0) {
        return error("%s: ftell failed", __func__);
    }
    pos.nPos = (unsigned int)fileOutPos;
    fileout << blockundo;

    // calculate & write checksum
    CHashWriter hasher(SER_GETHASH, PROTOCOL_VERSION);
    hasher << hashBlock;
    hasher << blockundo;
    fileout << hasher.GetHash();

    return true;
}

bool UndoReadFromDisk(CBlockUndo &blockundo, const CDiskBlockPos &pos,
                      const uint256 &hashBlock) {
    // Open history file to read
    CAutoFile filein(CDiskFiles::OpenUndoFile(pos, true), SER_DISK, CLIENT_VERSION);
    if (filein.IsNull()) {
        return error("%s: OpenUndoFile failed", __func__);
    }

    // Read block
    uint256 hashChecksum;
    // We need a CHashVerifier as reserializing may lose data
    CHashVerifier<CAutoFile> verifier(&filein);
    try {
        verifier << hashBlock;
        verifier >> blockundo;
        filein >> hashChecksum;
    } catch (const std::exception &e) {
        return error("%s: Deserialize or I/O error - %s", __func__, e.what());
    }

    // Verify checksum
    if (hashChecksum != verifier.GetHash()) {
        return error("%s: Checksum mismatch", __func__);
    }

    return true;
}

/** Abort with a message */
bool AbortNode(const std::string &strMessage,
               const std::string &userMessage = "") {
    SetMiscWarning(strMessage);
    LogPrintf("*** %s\n", strMessage);
    uiInterface.ThreadSafeMessageBox(
        userMessage.empty() ? _("Error: A fatal internal error occurred, see "
                                "bitcoind.log for details")
                            : userMessage,
        "", CClientUIInterface::MSG_ERROR);
    StartShutdown();
    return false;
}

bool AbortNode(CValidationState &state, const std::string &strMessage,
               const std::string &userMessage = "") {
    AbortNode(strMessage, userMessage);
    return state.Error(strMessage);
}

} // namespace

/** Restore the UTXO in a Coin at a given COutPoint. */
DisconnectResult UndoCoinSpend(const Coin &undo, CCoinsViewCache &view,
                               const COutPoint &out, const Config &config) {
    bool fClean = true;

    if (view.HaveCoin(out)) {
        // Overwriting transaction output.
        fClean = false;
    }

    if (undo.GetHeight() == 0) {
        // Missing undo metadata (height and coinbase). Older versions included
        // this information only in undo records for the last spend of a
        // transactions' outputs. This implies that it must be present for some
        // other output of the same tx.
        const Coin &alternate = AccessByTxid(view, out.GetTxId());
        if (alternate.IsSpent()) {
            // Adding output for transaction without known metadata
            return DISCONNECT_FAILED;
        }

        // This is somewhat ugly, but hopefully utility is limited. This is only
        // useful when working from legacy on disck data. In any case, putting
        // the correct information in there doesn't hurt.
        const_cast<Coin &>(undo) = Coin(undo.GetTxOut(), alternate.GetHeight(),
                                        alternate.IsCoinBase());
    }

    // The potential_overwrite parameter to AddCoin is only allowed to be false
    // if we know for sure that the coin did not already exist in the cache. As
    // we have queried for that above using HaveCoin, we don't need to guess.
    // When fClean is false, a coin already existed and it is an overwrite.
    view.AddCoin(out, std::move(undo), !fClean, config.GetGenesisActivationHeight());

    return fClean ? DISCONNECT_OK : DISCONNECT_UNCLEAN;
}

/**
 * Undo the effects of this block (with given index) on the UTXO set represented
 * by coins. When FAILED is returned, view is left in an indeterminate state.
 */
static DisconnectResult DisconnectBlock(const CBlock &block,
                                        const CBlockIndex *pindex,
                                        CCoinsViewCache &view,
                                        const task::CCancellationToken& shutdownToken) {
    CBlockUndo blockUndo;
    CDiskBlockPos pos = pindex->GetUndoPos();
    if (pos.IsNull()) {
        error("DisconnectBlock(): no undo data available");
        return DISCONNECT_FAILED;
    }

    if (!UndoReadFromDisk(blockUndo, pos, pindex->pprev->GetBlockHash())) {
        error("DisconnectBlock(): failure reading undo data");
        return DISCONNECT_FAILED;
    }

    return ApplyBlockUndo(blockUndo, block, pindex, view, shutdownToken);
}

DisconnectResult ApplyBlockUndo(const CBlockUndo &blockUndo,
                                const CBlock &block, const CBlockIndex *pindex,
                                CCoinsViewCache &view,
                                const task::CCancellationToken& shutdownToken) {
    bool fClean = true;

    if (blockUndo.vtxundo.size() + 1 != block.vtx.size()) {
        error("DisconnectBlock(): block and undo data inconsistent");
        return DISCONNECT_FAILED;
    }

    // Undo transactions in reverse order.
    size_t i = block.vtx.size();
    while (i-- > 0) {

        if (shutdownToken.IsCanceled())
        {
            return DISCONNECT_FAILED;
        }

        const CTransaction &tx = *(block.vtx[i]);
        uint256 txid = tx.GetId();

        Config &config = GlobalConfig::GetConfig();
        // Check that all outputs are available and match the outputs in the
        // block itself exactly.
        for (size_t o = 0; o < tx.vout.size(); o++) {
            if (tx.vout[o].scriptPubKey.IsUnspendable(IsGenesisEnabled(config, pindex->nHeight))) {
                continue;
            }

            COutPoint out(txid, o);
            Coin coin;
            bool is_spent = view.SpendCoin(out, &coin);
            if (!is_spent || tx.vout[o] != coin.GetTxOut()) {
                // transaction output mismatch
                fClean = false;
            }
        }

        // Restore inputs.
        if (i < 1) {
            // Skip the coinbase.
            continue;
        }

        const CTxUndo &txundo = blockUndo.vtxundo[i - 1];
        if (txundo.vprevout.size() != tx.vin.size()) {
            error("DisconnectBlock(): transaction and undo data inconsistent");
            return DISCONNECT_FAILED;
        }

        for (size_t j = tx.vin.size(); j-- > 0;) {
            const COutPoint &out = tx.vin[j].prevout;
            const Coin &undo = txundo.vprevout[j];
            DisconnectResult res = UndoCoinSpend(undo, view, out, config);
            if (res == DISCONNECT_FAILED) {
                return DISCONNECT_FAILED;
            }
            fClean = fClean && res != DISCONNECT_UNCLEAN;
        }
    }

    // Move best block pointer to previous block.
    view.SetBestBlock(block.hashPrevBlock);

    return fClean ? DISCONNECT_OK : DISCONNECT_UNCLEAN;
}

static std::unique_ptr<checkqueue::CCheckQueuePool<CScriptCheck, arith_uint256>> scriptCheckQueuePool;

void InitScriptCheckQueues(const Config& config, boost::thread_group& threadGroup)
{
    scriptCheckQueuePool =
        std::make_unique<checkqueue::CCheckQueuePool<CScriptCheck, arith_uint256>>(
            config.GetMaxParallelBlocks(),
            threadGroup,
            config.GetPerBlockScriptValidatorThreadsCount(),
            config.GetPerBlockScriptValidationMaxBatchSize());
}

void ShutdownScriptCheckQueues()
{
    scriptCheckQueuePool.reset();
}

// Returns the script flags which should be checked for a given block
static uint32_t GetBlockScriptFlags(const Config &config,
                                    const CBlockIndex *pChainTip) {
    const Consensus::Params &consensusparams =
        config.GetChainParams().GetConsensus();

    uint32_t flags = SCRIPT_VERIFY_NONE;

    // P2SH didn't become active until Apr 1 2012
    if (pChainTip->GetMedianTimePast() >= P2SH_ACTIVATION_TIME) {
        flags |= SCRIPT_VERIFY_P2SH;
    }

    // Start enforcing the DERSIG (BIP66) rule
    if ((pChainTip->nHeight + 1) >= consensusparams.BIP66Height) {
        flags |= SCRIPT_VERIFY_DERSIG;
    }

    // Start enforcing CHECKLOCKTIMEVERIFY (BIP65) rule
    if ((pChainTip->nHeight + 1) >= consensusparams.BIP65Height) {
        flags |= SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY;
    }

    // Start enforcing BIP112 (CSV).
    if ((pChainTip->nHeight + 1) >= consensusparams.CSVHeight) {
        flags |= SCRIPT_VERIFY_CHECKSEQUENCEVERIFY;
    }

    // If the UAHF is enabled, we start accepting replay protected txns
    if (IsUAHFenabled(config, pChainTip)) {
        flags |= SCRIPT_VERIFY_STRICTENC;
        flags |= SCRIPT_ENABLE_SIGHASH_FORKID;
    }

    // If the DAA HF is enabled, we start rejecting transaction that use a high
    // s in their signature. We also make sure that signature that are supposed
    // to fail (for instance in multisig or other forms of smart contracts) are
    // null.
    if (IsDAAEnabled(config, pChainTip)) {
        flags |= SCRIPT_VERIFY_LOW_S;
        flags |= SCRIPT_VERIFY_NULLFAIL;
    }

    if (IsGenesisEnabled(config, pChainTip->nHeight + 1)) {
        flags |= SCRIPT_GENESIS;
        flags |= SCRIPT_VERIFY_SIGPUSHONLY;
    }

    return flags;
}

static int64_t nTimeCheck = 0;
static int64_t nTimeForks = 0;
static int64_t nTimeVerify = 0;
static int64_t nTimeConnect = 0;
static int64_t nTimeIndex = 0;
static int64_t nTimeCallbacks = 0;
static int64_t nTimeTotal = 0;
static int64_t nTimeObtainLock = 0;

/**
 * Apply the effects of this block (with given index) on the UTXO set
 * represented by coins. Validity checks that depend on the UTXO set are also
 * done; ConnectBlock() can fail if those validity checks fail (among other
 * reasons).
 *
 * THROWS (only when parallelBlockValidation is set to true):
 *     - CBestBlockAttachmentCancellation when chain tip has changed while cs_main
 *       was unlocked (a different best block candidate has finished validation
 *       before we re-locked cs_main)
 *
 * THROWS:
 *     - CBlockValidationCancellation when validation was canceled through the
 *       cancellation token before it could finish. This can happen due to an
 *       external reason (e.g. shutting down the application) or internal (e.g.
 *       a better block candidate came in but all the checkers were already in
 *       use so check queue pool cancels the worst one for reuse with the better
 *       candidate).
 */
static bool ConnectBlock(
    const task::CCancellationToken& token,
    bool parallelBlockValidation,
    const Config &config,
    const CBlock &block,
    CValidationState &state,
    CBlockIndex *pindex,
    CCoinsViewCache &view,
    const arith_uint256& mostWorkOnChain,
    bool fJustCheck = false)
{
    AssertLockHeld(cs_main);

    int64_t nTimeStart = GetTimeMicros();

    // Check it again in case a previous version let a bad block in
    BlockValidationOptions validationOptions =
        BlockValidationOptions(!fJustCheck, !fJustCheck);
    if (!CheckBlock(config, block, state, pindex->nHeight, validationOptions)) {
        return error("%s: Consensus::CheckBlock: %s", __func__,
                     FormatStateMessage(state));
    }

    // Verify that the view's current state corresponds to the previous block
    uint256 hashPrevBlock =
        pindex->pprev == nullptr ? uint256() : pindex->pprev->GetBlockHash();
    assert(hashPrevBlock == view.GetBestBlock());

    // Special case for the genesis block, skipping connection of its
    // transactions (its coinbase is unspendable)
    const Consensus::Params &consensusParams =
        config.GetChainParams().GetConsensus();
    if (block.GetHash() == consensusParams.hashGenesisBlock) {
        if (!fJustCheck) {
            view.SetBestBlock(pindex->GetBlockHash());
        }

        return true;
    }

    bool fScriptChecks = true;
    if (!hashAssumeValid.IsNull()) {
        // We've been configured with the hash of a block which has been
        // externally verified to have a valid history. A suitable default value
        // is included with the software and updated from time to time. Because
        // validity relative to a piece of software is an objective fact these
        // defaults can be easily reviewed. This setting doesn't force the
        // selection of any particular chain but makes validating some faster by
        // effectively caching the result of part of the verification.
        BlockMap::const_iterator it = mapBlockIndex.find(hashAssumeValid);
        if (it != mapBlockIndex.end()) {
            if (it->second->GetAncestor(pindex->nHeight) == pindex &&
                pindexBestHeader->GetAncestor(pindex->nHeight) == pindex &&
                pindexBestHeader->nChainWork >= nMinimumChainWork) {
                // This block is a member of the assumed verified chain and an
                // ancestor of the best header. The equivalent time check
                // discourages hashpower from extorting the network via DOS
                // attack into accepting an invalid block through telling users
                // they must manually set assumevalid. Requiring a software
                // change or burying the invalid block, regardless of the
                // setting, makes it hard to hide the implication of the demand.
                // This also avoids having release candidates that are hardly
                // doing any signature verification at all in testing without
                // having to artificially set the default assumed verified block
                // further back. The test against nMinimumChainWork prevents the
                // skipping when denied access to any chain at least as good as
                // the expected chain.
                fScriptChecks =
                    (GetBlockProofEquivalentTime(
                         *pindexBestHeader, *pindex, *pindexBestHeader,
                         consensusParams) <= 60 * 60 * 24 * 7 * 2);
            }
        }
    }

    int64_t nTime1 = GetTimeMicros();
    nTimeCheck += nTime1 - nTimeStart;
    LogPrint(BCLog::BENCH, "    - Sanity checks: %.2fms [%.2fs]\n",
             0.001 * (nTime1 - nTimeStart), nTimeCheck * 0.000001);

    // Do not allow blocks that contain transactions which 'overwrite' older
    // transactions, unless those are already completely spent. If such
    // overwrites are allowed, coinbases and transactions depending upon those
    // can be duplicated to remove the ability to spend the first instance --
    // even after being sent to another address. See BIP30 and
    // http://r6.ca/blog/20120206T005236Z.html for more information. This logic
    // is not necessary for memory pool transactions, as AcceptToMemoryPool
    // already refuses previously-known transaction ids entirely. This rule was
    // originally applied to all blocks with a timestamp after March 15, 2012,
    // 0:00 UTC. Now that the whole chain is irreversibly beyond that time it is
    // applied to all blocks except the two in the chain that violate it. This
    // prevents exploiting the issue against nodes during their initial block
    // download.
    bool fEnforceBIP30 = (!pindex->phashBlock) || // Enforce on CreateNewBlock
                                                  // invocations which don't
                                                  // have a hash.
                         !((pindex->nHeight == 91842 &&
                            pindex->GetBlockHash() ==
                                uint256S("0x00000000000a4d0a398161ffc163c503763"
                                         "b1f4360639393e0e4c8e300e0caec")) ||
                           (pindex->nHeight == 91880 &&
                            pindex->GetBlockHash() ==
                                uint256S("0x00000000000743f190a18c5577a3c2d2a1f"
                                         "610ae9601ac046a38084ccb7cd721")));

    // Once BIP34 activated it was not possible to create new duplicate
    // coinbases and thus other than starting with the 2 existing duplicate
    // coinbase pairs, not possible to create overwriting txs. But by the time
    // BIP34 activated, in each of the existing pairs the duplicate coinbase had
    // overwritten the first before the first had been spent. Since those
    // coinbases are sufficiently buried its no longer possible to create
    // further duplicate transactions descending from the known pairs either. If
    // we're on the known chain at height greater than where BIP34 activated, we
    // can save the db accesses needed for the BIP30 check.
    CBlockIndex *pindexBIP34height =
        pindex->pprev->GetAncestor(consensusParams.BIP34Height);
    // Only continue to enforce if we're below BIP34 activation height or the
    // block hash at that height doesn't correspond.
    fEnforceBIP30 =
        fEnforceBIP30 &&
        (!pindexBIP34height ||
         !(pindexBIP34height->GetBlockHash() == consensusParams.BIP34Hash));

    if (fEnforceBIP30) {
        for (const auto &tx : block.vtx) {
            for (size_t o = 0; o < tx->vout.size(); o++) {
                if (view.HaveCoin(COutPoint(tx->GetId(), o))) {
                    return state.DoS(
                        100,
                        error("ConnectBlock(): tried to overwrite transaction"),
                        REJECT_INVALID, "bad-txns-BIP30");
                }
            }
        }
    }

    // Start enforcing BIP68 (sequence locks).
    int nLockTimeFlags = 0;
    if (pindex->nHeight >= consensusParams.CSVHeight) {
        nLockTimeFlags |= StandardNonFinalVerifyFlags(IsGenesisEnabled(config, pindex->nHeight));
    }
    const uint32_t flags = GetBlockScriptFlags(config, pindex->pprev);
    bool isGenesisEnabled = IsGenesisEnabled(config, pindex->nHeight);
    int64_t nTime2 = GetTimeMicros();
    nTimeForks += nTime2 - nTime1;
    LogPrint(BCLog::BENCH, "    - Fork checks: %.2fms [%.2fs]\n",
             0.001 * (nTime2 - nTime1), nTimeForks * 0.000001);

    CBlockUndo blockundo;

    // CCheckQueueScopeGuard that does nothing and does not belong to any pool.
    using NullScriptChecker =
        checkqueue::CCheckQueuePool<CScriptCheck, arith_uint256>::CCheckQueueScopeGuard;

    // Token for use during functional testing
    std::optional<task::CCancellationToken> checkPoolToken;

    auto control =
        fScriptChecks
        ? scriptCheckQueuePool->GetChecker(mostWorkOnChain, token, &checkPoolToken)
        : NullScriptChecker{};

    std::vector<int> prevheights;
    Amount nFees(0);
    size_t nInputs = 0;

    // Sigops counting. We need to do it again because of P2SH.
    uint64_t nSigOpsCount = 0;
    const uint64_t currentBlockSize =
        ::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION);
    // Sigops are not counted after Genesis anymore
    const uint64_t nMaxSigOpsCountConsensusBeforeGenesis = config.GetMaxBlockSigOpsConsensusBeforeGenesis(currentBlockSize);

    CDiskTxPos pos(pindex->GetBlockPos(),
                   GetSizeOfCompactSize(block.vtx.size()));
    std::vector<std::pair<uint256, CDiskTxPos>> vPos;
    vPos.reserve(block.vtx.size());
    blockundo.vtxundo.reserve(block.vtx.size() - 1);

    uint64_t maxTxSigOpsCountConsensusBeforeGenesis = config.GetMaxTxSigOpsCountConsensusBeforeGenesis();

    for (size_t i = 0; i < block.vtx.size(); i++) {
        const CTransaction &tx = *(block.vtx[i]);

        nInputs += tx.vin.size();

        if (!tx.IsCoinBase()) {
            if (!view.HaveInputs(tx)) {
                return state.DoS(
                    100, error("ConnectBlock(): inputs missing/spent"),
                    REJECT_INVALID, "bad-txns-inputs-missingorspent");
            }

            // Check that transaction is BIP68 final BIP68 lock checks (as
            // opposed to nLockTime checks) must be in ConnectBlock because they
            // require the UTXO set.
            prevheights.resize(tx.vin.size());
            for (size_t j = 0; j < tx.vin.size(); j++) {
                prevheights[j] = view.AccessCoin(tx.vin[j].prevout).GetHeight();
            }

            if (!SequenceLocks(tx, nLockTimeFlags, &prevheights, *pindex)) {
                return state.DoS(
                    100, error("%s: contains a non-BIP68-final transaction",
                               __func__),
                    REJECT_INVALID, "bad-txns-nonfinal");
            }
        }

        // After Genesis we don't count sigops when connecting blocks
        if (!isGenesisEnabled){
            // GetTransactionSigOpCount counts 2 types of sigops:
            // * legacy (always)
            // * p2sh (when P2SH enabled)
            bool sigOpCountError;
            uint64_t txSigOpsCount = GetTransactionSigOpCount(config, tx, view, flags & SCRIPT_VERIFY_P2SH, false, sigOpCountError);
            if (sigOpCountError || txSigOpsCount > maxTxSigOpsCountConsensusBeforeGenesis) {
                return state.DoS(100, false, REJECT_INVALID, "bad-txn-sigops");
            }

            nSigOpsCount += txSigOpsCount;
            if (nSigOpsCount > nMaxSigOpsCountConsensusBeforeGenesis) {
                return state.DoS(100, error("ConnectBlock(): too many sigops"),
                    REJECT_INVALID, "bad-blk-sigops");
            }
        }

        if (!tx.IsCoinBase()) {
            Amount fee = view.GetValueIn(tx) - tx.GetValueOut();
            nFees += fee;

            // Don't cache results if we're actually connecting blocks (still
            // consult the cache, though).
            bool fCacheResults = fJustCheck;

            std::vector<CScriptCheck> vChecks;

            auto res =
                CheckInputs(
                    token,
                    config,
                    true,
                    tx,
                    state,
                    view,
                    fScriptChecks,
                    flags,
                    fCacheResults,
                    fCacheResults,
                    PrecomputedTransactionData(tx),
                    &vChecks);
            if (!res.has_value())
            {
                // With current implementation this can never happen as providing
                // vChecks as parameter skips the path that checks the cancellation
                // token
                throw CBlockValidationCancellation{};
            }
            else if (!res.value())
            {
                return error("ConnectBlock(): CheckInputs on %s failed with %s",
                             tx.GetId().ToString(), FormatStateMessage(state));
            }

            if(fScriptChecks)
            {
                control.Add(vChecks);
            }
        }

        CTxUndo undoDummy;
        if (i > 0) {
            blockundo.vtxundo.push_back(CTxUndo());
        }
        UpdateCoins(tx, view, i == 0 ? undoDummy : blockundo.vtxundo.back(),
                    pindex->nHeight);

        vPos.push_back(std::make_pair(tx.GetId(), pos));
        pos.nTxOffset += ::GetSerializeSize(tx, SER_DISK, CLIENT_VERSION);
    }

    int64_t nTime3 = GetTimeMicros();
    nTimeConnect += nTime3 - nTime2;
    LogPrint(BCLog::BENCH,
             "      - Connect %u transactions: %.2fms (%.3fms/tx, "
             "%.3fms/txin) [%.2fs]\n",
             (unsigned)block.vtx.size(), 0.001 * (nTime3 - nTime2),
             0.001 * (nTime3 - nTime2) / block.vtx.size(),
             nInputs <= 1 ? 0 : 0.001 * (nTime3 - nTime2) / (nInputs - 1),
             nTimeConnect * 0.000001);

    Amount blockReward =
        nFees + GetBlockSubsidy(pindex->nHeight, consensusParams);
    if (block.vtx[0]->GetValueOut() > blockReward) {
        return state.DoS(100, error("ConnectBlock(): coinbase pays too much "
                                    "(actual=%d vs limit=%d)",
                                    block.vtx[0]->GetValueOut(), blockReward),
                         REJECT_INVALID, "bad-cb-amount");
    }

    const CBlockIndex* tipBeforeMainLockReleased = chainActive.Tip();

    int64_t nTime4; // This is set inside scope below
    {
        auto guard =
            blockValidationStatus.getScopedCurrentlyValidatingBlock(*pindex);

        /* Script validation is the most expensive part and is also not cs_main
        dependent so in case of parallel block validation we release it for
        the duration of validation.
        After we obtain the lock once again we check if chain tip has changed
        in the meantime - if not we continue as if we had a lock all along,
        otherwise we skip chain tip update part and retry with a new candidate.*/
        std::unique_ptr<CTemporaryLeaveCriticalSectionGuard> csGuard =
            parallelBlockValidation
            ? std::make_unique<CTemporaryLeaveCriticalSectionGuard>(cs_main)
            : nullptr;

        if(checkPoolToken)
        {
            // We only wait during tests and even then only if validation would
            // be performed.
            blockValidationStatus.waitIfRequired(
                pindex->GetBlockHash(),
                task::CCancellationToken::JoinToken(checkPoolToken.value(), token));
        }
        auto controlValidationStatusOK = control.Wait();

        if (!controlValidationStatusOK.has_value())
        {
            // validation was terminated before it was able to complete so we should
            // skip validity setting to SCRIPTS
            throw CBlockValidationCancellation{};
        }

        if (!controlValidationStatusOK.value())
        {
            return state.DoS(100, false, REJECT_INVALID, "blk-bad-inputs", false,
                             "parallel script check failed");
        }

        // must be inside this scope as csGuard can take a while to re-obtain
        // cs_main lock and we don't want that time to count to validation
        // duration time
        nTime4 = GetTimeMicros();
    }

    // this is the time needed to re-obtain cs_main lock after validation is
    // complete - bound to csGuard in the scope above
    int64_t lockReObtainTime = GetTimeMicros() - nTime4;
    nTimeObtainLock += lockReObtainTime;

    nTimeVerify += nTime4 - nTime2;
    LogPrint(BCLog::BENCH,
             "    - Verify %zu txins: %.2fms (%.3fms/txin) [%.2fs]\n",
             nInputs - 1, 0.001 * (nTime4 - nTime2),
             nInputs <= 1 ? 0 : 0.001 * (nTime4 - nTime2) / (nInputs - 1),
             nTimeVerify * 0.000001);

    LogPrint(BCLog::BENCH, "    - Time to reobtain the lock: %.2fms [%.2fs]\n",
             0.001 * lockReObtainTime, nTimeObtainLock * 0.000001);

    if (fJustCheck) {
        return true;
    }

    // Write undo information to disk
    if (pindex->GetUndoPos().IsNull() ||
        !pindex->IsValid(BlockValidity::SCRIPTS)) {
        if (pindex->GetUndoPos().IsNull()) {
            CDiskBlockPos _pos;
            if (!pBlockFileInfoStore->FindUndoPos(
                    state, pindex->nFile, _pos,
                    ::GetSerializeSize(blockundo, SER_DISK, CLIENT_VERSION) +
                        40, fCheckForPruning)) {
                return error("ConnectBlock(): FindUndoPos failed");
            }
            if (!UndoWriteToDisk(blockundo, _pos, pindex->pprev->GetBlockHash(),
                                 config.GetChainParams().DiskMagic())) {
                return AbortNode(state, "Failed to write undo data");
            }

            // update nUndoPos in block index
            pindex->nUndoPos = _pos.nPos;
            pindex->nStatus = pindex->nStatus.withUndo();
        }

        // since we are changing validation time we need to update
        // setBlockIndexCandidates as well - it sorts by that time
        setBlockIndexCandidates.erase(pindex);
        pindex->RaiseValidity(BlockValidity::SCRIPTS);
        setBlockIndexCandidates.insert(pindex);

        setDirtyBlockIndex.insert(pindex);
    }

    if (fTxIndex && !pblocktree->WriteTxIndex(vPos)) {
        return AbortNode(state, "Failed to write transaction index");
    }

    if (parallelBlockValidation &&
        tipBeforeMainLockReleased != chainActive.Tip())
    {
        // a different block managed to become best block before this one
        // so we should terminate connecting process
        throw CBestBlockAttachmentCancellation{};
    }

    // add this block to the view's block chain
    view.SetBestBlock(pindex->GetBlockHash());

    int64_t nTime5 = GetTimeMicros();
    nTimeIndex += nTime5 - nTime4;
    LogPrint(BCLog::BENCH, "    - Index writing: %.2fms [%.2fs]\n",
             0.001 * (nTime5 - nTime4), nTimeIndex * 0.000001);

    int64_t nTime6 = GetTimeMicros();
    nTimeCallbacks += nTime6 - nTime5;
    LogPrint(BCLog::BENCH, "    - Callbacks: %.2fms [%.2fs]\n",
             0.001 * (nTime6 - nTime5), nTimeCallbacks * 0.000001);

    return true;
}


/**
 * Update the on-disk chain state.
 */
bool FlushStateToDisk(
    const CChainParams &chainparams,
    CValidationState &state,
    FlushStateMode mode,
    int nManualPruneHeight) {

    int64_t nMempoolUsage = mempool.DynamicMemoryUsage();
    LOCK(cs_main);
    static int64_t nLastWrite = 0;
    static int64_t nLastFlush = 0;
    static int64_t nLastSetChain = 0;
    std::set<int> setFilesToPrune;
    bool fFlushForPrune = false;
    bool fDoFullFlush = false;
    int64_t nNow = 0;
    try {
        {
            LOCK(pBlockFileInfoStore->GetLock());
            if (fPruneMode && (fCheckForPruning || nManualPruneHeight > 0) &&
                !fReindex) {
                if (nManualPruneHeight > 0) {
                    pBlockFileInfoStore->FindFilesToPruneManual(setFilesToPrune, nManualPruneHeight);
                } else {
                    pBlockFileInfoStore->FindFilesToPrune(setFilesToPrune,
                                     chainparams.PruneAfterHeight());
                    fCheckForPruning = false;
                }
                if (!setFilesToPrune.empty()) {
                    fFlushForPrune = true;
                    if (!fHavePruned) {
                        pblocktree->WriteFlag("prunedblockfiles", true);
                        fHavePruned = true;
                    }
                }
            }
            nNow = GetTimeMicros();
            // Avoid writing/flushing immediately after startup.
            if (nLastWrite == 0) {
                nLastWrite = nNow;
            }
            if (nLastFlush == 0) {
                nLastFlush = nNow;
            }
            if (nLastSetChain == 0) {
                nLastSetChain = nNow;
            }
            int64_t nMempoolSizeMax =
                gArgs.GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * 1000000;
            int64_t cacheSize = pcoinsTip->DynamicMemoryUsage();
            int64_t nTotalSpace =
                nCoinCacheUsage +
                std::max<int64_t>(nMempoolSizeMax - nMempoolUsage, 0);
            // The cache is large and we're within 10% and 10 MiB of the limit,
            // but we have time now (not in the middle of a block processing).
            bool fCacheLarge =
                mode == FLUSH_STATE_PERIODIC &&
                cacheSize > std::max((9 * nTotalSpace) / 10,
                                     nTotalSpace -
                                         MAX_BLOCK_COINSDB_USAGE * 1024 * 1024);
            // The cache is over the limit, we have to write now.
            bool fCacheCritical =
                mode == FLUSH_STATE_IF_NEEDED && cacheSize > nTotalSpace;
            // It's been a while since we wrote the block index to disk. Do this
            // frequently, so we don't need to redownload after a crash.
            bool fPeriodicWrite =
                mode == FLUSH_STATE_PERIODIC &&
                nNow > nLastWrite + (int64_t)DATABASE_WRITE_INTERVAL * 1000000;
            // It's been very long since we flushed the cache. Do this
            // infrequently, to optimize cache usage.
            bool fPeriodicFlush =
                mode == FLUSH_STATE_PERIODIC &&
                nNow > nLastFlush + (int64_t)DATABASE_FLUSH_INTERVAL * 1000000;
            // Combine all conditions that result in a full cache flush.
            fDoFullFlush = (mode == FLUSH_STATE_ALWAYS) || fCacheLarge ||
                           fCacheCritical || fPeriodicFlush || fFlushForPrune;
            // Write blocks and block index to disk.
            if (fDoFullFlush || fPeriodicWrite) {
                // Depend on nMinDiskSpace to ensure we can write block index
                if (!CheckDiskSpace(0)) {
                    return state.Error("out of disk space");
                }
                // First make sure all block and undo data is flushed to disk.
                pBlockFileInfoStore->FlushBlockFile();
                // Then update all block file information (which may refer to
                // block and undo files).
                {
                    
                    std::vector<std::pair<int, const CBlockFileInfo *>> vFiles = pBlockFileInfoStore->GetAndClearDirtyFileInfo();
                    std::vector<const CBlockIndex *> vBlocks;
                    vBlocks.reserve(setDirtyBlockIndex.size());
                    for (std::set<CBlockIndex *>::iterator it =
                             setDirtyBlockIndex.begin();
                         it != setDirtyBlockIndex.end();) {
                        vBlocks.push_back(*it);
                        setDirtyBlockIndex.erase(it++);
                    }
                    if (!pblocktree->WriteBatchSync(vFiles, pBlockFileInfoStore->GetnLastBlockFile(),
                                                    vBlocks)) {
                        return AbortNode(
                            state, "Failed to write to block index database");
                    }
                }
                // Finally remove any pruned files
                if (fFlushForPrune) UnlinkPrunedFiles(setFilesToPrune);
                nLastWrite = nNow;
            }
            // Flush best chain related state. This can only be done if the
            // blocks / block index write was also done.
            if (fDoFullFlush) {
                // Typical Coin structures on disk are around 48 bytes in size.
                // Pushing a new one to the database can cause it to be written
                // twice (once in the log, and once in the tables). This is
                // already an overestimation, as most will delete an existing
                // entry or overwrite one. Still, use a conservative safety
                // factor of 2.
                if (!CheckDiskSpace(48 * 2 * 2 * pcoinsTip->GetCacheSize())) {
                    return state.Error("out of disk space");
                }
                // Flush the chainstate (which may refer to block index
                // entries).
                if (!pcoinsTip->Flush()) {
                    return AbortNode(state, "Failed to write to coin database");
                }
                nLastFlush = nNow;
            }
        }
        if (fDoFullFlush ||
            ((mode == FLUSH_STATE_ALWAYS || mode == FLUSH_STATE_PERIODIC) &&
             nNow >
                 nLastSetChain + (int64_t)DATABASE_WRITE_INTERVAL * 1000000)) {
            // Update best block in wallet (so we can detect restored wallets).
            GetMainSignals().SetBestChain(chainActive.GetLocator());
            nLastSetChain = nNow;
        }
    } catch (const std::runtime_error &e) {
        return AbortNode(
            state, std::string("System error while flushing: ") + e.what());
    }
    return true;
}

void FlushStateToDisk() {
    CValidationState state;
    const CChainParams &chainparams = Params();
    FlushStateToDisk(chainparams, state, FLUSH_STATE_ALWAYS);
}

void PruneAndFlush() {
    CValidationState state;
    fCheckForPruning = true;
    const CChainParams &chainparams = Params();
    FlushStateToDisk(chainparams, state, FLUSH_STATE_NONE);
}

/**
 * Update chainActive and related internal data structures when adding a new
 * block to the chain tip.
 */
static void UpdateTip(const Config &config, CBlockIndex *pindexNew) {
    chainActive.SetTip(pindexNew);
    chainActiveSharedData.SetChainActiveHeight(chainActive.Height());
    chainActiveSharedData.SetChainActiveTipBlockHash(chainActive.Tip()->GetBlockHash());

    // New best block
    mempool.AddTransactionsUpdated(1);

    cvBlockChange.notify_all();

    std::vector<std::string> warningMessages;
    if (!IsInitialBlockDownload()) {
        int nUpgraded = 0;
        const CBlockIndex *pindex = chainActive.Tip();

        // Check the version of the last 100 blocks to see if we need to
        // upgrade:
        for (int i = 0; i < 100 && pindex != nullptr; i++) {
            int32_t nExpectedVersion = VERSIONBITS_TOP_BITS;
            if (pindex->nVersion > VERSIONBITS_LAST_OLD_BLOCK_VERSION &&
                (pindex->nVersion & ~nExpectedVersion) != 0) {
                ++nUpgraded;
            }
            pindex = pindex->pprev;
        }
        if (nUpgraded > 0) {
            warningMessages.push_back(strprintf(
                "%d of last 100 blocks have unexpected version", nUpgraded));
        }
    }

    LogPrintf("%s: new best=%s height=%d version=0x%08x log2_work=%.8g tx=%lu "
              "date='%s' progress=%f cache=%.1fMiB(%utxo)",
              __func__, chainActive.Tip()->GetBlockHash().ToString(),
              chainActive.Height(), chainActive.Tip()->nVersion,
              log(chainActive.Tip()->nChainWork.getdouble()) / log(2.0),
              (unsigned long)chainActive.Tip()->nChainTx,
              DateTimeStrFormat("%Y-%m-%d %H:%M:%S",
                                chainActive.Tip()->GetBlockTime()),
              GuessVerificationProgress(config.GetChainParams().TxData(),
                                        chainActive.Tip()),
              pcoinsTip->DynamicMemoryUsage() * (1.0 / (1 << 20)),
              pcoinsTip->GetCacheSize());

    if (!warningMessages.empty()) {
        LogPrintf(" warning='%s'",
                  boost::algorithm::join(warningMessages, ", "));
    }
    LogPrintf("\n");
}

static void FinalizeGenesisCrossing(const Config &config, int height, const CJournalChangeSetPtr& changeSet)
{
    if ((IsGenesisEnabled(config, height + 1)) &&
        (!IsGenesisEnabled(config, height)))
    {
        mempool.Clear();
        ClearCache();
        if(changeSet)
        {
            changeSet->clear();
        }
    }
}

/**
 * Disconnect chainActive's tip.
 * After calling, the mempool will be in an inconsistent state, with
 * transactions from disconnected blocks being added to disconnectpool. You
 * should make the mempool consistent again by calling UpdateMempoolForReorg.
 * with cs_main held.
 *
 * If disconnectpool is nullptr, then no disconnected transactions are added to
 * disconnectpool (note that the caller is responsible for mempool consistency
 * in any case).
 */
static bool DisconnectTip(const Config &config, CValidationState &state,
                          DisconnectedBlockTransactions *disconnectpool,
                          const CJournalChangeSetPtr& changeSet)
{
    CBlockIndex *pindexDelete = chainActive.Tip();
    assert(pindexDelete);

    FinalizeGenesisCrossing(config, pindexDelete->nHeight, changeSet);

    // Read block from disk.
    std::shared_ptr<CBlock> pblock = std::make_shared<CBlock>();
    CBlock &block = *pblock;
    if (!ReadBlockFromDisk(block, pindexDelete, config)) {
        return AbortNode(state, "Failed to read block");
    }

    // Apply the block atomically to the chain state.
    int64_t nStart = GetTimeMicros();
    {
        CCoinsViewCache view(pcoinsTip);
        assert(view.GetBestBlock() == pindexDelete->GetBlockHash());
        // Use new private CancellationSource that can not be cancelled
        if (DisconnectBlock(block, pindexDelete, view, task::CCancellationSource::Make()->GetToken()) != DISCONNECT_OK) {
            return error("DisconnectTip(): DisconnectBlock %s failed",
                         pindexDelete->GetBlockHash().ToString());
        }

        bool flushed = view.Flush();
        assert(flushed);
    }

    LogPrint(BCLog::BENCH, "- Disconnect block: %.2fms\n",
             (GetTimeMicros() - nStart) * 0.001);

    // Write the chain state to disk, if necessary.
    if (!FlushStateToDisk(config.GetChainParams(), state,
                          FLUSH_STATE_IF_NEEDED)) {
        return false;
    }

    if (disconnectpool) {
        // Save transactions to re-add to mempool at end of reorg
        for (const auto &tx : boost::adaptors::reverse(block.vtx)) {
            disconnectpool->addTransaction(tx);
        }


        //  The amount of transactions we are willing to store during reorg is the same as max mempool size
        uint64_t maxDisconnectedTxPoolSize = gArgs.GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * ONE_MEGABYTE;
        while (disconnectpool->DynamicMemoryUsage() > maxDisconnectedTxPoolSize) {
            // Drop the earliest entry, and remove its children from the
            // mempool.
            auto it = disconnectpool->queuedTx.get<insertion_order>().begin();
            mempool.RemoveRecursive(**it, changeSet, MemPoolRemovalReason::REORG);
            disconnectpool->removeEntry(it);
        }
    }

    // Update chainActive and related variables.
    UpdateTip(config, pindexDelete->pprev);
    // Let wallets know transactions went from 1-confirmed to
    // 0-confirmed or conflicted:
    GetMainSignals().BlockDisconnected(pblock);
    return true;
}

static int64_t nTimeReadFromDisk = 0;
static int64_t nTimeConnectTotal = 0;
static int64_t nTimeFlush = 0;
static int64_t nTimeChainState = 0;
static int64_t nTimePostConnect = 0;

struct PerBlockConnectTrace {
    CBlockIndex *pindex = nullptr;
    std::shared_ptr<const CBlock> pblock;
    std::shared_ptr<std::vector<CTransactionRef>> conflictedTxs;
    PerBlockConnectTrace()
        : conflictedTxs(std::make_shared<std::vector<CTransactionRef>>()) {}
};

/**
 * Used to track blocks whose transactions were applied to the UTXO state as a
 * part of a single ActivateBestChainStep call.
 *
 * This class also tracks transactions that are removed from the mempool as
 * conflicts (per block) and can be used to pass all those transactions through
 * SyncTransaction.
 *
 * This class assumes (and asserts) that the conflicted transactions for a given
 * block are added via mempool callbacks prior to the BlockConnected()
 * associated with those transactions. If any transactions are marked
 * conflicted, it is assumed that an associated block will always be added.
 *
 * This class is single-use, once you call GetBlocksConnected() you have to
 * throw it away and make a new one.
 */
class ConnectTrace {
private:
    std::vector<PerBlockConnectTrace> blocksConnected;
    CTxMemPool &pool;
    bool mTracingPoolEntryRemovedEvents = false;

    void ConnectToPoolEntryRemovedEvent()
    {
        mTracingPoolEntryRemovedEvents = true;
        pool.NotifyEntryRemoved.connect(
            boost::bind(&ConnectTrace::NotifyEntryRemoved, this, _1, _2));
    }

    void DisconnectFromPoolEntryRemovedEvent()
    {
        mTracingPoolEntryRemovedEvents = false;
        pool.NotifyEntryRemoved.disconnect(
            boost::bind(&ConnectTrace::NotifyEntryRemoved, this, _1, _2));
    }

public:
    ConnectTrace(CTxMemPool &_pool) : blocksConnected(1), pool(_pool)
    {
        ConnectToPoolEntryRemovedEvent();
    }

    ~ConnectTrace()
    {
        DisconnectFromPoolEntryRemovedEvent();
    }

    void TracePoolEntryRemovedEvents(bool trace)
    {
        if(trace && !mTracingPoolEntryRemovedEvents)
        {
            ConnectToPoolEntryRemovedEvent();
        }
        else if(!trace && mTracingPoolEntryRemovedEvents)
        {
            DisconnectFromPoolEntryRemovedEvent();
        }
    }

    void BlockConnected(CBlockIndex *pindex,
                        std::shared_ptr<const CBlock> pblock) {
        assert(!blocksConnected.back().pindex);
        assert(pindex);
        assert(pblock);
        blocksConnected.back().pindex = pindex;
        blocksConnected.back().pblock = std::move(pblock);
        blocksConnected.emplace_back();
    }

    std::vector<PerBlockConnectTrace> &GetBlocksConnected() {
        // We always keep one extra block at the end of our list because blocks
        // are added after all the conflicted transactions have been filled in.
        // Thus, the last entry should always be an empty one waiting for the
        // transactions from the next block. We pop the last entry here to make
        // sure the list we return is sane.
        assert(!blocksConnected.back().pindex);
        assert(blocksConnected.back().conflictedTxs->empty());
        blocksConnected.pop_back();
        return blocksConnected;
    }

    void NotifyEntryRemoved(CTransactionRef txRemoved,
                            MemPoolRemovalReason reason) {
        assert(!blocksConnected.back().pindex);
        if (reason == MemPoolRemovalReason::CONFLICT) {
            blocksConnected.back().conflictedTxs->emplace_back(
                std::move(txRemoved));
        }
    }
};

/**
 * Connect a new block to chainActive. pblock is either nullptr or a pointer to
 * a CBlock corresponding to pindexNew, to bypass loading it again from disk.
 *
 * The block is always added to connectTrace (either after loading from disk or
 * by copying pblock) - if that is not intended, care must be taken to remove
 * the last entry in blocksConnected in case of failure.
 */
static bool ConnectTip(
    bool parallelBlockValidation,
    const task::CCancellationToken& token,
    const Config &config,
    CValidationState &state,
    CBlockIndex *pindexNew,
    const std::shared_ptr<const CBlock> &pblock,
    ConnectTrace &connectTrace,
    DisconnectedBlockTransactions &disconnectpool,
    const CJournalChangeSetPtr& changeSet,
    const arith_uint256& mostWorkOnChain)
{
    assert(pindexNew->pprev == chainActive.Tip());
    // Read block from disk.
    int64_t nTime1 = GetTimeMicros();
    std::shared_ptr<const CBlock> pthisBlock;
    if (!pblock) {
        std::shared_ptr<CBlock> pblockNew = std::make_shared<CBlock>();
        if (!ReadBlockFromDisk(*pblockNew, pindexNew, config)) {
            return AbortNode(state, "Failed to read block");
        }
        pthisBlock = pblockNew;
    } else {
        pthisBlock = pblock;
    }

    const CBlock &blockConnecting = *pthisBlock;

    // Apply the block atomically to the chain state.
    int64_t nTime2 = GetTimeMicros();
    nTimeReadFromDisk += nTime2 - nTime1;
    int64_t nTime3;
    LogPrint(BCLog::BENCH, "  - Load block from disk: %.2fms [%.2fs]\n",
             (nTime2 - nTime1) * 0.001, nTimeReadFromDisk * 0.000001);
    {
        CCoinsViewCache view(pcoinsTip);

        // Temporarily stop tracing events if we are in parallel validation as
        // we will possibly release cs_main lock for a while. In case of an
        // exception we don't need to re-enable it since we won't be using the
        // result
        connectTrace.TracePoolEntryRemovedEvents(!parallelBlockValidation);

        bool rv =
            ConnectBlock(
                token,
                parallelBlockValidation,
                config,
                blockConnecting,
                state,
                pindexNew,
                view,
                mostWorkOnChain);

        // re-enable tracing of events if it was disabled
        connectTrace.TracePoolEntryRemovedEvents(true);

        GetMainSignals().BlockChecked(blockConnecting, state);
        if (!rv) {
            if (state.IsInvalid()) {
                InvalidBlockFound(pindexNew, state);
            }
            return error("ConnectTip(): ConnectBlock %s failed (%s)",
                         pindexNew->GetBlockHash().ToString(),
                         FormatStateMessage(state));
        }
        nTime3 = GetTimeMicros();
        nTimeConnectTotal += nTime3 - nTime2;
        LogPrint(BCLog::BENCH, "  - Connect total: %.2fms [%.2fs]\n",
                 (nTime3 - nTime2) * 0.001, nTimeConnectTotal * 0.000001);
        bool flushed = view.Flush();
        assert(flushed);
    }
    int64_t nTime4 = GetTimeMicros();
    nTimeFlush += nTime4 - nTime3;
    LogPrint(BCLog::BENCH, "  - Flush: %.2fms [%.2fs]\n",
             (nTime4 - nTime3) * 0.001, nTimeFlush * 0.000001);
    // Write the chain state to disk, if necessary.
    if (!FlushStateToDisk(config.GetChainParams(), state,
                          FLUSH_STATE_IF_NEEDED)) {
        return false;
    }
    int64_t nTime5 = GetTimeMicros();
    nTimeChainState += nTime5 - nTime4;
    LogPrint(BCLog::BENCH, "  - Writing chainstate: %.2fms [%.2fs]\n",
             (nTime5 - nTime4) * 0.001, nTimeChainState * 0.000001);
    // Remove conflicting transactions from the mempool.;
    mempool.RemoveForBlock(blockConnecting.vtx, pindexNew->nHeight, changeSet);
    if(g_connman)
    {
        g_connman->DequeueTransactions(blockConnecting.vtx);
    }
    disconnectpool.removeForBlock(blockConnecting.vtx);
    // Update chainActive & related variables.
    UpdateTip(config, pindexNew);

    int64_t nTime6 = GetTimeMicros();
    nTimePostConnect += nTime6 - nTime5;
    nTimeTotal += nTime6 - nTime1;
    LogPrint(BCLog::BENCH, "  - Connect postprocess: %.2fms [%.2fs]\n",
             (nTime6 - nTime5) * 0.001, nTimePostConnect * 0.000001);
    LogPrint(BCLog::BENCH, "- Connect block: %.2fms [%.2fs]\n",
             (nTime6 - nTime1) * 0.001, nTimeTotal * 0.000001);

    connectTrace.BlockConnected(pindexNew, std::move(pthisBlock));

    FinalizeGenesisCrossing(config, pindexNew->nHeight, changeSet);

    return true;
}

/**
 * Return the tip of the chain with the most work in it, that isn't known to be
 * invalid (it's however far from certain to be valid).
 */
static CBlockIndex *FindMostWorkChain() {
    do {
        CBlockIndex *pindexNew = nullptr;

        // Find the best candidate header.
        {
            std::set<CBlockIndex *, CBlockIndexWorkComparator>::reverse_iterator
                it = setBlockIndexCandidates.rbegin();
            if (it == setBlockIndexCandidates.rend()) {
                return nullptr;
            }
            pindexNew = *it;
        }

        // Check whether all blocks on the path between the currently active
        // chain and the candidate are valid. Just going until the active chain
        // is an optimization, as we know all blocks in it are valid already.
        CBlockIndex *pindexTest = pindexNew;
        bool fInvalidAncestor = false;
        while (pindexTest && !chainActive.Contains(pindexTest)) {
            assert(pindexTest->nChainTx || pindexTest->nHeight == 0);

            // Pruned nodes may have entries in setBlockIndexCandidates for
            // which block files have been deleted. Remove those as candidates
            // for the most work chain if we come across them; we can't switch
            // to a chain unless we have all the non-active-chain parent blocks.
            bool fInvalidChain = pindexTest->nStatus.isInvalid();
            bool fMissingData = !pindexTest->nStatus.hasData();
            if (fInvalidChain || fMissingData) 
            {
                if (fInvalidChain)
                {
                    // Candidate chain is not usable (either invalid or missing
                    // data)
                    if ((pindexBestInvalid == nullptr ||
                        pindexNew->nChainWork > pindexBestInvalid->nChainWork)) {
                        pindexBestInvalid = pindexNew;
                    }
                    // Invalidate chain
                    InvalidateChain(pindexTest);
                }
                else if (fMissingData)
                {
                    CBlockIndex* pindexFailed = pindexNew;
                    // Remove the entire chain from the set.
                    while (pindexTest != pindexFailed)
                    {
                        // If we're missing data, then add back to
                        // mapBlocksUnlinked, so that if the block arrives in
                        // the future we can try adding to
                        // setBlockIndexCandidates again.
                        mapBlocksUnlinked.insert(
                            std::make_pair(pindexFailed->pprev, pindexFailed));
                        setBlockIndexCandidates.erase(pindexFailed);
                        pindexFailed = pindexFailed->pprev;
                    }
                }
                setBlockIndexCandidates.erase(pindexTest);
                fInvalidAncestor = true;
                break;
            }
            pindexTest = pindexTest->pprev;
        }
        if (!fInvalidAncestor) {
            return pindexNew;
        }
    } while (true);
}

/** Delete all entries in setBlockIndexCandidates that are worse than the
 * current tip. */
static void PruneBlockIndexCandidates() {
    // Note that we can't delete the current block itself, as we may need to
    // return to it later in case a reorganization to a better block fails.
    std::set<CBlockIndex *, CBlockIndexWorkComparator>::iterator it =
        setBlockIndexCandidates.begin();
    while (it != setBlockIndexCandidates.end() &&
           setBlockIndexCandidates.value_comp()(*it, chainActive.Tip())) {
        setBlockIndexCandidates.erase(it++);
    }
    // Either the current tip or a successor of it we're working towards is left
    // in setBlockIndexCandidates.
    assert(!setBlockIndexCandidates.empty());
}

/**
 * Try to make some progress towards making pindexMostWork the active block.
 * pblock is either nullptr or a pointer to a CBlock corresponding to
 * pindexMostWork.
 */
static bool ActivateBestChainStep(
    const task::CCancellationToken& token,
    const Config &config,
    CValidationState &state,
    CBlockIndex *pindexMostWork,
    const std::shared_ptr<const CBlock> &pblock,
    bool &fInvalidFound,
    ConnectTrace &connectTrace,
    const CJournalChangeSetPtr& changeSet)
{
    AssertLockHeld(cs_main);
    const CBlockIndex *pindexOldTip = chainActive.Tip();
    const CBlockIndex *pindexFork = chainActive.FindFork(pindexMostWork);

    // Disconnect active blocks which are no longer in the best chain.
    bool fBlocksDisconnected = false;
    DisconnectedBlockTransactions disconnectpool;
    while (chainActive.Tip() && chainActive.Tip() != pindexFork) {
        if (!DisconnectTip(config, state, &disconnectpool, changeSet)) {
            // This is likely a fatal error, but keep the mempool consistent,
            // just in case. Only remove from the mempool in this case.
            UpdateMempoolForReorg(config, disconnectpool, false, changeSet);
            return false;
        }
        fBlocksDisconnected = true;
    }

    // Build list of new blocks to connect.
    std::vector<CBlockIndex *> vpindexToConnect;
    bool fContinue = true;
    int nHeight = pindexFork ? pindexFork->nHeight : -1;
    while (fContinue && nHeight != pindexMostWork->nHeight) {
        // Don't iterate the entire list of potential improvements toward the
        // best tip, as we likely only need a few blocks along the way.
        int nTargetHeight = std::min(nHeight + 32, pindexMostWork->nHeight);
        vpindexToConnect.clear();
        vpindexToConnect.reserve(nTargetHeight - nHeight);
        CBlockIndex *pindexIter = pindexMostWork->GetAncestor(nTargetHeight);
        while (pindexIter && pindexIter->nHeight != nHeight) {
            vpindexToConnect.push_back(pindexIter);
            pindexIter = pindexIter->pprev;
        }
        nHeight = nTargetHeight;

        // Connect new blocks.
        for (CBlockIndex *pindexConnect :
             boost::adaptors::reverse(vpindexToConnect)) {
            if (!ConnectTip(
                    /* We always want to get to the same nChainWork amount as
                    we started with before enabling parallel validation as we
                    don't want to end up in a situation where sibling blocks
                    from older chain items are once again eligible for parallel
                    validation thus wasting resources. We also don't wish to
                    end up announcing older chain items as new best tip.*/
                    pindexOldTip && chainActive.Tip()->nChainWork == pindexOldTip->nChainWork,
                    token,
                    config,
                    state,
                    pindexConnect,
                    pindexConnect == pindexMostWork
                        ? pblock
                        : std::shared_ptr<const CBlock>(),
                    connectTrace,
                    disconnectpool,
                    changeSet,
                    pindexMostWork->nChainWork))
            {
                if (state.IsInvalid()) {
                    // The block violates a consensus rule.
                    if (!state.CorruptionPossible()) {
                        InvalidChainFound(vpindexToConnect.back());
                    }
                    state = CValidationState();
                    fInvalidFound = true;
                    fContinue = false;
                    break;
                } else {
                    // A system error occurred (disk space, database error,
                    // ...).
                    // Make the mempool consistent with the current tip, just in
                    // case any observers try to use it before shutdown.
                    UpdateMempoolForReorg(config, disconnectpool, false, changeSet);
                    return false;
                }
            } else {
                PruneBlockIndexCandidates();
                if (!pindexOldTip ||
                    chainActive.Tip()->nChainWork > pindexOldTip->nChainWork) {
                    // We're in a better position than we were. Return
                    // temporarily to release the lock.
                    fContinue = false;
                    break;
                }
            }
        }
    }

    if (fBlocksDisconnected)
    {
        // Whatever we thought this change set might be for, it's now definitely a reorg
        changeSet->updateForReorg();

        // If any blocks were disconnected, disconnectpool may be non empty. Add
        // any disconnected transactions back to the mempool.
        UpdateMempoolForReorg(config, disconnectpool, true, changeSet);
    }

    if(changeSet)
    {
        changeSet->apply();
    }
    mempool.CheckMempool(pcoinsTip, changeSet);

    return true;
}

static void NotifyHeaderTip() {
    bool fNotify = false;
    bool fInitialBlockDownload = false;
    static CBlockIndex *pindexHeaderOld = nullptr;
    CBlockIndex *pindexHeader = nullptr;
    {
        LOCK(cs_main);
        pindexHeader = pindexBestHeader;

        if (pindexHeader != pindexHeaderOld) {
            fNotify = true;
            fInitialBlockDownload = IsInitialBlockDownload();
            pindexHeaderOld = pindexHeader;
        }
    }
    // Send block tip changed notifications without cs_main
    if (fNotify) {
        uiInterface.NotifyHeaderTip(fInitialBlockDownload, pindexHeader);
    }
}

/**
 * Find chain with most work that is considered currently the best but prefer
 * provided block chain if it contains the same amount of work and same parent
 * as the designated best chain.
 * This enables us to process multiple "best" tips in parallel thus
 * preventing one long validating block from delaying alternatives.
 */
static CBlockIndex* ConsiderBlockForMostWorkChain(
    CBlockIndex& mostWork,
    const CBlock& block,
    const CBlockIndex& currentTip)
{
    if(block.GetHash() == mostWork.GetBlockHash() ||
       block.GetBlockHeader().hashPrevBlock != *currentTip.phashBlock)
    {
        return &mostWork;
    }

    auto it = mapBlockIndex.find(block.GetHash());

    // if block is missing from the mapBlockIndex then treat it as code bug
    // since every new block should be added to index before getting here
    assert(it != mapBlockIndex.end());
    assert(*it->second->pprev->phashBlock == block.GetBlockHeader().hashPrevBlock);

    CBlockIndex* indexOfNewBlock = it->second;

    if(mostWork.nChainWork > indexOfNewBlock->nChainWork
        || !indexOfNewBlock->IsValid(BlockValidity::TRANSACTIONS)
        || !indexOfNewBlock->nChainTx)
    {
        return &mostWork;
    }

    return indexOfNewBlock;
}

namespace
{
    /**
     * Class for use in ActivateBestChain() that clears cache by default and
     * preserves it if CCacheScopedGuard::DoNotClear() is called.
     */
    class CCacheScopedGuard
    {
    public:
        CCacheScopedGuard(CBlockIndex** guarding) : mGuarding{guarding} {}
        ~CCacheScopedGuard()
        {
            if(mGuarding)
            {
                *mGuarding = nullptr;
            }
        }
        void DoNotClear() {mGuarding = nullptr;}

    private:
        CBlockIndex** mGuarding;
    };
}

bool ActivateBestChain(
    const task::CCancellationToken& token,
    const Config &config,
    CValidationState &state,
    const CJournalChangeSetPtr& changeSet,
    std::shared_ptr<const CBlock> pblock)
{
    // Note that while we're often called here from ProcessNewBlock, this is
    // far from a guarantee. Things in the P2P/RPC will often end up calling
    // us in the middle of ProcessNewBlock - do not assume pblock is set
    // sanely for performance or correctness!

    // We cache pindexMostWork as with cases where we have multiple consecutive
    // known blocks (e.g initial block download) we don't want to check after
    // each block which block is the next best block
    CBlockIndex* pindexMostWork = nullptr;

    const CBlockIndex* pindexNewTip = nullptr;
    bool tipChanged = false;

    do {
        try
        {
            boost::this_thread::interruption_point();
            if (token.IsCanceled()) {
                break;
            }

            const CBlockIndex *pindexFork;
            bool fInitialDownload;
            {
                LOCK(cs_main);

                // Destructed before cs_main is unlocked (during script
                // validation cs_main can be released so during that time
                // signal processing is disabled for this class to prevent it
                // from being used outside cs_main lock).
                ConnectTrace connectTrace(mempool);

                const CBlockIndex* pindexOldTip = chainActive.Tip();

                // make sure that we clear cache by default and only preserve it
                // when we manage to change tip and clear it otherwise
                CCacheScopedGuard cacheGuard{&pindexMostWork};

                // If we've not yet calculated the best chain, or someone else
                // has updated the current tip from under us, work out the best
                // new tip to aim for.
                if (pindexMostWork == nullptr || pindexNewTip != chainActive.Tip())
                {
                    pindexMostWork = FindMostWorkChain();

                    // Whether we have anything to do at all.
                    if (pindexMostWork == nullptr)
                    {
                        break;
                    }

                    // if block was provided consider it as an alternative candidate
                    if(pblock && pindexOldTip != nullptr)
                    {
                        pindexMostWork =
                            ConsiderBlockForMostWorkChain(
                                *pindexMostWork,
                                *pblock,
                                *pindexOldTip);
                    }

                    if(pindexMostWork == pindexOldTip)
                    {
                        break;
                    }
                }

                // make sure that we don't start validating child on the path
                // that is already covered by a parent that is currently in
                // validation
                if(blockValidationStatus.isAncestorInValidation(*pindexMostWork))
                {
                    LogPrintf(
                        "Block %s will not be considered by the current"
                        " tip activation as a different activation is"
                        " already validating it's ancestor and moving"
                        " towards this block.\n",
                        pindexMostWork->phashBlock->GetHex());

                    break;
                }

                // make sure that we don't start validating a sibling if we
                // have already filled up all block validation queues as that
                // would cause blocking on wait for a idle validator - this is
                // p2p related where we have maxParallelBlocks + 1 async worker
                // threads and we always want to have one extra worker thread
                // for blocks with more work that will be able to steal a
                // validation queue from the worse blocks that are already being
                // validated (preventing poisonous blocks from blocking all
                // worker threads without the possibility of terminating their
                // validation once a better block arrives)
                if(blockValidationStatus.areNSiblingsInValidation(
                    *pindexMostWork,
                    config.GetMaxParallelBlocks()))
                {
                    LogPrintf(
                        "Block %s will not be considered by the current"
                        " tip activation as the maximum parallel block"
                        " validations are already running on siblings"
                        " - block will be re-considered if this branch is"
                        " built upon by subsequent accepted blocks.\n",
                        pindexMostWork->phashBlock->GetHex());

                    break;
                }

                bool fInvalidFound = false;
                std::shared_ptr<const CBlock> nullBlockPtr;
                if (!ActivateBestChainStep(
                        token,
                        config, state, pindexMostWork,
                        pblock &&
                                pblock->GetHash() == pindexMostWork->GetBlockHash()
                            ? pblock
                            : nullBlockPtr,
                        fInvalidFound,
                        connectTrace,
                        changeSet))
                {
                    return false;
                }

                pindexNewTip = chainActive.Tip();

                if (!fInvalidFound && pindexMostWork != pindexNewTip)
                {
                    // Preserve cache as there is more work to be done on this
                    // path
                    cacheGuard.DoNotClear();
                }

                pindexFork = chainActive.FindFork(pindexOldTip);
                fInitialDownload = IsInitialBlockDownload();

                for (const PerBlockConnectTrace &trace :
                     connectTrace.GetBlocksConnected()) {
                    assert(trace.pblock && trace.pindex);
                    GetMainSignals().BlockConnected(trace.pblock, trace.pindex,
                                                    *trace.conflictedTxs);
                }
                
                if (pindexNewTip)
                {
                    // check if new tip affects safe mode
                    CheckSafeModeParameters(pindexNewTip);
                }
                if (pindexOldTip)
                {
                    // check if old tip affects safe mode
                    CheckSafeModeParameters(pindexOldTip);
                }
            }
            // When we reach this point, we switched to a new tip (stored in
            // pindexNewTip).

            // Notifications/callbacks that can run without cs_main

            // Notify external listeners about the new tip.
            GetMainSignals().UpdatedBlockTip(pindexNewTip, pindexFork,
                                             fInitialDownload);

            // Always notify the UI if a new block tip was connected
            if (pindexFork != pindexNewTip) {
                uiInterface.NotifyBlockTip(fInitialDownload, pindexNewTip);
            }

            tipChanged = true;
        }
        catch(const CBestBlockAttachmentCancellation&)
        {
            std::string hash{pblock ? pblock->GetHash().GetHex() : ""};

            LogPrintf(
                "Block %s was not activated as best chain as a better block was"
                " already validated before this one was fully validated.\n",
                hash);
        }
        catch(const CBlockValidationCancellation&)
        {
            std::string hash{pblock ? pblock->GetHash().GetHex() : ""};

            LogPrintf(
                "Block %s validation was terminated before completion. It will"
                " not be considered for best block chain at this moment.\n",
                hash);
        }
    } while (true); // loop exit should be determined inside cs_main lock above

    if(!tipChanged)
    {
        return true;
    }

    const CChainParams &params = config.GetChainParams();
    CheckBlockIndex(params.GetConsensus());

    // Write changes periodically to disk, after relay.
    if (!FlushStateToDisk(params, state, FLUSH_STATE_PERIODIC)) {
        return false;
    }

    int nStopAtHeight = gArgs.GetArg("-stopatheight", DEFAULT_STOPATHEIGHT);
    if (nStopAtHeight && pindexNewTip &&
        pindexNewTip->nHeight >= nStopAtHeight) {
        StartShutdown();
    }

    return true;
}

bool IsBlockABestChainTipCandidate(CBlockIndex& index)
{
    AssertLockHeld(cs_main);

    return (setBlockIndexCandidates.find(&index) != setBlockIndexCandidates.end());
}

bool AreOlderOrEqualUnvalidatedBlockIndexCandidates(
    const std::chrono::time_point<std::chrono::system_clock>& comparisonTime)
{
    AssertLockHeld(cs_main);

    auto time = std::chrono::system_clock::to_time_t(comparisonTime);

    for(const CBlockIndex* pindex : setBlockIndexCandidates)
    {
        if(time >= pindex->GetHeaderReceivedTime() &&
            !pindex->IsValid(BlockValidity::SCRIPTS) &&
            pindex->nChainWork > chainActive.Tip()->nChainWork)
        {
            return true;
        }
    }

    return false;
}

bool PreciousBlock(const Config &config, CValidationState &state,
                   CBlockIndex *pindex) {
    {
        LOCK(cs_main);
        if (pindex->nChainWork < chainActive.Tip()->nChainWork) {
            // Nothing to do, this block is not at the tip.
            return true;
        }
        if (chainActive.Tip()->nChainWork > nLastPreciousChainwork) {
            // The chain has been extended since the last call, reset the
            // counter.
            nBlockReverseSequenceId = -1;
        }
        nLastPreciousChainwork = chainActive.Tip()->nChainWork;
        setBlockIndexCandidates.erase(pindex);
        pindex->IgnoreValidationTime();
        pindex->nSequenceId = nBlockReverseSequenceId;
        if (nBlockReverseSequenceId > std::numeric_limits<int32_t>::min()) {
            // We can't keep reducing the counter if somebody really wants to
            // call preciousblock 2**31-1 times on the same set of tips...
            nBlockReverseSequenceId--;
        }
        if (pindex->IsValid(BlockValidity::TRANSACTIONS) && pindex->nChainTx) {
            setBlockIndexCandidates.insert(pindex);
            PruneBlockIndexCandidates();
        }
    }

    CJournalChangeSetPtr changeSet { mempool.getJournalBuilder()->getNewChangeSet(JournalUpdateReason::REORG) };
    auto source = task::CCancellationSource::Make();
    // state is used to report errors, not block related invalidity
    // (see description of ActivateBestChain)
    return ActivateBestChain(task::CCancellationToken::JoinToken(source->GetToken(), GetShutdownToken()), config, state, changeSet);
}

bool InvalidateBlock(const Config &config, CValidationState &state,
                     CBlockIndex *pindex) {
    AssertLockHeld(cs_main);

    // Mark the block itself as invalid.
    pindex->nStatus = pindex->nStatus.withFailed();
    setDirtyBlockIndex.insert(pindex);
    setBlockIndexCandidates.erase(pindex);

    DisconnectedBlockTransactions disconnectpool;
    CJournalChangeSetPtr changeSet { mempool.getJournalBuilder()->getNewChangeSet(JournalUpdateReason::REORG) };
    if (chainActive.Contains(pindex))
    {
        while (chainActive.Contains(pindex))
        {
            CBlockIndex* pindexWalk = chainActive.Tip();
            pindexWalk->nStatus = pindexWalk->nStatus.withFailedParent();
            setDirtyBlockIndex.insert(pindexWalk);
            setBlockIndexCandidates.erase(pindexWalk);
            // ActivateBestChain considers blocks already in chainActive
            // unconditionally valid already, so force disconnect away from it.
            if (!DisconnectTip(config, state, &disconnectpool, changeSet))
            {
                // It's probably hopeless to try to make the mempool consistent
                // here if DisconnectTip failed, but we can try.
                UpdateMempoolForReorg(config, disconnectpool, false, changeSet);
                return false;
            }
        }
    }
    else
    {
        // in case of invalidating block that is not on active chain make sure
        // that we mark all its descendants (whole chain) as invalid 
        InvalidateChain(pindex);
    }

    // DisconnectTip will add transactions to disconnectpool; try to add these
    // back to the mempool.
    UpdateMempoolForReorg(config, disconnectpool, true, changeSet);

    // The resulting new best tip may not be in setBlockIndexCandidates anymore,
    // so add it again.
    for (const std::pair<const uint256, CBlockIndex *> &it : mapBlockIndex) {
        CBlockIndex *i = it.second;
        if (i->IsValid(BlockValidity::TRANSACTIONS) && i->nChainTx &&
            !setBlockIndexCandidates.value_comp()(i, chainActive.Tip())) {
            setBlockIndexCandidates.insert(i);
        }
    }

    InvalidChainFound(pindex);
    uiInterface.NotifyBlockTip(IsInitialBlockDownload(), pindex->pprev);

    if (state.IsValid()) {
        auto source = task::CCancellationSource::Make();
        // state is used to report errors, not block related invalidity
        // (see description of ActivateBestChain)
        ActivateBestChain(task::CCancellationToken::JoinToken(source->GetToken(), GetShutdownToken()), config, state, changeSet);
    }

    // Check mempool & journal
    mempool.CheckMempool(pcoinsTip, changeSet);

    return true;
}

void InvalidateBlocksFromConfig(const Config &config)
{
    for ( const auto& invalidBlockHash: config.GetInvalidBlocks() )
    {
        CValidationState state;
        {
            LOCK(cs_main);
            if (mapBlockIndex.count(invalidBlockHash) == 0) {
                LogPrintf("Block %s that is marked as invalid is not found.\n", invalidBlockHash.GetHex());
                continue;
            }
                
            CBlockIndex *pblockindex = mapBlockIndex[invalidBlockHash];
            LogPrintf("Invalidating Block %s.\n", invalidBlockHash.GetHex());
            InvalidateBlock(config, state, pblockindex);
        }
                
        if (!state.IsValid()) 
        {
            LogPrintf("Problem when invalidating block: %s.\n",state.GetRejectReason());
        }
    }
}

bool ResetBlockFailureFlags(CBlockIndex *pindex) {
    AssertLockHeld(cs_main);

    int nHeight = pindex->nHeight;

    // Remove the invalidity flag from this block and all its descendants.
    BlockMap::iterator it = mapBlockIndex.begin();
    while (it != mapBlockIndex.end()) {
        if (!it->second->IsValid() &&
            it->second->GetAncestor(nHeight) == pindex) {
            it->second->nStatus = it->second->nStatus.withClearedFailureFlags();
            setDirtyBlockIndex.insert(it->second);
            if (it->second->IsValid(BlockValidity::TRANSACTIONS) &&
                it->second->nChainTx &&
                setBlockIndexCandidates.value_comp()(chainActive.Tip(),
                                                     it->second)) {
                setBlockIndexCandidates.insert(it->second);
            }
            if (it->second == pindexBestInvalid) {
                // Reset invalid block marker if it was pointing to one of
                // those.
                pindexBestInvalid = nullptr;
            }
        }
        it++;
    }

    // Remove the invalidity flag from all ancestors too.
    while (pindex != nullptr) {
        if (pindex->nStatus.isInvalid()) {
            pindex->nStatus = pindex->nStatus.withClearedFailureFlags();
            setDirtyBlockIndex.insert(pindex);
        }
        pindex = pindex->pprev;
    }
    return true;
}

static CBlockIndex *AddToBlockIndex(const CBlockHeader &block) {
    // Check for duplicate
    uint256 hash = block.GetHash();
    BlockMap::iterator it = mapBlockIndex.find(hash);
    if (it != mapBlockIndex.end()) {
        return it->second;
    }

    // Construct new block index object
    CBlockIndex *pindexNew = new CBlockIndex(block);
    assert(pindexNew);
    // We assign the sequence id to blocks only when the full data is available,
    // to avoid miners withholding blocks but broadcasting headers, to get a
    // competitive advantage.
    pindexNew->nSequenceId = 0;
    BlockMap::iterator mi =
        mapBlockIndex.insert(std::make_pair(hash, pindexNew)).first;
    pindexNew->phashBlock = &((*mi).first);
    BlockMap::iterator miPrev = mapBlockIndex.find(block.hashPrevBlock);
    if (miPrev != mapBlockIndex.end()) {
        pindexNew->pprev = (*miPrev).second;
        pindexNew->nHeight = pindexNew->pprev->nHeight + 1;
        pindexNew->BuildSkip();
    }
    pindexNew->nTimeReceived = GetTime();
    pindexNew->nTimeMax =
        (pindexNew->pprev
             ? std::max(pindexNew->pprev->nTimeMax, pindexNew->nTime)
             : pindexNew->nTime);
    pindexNew->SetChainWork();
    pindexNew->RaiseValidity(BlockValidity::TREE);
    if (pindexBestHeader == nullptr ||
        pindexBestHeader->nChainWork < pindexNew->nChainWork) {
        pindexBestHeader = pindexNew;
    }

    setDirtyBlockIndex.insert(pindexNew);

    // Check if adding new block index triggers safe mode
    CheckSafeModeParameters(pindexNew);

    return pindexNew;
}

void InvalidateChain(const CBlockIndex* pindexNew)
{
    std::set<CBlockIndex*> setTipCandidates;
    std::set<CBlockIndex*> setPrevs;

    // Check that we are invalidating chain from an invalid block
    assert(pindexNew->nStatus.isInvalid());

    // Check if invalid block is on current active chain
    bool isInvalidBlockOnActiveChain = chainActive.Contains(pindexNew);

    // Collect blocks that are not part of currently active chain
    for (const std::pair<const uint256, CBlockIndex*>& item : mapBlockIndex)
    {
        // Tip candidates are only blocks above invalid block 
        // If we are invalid block is not on active chain than we 
        // need only fork tips not active tip
        if (item.second->nHeight > pindexNew->nHeight &&
            (isInvalidBlockOnActiveChain || !chainActive.Contains(item.second)))
        {
            setTipCandidates.insert(item.second);
            setPrevs.insert(item.second->pprev);
        }
    }

    std::set<CBlockIndex*> setTips;
    std::set_difference(setTipCandidates.begin(), setTipCandidates.end(),
                        setPrevs.begin(), setPrevs.end(),
                        std::inserter(setTips, setTips.begin()));

    for (std::set< CBlockIndex*>::iterator it = setTips.begin();
         it != setTips.end(); ++it)
    {
        // Check if pindexNew is in this chain
        CBlockIndex* pindexWalk = (*it);
        while (pindexWalk->nHeight > pindexNew->nHeight)
        {
            pindexWalk = pindexWalk->pprev;
        }
        if (pindexWalk == pindexNew)
        {
            // Set status of all descendant blocks to withFailedParent
            pindexWalk = (*it);
            while (pindexWalk != pindexNew)
            {
                pindexWalk->nStatus = pindexWalk->nStatus.withFailedParent();
                setDirtyBlockIndex.insert(pindexWalk);
                setBlockIndexCandidates.erase(pindexWalk);
                pindexWalk = pindexWalk->pprev;
            }
            // Check if we have to enter safe mode if chain has been invalidated
            CheckSafeModeParameters(*it);
        }
    }
}

bool CheckBlockTTOROrder(const CBlock& block)
{
    std::set<TxId> usedInputs;
    for (const auto& tx : block.vtx)
    {
        // If current transactions is found after another transaction 
        // that spends any output of current transaction, then the block 
        // violates TTOR order.
        if (usedInputs.find(tx->GetId()) != usedInputs.end())
        {
            return false;
        }
        for (const auto& vin : tx->vin)
        {
            // Skip coinbase
            if (!vin.prevout.IsNull())
            {
                usedInputs.insert(vin.prevout.GetTxId());
            }
        }
    }
    return true;
}

/**
 * Mark a block as having its data received and checked (up to
 * BLOCK_VALID_TRANSACTIONS).
 */
static bool ReceivedBlockTransactions(
    const CBlock &block,
    CValidationState &state,
    CBlockIndex *pindexNew,
    const CDiskBlockPos &pos,
    const CDiskBlockMetaData& metaData)
{
    // Validate TTOR order for blocks that are MIN_TTOR_VALIDATION_DISTANCE blocks or more from active tip
    if (chainActive.Tip() && chainActive.Tip()->nHeight - pindexNew->nHeight >= MIN_TTOR_VALIDATION_DISTANCE)
    {
        if (!CheckBlockTTOROrder(block))
        {
            LogPrintf("Block %s at height %d violates TTOR order.\n", block.GetHash().ToString(), pindexNew->nHeight);
            // Mark the block itself as invalid.
            pindexNew->nStatus = pindexNew->nStatus.withFailed();
            setDirtyBlockIndex.insert(pindexNew);
            setBlockIndexCandidates.erase(pindexNew);
            InvalidateChain(pindexNew);
            InvalidChainFound(pindexNew);
            return state.Invalid(false, 0, "bad-blk-ttor");
        }
    }

    pindexNew->SetDiskBlockData(block.vtx.size(), pos, metaData);
    setDirtyBlockIndex.insert(pindexNew);

    if (pindexNew->pprev == nullptr || pindexNew->pprev->nChainTx) {
        // If pindexNew is the genesis block or all parents are
        // BLOCK_VALID_TRANSACTIONS.
        std::deque<CBlockIndex *> queue;
        queue.push_back(pindexNew);

        // Recursively process any descendant blocks that now may be eligible to
        // be connected.
        while (!queue.empty()) {
            CBlockIndex *pindex = queue.front();
            queue.pop_front();
            pindex->nChainTx =
                (pindex->pprev ? pindex->pprev->nChainTx : 0) + pindex->nTx;
            {
                LOCK(cs_nBlockSequenceId);
                pindex->nSequenceId = nBlockSequenceId++;
            }
            if (chainActive.Tip() == nullptr ||
                !setBlockIndexCandidates.value_comp()(pindex,
                                                      chainActive.Tip())) {
                setBlockIndexCandidates.insert(pindex);
            }
            std::pair<std::multimap<CBlockIndex *, CBlockIndex *>::iterator,
                      std::multimap<CBlockIndex *, CBlockIndex *>::iterator>
                range = mapBlocksUnlinked.equal_range(pindex);
            while (range.first != range.second) {
                std::multimap<CBlockIndex *, CBlockIndex *>::iterator it =
                    range.first;
                queue.push_back(it->second);
                range.first++;
                mapBlocksUnlinked.erase(it);
            }
        }
    } else if (pindexNew->pprev &&
               pindexNew->pprev->IsValid(BlockValidity::TREE)) {
        mapBlocksUnlinked.insert(std::make_pair(pindexNew->pprev, pindexNew));
    }

    return true;
}

/**
 * Return true if the provided block header is valid.
 * Only verify PoW if blockValidationOptions is configured to do so.
 * This allows validation of headers on which the PoW hasn't been done.
 * For example: to validate template handed to mining software.
 * Do not call this for any check that depends on the context.
 * For context-dependant calls, see ContextualCheckBlockHeader.
 */
static bool CheckBlockHeader(
    const Config &config, const CBlockHeader &block, CValidationState &state,
    BlockValidationOptions validationOptions = BlockValidationOptions()) {
    // Check proof of work matches claimed amount
    if (validationOptions.shouldValidatePoW() &&
        !CheckProofOfWork(block.GetHash(), block.nBits, config)) {
        return state.DoS(50, false, REJECT_INVALID, "high-hash", false,
                         "proof of work failed");
    }

    return true;
}

bool CheckBlock(const Config &config, const CBlock &block,
                CValidationState &state,
                int blockHeight,
                BlockValidationOptions validationOptions) {
    // These are checks that are independent of context.
    if (block.fChecked) {
        return true;
    }

    // Check that the header is valid (particularly PoW).  This is mostly
    // redundant with the call in AcceptBlockHeader.
    if (!CheckBlockHeader(config, block, state, validationOptions)) {
        return false;
    }

    // Check the merkle root.
    if (validationOptions.shouldValidateMerkleRoot()) {
        bool mutated;
        uint256 hashMerkleRoot2 = BlockMerkleRoot(block, &mutated);
        if (block.hashMerkleRoot != hashMerkleRoot2) {
            return state.DoS(100, false, REJECT_INVALID, "bad-txnmrklroot",
                             true, "hashMerkleRoot mismatch");
        }

        // Check for merkle tree malleability (CVE-2012-2459): repeating
        // sequences of transactions in a block without affecting the merkle
        // root of a block, while still invalidating it.
        if (mutated) {
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-duplicate",
                             true, "duplicate transaction");
        }
    }

    // All potential-corruption validation must be done before we do any
    // transaction validation, as otherwise we may mark the header as invalid
    // because we receive the wrong transactions for it.

    // First transaction must be coinbase.
    if (block.vtx.empty()) {
        return state.DoS(100, false, REJECT_INVALID, "bad-cb-missing", false,
                         "first tx is not coinbase");
    }

    // Size limits.
    auto nMaxBlockSize = config.GetMaxBlockSize();

    // Bail early if there is no way this block is of reasonable size.  
    if ( MIN_TRANSACTION_SIZE > 0 && block.vtx.size () > (nMaxBlockSize/MIN_TRANSACTION_SIZE)){
        return state.DoS(100, false, REJECT_INVALID, "bad-blk-length", false,"size limits failed");
    }

    auto currentBlockSize =
        ::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION);
    if (currentBlockSize > nMaxBlockSize) {
        return state.DoS(100, false, REJECT_INVALID, "bad-blk-length", false,
                         "size limits failed");
    }

    bool isGenesisEnabled = IsGenesisEnabled(config, blockHeight);
    uint64_t maxTxSigOpsCountConsensusBeforeGenesis = config.GetMaxTxSigOpsCountConsensusBeforeGenesis();
    uint64_t maxTxSizeConsensus = config.GetMaxTxSize(isGenesisEnabled, true);

    // And a valid coinbase.
    if (!CheckCoinbase(*block.vtx[0], state, maxTxSigOpsCountConsensusBeforeGenesis, maxTxSizeConsensus, isGenesisEnabled)) {
        return state.Invalid(false, state.GetRejectCode(),
                             state.GetRejectReason(),
                             strprintf("Coinbase check failed (txid %s) %s",
                                       block.vtx[0]->GetId().ToString(),
                                       state.GetDebugMessage()));
    }

    // Keep track of the sigops count.
    uint64_t nSigOps = 0;
    // Sigops are not counted after Genesis anymore
    auto nMaxSigOpsCountConsensusBeforeGenesis = config.GetMaxBlockSigOpsConsensusBeforeGenesis(currentBlockSize);

    // Check transactions
    auto txCount = block.vtx.size();
    auto *tx = block.vtx[0].get();

    size_t i = 0;
    while (true) {
        // After Genesis we don't count sigops when verifying blocks
        if (!isGenesisEnabled){
            // Count the sigops for the current transaction. If the total sigops
            // count is too high, the the block is invalid.
            bool sigOpCountError;
            nSigOps += GetSigOpCountWithoutP2SH(*tx, false, sigOpCountError);
            if (sigOpCountError || nSigOps > nMaxSigOpsCountConsensusBeforeGenesis) {
                return state.DoS(100, false, REJECT_INVALID, "bad-blk-sigops",
                    false, "out-of-bounds SigOpCount");
            }
        }
        // Go to the next transaction.
        i++;

        // We reached the end of the block, success.
        if (i >= txCount) {
            break;
        }

        // Check that the transaction is valid. Because this check differs for
        // the coinbase, the loop is arranged such as this only runs after at
        // least one increment.
        tx = block.vtx[i].get();
        if (!CheckRegularTransaction(*tx, state, maxTxSigOpsCountConsensusBeforeGenesis, maxTxSizeConsensus, isGenesisEnabled)) {
            return state.Invalid(
                false, state.GetRejectCode(), state.GetRejectReason(),
                strprintf("Transaction check failed (txid %s) %s",
                          tx->GetId().ToString(), state.GetDebugMessage()));
        }
    }

    if ((validationOptions.shouldValidatePoW() && validationOptions.shouldValidateMerkleRoot()) ||
         validationOptions.shouldMarkChecked())
    {
        block.fChecked = true;
    }

    return true;
}

static bool CheckIndexAgainstCheckpoint(const CBlockIndex *pindexPrev,
                                        CValidationState &state,
                                        const CChainParams &chainparams,
                                        const uint256 &hash) {
    int nHeight = pindexPrev->nHeight + 1;
    const CCheckpointData &checkpoints = chainparams.Checkpoints();

    // Check that the block chain matches the known block chain up to a
    // checkpoint.
    if (!Checkpoints::CheckBlock(checkpoints, nHeight, hash)) {
        return state.DoS(100, error("%s: rejected by checkpoint lock-in at %d",
                                    __func__, nHeight),
                         REJECT_CHECKPOINT, "checkpoint mismatch");
    }

    // Don't accept any forks from the main chain prior to last checkpoint.
    // GetLastCheckpoint finds the last checkpoint in MapCheckpoints that's in
    // our MapBlockIndex.
    CBlockIndex *pcheckpoint = Checkpoints::GetLastCheckpoint(checkpoints);
    if (pcheckpoint && nHeight < pcheckpoint->nHeight) {
        return state.DoS(
            100,
            error("%s: forked chain older than last checkpoint (height %d)",
                  __func__, nHeight),
            REJECT_CHECKPOINT, "bad-fork-prior-to-checkpoint");
    }

    return true;
}

static bool ContextualCheckBlockHeader(const Config &config,
                                       const CBlockHeader &block,
                                       CValidationState &state,
                                       const CBlockIndex *pindexPrev,
                                       int64_t nAdjustedTime) {
    const Consensus::Params &consensusParams =
        config.GetChainParams().GetConsensus();

    const int nHeight = pindexPrev == nullptr ? 0 : pindexPrev->nHeight + 1;

    // Check proof of work
    if (block.nBits != GetNextWorkRequired(pindexPrev, &block, config)) {
        LogPrintf("bad bits after height: %d\n", pindexPrev->nHeight);
        return state.DoS(100, false, REJECT_INVALID, "bad-diffbits", false,
                         "incorrect proof of work");
    }

    // Check timestamp against prev
    if (block.GetBlockTime() <= pindexPrev->GetMedianTimePast()) {
        return state.Invalid(false, REJECT_INVALID, "time-too-old",
                             "block's timestamp is too early");
    }

    // Check timestamp
    if (block.GetBlockTime() > nAdjustedTime + MAX_FUTURE_BLOCK_TIME) {
        return state.Invalid(false, REJECT_INVALID, "time-too-new",
                             "block timestamp too far in the future");
    }

    // Reject outdated version blocks when 95% (75% on testnet) of the network
    // has upgraded:
    // check for version 2, 3 and 4 upgrades
    if ((block.nVersion < 2 && nHeight >= consensusParams.BIP34Height) ||
        (block.nVersion < 3 && nHeight >= consensusParams.BIP66Height) ||
        (block.nVersion < 4 && nHeight >= consensusParams.BIP65Height)) {
        return state.Invalid(
            false, REJECT_OBSOLETE,
            strprintf("bad-version(0x%08x)", block.nVersion),
            strprintf("rejected nVersion=0x%08x block", block.nVersion));
    }

    return true;
}

bool ContextualCheckTransaction(const Config &config, const CTransaction &tx,
                                CValidationState &state, int nHeight,
                                int64_t nLockTimeCutoff, bool fromBlock)
{
    if(!IsFinalTx(tx, nHeight, nLockTimeCutoff))
    {
        state.SetNonFinal();
        if(!fromBlock && IsGenesisEnabled(config, nHeight))
        {
            return false;
        }

        // While this is only one transaction, we use txns in the error to
        // ensure continuity with other clients.
        return state.DoS(10, false, REJECT_INVALID, "bad-txns-nonfinal", false,
                         "non-final transaction");
    }

    return true;
}

bool ContextualCheckTransactionForCurrentBlock(
    const Config &config,
    const CTransaction &tx,
    int nChainActiveHeight,
    int nMedianTimePast,
    CValidationState &state,
    int flags) {

    // By convention a negative value for flags indicates that the current
    // network-enforced consensus rules should be used. In a future soft-fork
    // scenario that would mean checking which rules would be enforced for the
    // next block and setting the appropriate flags. At the present time no
    // soft-forks are scheduled, so no flags are set.
    flags = std::max(flags, 0);

    // ContextualCheckTransactionForCurrentBlock() uses chainActive.Height()+1
    // to evaluate nLockTime because when IsFinalTx() is called within
    // CBlock::AcceptBlock(), the height of the block *being* evaluated is what
    // is used. Thus if we want to know if a transaction can be part of the
    // *next* block, we need to call ContextualCheckTransaction() with one more
    // than chainActive.Height().
    const int nBlockHeight = nChainActiveHeight + 1;

    // BIP113 will require that time-locked transactions have nLockTime set to
    // less than the median time of the previous block they're contained in.
    // When the next block is created its previous block will be the current
    // chain tip, so we use that to calculate the median time passed to
    // ContextualCheckTransaction() if LOCKTIME_MEDIAN_TIME_PAST is set.
    const int64_t nLockTimeCutoff = (flags & LOCKTIME_MEDIAN_TIME_PAST)
                                        ? nMedianTimePast
                                        : GetAdjustedTime();

    return ContextualCheckTransaction(
                config,
                tx,
                state,
                nBlockHeight,
                nLockTimeCutoff,
                false);
}

static bool ContextualCheckBlock(const Config &config, const CBlock &block,
                                 CValidationState &state,
                                 const CBlockIndex *pindexPrev) {
    const int nHeight = pindexPrev == nullptr ? 0 : pindexPrev->nHeight + 1;
    const Consensus::Params &consensusParams =
        config.GetChainParams().GetConsensus();

    // Start enforcing BIP113 (Median Time Past)
    int nLockTimeFlags = 0;
    if (nHeight >= consensusParams.CSVHeight) {
        nLockTimeFlags |= LOCKTIME_MEDIAN_TIME_PAST;
    }

    // Check if block has the right size. Maximum accepted block size changes
    // according to predetermined schedule unless user has overriden this by 
    // specifying -excessiveblocksize command line parameter 
    const int64_t nMedianTimePast =
        pindexPrev == nullptr ? 0 : pindexPrev->GetMedianTimePast();

    const uint64_t nMaxBlockSize = config.GetMaxBlockSize();

    const uint64_t currentBlockSize =
        ::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION);
    if (currentBlockSize > nMaxBlockSize) {
        return state.DoS(100, false, REJECT_INVALID, "bad-blk-length",
                        false, "size limits failed");
    }

    const int64_t nLockTimeCutoff = (nLockTimeFlags & LOCKTIME_MEDIAN_TIME_PAST)
                                        ? nMedianTimePast
                                        : block.GetBlockTime();

    // Check that all transactions are finalized
    for (const auto &tx : block.vtx) {
        if (!ContextualCheckTransaction(config, *tx, state, nHeight,
                                        nLockTimeCutoff, true)) {
            // state set by ContextualCheckTransaction.
            return false;
        }
    }

    // Enforce rule that the coinbase starts with serialized block height
    if (nHeight >= consensusParams.BIP34Height) {
        CScript expect = CScript() << nHeight;
        if (block.vtx[0]->vin[0].scriptSig.size() < expect.size() ||
            !std::equal(expect.begin(), expect.end(),
                        block.vtx[0]->vin[0].scriptSig.begin())) {
            return state.DoS(100, false, REJECT_INVALID, "bad-cb-height", false,
                             "block height mismatch in coinbase");
        }
    }

    return true;
}

/**
 * If found, returns an index of a previous block. 
 */
static const CBlockIndex* FindPreviousBlockIndex(const CBlockHeader &block, CValidationState &state)
{
    AssertLockHeld(cs_main);

    CBlockIndex* ppindex = nullptr;

    BlockMap::iterator mi = mapBlockIndex.find(block.hashPrevBlock);
    if (mi != mapBlockIndex.end())
    {
        ppindex = (*mi).second;
        if (ppindex->nStatus.isInvalid())
        {
            state.DoS(100, error("%s: prev block invalid", __func__), REJECT_INVALID, "bad-prevblk");
            ppindex = nullptr;
        }
    }
    else
    {
        state.DoS(10, error("%s: prev block not found", __func__), 0, "prev-blk-not-found");
    }

    return ppindex;
}

/**
 * If the provided block header is valid, add it to the block index.
 *
 * Returns true if the block is succesfully added to the block index.
 */
static bool AcceptBlockHeader(const Config &config, const CBlockHeader &block,
                              CValidationState &state, CBlockIndex **ppindex) {
    AssertLockHeld(cs_main);
    const CChainParams &chainparams = config.GetChainParams();

    uint256 hash = block.GetHash();
    
    if (config.IsBlockInvalidated(hash))
    {
        return state.Invalid(error("%s: block %s is marked as invalid from command line",
                                   __func__, hash.ToString()),
                             10, "block is marked as invalid");
    }

    // Check for duplicate
    BlockMap::iterator miSelf = mapBlockIndex.find(hash);
    CBlockIndex *pindex = nullptr;
    if (hash != chainparams.GetConsensus().hashGenesisBlock) {
        if (miSelf != mapBlockIndex.end()) {
            // Block header is already known.
            pindex = miSelf->second;
            if (ppindex) {
                *ppindex = pindex;
            }
            if (pindex->nStatus.isInvalid()) {
                return state.Invalid(error("%s: block %s is marked invalid",
                                           __func__, hash.ToString()),
                                     0, "duplicate");
            }
            return true;
        }

        if (!CheckBlockHeader(config, block, state)) {
            return error("%s: Consensus::CheckBlockHeader: %s, %s", __func__,
                         hash.ToString(), FormatStateMessage(state));
        }

        const CBlockIndex *pindexPrev = FindPreviousBlockIndex(block, state);
        if (!pindexPrev)
        {
            // Error state is logged in FindPreviousBlockIndex
            return false;
        }

        if (fCheckpointsEnabled &&
            !CheckIndexAgainstCheckpoint(pindexPrev, state, chainparams,
                                         hash)) {
            return error("%s: CheckIndexAgainstCheckpoint(): %s", __func__,
                         state.GetRejectReason().c_str());
        }

        if (!ContextualCheckBlockHeader(config, block, state, pindexPrev,
                                        GetAdjustedTime())) {
            return error("%s: Consensus::ContextualCheckBlockHeader: %s, %s",
                         __func__, hash.ToString(), FormatStateMessage(state));
        }
    }

    if (pindex == nullptr) {
        pindex = AddToBlockIndex(block);
    }

    if (ppindex) {
        *ppindex = pindex;
    }

    CheckBlockIndex(chainparams.GetConsensus());

    return true;
}

// Exposed wrapper for AcceptBlockHeader
bool ProcessNewBlockHeaders(const Config &config,
                            const std::vector<CBlockHeader> &headers,
                            CValidationState &state,
                            const CBlockIndex **ppindex) {
    {
        LOCK(cs_main);
        for (const CBlockHeader &header : headers) {
            // Use a temp pindex instead of ppindex to avoid a const_cast
            CBlockIndex *pindex = nullptr;
            if (!AcceptBlockHeader(config, header, state, &pindex)) {
                return false;
            }
            if (ppindex) {
                *ppindex = pindex;
            }
        }
    }
    NotifyHeaderTip();
    return true;
}

/**
 * Store a block on disk.
 *
 * @param[in]     config     The global config.
 * @param[in-out] pblock     The block we want to accept.
 * @param[out]    ppindex    The last new block index, only set if the block
 *                           was accepted.
 * @param[in]     fRequested A boolean to indicate if this block was requested
 *                           from our peers.
 * @param[in]     dbp        If non-null, the disk position of the block.
 * @param[in-out] fNewBlock  True if block was first received via this call.
 * @return True if the block is accepted as a valid block and written to disk.
 */
static bool AcceptBlock(const Config& config,
    const std::shared_ptr<const CBlock>& pblock,
    CValidationState& state, CBlockIndex** ppindex,
    bool fRequested, const CDiskBlockPos* dbp,
    bool* fNewBlock) {
    AssertLockHeld(cs_main);

    const CBlock& block = *pblock;
    if (fNewBlock) {
        *fNewBlock = false;
    }

    CBlockIndex* pindexDummy = nullptr;
    CBlockIndex*& pindex = ppindex ? *ppindex : pindexDummy;

    if (!AcceptBlockHeader(config, block, state, &pindex)) {
        return false;
    }

    // Try to process all requested blocks that we don't have, but only
    // process an unrequested block if it's new and has enough work to
    // advance our tip, and isn't too many blocks ahead.
    bool fAlreadyHave = pindex->nStatus.hasData();

    // Compare block header timestamps and received times of the block and the
    // chaintip.  If they have the same chain height, just log the time
    // difference for both.
    int64_t newBlockTimeDiff = std::llabs(pindex->GetReceivedTimeDiff());
    int64_t chainTipTimeDiff =
        chainActive.Tip() ? std::llabs(chainActive.Tip()->GetReceivedTimeDiff())
        : 0;

    bool isSameHeightAndMoreHonestlyMined =
        chainActive.Tip() &&
        (pindex->nChainWork == chainActive.Tip()->nChainWork) &&
        (newBlockTimeDiff < chainTipTimeDiff);
    if (isSameHeightAndMoreHonestlyMined) {
        LogPrintf("Chain tip timestamp-to-received-time difference: hash=%s, "
            "diff=%d\n",
            chainActive.Tip()->GetBlockHash().ToString(),
            chainTipTimeDiff);
        LogPrintf("New block timestamp-to-received-time difference: hash=%s, "
            "diff=%d\n",
            pindex->GetBlockHash().ToString(), newBlockTimeDiff);
    }

    bool fHasMoreWork =
        (chainActive.Tip() ? pindex->nChainWork > chainActive.Tip()->nChainWork
            : true);
    // Blocks that are too out-of-order needlessly limit the effectiveness of
    // pruning, because pruning will not delete block files that contain any
    // blocks which are too close in height to the tip.  Apply this test
    // regardless of whether pruning is enabled; it should generally be safe to
    // not process unrequested blocks.
    bool fTooFarAhead =
        (pindex->nHeight > int(chainActive.Height() + MIN_BLOCKS_TO_KEEP));

    // TODO: Decouple this function from the block download logic by removing
    // fRequested
    // This requires some new chain datastructure to efficiently look up if a
    // block is in a chain leading to a candidate for best tip, despite not
    // being such a candidate itself.

    // TODO: deal better with return value and error conditions for duplicate
    // and unrequested blocks.
    if (fAlreadyHave) {
        return true;
    }

    // If we didn't ask for it:
    if (!fRequested) {
        // This is a previously-processed block that was pruned.
        if (pindex->nTx != 0) {
            return true;
        }

        // Don't process less-work chains.
        if (!fHasMoreWork) {
            return true;
        }

        // Block height is too high.
        if (fTooFarAhead) {
            return true;
        }
    }

    if (fNewBlock) {
        *fNewBlock = true;
    }

    if (!CheckBlock(config, block, state, pindex->nHeight) ||
        !ContextualCheckBlock(config, block, state, pindex->pprev)) {
        if (state.IsInvalid() && !state.CorruptionPossible()) {
            pindex->nStatus = pindex->nStatus.withFailed();
            setDirtyBlockIndex.insert(pindex);
        }
        return error("%s: %s (block %s)", __func__, FormatStateMessage(state),
            block.GetHash().ToString());
    }

    // Header is valid/has work and the merkle tree is good.
    // Relay now, but if it does not build on our best tip, let the
    // SendMessages loop relay it.
    if (!IsInitialBlockDownload() && chainActive.Tip() == pindex->pprev) {
        GetMainSignals().NewPoWValidBlock(pindex, pblock);
    }

    int nHeight = pindex->nHeight;
    const CChainParams& chainparams = config.GetChainParams();

    // Write block to history file
    try {
        uint64_t nBlockSize =
            ::GetSerializeSize(block, SER_DISK, CLIENT_VERSION);
        CDiskBlockPos blockPos;
        if (dbp != nullptr) {
            blockPos = *dbp;
        }
        if (!pBlockFileInfoStore->FindBlockPos(config, state, blockPos,
            (nBlockSize + GetBlockFileBlockHeaderSize(nBlockSize)), nHeight,
            block.GetBlockTime(), fCheckForPruning, dbp != nullptr)) {
            return error("AcceptBlock(): FindBlockPos failed");
        }
        CDiskBlockMetaData metaData;
        if (dbp == nullptr) {
            if (!WriteBlockToDisk(block, blockPos, chainparams.DiskMagic(), metaData)) {
                AbortNode(state, "Failed to write block");
            }
        }
        if (!ReceivedBlockTransactions(block, state, pindex, blockPos, metaData)) {
            return error("AcceptBlock(): ReceivedBlockTransactions failed");
        }
    }
    catch (const std::runtime_error& e) {
        return AbortNode(state, std::string("System error: ") + e.what());
    }

    if (fCheckForPruning) {
        // we just allocated more disk space for block files.
        FlushStateToDisk(config.GetChainParams(), state, FLUSH_STATE_NONE);
    }

    if (!chainActive.Contains(pindex))
    {
        // if we are accepting block from fork check if it changes safe mode level
        CheckSafeModeParameters(pindex);
    }

    return true;
}

bool VerifyNewBlock(const Config &config,
                    const std::shared_ptr<const CBlock> pblock) {

    CValidationState state;
    BlockValidationOptions validationOptions{false, true};
    const CBlockIndex *pindexPrev = nullptr;

    {
        LOCK(cs_main);
        
        pindexPrev = FindPreviousBlockIndex(*pblock, state);
        if (!pindexPrev)
        {
            return false;
        }
    }

    bool ret = CheckBlock(config, *pblock, state, pindexPrev->nHeight + 1, validationOptions);

    GetMainSignals().BlockChecked(*pblock, state);

    if (!ret) {
        return error("%s: VerifyNewBlock FAILED", __func__);
    }

    return true;
}

std::function<bool()> ProcessNewBlockWithAsyncBestChainActivation(
    task::CCancellationToken&& token,
    const Config& config,
    const std::shared_ptr<const CBlock>& pblock,
    bool fForceProcessing,
    bool* fNewBlock)
{
    auto guard = CBlockProcessing::GetCountGuard();

    {
        CBlockIndex *pindex = nullptr;
        if (fNewBlock) {
            *fNewBlock = false;
        }

        const CChainParams &chainparams = config.GetChainParams();

        CValidationState state;
        const CBlockIndex *pindexPrev = nullptr;

        {
            LOCK(cs_main);
            // We need previous block index to calculate current block height used by CheckBlock. This check is later repeated in AcceptBlockHeader
            pindexPrev = FindPreviousBlockIndex(*pblock, state);
            if (!pindexPrev)
            {
                return {};
            }
        }

        // Ensure that CheckBlock() passes before calling AcceptBlock, as
        // belt-and-suspenders.
        bool ret = CheckBlock(config, *pblock, state, pindexPrev->nHeight + 1);

        LOCK(cs_main);

        if (ret) {
            // Store to disk
            ret = AcceptBlock(config, pblock, state, &pindex, fForceProcessing,
                              nullptr, fNewBlock);
        }
        CheckBlockIndex(chainparams.GetConsensus());
        if (!ret) {
            GetMainSignals().BlockChecked(*pblock, state);
            error("%s: AcceptBlock FAILED", __func__);

            return {};
        }
    }

    NotifyHeaderTip();

    auto bestChainActivation =
        [&config, pblock, guard, token]
        {
            // dummyState is used to report errors, not block related invalidity - ignore it
            // (see description of ActivateBestChain)
            CValidationState dummyState;

            CJournalChangeSetPtr changeSet { mempool.getJournalBuilder()->getNewChangeSet(JournalUpdateReason::NEW_BLOCK) };

            if (!ActivateBestChain(
                    token,
                    config,
                    dummyState,
                    changeSet,
                    pblock))
            {
                return error("%s: ActivateBestChain failed", __func__);
            }

            return true;
        };

    return bestChainActivation;
}

bool ProcessNewBlock(const Config &config,
                     const std::shared_ptr<const CBlock>& pblock,
                     bool fForceProcessing, bool *fNewBlock)
{
    auto source = task::CCancellationSource::Make();
    auto bestChainActivation =
        ProcessNewBlockWithAsyncBestChainActivation(
            task::CCancellationToken::JoinToken(source->GetToken(), GetShutdownToken()), config, pblock, fForceProcessing, fNewBlock);

    if(!bestChainActivation)
    {
        return false;
    }

    return bestChainActivation();
}

int GetProcessingBlocksCount()
{
    return CBlockProcessing::Count();
}

bool TestBlockValidity(const Config &config, CValidationState &state,
                       const CBlock &block, CBlockIndex *pindexPrev,
                       BlockValidationOptions validationOptions) {
    AssertLockHeld(cs_main);
    const CChainParams &chainparams = config.GetChainParams();

    assert(pindexPrev && pindexPrev == chainActive.Tip());
    if (fCheckpointsEnabled &&
        !CheckIndexAgainstCheckpoint(pindexPrev, state, chainparams,
                                     block.GetHash())) {
        return error("%s: CheckIndexAgainstCheckpoint(): %s", __func__,
                     state.GetRejectReason().c_str());
    }

    CCoinsViewCache viewNew(pcoinsTip);
    CBlockIndex indexDummy(block);
    uint256 dummyHash;
    indexDummy.phashBlock = &dummyHash;
    indexDummy.pprev = pindexPrev;
    indexDummy.nHeight = pindexPrev->nHeight + 1;
    indexDummy.SetChainWork();

    // NOTE: CheckBlockHeader is called by CheckBlock
    if (!ContextualCheckBlockHeader(config, block, state, pindexPrev,
                                    GetAdjustedTime())) {
        return error("%s: Consensus::ContextualCheckBlockHeader: %s", __func__,
                     FormatStateMessage(state));
    }
    if (!CheckBlock(config, block, state, indexDummy.nHeight, validationOptions)) {
        return error("%s: Consensus::CheckBlock: %s", __func__,
                     FormatStateMessage(state));
    }
    if (!ContextualCheckBlock(config, block, state, pindexPrev)) {
        return error("%s: Consensus::ContextualCheckBlock: %s", __func__,
                     FormatStateMessage(state));
    }
    auto source = task::CCancellationSource::Make();
    if (!ConnectBlock(source->GetToken(), false, config, block, state, &indexDummy, viewNew, indexDummy.nChainWork, true))
    {
        return false;
    }

    assert(state.IsValid());
    return true;
}

/**
 * BLOCK PRUNING CODE
 */

/**
 * Prune a block file (modify associated database entries)
 */
static void PruneOneBlockFile(const int fileNumber) {
    for (const std::pair<const uint256, CBlockIndex *> &it : mapBlockIndex) {
        CBlockIndex *pindex = it.second;
        if (pindex->nFile == fileNumber) {
            pindex->ClearFileInfo();
            setDirtyBlockIndex.insert(pindex);

            // Prune from mapBlocksUnlinked -- any block we prune would have
            // to be downloaded again in order to consider its chain, at which
            // point it would be considered as a candidate for
            // mapBlocksUnlinked or setBlockIndexCandidates.
            std::pair<std::multimap<CBlockIndex *, CBlockIndex *>::iterator,
                      std::multimap<CBlockIndex *, CBlockIndex *>::iterator>
                range = mapBlocksUnlinked.equal_range(pindex->pprev);
            while (range.first != range.second) {
                std::multimap<CBlockIndex *, CBlockIndex *>::iterator _it =
                    range.first;
                range.first++;
                if (_it->second == pindex) {
                    mapBlocksUnlinked.erase(_it);
                }
            }
        }
    }

    pBlockFileInfoStore->ClearFileInfo(fileNumber);
}

void UnlinkPrunedFiles(const std::set<int> &setFilesToPrune)
{
    for (const int i : setFilesToPrune)
    {
        CDiskBlockPos pos{i, 0};
        boost::system::error_code ec;
        fs::remove(GetBlockPosFilename(pos, "blk"), ec);

        if(!ec) // if there was no error
        {
            // only delete rev file and remove block index data if blk file
            // deletion succeeded otherwise keep the data for now as it's most
            // likely still being used
            fs::remove(GetBlockPosFilename(pos, "rev"));
            PruneOneBlockFile(i);
            LogPrintf("Prune: %s deleted blk/rev (%05u)\n", __func__, i);
        }
        else
        {
            LogPrintf(
                "Prune: %s deletion skipped blk/rev (%05u). "
                "File is most likely still in use\n",
                __func__,
                i);
        }
    }
}

/* This function is called from the RPC code for pruneblockchain */
void PruneBlockFilesManual(int nManualPruneHeight) {
    CValidationState state;
    const CChainParams &chainparams = Params();
    FlushStateToDisk(chainparams, state, FLUSH_STATE_NONE, nManualPruneHeight);
}


bool CheckDiskSpace(uint64_t nAdditionalBytes) {
    uint64_t nFreeBytesAvailable = fs::space(GetDataDir()).available;

    // Check for nMinDiskSpace bytes (currently 50MB)
    if (nFreeBytesAvailable < nMinDiskSpace + nAdditionalBytes) {
        return AbortNode("Disk space is low!", _("Error: Disk space is low!"));
    }

    return true;
}

FILE *CDiskFiles::OpenDiskFile(const CDiskBlockPos &pos, const char *prefix,
                          bool fReadOnly) {
    if (pos.IsNull()) {
        return nullptr;
    }

    fs::path path = GetBlockPosFilename(pos, prefix);
    fs::create_directories(path.parent_path());
    FILE *file = fsbridge::fopen(path, "rb+");
    if (!file && !fReadOnly) {
        file = fsbridge::fopen(path, "wb+");
    }
    if (!file) {
        LogPrintf("Unable to open file %s\n", path.string());
        return nullptr;
    }
    if (pos.nPos) {
        if (fseek(file, pos.nPos, SEEK_SET)) {
            LogPrintf("Unable to seek to position %u of %s\n", pos.nPos,
                      path.string());
            fclose(file);
            return nullptr;
        }
    }
    return file;
}

FILE *CDiskFiles::OpenBlockFile(const CDiskBlockPos &pos, bool fReadOnly) {
    return OpenDiskFile(pos, "blk", fReadOnly);
}

FILE *CDiskFiles::OpenUndoFile(const CDiskBlockPos &pos, bool fReadOnly) {
    return OpenDiskFile(pos, "rev", fReadOnly);
}

fs::path GetBlockPosFilename(const CDiskBlockPos &pos, const char *prefix) {
    return GetDataDir() / "blocks" / strprintf("%s%05u.dat", prefix, pos.nFile);
}

CBlockIndex *InsertBlockIndex(uint256 hash) {
    if (hash.IsNull()) {
        return nullptr;
    }

    // Return existing
    BlockMap::iterator mi = mapBlockIndex.find(hash);
    if (mi != mapBlockIndex.end()) {
        return (*mi).second;
    }

    // Create new
    CBlockIndex *pindexNew = new CBlockIndex();
    if (!pindexNew) {
        throw std::runtime_error(std::string(__func__) +
                                 ": new CBlockIndex failed");
    }

    mi = mapBlockIndex.insert(std::make_pair(hash, pindexNew)).first;
    pindexNew->phashBlock = &((*mi).first);

    return pindexNew;
}


static bool LoadBlockIndexDB(const CChainParams &chainparams) {
    if (!pblocktree->LoadBlockIndexGuts(InsertBlockIndex)) {
        return false;
    }

    boost::this_thread::interruption_point();

    // Calculate nChainWork
    std::vector<std::pair<int, CBlockIndex *>> vSortedByHeight;
    vSortedByHeight.reserve(mapBlockIndex.size());
    for (const std::pair<uint256, CBlockIndex *> &item : mapBlockIndex) {
        CBlockIndex *pindex = item.second;
        vSortedByHeight.push_back(std::make_pair(pindex->nHeight, pindex));
    }
    sort(vSortedByHeight.begin(), vSortedByHeight.end());
    for (const std::pair<int, CBlockIndex *> &item : vSortedByHeight) {
        CBlockIndex *pindex = item.second;
        pindex->SetChainWork();
        pindex->nTimeMax =
            (pindex->pprev ? std::max(pindex->pprev->nTimeMax, pindex->nTime)
                           : pindex->nTime);
        // We can link the chain of blocks for which we've received transactions
        // at some point. Pruned nodes may have deleted the block.
        if (pindex->nTx > 0) {
            if (pindex->pprev) {
                if (pindex->pprev->nChainTx) {
                    pindex->nChainTx = pindex->pprev->nChainTx + pindex->nTx;
                } else {
                    pindex->nChainTx = 0;
                    mapBlocksUnlinked.insert(
                        std::make_pair(pindex->pprev, pindex));
                }
            } else {
                pindex->nChainTx = pindex->nTx;
            }
        }
        if (pindex->IsValid(BlockValidity::TRANSACTIONS) &&
            (pindex->nChainTx || pindex->pprev == nullptr)) {
            setBlockIndexCandidates.insert(pindex);
        }
        if (pindex->nStatus.isInvalid() &&
            (!pindexBestInvalid ||
             pindex->nChainWork > pindexBestInvalid->nChainWork)) {
            pindexBestInvalid = pindex;
        }
        if (pindex->pprev) {
            pindex->BuildSkip();
        }
        if (pindex->IsValid(BlockValidity::TREE) &&
            (pindexBestHeader == nullptr ||
             CBlockIndexWorkComparator()(pindexBestHeader, pindex))) {
            pindexBestHeader = pindex;
        }
    }

    // Load block file info
    int nLastBlockFileLocal = 0;
    pblocktree->ReadLastBlockFile(nLastBlockFileLocal);
    pBlockFileInfoStore->LoadBlockFileInfo(nLastBlockFileLocal, *pblocktree);

    // Check presence of blk files
    LogPrintf("Checking all blk files are present...\n");
    std::set<int> setBlkDataFiles;
    for (const std::pair<uint256, CBlockIndex *> &item : mapBlockIndex) {
        CBlockIndex *pindex = item.second;
        if (pindex->nStatus.hasData()) {
            setBlkDataFiles.insert(pindex->nFile);
        }
    }
    for (const int i : setBlkDataFiles) {
        CDiskBlockPos pos(i, 0);
        if (CAutoFile(CDiskFiles::OpenBlockFile(pos, true), SER_DISK, CLIENT_VERSION)
                .IsNull()) {
            return false;
        }
    }

    // Check whether we have ever pruned block & undo files
    pblocktree->ReadFlag("prunedblockfiles", fHavePruned);
    if (fHavePruned) {
        LogPrintf(
            "LoadBlockIndexDB(): Block files have previously been pruned\n");
    }

    // Check whether we need to continue reindexing
    bool fReindexing = false;
    pblocktree->ReadReindexing(fReindexing);
    fReindex |= fReindexing;

    // Check whether we have a transaction index
    pblocktree->ReadFlag("txindex", fTxIndex);
    LogPrintf("%s: transaction index %s\n", __func__,
              fTxIndex ? "enabled" : "disabled");

    return true;
}

void LoadChainTip(const CChainParams &chainparams) {
    if (chainActive.Tip() &&
        chainActive.Tip()->GetBlockHash() == pcoinsTip->GetBestBlock()) {
        return;
    }

    // Load pointer to end of best chain
    BlockMap::iterator it = mapBlockIndex.find(pcoinsTip->GetBestBlock());
    if (it == mapBlockIndex.end()) {
        return;
    }

    chainActive.SetTip(it->second);
    chainActiveSharedData.SetChainActiveHeight(chainActive.Height());
    chainActiveSharedData.SetChainActiveTipBlockHash(chainActive.Tip()->GetBlockHash());

    PruneBlockIndexCandidates();

    LogPrintf(
        "Loaded best chain: hashBestChain=%s height=%d date=%s progress=%f\n",
        chainActive.Tip()->GetBlockHash().ToString(), chainActive.Height(),
        DateTimeStrFormat("%Y-%m-%d %H:%M:%S",
                          chainActive.Tip()->GetBlockTime()),
        GuessVerificationProgress(chainparams.TxData(), chainActive.Tip()));
}

CVerifyDB::CVerifyDB() {
    uiInterface.ShowProgress(_("Verifying blocks..."), 0);
}

CVerifyDB::~CVerifyDB() {
    uiInterface.ShowProgress("", 100);
}

bool CVerifyDB::VerifyDB(const Config &config, CCoinsView *coinsview,
                         int nCheckLevel, int nCheckDepth, const task::CCancellationToken& shutdownToken) {
    LOCK(cs_main);
    if (chainActive.Tip() == nullptr || chainActive.Tip()->pprev == nullptr) {
        return true;
    }

    // Verify blocks in the best chain
    if (nCheckDepth <= 0) {
        // suffices until the year 19000
        nCheckDepth = 1000000000;
    }

    if (nCheckDepth > chainActive.Height()) {
        nCheckDepth = chainActive.Height();
    }

    nCheckLevel = std::max(0, std::min(4, nCheckLevel));
    LogPrintf("Verifying last %i blocks at level %i\n", nCheckDepth,
              nCheckLevel);

    CCoinsViewCache coins(coinsview);
    CBlockIndex *pindexState = chainActive.Tip();
    CBlockIndex *pindexFailure = nullptr;
    size_t nGoodTransactions = 0;
    CValidationState state;
    int reportDone = 0;
    LogPrintf("[0%%]...");
    for (CBlockIndex *pindex = chainActive.Tip(); pindex && pindex->pprev;
         pindex = pindex->pprev) {
        int percentageDone = std::max(
            1, std::min(
                   99,
                   (int)(((double)(chainActive.Height() - pindex->nHeight)) /
                         (double)nCheckDepth * (nCheckLevel >= 4 ? 50 : 100))));

        if (reportDone < percentageDone / 10) {
            // report every 10% step
            LogPrintf("[%d%%]...", percentageDone);
            reportDone = percentageDone / 10;
        }

        uiInterface.ShowProgress(_("Verifying blocks..."), percentageDone);
        if (pindex->nHeight < chainActive.Height() - nCheckDepth) {
            break;
        }

        if (fPruneMode && !pindex->nStatus.hasData()) {
            // If pruning, only go back as far as we have data.
            LogPrintf("VerifyDB(): block verification stopping at height %d "
                      "(pruning, no data)\n",
                      pindex->nHeight);
            break;
        }

        CBlock block;

        // check level 0: read from disk
        if (!ReadBlockFromDisk(block, pindex, config)) {
            return error(
                "VerifyDB(): *** ReadBlockFromDisk failed at %d, hash=%s",
                pindex->nHeight, pindex->GetBlockHash().ToString());
        }

        if (shutdownToken.IsCanceled())
        {
            return true;
        }

        // check level 1: verify block validity
        if (nCheckLevel >= 1 && !CheckBlock(config, block, state, pindex->nHeight)) {
            return error("%s: *** found bad block at %d, hash=%s (%s)\n",
                         __func__, pindex->nHeight,
                         pindex->GetBlockHash().ToString(),
                         FormatStateMessage(state));
        }

        if (shutdownToken.IsCanceled())
        {
            return true;
        }

        // check level 2: verify undo validity
        if (nCheckLevel >= 2 && pindex) {
            CBlockUndo undo;
            CDiskBlockPos pos = pindex->GetUndoPos();
            if (!pos.IsNull()) {
                if (!UndoReadFromDisk(undo, pos,
                                      pindex->pprev->GetBlockHash())) {
                    return error(
                        "VerifyDB(): *** found bad undo data at %d, hash=%s\n",
                        pindex->nHeight, pindex->GetBlockHash().ToString());
                }
            }
        }

        if (shutdownToken.IsCanceled())
        {
            return true;
        }

        // check level 3: check for inconsistencies during memory-only
        // disconnect of tip blocks
        if (nCheckLevel >= 3 && pindex == pindexState &&
            (coins.DynamicMemoryUsage() + pcoinsTip->DynamicMemoryUsage()) <=
                nCoinCacheUsage) {
            assert(coins.GetBestBlock() == pindex->GetBlockHash());
            DisconnectResult res = DisconnectBlock(block, pindex, coins, shutdownToken);
            if (res == DISCONNECT_FAILED && !shutdownToken.IsCanceled()) {
                return error("VerifyDB(): *** irrecoverable inconsistency in "
                             "block data at %d, hash=%s",
                             pindex->nHeight,
                             pindex->GetBlockHash().ToString());
            }
            pindexState = pindex->pprev;
            if (res == DISCONNECT_UNCLEAN) {
                nGoodTransactions = 0;
                pindexFailure = pindex;
            } else {
                nGoodTransactions += block.vtx.size();
            }
        }

        if (shutdownToken.IsCanceled()) {
            return true;
        }
    }

    if (pindexFailure) {
        return error("VerifyDB(): *** coin database inconsistencies found "
                     "(last %i blocks, %zu good transactions before that)\n",
                     chainActive.Height() - pindexFailure->nHeight + 1,
                     nGoodTransactions);
    }

    // check level 4: try reconnecting blocks
    if (nCheckLevel >= 4) {
        CBlockIndex *pindex = pindexState;
        while (pindex != chainActive.Tip()) {
            uiInterface.ShowProgress(
                _("Verifying blocks..."),
                std::max(1,
                         std::min(99,
                                  100 - (int)(((double)(chainActive.Height() -
                                                        pindex->nHeight)) /
                                              (double)nCheckDepth * 50))));
            pindex = chainActive.Next(pindex);
            CBlock block;
            if (!ReadBlockFromDisk(block, pindex, config)) {
                return error(
                    "VerifyDB(): *** ReadBlockFromDisk failed at %d, hash=%s",
                    pindex->nHeight, pindex->GetBlockHash().ToString());
            }
            auto source = task::CCancellationSource::Make();
            if (!ConnectBlock(source->GetToken(), false, config, block, state, pindex, coins, pindex->nChainWork)) {
                return error(
                    "VerifyDB(): *** found unconnectable block at %d, hash=%s",
                    pindex->nHeight, pindex->GetBlockHash().ToString());
            }
        }
    }

    LogPrintf("[DONE].\n");
    LogPrintf("No coin database inconsistencies in last %i blocks (%zu "
              "transactions)\n",
              chainActive.Height() - pindexState->nHeight, nGoodTransactions);

    return true;
}

/**
 * Apply the effects of a block on the utxo cache, ignoring that it may already
 * have been applied.
 */
static bool RollforwardBlock(const CBlockIndex *pindex, CCoinsViewCache &inputs,
                             const Config &config) {
    // TODO: merge with ConnectBlock
    CBlock block;
    if (!ReadBlockFromDisk(block, pindex, config)) {
        return error("ReplayBlock(): ReadBlockFromDisk failed at %d, hash=%s",
                     pindex->nHeight, pindex->GetBlockHash().ToString());
    }

    for (const CTransactionRef &tx : block.vtx) {
        if (!tx->IsCoinBase()) {
            for (const CTxIn &txin : tx->vin) {
                inputs.SpendCoin(txin.prevout);
            }
        }

        // Pass check = true as every addition may be an overwrite.
        AddCoins(inputs, *tx, pindex->nHeight, config.GetGenesisActivationHeight(), true);
    }

    return true;
}

bool ReplayBlocks(const Config &config, CCoinsView *view) {
    LOCK(cs_main);

    CCoinsViewCache cache(view);

    std::vector<uint256> hashHeads = view->GetHeadBlocks();
    if (hashHeads.empty()) {
        // We're already in a consistent state.
        return true;
    }
    if (hashHeads.size() != 2) {
        return error("ReplayBlocks(): unknown inconsistent state");
    }

    uiInterface.ShowProgress(_("Replaying blocks..."), 0);
    LogPrintf("Replaying blocks\n");

    // Old tip during the interrupted flush.
    const CBlockIndex *pindexOld = nullptr;
    // New tip during the interrupted flush.
    const CBlockIndex *pindexNew;
    // Latest block common to both the old and the new tip.
    const CBlockIndex *pindexFork = nullptr;

    if (mapBlockIndex.count(hashHeads[0]) == 0) {
        return error(
            "ReplayBlocks(): reorganization to unknown block requested");
    }
    pindexNew = mapBlockIndex[hashHeads[0]];

    if (!hashHeads[1].IsNull()) {
        // The old tip is allowed to be 0, indicating it's the first flush.
        if (mapBlockIndex.count(hashHeads[1]) == 0) {
            return error(
                "ReplayBlocks(): reorganization from unknown block requested");
        }
        pindexOld = mapBlockIndex[hashHeads[1]];
        pindexFork = LastCommonAncestor(pindexOld, pindexNew);
        assert(pindexFork != nullptr);
    }

    // Rollback along the old branch.
    while (pindexOld != pindexFork) {
        if (pindexOld->nHeight > 0) {
            // Never disconnect the genesis block.
            CBlock block;
            if (!ReadBlockFromDisk(block, pindexOld, config)) {
                return error("RollbackBlock(): ReadBlockFromDisk() failed at "
                             "%d, hash=%s",
                             pindexOld->nHeight,
                             pindexOld->GetBlockHash().ToString());
            }
            LogPrintf("Rolling back %s (%i)\n",
                      pindexOld->GetBlockHash().ToString(), pindexOld->nHeight);
            // Use new private CancellationSource that can not be cancelled
            DisconnectResult res = DisconnectBlock(block, pindexOld, cache, task::CCancellationSource::Make()->GetToken());
            if (res == DISCONNECT_FAILED) {
                return error(
                    "RollbackBlock(): DisconnectBlock failed at %d, hash=%s",
                    pindexOld->nHeight, pindexOld->GetBlockHash().ToString());
            }
            // If DISCONNECT_UNCLEAN is returned, it means a non-existing UTXO
            // was deleted, or an existing UTXO was overwritten. It corresponds
            // to cases where the block-to-be-disconnect never had all its
            // operations applied to the UTXO set. However, as both writing a
            // UTXO and deleting a UTXO are idempotent operations, the result is
            // still a version of the UTXO set with the effects of that block
            // undone.
        }
        pindexOld = pindexOld->pprev;
    }

    // Roll forward from the forking point to the new tip.
    int nForkHeight = pindexFork ? pindexFork->nHeight : 0;
    for (int nHeight = nForkHeight + 1; nHeight <= pindexNew->nHeight;
         ++nHeight) {
        const CBlockIndex *pindex = pindexNew->GetAncestor(nHeight);
        LogPrintf("Rolling forward %s (%i)\n",
                  pindex->GetBlockHash().ToString(), nHeight);
        if (!RollforwardBlock(pindex, cache, config)) {
            return false;
        }
    }

    cache.SetBestBlock(pindexNew->GetBlockHash());
    cache.Flush();
    uiInterface.ShowProgress("", 100);
    return true;
}

bool RewindBlockIndex(const Config &config) {
    LOCK(cs_main);

    const CChainParams &params = config.GetChainParams();
    int nHeight = chainActive.Height() + 1;

    // nHeight is now the height of the first insufficiently-validated block, or
    // tipheight + 1
    CValidationState state;
    CBlockIndex *pindex = chainActive.Tip();
    CJournalChangeSetPtr changeSet { mempool.getJournalBuilder()->getNewChangeSet(JournalUpdateReason::REORG) };
    while (chainActive.Height() >= nHeight) {
        if (fPruneMode && !chainActive.Tip()->nStatus.hasData()) {
            // If pruning, don't try rewinding past the HAVE_DATA point; since
            // older blocks can't be served anyway, there's no need to walk
            // further, and trying to DisconnectTip() will fail (and require a
            // needless reindex/redownload of the blockchain).
            break;
        }
        if (!DisconnectTip(config, state, nullptr, changeSet)) {
            return error(
                "RewindBlockIndex: unable to disconnect block at height %i",
                pindex->nHeight);
        }
        // Occasionally flush state to disk.
        if (!FlushStateToDisk(params, state, FLUSH_STATE_PERIODIC)) {
            return false;
        }
    }

    // Reduce validity flag and have-data flags.
    // We do this after actual disconnecting, otherwise we'll end up writing the
    // lack of data to disk before writing the chainstate, resulting in a
    // failure to continue if interrupted.
    for (BlockMap::iterator it = mapBlockIndex.begin();
         it != mapBlockIndex.end(); it++) {
        CBlockIndex *pindexIter = it->second;

        if (pindexIter->IsValid(BlockValidity::TRANSACTIONS) &&
            pindexIter->nChainTx) {
            setBlockIndexCandidates.insert(pindexIter);
        }
    }

    PruneBlockIndexCandidates();

    CheckBlockIndex(params.GetConsensus());

    if (!FlushStateToDisk(params, state, FLUSH_STATE_ALWAYS)) {
        return false;
    }

    return true;
}

// May NOT be used after any connections are up as much of the peer-processing
// logic assumes a consistent block index state
void UnloadBlockIndex() {
    LOCK(cs_main);
    setBlockIndexCandidates.clear();
    chainActive.SetTip(nullptr);
    chainActiveSharedData.SetChainActiveHeight(0);
    chainActiveSharedData.SetChainActiveTipBlockHash(uint256());
    pindexBestInvalid = nullptr;
    pindexBestHeader = nullptr;
    mempool.Clear();
    mapBlocksUnlinked.clear();
    pBlockFileInfoStore->Clear();
    nBlockSequenceId = 1;
    setDirtyBlockIndex.clear();

    for (BlockMap::value_type &entry : mapBlockIndex) {
        delete entry.second;
    }
    mapBlockIndex.clear();
    fHavePruned = false;
}

bool LoadBlockIndex(const CChainParams &chainparams) {
    // Load block index from databases
    if (!fReindex && !LoadBlockIndexDB(chainparams)) {
        return false;
    }
    return true;
}

bool InitBlockIndex(const Config &config) {
    LOCK(cs_main);

    // Check whether we're already initialized
    if (chainActive.Genesis() != nullptr) {
        return true;
    }

    // Use the provided setting for -txindex in the new database
    fTxIndex = gArgs.GetBoolArg("-txindex", DEFAULT_TXINDEX);
    pblocktree->WriteFlag("txindex", fTxIndex);
    LogPrintf("Initializing databases...\n");

    // Only add the genesis block if not reindexing (in which case we reuse the
    // one already on disk)
    if (!fReindex) {
        try {
            const CChainParams &chainparams = config.GetChainParams();
            CBlock &block = const_cast<CBlock &>(chainparams.GenesisBlock());
            // Start new block file
            uint64_t nBlockSize = ::GetSerializeSize(block, SER_DISK, CLIENT_VERSION);
            uint64_t nBlockSizeWithHeader =
                nBlockSize
                + GetBlockFileBlockHeaderSize(nBlockSize);
            CDiskBlockPos blockPos;
            CValidationState state;
            if (!pBlockFileInfoStore->FindBlockPos(config, state, blockPos,
                               nBlockSizeWithHeader, 0, block.GetBlockTime(),
                               fCheckForPruning)) {
                return error("LoadBlockIndex(): FindBlockPos failed");
            }
            CDiskBlockMetaData metaData;
            if (!WriteBlockToDisk(block, blockPos, chainparams.DiskMagic(), metaData)) {
                return error(
                    "LoadBlockIndex(): writing genesis block to disk failed");
            }
            CBlockIndex *pindex = AddToBlockIndex(block);
            if (!ReceivedBlockTransactions(block, state, pindex, blockPos, metaData)) {
                return error("LoadBlockIndex(): genesis block not accepted");
            }
        } catch (const std::runtime_error &e) {
            return error(
                "LoadBlockIndex(): failed to initialize block database: %s",
                e.what());
        }
    }

    return true;
}

void ReindexAllBlockFiles(const Config &config, CBlockTreeDB *pblocktree, bool& fReindex)
{
    
    int nFile = 0;
    while (true) {
        CDiskBlockPos pos(nFile, 0);
        if (!fs::exists(GetBlockPosFilename(pos, "blk"))) {
            // No block files left to reindex
            break;
        }
        FILE *file = CDiskFiles::OpenBlockFile(pos, true);
        if (!file) {
            // This error is logged in OpenBlockFile
            break;
        }
        LogPrintf("Reindexing block file blk%05u.dat...\n",
            (unsigned int)nFile);
        LoadExternalBlockFile(config, file, &pos);
        nFile++;
    }

    pblocktree->WriteReindexing(false);
    fReindex = false;
    LogPrintf("Reindexing finished\n");
    // To avoid ending up in a situation without genesis block, re-try
    // initializing (no-op if reindexing worked):
    InitBlockIndex(config);
}

bool LoadExternalBlockFile(const Config &config, FILE *fileIn,
                           CDiskBlockPos *dbp) {
    // Map of disk positions for blocks with unknown parent (only used for
    // reindex)
    static std::multimap<uint256, CDiskBlockPos> mapBlocksUnknownParent;
    int64_t nStart = GetTimeMillis();

    const CChainParams &chainparams = config.GetChainParams();

    int nLoaded = 0;
    try {
        // This takes over fileIn and calls fclose() on it in the CBufferedFile destructor.
        CBufferedFile blkdat(fileIn, 2 * ONE_MEGABYTE, ONE_MEGABYTE + 8, SER_DISK,
                             CLIENT_VERSION);
        uint64_t nRewind = blkdat.GetPos();
        while (!blkdat.eof()) {
            boost::this_thread::interruption_point();

            blkdat.SetPos(nRewind);
            // Start one byte further next time, in case of failure.
            nRewind++;
            // Remove former limit.
            blkdat.SetLimit();
            uint64_t nSize = 0;
            uint32_t nSizeLegacy = 0;
            try {
                // Locate a header.
                uint8_t buf[CMessageHeader::MESSAGE_START_SIZE];
                blkdat.FindByte(chainparams.DiskMagic()[0]);
                nRewind = blkdat.GetPos() + 1;
                blkdat >> FLATDATA(buf);
                if (memcmp(buf, chainparams.DiskMagic().data(),
                           CMessageHeader::MESSAGE_START_SIZE)) {
                    continue;
                }
                // Read 32 bit size. If it is equal to 32 max than read also 64 bit size.
                blkdat >> nSizeLegacy;
                if (nSizeLegacy == std::numeric_limits<uint32_t>::max())
                {
                    blkdat >> nSize;
                }
                else
                {
                    nSize = nSizeLegacy;
                }

                if (nSize < 80) {
                    continue;
                }
            } catch (const std::exception &) {
                // No valid block header found; don't complain.
                break;
            }
            try {
                // read block
                uint64_t nBlockPos = blkdat.GetPos();
                if (dbp) {
                    dbp->nPos = nBlockPos;
                }
                blkdat.SetLimit(nBlockPos + nSize);
                blkdat.SetPos(nBlockPos);
                std::shared_ptr<CBlock> pblock = std::make_shared<CBlock>();
                CBlock &block = *pblock;
                blkdat >> block;
                nRewind = blkdat.GetPos();

                // detect out of order blocks, and store them for later
                uint256 hash = block.GetHash();
                if (hash != chainparams.GetConsensus().hashGenesisBlock &&
                    mapBlockIndex.find(block.hashPrevBlock) ==
                        mapBlockIndex.end()) {
                    LogPrint(BCLog::REINDEX,
                             "%s: Out of order block %s, parent %s not known\n",
                             __func__, hash.ToString(),
                             block.hashPrevBlock.ToString());
                    if (dbp) {
                        mapBlocksUnknownParent.insert(
                            std::make_pair(block.hashPrevBlock, *dbp));
                    }
                    continue;
                }

                // process in case the block isn't known yet
                if (mapBlockIndex.count(hash) == 0 ||
                    !mapBlockIndex[hash]->nStatus.hasData()) {
                    LOCK(cs_main);
                    CValidationState state;
                    if (AcceptBlock(config, pblock, state, nullptr, true, dbp,
                                    nullptr)) {
                        nLoaded++;
                    }
                    if (state.IsError()) {
                        break;
                    }
                } else if (hash !=
                               chainparams.GetConsensus().hashGenesisBlock &&
                           mapBlockIndex[hash]->nHeight % 1000 == 0) {
                    LogPrint(
                        BCLog::REINDEX,
                        "Block Import: already had block %s at height %d\n",
                        hash.ToString(), mapBlockIndex[hash]->nHeight);
                }

                // Activate the genesis block so normal node progress can
                // continue
                if (hash == chainparams.GetConsensus().hashGenesisBlock) {
                    // dummyState is used to report errors, not block related invalidity - ignore it
                    // (see description of ActivateBestChain)
                    CValidationState dummyState;
                    CJournalChangeSetPtr changeSet { mempool.getJournalBuilder()->getNewChangeSet(JournalUpdateReason::REORG) };
                    auto source = task::CCancellationSource::Make();
                    if (!ActivateBestChain(task::CCancellationToken::JoinToken(source->GetToken(), GetShutdownToken()), config, dummyState, changeSet)) {
                        break;
                    }
                }

                NotifyHeaderTip();

                // Recursively process earlier encountered successors of this
                // block
                std::deque<uint256> queue;
                queue.push_back(hash);
                while (!queue.empty()) {
                    uint256 head = queue.front();
                    queue.pop_front();
                    std::pair<std::multimap<uint256, CDiskBlockPos>::iterator,
                              std::multimap<uint256, CDiskBlockPos>::iterator>
                        range = mapBlocksUnknownParent.equal_range(head);
                    while (range.first != range.second) {
                        std::multimap<uint256, CDiskBlockPos>::iterator it =
                            range.first;
                        std::shared_ptr<CBlock> pblockrecursive =
                            std::make_shared<CBlock>();
                        if (ReadBlockFromDisk(*pblockrecursive, it->second,
                                              config)) {
                            LogPrint(
                                BCLog::REINDEX,
                                "%s: Processing out of order child %s of %s\n",
                                __func__, pblockrecursive->GetHash().ToString(),
                                head.ToString());
                            LOCK(cs_main);
                            CValidationState dummy;
                            if (AcceptBlock(config, pblockrecursive, dummy,
                                            nullptr, true, &it->second,
                                            nullptr)) {
                                nLoaded++;
                                queue.push_back(pblockrecursive->GetHash());
                            }
                        }
                        range.first++;
                        mapBlocksUnknownParent.erase(it);
                        NotifyHeaderTip();
                    }
                }
            } catch (const std::exception &e) {
                LogPrintf("%s: Deserialize or I/O error - %s\n", __func__,
                          e.what());
            }
        }
    } catch (const std::runtime_error &e) {
        AbortNode(std::string("System error: ") + e.what());
    }
    if (nLoaded > 0) {
        LogPrintf("Loaded %i blocks from external file in %dms\n", nLoaded,
                  GetTimeMillis() - nStart);
    }
    return nLoaded > 0;
}

static void CheckBlockIndex(const Consensus::Params &consensusParams) {
    if (!fCheckBlockIndex) {
        return;
    }

    LOCK(cs_main);

    // During a reindex, we read the genesis block and call CheckBlockIndex
    // before ActivateBestChain, so we have the genesis block in mapBlockIndex
    // but no active chain. (A few of the tests when iterating the block tree
    // require that chainActive has been initialized.)
    if (chainActive.Height() < 0) {
        assert(mapBlockIndex.size() <= 1);
        return;
    }

    // Build forward-pointing map of the entire block tree.
    std::multimap<CBlockIndex *, CBlockIndex *> forward;
    for (BlockMap::iterator it = mapBlockIndex.begin();
         it != mapBlockIndex.end(); it++) {
        forward.insert(std::make_pair(it->second->pprev, it->second));
    }

    assert(forward.size() == mapBlockIndex.size());

    std::pair<std::multimap<CBlockIndex *, CBlockIndex *>::iterator,
              std::multimap<CBlockIndex *, CBlockIndex *>::iterator>
        rangeGenesis = forward.equal_range(nullptr);
    CBlockIndex *pindex = rangeGenesis.first->second;
    rangeGenesis.first++;
    // There is only one index entry with parent nullptr.
    assert(rangeGenesis.first == rangeGenesis.second);

    // Iterate over the entire block tree, using depth-first search.
    // Along the way, remember whether there are blocks on the path from genesis
    // block being explored which are the first to have certain properties.
    size_t nNodes = 0;
    int nHeight = 0;
    // Oldest ancestor of pindex which is invalid.
    CBlockIndex *pindexFirstInvalid = nullptr;
    // Oldest ancestor of pindex which does not have data available.
    CBlockIndex *pindexFirstMissing = nullptr;
    // Oldest ancestor of pindex for which nTx == 0.
    CBlockIndex *pindexFirstNeverProcessed = nullptr;
    // Oldest ancestor of pindex which does not have BLOCK_VALID_TREE
    // (regardless of being valid or not).
    CBlockIndex *pindexFirstNotTreeValid = nullptr;
    // Oldest ancestor of pindex which does not have BLOCK_VALID_TRANSACTIONS
    // (regardless of being valid or not).
    CBlockIndex *pindexFirstNotTransactionsValid = nullptr;
    // Oldest ancestor of pindex which does not have BLOCK_VALID_CHAIN
    // (regardless of being valid or not).
    CBlockIndex *pindexFirstNotChainValid = nullptr;
    // Oldest ancestor of pindex which does not have BLOCK_VALID_SCRIPTS
    // (regardless of being valid or not).
    CBlockIndex *pindexFirstNotScriptsValid = nullptr;
    while (pindex != nullptr) {
        nNodes++;
        if (pindexFirstInvalid == nullptr && pindex->nStatus.hasFailed()) {
            pindexFirstInvalid = pindex;
        }
        if (pindexFirstMissing == nullptr && !pindex->nStatus.hasData()) {
            pindexFirstMissing = pindex;
        }
        if (pindexFirstNeverProcessed == nullptr && pindex->nTx == 0) {
            pindexFirstNeverProcessed = pindex;
        }
        if (pindex->pprev != nullptr && pindexFirstNotTreeValid == nullptr &&
            pindex->nStatus.getValidity() < BlockValidity::TREE) {
            pindexFirstNotTreeValid = pindex;
        }
        if (pindex->pprev != nullptr &&
            pindexFirstNotTransactionsValid == nullptr &&
            pindex->nStatus.getValidity() < BlockValidity::TRANSACTIONS) {
            pindexFirstNotTransactionsValid = pindex;
        }
        if (pindex->pprev != nullptr && pindexFirstNotChainValid == nullptr &&
            pindex->nStatus.getValidity() < BlockValidity::CHAIN) {
            pindexFirstNotChainValid = pindex;
        }
        if (pindex->pprev != nullptr && pindexFirstNotScriptsValid == nullptr &&
            pindex->nStatus.getValidity() < BlockValidity::SCRIPTS) {
            pindexFirstNotScriptsValid = pindex;
        }

        // Begin: actual consistency checks.
        if (pindex->pprev == nullptr) {
            // Genesis block checks.
            // Genesis block's hash must match.
            assert(pindex->GetBlockHash() == consensusParams.hashGenesisBlock);
            // The current active chain's genesis block must be this block.
            assert(pindex == chainActive.Genesis());
        }
        if (pindex->nChainTx == 0) {
            // nSequenceId can't be set positive for blocks that aren't linked
            // (negative is used for preciousblock)
            assert(pindex->nSequenceId <= 0);
        }
        // VALID_TRANSACTIONS is equivalent to nTx > 0 for all nodes (whether or
        // not pruning has occurred). HAVE_DATA is only equivalent to nTx > 0
        // (or VALID_TRANSACTIONS) if no pruning has occurred.
        if (!fHavePruned) {
            // If we've never pruned, then HAVE_DATA should be equivalent to nTx
            // > 0
            assert(!pindex->nStatus.hasData() == (pindex->nTx == 0));
            assert(pindexFirstMissing == pindexFirstNeverProcessed);
        } else if (pindex->nStatus.hasData()) {
            // If we have pruned, then we can only say that HAVE_DATA implies
            // nTx > 0
            assert(pindex->nTx > 0);
        }
        if (pindex->nStatus.hasUndo()) {
            assert(pindex->nStatus.hasData());
        }
        // This is pruning-independent.
        assert((pindex->nStatus.getValidity() >= BlockValidity::TRANSACTIONS) ==
               (pindex->nTx > 0));
        // All parents having had data (at some point) is equivalent to all
        // parents being VALID_TRANSACTIONS, which is equivalent to nChainTx
        // being set.
        // nChainTx != 0 is used to signal that all parent blocks have been
        // processed (but may have been pruned).
        assert((pindexFirstNeverProcessed != nullptr) ==
               (pindex->nChainTx == 0));
        assert((pindexFirstNotTransactionsValid != nullptr) ==
               (pindex->nChainTx == 0));
        // nHeight must be consistent.
        assert(pindex->nHeight == nHeight);
        // For every block except the genesis block, the chainwork must be
        // larger than the parent's.
        assert(pindex->pprev == nullptr ||
               pindex->nChainWork >= pindex->pprev->nChainWork);
        // The pskip pointer must point back for all but the first 2 blocks.
        assert(nHeight < 2 ||
               (pindex->pskip && (pindex->pskip->nHeight < nHeight)));
        // All mapBlockIndex entries must at least be TREE valid
        assert(pindexFirstNotTreeValid == nullptr);
        if (pindex->nStatus.getValidity() >= BlockValidity::TREE) {
            // TREE valid implies all parents are TREE valid
            assert(pindexFirstNotTreeValid == nullptr);
        }
        if (pindex->nStatus.getValidity() >= BlockValidity::CHAIN) {
            // CHAIN valid implies all parents are CHAIN valid
            assert(pindexFirstNotChainValid == nullptr);
        }
        if (pindex->nStatus.getValidity() >= BlockValidity::SCRIPTS) {
            // SCRIPTS valid implies all parents are SCRIPTS valid
            assert(pindexFirstNotScriptsValid == nullptr);
        }
        if (pindexFirstInvalid == nullptr) {
            // Checks for not-invalid blocks.
            // The failed mask cannot be set for blocks without invalid parents.
            assert(!pindex->nStatus.isInvalid());
        }
        // Check whether this block is in mapBlocksUnlinked.
        std::pair<std::multimap<CBlockIndex *, CBlockIndex *>::iterator,
                  std::multimap<CBlockIndex *, CBlockIndex *>::iterator>
            rangeUnlinked = mapBlocksUnlinked.equal_range(pindex->pprev);
        bool foundInUnlinked = false;
        while (rangeUnlinked.first != rangeUnlinked.second) {
            assert(rangeUnlinked.first->first == pindex->pprev);
            if (rangeUnlinked.first->second == pindex) {
                foundInUnlinked = true;
                break;
            }
            rangeUnlinked.first++;
        }
        if (pindex->pprev && pindex->nStatus.hasData() &&
            pindexFirstNeverProcessed != nullptr &&
            pindexFirstInvalid == nullptr) {
            // If this block has block data available, some parent was never
            // received, and has no invalid parents, it must be in
            // mapBlocksUnlinked.
            assert(foundInUnlinked);
        }
        if (!pindex->nStatus.hasData()) {
            // Can't be in mapBlocksUnlinked if we don't HAVE_DATA
            assert(!foundInUnlinked);
        }
        if (pindexFirstMissing == nullptr) {
            // We aren't missing data for any parent -- cannot be in
            // mapBlocksUnlinked.
            assert(!foundInUnlinked);
        }
        if (pindex->pprev && pindex->nStatus.hasData() &&
            pindexFirstNeverProcessed == nullptr &&
            pindexFirstMissing != nullptr) {
            // We HAVE_DATA for this block, have received data for all parents
            // at some point, but we're currently missing data for some parent.
            // We must have pruned.
            assert(fHavePruned);
            // This block may have entered mapBlocksUnlinked if:
            //  - it has a descendant that at some point had more work than the
            //    tip, and
            //  - we tried switching to that descendant but were missing
            //    data for some intermediate block between chainActive and the
            //    tip.
            // So if this block is itself better than chainActive.Tip() and it
            // wasn't in
            // setBlockIndexCandidates, then it must be in mapBlocksUnlinked.
            if (!CBlockIndexWorkComparator()(pindex, chainActive.Tip()) &&
                setBlockIndexCandidates.count(pindex) == 0) {
                if (pindexFirstInvalid == nullptr) {
                    assert(foundInUnlinked);
                }
            }
        }
        // assert(pindex->GetBlockHash() == pindex->GetBlockHeader().GetHash());
        // // Perhaps too slow
        // End: actual consistency checks.

        // Try descending into the first subnode.
        std::pair<std::multimap<CBlockIndex *, CBlockIndex *>::iterator,
                  std::multimap<CBlockIndex *, CBlockIndex *>::iterator>
            range = forward.equal_range(pindex);
        if (range.first != range.second) {
            // A subnode was found.
            pindex = range.first->second;
            nHeight++;
            continue;
        }
        // This is a leaf node. Move upwards until we reach a node of which we
        // have not yet visited the last child.
        while (pindex) {
            // We are going to either move to a parent or a sibling of pindex.
            // If pindex was the first with a certain property, unset the
            // corresponding variable.
            if (pindex == pindexFirstInvalid) {
                pindexFirstInvalid = nullptr;
            }
            if (pindex == pindexFirstMissing) {
                pindexFirstMissing = nullptr;
            }
            if (pindex == pindexFirstNeverProcessed) {
                pindexFirstNeverProcessed = nullptr;
            }
            if (pindex == pindexFirstNotTreeValid) {
                pindexFirstNotTreeValid = nullptr;
            }
            if (pindex == pindexFirstNotTransactionsValid) {
                pindexFirstNotTransactionsValid = nullptr;
            }
            if (pindex == pindexFirstNotChainValid) {
                pindexFirstNotChainValid = nullptr;
            }
            if (pindex == pindexFirstNotScriptsValid) {
                pindexFirstNotScriptsValid = nullptr;
            }
            // Find our parent.
            CBlockIndex *pindexPar = pindex->pprev;
            // Find which child we just visited.
            std::pair<std::multimap<CBlockIndex *, CBlockIndex *>::iterator,
                      std::multimap<CBlockIndex *, CBlockIndex *>::iterator>
                rangePar = forward.equal_range(pindexPar);
            while (rangePar.first->second != pindex) {
                // Our parent must have at least the node we're coming from as
                // child.
                assert(rangePar.first != rangePar.second);
                rangePar.first++;
            }
            // Proceed to the next one.
            rangePar.first++;
            if (rangePar.first != rangePar.second) {
                // Move to the sibling.
                pindex = rangePar.first->second;
                break;
            } else {
                // Move up further.
                pindex = pindexPar;
                nHeight--;
                continue;
            }
        }
    }

    // Check that we actually traversed the entire map.
    assert(nNodes == forward.size());
}

std::string CBlockFileInfo::ToString() const {
    return strprintf(
        "CBlockFileInfo(blocks=%u, size=%u, heights=%u...%u, time=%s...%s)",
        nBlocks, nSize, nHeightFirst, nHeightLast,
        DateTimeStrFormat("%Y-%m-%d", nTimeFirst),
        DateTimeStrFormat("%Y-%m-%d", nTimeLast));
}


CBlockFileInfo *GetBlockFileInfo(size_t n) {
    return pBlockFileInfoStore->GetBlockFileInfo(n);
}

static const uint64_t MEMPOOL_DUMP_VERSION = 1;

bool LoadMempool(const Config &config, const task::CCancellationToken& shutdownToken)
{
    try {
        int64_t nExpiryTimeout = gArgs.GetArg("-mempoolexpiry", DEFAULT_MEMPOOL_EXPIRY) * 60 * 60;
        FILE *filestr = fsbridge::fopen(GetDataDir() / "mempool.dat", "rb");
        CAutoFile file(filestr, SER_DISK, CLIENT_VERSION);
        if (file.IsNull()) {
            throw std::runtime_error("Failed to open mempool file from disk");
        }

        int64_t count = 0;
        int64_t skipped = 0;
        int64_t failed = 0;
        int64_t nNow = GetTime();

        uint64_t version;
        file >> version;
        if (version != MEMPOOL_DUMP_VERSION) {
            throw std::runtime_error("Bad mempool dump version");
        }
        uint64_t num;
        file >> num;
        double prioritydummy = 0;
        // Take a reference to the validator.
        const auto& txValidator = g_connman->getTxnValidator();
        while (num--) {
            CTransactionRef tx;
            int64_t nTime;
            int64_t nFeeDelta;
            file >> tx;
            file >> nTime;
            file >> nFeeDelta;
            Amount amountdelta(nFeeDelta);
            if (amountdelta != Amount(0)) {
                mempool.PrioritiseTransaction(tx->GetId(),
                                              tx->GetId().ToString(),
                                              prioritydummy, amountdelta);
            }
            if (nTime + nExpiryTimeout > nNow) {
                // Mempool Journal ChangeSet
                CJournalChangeSetPtr changeSet {
                    mempool.getJournalBuilder()->getNewChangeSet(JournalUpdateReason::INIT)
                };
                const CValidationState& state {
                    // Execute txn validation synchronously.
                    txValidator->processValidation(
                                        std::make_shared<CTxInputData>(
                                                            TxSource::file, // tx source
                                                            TxValidationPriority::normal,  // tx validation priority
                                                            tx,    // a pointer to the tx
                                                            nTime, // nAcceptTime
                                                            true),  // fLimitFree
                                        changeSet, // an instance of the mempool journal
                                        true) // fLimitMempoolSize
                };
                // Check results
                if (state.IsValid()) {
                    ++count;
                } else {
                    ++failed;
                }
            } else {
                ++skipped;
            }
            if (shutdownToken.IsCanceled()) {
                return false;
            }
        }
        std::map<uint256, Amount> mapDeltas;
        file >> mapDeltas;

        for (const auto &i : mapDeltas) {
            mempool.PrioritiseTransaction(i.first, i.first.ToString(),
                                          prioritydummy, i.second);
        }

        LogPrintf("Imported mempool transactions from disk: %i successes, %i "
                  "failed, %i expired\n",
                  count, failed, skipped);

    }
    catch (const std::exception &e) {
        LogPrintf("Failed to deserialize mempool data on disk: %s. Continuing anyway.\n",
                  e.what());
    }

    // Restore non-final transactions
    return mempool.getNonFinalPool().loadMempool(shutdownToken);
}

void DumpMempool(void) {
    int64_t start = GetTimeMicros();

    std::map<uint256, Amount> mapDeltas;
    std::vector<TxMempoolInfo> vinfo;

    {
        std::shared_lock lock(mempool.smtx);
        for (const auto &i : mempool.mapDeltas) {
            mapDeltas[i.first] = i.second.second;
        }
        vinfo = mempool.InfoAllNL();
    }

    int64_t mid = GetTimeMicros();

    try {
        FILE *filestr = fsbridge::fopen(GetDataDir() / "mempool.dat.new", "wb");
        if (!filestr) {
            return;
        }

        CAutoFile file(filestr, SER_DISK, CLIENT_VERSION);

        uint64_t version = MEMPOOL_DUMP_VERSION;
        file << version;

        file << (uint64_t)vinfo.size();
        for (const auto &i : vinfo) {
            file << *(i.tx);
            file << (int64_t)i.nTime;
            file << (int64_t)i.nFeeDelta.GetSatoshis();
            mapDeltas.erase(i.tx->GetId());
        }

        file << mapDeltas;
        FileCommit(file.Get());
        file.fclose();
        RenameOver(GetDataDir() / "mempool.dat.new",
                   GetDataDir() / "mempool.dat");
        int64_t last = GetTimeMicros();
        LogPrintf("Dumped mempool: %gs to copy, %gs to dump\n",
                  (mid - start) * 0.000001, (last - mid) * 0.000001);
    } catch (const std::exception &e) {
        LogPrintf("Failed to dump mempool: %s. Continuing anyway.\n", e.what());
    }

    // Dump non-final pool
    mempool.getNonFinalPool().dumpMempool();
}

//! Guess how far we are in the verification process at the given block index
double GuessVerificationProgress(const ChainTxData &data, CBlockIndex *pindex) {
    if (pindex == nullptr) {
        return 0.0;
    }

    int64_t nNow = time(nullptr);

    double fTxTotal;
    if (pindex->nChainTx <= data.nTxCount) {
        fTxTotal = data.nTxCount + (nNow - data.nTime) * data.dTxRate;
    } else {
        fTxTotal =
            pindex->nChainTx + (nNow - pindex->GetBlockTime()) * data.dTxRate;
    }

    return pindex->nChainTx / fTxTotal;
}

class CMainCleanup {
public:
    CMainCleanup() {}
    ~CMainCleanup() {
        // block headers
        for (const std::pair<const uint256, CBlockIndex *> &it :
             mapBlockIndex) {
            delete it.second;
        }
        mapBlockIndex.clear();
    }
} instance_of_cmaincleanup;
