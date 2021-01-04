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
}

// Construction
JournalingBlockAssembler::JournalingBlockAssembler(const Config& config)
: BlockAssembler{config}, mMaxSlotTransactions{GetMaxTxnBatch()}, mNewBlockFill{GetFillAfterNewBlock()}
{
    // Create a new starting block
    newBlock();
    // Initialise our starting position
    mState.mJournalPos = CJournal::ReadLock{mJournal}.begin();

    // Launch our main worker thread
    future_ = std::async(std::launch::async,
                         &JournalingBlockAssembler::threadBlockUpdate, this);
}

// Destruction
JournalingBlockAssembler::~JournalingBlockAssembler()
{
    promise_.set_value(); // Tell worker to finish
    future_.wait();       // Wait for worker to finish
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
        updateBlock(pindexPrevNew, mNewBlockFill? std::numeric_limits<uint64_t>::max() : mMaxSlotTransactions.load());
        // Copy our current transactions into the block
        block->vtx = mBlockTxns;
    }

    // Fill in the block header fields
    FillBlockHeader(block, pindexPrevNew, scriptPubKeyIn, mState.mBlockFees);

    // If required, check block validity
    if(mConfig.GetTestBlockCandidateValidity())
    {
        CValidationState state {};
        BlockValidationOptions validationOptions { false, false, true };
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
    LOCK(cs_main);
    return ComputeMaxGeneratedBlockSize(chainActive.Tip());
}

// Thread entry point for block update processing
void JournalingBlockAssembler::threadBlockUpdate() noexcept
{
    try
    {
        LogPrint(BCLog::JOURNAL, "JournalingBlockAssembler thread starting\n");
        const auto future{promise_.get_future()};
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
                break;
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
            int32_t height { pindex->nHeight + 1 };
            mLockTimeCutoff = (StandardNonFinalVerifyFlags(IsGenesisEnabled(mConfig, height)) & LOCKTIME_MEDIAN_TIME_PAST) ?
                pindex->GetMedianTimePast() : GetAdjustedTime();
        }

        // Lock journal to prevent changes while we iterate over it
        CJournal::ReadLock journalLock { mJournal };

        // Does our journal or iterator need replacing?
        while(!mJournal->getCurrent() || !mState.mJournalPos.valid())
        {
            // Release old lock, update journal/block, take new lock
            journalLock = CJournal::ReadLock {};
            newBlock();
            journalLock = CJournal::ReadLock { mJournal };

            // Reset our position to the start of the journal
            mState.mJournalPos = journalLock.begin();
        }

        // Reposition our journal index incase we were previously at the end and now
        // some new additions have arrived.
        mState.mJournalPos.reset();

        // Read and process transactions from the journal until either we've done as many
        // as we allow this go or we reach the end of the journal.
        CJournal::Index journalEnd {journalLock.end()};
        bool finished { mState.mJournalPos == journalEnd };

        while(!finished)
        {
            // Try to add another txn or a whole group of txns to the block
            // mMaxTransactions is an internal limit used to reduce lock contention
            // When we're adding a group we may add more transactions and that's OK
            size_t nAdded = addTransactionOrGroup(pindex, journalEnd);
            if(nAdded)
            {
                txnNum += nAdded;

                // Set updated flag
                mRecentlyUpdated = true;

                // We're finished if we've reached the end of the journal, or we've added
                // as many transactions this iteration as we're allowed.
                finished = (mState.mJournalPos == journalEnd  || txnNum >= maxTxns);
            }
            else
            {
                // We're also finished once we can't add any more transactions.
                finished = true;
            }
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
}

size_t JournalingBlockAssembler::addTransactionOrGroup(const CBlockIndex* pindex, const CJournal::Index& journalEnd)
{
    auto& groupId { mState.mJournalPos.at().getGroupId() };
    if (!groupId)
    {
        return addTransaction(pindex);
    }
    else
    {
        GroupCheckpoint checkpoint {*this};
        size_t nAddedTotal {0};
        while (mState.mJournalPos != journalEnd && groupId == mState.mJournalPos.at().getGroupId()) {
            size_t nAdded = addTransaction(pindex);
            if (!nAdded) {
                checkpoint.rollback();
                return 0;
            }
            nAddedTotal += nAdded;
        }
        checkpoint.commit();
        return nAddedTotal;
    }
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
    if (!mShouldRollback) {
        return;
    }
    mShouldRollback = false;
    mAssembler.mState = mAssemblerStateCheckpoint;
    mBlockTxnsCheckpoint.trimToSize();
    mTxFeesCheckpoint.trimToSize();
}

// Test whether we can add another transaction to the next block, and if
// so do it - Caller holds mutex
size_t JournalingBlockAssembler::addTransaction(const CBlockIndex* pindex)
{
    const CJournalEntry& entry { mState.mJournalPos.at() };

    // Check for block being full
    uint64_t maxBlockSize { ComputeMaxGeneratedBlockSize(pindex) };
    uint64_t txnSize { entry.getTxnSize() };
    uint64_t blockSizeWithTx { mState.mBlockSize + txnSize };
    if(blockSizeWithTx >= maxBlockSize)
    {
        return 0;
    }

    // FIXME: We may read the transaction from disk and then throw
    //        it away if the contextual check fails.
    const auto txn = entry.getTxn()->GetTx();
    if (txn == nullptr) {
        LogPrint(BCLog::JOURNAL, "JournalingBlockAssembler found stale wrapper in the journal. need to start over.\n");
        return 0;
    }

    // Must check that lock times are still valid
    if(pindex)
    {
        CValidationState state {};
        if(!ContextualCheckTransaction(mConfig, *txn, state, pindex->nHeight + 1, mLockTimeCutoff, false))
        {
            return 0;
        }
    }

    // Append next txn to the block template
    mBlockTxns.emplace_back(txn);
    mTxFees.emplace_back(entry.getFee());

    // Update block accounting details
    mState.mBlockSize = blockSizeWithTx;
    mState.mBlockFees += entry.getFee();

    // Move to the next item in the journal
    ++mState.mJournalPos;

    return 1;
}

