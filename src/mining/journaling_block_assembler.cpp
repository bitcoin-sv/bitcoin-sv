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

using mining::CBlockTemplate;
using mining::JournalingBlockAssembler;

// Construction
JournalingBlockAssembler::JournalingBlockAssembler(const Config& config)
: BlockAssembler{config}
{
    // Create a new starting block
    newBlock();
    // Initialise our starting position
    mJournalPos = CJournal::ReadLock{mJournal}.begin();

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
        updateBlock(pindexPrevNew);
        // Copy our current transactions into the block
        block->vtx = mBlockTxns;
    }

    // Fill in the block header fields
    FillBlockHeader(block, pindexPrevNew, scriptPubKeyIn);

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

    uint64_t serializeSize { GetSerializeSize(*block, SER_NETWORK, PROTOCOL_VERSION) };
    LogPrintf("JournalingBlockAssembler::CreateNewBlock(): total size: %u txs: %u fees: %ld sigops %d\n",
        serializeSize, block->vtx.size() - 1, mBlockFees, mBlockSigOps);

    bool isGenesisEnabled = IsGenesisEnabled(mConfig, chainActive.Height() + 1);
    bool sigOpCountError;

    // Build template
    std::unique_ptr<CBlockTemplate> blockTemplate { std::make_unique<CBlockTemplate>(block) };
    blockTemplate->vTxFees = mTxFees;
    blockTemplate->vTxSigOpsCount = mTxSigOpsCount;
    blockTemplate->vTxFees[0] = -1 * mBlockFees;

    int64_t txSigOpCount = static_cast<int64_t>(GetSigOpCountWithoutP2SH(*block->vtx[0], isGenesisEnabled, sigOpCountError));
    // This can happen if supplied coinbase scriptPubKeyIn contains multisig with too many public keys
    if (sigOpCountError)
    {
        blockTemplate = nullptr;
    }
    else
    {
        blockTemplate->vTxSigOpsCount[0] = txSigOpCount;
    }
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
                // Get chain tip
                const CBlockIndex* pindex {nullptr};
                {
                    LOCK(cs_main);
                    pindex = chainActive.Tip();
                }

                // Update block template
                std::unique_lock<std::mutex> lock { mMtx };
                updateBlock(pindex);
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
void JournalingBlockAssembler::updateBlock(const CBlockIndex* pindex)
{
    size_t txnNum {0};

    try
    {
        // Update chain state
        if(pindex)
        {
            int height { pindex->nHeight + 1 };
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

        // Read and process transactions from the journal until either we've done as many
        // as we allow this go or we reach the end of the journal.
        bool finished { mJournalPos == journalLock.end() };
        while(!finished)
        {
            // Try to add another txn to the block
            if(addTransaction(pindex))
            {
                ++txnNum;

                // We're finished if we've reached the end of the journal, or we've added
                // as many transactions this iteration as we're allowed.
                finished = (mJournalPos == journalLock.end() || txnNum >= mMaxTransactions);
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
        LogPrint(BCLog::JOURNAL, "JournalingBlockAssembler processed %d transactions from the journal\n", txnNum);
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
    mJournal = mempool.getJournalBuilder()->getCurrentJournal();

    // Reset transaction list
    mBlockTxns.clear();
    mTxFees.clear();
    mTxSigOpsCount.clear();

    // Reset other accounting information
    mBlockFees = Amount{0};
    mBlockSigOps = COINBASE_SIG_OPS;
    mBlockSize = COINBASE_SIZE;

    // Add dummy coinbase as first transaction
    mBlockTxns.emplace_back();
    mTxFees.emplace_back(Amount{-1});
    mTxSigOpsCount.emplace_back(-1);

    // Set updated flag
    mRecentlyUpdated = true;
}

// Test whether we can add another transaction to the next block, and if
// so do it - Caller holds mutex
bool JournalingBlockAssembler::addTransaction(const CBlockIndex* pindex)
{
    const CJournalEntry& entry { mJournalPos.at() };
    const CTransactionRef& txn { entry.getTxn() };

    // Check for block being full
    uint64_t maxBlockSize { ComputeMaxGeneratedBlockSize(pindex) };
    uint64_t txnSize { txn->GetTotalSize() };
    uint64_t blockSizeWithTx { mBlockSize + txnSize };
    if(blockSizeWithTx >= maxBlockSize)
    {
        return false;
    }

    uint64_t txnSigOps{ static_cast<uint64_t>(entry.getSigOpsCount()) };
    uint64_t blockSigOpsWithTx{ mBlockSigOps + txnSigOps };

    // After Genesis we don't count sigops anymore
    if (!IsGenesisEnabled(mConfig, pindex->nHeight + 1))
    {
        // Check sig ops count
        uint64_t maxBlockSigOps = mConfig.GetMaxBlockSigOpsConsensusBeforeGenesis(blockSizeWithTx);
     
        if (blockSigOpsWithTx >= maxBlockSigOps)
        {
            return false;
        }
    }

    // Must check that lock times are still valid
    if(pindex)
    {
        CValidationState state {};
        if(!ContextualCheckTransaction(mConfig, *txn, state, pindex->nHeight + 1, mLockTimeCutoff, false))
        {
            return false;
        }
    }

    // Append next txn to the block template
    mBlockTxns.emplace_back(txn);
    mTxFees.emplace_back(entry.getFee());
    mTxSigOpsCount.emplace_back(entry.getSigOpsCount());

    // Update block accounting details
    mBlockSize = blockSizeWithTx;
    mBlockSigOps = blockSigOpsWithTx;
    mBlockFees += entry.getFee();

    // Set updated flag
    mRecentlyUpdated = true;

    // Move to the next item in the journal
    ++mJournalPos;

    return true;
}

