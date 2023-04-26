// Copyright (c) 2022 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "config.h"
#include "mining/assembler.h"
#include "mining/journaling_block_assembler.h"
#include "pow.h"
#include "rpc/mining.h"

#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>

using mining::BlockAssembler;
using mining::BlockAssemblerRef;
using mining::CBlockTemplate;
using mining::CMiningFactory;
using mining::JournalingBlockAssembler;

namespace
{
    // Only used as unique identifier
    class jba_tests_uid;

    // Testing fixture that creates a REGTEST-mode block chain with spendable coins
    struct SetupJBAChain : public TestChain100Setup
    {
        SetupJBAChain() : TestChain100Setup{}
        {
            // Create us some spendable coinbase txns
            for(int i = 0; i < 25; ++i)
            {
                CreateAndProcessBlock(nullptr);
                CTransactionRef conbaseTxn { MakeTransactionRef(coinbaseTxns[i]) };
                mFundingTxns.push_back(conbaseTxn);
            }

            // Make sure our JBA only runs when polled by CreateNewBlock
            gArgs.ForceSetArg("-jbarunfrequency", std::to_string(std::numeric_limits<unsigned>::max()));

            // Limit max block size to something small so we can easily approach it
            testConfig.SetMaxGeneratedBlockSize(ONE_MEGABYTE);

            // Enable block template validity checking
            testConfig.SetTestBlockCandidateValidity(true);
        }

        // Create a new block and add it to the blockchain
        CBlock CreateAndProcessBlock(BlockAssemblerRef assembler)
        {
            // If no assembler specified, use the global one
            if(!assembler)
            {
                assembler = mining::g_miningFactory->GetAssembler();
            }

            CBlockIndex* pindexPrev {nullptr};
            const auto& pblocktemplate { assembler->CreateNewBlock(coinbaseScriptPubKey, pindexPrev) };
            CBlockRef blockRef { pblocktemplate->GetBlockRef() };
            CBlock& block { *blockRef };

            // IncrementExtraNonce creates a valid coinbase
            unsigned int extraNonce {0};
            IncrementExtraNonce(&block, pindexPrev, extraNonce);

            // Solve block
            while(!CheckProofOfWork(block.GetHash(), block.nBits, testConfig))
            {
                ++block.nNonce;
            }

            int32_t oldHeight { chainActive.Height() };
            BOOST_CHECK(ProcessNewBlock(testConfig, blockRef, true, nullptr, CBlockSource::MakeLocal("test")));
            BOOST_CHECK_EQUAL(chainActive.Height(), oldHeight + 1);
            coinbaseTxns.push_back(*(block.vtx[0]));

            return block;
        }

        // Build and submit txn to mempool
        CTransactionRef SubmitTxn(const std::vector<CTransactionRef>& fundingTxns,
                                  const Amount& fee,
                                  size_t padding = ONE_KIBIBYTE*50)
        {
            CMutableTransaction txn {};
            std::vector<Amount> values {};
            for(const auto& fundingTxn : fundingTxns)
            {
                txn.vin.emplace_back(CTxIn { COutPoint { fundingTxn->GetId(), 0 }, CScript() });
                values.push_back(fundingTxn->vout[0].nValue);
            }
            txn.vout.resize(1);
            txn.vout[0].nValue = std::accumulate(values.begin(), values.end(), Amount{0}) - fee;
            txn.vout[0].scriptPubKey = coinbaseScriptPubKey;

            // Padding if required
            if(padding > 0)
            {
                txn.vout.resize(2);
                txn.vout[1].nValue = Amount{0};
                txn.vout[1].scriptPubKey = CScript() << OP_FALSE << OP_RETURN << std::vector<uint8_t>(padding);
            }

            // Sign inputs
            for(unsigned i = 0; i < txn.vin.size(); ++i)
            {
                std::vector<uint8_t> vchSig {};
                uint256 hash = SignatureHash(coinbaseScriptPubKey, CTransaction{txn}, i, SigHashType().withForkId(), values[i]);
                BOOST_CHECK(coinbaseKey.Sign(hash, vchSig));
                vchSig.push_back(uint8_t(SIGHASH_ALL | SIGHASH_FORKID));
                txn.vin[i].scriptSig << vchSig;
            }

            CTransactionRef txnRef { MakeTransactionRef(std::move(txn)) };

            // Submit to mempool so it gets included in next block
            auto beforeSize { mempool.Size() };
            auto pTxInputData {
                std::make_shared<CTxInputData>(
                    connman->GetTxIdTracker(),
                    txnRef,
                    TxSource::rpc,
                    TxValidationPriority::normal,
                    TxStorage::memory,
                    GetTime())
            };
            mining::CJournalChangeSetPtr changeSet {nullptr};
            const auto& status { connman->getTxnValidator()->processValidation(pTxInputData, changeSet) };
            BOOST_CHECK(status.IsValid());
            BOOST_CHECK_EQUAL(mempool.Size(), beforeSize + 1);
            return txnRef;
        }

        // Create and add a txn to the mempool
        using OptTxnVec = std::optional<std::reference_wrapper<std::vector<CTransactionRef>>>;
        CTransactionRef AddSingleTransaction(std::vector<CTransactionRef> fundingTxns = {},
                                             OptTxnVec txnStore = std::nullopt)
        {
            // Fetch funding txn if not provided
            if(fundingTxns.empty() || (fundingTxns.size() == 1 && fundingTxns[0] == nullptr))
            {
                assert(! mFundingTxns.empty());
                fundingTxns = { mFundingTxns.front() };
                mFundingTxns.erase(mFundingTxns.begin());
            }

            // Add with sufficient fee
            CTransactionRef txn { SubmitTxn(fundingTxns, mTxnFee) };
            if(txnStore)
            {
                txnStore.value().get().push_back(txn);
            }
            return txn;
        }

        // Create and add a CPFP group of txns to the mempool
        CTransactionRef AddCPFPTransactions(unsigned groupLength,
                                            CTransactionRef fundingTxn = nullptr,
                                            OptTxnVec txnStore = std::nullopt)
        {
            assert(groupLength >= 2);

            // Fetch funding txn if not provided
            if(! fundingTxn)
            {
                assert(! mFundingTxns.empty());
                fundingTxn = mFundingTxns.front();
                mFundingTxns.erase(mFundingTxns.begin());
            }

            // Create chain of low paying parents
            for(unsigned i = 0; i < groupLength - 1; ++i)
            {
                fundingTxn = SubmitTxn({fundingTxn}, Amount{1});
                if(txnStore)
                {
                    txnStore.value().get().push_back(fundingTxn);
                }
            }

            // Add paying child
            CTransactionRef finalTxn { SubmitTxn({fundingTxn}, static_cast<int>(groupLength) * mTxnFee) };
            if(txnStore)
            {
                txnStore.value().get().push_back(finalTxn);
            }
            return finalTxn;
        }

        // List of spendable txns for testing with
        std::vector<CTransactionRef> mFundingTxns {};

        // Coinbase script
        CScript coinbaseScriptPubKey { CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG };

        // Estimated fee per 50k txn we test with
        Amount mTxnFee { 50000 };
    };
}

// For inspection / modification of the JBA
template<>
struct JournalingBlockAssembler::UnitTestAccess<jba_tests_uid>
{
    static unsigned GetThrottlingThreshold(const JournalingBlockAssembler& jba)
    {
        return jba.mThrottlingThreshold;
    }
    static void SetThrottlingThreshold(JournalingBlockAssembler& jba, unsigned threshold)
    {
        jba.mThrottlingThreshold = threshold;
    }
    static bool GetEnteredThrottling(const JournalingBlockAssembler& jba)
    {
        return jba.mEnteredThrottling;
    }
};
using JBAAccess = JournalingBlockAssembler::UnitTestAccess<jba_tests_uid>;

BOOST_AUTO_TEST_SUITE(jba_selfish_mining)

// Test the basic no selfish mining prevention required (non-throttling) case
BOOST_FIXTURE_TEST_CASE(NoSelfishNoThrottling, SetupJBAChain)
{
    std::unique_ptr<CBlockTemplate> pblocktemplate {nullptr};
    CBlockIndex* pindexPrev {nullptr};
    CMiningFactory miningFactory { testConfig };
    auto jba { std::dynamic_pointer_cast<JournalingBlockAssembler>(miningFactory.GetAssembler()) };
    BOOST_REQUIRE(jba);

    // Initial block creation; nothing in the mempool or the journal
    BOOST_CHECK_EQUAL(mempool.Size(), 0U);
    BOOST_CHECK(pblocktemplate = jba->CreateNewBlock(coinbaseScriptPubKey, pindexPrev));
    BlockAssembler::BlockStats blockStats { jba->getLastBlockStats() };
    BOOST_CHECK_EQUAL(blockStats.txCount, 0U);
    BOOST_CHECK(! JBAAccess::GetEnteredThrottling(*jba));

    // Add a single standalone txn
    BOOST_CHECK(AddSingleTransaction());
    BOOST_CHECK(pblocktemplate = jba->CreateNewBlock(coinbaseScriptPubKey, pindexPrev));
    blockStats = jba->getLastBlockStats();
    BOOST_CHECK_EQUAL(blockStats.txCount, 1U);
    BOOST_CHECK(! JBAAccess::GetEnteredThrottling(*jba));

    // Add a small CPFP group
    BOOST_CHECK(AddCPFPTransactions(3));
    BOOST_CHECK(pblocktemplate = jba->CreateNewBlock(coinbaseScriptPubKey, pindexPrev));
    blockStats = jba->getLastBlockStats();
    BOOST_CHECK_EQUAL(blockStats.txCount, 4U);
    BOOST_CHECK(! JBAAccess::GetEnteredThrottling(*jba));

    // Accept the block and check JBA isn't throttling for new block
    CreateAndProcessBlock(jba);
    BOOST_CHECK_EQUAL(chainActive.Tip()->GetBlockTxCount(), 5U);
    BOOST_CHECK(pblocktemplate = jba->CreateNewBlock(coinbaseScriptPubKey, pindexPrev));
    blockStats = jba->getLastBlockStats();
    BOOST_CHECK_EQUAL(blockStats.txCount, 0U);
    BOOST_CHECK(! JBAAccess::GetEnteredThrottling(*jba));
}

// Test transaction throttling only
BOOST_FIXTURE_TEST_CASE(NoSelfishJustThrottling, SetupJBAChain)
{
    std::unique_ptr<CBlockTemplate> pblocktemplate {nullptr};
    CBlockIndex* pindexPrev {nullptr};
    CMiningFactory miningFactory { testConfig };
    auto jba { std::dynamic_pointer_cast<JournalingBlockAssembler>(miningFactory.GetAssembler()) };
    BOOST_REQUIRE(jba);

    // Set throttling threshold to 18 txns
    JBAAccess::SetThrottlingThreshold(*jba, 90);

    // Set an initial mock time
    int64_t mockTime { GetAdjustedTime() };
    SetMockTime(mockTime);

    // Put 17 txns in the mempool; should not be too many to take us over throttling threshold
    CTransactionRef fundingTxn {nullptr};
    unsigned int INITIAL_TXN_BATCH_SIZE {17};
    for(unsigned int i = 0; i < INITIAL_TXN_BATCH_SIZE; ++i)
    {
        BOOST_CHECK(fundingTxn = AddSingleTransaction({fundingTxn}));
    }
    BOOST_CHECK(pblocktemplate = jba->CreateNewBlock(coinbaseScriptPubKey, pindexPrev));
    BlockAssembler::BlockStats blockStats { jba->getLastBlockStats() };
    BOOST_CHECK_EQUAL(blockStats.txCount, INITIAL_TXN_BATCH_SIZE);
    BOOST_CHECK(! JBAAccess::GetEnteredThrottling(*jba));

    // Put another 3 transactions in the mempool; the first will be enough to make the JBA start throttling
    for(unsigned int i = 0; i < 3; ++i)
    {
        BOOST_CHECK(fundingTxn = AddSingleTransaction({fundingTxn}));
    }

    // JBA should take 1 more txn and then start throttling
    BOOST_CHECK(pblocktemplate = jba->CreateNewBlock(coinbaseScriptPubKey, pindexPrev));
    blockStats = jba->getLastBlockStats();
    BOOST_CHECK_EQUAL(blockStats.txCount, INITIAL_TXN_BATCH_SIZE + 1);
    BOOST_CHECK(JBAAccess::GetEnteredThrottling(*jba));

    // JBA won't take another txn while throttling if time hasn't moved on
    BOOST_CHECK(pblocktemplate = jba->CreateNewBlock(coinbaseScriptPubKey, pindexPrev));
    blockStats = jba->getLastBlockStats();
    BOOST_CHECK_EQUAL(blockStats.txCount, INITIAL_TXN_BATCH_SIZE + 1);
    BOOST_CHECK(JBAAccess::GetEnteredThrottling(*jba));

    // After a second the JBA should take another transaction
    mockTime += 1;
    SetMockTime(mockTime);
    BOOST_CHECK(pblocktemplate = jba->CreateNewBlock(coinbaseScriptPubKey, pindexPrev));
    blockStats = jba->getLastBlockStats();
    BOOST_CHECK_EQUAL(blockStats.txCount, INITIAL_TXN_BATCH_SIZE + 2);
    BOOST_CHECK(JBAAccess::GetEnteredThrottling(*jba));

    // The block template is now at max size so the JBA shouldn't take any more txns
    mockTime += 1;
    SetMockTime(mockTime);
    BOOST_CHECK(pblocktemplate = jba->CreateNewBlock(coinbaseScriptPubKey, pindexPrev));
    blockStats = jba->getLastBlockStats();
    BOOST_CHECK_EQUAL(blockStats.txCount, INITIAL_TXN_BATCH_SIZE + 2);
    BOOST_CHECK(JBAAccess::GetEnteredThrottling(*jba));

    // Accept the block and check JBA isn't throttling for a new block with the single remaining txn in
    constexpr unsigned COINBASE_TXN {1};
    CreateAndProcessBlock(jba);
    BOOST_CHECK_EQUAL(chainActive.Tip()->GetBlockTxCount(), INITIAL_TXN_BATCH_SIZE + 2 + COINBASE_TXN);
    BOOST_CHECK(pblocktemplate = jba->CreateNewBlock(coinbaseScriptPubKey, pindexPrev));
    blockStats = jba->getLastBlockStats();
    BOOST_CHECK_EQUAL(blockStats.txCount, 1U);
    BOOST_CHECK(! JBAAccess::GetEnteredThrottling(*jba));


    // Modify throttling threshold and lower it to 10 txns
    JBAAccess::SetThrottlingThreshold(*jba, 50);

    // Add another 7 txns to the mempool to take us near the throttling threshold (8 txns total)
    INITIAL_TXN_BATCH_SIZE = 7;
    fundingTxn = nullptr;
    for(unsigned int i = 0; i < INITIAL_TXN_BATCH_SIZE; ++i)
    {
        BOOST_CHECK(fundingTxn = AddSingleTransaction({fundingTxn}));
    }
    BOOST_CHECK(pblocktemplate = jba->CreateNewBlock(coinbaseScriptPubKey, pindexPrev));
    blockStats = jba->getLastBlockStats();
    BOOST_CHECK_EQUAL(blockStats.txCount, 1 + INITIAL_TXN_BATCH_SIZE);
    BOOST_CHECK(! JBAAccess::GetEnteredThrottling(*jba));

    // Put CPFP groups of size 3,5,5 in the mempool
    unsigned INITIAL_CPFP_GROUP_SIZE {3};
    unsigned NEXT_CPFP_GROUP_SIZE {5};
    BOOST_CHECK(AddCPFPTransactions(INITIAL_CPFP_GROUP_SIZE));
    BOOST_CHECK(AddCPFPTransactions(NEXT_CPFP_GROUP_SIZE));
    BOOST_CHECK(AddCPFPTransactions(NEXT_CPFP_GROUP_SIZE));

    // Put 1 more single standalone txn to the mempool
    AddSingleTransaction();

    // JBA should take the first group of 3 and then start throttling
    BOOST_CHECK(pblocktemplate = jba->CreateNewBlock(coinbaseScriptPubKey, pindexPrev));
    blockStats = jba->getLastBlockStats();
    BOOST_CHECK_EQUAL(blockStats.txCount, 1 + INITIAL_TXN_BATCH_SIZE + INITIAL_CPFP_GROUP_SIZE);
    BOOST_CHECK(JBAAccess::GetEnteredThrottling(*jba));

    // JBA won't take another group if time hasn't moved on
    BOOST_CHECK(pblocktemplate = jba->CreateNewBlock(coinbaseScriptPubKey, pindexPrev));
    blockStats = jba->getLastBlockStats();
    BOOST_CHECK_EQUAL(blockStats.txCount, 1 + INITIAL_TXN_BATCH_SIZE + INITIAL_CPFP_GROUP_SIZE);
    BOOST_CHECK(JBAAccess::GetEnteredThrottling(*jba));

    // After a second JBA should take next group of 5
    mockTime += 1;
    SetMockTime(mockTime);
    BOOST_CHECK(pblocktemplate = jba->CreateNewBlock(coinbaseScriptPubKey, pindexPrev));
    blockStats = jba->getLastBlockStats();
    BOOST_CHECK_EQUAL(blockStats.txCount, 1 + INITIAL_TXN_BATCH_SIZE + INITIAL_CPFP_GROUP_SIZE + NEXT_CPFP_GROUP_SIZE);
    BOOST_CHECK(JBAAccess::GetEnteredThrottling(*jba));

    // The block template is now nearly full so the JBA can't take the final group of 5,
    // but it can take the final single txn.
    mockTime += 1;
    SetMockTime(mockTime);
    BOOST_CHECK(pblocktemplate = jba->CreateNewBlock(coinbaseScriptPubKey, pindexPrev));
    blockStats = jba->getLastBlockStats();
    BOOST_CHECK_EQUAL(blockStats.txCount, 1 + INITIAL_TXN_BATCH_SIZE + INITIAL_CPFP_GROUP_SIZE + NEXT_CPFP_GROUP_SIZE + 1);
    BOOST_CHECK(JBAAccess::GetEnteredThrottling(*jba));

    // Now the JBA really can't take anything else
    mockTime += 1;
    SetMockTime(mockTime);
    BOOST_CHECK(pblocktemplate = jba->CreateNewBlock(coinbaseScriptPubKey, pindexPrev));
    blockStats = jba->getLastBlockStats();
    BOOST_CHECK_EQUAL(blockStats.txCount, 1 + INITIAL_TXN_BATCH_SIZE + INITIAL_CPFP_GROUP_SIZE + NEXT_CPFP_GROUP_SIZE + 1);
    BOOST_CHECK(JBAAccess::GetEnteredThrottling(*jba));

    // Accept the block and check JBA isn't throttling for a new block with the single remaining group in
    CreateAndProcessBlock(jba);
    BOOST_CHECK_EQUAL(chainActive.Tip()->GetBlockTxCount(), 1 + INITIAL_TXN_BATCH_SIZE + INITIAL_CPFP_GROUP_SIZE + NEXT_CPFP_GROUP_SIZE + 1 + COINBASE_TXN);
    BOOST_CHECK(pblocktemplate = jba->CreateNewBlock(coinbaseScriptPubKey, pindexPrev));
    blockStats = jba->getLastBlockStats();
    BOOST_CHECK_EQUAL(blockStats.txCount, NEXT_CPFP_GROUP_SIZE);
    BOOST_CHECK(! JBAAccess::GetEnteredThrottling(*jba));
}

// Test throttling and selfish mining prevention
BOOST_FIXTURE_TEST_CASE(SelfishAndThrottling, SetupJBAChain)
{
    std::unique_ptr<CBlockTemplate> pblocktemplate {nullptr};
    CBlockIndex* pindexPrev {nullptr};
    CMiningFactory miningFactory { testConfig };
    auto jba { std::dynamic_pointer_cast<JournalingBlockAssembler>(miningFactory.GetAssembler()) };
    BOOST_REQUIRE(jba);
    BOOST_CHECK(! JBAAccess::GetEnteredThrottling(*jba));

    // Set throttling threshold to 7 txns
    JBAAccess::SetThrottlingThreshold(*jba, 35);

    // Test mock time and calculate a selfish txn time earlier than that
    int64_t mockTime { GetAdjustedTime() };
    int64_t selfishTime { mockTime - testConfig.GetMinBlockMempoolTimeDifferenceSelfish() - 1 };

    // Somewhere to store transactions we're going to create
    std::vector<CTransactionRef> selfishSingleTxns {};
    std::vector<CTransactionRef> selfishCPFPTxns {};
    std::vector<CTransactionRef> nonSelfishCPFPTxns {};
    CTransactionRef nonSelfishSingleTxn {nullptr};

    // Lambda to check if txn is in the block
    auto IsTxnInBlock = [](const CTransactionRef& txn, const CBlockRef& block)
    {
        const auto it = std::find_if(block->vtx.begin(), block->vtx.end(), [&txn](const CTransactionRef& blockTxn)
            {
                return txn->GetId() == blockTxn->GetId();
            }
        );

        return it != block->vtx.end();
    };

    // Put 9 txns in the mempool with times that will register as selfish
    SetMockTime(selfishTime);
    unsigned int INITIAL_SELFISH_TXN_BATCH_SIZE {9};
    for(unsigned int i = 0; i < INITIAL_SELFISH_TXN_BATCH_SIZE; ++i)
    {
        BOOST_CHECK(AddSingleTransaction({}, selfishSingleTxns));
    }

    // Put 3 CPFP groups size 2 in the mempool with time that will register as selfish
    unsigned int SELFISH_CPFP_GROUP_SIZE {2};
    BOOST_CHECK(AddCPFPTransactions(SELFISH_CPFP_GROUP_SIZE, nullptr, selfishCPFPTxns));
    BOOST_CHECK(AddCPFPTransactions(SELFISH_CPFP_GROUP_SIZE, nullptr, selfishCPFPTxns));
    BOOST_CHECK(AddCPFPTransactions(SELFISH_CPFP_GROUP_SIZE, nullptr, selfishCPFPTxns));

    // Put another txn in mempool with current time
    SetMockTime(mockTime);
    BOOST_CHECK(nonSelfishSingleTxn = AddSingleTransaction());

    // Put CPFP group size 2 in the mempool with current time
    unsigned int NON_SELFISH_CPFP_GROUP_SIZE {2};
    BOOST_CHECK(AddCPFPTransactions(NON_SELFISH_CPFP_GROUP_SIZE, nullptr, nonSelfishCPFPTxns));

    BOOST_CHECK_EQUAL(selfishSingleTxns.size(), 9U);
    BOOST_CHECK_EQUAL(selfishCPFPTxns.size(), 6U);
    BOOST_CHECK_EQUAL(nonSelfishCPFPTxns.size(), 2U);


    // JBA will take 7 txns then enter throttling
    BOOST_CHECK(pblocktemplate = jba->CreateNewBlock(coinbaseScriptPubKey, pindexPrev));
    BlockAssembler::BlockStats blockStats { jba->getLastBlockStats() };
    BOOST_CHECK_EQUAL(blockStats.txCount, INITIAL_SELFISH_TXN_BATCH_SIZE - 2);
    BOOST_CHECK(JBAAccess::GetEnteredThrottling(*jba));

    // Even if time moves on JBA will not take a selfish single txn or selfish CPFP group
    mockTime += 1;
    SetMockTime(mockTime);
    BOOST_CHECK(pblocktemplate = jba->CreateNewBlock(coinbaseScriptPubKey, pindexPrev));
    BOOST_CHECK(! IsTxnInBlock(selfishSingleTxns[7], pblocktemplate->GetBlockRef()));
    BOOST_CHECK(! IsTxnInBlock(selfishSingleTxns[8], pblocktemplate->GetBlockRef()));
    for(const auto& selfishTxn : selfishCPFPTxns)
    {
        BOOST_CHECK(! IsTxnInBlock(selfishTxn, pblocktemplate->GetBlockRef()));
    }

    // JBA has taken 10th single txn (skipping selfish 8th/9th and selfish CPFP group)
    blockStats = jba->getLastBlockStats();
    BOOST_CHECK_EQUAL(blockStats.txCount, INITIAL_SELFISH_TXN_BATCH_SIZE - 2 + 1);
    BOOST_CHECK(IsTxnInBlock(nonSelfishSingleTxn, pblocktemplate->GetBlockRef()));

    // Time moves on another second and JBA takes non-selfish CPFP group
    mockTime += 1;
    SetMockTime(mockTime);
    BOOST_CHECK(pblocktemplate = jba->CreateNewBlock(coinbaseScriptPubKey, pindexPrev));
    blockStats = jba->getLastBlockStats();
    BOOST_CHECK_EQUAL(blockStats.txCount, INITIAL_SELFISH_TXN_BATCH_SIZE - 2 + 1 + NON_SELFISH_CPFP_GROUP_SIZE);
    for(const auto& txn : nonSelfishCPFPTxns)
    {
        BOOST_CHECK(IsTxnInBlock(txn, pblocktemplate->GetBlockRef()));
    }

    // JBA takes nothing more even if time moves on
    mockTime += 1;
    SetMockTime(mockTime);
    BOOST_CHECK(pblocktemplate = jba->CreateNewBlock(coinbaseScriptPubKey, pindexPrev));
    blockStats = jba->getLastBlockStats();
    BOOST_CHECK_EQUAL(blockStats.txCount, INITIAL_SELFISH_TXN_BATCH_SIZE - 2 + 1 + NON_SELFISH_CPFP_GROUP_SIZE);


    // Add txn with current time that spends selfish individual txn
    CTransactionRef nonSelfishSpendingTxn {nullptr};
    BOOST_CHECK(nonSelfishSpendingTxn = AddSingleTransaction({selfishSingleTxns[7]}));

    // JBA now takes selfish individual txn and the spending child (time has already moved on
    // sufficiently even though we are throttling)
    BOOST_CHECK(pblocktemplate = jba->CreateNewBlock(coinbaseScriptPubKey, pindexPrev));
    blockStats = jba->getLastBlockStats();
    BOOST_CHECK_EQUAL(blockStats.txCount, INITIAL_SELFISH_TXN_BATCH_SIZE - 1 + 1 + NON_SELFISH_CPFP_GROUP_SIZE + 1);
    BOOST_CHECK(IsTxnInBlock(selfishSingleTxns[7], pblocktemplate->GetBlockRef()));
    BOOST_CHECK(IsTxnInBlock(nonSelfishSpendingTxn, pblocktemplate->GetBlockRef()));


    // Add txn with current time that spends first selfish CPFP group
    CTransactionRef nonSelfishCPFPSpendingTxn {nullptr};
    BOOST_CHECK(nonSelfishCPFPSpendingTxn = AddSingleTransaction({selfishCPFPTxns[1]}));

    // Without time ticking JBA still takes nothing
    BOOST_CHECK(pblocktemplate = jba->CreateNewBlock(coinbaseScriptPubKey, pindexPrev));
    blockStats = jba->getLastBlockStats();
    BOOST_CHECK_EQUAL(blockStats.txCount, INITIAL_SELFISH_TXN_BATCH_SIZE - 1 + 1 + NON_SELFISH_CPFP_GROUP_SIZE + 1);

    // Time moves a second and JBA now takes selfish CPFP group and the spending child
    mockTime += 1;
    SetMockTime(mockTime);
    BOOST_CHECK(pblocktemplate = jba->CreateNewBlock(coinbaseScriptPubKey, pindexPrev));
    blockStats = jba->getLastBlockStats();
    BOOST_CHECK_EQUAL(blockStats.txCount, INITIAL_SELFISH_TXN_BATCH_SIZE - 1 + 1 + NON_SELFISH_CPFP_GROUP_SIZE + 1 + SELFISH_CPFP_GROUP_SIZE + 1);
    BOOST_CHECK(IsTxnInBlock(selfishCPFPTxns[0], pblocktemplate->GetBlockRef()));
    BOOST_CHECK(IsTxnInBlock(selfishCPFPTxns[1], pblocktemplate->GetBlockRef()));
    BOOST_CHECK(IsTxnInBlock(nonSelfishCPFPSpendingTxn, pblocktemplate->GetBlockRef()));

    // Add CPFP group with current time that spends 2nd selfish CPFP group
    nonSelfishCPFPTxns.clear();
    BOOST_CHECK(AddCPFPTransactions(NON_SELFISH_CPFP_GROUP_SIZE, selfishCPFPTxns[3], nonSelfishCPFPTxns));

    // Without time ticking JBA still takes nothing
    BOOST_CHECK(pblocktemplate = jba->CreateNewBlock(coinbaseScriptPubKey, pindexPrev));
    blockStats = jba->getLastBlockStats();
    BOOST_CHECK_EQUAL(blockStats.txCount, INITIAL_SELFISH_TXN_BATCH_SIZE - 1 + 1 + NON_SELFISH_CPFP_GROUP_SIZE + 1 + SELFISH_CPFP_GROUP_SIZE + 1);

    // Time moves a second and JBA now takes selfish CPFP group and the CPFP group that spends it
    mockTime += 1;
    SetMockTime(mockTime);
    BOOST_CHECK(pblocktemplate = jba->CreateNewBlock(coinbaseScriptPubKey, pindexPrev));
    blockStats = jba->getLastBlockStats();
    BOOST_CHECK_EQUAL(blockStats.txCount, INITIAL_SELFISH_TXN_BATCH_SIZE - 1 + 1 + NON_SELFISH_CPFP_GROUP_SIZE + 1 + SELFISH_CPFP_GROUP_SIZE + 1 +
                                          SELFISH_CPFP_GROUP_SIZE + NON_SELFISH_CPFP_GROUP_SIZE);
    BOOST_CHECK(IsTxnInBlock(selfishCPFPTxns[2], pblocktemplate->GetBlockRef()));
    BOOST_CHECK(IsTxnInBlock(selfishCPFPTxns[3], pblocktemplate->GetBlockRef()));
    BOOST_CHECK(IsTxnInBlock(nonSelfishCPFPTxns[0], pblocktemplate->GetBlockRef()));
    BOOST_CHECK(IsTxnInBlock(nonSelfishCPFPTxns[1], pblocktemplate->GetBlockRef()));


    // Accept the block and check JBA isn't throttling for a new block with the single remaining txn and group in
    constexpr unsigned COINBASE_TXN {1};
    CreateAndProcessBlock(jba);
    BOOST_CHECK_EQUAL(chainActive.Tip()->GetBlockTxCount(), INITIAL_SELFISH_TXN_BATCH_SIZE - 1 + 1 + NON_SELFISH_CPFP_GROUP_SIZE + 1 +
                                                            SELFISH_CPFP_GROUP_SIZE + 1 + SELFISH_CPFP_GROUP_SIZE + NON_SELFISH_CPFP_GROUP_SIZE +
                                                            COINBASE_TXN);
    BOOST_CHECK(pblocktemplate = jba->CreateNewBlock(coinbaseScriptPubKey, pindexPrev));
    blockStats = jba->getLastBlockStats();
    BOOST_CHECK_EQUAL(blockStats.txCount, 1 + SELFISH_CPFP_GROUP_SIZE);
    BOOST_CHECK(! JBAAccess::GetEnteredThrottling(*jba));


    // Add txns to start us throttling again
    unsigned int NEXT_TXN_BATCH_SIZE {4};
    for(unsigned int i = 0; i < NEXT_TXN_BATCH_SIZE; ++i)
    {
        BOOST_CHECK(AddSingleTransaction());
    }
    BOOST_CHECK(pblocktemplate = jba->CreateNewBlock(coinbaseScriptPubKey, pindexPrev));
    blockStats = jba->getLastBlockStats();
    BOOST_CHECK_EQUAL(blockStats.txCount, 1 + SELFISH_CPFP_GROUP_SIZE + NEXT_TXN_BATCH_SIZE);
    BOOST_CHECK(JBAAccess::GetEnteredThrottling(*jba));

    // Put single txn and a CPFP group in the mempool that are selfish
    SetMockTime(selfishTime);
    CTransactionRef selfishSingleTxn {nullptr};
    BOOST_CHECK(selfishSingleTxn = AddSingleTransaction());
    selfishCPFPTxns.clear();
    BOOST_CHECK(AddCPFPTransactions(SELFISH_CPFP_GROUP_SIZE, nullptr, selfishCPFPTxns));

    // Check JBA isn't taking any selfish txns
    mockTime += 1;
    SetMockTime(mockTime);
    BOOST_CHECK(pblocktemplate = jba->CreateNewBlock(coinbaseScriptPubKey, pindexPrev));
    blockStats = jba->getLastBlockStats();
    BOOST_CHECK_EQUAL(blockStats.txCount, 1 + SELFISH_CPFP_GROUP_SIZE + NEXT_TXN_BATCH_SIZE);

    // Add non-selfish txn to the mempool that spends selfish single txn and spends selfish CPFP group
    BOOST_CHECK(nonSelfishSpendingTxn = AddSingleTransaction({selfishSingleTxn, selfishCPFPTxns[1]}));

    // Verify JBA takes all txns as a single group
    BOOST_CHECK(pblocktemplate = jba->CreateNewBlock(coinbaseScriptPubKey, pindexPrev));
    blockStats = jba->getLastBlockStats();
    BOOST_CHECK_EQUAL(blockStats.txCount, 1 + SELFISH_CPFP_GROUP_SIZE + NEXT_TXN_BATCH_SIZE + 1 + SELFISH_CPFP_GROUP_SIZE + 1);
    BOOST_CHECK(IsTxnInBlock(selfishSingleTxn, pblocktemplate->GetBlockRef()));
    BOOST_CHECK(IsTxnInBlock(selfishCPFPTxns[0], pblocktemplate->GetBlockRef()));
    BOOST_CHECK(IsTxnInBlock(selfishCPFPTxns[1], pblocktemplate->GetBlockRef()));
    BOOST_CHECK(IsTxnInBlock(nonSelfishSpendingTxn, pblocktemplate->GetBlockRef()));

    // Check we can accept the block
    CreateAndProcessBlock(jba);
    BOOST_CHECK_EQUAL(chainActive.Tip()->GetBlockTxCount(), 1 + SELFISH_CPFP_GROUP_SIZE + NEXT_TXN_BATCH_SIZE + 1 +
                                                            SELFISH_CPFP_GROUP_SIZE + 1 + COINBASE_TXN);
    BOOST_CHECK(pblocktemplate = jba->CreateNewBlock(coinbaseScriptPubKey, pindexPrev));
    blockStats = jba->getLastBlockStats();
    BOOST_CHECK_EQUAL(blockStats.txCount, 0U);
    BOOST_CHECK(! JBAAccess::GetEnteredThrottling(*jba));
}

BOOST_AUTO_TEST_SUITE_END()

