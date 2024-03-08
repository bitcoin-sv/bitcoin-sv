// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2017-2018 The Bitcoin developers
// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "validation.h"
#include "abort_node.h"
#include "arith_uint256.h"
#include "async_file_reader.h"
#include "block_file_access.h"
#include "block_index_store.h"
#include "block_index_store_loader.h"
#include "blockfileinfostore.h"
#include "blockindex_with_descendants.h"
#include "blockstreams.h"
#include "chainparams.h"
#include "checkpoints.h"
#include "checkqueuepool.h"
#include "config.h"
#include "consensus/consensus.h"
#include "consensus/merkle.h"
#include "consensus/validation.h"
#include "disk_tx_pos.h"
#include "frozentxo.h"
#include "frozentxo_db.h"
#include "frozentxo_logging.h"
#include "fs.h"
#include "hash.h"
#include "init.h"
#include "invalid_txn_publisher.h"
#include "metrics.h"
#include "miner_id/miner_id_db.h"
#include "miner_id/miner_info_tracker.h"
#include "mining/journal_builder.h"
#include "net/net.h"
#include "net/net_processing.h"
#include "netmessagemaker.h"
#include "policy/policy.h"
#include "pow.h"
#include "primitives/block.h"
#include "primitives/transaction.h"
#include "processing_block_index.h"
#include "pubkey.h"
#include "script/scriptcache.h"
#include "script/sigcache.h"
#include "script/standard.h"
#include "taskcancellation.h"
#include "timedata.h"
#include "tinyformat.h"
#include "txdb.h"
#include "txn_grouper.h"
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
#include "block_file_access.h"
#include "invalid_txn_publisher.h"
#include "blockindex_with_descendants.h"
#include "metrics.h"
#include "safe_mode.h"

#include <atomic>

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

BlockIndexStore mapBlockIndex;
CChain chainActive;
CWaitableCriticalSection csBestBlock;
CConditionVariable cvBlockChange;
std::atomic_bool fImporting(false);
std::atomic_bool fReindex{ false };
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

const CBlockIndex *pindexBestInvalid;

/**
 * The set of all CBlockIndex entries with BLOCK_VALID_TRANSACTIONS (for itself
 * and all ancestors) and as good as our current tip or better. Entries may be
 * failed, though, and pruning nodes may be missing the data for the block.
 */
std::set<CBlockIndex*, CBlockIndexWorkComparator> setBlockIndexCandidates;
/**
 * All pairs A->B, where A (or one of its ancestors) misses transactions, but B
 * has transactions. Pruned nodes may have entries where B is missing data.
 */
std::multimap<const CBlockIndex *, CBlockIndex *> mapBlocksUnlinked;





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

} // namespace

const CBlockIndex *FindForkInGlobalIndex(const CChain &chain,
                                   const CBlockLocator &locator) {
    // Find the first block the caller has in the main chain
    for (const uint256 &hash : locator.vHave) {
        if (auto pindex = mapBlockIndex.Get(hash); pindex)
        {
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

std::unique_ptr<CoinsDB> pcoinsTip;
CBlockTreeDB *pblocktree = nullptr;

/**
 * Test whether the given transaction is final for the given height and time.
 */
bool IsFinalTx(const CTransaction &tx, int32_t nBlockHeight, int64_t nBlockTime)
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
static std::pair<int32_t, int64_t>
CalculateSequenceLocks(const CTransaction &tx, int flags,
                       std::vector<int32_t> *prevHeights,
                       const CBlockIndex &block) {
    assert(prevHeights->size() == tx.vin.size());

    // Will be set to the equivalent height- and time-based nLockTime
    // values that would be necessary to satisfy all relative lock-
    // time constraints given our view of block chain history.
    // The semantics of nLockTime are the last invalid height/time, so
    // use -1 to have the effect of any height or time being valid.
    int32_t nMinHeight = -1;
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

        int32_t nCoinHeight = (*prevHeights)[txinIndex];

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
                                  std::pair<int32_t, int64_t> lockPair) {
    assert(!block.IsGenesis());
    int64_t nBlockTime = block.GetPrev()->GetMedianTimePast();
    if (lockPair.first >= block.GetHeight() || lockPair.second >= nBlockTime) {
        return false;
    }

    return true;
}

bool SequenceLocks(const CTransaction &tx, int flags,
                   std::vector<int32_t> *prevHeights, const CBlockIndex &block) {
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
    const CBlockIndex& tip,
    const CTransaction &tx,
    const Config& config,
    int flags,
    LockPoints *lp,
    CCoinsViewCache* viewMemPool)
{
    // Post-genesis we don't care about the old sequence lock calculations
    if(IsGenesisEnabled(config, tip.GetHeight()))
    {
        return true;
    }

    // CheckSequenceLocks() uses chainActive.Height()+1 to evaluate height based
    // locks because when SequenceLocks() is called within ConnectBlock(), the
    // height of the block *being* evaluated is what is used. Thus if we want to
    // know if a transaction can be part of the *next* block, we need to use one
    // more than chainActive.Height()

    CBlockIndex::TemporaryBlockIndex index{ const_cast<CBlockIndex&>(tip), {} };
    std::pair<int32_t, int64_t> lockPair;
    if (bool useExistingLockPoints = (viewMemPool == nullptr); useExistingLockPoints) {
        assert(lp);
        lockPair.first = lp->height;
        lockPair.second = lp->time;
    } else {
        std::vector<int32_t> prevheights;
        prevheights.resize(tx.vin.size());
        for (size_t txinIndex = 0; txinIndex < tx.vin.size(); txinIndex++) {
            const CTxIn &txin = tx.vin[txinIndex];
            if (auto coin = viewMemPool->GetCoin(txin.prevout); !coin.has_value() || coin->IsSpent())
            {
                return error("%s: Missing input", __func__);
            }
            else if (coin->GetHeight() == MEMPOOL_HEIGHT)
            {
                // Assume all mempool transaction confirm in the next block
                prevheights[txinIndex] = tip.GetHeight() + 1;
            } else {
                prevheights[txinIndex] = coin->GetHeight();
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
            int32_t maxInputHeight = 0;
            for (int32_t height : prevheights) {
                // Can ignore mempool inputs since we'll fail if they had
                // non-zero locks
                if (height != tip.GetHeight() + 1) {
                    maxInputHeight = std::max(maxInputHeight, height);
                }
            }
            lp->maxInputBlock = tip.GetAncestor(maxInputHeight);
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

uint64_t GetP2SHSigOpCount(const Config& config,
                           const CTransaction& tx,
                           const ICoinsViewCache& inputs,
                           bool& sigOpCountError)
{
    sigOpCountError = false;
    if (tx.IsCoinBase()) {
        return 0;
    }

    uint64_t nSigOps = 0;
    for (auto &i : tx.vin) {
        auto coin = inputs.GetCoinWithScript(i.prevout);
        assert(coin.has_value() && !coin->IsSpent());

        bool genesisEnabled = true;
        if (coin->GetHeight() != MEMPOOL_HEIGHT){
            genesisEnabled = IsGenesisEnabled(config, coin->GetHeight());
        }
        if (genesisEnabled) {
            continue;
        }
        const CTxOut &prevout = coin->GetTxOut();
        if (IsP2SH(prevout.scriptPubKey)) {
            nSigOps += prevout.scriptPubKey.GetSigOpCount(i.scriptSig, genesisEnabled, sigOpCountError);
            if (sigOpCountError) {
                return nSigOps;
            }
        }
    }
    return nSigOps;
}

uint64_t GetTransactionSigOpCount(const Config& config, 
                                  const CTransaction& tx,
                                  const ICoinsViewCache& inputs,
                                  bool checkP2SH,
                                  bool isGenesisEnabled, 
                                  bool& sigOpCountError)
{
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

bool CheckCoinbase(const CTransaction& tx, CValidationState& state, uint64_t maxTxSigOpsCountConsensusBeforeGenesis, uint64_t maxTxSizeConsensus, bool isGenesisEnabled, int32_t height)
{
    if (!tx.IsCoinBase()) {
        return state.DoS(100, false, REJECT_INVALID, "bad-cb-missing",
                         "first tx is not coinbase");
    }

    if (!CheckTransactionCommon(tx, state, maxTxSigOpsCountConsensusBeforeGenesis, maxTxSizeConsensus, isGenesisEnabled)) {
        // CheckTransactionCommon fill in the state.
        return false;
    }

    if (tx.vin[0].scriptSig.size() < 2 || tx.vin[0].scriptSig.size() > MAX_COINBASE_SCRIPTSIG_SIZE) {
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
                return IsP2SH(o.scriptPubKey); 
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

static bool IsUAHFenabled(const Config &config, int32_t nHeight) {
    return nHeight >= config.GetChainParams().GetConsensus().uahfHeight;
}

bool IsDAAEnabled(const Config &config, int32_t nHeight) {
    return nHeight >= config.GetChainParams().GetConsensus().daaHeight;
}

bool IsGenesisEnabled(const Config &config, int32_t nHeight) {
    if (nHeight == MEMPOOL_HEIGHT) {
        throw std::runtime_error("A coin with height == MEMPOOL_HEIGHT was passed "
            "to IsGenesisEnabled() overload that does not handle this case. "
            "Use the overload that takes Coin as parameter");
    }

    return nHeight >= config.GetGenesisActivationHeight();
}

bool IsGenesisEnabled(const Config& config, const CoinWithScript& coin, int32_t mempoolHeight) {
    auto height = coin.GetHeight();
    if (height == MEMPOOL_HEIGHT) {
        return mempoolHeight >= config.GetGenesisActivationHeight();
    }
    return height >= config.GetGenesisActivationHeight();
}

bool IsGenesisEnabled(const Config &config, const CBlockIndex* pindexPrev) {
    if (pindexPrev == nullptr) {
        return false;
    }

    // Genesis is enabled on the currently processed block, not on the current tip.
    return IsGenesisEnabled(config, pindexPrev->GetHeight() + 1);
}

// Used to avoid mempool polluting consensus critical paths if CCoinsViewMempool
// were somehow broken and returning the wrong scriptPubKeys.
//
// The function is only called by TxnValidation.
// TxnValidation is called by the Validator which holds cs_main lock during a call.
// view is constructed as local variable (by TxnValidation), populated and then disconnected from backing view,
// so that it can not be shared by other threads.
// Mt support is present in CoinsDB class.
static std::optional<bool> CheckInputsFromMempoolAndCache(
    const task::CCancellationToken& token,
    const Config& config,
    const CTransaction& tx,
    CValidationState& state,
    CCoinsViewMemPool& underlyingMempool,
    const CCoinsViewCache& view,
    const uint32_t flags,
    bool cacheSigStore,
    PrecomputedTransactionData& txdata) {

    assert(!tx.IsCoinBase());
    for (const CTxIn &txin : tx.vin) {
        // At this point we haven't actually checked if the coins are all
        // available (or shouldn't assume we have, since CheckInputs does). So
        // we just return failure if the inputs are not available here, and then
        // only have to check equivalence for available inputs.
        auto coin = view.GetCoin(txin.prevout);
        if (!coin.has_value() || coin->IsSpent()) {
            return false;
        }
        auto txFrom = underlyingMempool.GetCachedTransactionRef(txin.prevout);
        if (txFrom) {
            assert(txFrom->GetHash() == txin.prevout.GetTxId());
            assert(txFrom->vout.size() > txin.prevout.GetN());
            assert(txFrom->vout[txin.prevout.GetN()].nValue == coin->GetAmount());
            // Most scripts are of the same size but we don't want to pay for
            // script loading just to assert
            assert(txFrom->vout[txin.prevout.GetN()].scriptPubKey.size() == coin->GetScriptSize());
        } else {
            auto coinFromDisk = underlyingMempool.GetCoinFromDB(txin.prevout);
            assert(coinFromDisk.has_value() && !coinFromDisk->IsSpent());
            assert(coinFromDisk->GetAmount() == coin->GetAmount());
            // Most scripts are of the same size but we don't want to pay for
            // script loading just to assert
            assert(coinFromDisk->GetScriptSize() == coin->GetScriptSize());
        }
    }

    CFrozenTXOCheck frozenTXOCheck{
        chainActive.Tip()->GetHeight() + 1,
        "mempool and cache",
        chainActive.Tip()->GetBlockHash(),
        0}; // Data not known

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
                txdata,
                frozenTXOCheck);
}

static bool CheckTxOutputs(
    const CTransaction &tx,
    const CoinsDB& coinsTip,
    const CCoinsViewCache& view,
    std::vector<COutPoint> &vCoinsToUncache) {
    const TxId txid = tx.GetId();
    // Do we already have it?
    for (size_t out = 0; out < tx.vout.size(); out++) {
        COutPoint outpoint(txid, out);
        bool had_coin_in_cache = coinsTip.HaveCoinInCache(outpoint);
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

static bool IsAbsurdlyHighFeeSetForTxn(
    const Amount& nFees,
    const Amount& nAbsurdFee) {
    // Check a condition for txn's absurdly hight fee
    return !(nAbsurdFee != Amount(0) && nFees > nAbsurdFee);
}

static bool CheckTxSpendsCoinbaseOrConfiscation(
    const CTransaction &tx,
    const CCoinsViewCache& view) {
    // Keep track of transactions that spend a coinbase, which we re-scan
    // during reorgs to ensure COINBASE_MATURITY is still met.
    for (const CTxIn &txin : tx.vin) {
        if (auto coin = view.GetCoin(txin.prevout);
            coin.has_value() && (coin->IsCoinBase() || coin->IsConfiscation())) {
            return true;
        }
    }
    return false;
}

static Amount GetMempoolRejectFee(
        const Config& config,
        const CTxMemPool &pool,
        unsigned int nTxSize) {
    // Get mempool reject fee
    return pool.GetMinFee(config.GetMaxMempool())
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

static bool CheckAncestorLimits(const CTxMemPool& pool,
                                const CTxMemPoolEntry& entry,
                                std::string& errString,
                                const Config& config) {
    const auto limitAncestors = config.GetLimitAncestorCount();
    const auto limitSecondaryMempoolAncestors = config.GetLimitSecondaryMempoolAncestorCount();
    return pool.CheckAncestorLimits(entry,
                                    limitAncestors,
                                    limitSecondaryMempoolAncestors,
                                    std::ref(errString));
}

uint32_t GetScriptVerifyFlags(const Config &config, bool genesisEnabled) {
    // Check inputs based on the set of flags we activate.
    uint32_t scriptVerifyFlags = StandardScriptVerifyFlags(genesisEnabled, false);
    if (!config.GetChainParams().RequireStandard()) {
        if (config.IsSetPromiscuousMempoolFlags())
        {
            scriptVerifyFlags = config.GetPromiscuousMempoolFlags();
        }
        scriptVerifyFlags = SCRIPT_ENABLE_SIGHASH_FORKID | scriptVerifyFlags;
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

MempoolSizeLimits MempoolSizeLimits::FromConfig() {
    const auto limitMemory = GlobalConfig::GetConfig().GetMaxMempool();
    const auto limitDisk = GlobalConfig::GetConfig().GetMaxMempoolSizeDisk();
    const auto limitSecondaryRatio = GlobalConfig::GetConfig().GetMempoolMaxPercentCPFP() / 100.0;
    const auto limitExpiry = GlobalConfig::GetConfig().GetMemPoolExpiry();
    return MempoolSizeLimits(
        limitMemory,
        limitDisk,
        limitSecondaryRatio * limitMemory,
        limitExpiry);
}

std::vector<TxId> LimitMempoolSize(
    CTxMemPool &pool,
    const CJournalChangeSetPtr& changeSet,
    const MempoolSizeLimits& limits) {

    int expired = pool.Expire(GetTime() - limits.Age(), changeSet);
    if (expired != 0) {
        LogPrint(BCLog::MEMPOOL,
                 "Expired %i transactions from the memory pool\n", expired);
    }
    size_t usageTotal = pool.DynamicMemoryUsage();
    size_t usageSecondary = pool.SecondaryMempoolUsage();

    std::vector<COutPoint> vNoSpendsRemaining;
    std::vector<TxId> vRemovedTxIds;

    if ((usageTotal > limits.Total()) || (usageSecondary > limits.Secondary()))
    {
        size_t targetSize = std::min(usageTotal, limits.Total());
        if (usageSecondary > limits.Secondary())
        {
            size_t secondaryExcess = usageSecondary - limits.Secondary();
            targetSize -= secondaryExcess;
        }
        vRemovedTxIds =
            pool.TrimToSize(targetSize, changeSet, &vNoSpendsRemaining);
        usageTotal = pool.DynamicMemoryUsage();
        for (const auto& txid: vRemovedTxIds) {
            LogPrint(BCLog::MEMPOOL, "Limit mempool size: txn= %s removed from the memory pool\n",
                txid.ToString());
        }
    }

    // Disk usage is eventually consistent with total usage.
    size_t usageDisk = pool.GetDiskUsage();
    // Clamp the difference to zero to avoid nasty surprises.
    size_t usageMemory = std::max(usageTotal, usageDisk) - usageDisk;

    // Since this is called often we'll track the limit pretty close
    if (usageMemory > limits.Memory()) {
        size_t toWriteOut = usageMemory - limits.Memory();
        pool.SaveTxsToDisk(toWriteOut);
    }
    pcoinsTip->Uncache(vNoSpendsRemaining);
    return vRemovedTxIds;
}

void CommitTxToMempool(
    const TxInputDataSPtr& pTxInputData,
    const CTxMemPoolEntry& pMempoolEntry,
    TxStorage txStorage,
    CTxMemPool& pool,
    CValidationState& state,
    const CJournalChangeSetPtr& changeSet,
    bool fLimitMempoolSize,
    size_t* pnPrimaryMempoolSize,
    size_t* pnSecondaryMempoolSize,
    size_t* pnDynamicMemoryUsage) {

    const CTransactionRef& ptx = pTxInputData->GetTxnPtr();
    const CTransaction &tx = *ptx;
    const TxId txid = tx.GetId();

    // Post-genesis, non-final txns have their own mempool
    if(state.IsNonFinal() || pool.getNonFinalPool().finalisesExistingTransaction(ptx))
    {
        if (txStorage != TxStorage::memory)
        {
            // Remove the transaction from disk because the non-final memppool
            // does not use the txdb.
            pool.RemoveTxFromDisk(ptx);
        }

        // Post-genesis, non-final txns have their own mempool
        TxMempoolInfo info { pMempoolEntry };
        pool.getNonFinalPool().addOrUpdateTransaction(info, pTxInputData, state);
        return;
    }

    // Store transaction in the mempool.
    pool.AddUnchecked(
            txid,
            pMempoolEntry,
            txStorage,
            changeSet,
            pnPrimaryMempoolSize,
            pnSecondaryMempoolSize,
            pnDynamicMemoryUsage);
    // Check if the mempool size needs to be limited.
    if (fLimitMempoolSize) {
        // Trim mempool and check if tx was trimmed.
        LimitMempoolSize(
            pool,
            changeSet,
            MempoolSizeLimits::FromConfig());
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

static bool IsGenesisGracefulPeriod(const Config& config, int32_t spendHeight)
{
    if (((config.GetGenesisActivationHeight() - static_cast<int32_t>(config.GetGenesisGracefulPeriod())) < spendHeight) &&
        ((config.GetGenesisActivationHeight() + static_cast<int32_t>(config.GetGenesisGracefulPeriod())) > spendHeight))
    {
        return true;
    }
    return false;
}

static CBlockSource TxInputDataToSource(const CTxInputData& data)
{
    switch(data.GetTxSource())
    {
    case TxSource::file:
        return CBlockSource::MakeLocal("file");
    case TxSource::reorg:
        return CBlockSource::MakeLocal("reorg");
    case TxSource::wallet:
        return CBlockSource::MakeLocal("wallet");
    case TxSource::rpc:
        return CBlockSource::MakeRPC();
    case TxSource::p2p:
        if(const CNodePtr& pNode = data.GetNodePtr().lock())
        {
            return CBlockSource::MakeP2P(pNode->GetAssociation().GetPeerAddr().ToString());
        }

        // for unit tests only - test_txvalidator.cpp
        return CBlockSource::MakeP2P("disconnected");
    default:
        return CBlockSource::MakeUnknown();
    }
}

static std::shared_ptr<task::CCancellationSource> MakeValidationCancellationSource(
    bool fUseLimits,
    const Config& config,
    const TxValidationPriority txPriority,
    task::CTimedCancellationBudget& cancellationBudget)
{
    if (!fUseLimits) {
        return task::CCancellationSource::Make();
    }
    auto duration = (TxValidationPriority::high == txPriority || TxValidationPriority::normal == txPriority)
                   ? config.GetMaxStdTxnValidationDuration()
                   : config.GetMaxNonStdTxnValidationDuration();
    if (config.GetValidationClockCPU()) {
        return task::CThreadTimedCancellationSource::Make( duration, cancellationBudget);
    } else {
        return task::CTimedCancellationSource::Make( duration, cancellationBudget);
    }
}

CTxnValResult TxnValidation(
    const TxInputDataSPtr& pTxInputData,
    const Config& config,
    CTxMemPool& pool,
    TxnDoubleSpendDetectorSPtr dsDetector,
    bool fUseLimits,
    task::CTimedCancellationBudget& cancellationBudget)
{
    using Result = CTxnValResult;

    const CTransactionRef& ptx = pTxInputData->GetTxnPtr();
    const CTransaction &tx = *ptx;
    const TxId txid = tx.GetId();
    const int64_t nAcceptTime = pTxInputData->GetAcceptTime();
    const Amount nAbsurdFee = pTxInputData->GetAbsurdFee();

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
    auto source = MakeValidationCancellationSource(fUseLimits, config, pTxInputData->GetTxValidationPriority(), cancellationBudget);

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
        int32_t height { tip->GetHeight() };
        lockTimeFlags = StandardNonFinalVerifyFlags(IsGenesisEnabled(config, height));
        ContextualCheckTransactionForCurrentBlock(config, tx, height, tip->GetMedianTimePast(),
            ctxState, lockTimeFlags);
        if(ctxState.IsNonFinal() || ctxState.IsInvalid()) {
            if(ctxState.IsInvalid()) {
                // We copy the state from a dummy to ensure we don't increase the
                // ban score of peer for transaction that could be valid in the future.
                state.DoS(0, false, REJECT_NONSTANDARD,
                          ctxState.GetRejectReason(),
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

            // Bail out early if replacement of non-final txns exceeds rate limit
            if(!mempool.getNonFinalPool().checkUpdateWithinRate(ptx, state)) {
                // state set in call to checkUpdateWithinRate
                return Result{state, pTxInputData};
            }

            // Currently we don't allow chains of non-final txns
            if(DoesNonFinalSpendNonFinal(tx)) {
                state.DoS(0, false, REJECT_NONSTANDARD, "too-long-non-final-chain",
                    "Attempt to spend non-final transaction");
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
    if (auto conflictsWith = pool.CheckTxConflicts(ptx, isFinal); !conflictsWith.empty()) {
        state.SetMempoolConflictDetected( std::move(conflictsWith) );
        // Disable replacement feature for good
        state.Invalid(false, REJECT_CONFLICT, "txn-mempool-conflict");
        return Result{state, pTxInputData};
    }

    Amount nValueIn(0);
    LockPoints lp;
    // Combine db & mempool views together.
    CoinsDBView tipView{ *pcoinsTip };
    CCoinsViewMemPool viewMemPool(tipView, pool);
    CCoinsViewCache view(viewMemPool);
    // Prepare coins to uncache list for inputs
    for (const CTxIn& txin : tx.vin)
    {
        // Check if txin.prevout available as a UTXO tx.
        if (!pcoinsTip->HaveCoinInCache(txin.prevout))
        {
            vCoinsToUncache.push_back(txin.prevout);
        }
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
        // Do we already have it?
        if(!CheckTxOutputs(tx, *pcoinsTip, view, vCoinsToUncache)) {
           state.Invalid(false, REJECT_ALREADY_KNOWN,
                        "txn-already-known");
        } else {
            state.SetMissingInputs();
            state.Invalid();
        }
        return Result{state, pTxInputData, vCoinsToUncache};
    }
    // Bring the best block into scope.
    view.GetBestBlock();
    // Calculate txn's value-in
    nValueIn = view.GetValueIn(tx);
    // Only accept BIP68 sequence locked transactions that can be mined
    // in the next block; we don't want our mempool filled up with
    // transactions that can't be mined yet. Must keep pool.cs for this
    // unless we change CheckSequenceLocks to take a CoinsViewCache
    // instead of create its own.
    if (!CheckSequenceLocks(*chainActive.Tip(), tx, config, lockTimeFlags, &lp, &view)) {
        state.DoS(0, false, REJECT_NONSTANDARD,
                 "non-BIP68-final");
        return Result{state, pTxInputData, vCoinsToUncache};
    }

    // Checking for non-standard outputs as inputs.
    if (!acceptNonStandardOutput)
    {
        auto res =
            AreInputsStandard(source->GetToken(), config, tx, view, chainActive.Height() + 1);

        if (!res.has_value())
        {
            state.SetValidationTimeoutExceeded();
            state.DoS(0, false, REJECT_NONSTANDARD, "too-long-validation-time");
            return Result{state, pTxInputData, vCoinsToUncache};
        }
        else if (!res.value())
        {
            state.Invalid(false, REJECT_NONSTANDARD,
                         "bad-txns-nonstandard-inputs");
            return Result{state, pTxInputData, vCoinsToUncache};
        }
    }
    else if (fUseLimits && (TxValidationPriority::low != pTxInputData->GetTxValidationPriority()))
    {
        auto res =
            AreInputsStandard(source->GetToken(), config, tx, view, chainActive.Height() + 1);
        if (!res.has_value() || !res.value()) {
            state.SetValidationTimeoutExceeded();
            state.DoS(0, false, REJECT_NONSTANDARD, "too-long-validation-time");
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
    pool.ApplyDeltas(txid, nModifiedFees);

    // Calculate tx's size.
    const unsigned int nTxSize = ptx->GetTotalSize();

    // Make sure that underfunded consolidation transactions still pass.
    // Note that consolidation transactions paying a voluntary fee will
    // be treated with higher priority. The higher the fee the higher
    // the priority
    AnnotatedType<bool> isFree = IsFreeConsolidationTxn(config, tx, view, chainActive.Height());
    if (isFree.value) {
        const CFeeRate blockMinTxFee = pool.GetBlockMinTxFee();
        const Amount consolidationDelta = blockMinTxFee.GetFee(nTxSize);
        if (nModifiedFees == nFees) {
            pool.PrioritiseTransaction(txid, txid.ToString(), consolidationDelta);
            pool.ApplyDeltas(txid, nModifiedFees);
        }
        if(isFree.hint) {
            LogPrint(BCLog::TXNVAL,isFree.hint.value());
        }
    }

    // Keep track of transactions that spend a coinbase, which we re-scan
    // during reorgs to ensure COINBASE_MATURITY is still met.
    const bool fSpendsCoinbaseOrConfiscation = CheckTxSpendsCoinbaseOrConfiscation(tx, view);

    // Check mempool minimal fee requirement.
    const Amount& nMempoolRejectFee = GetMempoolRejectFee(config, pool, nTxSize);
    if(!CheckMempoolMinFee(nModifiedFees, nMempoolRejectFee)) {
        // If this was considered a consolidation but not accepted as such,
        // then print us a hint
        if (!isFree.value && isFree.hint)
            LogPrint(BCLog::TXNVAL,isFree.hint.value());
        state.DoS(0, false, REJECT_INSUFFICIENTFEE,
                 "mempool min fee not met",
                  strprintf("%d < %d", nFees, nMempoolRejectFee));
        return Result{state, pTxInputData, vCoinsToUncache};
    }
    //
    // Create an entry point for the transaction (a basic unit in the mempool).
    //
    // chainActive.Height() can never be negative when adding transactions to the mempool,
    // since active chain contains at least genesis block.
    int32_t uiChainActiveHeight = std::max(chainActive.Height(), 0);
    std::shared_ptr<CTxMemPoolEntry> pMempoolEntry {
        std::make_shared<CTxMemPoolEntry>(
            ptx,
            nFees,
            nAcceptTime,
            uiChainActiveHeight,
            fSpendsCoinbaseOrConfiscation,
            lp) };

    // Calculate in-mempool ancestors, up to a limit.
    std::string errString;
    if (!CheckAncestorLimits(pool, *pMempoolEntry, errString, config)) {
        state.DoS(0, false, REJECT_NONSTANDARD,
                 "too-long-mempool-chain",
                  errString);
        return Result{state, pTxInputData, vCoinsToUncache};
    }

    CFrozenTXOCheck frozenTXOCheck{
        chainActive.Tip()->GetHeight() + 1,
        TxInputDataToSource(*pTxInputData).ToString(),
        chainActive.Tip()->GetBlockHash(),
        nAcceptTime};

    // We are getting flags as they would be if the utxos are before genesis. 
    // "CheckInputs" is adding specific flags for each input based on its height in the main chain
    uint32_t scriptVerifyFlags = GetScriptVerifyFlags(config, IsGenesisEnabled(config, chainActive.Height() + 1));
    // Turn off flags that may be on in scriptVerifyFlags, but we explicitly want them to be skipped
    scriptVerifyFlags &= ~pTxInputData->GetSkipScriptFlags();
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
            txdata,
            frozenTXOCheck);

    if (!res.has_value())
    {
        state.SetValidationTimeoutExceeded();
        state.DoS(0, false, REJECT_NONSTANDARD,
                 "too-long-validation-time",
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
            viewMemPool,
            view,
            currentBlockScriptVerifyFlags,
            true,
            txdata);
    if (!res.has_value())
    {
        state.SetValidationTimeoutExceeded();
        state.DoS(0, false, REJECT_NONSTANDARD,
                 "too-long-validation-time",
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
                txdata,
                frozenTXOCheck);
        if (!res.has_value())
        {
            state.SetValidationTimeoutExceeded();
            state.DoS(0, false, REJECT_NONSTANDARD,
                     "too-long-validation-time",
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

    // Finished all script checks
    state.SetScriptsChecked();

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
                  std::move(pMempoolEntry)};
}

CValidationState HandleTxnProcessingException(
    const std::string& sExceptionMsg,
    const TxInputDataSPtr& pTxInputData,
    const CTxnValResult& txnValResult,
    const CTxMemPool& pool,
    CTxnHandlers& handlers) {

    const CTransactionRef& ptx = pTxInputData->GetTxnPtr();
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
             enum_cast<std::string>(pTxInputData->GetTxSource()),
             tx.GetId().ToString(),
             sTxnStateMsg);
    return state;
}


std::vector<std::pair<CTxnValResult, CTask::Status>> TxnValidationProcessingTask(
    const TxInputDataSPtrRefVec& vTxInputData,
    const Config& config,
    CTxMemPool& pool,
    CTxnHandlers& handlers,
    bool fUseLimits,
    std::chrono::steady_clock::time_point end_time_point) {
#ifdef COLLECT_METRICS
    static metrics::Histogram durations_t {"PTV_TX_TIME_MS", 5000};
    static metrics::Histogram durations_cpu {"PTV_TX_CPU_MS", 5000};
    static metrics::Histogram durations_chain_t {"PTV_CHAIN_TIME_MS", 5000};
    static metrics::Histogram durations_chain_cpu {"PTV_CHAIN_CPU_MS", 5000};
    static metrics::Histogram chainLengths {"PTV_CHAIN_LENGTH", 1000};
    static metrics::Histogram durations_mempool_t_ms {"PTV_MEMPOOL_DURATION_TIME_MS", 5000};
    static metrics::Histogram durations_mempool_t_s {"PTV_MEMPOOL_DURATION_TIME_S", 5000};
    static metrics::Histogram durations_queue_t_ms {"PTV_QUEUE_DURATION_TIME_MS", 5000};
    static metrics::Histogram durations_queue_t_s {"PTV_QUEUE_DURATION_TIME_S", 5000};
    static metrics::HistogramWriter histogramLogger {"PTV", std::chrono::milliseconds {10000}, []() {
        durations_t.dump();
        durations_cpu.dump();
        durations_chain_t.dump();
        durations_chain_cpu.dump();
        chainLengths.dump();
        durations_mempool_t_ms.dump();
        durations_mempool_t_s.dump();
        durations_queue_t_ms.dump();
        durations_queue_t_s.dump();
    }};
    chainLengths.count(vTxInputData.size());
    auto chainTimeTimer = metrics::TimedScope<std::chrono::steady_clock, std::chrono::milliseconds> {durations_chain_t};
    auto chainCpuTimer = metrics::TimedScope<task::thread_clock, std::chrono::milliseconds> {durations_chain_cpu};
#endif
    size_t chainLength = vTxInputData.size();
    if (chainLength > 1) {
        LogPrint(BCLog::TXNVAL,
                "A non-trivial chain detected, length=%zu\n", chainLength);
    }
    std::vector<std::pair<CTxnValResult, CTask::Status>> results {};
    auto cancellationBudget = task::CTimedCancellationBudget {config.GetMaxTxnChainValidationBudget()};
    results.reserve(chainLength);
    for (const auto& elem : vTxInputData) {
        // Check if time to trigger validation elapsed (skip this check if end_time_point == 0).
        if (!(std::chrono::steady_clock::time_point(std::chrono::milliseconds(0)) == end_time_point) &&
            !(std::chrono::steady_clock::now() < end_time_point) &&
            (results.empty() || results.back().first.mState.IsValid())) {
            // it's safe to cancel (and retry) the chain only when the chain has processed OK.
            // otherwise we rely on the error-copying approach below
            results.emplace_back(CTxnValResult{CValidationState(), elem.get()}, CTask::Status::Canceled);
            continue;
        }
        CTxnValResult result {};
        try {
#ifdef COLLECT_METRICS
            auto timeTimer = metrics::TimedScope<std::chrono::steady_clock, std::chrono::milliseconds> { durations_t };
            auto cpuTimer = metrics::TimedScope<task::thread_clock, std::chrono::milliseconds> { durations_cpu };
            if (!elem.get()->IsOrphanTxn()) {
                // we are first time through validation === time in network queues
                auto e2e = elem.get()->GetLifetime();
                durations_queue_t_ms.count(std::chrono::duration_cast<std::chrono::milliseconds>(e2e).count());
                durations_queue_t_s.count(std::chrono::duration_cast<std::chrono::seconds>(e2e).count());
            }
#endif
            // Execute validation for the given txn
            result =
                TxnValidation(
                    elem,
                    elem.get()->GetConfig(config),
                    pool,
                    handlers.mpTxnDoubleSpendDetector,
                    fUseLimits,
                    cancellationBudget);
            // Process validated results
            ProcessValidatedTxn(pool, result, handlers, false, config);
#ifdef COLLECT_METRICS
            if (result.mState.IsValid()) {
                using namespace std::chrono;
                auto e2e = result.mTxInputData->GetLifetime();
                durations_mempool_t_ms.count(duration_cast<milliseconds>(e2e).count());
                durations_mempool_t_s.count(duration_cast<seconds>(e2e).count());
            }
#endif
            // Forward results to the next processing stage
            results.emplace_back(std::move(result), CTask::Status::RanToCompletion);
        } catch (const std::exception& e) {
            results.emplace_back(
                    CTxnValResult{HandleTxnProcessingException("An exception thrown in txn processing: " + std::string(e.what()),
                      elem.get(),
                      result,
                      pool,
                      handlers), elem.get()},
                    CTask::Status::Faulted);
        } catch (...) {
            results.emplace_back(
                    CTxnValResult{HandleTxnProcessingException("Unexpected exception in txn processing",
                      elem.get(),
                      result,
                      pool,
                      handlers), elem.get()},
                    CTask::Status::Faulted);
        }
    }
    return results;
}

static void HandleInvalidP2POrphanTxn(
    const CTxnValResult& txStatus,
    CTxnHandlers& handlers);

static void HandleInvalidP2PNonOrphanTxn(
    const CTxnValResult& txStatus,
    CTxnHandlers& handlers,
    const Config &config);

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
    const bool fOrphanTxn = txStatus.mTxInputData->IsOrphanTxn();
    const CTransactionRef& ptx = txStatus.mTxInputData->GetTxnPtr();
    const CTransaction &tx = *ptx;
    const CValidationState& state = txStatus.mState;
    const TxSource source = txStatus.mTxInputData->GetTxSource();
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
    const size_t& nPrimaryMempoolSize,
    const size_t& nSecondaryMempoolSize,
    const size_t& nDynamicMemoryUsage) {

    const bool fOrphanTxn = txStatus.mTxInputData->IsOrphanTxn();
    const CTransactionRef& ptx = txStatus.mTxInputData->GetTxnPtr();
    const CTransaction &tx = *ptx;
    const CValidationState& state = txStatus.mState;
    const CNodePtr& pNode = txStatus.mTxInputData->GetNodePtr().lock();
    const TxSource source = txStatus.mTxInputData->GetTxSource();
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
             "%s: %s txn= %s %s (poolsz %zu txn (pri=%zu,sec=%zu), %zu kB) %s\n",
             enum_cast<std::string>(source),
             state.IsStandardTx() ? "standard" : "nonstandard",
             tx.GetId().ToString(),
             sTxnStatusMsg,
             nPrimaryMempoolSize + nSecondaryMempoolSize,
             nPrimaryMempoolSize,
             nSecondaryMempoolSize,
             nDynamicMemoryUsage / 1000,
             TxSource::p2p == source ? "peer=" + csPeerId  : "");
}

void PublishInvalidTransaction(CTxnValResult& txStatus)
{
    if (!g_connman)
    {
        return;
    }

    const bool processingCompleted = 
            (TxValidationPriority::low == txStatus.mTxInputData->GetTxValidationPriority() ||
            !txStatus.mState.IsValidationTimeoutExceeded());

    if (!processingCompleted)
    {
        // we will end up in the low priority queue
        return;
    }

    auto pNode = txStatus.mTxInputData->GetNodePtr().lock();
    InvalidTxnInfo::TxDetails details{
        txStatus.mTxInputData->GetTxSource(),
        pNode ? pNode->GetId() : -1,
        pNode ? pNode->GetAddrName() : ""};
    g_connman->getInvalidTxnPublisher().Publish(
        {txStatus.mTxInputData->GetTxnPtr(), details, std::time(nullptr), txStatus.mState} );
}

void ProcessValidatedTxn(
    CTxMemPool& pool,
    CTxnValResult& txStatus,
    CTxnHandlers& handlers,
    bool fLimitMempoolSize,
    const Config &config) {

    TxSource source {
        txStatus.mTxInputData->GetTxSource()
    };
    CValidationState& state = txStatus.mState;
    const CTransactionRef& ptx = txStatus.mTxInputData->GetTxnPtr();
    const CTransaction &tx = *ptx;
    const TxStorage txStorage = txStatus.mTxInputData->GetTxStorage();
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
            const bool fOrphanTxn = txStatus.mTxInputData->IsOrphanTxn();
            if (fOrphanTxn) {
                HandleInvalidP2POrphanTxn(txStatus, handlers);
            } else {
                HandleInvalidP2PNonOrphanTxn(txStatus, handlers, config);
            }
        } else if (handlers.mpOrphanTxns && state.IsMissingInputs()) {
            handlers.mpOrphanTxns->addTxn(txStatus.mTxInputData);
        }

        // Skip publish transactions with rejection reason txn-already-in-mempool
        // or txn-already-known.
        if (state.GetRejectCode() != REJECT_ALREADY_KNOWN) {
            PublishInvalidTransaction(txStatus);
        }

        // Logging txn status
        LogTxnInvalidStatus(txStatus);
    }
    // Txn validation has succeeded.
    else {
        /**
         * Send transaction to the mempool
         */
        size_t nPrimaryMempoolSize {};
        size_t nSecondaryMempoolSize {};
        size_t nDynamicMemoryUsage {};
        // Check if required log categories are enabled
        bool fMempoolLogs = LogAcceptCategory(BCLog::MEMPOOL) || LogAcceptCategory(BCLog::MEMPOOLREJ);
        // Commit transaction
        CommitTxToMempool(
            txStatus.mTxInputData,
            *(txStatus.mpEntry),
            txStorage,
            pool,
            state,
            handlers.mJournalChangeSet,
            fLimitMempoolSize,
            fMempoolLogs ? &nPrimaryMempoolSize : nullptr,
            fMempoolLogs ? &nSecondaryMempoolSize : nullptr,
            fMempoolLogs ? &nDynamicMemoryUsage : nullptr);
        // Check txn's commit status and do all required actions.
        if (TxSource::p2p == source) {
            PostValidationStepsForP2PTxn(txStatus, pool, handlers);
        }
        else if(TxSource::finalised == source) {
            PostValidationStepsForFinalisedTxn(txStatus, pool, handlers);
        }
        // Logging txn commit status
        if (!state.IsResubmittedTx()) {
            LogTxnCommitStatus(txStatus,
                               nPrimaryMempoolSize,
                               nSecondaryMempoolSize,
                               nDynamicMemoryUsage);
        }
    }
    // If txn validation or commit has failed then:
    // - uncache coins
    // If txn is accepted by the mempool and orphan handler is present then:
    // - collect txn's outpoints
    // - remove txn from the orphan queue
    if (!state.IsValid()) {
        if(!txStatus.mCoinsToUncache.empty())
        {
            // This is necessary even for new transactions that don't change
            // coins database as there is uncaching mechanism that uncaches
            // coins that were loaded for transaction validation and weren't
            // in the cache before the validation started.
            pcoinsTip->Uncache(txStatus.mCoinsToUncache);
        }
    } else if (handlers.mpOrphanTxns) {
        // At this stage we want to collect tx data of successfully accepted txn.
        // There might be other related txns being validated at the same time.
        handlers.mpOrphanTxns->collectTxData(tx);
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
    const CTransaction &tx,
    const Config &config) {
    for (const CTxIn &txin : tx.vin) {
        // FIXME: MSG_TX should use a TxHash, not a TxId.
        CInv inv(MSG_TX, txin.prevout.GetTxId());
        pNode->AddInventoryKnown(inv);
        // Check if txn is already known.
        if (!IsTxnKnown(inv)) {
            pNode->AskFor(inv, config);
        }
    }
}

static void HandleOrphanAndRejectedP2PTxns(
    const CNodePtr& pNode,
    const CTxnValResult& txStatus,
    CTxnHandlers& handlers,
    const Config &config) {

    const CTransactionRef& ptx = txStatus.mTxInputData->GetTxnPtr();
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
            AskForMissingParents(pNode, tx, config);
            handlers.mpOrphanTxns->addTxn(txStatus.mTxInputData);
        }
        // DoS prevention: do not allow mpOrphanTxns to grow unbounded
        uint64_t nMaxOrphanTxnsSize{
            GlobalConfig::GetConfig().GetMaxOrphanTxSize()
        };
        uint64_t nMaxOrphanTxnHysteresis { nMaxOrphanTxnsSize / 10 }; // 10% seems to work fine
        unsigned int nEvicted = handlers.mpOrphanTxns->limitTxnsSize(nMaxOrphanTxnsSize, nMaxOrphanTxnHysteresis);
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

    const CNodePtr& pNode = pTxInputData->GetNodePtr().lock();
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
                            pTxInputData->GetTxnPtr()->GetId()));
    }
}

static void HandleInvalidP2POrphanTxn(
    const CTxnValResult& txStatus,
    CTxnHandlers& handlers) {

    const CNodePtr& pNode = txStatus.mTxInputData->GetNodePtr().lock();
    if (!pNode) {
        LogPrint(BCLog::TXNVAL, "An invalid reference: Node doesn't exist\n");
        return;
    }
    const CTransactionRef& ptx = txStatus.mTxInputData->GetTxnPtr();
    const CTransaction &tx = *ptx;
    const CValidationState& state = txStatus.mState;
    // Check if the given p2p txn is considered as fully processed (validated)
    const bool fTxProcessingCompleted =
        (TxValidationPriority::low == txStatus.mTxInputData->GetTxValidationPriority() ||
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
    CTxnHandlers& handlers,
    const Config &config) {

    const CNodePtr& pNode = txStatus.mTxInputData->GetNodePtr().lock();
    if (!pNode) {
        LogPrint(BCLog::TXNVAL, "An invalid reference: Node doesn't exist\n");
        return;
    }
    const CValidationState& state = txStatus.mState;
    // Handle txn with missing inputs
    if (state.IsMissingInputs()) {
        HandleOrphanAndRejectedP2PTxns(pNode, txStatus, handlers, config);
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

    const CTransactionRef& ptx = txStatus.mTxInputData->GetTxnPtr();
    const CTransaction &tx = *ptx;
    const CValidationState& state = txStatus.mState;
    // Check if the given p2p txn is considered as fully processed (validated).
    if (TxValidationPriority::low == txStatus.mTxInputData->GetTxValidationPriority() ||
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

    const CNodePtr& pNode = txStatus.mTxInputData->GetNodePtr().lock();
    if (!pNode) {
        LogPrint(BCLog::TXNVAL, "An invalid reference: Node doesn't exist\n");
        return;
    }
    const CTransactionRef& ptx = txStatus.mTxInputData->GetTxnPtr();
    const CValidationState& state = txStatus.mState;
    // Post processing step for successfully commited txns (non-orphans & orphans)
    if (state.IsValid()) {
        // Finalising txns have another round of validation before making it into the
        // mempool, hold off relaying them until that has completed.
        if(pool.Exists(ptx->GetId()) || pool.getNonFinalPool().exists(ptx->GetId())) {
            pool.CheckMempool(*pcoinsTip, handlers.mJournalChangeSet);
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
        if (TxValidationPriority::low == txStatus.mTxInputData->GetTxValidationPriority() ||
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
    const CTransactionRef& ptx { txStatus.mTxInputData->GetTxnPtr() };
    const CValidationState& state { txStatus.mState };

    if(state.IsValid())
    {
        pool.CheckMempool(*pcoinsTip, handlers.mJournalChangeSet);
        RelayTransaction(*ptx, *g_connman);
    }
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
    const CBlockIndex *pindexSlow = nullptr;
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
            if (!BlockFileAccess::LoadBlockHashAndTx( postx, hashBlock, txOut ))
            {
                return false;
            }
            if (txOut->GetId() != txid) {
                return error("%s: txid mismatch", __func__);
            }
            auto foundBlockIndex = mapBlockIndex.Get(hashBlock);
            if (!foundBlockIndex)
            {
                return error("%s: mapBlockIndex mismatch  ", __func__);
            }
            isGenesisEnabled = IsGenesisEnabled(config, foundBlockIndex->GetHeight());
            return true;
        }
    }

    // use coin database to locate block that contains transaction, and scan it
    if (fAllowSlow) {
        CoinsDBView view{ *pcoinsTip };

        if (auto coin = view.GetCoinByTxId(txid);
            coin.has_value() && !coin->IsSpent()) {
            pindexSlow = chainActive[coin->GetHeight()];
        }
    }

    if (pindexSlow) {
        if (auto blockStreamReader=pindexSlow->GetDiskBlockStreamReader(config)) {
            while (!blockStreamReader->EndOfStream()) {
                const CTransaction* tx = blockStreamReader->ReadTransaction_NoThrow();
                if(!tx)
                {
                    break;
                }
                if (tx->GetId() == txid) {
                    txOut = blockStreamReader->GetLastTransactionRef();
                    hashBlock = pindexSlow->GetBlockHash();
                    isGenesisEnabled = IsGenesisEnabled(config, pindexSlow->GetHeight());
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

Amount GetBlockSubsidy(int32_t nHeight, const Consensus::Params &consensusParams) {
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
    // Once this function has returned false, all subsequent calls from the same
    // thread will always return false.
    static std::atomic<bool> latchToFalse{false};
    // Optimization: pre-test latch before taking the lock.
    if (latchToFalse.load(std::memory_order_relaxed)) {
        return false;
    }

    static std::mutex mutexLatchToFalse;

    std::lock_guard lock{ mutexLatchToFalse };

    if (latchToFalse.load(std::memory_order_relaxed)) {
        return false;
    }

    auto tip = chainActive.Tip();

    if (fImporting || fReindex) {
        return true;
    }
    if (tip == nullptr) {
        return true;
    }
    if (tip->GetChainWork() < nMinimumChainWork)
    {
        return true;
    }
    if (tip->GetBlockTime() < (GetTime() - nMaxTipAge)) {
        return true;
    }
    LogPrintf("Leaving InitialBlockDownload (latching to false)\n");
    latchToFalse.store(true, std::memory_order_relaxed);
    return false;
}

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



/**
    * Finds first invalid block from pindexForkTip. Returns nullptr if none was found.
    */
const CBlockIndex* FindInvalidBlockOnFork(const CBlockIndex* pindexForkTip)
{
    AssertLockHeld(cs_main);

    const CBlockIndex* pindexWalk = pindexForkTip;
    while (pindexWalk && !chainActive.Contains(pindexWalk))
    {
        if (pindexWalk->getStatus().isInvalid())
        {
            return pindexWalk;
        }
        pindexWalk = pindexWalk->GetPrev();
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
            pindexWalk->ModifyStatusWithFailedParent(mapBlockIndex);
            setBlockIndexCandidates.erase(pindexWalk);
            pindexWalk = pindexWalk->GetPrev();
        }
    }
}

/**
 * This method finds all chain tips except active tip
 */
std::set<CBlockIndex*> GetForkTips()
{
    AssertLockHeld(cs_main);
    
    std::set<CBlockIndex*> setTips;
    std::set<CBlockIndex*> setTipCandidates;
    std::set<CBlockIndex*> setPrevs;

    mapBlockIndex.ForEachMutable(
        [&](CBlockIndex& index)
        {
            if (!chainActive.Contains(&index))
            {
                setTipCandidates.insert(&index);
                setPrevs.insert(index.GetPrev());
            }
        });

    
    std::set_difference(setTipCandidates.begin(), setTipCandidates.end(),
                        setPrevs.begin(), setPrevs.end(),
                        std::inserter(setTips, setTips.begin()));
    
    return setTips;
}

/**
 * This method is called on node startup. It has two tasks:
 *  1. Restore global safe mode state
 *  2. Validate that all header only fork tips have correct tip status
 */
void CheckSafeModeParametersForAllForksOnStartup(const Config& config)
{
    LOCK(cs_main);

    int64_t nStart = GetTimeMillis();

    std::set<CBlockIndex*> setTips = GetForkTips();

    SafeModeClear();

    for (auto& tip :setTips)
    {
        // This is needed because older versions of node did not correctly 
        // mark descendants of an invalid block on forks.
        if (!tip->getStatus().isInvalid() && tip->GetChainTx() == 0)
        {
            // if tip is valid headers only check fork if it has invalid block
            CheckForkForInvalidBlocks(tip);
        }
        // Restore global safe mode state,
        CheckSafeModeParameters(config, tip);
    }
    LogPrintf("%s: global safe mode state restored to level %d in %dms\n", 
              __func__, static_cast<int>(GetSafeModeLevel()),
              GetTimeMillis() - nStart);
}

static void InvalidChainFound(const Config& config, const CBlockIndex *pindexNew)
{
    auto chainWork = pindexNew->GetChainWork();
    if (!pindexBestInvalid ||
        chainWork > pindexBestInvalid->GetChainWork())
    {
        pindexBestInvalid = pindexNew;
    }

    LogPrintf(
        "%s: invalid block=%s  height=%d  log2_work=%.8g  date=%s\n", __func__,
        pindexNew->GetBlockHash().ToString(), pindexNew->GetHeight(),
        log(chainWork.getdouble()) / log(2.0),
        DateTimeStrFormat("%Y-%m-%d %H:%M:%S", pindexNew->GetBlockTime()));
    const CBlockIndex *tip = chainActive.Tip();
    assert(tip);
    LogPrintf("%s:  current best=%s  height=%d  log2_work=%.8g  date=%s\n",
              __func__, tip->GetBlockHash().ToString(), chainActive.Height(),
              log(tip->GetChainWork().getdouble()) / log(2.0),
              DateTimeStrFormat("%Y-%m-%d %H:%M:%S", tip->GetBlockTime()));
    CheckSafeModeParameters(config, pindexNew);
}

static void InvalidBlockFound(const Config& config,
                              CBlockIndex* pindex,
                              const CBlock& block,
                              const CValidationState& state)
{
    if(state.GetRejectCode() != REJECT_SOFT_CONSENSUS_FREEZE &&
       !state.CorruptionPossible())
    {
        pindex->ModifyStatusWithFailed(mapBlockIndex);
        setBlockIndexCandidates.erase(pindex);
        InvalidChainFound(config, pindex);
    }

    // Update miner ID database if required
    if(g_minerIDs) {
        g_minerIDs->InvalidBlock(block, pindex->GetHeight());
    }
}

void UpdateCoins(const CTransaction& tx, ICoinsViewCache& inputs,
                 CTxUndo& txundo, int32_t nHeight)
{
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
    AddCoins(inputs, tx, CFrozenTXOCheck::IsConfiscationTx(tx), nHeight, GlobalConfig::GetConfig().GetGenesisActivationHeight());
}

void UpdateCoins(const CTransaction& tx, ICoinsViewCache& inputs, int32_t nHeight)
{
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

std::pair<int32_t,int> GetSpendHeightAndMTP(const ICoinsViewCache& inputs)
{
    const CBlockIndex* pindexPrev = mapBlockIndex.Get(inputs.GetBestBlock());
    return { pindexPrev->GetHeight() + 1, pindexPrev->GetMedianTimePast() };
}

namespace Consensus {
bool CheckTxInputs(const CTransaction& tx, CValidationState& state,
                   const ICoinsViewCache& inputs, int32_t nSpendHeight,
                   CFrozenTXOCheck& frozenTXOCheck)
{
    // This doesn't trigger the DoS code on purpose; if it did, it would make it
    // easier for an attacker to attempt to split the network.
    if (!inputs.HaveInputs(tx)) {
        return state.Invalid(false, 0, "", "Inputs unavailable");
    }

    // Are we checking inputs for confiscation transaction?
    const bool isConfiscationTx = CFrozenTXOCheck::IsConfiscationTx(tx);
    if(isConfiscationTx)
    {
        // Validate contents of confiscation transaction
        if(!CFrozenTXOCheck::ValidateConfiscationTxContents(tx))
        {
            return
                state.Invalid(
                    false,
                    REJECT_INVALID,
                    "bad-ctx-invalid",
                    "confiscation transaction is invalid");
        }

        // Confiscation transaction must be whitelisted and valid at given height
        if(!frozenTXOCheck.CheckConfiscationTxWhitelisted(tx))
        {
            return
                state.Invalid(
                    false,
                    REJECT_INVALID,
                    "bad-ctx-not-whitelisted",
                    "confiscation transaction is not whitelisted");
        }
    }

    Amount nValueIn(0);
    Amount nFees(0);
    for (const auto &in : tx.vin) {
        const COutPoint &prevout = in.prevout;

        if(!isConfiscationTx)
        {
            // For normal transaction no input must be frozen
            if(!frozenTXOCheck.Check(prevout, tx))
            {
                return
                    state.Invalid(
                        false,
                    frozenTXOCheck.IsCheckOnBlock() ?
                        REJECT_SOFT_CONSENSUS_FREEZE :
                        REJECT_INVALID,
                        "bad-txns-inputs-frozen",
                        "tried to spend blacklisted input");
            }
        }
        // For confiscation transaction all inputs must be frozen, but this is implicitly guaranteed here,
        // since confiscation transaction is whitelisted and consequently all of its inputs are on Confiscation
        // blacklist and therefore consensus frozen on every height.

        auto coin = inputs.GetCoin(prevout);
        assert(coin.has_value() && !coin->IsSpent());

        // If prev is coinbase, check that it's matured
        if (coin->IsCoinBase()) {
            if (nSpendHeight - coin->GetHeight() < COINBASE_MATURITY) {
                return state.Invalid(
                    false, REJECT_INVALID,
                    "bad-txns-premature-spend-of-coinbase",
                    strprintf("tried to spend coinbase at depth %d",
                              nSpendHeight - coin->GetHeight()));
            }
        }

        // If prev is output of a confiscation transaction, check that it's matured
        if (coin->IsConfiscation()) {
            if (nSpendHeight - coin->GetHeight() < CONFISCATION_MATURITY) {
                return state.Invalid(
                    false, REJECT_INVALID,
                    "bad-txns-premature-spend-of-confiscation",
                    strprintf("tried to spend confiscation at depth %d",
                              nSpendHeight - coin->GetHeight()));
            }
        }

        // Check for negative or overflow input values
        nValueIn += coin->GetAmount();
        if (!MoneyRange(coin->GetAmount()) || !MoneyRange(nValueIn)) {
            return state.DoS(100, false, REJECT_INVALID,
                             "bad-txns-inputvalues-outofrange");
        }
    }

    if (nValueIn < tx.GetValueOut()) {
        return state.DoS(100, false, REJECT_INVALID, "bad-txns-in-belowout",
                         strprintf("value in (%s) < value out (%s)",
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

int32_t GetInputScriptBlockHeight(int32_t coinHeight) {
    if (coinHeight == MEMPOOL_HEIGHT) {
        // When spending an output that was created in mempool, we assume that it will be mined in the next block.
        return chainActive.Height() + 1;
    }

    return coinHeight;
}

std::optional<bool> CheckInputScripts(
    const task::CCancellationToken& token,
    const Config& config,
    bool consensus,
    const CScript& scriptPubKey,
    const Amount& amount,
    const CTransaction& tx,
    CValidationState& state,
    size_t input,
    int32_t coinHeight,
    int32_t spendHeight,
    const uint32_t flags,
    bool sigCacheStore,
    const PrecomputedTransactionData& txdata,
    std::vector<CScriptCheck>* pvChecks)
{
    int32_t inputScriptBlockHeight = GetInputScriptBlockHeight(coinHeight);
    uint32_t perInputScriptFlags = 0;
    bool isGenesisEnabled = IsGenesisEnabled(config, inputScriptBlockHeight);
    if (isGenesisEnabled)
    {
        perInputScriptFlags = SCRIPT_UTXO_AFTER_GENESIS;
    }

    // ScriptExecutionCache does NOT contain per-input flags. That's why we clear the
    // cache when we are about to cross genesis activation line (see function FinalizeGenesisCrossing).
    // Verify signature
    CScriptCheck check(config, consensus, scriptPubKey, amount, tx, input, flags | perInputScriptFlags, sigCacheStore, txdata);
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
        // A violation of policy limit, for max-script-num-length, results in an increase of banning score by 10.
        // A failure is detected by a script number overflow in computations.
        if (!genesisGracefulPeriod && !consensus && SCRIPT_ERR_SCRIPTNUM_OVERFLOW == check.GetScriptError()) {
            return state.DoS(
                10, false, REJECT_INVALID,
                strprintf("max-script-num-length-policy-limit-violated (%s)",
                          ScriptErrorString(check.GetScriptError())));
        }
        // Checking script conditions with non-mandatory flags.
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
                scriptPubKey, amount, tx, input,
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
                    scriptPubKey, amount, tx, input,
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

    return true;
}

std::optional<bool> CheckInputs(
    const task::CCancellationToken& token,
    const Config& config,
    bool consensus,
    const CTransaction& tx,
    CValidationState& state,
    const ICoinsViewCache& inputs,
    bool fScriptChecks,
    const uint32_t flags,
    bool sigCacheStore,
    bool scriptCacheStore,
    const PrecomputedTransactionData& txdata,
    CFrozenTXOCheck& frozenTXOCheck,
    std::vector<CScriptCheck>* pvChecks)
{
    assert(!tx.IsCoinBase());

    const auto [ spendHeight, mtp ] = GetSpendHeightAndMTP(inputs);
    (void)mtp;  // Silence unused variable warning
    if (!Consensus::CheckTxInputs(tx, state, inputs, spendHeight, frozenTXOCheck))
    {
        return false;
    }

    if(CFrozenTXOCheck::IsConfiscationTx(tx))
    {
        // If we're checking inputs for confiscation transaction, scripts are valid by definition and do not need to be checked.
        // Note that here we already know that confiscation transaction is valid (whitelisted, valid contents, unspent inputs...) because this was checked by CheckTxInputs() above.
        return true;
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
        auto coin = inputs.GetCoinWithScript(prevout);
        assert(coin.has_value() && !coin->IsSpent());

        // We very carefully only pass in things to CScriptCheck which are
        // clearly committed to by tx' witness hash. This provides a sanity
        // check that our caching is not introducing consensus failures through
        // additional data in, eg, the coins being spent being checked as a part
        // of CScriptCheck.
        const CScript &scriptPubKey = coin->GetTxOut().scriptPubKey;
        const Amount amount = coin->GetTxOut().nValue;

        auto res = CheckInputScripts(token, config, consensus, scriptPubKey, amount, tx, state, i, coin->GetHeight(),
            spendHeight, flags, sigCacheStore, txdata, pvChecks);
        if(!res.has_value())
        {
            return {};
        }
        else if(!res.value())
        {
            return false;
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

/** Restore the UTXO in a Coin at a given COutPoint. */
DisconnectResult UndoCoinSpend(const CoinWithScript &undo, CCoinsViewCache &view,
                               const COutPoint &out, const Config &config) {
    bool fClean = true;

    if (view.HaveCoin(out)) {
        // Overwriting transaction output.
        fClean = false;
    }

    // The potential_overwrite parameter to AddCoin is only allowed to be false
    // if we know for sure that the coin did not already exist in the cache. As
    // we have queried for that above using HaveCoin, we don't need to guess.
    // When fClean is false, a coin already existed and it is an overwrite.
    view.AddCoin(out, undo.MakeOwning(), !fClean, config.GetGenesisActivationHeight());

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

uint32_t GetBlockScriptFlags(const Config& config, const CBlockIndex* pChainTip)
{
    const Consensus::Params &consensusparams =
        config.GetChainParams().GetConsensus();

    uint32_t flags = SCRIPT_VERIFY_NONE;

    // P2SH didn't become active until Apr 1 2012
    if (pChainTip->GetMedianTimePast() >= P2SH_ACTIVATION_TIME) {
        flags |= SCRIPT_VERIFY_P2SH;
    }

    // Start enforcing the DERSIG (BIP66) rule
    if ((pChainTip->GetHeight() + 1) >= consensusparams.BIP66Height) {
        flags |= SCRIPT_VERIFY_DERSIG;
    }

    // Start enforcing CHECKLOCKTIMEVERIFY (BIP65) rule
    if ((pChainTip->GetHeight() + 1) >= consensusparams.BIP65Height) {
        flags |= SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY;
    }

    // Start enforcing BIP112 (CSV).
    if ((pChainTip->GetHeight() + 1) >= consensusparams.CSVHeight) {
        flags |= SCRIPT_VERIFY_CHECKSEQUENCEVERIFY;
    }

    // If the UAHF is enabled, we start accepting replay protected txns
    if (IsUAHFenabled(config, pChainTip->GetHeight())) {
        flags |= SCRIPT_VERIFY_STRICTENC;
        flags |= SCRIPT_ENABLE_SIGHASH_FORKID;
    }

    // If the DAA HF is enabled, we start rejecting transaction that use a high
    // s in their signature. We also make sure that signature that are supposed
    // to fail (for instance in multisig or other forms of smart contracts) are
    // null.
    if (IsDAAEnabled(config, pChainTip->GetHeight())) {
        flags |= SCRIPT_VERIFY_LOW_S;
        flags |= SCRIPT_VERIFY_NULLFAIL;
    }

    if (IsGenesisEnabled(config, pChainTip->GetHeight() + 1)) {
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



class BlockConnector
{
public:
    BlockConnector(
        bool parallelBlockValidation_,
        bool parallelTxnValidation_,
        const Config& config_,
        const CBlock& block_,
        CValidationState& state_,
        CBlockIndex* pindex_,
        CCoinsViewCache& view_,
        std::int32_t mostWorkBlockHeight_,
        const arith_uint256& mostWorkOnChain_,
        bool fJustCheck_ )
    : config{ config_ }
    , block{ block_ }
    , state{ state_ }
    , pindex{ pindex_ }
    , view{ view_ }
    , mostWorkBlockHeight{ mostWorkBlockHeight_ }
    , mostWorkOnChain{ mostWorkOnChain_ }
    , fJustCheck{ fJustCheck_ }
    , parallelBlockValidation{ parallelBlockValidation_ }
    , parallelTxnValidation{ parallelTxnValidation_ }
    {}

    bool Connect( const task::CCancellationToken& token )
    {
        AssertLockHeld(cs_main);

        int64_t nTimeStart = GetTimeMicros();

        // Check it again in case a previous version let a bad block in
        BlockValidationOptions validationOptions = BlockValidationOptions()
            .withCheckPoW(!fJustCheck)
            .withCheckMerkleRoot(!fJustCheck);
        if (!CheckBlock(config, block, state, pindex->GetHeight(), validationOptions)) {
            return error("%s: Consensus::CheckBlock: %s", __func__,
                         FormatStateMessage(state));
        }

        // Verify that the view's current state corresponds to the previous block
        uint256 hashPrevBlock =
            pindex->IsGenesis() ? uint256() : pindex->GetPrev()->GetBlockHash();
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
        bool fEnforceBIP30 = !((pindex->GetHeight() == 91842 &&
                                pindex->GetBlockHash() ==
                                    uint256S("0x00000000000a4d0a398161ffc163c503763"
                                             "b1f4360639393e0e4c8e300e0caec")) ||
                               (pindex->GetHeight() == 91880 &&
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
        const CBlockIndex* pindexBIP34height =
            pindex->GetPrev()->GetAncestor(consensusParams.BIP34Height);
        // Only continue to enforce if we're below BIP34 activation height or the
        // block hash at that height doesn't correspond.
        fEnforceBIP30 =
            fEnforceBIP30 &&
            (!pindexBIP34height ||
             !(pindexBIP34height->GetBlockHash() == consensusParams.BIP34Hash));

        if(config.GetDisableBIP30Checks())
        {
            fEnforceBIP30 = false;
        }

        if (fEnforceBIP30) {
            for (const auto &tx : block.vtx) {
                for (size_t o = 0; o < tx->vout.size(); o++) {
                    if (view.HaveCoin(COutPoint(tx->GetId(), o))) {
                        auto result = state.DoS(
                            100,
                            error("ConnectBlock(): tried to overwrite transaction"),
                            REJECT_INVALID, "bad-txns-BIP30");
                        if(!state.IsValid() && g_connman)
                        {
                            g_connman->getInvalidTxnPublisher().Publish( { tx, pindex, state } );
                        }
                        return result;
                    }
                }
            }
        }

        const int64_t nTime2 = GetTimeMicros();
        nTimeForks += nTime2 - nTime1;
        LogPrint(BCLog::BENCH, "    - Fork checks: %.2fms [%.2fs]\n",
                 0.001 * (nTime2 - nTime1), nTimeForks * 0.000001);

        CBlockUndo blockundo;

        std::atomic_size_t nInputs = 0;

        int64_t nTime4; // This is set inside scope below

        if (parallelBlockValidation)
        {
            /* Script validation is the most expensive part and is also not cs_main
            dependent so in case of parallel block validation we release it for
            the duration of validation.
            After we obtain the lock once again we check if chain tip has changed
            in the meantime - if not we continue as if we had a lock all along,
            otherwise we skip chain tip update part and retry with a new candidate.*/
            class LeaveCriticalSectionGuard
            {
            public:
                LeaveCriticalSectionGuard( CCoinsViewCache& view )
                    : mView{ view }
                {
                    LEAVE_CRITICAL_SECTION(cs_main)
                }
                ~LeaveCriticalSectionGuard()
                {
                    // Make sure that we aren't holding view locked before
                    // re-obtaining cs_main as that could cause a dead lock.
                    mView.ForceDetach();
                    ENTER_CRITICAL_SECTION(cs_main)
                }

            private:
                CCoinsViewCache& mView;
            } csGuard{ view };

            if (!checkScripts( token, nTime2, nInputs, blockundo, mostWorkBlockHeight ))
            {
                return false;
            }

            // must be inside this scope as csGuard can take a while to re-obtain
            // cs_main lock and we don't want that time to count to validation
            // duration time
            nTime4 = GetTimeMicros();
        }
        else
        {
            if (!checkScripts( token, nTime2, nInputs, blockundo, mostWorkBlockHeight ))
            {
                return false;
            }

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
        {
            // since we are changing validation time we need to update
            // setBlockIndexCandidates as well - it sorts by that time
            setBlockIndexCandidates.erase(pindex);

            bool res =
                 pindex->writeUndoToDisk(
                    state,
                    blockundo,
                    fCheckForPruning,
                    config,
                    mapBlockIndex);

            setBlockIndexCandidates.insert(pindex);

            if (!res)
            {
                // Failed to write undo data.
                return false;
            }
        }

        if (fTxIndex)
        {
            // Calculate transaction indexing information
            std::vector<std::pair<uint256, CDiskTxPos>> vPos {};
            vPos.reserve(block.vtx.size());

            CDiskTxPos pos { pindex->GetBlockPos(), GetSizeOfCompactSize(block.vtx.size()) };
            for(const auto& txn : block.vtx)
            {
                vPos.push_back(std::make_pair(txn->GetId(), pos));
                pos = { pos, pos.TxOffset() + ::GetSerializeSize(*txn, SER_DISK, CLIENT_VERSION) };
            }

            // Write it out
            if(!pblocktree->WriteTxIndex(vPos))
            {
                return AbortNode(state, "Failed to write transaction index");
            }
        }

        if (parallelBlockValidation)
        {
            // TryReattach() will succeed if best block in active chain hasn't
            // changed since ForceDetach().
            if (!view.TryReattach())
            {
                // a different block managed to become best block before this one
                // so we should terminate connecting process
                throw CBestBlockAttachmentCancellation{};
            }
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

private:

    using ScriptChecker = checkqueue::CCheckQueuePool<CScriptCheck, arith_uint256>::CCheckQueueScopeGuard;

    bool BlockValidateTxns(size_t shardNum,
                           CCoinsViewCache::Shard& shard,
                           const std::vector<TxnGrouper::UPtrTxnGroup>& groups,
                           const task::CCancellationToken& token,
                           ScriptChecker& control,
                           CBlockIndex* pindex,
                           CFrozenTXOCheck& frozenTXOCheck,
                           CBlockUndo& blockundo,
                           std::vector<CValidationState>& states,
                           std::vector<Amount>& fees,
                           std::atomic_size_t& nInputs,
                           std::atomic_uint64_t& nSigOpsCount,
                           const int nLockTimeFlags,
                           const uint32_t flags,
                           const bool isGenesisEnabled,
                           const bool fScriptChecks,
                           const uint64_t maxTxSigOpsCountConsensusBeforeGenesis,
                           const uint64_t nMaxSigOpsCountConsensusBeforeGenesis)
    {
        const TxnGrouper::UPtrTxnGroup& group { groups[shardNum] };
        CValidationState& state { states[shardNum] };
        Amount& nFees { fees[shardNum] };

        // If this group of txns is significantly larger than the smallest other group,
        // it probably makes sense to use parallel script validation here.
        // "significantly larger" is (by experiment) set to 8 times.
        constexpr unsigned smallestGroupMultiplier {8};
        const auto& smallestGroup { std::min_element(groups.begin(), groups.end(),
            [](const auto& g1, const auto& g2)
            {
                return g1->size() < g2->size();
            }
        ) };
        size_t smallestGroupSize { (*smallestGroup)->size() };
        bool parallelScriptChecks { groups.size() == 1 || group->size() >= (smallestGroupMultiplier * smallestGroupSize) };

        for(const auto& txnAndIndex : *group)
        {
            const CTransaction& tx { *txnAndIndex.mTxn };

            CScopedInvalidTxSenderBlock dumper(
                g_connman ? (&g_connman->getInvalidTxnPublisher()) : nullptr,
                txnAndIndex.mTxn, pindex, state);

            nInputs += tx.vin.size();

            if (!tx.IsCoinBase()) {
                if (!shard.HaveInputs(tx)) {
                    return state.DoS(
                        100, error("ConnectBlock(): inputs missing/spent"),
                        REJECT_INVALID, "bad-txns-inputs-missingorspent");
                }

                // Check that transaction is BIP68 final BIP68 lock checks (as
                // opposed to nLockTime checks) must be in ConnectBlock because they
                // require the UTXO set.
                std::vector<int32_t> prevheights(tx.vin.size());
                for (size_t j = 0; j < tx.vin.size(); j++) {
                    prevheights[j] = shard.GetCoin(tx.vin[j].prevout)->GetHeight();
                }

                if (!SequenceLocks(tx, nLockTimeFlags, &prevheights, *pindex)) {
                    return state.DoS(
                        100, error("ConnectBlock(): contains a non-BIP68-final transaction"),
                        REJECT_INVALID, "bad-txns-nonfinal");
                }
            }

            // After Genesis we don't count sigops when connecting blocks
            if (!isGenesisEnabled) {
                // GetTransactionSigOpCount counts 2 types of sigops:
                // * legacy (always)
                // * p2sh (when P2SH enabled)
                bool sigOpCountError;
                uint64_t txSigOpsCount = GetTransactionSigOpCount(config, tx, shard, flags & SCRIPT_VERIFY_P2SH, false, sigOpCountError);
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
                Amount fee = shard.GetValueIn(tx) - tx.GetValueOut();
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
                        shard,
                        fScriptChecks,
                        flags,
                        fCacheResults,
                        fCacheResults,
                        PrecomputedTransactionData(tx),
                        frozenTXOCheck,
                        &vChecks);
                if (!res.has_value())
                {
                    // With current implementation this can never happen as providing vChecks
                    // as parameter skips the path that checks the cancellation token
                    throw CBlockValidationCancellation{};
                }
                else if (!res.value())
                {
                    if (state.GetRejectCode() == REJECT_SOFT_CONSENSUS_FREEZE)
                    {
                        softConsensusFreeze(
                            *pindex,
                            config.GetSoftConsensusFreezeDuration() );
                    }

                    return error("ConnectBlock(): CheckInputs on %s failed with %s",
                                 tx.GetId().ToString(), FormatStateMessage(state));
                }

                if(fScriptChecks)
                {
                    if(parallelScriptChecks)
                    {
                        control.Add(vChecks);
                    }
                    else
                    {
                        for(auto& check : vChecks)
                        {
                            if(auto scriptRes = check(token); !scriptRes.has_value())
                            {
                                throw CBlockValidationCancellation {};
                            }
                            else if(!scriptRes.value())
                            {
                                return state.DoS(100, false, REJECT_INVALID,
                                   strprintf("blk-bad-inputs (%s)", ScriptErrorString(check.GetScriptError())));
                            }
                        }
                    }
                }
            }

            if(tx.IsCoinBase())
            {
                UpdateCoins(tx, shard, pindex->GetHeight());
            }
            else
            {
                UpdateCoins(tx, shard, blockundo.vtxundo[txnAndIndex.mIndex - 1], pindex->GetHeight());
            }
        }

        return true;
    }

    bool checkScripts(
        const task::CCancellationToken& token,
        int64_t nTime2,
        std::atomic_size_t& nInputs,
        CBlockUndo& blockundo,
        std::int32_t mostWorkBlockHeight )
    {
        blockundo.vtxundo.reserve(block.vtx.size() - 1);

        const Consensus::Params& consensusParams =
            config.GetChainParams().GetConsensus();
        bool isGenesisEnabled = IsGenesisEnabled(config, pindex->GetHeight());

        // Start enforcing BIP68 (sequence locks).
        int nLockTimeFlags = 0;
        if (pindex->GetHeight() >= consensusParams.CSVHeight) {
            nLockTimeFlags |= StandardNonFinalVerifyFlags(IsGenesisEnabled(config, pindex->GetHeight()));
        }

        uint64_t maxTxSigOpsCountConsensusBeforeGenesis = config.GetMaxTxSigOpsCountConsensusBeforeGenesis();
        std::vector<int32_t> prevheights;
        const uint32_t flags = GetBlockScriptFlags(config, pindex->GetPrev());
        std::atomic_uint64_t nSigOpsCount = 0;
        Amount nFees(0);

        // Sigops counting. We need to do it again because of P2SH.
        const uint64_t currentBlockSize =
            ::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION);
        // Sigops are not counted after Genesis anymore
        const uint64_t nMaxSigOpsCountConsensusBeforeGenesis = config.GetMaxBlockSigOpsConsensusBeforeGenesis(currentBlockSize);

        bool fScriptChecks = true;
        if (!hashAssumeValid.IsNull()) {
            // We've been configured with the hash of a block which has been
            // externally verified to have a valid history. A suitable default value
            // is included with the software and updated from time to time. Because
            // validity relative to a piece of software is an objective fact these
            // defaults can be easily reviewed. This setting doesn't force the
            // selection of any particular chain but makes validating some faster by
            // effectively caching the result of part of the verification.
            if (auto index = mapBlockIndex.Get(hashAssumeValid); index)
            {
                const auto& bestHeader = mapBlockIndex.GetBestHeader();
                if (index->GetAncestor(pindex->GetHeight()) == pindex &&
                    bestHeader.GetAncestor(pindex->GetHeight()) == pindex &&
                    bestHeader.GetChainWork() >= nMinimumChainWork)
                {
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
                             bestHeader, *pindex, bestHeader,
                             consensusParams) <= 60 * 60 * 24 * 7 * 2);
                }
            }
        }

        // Token for use during functional testing
        std::optional<task::CCancellationToken> checkPoolToken;

        // CCheckQueueScopeGuard that does nothing and does not belong to any pool.
        using NullScriptChecker = ScriptChecker;
        ScriptChecker control =
            fScriptChecks
            ? scriptCheckQueuePool->GetChecker(mostWorkOnChain, token, &checkPoolToken)
            : NullScriptChecker{};

        CFrozenTXOCheck frozenTXOCheck{ *pindex };
        if( config.GetEnableAssumeWhitelistedBlockDepth() )
        {
            if( (mostWorkBlockHeight - pindex->GetHeight()) >= config.GetAssumeWhitelistedBlockDepth() )
            {
                // This block is deep enough under the block with most work to assume that a confiscation transaction is whitelisted
                // even if its TxId is not present in our frozen TXO database.
                // Note that block with most work is only available after its contents have already been downloaded.
                // Consequently this check may not work during IBD where descendant blocks have not been downloaded so that
                // block with most work is the same as block currently being validated.
                // Checking against most work block, however, does provide a guarantee that this block extends the chain towards
                // the block with most work, which means that (small) reorgs are handled properly.
                frozenTXOCheck.DisableEnforcingConfiscationTransactionChecks();
            }
            else if( const auto& bestHeader = mapBlockIndex.GetBestHeader();
                     (bestHeader.GetHeight() - pindex->GetHeight()) >= config.GetAssumeWhitelistedBlockDepth() &&
                     bestHeader.GetAncestor(pindex->GetHeight()) == pindex )
            {
                // This block is deep enough under the block with best known header to assume that a confiscation transaction is whitelisted
                // even if its TxId is not present in our frozen TXO database.
                // Best known header is always available, but this block may not necessarily extend the chain towards it (e.g. in
                // case of soft consensus freeze). Therefore the ancestor check also needs to be performed here.
                // But checking depth against the best known header will work properly during IBD, which is the primary use case for
                // configuration option -assumewhitelistedblockdepth.
                frozenTXOCheck.DisableEnforcingConfiscationTransactionChecks();
            }
            // NOTE: It is also possible to have a large reorg towards the block whose header is not the best and also not have block
            //       with most work available during reorg. In this case confiscation transactions in blocks on new chain will not
            //       not be assumed whitelisted (even if block is deep enough) because here we do not yet know how deep the block is
            //       in the new chain.
            //       Such cases are rare and are assumed to be handled manually by the node operator in the same way as if a new block
            //       with non-whitelisted confiscation transaction is mined.
        }

        // Setup for parallel txn validation if required
        size_t maxThreads { parallelTxnValidation? static_cast<size_t>(config.GetPerBlockTxnValidatorThreadsCount()) : 1UL };
        uint64_t batchSize { config.GetBlockValidationTxBatchSize() };
        size_t numThreads { block.vtx.size() / batchSize };
        numThreads = std::clamp(numThreads, size_t(1), maxThreads);

        int64_t startGroupTime { GetTimeMicros() };
        TxnGrouper grouper {};
        const std::vector<TxnGrouper::UPtrTxnGroup> txnGroups { grouper.GetNumGroups(block.vtx, numThreads, batchSize) };
        int64_t groupTime { GetTimeMicros() - startGroupTime };
        size_t numGroups { txnGroups.size() };
        std::stringstream groupSizesStr {};
        for(size_t i = 0; i < numGroups; ++i)
        {
            if(i > 0)
            {
                groupSizesStr << ",";
            }
            groupSizesStr << txnGroups[i]->size();
        }
        LogPrint(BCLog::BENCH, "        - Group %ld transactions into %d groups of sizes [%s]: %.2fms\n",
            block.vtx.size(), numGroups, groupSizesStr.str(), 0.001 * groupTime);

        std::vector<CValidationState> states(numGroups);
        std::vector<Amount> allFees(numGroups);

        // Make space for all but the coinbase
        blockundo.vtxundo.resize(block.vtx.size() - 1);

        // Cache all inputs
        int64_t startCacheTime { GetTimeMicros() };
        view.CacheInputs(block.vtx);
        int64_t cacheTime { GetTimeMicros() - startCacheTime };
        LogPrint(BCLog::BENCH, "        - Cache: %.2fms\n", 0.001 * cacheTime);

        // Validate
        int64_t validateStartTime { GetTimeMicros() };
        std::vector<bool> results { view.RunSharded(numGroups,
            std::bind(&BlockConnector::BlockValidateTxns, this, std::placeholders::_1, std::placeholders::_2,
                std::cref(txnGroups),
                std::cref(token),
                std::ref(control),
                pindex,
                std::ref(frozenTXOCheck),
                std::ref(blockundo),
                std::ref(states),
                std::ref(allFees),
                std::ref(nInputs),
                std::ref(nSigOpsCount),
                nLockTimeFlags,
                flags,
                isGenesisEnabled,
                fScriptChecks,
                maxTxSigOpsCountConsensusBeforeGenesis,
                nMaxSigOpsCountConsensusBeforeGenesis
            )
        )};

        // Check results
        for(const auto& s : states)
        {
            if(!s.IsValid())
            {
                // Just return details of first failure
                state = s;
                return false;
            }
        }

        // Total up fees
        nFees = std::accumulate(allFees.begin(), allFees.end(), Amount{0});

        int64_t validateTime { GetTimeMicros() - validateStartTime };
        LogPrint(BCLog::BENCH, "        - Validate %ld transactions: %.2fms\n", block.vtx.size(), 0.001 * validateTime);

        if (parallelBlockValidation)
        {
            view.ForceDetach();
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
            nFees + GetBlockSubsidy(pindex->GetHeight(), consensusParams);
        if (block.vtx[0]->GetValueOut() > blockReward) {
            auto result = state.DoS(100, error("ConnectBlock(): coinbase pays too much "
                                               "(actual=%d vs limit=%d)",
                                               block.vtx[0]->GetValueOut(), blockReward),
                                    REJECT_INVALID, "bad-cb-amount");
            if(!state.IsValid() && g_connman)
            {
                g_connman->getInvalidTxnPublisher().Publish( { block.vtx[0], pindex, state } );
            }
            return result;
        }

        if(checkPoolToken)
        {
            // We only wait during tests and even then only if validation would
            // be performed.
            blockValidationStatus.waitIfRequired(
                pindex->GetBlockHash(),
                task::CCancellationToken::JoinToken(checkPoolToken.value(), token));
        }

        std::vector<CScriptCheck> failedChecks;
        auto controlValidationStatusOK = control.Wait(&failedChecks);

        if (!controlValidationStatusOK.has_value())
        {
            // validation was terminated before it was able to complete so we should
            // skip validity setting to SCRIPTS
            throw CBlockValidationCancellation{};
        }

        if (!controlValidationStatusOK.value())
        {
            for(const auto& check : failedChecks)
            {
                auto it = std::find_if(block.vtx.begin(), block.vtx.end(),
                                [&check](const CTransactionRef& tx)
                                {
                                    return tx.get() == check.GetTransaction();
                                });
                if (it == block.vtx.end())
                {
                    continue;
                }

                CValidationState state;
                state.Invalid( false,
                               REJECT_INVALID,
                               strprintf("blk-bad-inputs (%s)",
                                         ScriptErrorString(check.GetScriptError())));
                if(g_connman)
                {
                    g_connman->getInvalidTxnPublisher().Publish( { *it, pindex, state } );
                }
            }

            return state.DoS(100, false, REJECT_INVALID, "blk-bad-inputs",
                             "parallel script check failed");
        }

        return true;
    }

    void softConsensusFreeze( CBlockIndex& index, std::int32_t duration )
    {
        assert( duration>=0 );

        LogPrintf(
            "Soft consensus freezing block %s for %d blocks.\n",
            index.GetBlockHash().ToString(),
            duration );

        index.SetSoftConsensusFreezeFor( duration, mapBlockIndex );

        BlockIndexWithDescendants blocks{
            &index,
            mapBlockIndex,
            (std::numeric_limits<std::int32_t>::max() - duration > index.GetHeight()) ? index.GetHeight() + duration : duration };

        for(auto* item=blocks.Root()->Next(); item!=nullptr; item=item->Next())
        {
            item->BlockIndex()->UpdateSoftConsensusFreezeFromParent();
        }
    }

    const Config& config;
    const CBlock& block;
    CValidationState& state;
    CBlockIndex* pindex;
    CCoinsViewCache& view;
    std::int32_t mostWorkBlockHeight;
    const arith_uint256& mostWorkOnChain;
    bool fJustCheck;
    bool parallelBlockValidation;
    bool parallelTxnValidation;
};

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
    bool parallelTxnValidation,
    const Config &config,
    const CBlock &block,
    CValidationState &state,
    CBlockIndex *pindex,
    CCoinsViewCache &view,
    std::int32_t mostWorkBlockHeight,
    const arith_uint256& mostWorkOnChain,
    bool fJustCheck = false)
{
    BlockConnector connector{
        parallelBlockValidation,
        parallelTxnValidation,
        config,
        block,
        state,
        pindex,
        view,
        mostWorkBlockHeight,
        mostWorkOnChain,
        fJustCheck };

    return connector.Connect( token );
}


/**
 * Update the on-disk chain state.
 */
bool FlushStateToDisk(
    const CChainParams &chainparams,
    CValidationState &state,
    FlushStateMode mode,
    int32_t nManualPruneHeight) {

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
                    pBlockFileInfoStore->FindFilesToPruneManual(GlobalConfig::GetConfig(), setFilesToPrune, nManualPruneHeight);
                } else {
                    pBlockFileInfoStore->FindFilesToPrune(GlobalConfig::GetConfig(), setFilesToPrune,
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
            int64_t nMempoolSizeMax = GlobalConfig::GetConfig().GetMaxMempool();
            int64_t cacheSize = pcoinsTip->DynamicMemoryUsage();
            int64_t nTotalSpace =
                nCoinCacheUsage +
                std::max<int64_t>(nMempoolSizeMax - nMempoolUsage, 0);
            // The cache is large and we're within 10% and 10 MiB of the limit,
            // but we have time now (not in the middle of a block processing).
            bool fCacheLarge =
                mode == FLUSH_STATE_PERIODIC &&
                cacheSize > std::max((9 * nTotalSpace) / 10,
                                     nTotalSpace - MAX_BLOCK_COINSDB_USAGE * static_cast<int64_t>(ONE_MEBIBYTE));
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

                // Finally remove any pruned files
                //
                // NOTE: This must happen before dirty block info write to disk
                // below (pblocktree->WriteBatchSync)
                if (fFlushForPrune) UnlinkPrunedFiles(setFilesToPrune);

                // Then update all block file information (which may refer to
                // block and undo files).
                {
                    
                    std::vector<std::pair<int, const CBlockFileInfo *>> vFiles = pBlockFileInfoStore->GetAndClearDirtyFileInfo();
                    auto vBlocks = mapBlockIndex.ExtractDirtyBlockIndices();
                    if (!pblocktree->WriteBatchSync(vFiles, pBlockFileInfoStore->GetnLastBlockFile(),
                                                    vBlocks)) {
                        return AbortNode(
                            state, "Failed to write to block index database");
                    }
                }
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
                //
                // FIXME: this value is imprecise as it expects default size of
                //        scripts (so smaller than needed) while using script
                //        size would require too much space as most scripts are
                //        compressed. In future we will store compressed size
                //        so this code should be changed then.
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
static void UpdateTip(const Config &config, CBlockIndex *pindexNew)
{
    chainActive.SetTip(pindexNew);

    // New best block
    mempool.AddTransactionsUpdated(1);

    cvBlockChange.notify_all();

    LogPrintf("%s: new best=%s height=%d version=0x%08x log2_work=%.8g tx=%lu "
              "date='%s' progress=%f cache=%.1fMiB(%utxo)\n",
              __func__, chainActive.Tip()->GetBlockHash().ToString(),
              chainActive.Height(), chainActive.Tip()->GetVersion(),
              log(chainActive.Tip()->GetChainWork().getdouble()) / log(2.0),
              (unsigned long)chainActive.Tip()->GetChainTx(),
              DateTimeStrFormat("%Y-%m-%d %H:%M:%S",
                                chainActive.Tip()->GetBlockTime()),
              GuessVerificationProgress(config.GetChainParams().TxData(),
                                        chainActive.Tip()),
              pcoinsTip->DynamicMemoryUsage() * (1.0 / (1 << 20)),
              pcoinsTip->GetCacheSize());
}

static void FinalizeGenesisCrossing(const Config &config, int32_t height, const CJournalChangeSetPtr& changeSet)
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
 * should make the mempool consistent again by calling mempool.AddToMempoolForReorg.
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
    int32_t blockHeight { pindexDelete->GetHeight() };

    FinalizeGenesisCrossing(config, blockHeight, changeSet);

    // Read block from disk.
    std::shared_ptr<CBlock> pblock = std::make_shared<CBlock>();
    CBlock &block = *pblock;
    if (!pindexDelete->ReadBlockFromDisk(block, config)) {
        return AbortNode(state, "Failed to read block");
    }

    // Apply the block atomically to the chain state.
    int64_t nStart = GetTimeMicros();
    {
        CoinsDBSpan pCoinsTipSpan{ *pcoinsTip };
        assert(pCoinsTipSpan.GetBestBlock() == pindexDelete->GetBlockHash());
        if (ProcessingBlockIndex(*pindexDelete).DisconnectBlock(block, pCoinsTipSpan, task::CCancellationSource::Make()->GetToken()) != DISCONNECT_OK) {
            return error("DisconnectTip(): DisconnectBlock %s failed",
                         pindexDelete->GetBlockHash().ToString());
        }

        // NOTE:
        // TryFlush() will never fail as cs_main is used to synchronize
        // the different threads that Flush() or TryFlush() data. If cs_main
        // guarantee is removed we must decide what to do in this case.
        auto flushed = pCoinsTipSpan.TryFlush();
        assert(flushed == CoinsDBSpan::WriteState::ok);
    }

    LogPrint(BCLog::BENCH, "- Disconnect block: %.2fms, hash=%s, height=%d\n",
             (GetTimeMicros() - nStart) * 0.001, pindexDelete->GetBlockHash().ToString(), pindexDelete->GetHeight());

    // Write the chain state to disk, if necessary.
    if (!FlushStateToDisk(config.GetChainParams(), state,
                          FLUSH_STATE_IF_NEEDED)) {
        return false;
    }

    if (disconnectpool) {
        //  The amount of transactions we are willing to store during reorg is the same as max mempool size
        uint64_t maxDisconnectedTxPoolSize = config.GetMaxMempool();
        // Save transactions to re-add to mempool at end of reorg
        mempool.AddToDisconnectPoolUpToLimit(changeSet, disconnectpool, maxDisconnectedTxPoolSize, block, blockHeight);
    }

    // Update chainActive and related variables.
    UpdateTip(config, pindexDelete->GetPrev());

    // Update miner ID database if required
    if(g_minerIDs) {
        g_minerIDs->BlockRemoved(block);
    }

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
static int64_t nTimeRemoveFromMempool = 0;
static int64_t nTimeMinerId = 0;

struct PerBlockConnectTrace {
    const CBlockIndex *pindex = nullptr;
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
    boost::signals2::scoped_connection slotConnection {};
    bool mTracingPoolEntryRemovedEvents = false;

    void ConnectToPoolEntryRemovedEvent()
    {
        using namespace boost::placeholders;
        mTracingPoolEntryRemovedEvents = true;
        slotConnection = pool.NotifyEntryRemoved.connect(boost::bind(&ConnectTrace::NotifyEntryRemoved, this, _1, _2));
    }

    void DisconnectFromPoolEntryRemovedEvent()
    {
        mTracingPoolEntryRemovedEvents = false;
        slotConnection.disconnect();
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

    void BlockConnected(const CBlockIndex *pindex,
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

    void NotifyEntryRemoved(const CTransactionWrapper& txRemoved,
                            MemPoolRemovalReason reason) {
        assert(!blocksConnected.back().pindex);
        if (reason == MemPoolRemovalReason::CONFLICT) {
            blocksConnected.back().conflictedTxs->emplace_back(txRemoved.GetTx());
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
    std::int32_t mostWorkBlockHeight,
    const arith_uint256& mostWorkOnChain)
{
    auto guard =
        blockValidationStatus.getScopedCurrentlyValidatingBlock( *pindexNew );

    assert(pindexNew->GetPrev() == chainActive.Tip());
    // Read block from disk.
    int64_t nTime1 = GetTimeMicros();
    std::shared_ptr<const CBlock> pthisBlock;
    if (!pblock) {
        std::shared_ptr<CBlock> pblockNew = std::make_shared<CBlock>();
        if (!pindexNew->ReadBlockFromDisk(*pblockNew, config)) {
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
        CoinsDBSpan pCoinsTipSpan{ *pcoinsTip };

        // Temporarily stop tracing events if we are in parallel validation as
        // we will possibly release cs_main lock for a while. In case of an
        // exception we don't need to re-enable it since we won't be using the
        // result
        connectTrace.TracePoolEntryRemovedEvents(!parallelBlockValidation);

        bool rv =
            ConnectBlock(
                token,
                parallelBlockValidation,
                true,
                config,
                blockConnecting,
                state,
                pindexNew,
                pCoinsTipSpan,
                mostWorkBlockHeight,
                mostWorkOnChain);

        // re-enable tracing of events if it was disabled
        connectTrace.TracePoolEntryRemovedEvents(true);

        GetMainSignals().BlockChecked(blockConnecting, state);
        if(!rv)
        {
            if(state.IsInvalid())
            {
                InvalidBlockFound(config, pindexNew, blockConnecting, state);
            }
            return error("ConnectTip(): ConnectBlock %s failed (%s)",
                         pindexNew->GetBlockHash().ToString(),
                         FormatStateMessage(state));
        }
        else {
            // Update miner ID database if required
            if(g_minerIDs) {
                int64_t nMinerIdStart = GetTimeMicros();
                g_minerIDs->BlockAdded(blockConnecting, pindexNew);
                int64_t nThisMinerIdTime = GetTimeMicros() - nMinerIdStart;
                nTimeMinerId += nThisMinerIdTime;
                LogPrint(BCLog::BENCH, "    - MinerID total: %.2fms [%.2fs]\n",
                    nThisMinerIdTime * 0.001, nTimeMinerId * 0.000001);
            }
        }
        nTime3 = GetTimeMicros();
        nTimeConnectTotal += nTime3 - nTime2;
        LogPrint(BCLog::BENCH, "  - Connect total: %.2fms [%.2fs]\n",
                 (nTime3 - nTime2) * 0.001, nTimeConnectTotal * 0.000001);

        // NOTE:
        // TryFlush() will never fail as cs_main is used to synchronize
        // the different threads that Flush() or TryFlush() data. If cs_main
        // guarantee is removed we must decide what to do in this case.
        auto flushed = pCoinsTipSpan.TryFlush();
        assert(flushed == CoinsDBSpan::WriteState::ok);
    }
    std::vector<CTransactionRef> txNew;
    auto asyncRemoveForBlock = std::async(std::launch::async,
        [&blockConnecting, &changeSet, &txNew, &config]()
        {
            RenameThread("Async RemoveForBlock");
            int64_t nTimeRemoveForBlock = GetTimeMicros();
            // Remove transactions from the mempool.;
            mempool.RemoveForBlock(blockConnecting.vtx, changeSet, blockConnecting.GetHash(), txNew, config);
            nTimeRemoveForBlock = GetTimeMicros() - nTimeRemoveForBlock;
            nTimeRemoveFromMempool += nTimeRemoveForBlock;
            LogPrint(BCLog::BENCH, "    - Remove transactions from the mempool: %.2fms [%.2fs]\n",
                    nTimeRemoveForBlock * 0.001, nTimeRemoveFromMempool * 0.000001);
        }
    );

    int64_t nTime4 = GetTimeMicros();
    nTimeFlush += nTime4 - nTime3;
    LogPrint(BCLog::BENCH, "  - Flush: %.2fms [%.2fs]\n",
             (nTime4 - nTime3) * 0.001, nTimeFlush * 0.000001);
    // Write the chain state to disk, if necessary.
    if (!FlushStateToDisk(config.GetChainParams(), state,
                          FLUSH_STATE_IF_NEEDED)) {
        asyncRemoveForBlock.wait();
        return false;
    }
    int64_t nTime5 = GetTimeMicros();
    nTimeChainState += nTime5 - nTime4;
    LogPrint(BCLog::BENCH, "  - Writing chainstate: %.2fms [%.2fs]\n",
             (nTime5 - nTime4) * 0.001, nTimeChainState * 0.000001);
    
    if (g_connman)
    {
        g_connman->DequeueTransactions(blockConnecting.vtx);
    }
    disconnectpool.removeForBlock(blockConnecting.vtx);

    asyncRemoveForBlock.wait();
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
    GetMainSignals().BlockConnected2(pindexNew, txNew);

    FinalizeGenesisCrossing(config, pindexNew->GetHeight(), changeSet);

    return true;
}

/**
 * Return the tip of the chain with the most work in it, that isn't known to be
 * invalid (it's however far from certain to be valid).
 */
static CBlockIndex *FindMostWorkChain(const Config &config) {
    do {
        CBlockIndex *pindexNew = nullptr;

        // Find the best candidate header.
        {
            std::set<CBlockIndex *, CBlockIndexWorkComparator>::reverse_iterator
                it = setBlockIndexCandidates.rbegin();
            do {
                if (it == setBlockIndexCandidates.rend()) {
                    return nullptr;
                }

                if ((*it)->IsSoftRejected() ||
                    (*it)->IsInSoftConsensusFreeze())
                {
                    ++it;
                    continue;
                }

                // We found a valid candidate header
                break;
            } while(true);

            pindexNew = *it;
        }

        // Check whether all blocks on the path between the currently active
        // chain and the candidate are valid. Just going until the active chain
        // is an optimization, as we know all blocks in it are valid already.
        CBlockIndex *pindexTest = pindexNew;
        bool fInvalidAncestor = false;
        while (pindexTest && !chainActive.Contains(pindexTest)) {
            assert(pindexTest->GetChainTx() || pindexTest->GetHeight() == 0);

            // Pruned nodes may have entries in setBlockIndexCandidates for
            // which block files have been deleted. Remove those as candidates
            // for the most work chain if we come across them; we can't switch
            // to a chain unless we have all the non-active-chain parent blocks.
            BlockStatus testStatus = pindexTest->getStatus();
            bool fInvalidChain = testStatus.isInvalid();
            bool fMissingData = !testStatus.hasData();
            if (fInvalidChain || fMissingData) 
            {
                if (fInvalidChain)
                {
                    // Candidate chain is not usable (either invalid or missing
                    // data)
                    if ((pindexBestInvalid == nullptr ||
                        pindexNew->GetChainWork() > pindexBestInvalid->GetChainWork())) {
                        pindexBestInvalid = pindexNew;
                    }
                    // Invalidate chain
                    InvalidateChain(config, pindexTest);
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
                            std::make_pair(pindexFailed->GetPrev(), pindexFailed));
                        setBlockIndexCandidates.erase(pindexFailed);
                        pindexFailed = pindexFailed->GetPrev();
                    }
                }
                setBlockIndexCandidates.erase(pindexTest);
                fInvalidAncestor = true;
                break;
            }
            pindexTest = pindexTest->GetPrev();
        }
        if (!fInvalidAncestor) {
            return pindexNew;
        }
    } while (true);
}

/** Delete all entries in setBlockIndexCandidates that are worse than the
 * current tip. */
static void PruneBlockIndexCandidates() {
    if (chainActive.Tip()->IsInSoftConsensusFreeze())
    {
        // Wait with the cleaning until the tip is back in the "guaranteed
        // not soft rejected" zone and we no longer expect that reorg will
        // fall back on a block that should not be at the tip.
        return;
    }

    // Note that we can't delete the current block itself, as we may need to
    // return to it later in case a reorganization to a better block fails.
    std::set<CBlockIndex *, CBlockIndexWorkComparator>::iterator it =
        setBlockIndexCandidates.begin();
    while (it != setBlockIndexCandidates.end() &&
           setBlockIndexCandidates.value_comp()(*it, chainActive.Tip()) &&
           // Current tip is better only if it is not considered soft rejected,
           // or if the other block is also soft rejected.
           // Otherwise the block index candidate must not be deleted so that we can
           // return to it later (e.g. in case the block we're currently working
           // towards turns out to be invalid).
           (!chainActive.Tip()->IsSoftRejected() || (*it)->IsSoftRejected())) {
        setBlockIndexCandidates.erase(it++);
    }
    // Either the current tip or a successor of it we're working towards is left
    // in setBlockIndexCandidates.
    assert(!setBlockIndexCandidates.empty());
}

static std::optional<bool> RemoveSoftConsensusFreezeBlocksFromActiveChainTipNL(
    const Config& config,
    const CJournalChangeSetPtr& changeSet,
    CValidationState& state,
    DisconnectedBlockTransactions& disconnectpool )
{
    std::int32_t disconnectCounter = 0;
    const CBlockIndex* walkIndex = chainActive.Tip();
    while ( walkIndex->IsInSoftConsensusFreeze() )
    {
        if (!DisconnectTip(config, state, &disconnectpool, changeSet)) {
            // This is likely a fatal error, but keep the mempool consistent,
            // just in case. Only remove from the mempool in this case.
            mempool.RemoveFromMempoolForReorg(config, disconnectpool, changeSet);

            return {};
        }

        ++disconnectCounter;
        walkIndex = chainActive.Tip();
    }

    LogPrintf(
        "Disconnected %d consensus frozen blocks back to tip %s.\n",
        disconnectCounter,
        chainActive.Tip()->GetBlockHash().ToString() );

    return disconnectCounter;
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

    try {

        class RAIIUpdateMempool
        {
            const Config& config;
            const CJournalChangeSetPtr& changeSet;
            bool fBlocksDisconnected = false;
            bool fDisconnectFailed = false;
            DisconnectedBlockTransactions disconnectpool;
            CValidationState& state;
        public:
            RAIIUpdateMempool(
                const Config& c,
                const CJournalChangeSetPtr& cs,
                const CBlockIndex* pindexFork,
                CValidationState& st)
                :config{c}
                ,changeSet{cs}
                ,state{ st }
            {
                auto needTipDisconnect = [&]{ return chainActive.Tip() && chainActive.Tip() != pindexFork; };

                if (needTipDisconnect())
                {
                    LogPrintf(
                        "Performing best chain tip %s rollback to older fork point %s.\n",
                        chainActive.Tip()->GetBlockHash().ToString(),
                        pindexFork->GetBlockHash().ToString() );

                    try
                    {
                        // we are diconnecting until we reach the fork point
                        do
                        {
                            if (!DisconnectTip(config, state, &disconnectpool, changeSet)) {
                                // This is likely a fatal error.
                                fDisconnectFailed = true;
                                return;
                            }
                            fBlocksDisconnected = true;
                        } while (needTipDisconnect());
                    }
                    catch( ... )
                    {
                        // handle exceptions in constructor as incompletely
                        // constructed instance will not call the destructor
                        UpdateIfNeeded();
                        throw;
                    }
                }
            }

            ~RAIIUpdateMempool()
            {
                if (std::uncaught_exceptions())
                {
                    RemoveSoftConsensusFreezeBlocksFromActiveChainTipNL(
                        config,
                        changeSet,
                        state,
                        disconnectpool );
                }

                UpdateIfNeeded();
            }
        
            void UpdateIfNeeded()
            {
                if(fBlocksDisconnected) {
                    changeSet->updateForReorg();
                    mempool.AddToMempoolForReorg(config, disconnectpool, changeSet);
                    fBlocksDisconnected = false;
                }
            }

            void MarkBlocksDisconnected() { fBlocksDisconnected = true; }

            DisconnectedBlockTransactions& GetDisconnectpool() {return disconnectpool;}
        
            bool DisconnectFailed() { return fDisconnectFailed;}
        };
        
        RAIIUpdateMempool reorgUpdate {config, changeSet, pindexFork, state};

        if (reorgUpdate.DisconnectFailed()) {
            return false;
        }

        // Build list of new blocks to connect.
        std::vector<CBlockIndex *> vpindexToConnect;
        bool fContinue = true;
        int32_t nHeight = pindexFork ? pindexFork->GetHeight() : -1;
        while (fContinue && nHeight != pindexMostWork->GetHeight()) {
            // Don't iterate the entire list of potential improvements toward the
            // best tip, as we likely only need a few blocks along the way.
            int32_t nTargetHeight = std::min(nHeight + 32, pindexMostWork->GetHeight());
            vpindexToConnect.clear();
            vpindexToConnect.reserve(static_cast<size_t>(nTargetHeight - nHeight));
            CBlockIndex *pindexIter = pindexMostWork->GetAncestor(nTargetHeight);
            while (pindexIter && pindexIter->GetHeight() != nHeight) {
                vpindexToConnect.push_back(pindexIter);
                pindexIter = pindexIter->GetPrev();
            }
            nHeight = nTargetHeight;

            // Connect new blocks.
            for (CBlockIndex *pindexConnect :
                 boost::adaptors::reverse(vpindexToConnect)) {

                /* We always want to get to the same nChainWork amount as
                we started with before enabling parallel validation as we
                don't want to end up in a situation where sibling blocks
                from older chain items are once again eligible for parallel
                validation thus wasting resources. We also don't wish to
                end up announcing older chain items as new best tip.*/                        
                bool parallelBlockValidation = pindexOldTip && chainActive.Tip()->GetChainWork() == pindexOldTip->GetChainWork();

                if(parallelBlockValidation)
                {
                    // During the next call to ConnectTip we will release the cs_main. (parallelBlockValidation flag)
                    // The mempool may not be consistent with current tip if we are in the reorg, 
                    // because a mempool transaction can have a parent which is currently in the disconnectpool.
                    // Here we are adding disconnected transactions to be in sync with current tip.
                    reorgUpdate.UpdateIfNeeded();
                }

                if (!ConnectTip(
                        parallelBlockValidation,
                        token,
                        config,
                        state,
                        pindexConnect,
                        pindexConnect == pindexMostWork
                            ? pblock
                            : std::shared_ptr<const CBlock>(),
                        connectTrace,
                        reorgUpdate.GetDisconnectpool(),
                        changeSet,
                        pindexMostWork->GetHeight(),
                        pindexMostWork->GetChainWork() ))
                {
                    auto result =
                        RemoveSoftConsensusFreezeBlocksFromActiveChainTipNL(
                            config,
                            changeSet,
                            state,
                            reorgUpdate.GetDisconnectpool() );
                    if (result.has_value())
                    {
                        if (result.value() == true)
                        {
                            reorgUpdate.MarkBlocksDisconnected();
                        }
                    }
                    else
                    {
                        return false;
                    }

                    if (state.IsInvalid()) {
                        // The block violates a consensus rule.
                        if (!state.CorruptionPossible()) {
                            InvalidChainFound(config, vpindexToConnect.back());
                        }
                        state = CValidationState();
                        fInvalidFound = true;
                        fContinue = false;
                        break;
                    } else {
                        // A system error occurred (disk space, database error, ...).
                        // The mempool will be updated with reorgUpdate if needed.
                        return false;
                    }
                } else {
                    PruneBlockIndexCandidates();
                    if (!pindexOldTip ||
                        chainActive.Tip()->GetChainWork() > pindexOldTip->GetChainWork())
                    {
                        // We're in a better position than we were. Return
                        // temporarily to release the lock.
                        fContinue = false;
                        break;
                    }
                }
            }
        }
        reorgUpdate.UpdateIfNeeded();

        // remove the minerid transactions that could not be mined by ourselves
        try {
            if (pindexOldTip) {
                // If this block was from someone else, then we have to remove our own
                // minerinfo transactions from the mempool
                std::vector<COutPoint> funds = g_MempoolDatarefTracker->funds();
                std::vector<TxId> datarefs;
                std::transform(
                        funds.cbegin(),
                        funds.cend(),
                        std::back_inserter(datarefs),
                        [](const COutPoint& p) {return p.GetTxId();});

                if (!datarefs.empty()) {

                    std::stringstream ss;
                    for (const auto& txid: datarefs)
                        ss << ' ' << txid.ToString();
                    LogPrint(BCLog::MINERID,
                             "minerinfotx tracker, remove minerinfo and dataref txns:%s\n", ss.str());

                    std::vector<TxId> toRemove = mempool.RemoveTxnsAndDescendants(datarefs, changeSet);
                    if (toRemove.size() != funds.size()) {
                        // if we mined them by error (calling bitcoin-cli generate for e.g.), then we have to
                        // store them as potential funds
                        for (const TxId& txid: toRemove) {
                            const auto it = std::find_if(
                                    funds.cbegin(),
                                    funds.cend(),
                                    [&txid](const COutPoint& p){return txid == p.GetTxId();});
                            if (it != funds.end())
                                funds.erase(it);
                        }
                        g_MempoolDatarefTracker->funds_replace(std::move(funds));
                        move_and_store(*g_MempoolDatarefTracker, *g_BlockDatarefTracker);
                    }
                    g_MempoolDatarefTracker->funds_clear();
                }
            }
        } catch(...) {
            LogPrintf("Exception caught while removing mineridinfo-tx from mempool\n");
        }

        // We will soon exit this function, lets update the mempool before we check it.
        mempool.CheckMempool(*pcoinsTip, changeSet);
        // If we made any changes lets apply them now.
        if(changeSet)
            changeSet->apply();

    } catch(...) {
        // We were probably cancelled.
        LogPrintf("Exception caught during ActivateBestChainStep;\n");
        throw;
    }
    return true;
}

static void NotifyHeaderTip() {
    bool fNotify = false;
    bool fInitialBlockDownload = false;
    static std::mutex pindexHeaderOldMutex;
    static const CBlockIndex *pindexHeaderOld = nullptr;
    const CBlockIndex& indexHeader = mapBlockIndex.GetBestHeader();

    if (std::lock_guard lock{ pindexHeaderOldMutex };
        &indexHeader != pindexHeaderOld)
    {
        fNotify = true;
        fInitialBlockDownload = IsInitialBlockDownload();
        pindexHeaderOld = &indexHeader;
    }

    // Send block tip changed notifications without cs_main
    if (fNotify) {
        uiInterface.NotifyHeaderTip(fInitialBlockDownload, &indexHeader);
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
       block.GetBlockHeader().hashPrevBlock != currentTip.GetBlockHash())
    {
        return &mostWork;
    }

    auto indexOfNewBlock = mapBlockIndex.Get(block.GetHash());

    // if block is missing from the mapBlockIndex then treat it as code bug
    // since every new block should be added to index before getting here
    assert(indexOfNewBlock);
    assert(indexOfNewBlock->GetPrev()->GetBlockHash() == block.GetBlockHeader().hashPrevBlock);

    if(mostWork.GetChainWork() > indexOfNewBlock->GetChainWork()
        || !indexOfNewBlock->IsValid(BlockValidity::TRANSACTIONS)
        || !indexOfNewBlock->GetChainTx()
        || indexOfNewBlock->IsInSoftConsensusFreeze())
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
                    pindexMostWork = FindMostWorkChain(config);

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
                        pindexMostWork->GetBlockHash().GetHex());

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
                        pindexMostWork->GetBlockHash().GetHex());

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
                    CheckSafeModeParameters(config, nullptr);
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

    {
        LOCK( cs_main ); // needed by safe_mode CheckSafeModeParameters (chainActive)
        // Write changes periodically to disk, after relay.
        if (!FlushStateToDisk(params, state, FLUSH_STATE_PERIODIC)) {
            CheckSafeModeParameters(config, nullptr);
            return false;
        }

        int32_t nStopAtHeight = config.GetStopAtHeight();
        if (nStopAtHeight && pindexNewTip &&
            pindexNewTip->GetHeight() >= nStopAtHeight) {
            StartShutdown();
        }

        CheckSafeModeParameters(config, pindexNewTip);
    }

    return true;
}

bool IsBlockABestChainTipCandidate(const CBlockIndex& index)
{
    AssertLockHeld(cs_main);

    return (setBlockIndexCandidates.find(const_cast<CBlockIndex*>(&index)) != setBlockIndexCandidates.end());
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
            pindex->GetChainWork() > chainActive.Tip()->GetChainWork())
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
        auto tipChainWork = chainActive.Tip()->GetChainWork();
        if (pindex->GetChainWork() < tipChainWork) {
            // Nothing to do, this block is not at the tip.
            return true;
        }
        if (tipChainWork > nLastPreciousChainwork) {
            // The chain has been extended since the last call, reset the
            // counter.
            nBlockReverseSequenceId = -1;
        }
        nLastPreciousChainwork = tipChainWork;
        setBlockIndexCandidates.erase(pindex);
        pindex->IgnoreValidationTime();
        pindex->SetSequenceId( nBlockReverseSequenceId );
        if (nBlockReverseSequenceId > std::numeric_limits<int32_t>::min()) {
            // We can't keep reducing the counter if somebody really wants to
            // call preciousblock 2**31-1 times on the same set of tips...
            nBlockReverseSequenceId--;
        }
        if (pindex->IsValid(BlockValidity::TRANSACTIONS) && pindex->GetChainTx())
        {
            setBlockIndexCandidates.insert(pindex);
            PruneBlockIndexCandidates();
        }
    }

    CJournalChangeSetPtr changeSet { mempool.getJournalBuilder().getNewChangeSet(JournalUpdateReason::REORG) };
    auto source = task::CCancellationSource::Make();
    // state is used to report errors, not block related invalidity
    // (see description of ActivateBestChain)
    return ActivateBestChain(task::CCancellationToken::JoinToken(source->GetToken(), GetShutdownToken()), config, state, changeSet);
}


namespace {

/**
 * Disconnect blocks from chain tip that are considered soft rejected
 *
 * @returns true: At least one block at tip was disconnected.
 *          false: Tip is not considered soft rejected and nothing was done.
 *          nullopt: There was an error when trying to disconnect the block at tip
 */
std::optional<bool> DisconnectSoftRejectedTipsNL(const Config& config, CValidationState& state, DisconnectedBlockTransactions& disconnectpool, CJournalChangeSetPtr& changeSet)
{
    AssertLockHeld(cs_main);

    bool tip_disconnected = false;
    while(chainActive.Tip()->IsSoftRejected())
    {
        const CBlockIndex* tip = chainActive.Tip();

        // ActivateBestChain considers blocks already in chainActive
        // unconditionally valid already, so force disconnect away from it.
        if (!DisconnectTip(config, state, &disconnectpool, changeSet))
        {
            // DisconnectTip has failed.
            return std::nullopt;
        }

        tip_disconnected = true;
        LogPrintf("Block %s was disconnected from active chain tip (height=%d) because it is considered soft rejected at this height and for the next %d block(s)\n",
                  tip->GetBlockHash().ToString(), tip->GetHeight(), tip->GetSoftRejectedFor());
    }

    return tip_disconnected;
}

} // anonymous namespace

bool InvalidateBlock(const Config &config, CValidationState &state,
                     CBlockIndex *pindex) {
    AssertLockHeld(cs_main);

    // Mark the block itself as invalid.
    pindex->ModifyStatusWithFailed(mapBlockIndex);
    setBlockIndexCandidates.erase(pindex);

    DisconnectedBlockTransactions disconnectpool;
    CJournalChangeSetPtr changeSet { mempool.getJournalBuilder().getNewChangeSet(JournalUpdateReason::REORG) };
    bool tip_disconnected = false;
    if (chainActive.Contains(pindex))
    {
        while (chainActive.Contains(pindex))
        {
            CBlockIndex* pindexWalk = chainActive.Tip();
            pindexWalk->ModifyStatusWithFailedParent(mapBlockIndex);
            setBlockIndexCandidates.erase(pindexWalk);
            // ActivateBestChain considers blocks already in chainActive
            // unconditionally valid already, so force disconnect away from it.
            if (!DisconnectTip(config, state, &disconnectpool, changeSet))
            {
                // It's probably hopeless to try to make the mempool consistent
                // here if DisconnectTip failed, but we can try.
                mempool.RemoveFromMempoolForReorg(config, disconnectpool, changeSet);
                return false;
            }
        }

        tip_disconnected = true;

        // Also disconnect any blocks from tip that may have become
        // soft rejected because the height is now lower
        if(!DisconnectSoftRejectedTipsNL(config, state, disconnectpool, changeSet).has_value())
        {
            // Disconnecting tip has failed.
            mempool.RemoveFromMempoolForReorg(config, disconnectpool, changeSet);
            return false;
        }

        auto result =
            RemoveSoftConsensusFreezeBlocksFromActiveChainTipNL(
                config,
                changeSet,
                state,
                disconnectpool );
        if (!result.has_value())
        {
            // Disconnecting tip has failed.
            mempool.RemoveFromMempoolForReorg(config, disconnectpool, changeSet);
            return false;
        }
    }
    else
    {
        // in case of invalidating block that is not on active chain make sure
        // that we mark all its descendants (whole chain) as invalid 
        InvalidateChain(config, pindex);
    }

    // DisconnectTip will add transactions to disconnectpool; try to add these
    // back to the mempool.
    mempool.AddToMempoolForReorg(config, disconnectpool, changeSet);

    // The resulting new best tip may not be in setBlockIndexCandidates anymore,
    // so add it again.
    mapBlockIndex.ForEachMutable(
        [&](CBlockIndex& index)
        {
            if (index.IsValid(BlockValidity::TRANSACTIONS) && index.GetChainTx() &&
                !setBlockIndexCandidates.value_comp()(&index, chainActive.Tip())) {
                setBlockIndexCandidates.insert(&index);
            }
        });

    InvalidChainFound(config, pindex);
    if(tip_disconnected)
    {
        uiInterface.NotifyBlockTip(IsInitialBlockDownload(), pindex->GetPrev());
    }

    // Make sure all on disk data is consistent after rewinding the tip
    if(! FlushStateToDisk(config.GetChainParams(), state, FLUSH_STATE_ALWAYS))
    {
        LogPrintf("Failed to flush to disk in InvalidateBlock\n");
        return false;
    }

    if (state.IsValid() && g_connman) {
        CScopedBlockOriginRegistry reg(pindex->GetBlockHash(), "invalidateblock");
        auto source = task::CCancellationSource::Make();
        // state is used to report errors, not block related invalidity
        // (see description of ActivateBestChain)
        ActivateBestChain(task::CCancellationToken::JoinToken(source->GetToken(), GetShutdownToken()), config, state, changeSet);
    }

    // Check mempool & journal
    mempool.CheckMempool(*pcoinsTip, changeSet);

    return true;
}

namespace {

/**
 * Set soft rejected status of root block in blockIndexWithDescendants and update affected descendants.
 *
 * Also marks all updated blocks as dirty.
 */
void SetRootSoftRejectedForNL(const BlockIndexWithDescendants& blockIndexWithDescendants, std::int32_t numBlocks)
{
    AssertLockHeld(cs_main);
    auto* item = blockIndexWithDescendants.Root();
    item->BlockIndex()->SetSoftRejectedFor(numBlocks, mapBlockIndex);

    item = item->Next();
    for(; item!=nullptr; item=item->Next())
    {
        // NOTE: tree is traversed depth first so that parents are always updated before children
        item->BlockIndex()->SetSoftRejectedFromParent(mapBlockIndex);
    }
}

} // anonymous namespace

bool SoftRejectBlockNL(const Config& config, CValidationState& state,
                       CBlockIndex* const pindex, std::int32_t numBlocks)
{
    assert(numBlocks>=0);
    AssertLockHeld(cs_main);

    if(pindex->GetHeight() == 0)
    {
        // It is logically incorrect to consider genesis block soft rejected.
        return error("SoftRejectBlockNL(): Genesis block %s cannot be soft rejected\n",
                     pindex->GetBlockHash().ToString());
    }

    if(pindex->IsSoftRejected())
    {
        // Soft rejection status can only be changed on blocks that were explicitly marked as soft rejected.
        if(pindex->ShouldBeConsideredSoftRejectedBecauseOfParent())
        {
            return error("SoftRejectBlockNL(): Block %s is already considered soft rejected because of its parent and cannot be marked independently\n",
                         pindex->GetBlockHash().ToString());

        }

        // Value of numBlocks can only be increased.
        // Consequently, length of active chain can only be decreased, which simplifies implementation.
        if(numBlocks <= pindex->GetSoftRejectedFor())
        {
            return error("SoftRejectBlockNL(): Block %s is currently marked as soft rejected for the next %d block(s) and this number can only be increased when rejecting\n",
                         pindex->GetBlockHash().ToString(), pindex->GetSoftRejectedFor());
        }
    }

    // Find all descendants of block on all chains up to height after which this block
    // is no longer considered soft rejected.
    const std::int32_t maxHeight = pindex->GetHeight() + numBlocks;
    const BlockIndexWithDescendants blocks{pindex, mapBlockIndex, maxHeight};

    // Check that setting this block as soft rejected will not affect subsequent
    // blocks that are already explicitly marked as soft rejected.
    for(auto* item = blocks.Root()->Next(); item!=nullptr; item=item->Next())
    {
        if(item->BlockIndex()->IsSoftRejected() && !item->BlockIndex()->ShouldBeConsideredSoftRejectedBecauseOfParent())
        {
            return error("SoftRejectBlockNL(): Block %s cannot be marked soft rejected for the next %d block(s) because this would affect descendant block %s that is also marked as soft rejected\n",
                         pindex->GetBlockHash().ToString(), pindex->GetSoftRejectedFor(), item->BlockIndex()->GetBlockHash().ToString());
        }
    }

    // Remember original soft rejection status of this block so that it can be restored if something goes wrong
    const std::int32_t old_SoftRejectedFor = pindex->GetSoftRejectedFor();

    // Set (or change) soft rejection status of this block and update affected descendants.
    SetRootSoftRejectedForNL(blocks, numBlocks);

    // Disconnect blocks from chain tip that are considered soft rejected
    DisconnectedBlockTransactions disconnectpool;
    CJournalChangeSetPtr changeSet { mempool.getJournalBuilder().getNewChangeSet(JournalUpdateReason::REORG) };
    auto tip_disconnected = DisconnectSoftRejectedTipsNL(config, state, disconnectpool, changeSet);
    if(!tip_disconnected.has_value())
    {
        // Disconnect tip has failed.
        // Restore soft rejection status of this block as it was
        SetRootSoftRejectedForNL(blocks, old_SoftRejectedFor);

        // It's probably hopeless to try to make the mempool consistent
        // here if DisconnectTip failed, but we can try.
        mempool.RemoveFromMempoolForReorg(config, disconnectpool, changeSet);

        return false;
    }

    if(*tip_disconnected)
    {
        // If tip was disconnected, we also need to do some housekeeping.
        // NOTE: We do basically the same thing as it is done in the function InvalidateBlock
        //       except marking the chain as invalid.

        // DisconnectTip will add transactions to disconnectpool; try to add these
        // back to the mempool.
        mempool.AddToMempoolForReorg(config, disconnectpool, changeSet);

        // The resulting new best tip may not be in setBlockIndexCandidates anymore,
        // so add it again. Since the best tip may be on a different chain, we need to
        // scan whole block index.
        mapBlockIndex.ForEachMutable(
        [&](CBlockIndex& index)
        {
            if (index.IsValid(BlockValidity::TRANSACTIONS) && index.GetChainTx() &&
                !setBlockIndexCandidates.value_comp()(&index, chainActive.Tip())) {
                setBlockIndexCandidates.insert(&index);
            }
        });


        uiInterface.NotifyBlockTip(IsInitialBlockDownload(), chainActive.Tip());

        if (state.IsValid() && g_connman) {
            CScopedBlockOriginRegistry reg(pindex->GetBlockHash(), "softrejectblock");
            auto source = task::CCancellationSource::Make();
            // state is used to report errors, not block related invalidity
            // (see description of ActivateBestChain)
            ActivateBestChain(task::CCancellationToken::JoinToken(source->GetToken(), GetShutdownToken()), config, state, changeSet);
        }

        // Check mempool & journal
        mempool.CheckMempool(*pcoinsTip, changeSet);
    }

    return true;
}

bool AcceptSoftRejectedBlockNL(CBlockIndex* pindex, std::int32_t numBlocks)
{
    assert(numBlocks>=-1);
    AssertLockHeld(cs_main);

    if(!pindex->IsSoftRejected())
    {
        return error("AcceptSoftRejectedBlockNL(): Block %s is not soft rejected\n",
                     pindex->GetBlockHash().ToString());
    }

    // Soft rejection status can only be changed on blocks that were explicitly marked as soft rejected.
    if(pindex->ShouldBeConsideredSoftRejectedBecauseOfParent())
    {
        return error("AcceptSoftRejectedBlockNL(): Block %s is soft rejected because of its parent and cannot be accepted independently\n",
                     pindex->GetBlockHash().ToString());
    }

    // Value of numBlocks can only be decreased.
    // Consequently, length of active chain can only be increased, which simplifies implementation.
    if(numBlocks >= pindex->GetSoftRejectedFor())
    {
        return error("AcceptSoftRejectedBlockNL(): Block %s is currently marked as soft rejected for the next %d block(s) and this number can only be decreased when accepting\n",
                     pindex->GetBlockHash().ToString(), pindex->GetSoftRejectedFor());
    }

    // Find all descendants of block on all chains up to height after which this block was no longer considered soft rejected.
    const std::int32_t maxHeight = pindex->GetHeight() + pindex->GetSoftRejectedFor();
    const BlockIndexWithDescendants blocks{pindex, mapBlockIndex, maxHeight};

    // Unset (or change) soft rejection status of this block and update affected descendants.
    SetRootSoftRejectedForNL(blocks, numBlocks);

    return true;
}

void InvalidateBlocksFromConfig(const Config &config)
{
    for ( const auto& invalidBlockHash: config.GetInvalidBlocks() )
    {
        CValidationState state;
        {
            auto pblockindex = mapBlockIndex.Get(invalidBlockHash);
            if (!pblockindex)
            {
                LogPrintf("Block %s that is marked as invalid is not found.\n", invalidBlockHash.GetHex());
                continue;
            }

            LOCK(cs_main);

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

    int32_t nHeight = pindex->GetHeight();

    // Remove the invalidity flag from this block and all its descendants.
    mapBlockIndex.ForEachMutable(
        [&](CBlockIndex& index)
        {
            if (!index.IsValid() &&
                index.GetAncestor(nHeight) == pindex) {
                index.ModifyStatusWithClearedFailedFlags(mapBlockIndex);
                if (index.IsValid(BlockValidity::TRANSACTIONS) &&
                    index.GetChainTx() &&
                    setBlockIndexCandidates.value_comp()(chainActive.Tip(),
                                                         &index)) {
                    setBlockIndexCandidates.insert(&index);
                }
                if (&index == pindexBestInvalid) {
                    // Reset invalid block marker if it was pointing to one of
                    // those.
                    pindexBestInvalid = nullptr;
                }
            }
        });

    // Remove the invalidity flag from all ancestors too.
    while (pindex != nullptr) {
        if (pindex->getStatus().isInvalid()) {
            pindex->ModifyStatusWithClearedFailedFlags(mapBlockIndex);
        }
        pindex = pindex->GetPrev();
    }
    return true;
}

static CBlockIndex *AddToBlockIndex(const Config& config, const CBlockHeader &block) {
    if (auto index = mapBlockIndex.Get( block.GetHash() ); index) {
        return index;
    }

    // Construct new block index object
    auto pindexNew = mapBlockIndex.Insert( block );

    // Check if adding new block index triggers safe mode
    CheckSafeModeParameters(config, pindexNew);

    return pindexNew;
}

void InvalidateChain(const Config& config, const CBlockIndex* pindexNew)
{
    std::set<CBlockIndex*> setTipCandidates;
    std::set<CBlockIndex*> setPrevs;

    // Check that we are invalidating chain from an invalid block
    assert(pindexNew->getStatus().isInvalid());

    // Check if invalid block is on current active chain
    bool isInvalidBlockOnActiveChain = chainActive.Contains(pindexNew);

    // Collect blocks that are not part of currently active chain
    mapBlockIndex.ForEachMutable(
        [&](CBlockIndex& index)
        {
            // Tip candidates are only blocks above invalid block 
            // If we are invalid block is not on active chain than we 
            // need only fork tips not active tip
            if (index.GetHeight() > pindexNew->GetHeight() &&
                (isInvalidBlockOnActiveChain || !chainActive.Contains(&index)))
            {
                setTipCandidates.insert(&index);
                setPrevs.insert(index.GetPrev());
            }
        });

    std::set<CBlockIndex*> setTips;
    std::set_difference(setTipCandidates.begin(), setTipCandidates.end(),
                        setPrevs.begin(), setPrevs.end(),
                        std::inserter(setTips, setTips.begin()));

    for (std::set< CBlockIndex*>::iterator it = setTips.begin();
         it != setTips.end(); ++it)
    {
        // Check if pindexNew is in this chain
        CBlockIndex* pindexWalk = (*it);
        while (pindexWalk->GetHeight() > pindexNew->GetHeight())
        {
            pindexWalk = pindexWalk->GetPrev();
        }
        if (pindexWalk == pindexNew)
        {
            // Set status of all descendant blocks to withFailedParent
            pindexWalk = (*it);
            while (pindexWalk != pindexNew)
            {
                pindexWalk->ModifyStatusWithFailedParent(mapBlockIndex);
                setBlockIndexCandidates.erase(pindexWalk);
                pindexWalk = pindexWalk->GetPrev();
            }
        }
    }
    // Check if we have to enter safe mode if chain has been invalidated
    CheckSafeModeParameters(config, nullptr);
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
    const Config& config,
    const CBlock &block,
    CValidationState &state,
    CBlockIndex *pindexNew,
    const CDiskBlockPos &pos,
    const CDiskBlockMetaData& metaData,
    const CBlockSource& source)
{
    // Validate TTOR order for blocks that are MIN_TTOR_VALIDATION_DISTANCE blocks or more from active tip
    if (chainActive.Tip() && chainActive.Tip()->GetHeight() - pindexNew->GetHeight() >= MIN_TTOR_VALIDATION_DISTANCE)
    {
        if (!CheckBlockTTOROrder(block))
        {
            LogPrintf("Block %s at height %d violates TTOR order.\n", block.GetHash().ToString(), pindexNew->GetHeight());
            // Mark the block itself as invalid.
            pindexNew->ModifyStatusWithFailed(mapBlockIndex);
            setBlockIndexCandidates.erase(pindexNew);
            InvalidateChain(config, pindexNew);
            InvalidChainFound(config, pindexNew);
            return state.Invalid(false, 0, "bad-blk-ttor");
        }
    }

    pindexNew->SetDiskBlockData(block.vtx.size(), pos, metaData, source, mapBlockIndex);

    if (pindexNew->IsGenesis() || pindexNew->GetPrev()->GetChainTx())
    {
        // If pindexNew is the genesis block or all parents are
        // BLOCK_VALID_TRANSACTIONS.
        std::deque<CBlockIndex *> queue;
        queue.push_back(pindexNew);

        // Recursively process any descendant blocks that now may be eligible to
        // be connected.
        while (!queue.empty()) {
            CBlockIndex *pindex = queue.front();
            queue.pop_front();
            {
                LOCK(cs_nBlockSequenceId);
                pindex->SetChainTxAndSequenceId(
                    (!pindex->IsGenesis() ? pindex->GetPrev()->GetChainTx() : 0) + pindex->GetBlockTxCount(),
                    nBlockSequenceId++);
            }
            if (chainActive.Tip() == nullptr ||
                !setBlockIndexCandidates.value_comp()(pindex,
                                                      chainActive.Tip())) {
                setBlockIndexCandidates.insert(pindex);
            }
            auto range = mapBlocksUnlinked.equal_range(pindex);
            while (range.first != range.second) {
                auto it = range.first;
                queue.push_back(it->second);
                range.first++;
                mapBlocksUnlinked.erase(it);
            }
        }
    } else if (!pindexNew->IsGenesis() &&
               pindexNew->GetPrev()->IsValid(BlockValidity::TREE))
    {
        mapBlocksUnlinked.insert(std::make_pair(pindexNew->GetPrev(), pindexNew));
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
        return state.DoS(50, false, REJECT_INVALID, "high-hash",
                         "proof of work failed");
    }

    return true;
}

bool CheckBlock(const Config &config, const CBlock &block,
                CValidationState &state,
                int32_t blockHeight,
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
            return state.CorruptionOrDoS("bad-txnmrklroot", "hashMerkleRoot mismatch");
        }

        // Check for merkle tree malleability (CVE-2012-2459): repeating
        // sequences of transactions in a block without affecting the merkle
        // root of a block, while still invalidating it.
        if (mutated) {
            return state.CorruptionOrDoS("bad-txns-duplicate", "duplicate transaction");
        }
    }

    // All potential-corruption validation must be done before we do any
    // transaction validation, as otherwise we may mark the header as invalid
    // because we receive the wrong transactions for it.

    // First transaction must be coinbase.
    if (block.vtx.empty()) {
        return state.DoS(100, false, REJECT_INVALID, "bad-cb-missing",
                         "first tx is not coinbase");
    }

    // Size limits.
    auto nMaxBlockSize = config.GetMaxBlockSize();
    // This validation option shouldCheckMaxBlockSize() is set in generateBlocks() RPC.
    // If block size was checked during CreateNewBlock(), another check is not needed.
    // With setexcessiveblock() RPC method value maxBlockSize may change to lower value
    // during block validation. Thus, block could be rejected because it would exceed
    // the max block size, even though it was accepted when block was created.
    if (validationOptions.shouldCheckMaxBlockSize()) {
        // Bail early if there is no way this block is of reasonable size.  
        if ( MIN_TRANSACTION_SIZE > 0 && block.vtx.size () > (nMaxBlockSize/MIN_TRANSACTION_SIZE)){
            return state.DoS(100, false, REJECT_INVALID, "bad-blk-length", "size limits failed");
        }

    }

    auto currentBlockSize = 
        ::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION);
    if (validationOptions.shouldCheckMaxBlockSize()) {
        if (currentBlockSize > nMaxBlockSize) {
            return state.DoS(100, false, REJECT_INVALID, "bad-blk-length",
                             "size limits failed");
        }
    }

    bool isGenesisEnabled = IsGenesisEnabled(config, blockHeight);
    uint64_t maxTxSigOpsCountConsensusBeforeGenesis = config.GetMaxTxSigOpsCountConsensusBeforeGenesis();
    uint64_t maxTxSizeConsensus = config.GetMaxTxSize(isGenesisEnabled, true);

    // And a valid coinbase.
    if (!CheckCoinbase(*block.vtx[0], state, maxTxSigOpsCountConsensusBeforeGenesis, maxTxSizeConsensus, isGenesisEnabled, blockHeight)) {
        auto result = state.Invalid(false, state.GetRejectCode(),
                                    state.GetRejectReason(),
                                    strprintf("Coinbase check failed (txid %s) %s",
                                              block.vtx[0]->GetId().ToString(),
                                              state.GetDebugMessage()));
        if(!state.IsValid() && g_connman)
        {
            g_connman->getInvalidTxnPublisher().Publish(
                { block.vtx[0], block.GetHash(), blockHeight, block.GetBlockTime(), state } );
        }
        return result;

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
                auto result = state.DoS(100, false, REJECT_INVALID, "bad-blk-sigops",
                                        "out-of-bounds SigOpCount");
                if(!state.IsValid() && g_connman)
                {
                    g_connman->getInvalidTxnPublisher().Publish(
                        { block.vtx[i], block.GetHash(), blockHeight, block.GetBlockTime(), state } );
                }
                return result;
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
            auto result = state.Invalid(

                false, state.GetRejectCode(), state.GetRejectReason(),
                strprintf("Transaction check failed (txid %s) %s",
                          tx->GetId().ToString(), state.GetDebugMessage()));
            if(!state.IsValid() && g_connman)
            {
                g_connman->getInvalidTxnPublisher().Publish(
                    { block.vtx[i], block.GetHash(), blockHeight, block.GetBlockTime(), state } );
            }
            return result;
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
    int32_t nHeight = pindexPrev->GetHeight() + 1;
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
    if (pcheckpoint && nHeight < pcheckpoint->GetHeight()) {
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

    const int32_t nHeight = pindexPrev == nullptr ? 0 : pindexPrev->GetHeight() + 1;

    // Check proof of work
    if (block.nBits != GetNextWorkRequired(pindexPrev, &block, config)) {
        LogPrintf("bad bits after height: %d\n", pindexPrev->GetHeight());
        return state.DoS(100, false, REJECT_INVALID, "bad-diffbits",
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
                                CValidationState &state, int32_t nHeight,
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
        return state.DoS(10, false, REJECT_INVALID, "bad-txns-nonfinal",
                         "non-final transaction");
    }

    return true;
}

bool ContextualCheckTransactionForCurrentBlock(
    const Config &config,
    const CTransaction &tx,
    int32_t nChainActiveHeight,
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
    const int32_t nBlockHeight = nChainActiveHeight + 1;

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
    const int32_t nHeight = pindexPrev == nullptr ? 0 : pindexPrev->GetHeight() + 1;
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
                        "size limits failed");
    }

    const int64_t nLockTimeCutoff = (nLockTimeFlags & LOCKTIME_MEDIAN_TIME_PAST)
                                        ? nMedianTimePast
                                        : block.GetBlockTime();

    // Check that all transactions are finalized
    for (const auto &tx : block.vtx) {
        if (!ContextualCheckTransaction(config, *tx, state, nHeight,
                                        nLockTimeCutoff, true)) {
            if(g_connman)
            {
                g_connman->getInvalidTxnPublisher().Publish(
                    { tx, block.GetHash(), nHeight, block.GetBlockTime(), state } );
            }
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

            auto result = state.DoS(100, false, REJECT_INVALID, "bad-cb-height",
                                    "block height mismatch in coinbase");
            if(!state.IsValid() && g_connman)
            {
                g_connman->getInvalidTxnPublisher().Publish(
                    { block.vtx[0], block.GetHash(), nHeight, block.GetBlockTime(), state } );
            }
            return result;
        }
    }

    return true;
}

/**
 * If found, returns an index of a previous block. 
 */
static const CBlockIndex* FindPreviousBlockIndex(const CBlockHeader &block, CValidationState &state)
{
    CBlockIndex* ppindex = mapBlockIndex.Get(block.hashPrevBlock);

    if (ppindex)
    {
        if (ppindex->getStatus().isInvalid())
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
bool AcceptBlockHeader(const Config& config,
                       const CBlockHeader& block,
                       CValidationState& state,
                       CBlockIndex** ppindex)
{
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
    if (hash != chainparams.GetConsensus().hashGenesisBlock) {
        if (CBlockIndex* pindex = mapBlockIndex.Get(hash); pindex)
        {
            // Block header is already known.
            if (ppindex) {
                *ppindex = pindex;
            }
            if (pindex->getStatus().isInvalid()) {
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

    if (CBlockIndex* newIdx = AddToBlockIndex(config, block); ppindex)
    {
        *ppindex = newIdx;
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
    bool *fNewBlock,
    const CBlockSource& source) {
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

    // Compare block header timestamps and received times of the block and the
    // chaintip.  If they have the same chain height, just log the time
    // difference for both.
    int64_t newBlockTimeDiff = std::llabs(pindex->GetReceivedTimeDiff());
    int64_t chainTipTimeDiff =
        chainActive.Tip() ? std::llabs(chainActive.Tip()->GetReceivedTimeDiff())
        : 0;

    auto chainWork = pindex->GetChainWork();
    bool isSameHeightAndMoreHonestlyMined =
        chainActive.Tip() &&
        (chainWork == chainActive.Tip()->GetChainWork()) &&
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
        (chainActive.Tip() ? chainWork > chainActive.Tip()->GetChainWork()
            : true);
    // Blocks that are too out-of-order needlessly limit the effectiveness of
    // pruning, because pruning will not delete block files that contain any
    // blocks which are too close in height to the tip. Apply this test
    // regardless of whether pruning is enabled; it should generally be safe to
    // not process unrequested blocks.
    bool fTooFarAhead =
        (pindex->GetHeight() > (chainActive.Height() + config.GetMinBlocksToKeep()));

    // TODO: Decouple this function from the block download logic by removing
    // fRequested
    // This requires some new chain datastructure to efficiently look up if a
    // block is in a chain leading to a candidate for best tip, despite not
    // being such a candidate itself.

    // Try to process all requested blocks that we don't have, but only
    // process an unrequested block if it's new and has enough work to
    // advance our tip, and isn't too many blocks ahead.
    // TODO: deal better with return value and error conditions for duplicate
    // and unrequested blocks.
    if (bool fAlreadyHave = pindex->getStatus().hasData(); fAlreadyHave)
    {
        return true;
    }

    // If we didn't ask for it:
    if (!fRequested) {
        // This is a previously-processed block that was pruned.
        if (pindex->GetBlockTxCount() != 0) {
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

    if (!CheckBlock(config, block, state, pindex->GetHeight()) ||
        !ContextualCheckBlock(config, block, state, pindex->GetPrev()))
    {
        if (state.IsInvalid() && !state.CorruptionPossible()) {
            pindex->ModifyStatusWithFailed(mapBlockIndex);
        }
        return error("%s: %s (block %s)", __func__, FormatStateMessage(state),
            block.GetHash().ToString());
    }

    // Header is valid/has work and the merkle tree is good.
    // Relay now, but if it does not build on our best tip, let the
    // SendMessages loop relay it.
    if (!IsInitialBlockDownload() && chainActive.Tip() == pindex->GetPrev())
    {
        GetMainSignals().NewPoWValidBlock(pindex, pblock);
    }

    int32_t nHeight = pindex->GetHeight();
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
            if (!BlockFileAccess::WriteBlockToDisk(block, blockPos, chainparams.DiskMagic(), metaData))
            {
                AbortNode(state, "Failed to write block");
            }
        }
        if (!ReceivedBlockTransactions(config, block, state, pindex, blockPos, metaData, source)) {
            return error("AcceptBlock(): ReceivedBlockTransactions failed");
        }
        std::string blockTimeAsString =
                        DateTimeStrFormat("%Y-%m-%d %H:%M:%S",
                                          block.GetBlockHeader().GetBlockTime());
        LogPrint(BCLog::BENCH, "Accepted block hash=%s, height=%d, size=%ld, num_tx=%u, block-time=%s, file=blk%05u.dat\n",
                 block.GetHash().ToString(),
                 nHeight,
                 metaData.diskDataSize,
                 block.vtx.size(),
                 blockTimeAsString,
                 blockPos.File());
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
        CheckSafeModeParameters(config, pindex);
    }

    return true;
}

bool VerifyNewBlock(const Config &config,
                    const std::shared_ptr<const CBlock> pblock) {

    CValidationState state;
    BlockValidationOptions validationOptions = BlockValidationOptions().withCheckPoW(false);
    const CBlockIndex* pindexPrev = FindPreviousBlockIndex(*pblock, state);
    if (!pindexPrev)
    {
        return false;
    }

    bool ret = CheckBlock(config, *pblock, state, pindexPrev->GetHeight() + 1, validationOptions);

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
    bool* fNewBlock,
    const CBlockSource& source,
    const BlockValidationOptions& validationOptions)
{
    auto guard = CBlockProcessing::GetCountGuard();

    {
        CBlockIndex *pindex = nullptr;
        if (fNewBlock) {
            *fNewBlock = false;
        }

        const CChainParams &chainparams = config.GetChainParams();

        CValidationState state;

        // We need previous block index to calculate current block height used by CheckBlock. This check is later repeated in AcceptBlockHeader
        const CBlockIndex* pindexPrev = FindPreviousBlockIndex(*pblock, state);
        if (!pindexPrev)
        {
            return {};
        }

        // Ensure that CheckBlock() passes before calling AcceptBlock, as
        // belt-and-suspenders.
        bool ret = CheckBlock(config, *pblock, state, pindexPrev->GetHeight() + 1, validationOptions);

        LOCK(cs_main);

        if (ret) {
            // Store to disk
            ret = AcceptBlock(config, pblock, state, &pindex, fForceProcessing,
                              nullptr, fNewBlock, source);
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

            CJournalChangeSetPtr changeSet { mempool.getJournalBuilder().getNewChangeSet(JournalUpdateReason::NEW_BLOCK) };

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
                     bool fForceProcessing, bool *fNewBlock,
                     const CBlockSource& blockSource,
                     const BlockValidationOptions& validationOptions)
{
    auto source = task::CCancellationSource::Make();
    auto bestChainActivation =
        ProcessNewBlockWithAsyncBestChainActivation(
            task::CCancellationToken::JoinToken(source->GetToken(), GetShutdownToken()), config, pblock, fForceProcessing, fNewBlock, blockSource, validationOptions);

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

    CoinsDBView view{ *pcoinsTip };
    CCoinsViewCache viewNew{ view };

    CBlockIndex::TemporaryBlockIndex indexDummy{ *pindexPrev, block };

    // NOTE: CheckBlockHeader is called by CheckBlock
    if (!ContextualCheckBlockHeader(config, block, state, pindexPrev,
                                    GetAdjustedTime())) {
        return error("%s: Consensus::ContextualCheckBlockHeader: %s", __func__,
                     FormatStateMessage(state));
    }
    if (!CheckBlock(config, block, state, indexDummy->GetHeight(), validationOptions)) {
        return error("%s: Consensus::CheckBlock: %s", __func__,
                     FormatStateMessage(state));
    }
    if (!ContextualCheckBlock(config, block, state, pindexPrev)) {
        return error("%s: Consensus::ContextualCheckBlock: %s", __func__,
                     FormatStateMessage(state));
    }
    auto source = task::CCancellationSource::Make();

    if (!ConnectBlock(source->GetToken(), false, true, config, block, state, indexDummy.get(), viewNew, chainActive.Height(), indexDummy->GetChainWork(), true))
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
    mapBlockIndex.ForEachMutable(
        [&](CBlockIndex& index)
        {
            if (index.ClearFileInfoIfFileNumberEquals(fileNumber, mapBlockIndex))
            {
                // Prune from mapBlocksUnlinked -- any block we prune would have
                // to be downloaded again in order to consider its chain, at which
                // point it would be considered as a candidate for
                // mapBlocksUnlinked or setBlockIndexCandidates.
                auto range = mapBlocksUnlinked.equal_range( index.GetPrev() );
                while (range.first != range.second) {
                    auto _it = range.first;
                    range.first++;
                    if (_it->second == &index) {
                        mapBlocksUnlinked.erase(_it);
                    }
                 }
            }
        });

    pBlockFileInfoStore->ClearFileInfo(fileNumber);
}

void UnlinkPrunedFiles(const std::set<int> &setFilesToPrune)
{
    for (const int i : setFilesToPrune)
    {
        if (BlockFileAccess::RemoveFile( i )) // if there was no error
        {
            // remove block index data if file deletion succeeded otherwise keep
            // the data for now as it's most likely still being used
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
void PruneBlockFilesManual(int32_t nManualPruneHeight) {
    CValidationState state;
    const CChainParams &chainparams = Params();
    FlushStateToDisk(chainparams, state, FLUSH_STATE_NONE, nManualPruneHeight);
}


bool CheckDiskSpace(uint64_t nAdditionalBytes) {
    fs::path datadir = GetDataDir();
    auto space = fs::space(datadir);

    // Check for nMinDiskSpace bytes (currently 50MB)
    if (space.available < nMinDiskSpace + nAdditionalBytes) {
        std::string msg = strprintf("Disk space is low for directory '%s'! "
                                    "Available:%lu, required: mindiskspace:%lu + additionalbytes:%lu, free:%lu, capacity:%lu",
                                    datadir.string(),
                                    space.available,
                                    nMinDiskSpace,
                                    nAdditionalBytes,
                                    space.free,
                                    space.capacity);
        return AbortNode(msg, _(strprintf("Error:%s", msg).c_str()));
    }

    return true;
}

static bool LoadBlockIndexDB(const CChainParams &chainparams) {
    if (!BlockIndexStoreLoader(mapBlockIndex).ForceLoad(
            GlobalConfig::GetConfig(),
            pblocktree->GetIterator()))
    {
        return false;
    }

    boost::this_thread::interruption_point();

    // Calculate chain work
    std::vector<std::pair<int32_t, CBlockIndex *>> vSortedByHeight;
    vSortedByHeight.reserve(mapBlockIndex.Count());
    mapBlockIndex.ForEachMutable(
        [&](CBlockIndex& index)
        {
            vSortedByHeight.push_back(std::make_pair(index.GetHeight(), &index));
        });
    sort(vSortedByHeight.begin(), vSortedByHeight.end());
    for (const std::pair<int32_t, CBlockIndex *> &item : vSortedByHeight) {
        CBlockIndex *pindex = item.second;
        CBlockIndex* pprev = pindex->GetPrev();
        if (!pindex->PostLoadIndexConnect() && pprev)
        {
            mapBlocksUnlinked.insert( std::make_pair(pprev, pindex) );
        }
        if (pindex->IsValid(BlockValidity::TRANSACTIONS) &&
            (pindex->GetChainTx() || pprev == nullptr)) {
            setBlockIndexCandidates.insert(pindex);
        }
        if (pindex->getStatus().isInvalid() &&
            (!pindexBestInvalid ||
             pindex->GetChainWork() > pindexBestInvalid->GetChainWork()))
        {
            pindexBestInvalid = pindex;
        }

        mapBlockIndex.SetBestHeader( *pindex );
    }

    // Load block file info
    int nLastBlockFileLocal = 0;
    pblocktree->ReadLastBlockFile(nLastBlockFileLocal);
    pBlockFileInfoStore->LoadBlockFileInfo(nLastBlockFileLocal, *pblocktree);

    // Check presence of blk files
    LogPrintf("Checking all blk files are present...\n");
    std::set<int> setBlkDataFiles;
    mapBlockIndex.ForEachMutable(
        [&](CBlockIndex& index)
        {
            if (auto nFile = index.GetFileNumber(); nFile.has_value())
            {
                setBlkDataFiles.insert(nFile.value());
            }
        });
    for (const int i : setBlkDataFiles) {
        if (auto file = BlockFileAccess::OpenBlockFile( i ); file == nullptr)
        {
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
    if (!fReindexing)
    {
        fReindex = false;
    }

    // Check whether we have a transaction index
    pblocktree->ReadFlag("txindex", fTxIndex);
    LogPrintf("%s: transaction index %s\n", __func__,
              fTxIndex ? "enabled" : "disabled");

    return true;
}

void LoadChainTip(const CChainParams &chainparams) {
    CoinsDBView view{*pcoinsTip};

    if (chainActive.Tip() &&
        chainActive.Tip()->GetBlockHash() == view.GetBestBlock()) {
        return;
    }

    // Load pointer to end of best chain
    auto index = mapBlockIndex.Get(view.GetBestBlock());
    if (!index)
    {
        return;
    }

    if(setBlockIndexCandidates.count(index) == 0)
    {
        throw std::runtime_error("LoadChainTip error: CoinsDB best block not in setBlockIndexCandidates");
    }

    chainActive.SetTip(index);
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

bool CVerifyDB::VerifyDB(const Config &config, CoinsDB& coinsview,
                         int nCheckLevel, int nCheckDepth, const task::CCancellationToken& shutdownToken) {
    LOCK(cs_main);
    if (chainActive.Tip() == nullptr || chainActive.Tip()->IsGenesis())
    {
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

    CoinsDBView view{ coinsview };
    CCoinsViewCache coins{ view };
    CBlockIndex *pindexState = chainActive.Tip();
    CBlockIndex *pindexFailure = nullptr;
    size_t nGoodTransactions = 0;
    CValidationState state;
    int reportDone = 0;
    LogPrintf("[0%%]...");
    for (CBlockIndex *pindex = chainActive.Tip(); pindex && !pindex->IsGenesis();
         pindex = pindex->GetPrev())
    {
        int percentageDone = std::max(
            1, std::min(
                   99,
                   (int)(((double)(chainActive.Height() - pindex->GetHeight())) /
                         (double)nCheckDepth * (nCheckLevel >= 4 ? 50 : 100))));

        if (reportDone < percentageDone / 10) {
            // report every 10% step
            LogPrintf("[%d%%]...", percentageDone);
            reportDone = percentageDone / 10;
        }

        uiInterface.ShowProgress(_("Verifying blocks..."), percentageDone);
        if (pindex->GetHeight() < chainActive.Height() - nCheckDepth) {
            break;
        }

        if (fPruneMode && !pindex->getStatus().hasData()) {
            // If pruning, only go back as far as we have data.
            LogPrintf("VerifyDB(): block verification stopping at height %d "
                      "(pruning, no data)\n",
                      pindex->GetHeight());
            break;
        }

        CBlock block;

        // check level 0: read from disk
        if (!pindex->ReadBlockFromDisk(block, config)) {
            return error(
                "VerifyDB(): *** ReadBlockFromDisk failed at %d, hash=%s",
                pindex->GetHeight(), pindex->GetBlockHash().ToString());
        }

        if (shutdownToken.IsCanceled())
        {
            return true;
        }

        // check level 1: verify block validity
        if (nCheckLevel >= 1 && !CheckBlock(config, block, state, pindex->GetHeight())) {
            return error("%s: *** found bad block at %d, hash=%s (%s)\n",
                         __func__, pindex->GetHeight(),
                         pindex->GetBlockHash().ToString(),
                         FormatStateMessage(state));
        }

        if (shutdownToken.IsCanceled())
        {
            return true;
        }

        // check level 2: verify undo validity
        if (nCheckLevel >= 2 && pindex) {
            if (!pindex->verifyUndoValidity())
            {
                return false;
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
            DisconnectResult res = ProcessingBlockIndex(*pindex).DisconnectBlock(block, coins, shutdownToken);
            if (res == DISCONNECT_FAILED && !shutdownToken.IsCanceled()) {
                return error("VerifyDB(): *** irrecoverable inconsistency in "
                             "block data at %d, hash=%s",
                             pindex->GetHeight(),
                             pindex->GetBlockHash().ToString());
            }
            pindexState = pindex->GetPrev();
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
                     chainActive.Height() - pindexFailure->GetHeight() + 1,
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
                                                        pindex->GetHeight())) /
                                              (double)nCheckDepth * 50))));
            pindex = chainActive.Next(pindex);
            CBlock block;
            if (!pindex->ReadBlockFromDisk(block, config)) {
                return error(
                    "VerifyDB(): *** ReadBlockFromDisk failed at %d, hash=%s",
                    pindex->GetHeight(), pindex->GetBlockHash().ToString());
            }
            auto source = task::CCancellationSource::Make();
            if (!ConnectBlock(source->GetToken(), false, false, config, block, state, pindex, coins, chainActive.Height(), pindex->GetChainWork()))
            {
                return error(
                    "VerifyDB(): *** found unconnectable block at %d, hash=%s",
                    pindex->GetHeight(), pindex->GetBlockHash().ToString());
            }
        }
    }

    LogPrintf("[DONE].\n");
    LogPrintf("No coin database inconsistencies in last %i blocks (%zu "
              "transactions)\n",
              chainActive.Height() - pindexState->GetHeight(), nGoodTransactions);

    return true;
}

/**
 * Apply the effects of a block on the utxo cache, ignoring that it may already
 * have been applied.
 */
static bool RollforwardBlock(const CBlockIndex *pindex, CoinsDBSpan &inputs,
                             const Config &config) {
    // TODO: merge with ConnectBlock
    auto blockStreamReader = pindex->GetDiskBlockStreamReader(config);
    if (!blockStreamReader) {
        return error("ReplayBlock(): GetDiskBlockStreamReader(CBlockIndex) failed at %d, hash=%s",
                     pindex->GetHeight(), pindex->GetBlockHash().ToString());
    }

    while (!blockStreamReader->EndOfStream()) {
        const CTransaction* tx = blockStreamReader->ReadTransaction_NoThrow();
        if(!tx)
        {
            return error("ReplayBlock(): ReadTransaction failed at %d, hash=%s",
                pindex->GetHeight(), pindex->GetBlockHash().ToString());
        }
        if (!tx->IsCoinBase()) {
            for (const CTxIn &txin : tx->vin) {
                inputs.SpendCoin(txin.prevout);
            }
        }

        // Pass check = true as every addition may be an overwrite.
        AddCoins(inputs, *tx, CFrozenTXOCheck::IsConfiscationTx(*tx), pindex->GetHeight(), config.GetGenesisActivationHeight(), true);
    }

    return true;
}

bool ReplayBlocks(const Config &config, CoinsDB& view) {
    LOCK(cs_main);

    CoinsDBSpan cache( view );

    std::vector<uint256> hashHeads = cache.GetHeadBlocks();
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
    auto pindexNew = mapBlockIndex.Get(hashHeads[0]);
    // Latest block common to both the old and the new tip.
    const CBlockIndex *pindexFork = nullptr;

    if (!pindexNew)
    {
        return error(
            "ReplayBlocks(): reorganization to unknown block requested");
    }

    if (!hashHeads[1].IsNull()) {
        // The old tip is allowed to be 0, indicating it's the first flush.
        pindexOld = mapBlockIndex.Get(hashHeads[1]);
        if (!pindexOld) {
            return error(
                "ReplayBlocks(): reorganization from unknown block requested");
        }
        pindexFork = LastCommonAncestor(pindexOld, pindexNew);
        assert(pindexFork != nullptr);
    }

    // Rollback along the old branch.
    while (pindexOld != pindexFork) {
        if (pindexOld->GetHeight() > 0) {
            // Never disconnect the genesis block.
            CBlock block;
            if (!pindexOld->ReadBlockFromDisk(block, config)) {
                return error("RollbackBlock(): ReadBlockFromDisk() failed at "
                             "%d, hash=%s",
                             pindexOld->GetHeight(),
                             pindexOld->GetBlockHash().ToString());
            }
            LogPrintf("Rolling back %s (%i)\n",
                      pindexOld->GetBlockHash().ToString(), pindexOld->GetHeight());
            // Use new private CancellationSource that can not be cancelled
            DisconnectResult res = ProcessingBlockIndex(const_cast<CBlockIndex&>(*pindexOld)).DisconnectBlock(block, cache, task::CCancellationSource::Make()->GetToken());
            if (res == DISCONNECT_FAILED) {
                return error(
                    "RollbackBlock(): DisconnectBlock failed at %d, hash=%s",
                    pindexOld->GetHeight(), pindexOld->GetBlockHash().ToString());
            }
            // If DISCONNECT_UNCLEAN is returned, it means a non-existing UTXO
            // was deleted, or an existing UTXO was overwritten. It corresponds
            // to cases where the block-to-be-disconnect never had all its
            // operations applied to the UTXO set. However, as both writing a
            // UTXO and deleting a UTXO are idempotent operations, the result is
            // still a version of the UTXO set with the effects of that block
            // undone.
        }
        pindexOld = pindexOld->GetPrev();
    }

    // Roll forward from the forking point to the new tip.
    int32_t nForkHeight = pindexFork ? pindexFork->GetHeight() : 0;
    for (int32_t nHeight = nForkHeight + 1; nHeight <= pindexNew->GetHeight();
         ++nHeight) {
        const CBlockIndex *pindex = pindexNew->GetAncestor(nHeight);
        LogPrintf("Rolling forward %s (%i)\n",
                  pindex->GetBlockHash().ToString(), nHeight);
        if (!RollforwardBlock(pindex, cache, config)) {
            return false;
        }
    }

    cache.SetBestBlock(pindexNew->GetBlockHash());

    // NOTE:
    // TryFlush() will never fail as cs_main is used to synchronize
    // the different threads that Flush() or TryFlush() data. If cs_main
    // guarantee is removed we must decide what to do in this case.
    auto flushed = cache.TryFlush();
    assert(flushed == CoinsDBSpan::WriteState::ok);
    uiInterface.ShowProgress("", 100);
    return true;
}

bool RewindBlockIndex(const Config &config) {
    LOCK(cs_main);

    const CChainParams &params = config.GetChainParams();
    int32_t nHeight = chainActive.Height() + 1;

    // nHeight is now the height of the first insufficiently-validated block, or
    // tipheight + 1
    CValidationState state;
    CBlockIndex *pindex = chainActive.Tip();
    CJournalChangeSetPtr changeSet { mempool.getJournalBuilder().getNewChangeSet(JournalUpdateReason::REORG) };
    while (chainActive.Height() >= nHeight) {
        if (fPruneMode && !chainActive.Tip()->getStatus().hasData()) {
            // If pruning, don't try rewinding past the HAVE_DATA point; since
            // older blocks can't be served anyway, there's no need to walk
            // further, and trying to DisconnectTip() will fail (and require a
            // needless reindex/redownload of the blockchain).
            break;
        }
        if (!DisconnectTip(config, state, nullptr, changeSet)) {
            return error(
                "RewindBlockIndex: unable to disconnect block at height %i",
                pindex->GetHeight());
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
    mapBlockIndex.ForEachMutable(
        [&](CBlockIndex& index)
        {
            if (index.IsValid(BlockValidity::TRANSACTIONS) &&
                index.GetChainTx()) {
                setBlockIndexCandidates.insert(&index);
            }
        });

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

    SafeModeClear();

    setBlockIndexCandidates.clear();
    chainActive.SetTip(nullptr);
    pindexBestInvalid = nullptr;
    // FIXME: CORE-1253, CORE-1232
    // Assumption: This is called only at startup before mempool.dat is restored.
    // This is a quick fix for CORE-1253 to prevent wiping mempoolTxDB at
    // startup, a more complete fix will be part of CORE-1232 work.
    if (mempool.Size() > 0) {
        mempool.Clear();
    }
    mapBlocksUnlinked.clear();
    pBlockFileInfoStore->Clear();
    nBlockSequenceId = 1;

    BlockIndexStoreLoader(mapBlockIndex).ForceClear();
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
            if (!BlockFileAccess::WriteBlockToDisk(block, blockPos, chainparams.DiskMagic(), metaData))
            {
                return error(
                    "LoadBlockIndex(): writing genesis block to disk failed");
            }
            CBlockIndex *pindex = AddToBlockIndex(config, block);
            if (!ReceivedBlockTransactions(config, block, state, pindex, blockPos, metaData,
                CBlockSource::MakeLocal("genesis")))
            {
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

void ReindexAllBlockFiles(const Config &config, CBlockTreeDB *pblocktree, std::atomic_bool& fReindex)
{
    
    int nFile = 0;
    while (true) {
        UniqueCFile file = BlockFileAccess::OpenBlockFile( nFile );
        if (file == nullptr)
        {
            // No block files left to reindex or an error occurred.
            // Potential errors are logged in GetBlockFile.
            break;
        }
        LogPrintf("Reindexing block file blk%05u.dat...\n",
            (unsigned int)nFile);
        CDiskBlockPos pos{ nFile, 0 };
        LoadExternalBlockFile(config, std::move(file), &pos);
        nFile++;
    }

    pblocktree->WriteReindexing(false);
    fReindex = false;
    LogPrintf("Reindexing finished\n");
    // To avoid ending up in a situation without genesis block, re-try
    // initializing (no-op if reindexing worked):
    InitBlockIndex(config);
}

bool LoadExternalBlockFile(const Config &config, UniqueCFile fileIn,
                           CDiskBlockPos *dbp) {
    // Map of disk positions for blocks with unknown parent (only used for
    // reindex)
    static std::multimap<uint256, CDiskBlockPos> mapBlocksUnknownParent;
    int64_t nStart = GetTimeMillis();

    const CChainParams &chainparams = config.GetChainParams();

    int nLoaded = 0;
    try {
        // This takes over fileIn and calls fclose() on it in the CBufferedFile destructor.
        CBufferedFile blkdat{
            { std::move(fileIn), SER_DISK, CLIENT_VERSION },
            2 * ONE_MEGABYTE,
            ONE_MEGABYTE + 8};
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
                uint8_t buf[CMessageFields::MESSAGE_START_SIZE];
                blkdat.FindByte(chainparams.DiskMagic()[0]);
                nRewind = blkdat.GetPos() + 1;
                blkdat >> FLATDATA(buf);
                if (memcmp(buf, chainparams.DiskMagic().data(),
                           CMessageFields::MESSAGE_START_SIZE)) {
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
                    *dbp = {dbp->File(), static_cast<unsigned int>(nBlockPos)};
                }
                blkdat.SetLimit(nBlockPos + nSize);
                blkdat.SetPos(nBlockPos);
                std::shared_ptr<CBlock> pblock = std::make_shared<CBlock>();
                CBlock &block = *pblock;
                blkdat >> block;
                nRewind = blkdat.GetPos();

                // detect out of order blocks, and store them for later
                uint256 hash = block.GetHash();
                if (auto prevIndex = mapBlockIndex.Get(block.hashPrevBlock);
                    hash != chainparams.GetConsensus().hashGenesisBlock && !prevIndex)
                {
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
                if (auto index = mapBlockIndex.Get(hash);
                    !index || !index->getStatus().hasData()) {
                    LOCK(cs_main);
                    CValidationState state;
                    if (AcceptBlock(config, pblock, state, nullptr, true, dbp,
                                    nullptr, CBlockSource::MakeLocal("external block file"))) {
                        nLoaded++;
                    }
                    if (state.IsError()) {
                        break;
                    }
                } else if (hash !=
                               chainparams.GetConsensus().hashGenesisBlock &&
                           index->GetHeight() % 1000 == 0) {
                    LogPrint(
                        BCLog::REINDEX,
                        "Block Import: already had block %s at height %d\n",
                        hash.ToString(), index->GetHeight());
                }

                // Activate the genesis block so normal node progress can
                // continue
                if (hash == chainparams.GetConsensus().hashGenesisBlock) {
                    // dummyState is used to report errors, not block related invalidity - ignore it
                    // (see description of ActivateBestChain)
                    CValidationState dummyState;
                    CJournalChangeSetPtr changeSet { mempool.getJournalBuilder().getNewChangeSet(JournalUpdateReason::REORG) };
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
                        if (BlockFileAccess::ReadBlockFromDisk(
                                *pblockrecursive,
                                it->second,
                                config))
                        {
                            LogPrint(
                                BCLog::REINDEX,
                                "%s: Processing out of order child %s of %s\n",
                                __func__, pblockrecursive->GetHash().ToString(),
                                head.ToString());
                            LOCK(cs_main);
                            CValidationState dummy;
                            if (AcceptBlock(config, pblockrecursive, dummy,
                                            nullptr, true, &it->second,
                                            nullptr, CBlockSource::MakeLocal("external block file"))) {
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
        assert(mapBlockIndex.Count() <= 1);
        return;
    }

    // Build forward-pointing map of the entire block tree.
    std::multimap<CBlockIndex *, CBlockIndex *> forward;
    mapBlockIndex.ForEachMutable(
        [&](CBlockIndex& index)
        {
            forward.insert(std::make_pair(index.GetPrev(), &index));
        });

    assert(forward.size() == mapBlockIndex.Count());

    auto rangeGenesis = forward.equal_range(nullptr);
    CBlockIndex *pindex = rangeGenesis.first->second;
    rangeGenesis.first++;
    // There is only one index entry with parent nullptr.
    assert(rangeGenesis.first == rangeGenesis.second);

    // Iterate over the entire block tree, using depth-first search.
    // Along the way, remember whether there are blocks on the path from genesis
    // block being explored which are the first to have certain properties.
    size_t nNodes = 0;
    int32_t nHeight = 0;
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
        BlockStatus status = pindex->getStatus();
        if (pindexFirstInvalid == nullptr && status.hasFailed()) {
            pindexFirstInvalid = pindex;
        }
        if (pindexFirstMissing == nullptr && !status.hasData()) {
            pindexFirstMissing = pindex;
        }
        if (pindexFirstNeverProcessed == nullptr && pindex->GetBlockTxCount() == 0)
        {
            pindexFirstNeverProcessed = pindex;
        }
        if (!pindex->IsGenesis())
        {
            if (pindexFirstNotTreeValid == nullptr &&
                status.getValidity() < BlockValidity::TREE)
            {
                pindexFirstNotTreeValid = pindex;
            }
            if (pindexFirstNotTransactionsValid == nullptr &&
                status.getValidity() < BlockValidity::TRANSACTIONS) {
                pindexFirstNotTransactionsValid = pindex;
            }
            if (pindexFirstNotChainValid == nullptr &&
                status.getValidity() < BlockValidity::CHAIN) {
                pindexFirstNotChainValid = pindex;
            }
            if (pindexFirstNotScriptsValid == nullptr &&
                status.getValidity() < BlockValidity::SCRIPTS) {
                pindexFirstNotScriptsValid = pindex;
            }
        }
        // Begin: actual consistency checks.
        else
        {
            // Genesis block checks.
            // Genesis block's hash must match.
            assert(pindex->GetBlockHash() == consensusParams.hashGenesisBlock);
            // The current active chain's genesis block must be this block.
            assert(pindex == chainActive.Genesis());
        }
        if (pindex->GetChainTx() == 0) {
            // nSequenceId can't be set positive for blocks that aren't linked
            // (negative is used for preciousblock)
            assert(pindex->GetSequenceId() <= 0);
        }
        // VALID_TRANSACTIONS is equivalent to nTx > 0 for all nodes (whether or
        // not pruning has occurred). HAVE_DATA is only equivalent to nTx > 0
        // (or VALID_TRANSACTIONS) if no pruning has occurred.
        if (!fHavePruned) {
            // If we've never pruned, then HAVE_DATA should be equivalent to nTx
            // > 0
            assert(!status.hasData() == (pindex->GetBlockTxCount() == 0));
            assert(pindexFirstMissing == pindexFirstNeverProcessed);
        } else if (status.hasData()) {
            // If we have pruned, then we can only say that HAVE_DATA implies
            // nTx > 0
            assert(pindex->GetBlockTxCount() > 0);
        }
        if (status.hasUndo()) {
            assert(status.hasData());
        }
        // This is pruning-independent.
        assert((status.getValidity() >= BlockValidity::TRANSACTIONS) ==
               (pindex->GetBlockTxCount() > 0));
        // All parents having had data (at some point) is equivalent to all
        // parents being VALID_TRANSACTIONS, which is equivalent to nChainTx
        // being set.
        // nChainTx != 0 is used to signal that all parent blocks have been
        // processed (but may have been pruned).
        assert((pindexFirstNeverProcessed != nullptr) ==
               (pindex->GetChainTx() == 0));
        assert((pindexFirstNotTransactionsValid != nullptr) ==
               (pindex->GetChainTx() == 0));
        // nHeight must be consistent.
        assert(pindex->GetHeight() == nHeight);
        // For every block except the genesis block, the chainwork must be
        // larger than the parent's.
        assert(pindex->IsGenesis() ||
               pindex->GetChainWork() >= pindex->GetPrev()->GetChainWork());
        // The pskip pointer must point back for all but the first 2 blocks.
        assert(nHeight < 2 ||
               (pindex->GetSkip() && (pindex->GetSkip()->GetHeight() < nHeight)));
        // All mapBlockIndex entries must at least be TREE valid
        assert(pindexFirstNotTreeValid == nullptr);
        if (status.getValidity() >= BlockValidity::TREE) {
            // TREE valid implies all parents are TREE valid
            assert(pindexFirstNotTreeValid == nullptr);
        }
        if (status.getValidity() >= BlockValidity::CHAIN) {
            // CHAIN valid implies all parents are CHAIN valid
            assert(pindexFirstNotChainValid == nullptr);
        }
        if (status.getValidity() >= BlockValidity::SCRIPTS) {
            // SCRIPTS valid implies all parents are SCRIPTS valid
            assert(pindexFirstNotScriptsValid == nullptr);
        }
        if (pindexFirstInvalid == nullptr) {
            // Checks for not-invalid blocks.
            // The failed mask cannot be set for blocks without invalid parents.
            assert(!status.isInvalid());
        }
        // Check whether this block is in mapBlocksUnlinked.
        auto rangeUnlinked = mapBlocksUnlinked.equal_range(pindex->GetPrev());
        bool foundInUnlinked = false;
        while (rangeUnlinked.first != rangeUnlinked.second) {
            assert(rangeUnlinked.first->first == pindex->GetPrev());
            if (rangeUnlinked.first->second == pindex) {
                foundInUnlinked = true;
                break;
            }
            rangeUnlinked.first++;
        }
        if (!pindex->IsGenesis() && status.hasData() &&
            pindexFirstNeverProcessed != nullptr &&
            pindexFirstInvalid == nullptr) {
            // If this block has block data available, some parent was never
            // received, and has no invalid parents, it must be in
            // mapBlocksUnlinked.
            assert(foundInUnlinked);
        }
        if (!status.hasData()) {
            // Can't be in mapBlocksUnlinked if we don't HAVE_DATA
            assert(!foundInUnlinked);
        }
        if (pindexFirstMissing == nullptr) {
            // We aren't missing data for any parent -- cannot be in
            // mapBlocksUnlinked.
            assert(!foundInUnlinked);
        }
        if (!pindex->IsGenesis() && status.hasData() &&
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
        auto range = forward.equal_range(pindex);
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
            CBlockIndex *pindexPar = pindex->GetPrev();
            // Find which child we just visited.
            auto rangePar = forward.equal_range(pindexPar);
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


CBlockFileInfo *GetBlockFileInfo(size_t n) {
    return pBlockFileInfoStore->GetBlockFileInfo(n);
}

//! Guess how far we are in the verification process at the given block index
double GuessVerificationProgress(const ChainTxData &data, const CBlockIndex *pindex) {
    if (pindex == nullptr) {
        return 0.0;
    }

    int64_t nNow = time(nullptr);

    double fTxTotal;
    if (pindex->GetChainTx() <= data.nTxCount) {
        fTxTotal = data.nTxCount + (nNow - data.nTime) * data.dTxRate;
    } else {
        fTxTotal =
            pindex->GetChainTx() + (nNow - pindex->GetBlockTime()) * data.dTxRate;
    }

    return pindex->GetChainTx() / fTxTotal;
}


void InitFrozenTXO(std::size_t cache_size)
{
    CFrozenTXOLogger::Init();
    CFrozenTXODB::Init(cache_size);
}

void ShutdownFrozenTXO()
{
    CFrozenTXODB::Shutdown();
    CFrozenTXOLogger::Shutdown();
}
