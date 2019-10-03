// Copyright (c) 2019 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include <mining/assembler.h>
#include <mining/journal.h>

#include <future>
#include <mutex>

namespace mining
{

/**
* A mining candidate builder that utilises the mempool journal.
*/
class JournalingBlockAssembler : public BlockAssembler
{
  public:

    // Construction/destruction
    JournalingBlockAssembler(const Config& config);
    ~JournalingBlockAssembler();

    // Construct a new block template with coinbase to scriptPubKeyIn
    std::unique_ptr<CBlockTemplate> CreateNewBlock(const CScript& scriptPubKeyIn, CBlockIndex*& pindexPrev) override;

    // Get the maximum generated block size for the current config and chain tip
    uint64_t GetMaxGeneratedBlockSize() const override;

    // Get (and reset) whether we might produce an updated template
    bool GetTemplateUpdated() override;

  private:

    // Thread entry point for block update processing
    void threadBlockUpdate() noexcept;

    // Update our block template with some new transactions
    void updateBlock(const CBlockIndex* pindex);

    // Create a new block for us to start working on
    void newBlock();

    // Test whether we can add another transaction to the next block and if so do it
    bool addTransaction(const CBlockIndex* pindex);

    // Our internal mutex
    mutable std::mutex mMtx {};

    // All details for the block we are currently building
    static constexpr uint64_t COINBASE_SIG_OPS {100};
    static constexpr uint64_t COINBASE_SIZE {1000};
    std::vector<CTransactionRef> mBlockTxns {};
    uint64_t mBlockSigOps {COINBASE_SIG_OPS};
    uint64_t mBlockSize {COINBASE_SIZE};
    std::vector<Amount> mTxFees {};
    std::vector<int64_t> mTxSigOpsCount {};

    // Chain context for the block
    int64_t mLockTimeCutoff {0};

    // Worker thread management
    std::future<void> future_;
    std::promise<void> promise_;

    // Frequency we run
    static constexpr unsigned DEFAULT_RUN_FREQUENCY_MILLIS {100};
    std::chrono::milliseconds mRunFrequency {DEFAULT_RUN_FREQUENCY_MILLIS};

    // Maximum number of transactions to process per time slot
    static constexpr size_t DEFAULT_MAX_TRANSACTIONS {1000};
    size_t mMaxTransactions {DEFAULT_MAX_TRANSACTIONS};

    // The journal we're reading from and our current position in that journal
    CJournalPtr mJournal {nullptr};
    CJournal::Index mJournalPos {};

    // Flag to indicate whether we have been updated
    std::atomic_bool mRecentlyUpdated {false};
};
}
