// Copyright (c) 2019 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file LICENSE.
#include <mining/journaling_block_assembler.h>

#include <chainparams.h>
#include <config.h>
#include <consensus/validation.h>
#include <logging.h>
#include <mining/journal_builder.h>
#include <timedata.h>
#include <txmempool.h>
#include <util.h>
#include <validation.h>

#include <limits>

using mining::CJournal;
using mining::CBlockTemplate;
using mining::JournalingBlockAssembler;

namespace
{
    // Getters for config values
    uint64_t GetMaxTxnBatch()
    {
        return static_cast<uint64_t>(gArgs.GetArg("-jbamaxtxnbatch", JournalingBlockAssembler::DEFAULT_MAX_SLOT_TRANSACTIONS));
    }
    bool GetFillAfterNewBlock()
    {
        return gArgs.GetBoolArg("-jbafillafternewblock", JournalingBlockAssembler::DEFAULT_NEW_BLOCK_FILL);
    }
    unsigned GetThrottleThreshold()
    {
        int64_t threshold { gArgs.GetArg("-jbathrottlethreshold", JournalingBlockAssembler::DEFAULT_THROTTLE_THRESHOLD) };
        if(threshold <= 0 || threshold > 100)
        {
            threshold = JournalingBlockAssembler::DEFAULT_THROTTLE_THRESHOLD;
        }
        return static_cast<unsigned>(threshold);
    }
    unsigned GetRunFrequency()
    {
        return static_cast<unsigned>(gArgs.GetArg("-jbarunfrequency", JournalingBlockAssembler::DEFAULT_RUN_FREQUENCY_MILLIS));
    }
}

// Construction
JournalingBlockAssembler::JournalingBlockAssembler(const Config& config)
: BlockAssembler{config}, mRunFrequency{GetRunFrequency()}, mMaxSlotTransactions{GetMaxTxnBatch()},
  mNewBlockFill{GetFillAfterNewBlock()}, mThrottlingThreshold{GetThrottleThreshold()}
{
    // Create a new starting block
    newBlock();
    // Initialise our starting position
    mJournalPos = CJournal::ReadLock{mJournal}.begin();

    // Launch our main worker thread
    mFuture = std::async(std::launch::async, &JournalingBlockAssembler::threadBlockUpdate, this);
}

// Destruction
JournalingBlockAssembler::~JournalingBlockAssembler()
{
    mPromise.set_value(); // Tell worker to finish
    mFuture.wait();       // Wait for worker to finish
}


// (Re)read our configuration parameters (for unit testing)
void JournalingBlockAssembler::ReadConfigParameters()
{
    // Get config values
    mMaxSlotTransactions = GetMaxTxnBatch();
    mNewBlockFill = GetFillAfterNewBlock();
}


// Construct a new block template with coinbase to scriptPubKeyIn
std::unique_ptr<CBlockTemplate> JournalingBlockAssembler::CreateNewBlock(const CScript& scriptPubKeyIn, CBlockIndex*& pindexPrev)
{
    CBlockRef block { std::make_shared<CBlock>() };

    // Get tip we're builing on
    LOCK(cs_main);
    CBlockIndex* pindexPrevNew { chainActive.Tip() };

    {
        std::unique_lock<std::mutex> lock { mMtx };

        // Get our best block even if the background thread hasn't run for a while
        uint64_t maxTxns { mNewBlockFill? std::numeric_limits<uint64_t>::max() : static_cast<uint64_t>(mMaxSlotTransactions.load() * 1.5) };
        updateBlock(pindexPrevNew, maxTxns);
        // Copy our current transactions into the block
        block->vtx = mBlockTxns;
    }

    // Fill in the block header fields
    FillBlockHeader(block, pindexPrevNew, scriptPubKeyIn, mState.mBlockFees);

    // If required, check block validity
    if(mConfig.GetTestBlockCandidateValidity())
    {
        CValidationState state {};
        BlockValidationOptions validationOptions = BlockValidationOptions()
            .withCheckPoW(false)
            .withCheckMerkleRoot(false)
            .withMarkChecked(true);
        if(!TestBlockValidity(mConfig, state, *block, pindexPrevNew, validationOptions))
        {
            throw std::runtime_error(strprintf("%s: TestBlockValidity failed: %s",
                                               __func__, FormatStateMessage(state)));
        }
    }

    BlockStats blockStats {
        block->vtx.size() - 1,
        GetSerializeSize(*block, SER_NETWORK, PROTOCOL_VERSION) };

    LogPrintf("JournalingBlockAssembler::CreateNewBlock(): total size: %u txs: %u fees: %ld sigops %d\n",
        blockStats.blockSize, blockStats.txCount, mState.mBlockFees, mState.mBlockSigOps);

    mLastBlockStats = blockStats;

    // Build template
    std::unique_ptr<CBlockTemplate> blockTemplate { std::make_unique<CBlockTemplate>(block) };
    blockTemplate->vTxFees = mTxFees;
    blockTemplate->vTxFees[0] = -1 * mState.mBlockFees;

    // Can now update callers pindexPrev
    pindexPrev = pindexPrevNew;
    mRecentlyUpdated = false;

    return blockTemplate;
}

// Get the maximum generated block size for the current config and chain tip
uint64_t JournalingBlockAssembler::GetMaxGeneratedBlockSize() const
{
    return ComputeMaxGeneratedBlockSize(chainActive.Tip());
}

// Thread entry point for block update processing
void JournalingBlockAssembler::threadBlockUpdate() noexcept
{
    try
    {
        LogPrint(BCLog::JOURNAL, "JournalingBlockAssembler thread starting\n");
        const auto future { mPromise.get_future() };
        while(true)
        {
            // Run every few seconds or until stopping
            const auto status = future.wait_for(mRunFrequency);
            if(status == std::future_status::timeout)
            {
                // Update block template
                std::unique_lock<std::mutex> lock { mMtx };
                updateBlock(chainActive.Tip(), mMaxSlotTransactions);
            }
            else if(status == std::future_status::ready)
            {
                break;
            }
        }

        LogPrint(BCLog::JOURNAL, "JournalingBlockAssembler thread stopping\n");
    }
    catch(...)
    {
        LogPrint(BCLog::JOURNAL, "Unexpected exception in JournalingBlockAssembler thread\n");
    }
}

// Update our block template with some new transactions - Caller holds mutex
void JournalingBlockAssembler::updateBlock(const CBlockIndex* pindex, uint64_t maxTxns)
{
    uint64_t txnNum {0};

    try
    {
        // Update chain state
        if(pindex)
        {
            int32_t height { pindex->GetHeight() + 1 };
            mLockTimeCutoff = (StandardNonFinalVerifyFlags(IsGenesisEnabled(mConfig, height)) & LOCKTIME_MEDIAN_TIME_PAST) ?
                pindex->GetMedianTimePast() : GetAdjustedTime();
        }

        // Lock journal to prevent changes while we iterate over it
        CJournal::ReadLock journalLock { mJournal };

        // Does our journal or iterator need replacing?
        while(!mJournal->getCurrent() || !mJournalPos.valid())
        {
            // Release old lock, update journal/block, take new lock
            journalLock = CJournal::ReadLock {};
            newBlock();
            journalLock = CJournal::ReadLock { mJournal };

            // Reset our position to the start of the journal
            mJournalPos = journalLock.begin();
        }

        // Reposition our journal index incase we were previously at the end and now
        // some new additions have arrived.
        mJournalPos.reset();

        // If we're throttling then only update once / second
        if(mEnteredThrottling && mLastUpdateTime >= GetTime())
        {
            return;
        }

        // Read and process transactions from the journal until either we've done as many
        // as we allow this go or we reach the end of the journal.
        CJournal::Index journalEnd { journalLock.end() };
        bool finished { mJournalPos == journalEnd };
        // ComputeMaxGeneratedBlockSize depends on two values (GetMaxGeneratedBlockSize and GetMaxBlockSize) 
        // each of which can be updated independently (two RPC functions). 
        // To properly solve this, we should replace two RPC functions 
        // with one that updates both values under the lock and change getters/setters in Config class so that 
        // both values are returned or changed. But we cannot remove a RPC function and 
        // this is also unlikely to be a problem in practice.
        // maxBlockSizeComputed is stored here to keep the same value throughout
        // the whole execution and to avoid locking/unlocking mutex too many times.
        uint64_t maxBlockSizeComputed { ComputeMaxGeneratedBlockSize(pindex) };
        uint64_t throttleLimit { (maxBlockSizeComputed / 100) * mThrottlingThreshold};
        while(!finished)
        {
            // Try to add another txn or a whole group of txns to the block
            AddTransactionResult res { addTransactionOrGroup(pindex, journalEnd, maxBlockSizeComputed) };

            // If we're above the throttling threshold then only add max 1 txn (or group) / sec
            if(mState.mBlockSize >= throttleLimit)
            {
                if(!mEnteredThrottling)
                {
                    // Log when we start throttling
                    LogPrint(BCLog::JOURNAL, "JournalingBlockAssembler started throttling\n");
                }
                mEnteredThrottling = true;
                maxTxns = static_cast<uint64_t>(GetTime() - mLastUpdateTime);
            }

            if(res.result == AddTransactionResult::Result::SUCCESS)
            {
                txnNum += res.numAdded;
                mLastUpdateTime = GetTime();
                mRecentlyUpdated = true;
            }

            // We're finished if we've hit an error, reached the end of the journal,
            // or we've added as many transactions this iteration as we're allowed.
            finished = (res.result == AddTransactionResult::Result::ERRORED ||
                        mJournalPos == journalEnd ||
                        txnNum >= maxTxns);
        }
    }
    catch(std::exception& e)
    {
        LogPrint(BCLog::JOURNAL, "JournalingBlockAssembler caught: %s\n", e.what());
    }

    if(txnNum > 0)
    {
        LogPrint(BCLog::JOURNAL, "JournalingBlockAssembler processed %llu transactions from the journal\n", txnNum);
    }
}

// Get (and reset) whether we might produce an updated template
bool JournalingBlockAssembler::GetTemplateUpdated()
{
    // Get and reset latch
    return mRecentlyUpdated.exchange(false);
}

// Create a new block for us to start working on - Caller holds mutex
void JournalingBlockAssembler::newBlock()
{
    LogPrint(BCLog::JOURNAL, "JournalingBlockAssembler replacing journal/iterator/block\n");

    // Get new current journal
    mJournal = mempool.getJournalBuilder().getCurrentJournal();

    // Reset transaction list
    mBlockTxns.clear();
    mTxFees.clear();

    // Reset other accounting information
    mState.mBlockFees = Amount{0};
    mState.mBlockSigOps = COINBASE_SIG_OPS;
    mState.mBlockSize = COINBASE_SIZE;

    // Add dummy coinbase as first transaction
    mBlockTxns.emplace_back();
    mTxFees.emplace_back(Amount{-1});

    // Set updated flag
    mRecentlyUpdated = true;

    // Reset entered throttling flag
    mEnteredThrottling = false;

    // Clear any old managed groups
    mGroupBuilder.Clear();
}

JournalingBlockAssembler::AddTransactionResult JournalingBlockAssembler::addTransactionOrGroup(
    const CBlockIndex* pindex,
    const CJournal::Index& journalEnd,
    uint64_t maxBlockSizeComputed)
{
    // Create checkpoint in case we need to rollback
    GroupCheckpoint checkpoint { *this };

    // Deal with any transaction grouping requirements
    std::optional<TxnGroupID> groupID { std::nullopt };
    const auto& cpfpGroup { mJournalPos.at().getGroupId() };
    if(cpfpGroup)
    {
        // Add all CPFP group members to the same txn group
        while(mJournalPos != journalEnd && cpfpGroup == mJournalPos.at().getGroupId())
        {
            const CJournalEntry& entry { mJournalPos.at() };
            groupID = mGroupBuilder.AddTxn(entry, groupID);
            ++mJournalPos;
        }
    }
    else
    {
        // Handle single txn
        groupID = mGroupBuilder.AddTxn(mJournalPos.at());
        ++mJournalPos;
    }

    // If we're currently throttling, then we need at least 1 of the txns
    // from this group we're about to add to be non-selfish.
    if(mEnteredThrottling)
    {
        if(mGroupBuilder.GetGroup(groupID.value()).IsSelfish(mConfig))
        {
            // All txns are selfish in this group, skip it
            return { AddTransactionResult::Result::SKIPPED };
        }
    }

    // Try to add all txns from the group we have ended up with
    size_t numAdded {0};
    for(const auto& txn : mGroupBuilder.GetGroup(groupID.value()))
    {
        AddTransactionResult res { addTransaction(pindex, maxBlockSizeComputed, txn) };
        if(! (res.result == AddTransactionResult::Result::SUCCESS))
        {
            // Couldn't add this txn from the group, so rollback entire group
            checkpoint.rollback();
            return res;
        }
        ++numAdded;
    }

    // Commit group
    checkpoint.commit();
    mGroupBuilder.RemoveGroup(groupID.value());

    // Return success with number of txns added
    return { AddTransactionResult::Result::SUCCESS, numAdded };
}

JournalingBlockAssembler::GroupCheckpoint::GroupCheckpoint(JournalingBlockAssembler& assembler)
: mAssembler {assembler}
, mAssemblerStateCheckpoint {assembler.mState}
, mBlockTxnsCheckpoint {assembler.mBlockTxns}
, mTxFeesCheckpoint {assembler.mTxFees}
{
}

void JournalingBlockAssembler::GroupCheckpoint::rollback()
{
    if(!mShouldRollback)
    {
        return;
    }
    mShouldRollback = false;
    mAssembler.mState = mAssemblerStateCheckpoint;
    mBlockTxnsCheckpoint.trimToSize();
    mTxFeesCheckpoint.trimToSize();
}

// Test whether we can add another transaction to the next block, and if
// so do it - Caller holds mutex
JournalingBlockAssembler::AddTransactionResult JournalingBlockAssembler::addTransaction(
    const CBlockIndex* pindex,
    uint64_t maxBlockSizeComputed,
    const CJournalEntry& entry)
{
    // Check for block being full
    uint64_t txnSize { entry.getTxnSize() };
    uint64_t blockSizeWithTx { mState.mBlockSize + txnSize };
    if(blockSizeWithTx >= maxBlockSizeComputed)
    {
        return { AddTransactionResult::Result::BLOCKFULL };
    }

    // FIXME: We may read the transaction from disk and then throw
    //        it away if the contextual check fails.
    const auto txn = entry.getTxn()->GetTx();
    if(txn == nullptr)
    {
        LogPrint(BCLog::JOURNAL, "JournalingBlockAssembler found stale wrapper in the journal. need to start over.\n");
        return { AddTransactionResult::Result::ERRORED };
    }

    // Must check that lock times are still valid
    if(pindex)
    {
        CValidationState state {};
        if(!ContextualCheckTransaction(mConfig, *txn, state, pindex->GetHeight() + 1, mLockTimeCutoff, false))
        {
            // Can try skipping this txn
            return { AddTransactionResult::Result::SKIPPED };
        }
    }

    // Append next txn to the block template
    mBlockTxns.emplace_back(txn);
    mTxFees.emplace_back(entry.getFee());

    // Update block accounting details
    mState.mBlockSize = blockSizeWithTx;
    mState.mBlockFees += entry.getFee();

    return { AddTransactionResult::Result::SUCCESS, 1 };
}

