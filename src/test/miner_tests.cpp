// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "mining/factory.h"
#include "mining/journal_builder.h"

#include "chainparams.h"
#include "coins.h"
#include "config.h"
#include "consensus/consensus.h"
#include "consensus/merkle.h"
#include "consensus/validation.h"
#include "policy/policy.h"
#include "pubkey.h"
#include "script/script_num.h"
#include "script/standard.h"
#include "txmempool.h"
#include "uint256.h"
#include "util.h"
#include "utilstrencodings.h"
#include "validation.h"

#include "test/test_bitcoin.h"

#include <memory>

#include <boost/test/unit_test.hpp>

using mining::BlockAssemblerRef;
using mining::CBlockTemplate;

namespace
{
    mining::CJournalChangeSetPtr nullChangeSet {nullptr};

    GlobalConfig config {};
    GlobalConfig configJournal {};

    void ResetConfig()
    {
        config.Reset();
        config.SetDefaultBlockSizeParams(Params().GetDefaultBlockSizeParams());
        config.SetGenesisActivationHeight(config.GetChainParams().GetConsensus().genesisHeight);
        configJournal.Reset();
        configJournal.SetDefaultBlockSizeParams(Params().GetDefaultBlockSizeParams());
        configJournal.SetMiningCandidateBuilder(mining::CMiningFactory::BlockAssemblerType::JOURNALING);
        configJournal.SetGenesisActivationHeight(config.GetChainParams().GetConsensus().genesisHeight);
    }
}

BOOST_FIXTURE_TEST_SUITE(miner_tests, TestingSetup)

static CFeeRate blockMinFeeRate = CFeeRate(DEFAULT_BLOCK_MIN_TX_FEE);

static struct {
    uint8_t extranonce;
    uint32_t nonce;
} blockinfo[] = {
    {4, 0xa4a3e223}, {2, 0x15c32f9e}, {1, 0x0375b547}, {1, 0x7004a8a5},
    {2, 0xce440296}, {2, 0x52cfe198}, {1, 0x77a72cd0}, {2, 0xbb5d6f84},
    {2, 0x83f30c2c}, {1, 0x48a73d5b}, {1, 0xef7dcd01}, {2, 0x6809c6c4},
    {2, 0x0883ab3c}, {1, 0x087bbbe2}, {2, 0x2104a814}, {2, 0xdffb6daa},
    {1, 0xee8a0a08}, {2, 0xba4237c1}, {1, 0xa70349dc}, {1, 0x344722bb},
    {3, 0xd6294733}, {2, 0xec9f5c94}, {2, 0xca2fbc28}, {1, 0x6ba4f406},
    {2, 0x015d4532}, {1, 0x6e119b7c}, {2, 0x43e8f314}, {2, 0x27962f38},
    {2, 0xb571b51b}, {2, 0xb36bee23}, {2, 0xd17924a8}, {2, 0x6bc212d9},
    {1, 0x630d4948}, {2, 0x9a4c4ebb}, {2, 0x554be537}, {1, 0xd63ddfc7},
    {2, 0xa10acc11}, {1, 0x759a8363}, {2, 0xfb73090d}, {1, 0xe82c6a34},
    {1, 0xe33e92d7}, {3, 0x658ef5cb}, {2, 0xba32ff22}, {5, 0x0227a10c},
    {1, 0xa9a70155}, {5, 0xd096d809}, {1, 0x37176174}, {1, 0x830b8d0f},
    {1, 0xc6e3910e}, {2, 0x823f3ca8}, {1, 0x99850849}, {1, 0x7521fb81},
    {1, 0xaacaabab}, {1, 0xd645a2eb}, {5, 0x7aea1781}, {5, 0x9d6e4b78},
    {1, 0x4ce90fd8}, {1, 0xabdc832d}, {6, 0x4a34f32a}, {2, 0xf2524c1c},
    {2, 0x1bbeb08a}, {1, 0xad47f480}, {1, 0x9f026aeb}, {1, 0x15a95049},
    {2, 0xd1cb95b2}, {2, 0xf84bbda5}, {1, 0x0fa62cd1}, {1, 0xe05f9169},
    {1, 0x78d194a9}, {5, 0x3e38147b}, {5, 0x737ba0d4}, {1, 0x63378e10},
    {1, 0x6d5f91cf}, {2, 0x88612eb8}, {2, 0xe9639484}, {1, 0xb7fabc9d},
    {2, 0x19b01592}, {1, 0x5a90dd31}, {2, 0x5bd7e028}, {2, 0x94d00323},
    {1, 0xa9b9c01a}, {1, 0x3a40de61}, {1, 0x56e7eec7}, {5, 0x859f7ef6},
    {1, 0xfd8e5630}, {1, 0x2b0c9f7f}, {1, 0xba700e26}, {1, 0x7170a408},
    {1, 0x70de86a8}, {1, 0x74d64cd5}, {1, 0x49e738a1}, {2, 0x6910b602},
    {0, 0x643c565f}, {1, 0x54264b3f}, {2, 0x97ea6396}, {2, 0x55174459},
    {2, 0x03e8779a}, {1, 0x98f34d8f}, {1, 0xc07b2b07}, {1, 0xdfe29668},
    {1, 0x3141c7c1}, {1, 0xb3b595f4}, {1, 0x735abf08}, {5, 0x623bfbce},
    {2, 0xd351e722}, {1, 0xf4ca48c9}, {1, 0x5b19c670}, {1, 0xa164bf0e},
    {2, 0xbbbeb305}, {2, 0xfe1c810a},
};

CBlockIndex CreateBlockIndex(int nHeight) {
    CBlockIndex index;
    index.nHeight = nHeight;
    index.pprev = chainActive.Tip();
    return index;
}

bool TestSequenceLocks(const CTransaction &tx, int flags) {
    std::shared_lock lock(mempool.smtx);
    return CheckSequenceLocks(tx, mempool, config, flags);
}

// Test suite for ancestor feerate transaction selection.
// Implemented as an additional function, rather than a separate test case, to
// allow reusing the blockchain created in CreateNewBlock_validity.
// Note that this test assumes blockprioritypercentage is 0.
void TestPackageSelection(Config &config, CScript scriptPubKey,
                          std::vector<CTransactionRef> &txFirst) {
    // Test the ancestor feerate transaction selection.
    TestMemPoolEntryHelper entry;

    // these 3 tests assume blockprioritypercentage is 0.
    config.SetBlockPriorityPercentage(0);

    mining::CMiningFactory miningFactory { config };

    // Test that a medium fee transaction will be selected after a higher fee
    // rate package with a low fee rate parent.
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].scriptSig = CScript() << OP_1;
    tx.vin[0].prevout = COutPoint(txFirst[0]->GetId(), 0);
    tx.vout.resize(1);
    tx.vout[0].nValue = Amount(5000000000LL - 1000);
    // This tx has a low fee: 1000 satoshis.
    // Save this txid for later use.
    TxId parentTxId = tx.GetId();
    mempool.AddUnchecked(parentTxId,
                         entry.Fee(Amount(1000))
                             .Time(GetTime())
                             .SpendsCoinbase(true)
                             .FromTx(tx),
                         nullChangeSet);

    // This tx has a medium fee: 10000 satoshis.
    tx.vin[0].prevout = COutPoint(txFirst[1]->GetId(), 0);
    tx.vout[0].nValue = Amount(5000000000LL - 10000);
    TxId mediumFeeTxId = tx.GetId();
    mempool.AddUnchecked(mediumFeeTxId,
                         entry.Fee(Amount(10000))
                             .Time(GetTime())
                             .SpendsCoinbase(true)
                             .FromTx(tx),
                         nullChangeSet);

    // This tx has a high fee, but depends on the first transaction.
    tx.vin[0].prevout = COutPoint(parentTxId, 0);
    // 50k satoshi fee.
    tx.vout[0].nValue = Amount(5000000000LL - 1000 - 50000);
    TxId highFeeTxId = tx.GetId();
    mempool.AddUnchecked(highFeeTxId,
                         entry.Fee(Amount(50000))
                             .Time(GetTime())
                             .SpendsCoinbase(false)
                             .FromTx(tx),
                         nullChangeSet);

    CBlockIndex* pindexPrev {nullptr};
    std::unique_ptr<CBlockTemplate> pblocktemplate =
        miningFactory.GetAssembler()->CreateNewBlock(scriptPubKey, pindexPrev);
    BOOST_CHECK(pblocktemplate->GetBlockRef()->vtx[1]->GetId() == parentTxId);
    BOOST_CHECK(pblocktemplate->GetBlockRef()->vtx[2]->GetId() == highFeeTxId);
    BOOST_CHECK(pblocktemplate->GetBlockRef()->vtx[3]->GetId() == mediumFeeTxId);

    // Test that a package below the block min tx fee doesn't get included
    tx.vin[0].prevout = COutPoint(highFeeTxId, 0);
    // 0 fee.
    tx.vout[0].nValue = Amount(5000000000LL - 1000 - 50000);
    TxId freeTxId = tx.GetId();
    mempool.AddUnchecked(freeTxId, entry.Fee(Amount(0)).FromTx(tx), nullChangeSet);
    size_t freeTxSize = ::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION);

    // Calculate a fee on child transaction that will put the package just
    // below the block min tx fee (assuming 1 child tx of the same size).
    Amount feeToUse = blockMinFeeRate.GetFee(2 * freeTxSize) - Amount(1);

    tx.vin[0].prevout = COutPoint(freeTxId, 0);
    tx.vout[0].nValue = Amount(5000000000LL - 1000 - 50000) - feeToUse;
    TxId lowFeeTxId = tx.GetId();
    mempool.AddUnchecked(lowFeeTxId, entry.Fee(feeToUse).FromTx(tx), nullChangeSet);
    pblocktemplate = miningFactory.GetAssembler()->CreateNewBlock(scriptPubKey, pindexPrev);
    // Verify that the free tx and the low fee tx didn't get selected.
    for (const auto &txn : pblocktemplate->GetBlockRef()->vtx) {
        BOOST_CHECK(txn->GetId() != freeTxId);
        BOOST_CHECK(txn->GetId() != lowFeeTxId);
    }

    // Test that packages above the min relay fee do get included, even if one
    // of the transactions is below the min relay fee. Remove the low fee
    // transaction and replace with a higher fee transaction
    mempool.RemoveRecursive(CTransaction(tx), nullChangeSet);
    // Now we should be just over the min relay fee.
    tx.vout[0].nValue -= Amount(2);
    lowFeeTxId = tx.GetId();
    mempool.AddUnchecked(lowFeeTxId,
                         entry.Fee(feeToUse + Amount(2)).FromTx(tx), nullChangeSet);
    pblocktemplate = miningFactory.GetAssembler()->CreateNewBlock(scriptPubKey, pindexPrev);
    BOOST_CHECK(pblocktemplate->GetBlockRef()->vtx[4]->GetId() == freeTxId);
    BOOST_CHECK(pblocktemplate->GetBlockRef()->vtx[5]->GetId() == lowFeeTxId);

    // Test that transaction selection properly updates ancestor fee
    // calculations as ancestor transactions get included in a block. Add a
    // 0-fee transaction that has 2 outputs.
    tx.vin[0].prevout = COutPoint(txFirst[2]->GetId(), 0);
    tx.vout.resize(2);
    tx.vout[0].nValue = Amount(5000000000LL - 100000000);
    // 1BCC output.
    tx.vout[1].nValue = Amount(100000000);
    TxId freeTxId2 = tx.GetId();
    mempool.AddUnchecked(freeTxId2,
                         entry.Fee(Amount(0)).SpendsCoinbase(true).FromTx(tx), nullChangeSet);

    // This tx can't be mined by itself.
    tx.vin[0].prevout = COutPoint(freeTxId2, 0);
    tx.vout.resize(1);
    feeToUse = blockMinFeeRate.GetFee(freeTxSize);
    tx.vout[0].nValue = Amount(5000000000LL) - Amount(100000000) - feeToUse;
    TxId lowFeeTxId2 = tx.GetId();
    mempool.AddUnchecked(lowFeeTxId2,
                         entry.Fee(feeToUse).SpendsCoinbase(false).FromTx(tx), nullChangeSet);
    pblocktemplate = miningFactory.GetAssembler()->CreateNewBlock(scriptPubKey, pindexPrev);

    // Verify that this tx isn't selected.
    for (const auto &txn : pblocktemplate->GetBlockRef()->vtx) {
        BOOST_CHECK(txn->GetId() != freeTxId2);
        BOOST_CHECK(txn->GetId() != lowFeeTxId2);
    }

    // This tx will be mineable, and should cause lowFeeTxId2 to be selected as
    // well.
    tx.vin[0].prevout = COutPoint(freeTxId2, 1);
    // 10k satoshi fee.
    tx.vout[0].nValue = Amount(100000000 - 10000);
    mempool.AddUnchecked(tx.GetId(), entry.Fee(Amount(10000)).FromTx(tx), nullChangeSet);
    pblocktemplate = miningFactory.GetAssembler()->CreateNewBlock(scriptPubKey, pindexPrev);
    BOOST_CHECK(pblocktemplate->GetBlockRef()->vtx[8]->GetId() == lowFeeTxId2);
}

// NOTE: These tests rely on CreateNewBlock doing its own self-validation!
BOOST_AUTO_TEST_CASE(CreateNewBlock_validity) {
    // Note that by default, these tests run with size accounting enabled.
    CScript scriptPubKey =
        CScript() << ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909"
                              "a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112"
                              "de5c384df7ba0b8d578a4c702b6bf11d5f")
                  << OP_CHECKSIG;
    std::unique_ptr<CBlockTemplate> pblocktemplate;
    std::unique_ptr<CBlockTemplate> pblocktemplateJournal;
    CMutableTransaction tx, tx2;
    CScript script;
    uint256 hash;
    TestMemPoolEntryHelper entry;
    entry.nFee = Amount(11);
    entry.dPriority = 111.0;
    entry.nHeight = 11;

    ResetConfig();

    mining::CMiningFactory miningFactory { config };
    mining::CMiningFactory journalMiningFactory { configJournal };

    LOCK(cs_main);
    fCheckpointsEnabled = false;

    // Simple block creation, nothing special yet:
    CBlockIndex* pindexPrev {nullptr};
    BOOST_CHECK(pblocktemplate = miningFactory.GetAssembler()->CreateNewBlock(scriptPubKey, pindexPrev));

    // We can't make transactions until we have inputs. Therefore, load 100
    // blocks :)
    int baseheight = 0;
    std::vector<CTransactionRef> txFirst;
    for (size_t i = 0; i < sizeof(blockinfo) / sizeof(*blockinfo); ++i) {
        // pointer for convenience.
        CBlockRef blockRef = pblocktemplate->GetBlockRef();
        CBlock *pblock = blockRef.get();
        pblock->nVersion = 1;
        pblock->nTime = chainActive.Tip()->GetMedianTimePast() + 1;
        CMutableTransaction txCoinbase(*pblock->vtx[0]);
        txCoinbase.nVersion = 1;
        txCoinbase.vin[0].scriptSig = CScript();
        txCoinbase.vin[0].scriptSig.push_back(blockinfo[i].extranonce);
        txCoinbase.vin[0].scriptSig.push_back(chainActive.Height());
        // Ignore the (optional) segwit commitment added by CreateNewBlock (as
        // the hardcoded nonces don't account for this)
        txCoinbase.vout.resize(1);
        txCoinbase.vout[0].scriptPubKey = CScript();
        pblock->vtx[0] = MakeTransactionRef(std::move(txCoinbase));
        if (txFirst.size() == 0) baseheight = chainActive.Height();
        if (txFirst.size() < 4) txFirst.push_back(pblock->vtx[0]);
        pblock->hashMerkleRoot = BlockMerkleRoot(*pblock);
        pblock->nNonce = blockinfo[i].nonce;
        std::shared_ptr<const CBlock> shared_pblock =
            std::make_shared<const CBlock>(*pblock);
        BOOST_CHECK(ProcessNewBlock(config, shared_pblock, true, nullptr));
        pblock->hashPrevBlock = pblock->GetHash();
    }

    // Just to make sure we can still make simple blocks.
    BOOST_CHECK(pblocktemplate = miningFactory.GetAssembler()->CreateNewBlock(scriptPubKey, pindexPrev));
    BOOST_CHECK(pblocktemplateJournal = journalMiningFactory.GetAssembler()->CreateNewBlock(scriptPubKey, pindexPrev));

    const Amount BLOCKSUBSIDY = 50 * COIN;
    const Amount LOWFEE = CENT;
    const Amount HIGHFEE = COIN;
    const Amount HIGHERFEE = 4 * COIN;

    
    // block sigops > limit: 1000 CHECKMULTISIG + 1
    tx.vin.resize(1);
    // NOTE: OP_NOP is used to force 20 SigOps for the CHECKMULTISIG
    tx.vin[0].scriptSig = CScript() << OP_0 << OP_0 << OP_0 << OP_NOP
                                    << OP_CHECKMULTISIG << OP_1;
    tx.vin[0].prevout = COutPoint(txFirst[0]->GetId(), 0);
    tx.vout.resize(1);
    tx.vout[0].nValue = BLOCKSUBSIDY;
    for (unsigned int i = 0; i < 1001; ++i) {
        tx.vout[0].nValue -= LOWFEE;
        hash = tx.GetId();
        // Only first tx spends coinbase.
        bool spendsCoinbase = (i == 0) ? true : false;
        // If we don't set the # of sig ops in the CTxMemPoolEntry, template
        // creation fails when validating.
        mempool.AddUnchecked(hash,
                             entry.Fee(LOWFEE)
                                 .Time(GetTime())
                                 .SpendsCoinbase(spendsCoinbase)
                                 .FromTx(tx),
                             nullChangeSet);
        tx.vin[0].prevout = COutPoint(hash, 0);
    }
    config.SetGenesisActivationHeight(500);
    configJournal.SetGenesisActivationHeight(500);
    config.SetTestBlockCandidateValidity(false);
    configJournal.SetTestBlockCandidateValidity(false);
    BOOST_CHECK_NO_THROW(miningFactory.GetAssembler()->CreateNewBlock(scriptPubKey, pindexPrev));
    BOOST_CHECK_NO_THROW(journalMiningFactory.GetAssembler()->CreateNewBlock(scriptPubKey, pindexPrev));
    config.SetTestBlockCandidateValidity(true);
    configJournal.SetTestBlockCandidateValidity(true);
    BOOST_CHECK_THROW(miningFactory.GetAssembler()->CreateNewBlock(scriptPubKey, pindexPrev), std::runtime_error);
    BOOST_CHECK_THROW(journalMiningFactory.GetAssembler()->CreateNewBlock(scriptPubKey, pindexPrev), std::runtime_error);

    mempool.Clear();

    tx.vin[0].prevout = COutPoint(txFirst[0]->GetId(), 0);
    tx.vout[0].nValue = BLOCKSUBSIDY;
    for (unsigned int i = 0; i < 1001; ++i) {
        tx.vout[0].nValue -= LOWFEE;
        hash = tx.GetId();
        // Only first tx spends coinbase.
        bool spendsCoinbase = (i == 0) ? true : false;
        // If we do set the # of sig ops in the CTxMemPoolEntry, template
        // creation passes.
        mempool.AddUnchecked(hash,
                             entry.Fee(LOWFEE)
                                 .Time(GetTime())
                                 .SpendsCoinbase(spendsCoinbase)
                                 .SigOpsCost(80)
                                 .FromTx(tx),
                             nullChangeSet);
        tx.vin[0].prevout = COutPoint(hash, 0);
    }
    BOOST_CHECK(pblocktemplate = miningFactory.GetAssembler()->CreateNewBlock(scriptPubKey, pindexPrev));
    BOOST_CHECK(pblocktemplateJournal = journalMiningFactory.GetAssembler()->CreateNewBlock(scriptPubKey, pindexPrev));
    mempool.Clear();

    // block size > limit
    tx.vin[0].scriptSig = CScript();
    // 18 * (520char + DROP) + OP_1 = 9433 bytes
    std::vector<uint8_t> vchData(520);
    for (unsigned int i = 0; i < 18; ++i) {
        tx.vin[0].scriptSig << vchData << OP_DROP;
    }

    tx.vin[0].scriptSig << OP_1;
    tx.vin[0].prevout = COutPoint(txFirst[0]->GetId(), 0);
    tx.vout[0].nValue = BLOCKSUBSIDY;
    for (unsigned int i = 0; i < 128; ++i) {
        tx.vout[0].nValue -= LOWFEE;
        hash = tx.GetId();
        // Only first tx spends coinbase.
        bool spendsCoinbase = (i == 0) ? true : false;
        mempool.AddUnchecked(hash,
                             entry.Fee(LOWFEE)
                                 .Time(GetTime())
                                 .SpendsCoinbase(spendsCoinbase)
                                 .FromTx(tx),
                             nullChangeSet);
        tx.vin[0].prevout = COutPoint(hash, 0);
    }
    BOOST_CHECK(pblocktemplate = miningFactory.GetAssembler()->CreateNewBlock(scriptPubKey, pindexPrev));
    BOOST_CHECK(pblocktemplateJournal = journalMiningFactory.GetAssembler()->CreateNewBlock(scriptPubKey, pindexPrev));
    mempool.Clear();

    // Orphan in mempool, template creation fails.
    hash = tx.GetId();
    mempool.AddUnchecked(hash, entry.Fee(LOWFEE).Time(GetTime()).FromTx(tx), nullChangeSet);
    config.SetTestBlockCandidateValidity(false);
    configJournal.SetTestBlockCandidateValidity(false);
    BOOST_CHECK_NO_THROW(miningFactory.GetAssembler()->CreateNewBlock(scriptPubKey, pindexPrev));
    BOOST_CHECK_NO_THROW(journalMiningFactory.GetAssembler()->CreateNewBlock(scriptPubKey, pindexPrev));
    config.SetTestBlockCandidateValidity(true);
    configJournal.SetTestBlockCandidateValidity(true);
    BOOST_CHECK_THROW(miningFactory.GetAssembler()->CreateNewBlock(scriptPubKey, pindexPrev), std::runtime_error);
    BOOST_CHECK_THROW(journalMiningFactory.GetAssembler()->CreateNewBlock(scriptPubKey, pindexPrev), std::runtime_error);

    mempool.Clear();

    // Child with higher priority than parent.
    tx.vin[0].scriptSig = CScript() << OP_1;
    tx.vin[0].prevout = COutPoint(txFirst[1]->GetId(), 0);
    tx.vout[0].nValue = BLOCKSUBSIDY - HIGHFEE;
    hash = tx.GetId();
    mempool.AddUnchecked(
        hash,
        entry.Fee(HIGHFEE).Time(GetTime()).SpendsCoinbase(true).FromTx(tx),
        nullChangeSet);
    tx.vin[0].prevout = COutPoint(hash, 0);
    tx.vin.resize(2);
    tx.vin[1].scriptSig = CScript() << OP_1;
    tx.vin[1].prevout = COutPoint(txFirst[0]->GetId(), 0);
    // First txn output + fresh coinbase - new txn fee.
    tx.vout[0].nValue = tx.vout[0].nValue + BLOCKSUBSIDY - HIGHERFEE;
    hash = tx.GetId();
    mempool.AddUnchecked(
        hash,
        entry.Fee(HIGHERFEE).Time(GetTime()).SpendsCoinbase(true).FromTx(tx),
        nullChangeSet);
    BOOST_CHECK(pblocktemplate = miningFactory.GetAssembler()->CreateNewBlock(scriptPubKey, pindexPrev));
    BOOST_CHECK(pblocktemplateJournal = journalMiningFactory.GetAssembler()->CreateNewBlock(scriptPubKey, pindexPrev));
    mempool.Clear();

    // Coinbase in mempool, template creation fails.
    tx.vin.resize(1);
    tx.vin[0].prevout = COutPoint();
    tx.vin[0].scriptSig = CScript() << OP_0 << OP_1;
    tx.vout[0].nValue = Amount(0);
    hash = tx.GetId();
    // Give it a fee so it'll get mined.
    mempool.AddUnchecked(
        hash,
        entry.Fee(LOWFEE).Time(GetTime()).SpendsCoinbase(false).FromTx(tx),
        nullChangeSet);
    config.SetTestBlockCandidateValidity(false);
    configJournal.SetTestBlockCandidateValidity(false);
    BOOST_CHECK_NO_THROW(miningFactory.GetAssembler()->CreateNewBlock(scriptPubKey, pindexPrev));
    BOOST_CHECK_NO_THROW(journalMiningFactory.GetAssembler()->CreateNewBlock(scriptPubKey, pindexPrev));
    config.SetTestBlockCandidateValidity(true);
    configJournal.SetTestBlockCandidateValidity(true);
    BOOST_CHECK_THROW(miningFactory.GetAssembler()->CreateNewBlock(scriptPubKey, pindexPrev), std::runtime_error);
    BOOST_CHECK_THROW(journalMiningFactory.GetAssembler()->CreateNewBlock(scriptPubKey, pindexPrev), std::runtime_error);

    mempool.Clear();

    // Invalid (pre-p2sh) txn in mempool, template creation fails.
    std::array<int64_t, CBlockIndex::nMedianTimeSpan> times;
    for (int i = 0; i < CBlockIndex::nMedianTimeSpan; i++) {
        // Trick the MedianTimePast.
        times[i] = chainActive.Tip()
                       ->GetAncestor(chainActive.Tip()->nHeight - i)
                       ->nTime;
        chainActive.Tip()->GetAncestor(chainActive.Tip()->nHeight - i)->nTime =
            P2SH_ACTIVATION_TIME;
    }

    tx.vin[0].prevout = COutPoint(txFirst[0]->GetId(), 0);
    tx.vin[0].scriptSig = CScript() << OP_1;
    tx.vout[0].nValue = BLOCKSUBSIDY - LOWFEE;
    script = CScript() << OP_0;
    tx.vout[0].scriptPubKey = GetScriptForDestination(CScriptID(script));
    hash = tx.GetId();
    mempool.AddUnchecked(
        hash,
        entry.Fee(LOWFEE).Time(GetTime()).SpendsCoinbase(true).FromTx(tx),
        nullChangeSet);
    tx.vin[0].prevout = COutPoint(hash, 0);
    tx.vin[0].scriptSig = CScript()
                          << std::vector<uint8_t>(script.begin(), script.end());
    tx.vout[0].nValue -= LOWFEE;
    hash = tx.GetId();
    mempool.AddUnchecked(
        hash,
        entry.Fee(LOWFEE).Time(GetTime()).SpendsCoinbase(false).FromTx(tx),
        nullChangeSet);
    config.SetTestBlockCandidateValidity(false);
    configJournal.SetTestBlockCandidateValidity(false);
    BOOST_CHECK_NO_THROW(miningFactory.GetAssembler()->CreateNewBlock(scriptPubKey, pindexPrev));
    BOOST_CHECK_NO_THROW(journalMiningFactory.GetAssembler()->CreateNewBlock(scriptPubKey, pindexPrev));
    config.SetTestBlockCandidateValidity(true);
    configJournal.SetTestBlockCandidateValidity(true);
    BOOST_CHECK_THROW(miningFactory.GetAssembler()->CreateNewBlock(scriptPubKey, pindexPrev), std::runtime_error);
    BOOST_CHECK_THROW(journalMiningFactory.GetAssembler()->CreateNewBlock(scriptPubKey, pindexPrev), std::runtime_error);

    mempool.Clear();
    for (int i = 0; i < CBlockIndex::nMedianTimeSpan; i++) {
        // Restore the MedianTimePast.
        chainActive.Tip()->GetAncestor(chainActive.Tip()->nHeight - i)->nTime =
            times[i];
    }

    // Double spend txn pair in mempool, template creation fails.
    tx.vin[0].prevout = COutPoint(txFirst[0]->GetId(), 0);
    tx.vin[0].scriptSig = CScript() << OP_1;
    tx.vout[0].nValue = BLOCKSUBSIDY - HIGHFEE;
    tx.vout[0].scriptPubKey = CScript() << OP_1;
    hash = tx.GetId();
    mempool.AddUnchecked(
        hash,
        entry.Fee(HIGHFEE).Time(GetTime()).SpendsCoinbase(true).FromTx(tx),
        nullChangeSet);
    tx.vout[0].scriptPubKey = CScript() << OP_2;
    hash = tx.GetId();
    mempool.AddUnchecked(
        hash,
        entry.Fee(HIGHFEE).Time(GetTime()).SpendsCoinbase(true).FromTx(tx),
        nullChangeSet);
    config.SetTestBlockCandidateValidity(false);
    configJournal.SetTestBlockCandidateValidity(false);
    BOOST_CHECK_NO_THROW(miningFactory.GetAssembler()->CreateNewBlock(scriptPubKey, pindexPrev));
    BOOST_CHECK_NO_THROW(journalMiningFactory.GetAssembler()->CreateNewBlock(scriptPubKey, pindexPrev));
    config.SetTestBlockCandidateValidity(true);
    configJournal.SetTestBlockCandidateValidity(true);
    BOOST_CHECK_THROW(miningFactory.GetAssembler()->CreateNewBlock(scriptPubKey, pindexPrev), std::runtime_error);
    BOOST_CHECK_THROW(journalMiningFactory.GetAssembler()->CreateNewBlock(scriptPubKey, pindexPrev), std::runtime_error);
    mempool.Clear();

    // Subsidy changing.
    int nHeight = chainActive.Height();
    // Create an actual 209999-long block chain (without valid blocks).
    while (chainActive.Tip()->nHeight < 209999) {
        CBlockIndex *prev = chainActive.Tip();
        CBlockIndex *next = new CBlockIndex();
        next->phashBlock = new uint256(InsecureRand256());
        pcoinsTip->SetBestBlock(next->GetBlockHash());
        next->pprev = prev;
        next->nHeight = prev->nHeight + 1;
        next->BuildSkip();
        chainActive.SetTip(next);
    }
    BOOST_CHECK(pblocktemplate = miningFactory.GetAssembler()->CreateNewBlock(scriptPubKey, pindexPrev));
    BOOST_CHECK(pblocktemplateJournal = journalMiningFactory.GetAssembler()->CreateNewBlock(scriptPubKey, pindexPrev));
    // Extend to a 210000-long block chain.
    while (chainActive.Tip()->nHeight < 210000) {
        CBlockIndex *prev = chainActive.Tip();
        CBlockIndex *next = new CBlockIndex();
        next->phashBlock = new uint256(InsecureRand256());
        pcoinsTip->SetBestBlock(next->GetBlockHash());
        next->pprev = prev;
        next->nHeight = prev->nHeight + 1;
        next->BuildSkip();
        chainActive.SetTip(next);
    }
    BOOST_CHECK(pblocktemplate = miningFactory.GetAssembler()->CreateNewBlock(scriptPubKey, pindexPrev));
    BOOST_CHECK(pblocktemplateJournal = journalMiningFactory.GetAssembler()->CreateNewBlock(scriptPubKey, pindexPrev));
    // Delete the dummy blocks again.
    while (chainActive.Tip()->nHeight > nHeight) {
        CBlockIndex *del = chainActive.Tip();
        chainActive.SetTip(del->pprev);
        pcoinsTip->SetBestBlock(del->pprev->GetBlockHash());
        delete del->phashBlock;
        delete del;
    }

    // non-final txs in mempool
    SetMockTime(chainActive.Tip()->GetMedianTimePast() + 1);
    int flags = LOCKTIME_VERIFY_SEQUENCE | LOCKTIME_MEDIAN_TIME_PAST;
    // height map
    std::vector<int> prevheights;

    // Relative height locked.
    tx.nVersion = 2;
    tx.vin.resize(1);
    prevheights.resize(1);
    // Only 1 transaction.
    tx.vin[0].prevout = COutPoint(txFirst[0]->GetId(), 0);
    tx.vin[0].scriptSig = CScript() << OP_1;
    // txFirst[0] is the 2nd block
    tx.vin[0].nSequence = chainActive.Tip()->nHeight + 1;
    prevheights[0] = baseheight + 1;
    tx.vout.resize(1);
    tx.vout[0].nValue = BLOCKSUBSIDY - HIGHFEE;
    tx.vout[0].scriptPubKey = CScript() << OP_1;
    tx.nLockTime = 0;
    hash = tx.GetId();
    mempool.AddUnchecked(
        hash,
        entry.Fee(HIGHFEE).Time(GetTime()).SpendsCoinbase(true).FromTx(tx),
        nullChangeSet);

    {
        // Locktime passes.
        GlobalConfig config;
        CValidationState state;
        BOOST_CHECK(ContextualCheckTransactionForCurrentBlock(
                        config,
                        CTransaction(tx),
                        chainActive.Height(),
                        chainActive.Tip()->GetMedianTimePast(),
                        state,
                        flags));
    }

    // Sequence locks fail.
    BOOST_CHECK(!TestSequenceLocks(CTransaction(tx), flags));
    // Sequence locks pass on 2nd block.
    BOOST_CHECK(
        SequenceLocks(CTransaction(tx), flags, &prevheights,
                      CreateBlockIndex(chainActive.Tip()->nHeight + 2)));

    // Relative time locked.
    tx.vin[0].prevout = COutPoint(txFirst[1]->GetId(), 0);
    // txFirst[1] is the 3rd block.
    tx.vin[0].nSequence = CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG |
                          (((chainActive.Tip()->GetMedianTimePast() + 1 -
                             chainActive[1]->GetMedianTimePast()) >>
                            CTxIn::SEQUENCE_LOCKTIME_GRANULARITY) +
                           1);
    prevheights[0] = baseheight + 2;
    hash = tx.GetId();
    mempool.AddUnchecked(hash, entry.Time(GetTime()).FromTx(tx), nullChangeSet);

    {
        // Locktime passes.
        GlobalConfig config;
        CValidationState state;
        BOOST_CHECK(ContextualCheckTransactionForCurrentBlock(
                        config,
                        CTransaction(tx),
                        chainActive.Height(),
                        chainActive.Tip()->GetMedianTimePast(),
                        state,
                        flags));
    }

    // Sequence locks fail.
    BOOST_CHECK(!TestSequenceLocks(CTransaction(tx), flags));

    for (int i = 0; i < CBlockIndex::nMedianTimeSpan; i++) {
        // Trick the MedianTimePast.
        chainActive.Tip()->GetAncestor(chainActive.Tip()->nHeight - i)->nTime +=
            512;
    }
    // Sequence locks pass 512 seconds later.
    BOOST_CHECK(
        SequenceLocks(CTransaction(tx), flags, &prevheights,
                      CreateBlockIndex(chainActive.Tip()->nHeight + 1)));
    for (int i = 0; i < CBlockIndex::nMedianTimeSpan; i++) {
        // Undo tricked MTP.
        chainActive.Tip()->GetAncestor(chainActive.Tip()->nHeight - i)->nTime -=
            512;
    }

    // Absolute height locked.
    tx.vin[0].prevout = COutPoint(txFirst[2]->GetId(), 0);
    tx.vin[0].nSequence = CTxIn::SEQUENCE_FINAL - 1;
    prevheights[0] = baseheight + 3;
    tx.nLockTime = chainActive.Tip()->nHeight + 1;
    hash = tx.GetId();
    mempool.AddUnchecked(hash, entry.Time(GetTime()).FromTx(tx), nullChangeSet);

    {
        // Locktime fails.
        CValidationState state;
        BOOST_CHECK(!ContextualCheckTransactionForCurrentBlock(
                        config,
                        CTransaction(tx),
                        chainActive.Height(),
                        chainActive.Tip()->GetMedianTimePast(),
                        state,
                        flags));
        BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-txns-nonfinal");
    }

    // Sequence locks pass.
    BOOST_CHECK(TestSequenceLocks(CTransaction(tx), flags));

    {
        // Locktime passes on 2nd block.
        GlobalConfig config;
        CValidationState state;
        BOOST_CHECK(ContextualCheckTransaction(
            config, CTransaction(tx), state, chainActive.Tip()->nHeight + 2,
            chainActive.Tip()->GetMedianTimePast(), false));
    }

    // Absolute time locked.
    tx.vin[0].prevout = COutPoint(txFirst[3]->GetId(), 0);
    tx.nLockTime = chainActive.Tip()->GetMedianTimePast();
    prevheights.resize(1);
    prevheights[0] = baseheight + 4;
    hash = tx.GetId();
    mempool.AddUnchecked(hash, entry.Time(GetTime()).FromTx(tx), nullChangeSet);

    {
        // Locktime fails.
        CValidationState state;
        BOOST_CHECK(!ContextualCheckTransactionForCurrentBlock(
                        config,
                        CTransaction(tx),
                        chainActive.Height(),
                        chainActive.Tip()->GetMedianTimePast(),
                        state,
                        flags));
        BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-txns-nonfinal");
    }

    // Sequence locks pass.
    BOOST_CHECK(TestSequenceLocks(CTransaction(tx), flags));

    {
        // Locktime passes 1 second later.
        GlobalConfig config;
        CValidationState state;
        BOOST_CHECK(ContextualCheckTransaction(
            config, CTransaction(tx), state, chainActive.Tip()->nHeight + 1,
            chainActive.Tip()->GetMedianTimePast() + 1, false));
    }

    // mempool-dependent transactions (not added)
    tx.vin[0].prevout = COutPoint(hash, 0);
    prevheights[0] = chainActive.Tip()->nHeight + 1;
    tx.nLockTime = 0;
    tx.vin[0].nSequence = 0;

    {
        // Locktime passes.
        GlobalConfig config;
        CValidationState state;
        BOOST_CHECK(ContextualCheckTransactionForCurrentBlock(
                        config,
                        CTransaction(tx),
                        chainActive.Height(),
                        chainActive.Tip()->GetMedianTimePast(),
                        state,
                        flags));
    }

    // Sequence locks pass.
    BOOST_CHECK(TestSequenceLocks(CTransaction(tx), flags));
    tx.vin[0].nSequence = 1;
    // Sequence locks fail.
    BOOST_CHECK(!TestSequenceLocks(CTransaction(tx), flags));
    tx.vin[0].nSequence = CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG;
    // Sequence locks pass.
    BOOST_CHECK(TestSequenceLocks(CTransaction(tx), flags));
    tx.vin[0].nSequence = CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG | 1;
    // Sequence locks fail.
    BOOST_CHECK(!TestSequenceLocks(CTransaction(tx), flags));

    BOOST_CHECK(pblocktemplate = miningFactory.GetAssembler()->CreateNewBlock(scriptPubKey, pindexPrev));
    BOOST_CHECK(pblocktemplateJournal = journalMiningFactory.GetAssembler()->CreateNewBlock(scriptPubKey, pindexPrev));

    // None of the of the absolute height/time locked tx should have made it
    // into the template because we still check IsFinalTx in CreateNewBlock, but
    // relative locked txs will if inconsistently added to mempool. For now
    // these will still generate a valid template until BIP68 soft fork.
    BOOST_CHECK_EQUAL(pblocktemplate->GetBlockRef()->vtx.size(), 3UL);
    BOOST_CHECK_EQUAL(pblocktemplateJournal->GetBlockRef()->vtx.size(), 3UL);
    // However if we advance height by 1 and time by 512, all of them should be
    // mined.
    for (int i = 0; i < CBlockIndex::nMedianTimeSpan; i++) {
        // Trick the MedianTimePast.
        chainActive.Tip()->GetAncestor(chainActive.Tip()->nHeight - i)->nTime +=
            512;
    }
    chainActive.Tip()->nHeight++;
    SetMockTime(chainActive.Tip()->GetMedianTimePast() + 1);

    BOOST_CHECK(pblocktemplate = miningFactory.GetAssembler()->CreateNewBlock(scriptPubKey, pindexPrev));
    BOOST_CHECK_EQUAL(pblocktemplate->GetBlockRef()->vtx.size(), 5UL);
    BOOST_CHECK(pblocktemplateJournal = journalMiningFactory.GetAssembler()->CreateNewBlock(scriptPubKey, pindexPrev));
    BOOST_CHECK_EQUAL(pblocktemplateJournal->GetBlockRef()->vtx.size(), 5UL);

    chainActive.Tip()->nHeight--;
    SetMockTime(0);
    mempool.Clear();

    TestPackageSelection(config, scriptPubKey, txFirst);

    fCheckpointsEnabled = true;
}

void CheckBlockMaxSize(uint64_t size, uint64_t expected)
{
    config.SetMaxGeneratedBlockSize(size);
    configJournal.SetMaxGeneratedBlockSize(size);

    mining::CMiningFactory miningFactory { config };
    mining::CMiningFactory journalMiningFactory { configJournal };

    BOOST_CHECK_EQUAL(miningFactory.GetAssembler()->GetMaxGeneratedBlockSize(), expected);
    BOOST_CHECK_EQUAL(journalMiningFactory.GetAssembler()->GetMaxGeneratedBlockSize(), expected);
}

BOOST_AUTO_TEST_CASE(BlockAssembler_construction)
{
    ResetConfig();

    // Make sure that default values are not overriden
    BOOST_REQUIRE(!config.MaxGeneratedBlockSizeOverridden());
    BOOST_REQUIRE(!configJournal.MaxGeneratedBlockSizeOverridden());

    uint64_t nDefaultMaxGeneratedBlockSize = config.GetMaxGeneratedBlockSize();
    uint64_t nDefaultMaxBlockSize = config.GetMaxBlockSize();

    // We are working on a fake chain and need to protect ourselves.
    LOCK(cs_main);

    // Test around historical 1MB (plus one byte because that's mandatory)
    BOOST_REQUIRE(config.SetMaxBlockSize(ONE_MEGABYTE + 1));
    BOOST_REQUIRE(configJournal.SetMaxBlockSize(ONE_MEGABYTE + 1));
    CheckBlockMaxSize(0, 1000); 
    CheckBlockMaxSize(1000, 1000);
    CheckBlockMaxSize(1001, 1001);
    CheckBlockMaxSize(12345, 12345);

    CheckBlockMaxSize(ONE_MEGABYTE - 1001, ONE_MEGABYTE - 1001);
    CheckBlockMaxSize(ONE_MEGABYTE - 1000, ONE_MEGABYTE - 1000);
    CheckBlockMaxSize(ONE_MEGABYTE - 999, ONE_MEGABYTE - 999);
    CheckBlockMaxSize(ONE_MEGABYTE, ONE_MEGABYTE - 999);

    // Test around default cap
    BOOST_REQUIRE(config.SetMaxBlockSize(nDefaultMaxBlockSize));
    BOOST_REQUIRE(configJournal.SetMaxBlockSize(nDefaultMaxBlockSize));

    // Now we can use the default max block size.
    CheckBlockMaxSize(nDefaultMaxBlockSize - 1001, nDefaultMaxBlockSize - 1001);
    CheckBlockMaxSize(nDefaultMaxBlockSize - 1000, nDefaultMaxBlockSize - 1000);
    CheckBlockMaxSize(nDefaultMaxBlockSize - 999, nDefaultMaxBlockSize - 1000);
    CheckBlockMaxSize(nDefaultMaxBlockSize, nDefaultMaxBlockSize - 1000);

    // If the parameter is not specified, we use
    // max(1K, min(DEFAULT_MAX_BLOCK_SIZE - 1K, DEFAULT_MAX_GENERATED_BLOCK_SIZE))
    {
        const auto expected { std::max(ONE_KILOBYTE,
                                std::min(nDefaultMaxBlockSize - ONE_KILOBYTE,
                                    nDefaultMaxGeneratedBlockSize)) };
        
        // Set generated max size to default
        CheckBlockMaxSize(nDefaultMaxGeneratedBlockSize, expected);
    }
}

void CheckBlockMaxSizeForTime(Config& config, uint64_t medianPastTime, uint64_t expectedSize)
{
    std::vector<CBlockIndex> blocks(11);

    // Construct chain  with desired median time. Set time of each block to 
    // the same value to get desired median past time.
    CBlockIndex* pprev{ nullptr };
    int height = 0;
    for (auto& block : blocks)
    {
        block.nTime = medianPastTime;
        block.pprev = pprev;
        block.nHeight = height;

        pprev = &block;
        height++;
    }


    // Make sure that we got correct median past time.
    BOOST_REQUIRE_EQUAL(blocks.back().GetMedianTimePast(), medianPastTime);

    {
        LOCK(cs_main);

        // chainActive is used by BlockAssembler to get median past time, which is used to select default block size
        chainActive.SetTip(&blocks.back());
    }

    mining::CMiningFactory miningFactory { config };
    BOOST_CHECK_EQUAL(miningFactory.GetAssembler()->GetMaxGeneratedBlockSize(), expectedSize);

    {
        LOCK(cs_main);

        chainActive.SetTip(nullptr); // cleanup
    }
}

BOOST_AUTO_TEST_CASE(BlockAssembler_construction_activate_new_blocksize)
{
    DefaultBlockSizeParams defaultParams{
        // activation time 
        1000,
        // max block size
        6000,
        // max generated block size before activation
        3000,
        // max generated block size after activation
        4000
    };

    ResetConfig();
    config.SetDefaultBlockSizeParams(defaultParams);
    configJournal.SetDefaultBlockSizeParams(defaultParams);

    CheckBlockMaxSizeForTime(config, 999, 3000);
    CheckBlockMaxSizeForTime(configJournal, 999, 3000);
    CheckBlockMaxSizeForTime(config, 1000, 4000);
    CheckBlockMaxSizeForTime(configJournal, 1000, 4000);
    CheckBlockMaxSizeForTime(config, 10001, 4000);
    CheckBlockMaxSizeForTime(configJournal, 10001, 4000);

    // When explicitly set, defaults values must not be used
    config.SetMaxGeneratedBlockSize(3333);
    configJournal.SetMaxGeneratedBlockSize(3333);
    CheckBlockMaxSizeForTime(config, 10001, 3333);
    CheckBlockMaxSizeForTime(configJournal, 10001, 3333);
}

BOOST_AUTO_TEST_CASE(JournalingBlockAssembler_Construction)
{
    ResetConfig();
    mining::CMiningFactory journalMiningFactory { configJournal };

    CScript scriptPubKey =
        CScript() << ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909"
                              "a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112"
                              "de5c384df7ba0b8d578a4c702b6bf11d5f")
                  << OP_CHECKSIG;
    CBlockIndex* pindexPrev {nullptr};

    std::unique_ptr<CBlockTemplate> bt { journalMiningFactory.GetAssembler()->CreateNewBlock(scriptPubKey, pindexPrev) };
    BOOST_REQUIRE(bt);
    BOOST_REQUIRE(bt->GetBlockRef());
    BOOST_CHECK_EQUAL(bt->GetBlockRef()->vtx.size(), 1);
}

BOOST_AUTO_TEST_SUITE_END()
