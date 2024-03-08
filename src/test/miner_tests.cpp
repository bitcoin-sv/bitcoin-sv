// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "mining/factory.h"
#include "mining/journal_builder.h"
#include "mining/journaling_block_assembler.h"

#include "block_index_store.h"
#include "chainparams.h"
#include "coins.h"
#include "config.h"
#include "consensus/consensus.h"
#include "consensus/merkle.h"
#include "consensus/validation.h"
#include "policy/policy.h"
#include "pow.h"
#include "pubkey.h"
#include "script/script_num.h"
#include "script/standard.h"
#include "txmempool.h"
#include "uint256.h"
#include "util.h"
#include "utilstrencodings.h"
#include "validation.h"

#include "mempool_test_access.h"

#include "test/test_bitcoin.h"

#include <memory>

#include <boost/test/unit_test.hpp>

using mining::BlockAssemblerRef;
using mining::CBlockTemplate;
using mining::JournalingBlockAssembler;

namespace
{
    mining::CJournalChangeSetPtr nullChangeSet {nullptr};
    
    class JournalingTestingSetup : public TestingSetup
    {
    public:
        JournalingTestingSetup()
            : TestingSetup(CBaseChainParams::MAIN, mining::CMiningFactory::BlockAssemblerType::JOURNALING)
        {}
    };

    class miner_tests_uid; // only used as unique identifier
}

// For inspection / modification of the JBA
template<>
struct JournalingBlockAssembler::UnitTestAccess<miner_tests_uid>
{
    static void NewBlock(JournalingBlockAssembler& jba)
    {
        std::lock_guard lock { jba.mMtx };
        jba.newBlock();
        CJournal::ReadLock journalLock { jba.mJournal };
        jba.mJournalPos = journalLock.begin();
    }
};
using JBAAccess = JournalingBlockAssembler::UnitTestAccess<miner_tests_uid>;

template <>
struct CoinsDB::UnitTestAccess<miner_tests_uid>
{
    UnitTestAccess() = delete;

    static void SetBestBlock(
        CoinsDB& provider,
        const uint256& hashBlock)
    {
        provider.hashBlock = hashBlock;
    }
};
using TestAccessCoinsDB = CoinsDB::UnitTestAccess<miner_tests_uid>;

template <>
struct CBlockIndex::UnitTestAccess<miner_tests_uid>
{
    UnitTestAccess() = delete;

    static void SetTime( CBlockIndex& index, int64_t time)
    {
        index.nTime = time;
    }

    static void AddTime( CBlockIndex& index, int64_t time)
    {
        index.nTime += time;
    }

    static void SubTime( CBlockIndex& index, int64_t time)
    {
        index.nTime -= time;
    }

    static void SetHeight( CBlockIndex& index, int32_t height, JournalingBlockAssembler& jba)
    {
        index.nHeight = height;
        // Since the height has changed force JBA to start a new block
        JBAAccess::NewBlock(jba);
    }
};
using TestAccessCBlockIndex = CBlockIndex::UnitTestAccess<miner_tests_uid>;

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

bool TestSequenceLocks(const CTransaction &tx, const Config& config, int flags) {
    CoinsDBView view{ *pcoinsTip };
    CCoinsViewMemPool viewMemPool{view, mempool};
    CCoinsViewCache cache{viewMemPool};

    return CheckSequenceLocks(*chainActive.Tip(), tx, config, flags, nullptr, &cache);
}

// NOTE: These tests rely on CreateNewBlock doing its own self-validation!
void Test_CreateNewBlock_validity(TestingSetup& testingSetup)
{
    // Note that by default, these tests run with size accounting enabled.
    CScript scriptPubKey =
        CScript() << ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909"
                              "a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112"
                              "de5c384df7ba0b8d578a4c702b6bf11d5f")
                  << OP_CHECKSIG;
    std::unique_ptr<CBlockTemplate> pblocktemplate;
    CMutableTransaction tx, tx2;
    CScript script;
    uint256 hash;
    TestMemPoolEntryHelper entry;
    entry.nFee = Amount(11);
    entry.nHeight = 11;

    LOCK(cs_main);
    fCheckpointsEnabled = false;

    // Simple block creation, nothing special yet:
    auto jba { std::dynamic_pointer_cast<JournalingBlockAssembler>(mining::g_miningFactory->GetAssembler()) };
    CBlockIndex* pindexPrev {nullptr};
    BOOST_CHECK(pblocktemplate = jba->CreateNewBlock(scriptPubKey, pindexPrev));

    // We can't make transactions until we have inputs. Therefore, load 100
    // blocks :)
    int32_t baseheight = 0;
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
        BOOST_CHECK(ProcessNewBlock(testingSetup.testConfig, shared_pblock, true, nullptr, CBlockSource::MakeLocal("test")));
        pblock->hashPrevBlock = pblock->GetHash();
    }

    // Just to make sure we can still make simple blocks.
    BOOST_CHECK(pblocktemplate = jba->CreateNewBlock(scriptPubKey, pindexPrev));

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
                             TxStorage::memory,
                             nullChangeSet);
        tx.vin[0].prevout = COutPoint(hash, 0);
    }
    testingSetup.testConfig.SetGenesisActivationHeight(500);
    testingSetup.testConfig.SetTestBlockCandidateValidity(false);
    BOOST_CHECK_NO_THROW(jba->CreateNewBlock(scriptPubKey, pindexPrev));
    testingSetup.testConfig.SetTestBlockCandidateValidity(true);
    BOOST_CHECK_THROW(jba->CreateNewBlock(scriptPubKey, pindexPrev), std::runtime_error);
    
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
                             TxStorage::memory,
                             nullChangeSet);
        tx.vin[0].prevout = COutPoint(hash, 0);
    }
    BOOST_CHECK(pblocktemplate = jba->CreateNewBlock(scriptPubKey, pindexPrev));
    mempool.Clear();

    // Orphan in mempool, template creation fails.
    hash = tx.GetId();
    mempool.AddUnchecked(hash, entry.Fee(LOWFEE).Time(GetTime()).FromTx(tx), TxStorage::memory, nullChangeSet);
    testingSetup.testConfig.SetTestBlockCandidateValidity(false);
    BOOST_CHECK_NO_THROW(jba->CreateNewBlock(scriptPubKey, pindexPrev));
    testingSetup.testConfig.SetTestBlockCandidateValidity(true);
    BOOST_CHECK_THROW(jba->CreateNewBlock(scriptPubKey, pindexPrev), std::runtime_error);
    
    mempool.Clear();

    // Child with higher priority than parent.
    tx.vin[0].scriptSig = CScript() << OP_1;
    tx.vin[0].prevout = COutPoint(txFirst[1]->GetId(), 0);
    tx.vout[0].nValue = BLOCKSUBSIDY - HIGHFEE;
    hash = tx.GetId();
    mempool.AddUnchecked(
        hash,
        entry.Fee(HIGHFEE).Time(GetTime()).SpendsCoinbase(true).FromTx(tx),
        TxStorage::memory, nullChangeSet);
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
        TxStorage::memory, nullChangeSet);
    BOOST_CHECK(pblocktemplate = jba->CreateNewBlock(scriptPubKey, pindexPrev));
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
        TxStorage::memory, nullChangeSet);
    testingSetup.testConfig.SetTestBlockCandidateValidity(false);
    BOOST_CHECK_NO_THROW(jba->CreateNewBlock(scriptPubKey, pindexPrev));
    testingSetup.testConfig.SetTestBlockCandidateValidity(true);
    BOOST_CHECK_THROW(jba->CreateNewBlock(scriptPubKey, pindexPrev), std::runtime_error);

    mempool.Clear();

    // Invalid (pre-p2sh) txn in mempool, template creation fails.
    std::array<int64_t, CBlockIndex::nMedianTimeSpan> times;
    for (int i = 0; i < CBlockIndex::nMedianTimeSpan; i++) {
        // Trick the MedianTimePast.
        times[i] = chainActive.Tip()
                       ->GetAncestor(chainActive.Tip()->GetHeight() - i)
                       ->GetBlockTime();
        TestAccessCBlockIndex::SetTime(
            *chainActive.Tip()->GetAncestor(chainActive.Tip()->GetHeight() - i),
            P2SH_ACTIVATION_TIME);
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
        TxStorage::memory, nullChangeSet);
    tx.vin[0].prevout = COutPoint(hash, 0);
    tx.vin[0].scriptSig = CScript()
                          << std::vector<uint8_t>(script.begin(), script.end());
    tx.vout[0].nValue -= LOWFEE;
    hash = tx.GetId();
    mempool.AddUnchecked(
        hash,
        entry.Fee(LOWFEE).Time(GetTime()).SpendsCoinbase(false).FromTx(tx),
        TxStorage::memory, nullChangeSet);
    testingSetup.testConfig.SetTestBlockCandidateValidity(false);
    BOOST_CHECK_NO_THROW(jba->CreateNewBlock(scriptPubKey, pindexPrev));
    testingSetup.testConfig.SetTestBlockCandidateValidity(true);
    BOOST_CHECK_THROW(jba->CreateNewBlock(scriptPubKey, pindexPrev), std::runtime_error);

    mempool.Clear();
    for (int i = 0; i < CBlockIndex::nMedianTimeSpan; i++) {
        // Restore the MedianTimePast.
        TestAccessCBlockIndex::SetTime(
            *chainActive.Tip()->GetAncestor(chainActive.Tip()->GetHeight() - i),
            times[i]);
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
        TxStorage::memory, nullChangeSet);
    tx.vout[0].scriptPubKey = CScript() << OP_2;
    hash = tx.GetId();
    mempool.AddUnchecked(
        hash,
        entry.Fee(HIGHFEE).Time(GetTime()).SpendsCoinbase(true).FromTx(tx),
        TxStorage::memory, nullChangeSet);
    testingSetup.testConfig.SetTestBlockCandidateValidity(false);
    BOOST_CHECK_NO_THROW(jba->CreateNewBlock(scriptPubKey, pindexPrev));
    testingSetup.testConfig.SetTestBlockCandidateValidity(true);
    BOOST_CHECK_THROW(jba->CreateNewBlock(scriptPubKey, pindexPrev), std::runtime_error);
    mempool.Clear();

    {
        // Subsidy changing.
        auto tipMarker = chainActive.Tip();
        // Create an actual 209999-long block chain (without valid blocks).
        while (chainActive.Tip()->GetHeight() < 209999) {
            CBlockHeader header;
            header.nTime = GetTime();
            header.hashPrevBlock = chainActive.Tip()->GetBlockHash();
            header.nBits = chainActive.Tip()->GetBits();
            CBlockIndex* next = mapBlockIndex.Insert( header );
            TestAccessCoinsDB::SetBestBlock(*pcoinsTip, next->GetBlockHash());
            chainActive.SetTip(next);
        }
        BOOST_CHECK(pblocktemplate = jba->CreateNewBlock(scriptPubKey, pindexPrev));
        // Extend to a 210000-long block chain.
        while (chainActive.Tip()->GetHeight() < 210000) {
            CBlockHeader header;
            header.nTime = GetTime();
            header.hashPrevBlock = chainActive.Tip()->GetBlockHash();
            header.nBits = chainActive.Tip()->GetBits();
            CBlockIndex* next = mapBlockIndex.Insert( header );
            TestAccessCoinsDB::SetBestBlock(*pcoinsTip, next->GetBlockHash());
            chainActive.SetTip(next);
        }
        BOOST_CHECK(pblocktemplate = jba->CreateNewBlock(scriptPubKey, pindexPrev));

        mining::g_miningFactory.reset();

        // Remove dummy blocks that were created in this scope from the active chain.
        chainActive.SetTip( tipMarker );
        TestAccessCoinsDB::SetBestBlock(*pcoinsTip, tipMarker->GetBlockHash());
    }

    mining::g_miningFactory = std::make_unique<mining::CMiningFactory>(testingSetup.testConfig);
    jba = std::dynamic_pointer_cast<JournalingBlockAssembler>(mining::g_miningFactory->GetAssembler());

    // non-final txs in mempool
    SetMockTime(chainActive.Tip()->GetMedianTimePast() + 1);
    int flags = LOCKTIME_VERIFY_SEQUENCE | LOCKTIME_MEDIAN_TIME_PAST;
    // height map
    std::vector<int32_t> prevheights;

    // Relative height locked.
    tx.nVersion = 2;
    tx.vin.resize(1);
    prevheights.resize(1);
    // Only 1 transaction.
    tx.vin[0].prevout = COutPoint(txFirst[0]->GetId(), 0);
    tx.vin[0].scriptSig = CScript() << OP_1;
    // txFirst[0] is the 2nd block
    tx.vin[0].nSequence = chainActive.Tip()->GetHeight() + 1;
    prevheights[0] = baseheight + 1;
    tx.vout.resize(1);
    tx.vout[0].nValue = BLOCKSUBSIDY - HIGHFEE;
    tx.vout[0].scriptPubKey = CScript() << OP_1;
    tx.nLockTime = 0;
    hash = tx.GetId();
    mempool.AddUnchecked(
        hash,
        entry.Fee(HIGHFEE).Time(GetTime()).SpendsCoinbase(true).FromTx(tx),
        TxStorage::memory, nullChangeSet);

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
    BOOST_CHECK(!TestSequenceLocks(CTransaction(tx), testingSetup.testConfig, flags));

    {
        CBlockIndex::TemporaryBlockIndex index{ *chainActive.Tip(), {} };

        TestAccessCBlockIndex::SetHeight(index, index->GetHeight() + 1, *jba);
        // Sequence locks pass on 2nd block.
        BOOST_CHECK(SequenceLocks(CTransaction(tx), flags, &prevheights, index));
    }

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
    mempool.AddUnchecked(hash, entry.Time(GetTime()).FromTx(tx), TxStorage::memory, nullChangeSet);

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
    BOOST_CHECK(!TestSequenceLocks(CTransaction(tx), testingSetup.testConfig, flags));

    for (int i = 0; i < CBlockIndex::nMedianTimeSpan; i++) {
        // Trick the MedianTimePast.
        TestAccessCBlockIndex::AddTime(
            *chainActive.Tip()->GetAncestor(chainActive.Tip()->GetHeight() - i),
            512);
    }

    {
        CBlockIndex::TemporaryBlockIndex index{ *chainActive.Tip(), {} };

        // Sequence locks pass 512 seconds later.
        BOOST_CHECK(
            SequenceLocks(CTransaction(tx), flags, &prevheights, index));
    }

    for (int i = 0; i < CBlockIndex::nMedianTimeSpan; i++) {
        // Undo tricked MTP.
        TestAccessCBlockIndex::SubTime(
            *chainActive.Tip()->GetAncestor(chainActive.Tip()->GetHeight() - i),
            512);
    }

    // Absolute height locked.
    tx.vin[0].prevout = COutPoint(txFirst[2]->GetId(), 0);
    tx.vin[0].nSequence = CTxIn::SEQUENCE_FINAL - 1;
    prevheights[0] = baseheight + 3;
    tx.nLockTime = chainActive.Tip()->GetHeight() + 1;
    hash = tx.GetId();
    mempool.AddUnchecked(hash, entry.Time(GetTime()).FromTx(tx), TxStorage::memory, nullChangeSet);

    {
        // Locktime fails.
        CValidationState state;
        BOOST_CHECK(!ContextualCheckTransactionForCurrentBlock(
                        testingSetup.testConfig,
                        CTransaction(tx),
                        chainActive.Height(),
                        chainActive.Tip()->GetMedianTimePast(),
                        state,
                        flags));
        BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-txns-nonfinal");
    }

    // Sequence locks pass.
    BOOST_CHECK(TestSequenceLocks(CTransaction(tx), testingSetup.testConfig, flags));

    {
        // Locktime passes on 2nd block.
        GlobalConfig config;
        CValidationState state;
        BOOST_CHECK(ContextualCheckTransaction(
            config, CTransaction(tx), state, chainActive.Tip()->GetHeight() + 2,
            chainActive.Tip()->GetMedianTimePast(), false));
    }

    // Absolute time locked.
    tx.vin[0].prevout = COutPoint(txFirst[3]->GetId(), 0);
    tx.nLockTime = chainActive.Tip()->GetMedianTimePast();
    prevheights.resize(1);
    prevheights[0] = baseheight + 4;
    hash = tx.GetId();
    mempool.AddUnchecked(hash, entry.Time(GetTime()).FromTx(tx), TxStorage::memory, nullChangeSet);

    {
        // Locktime fails.
        CValidationState state;
        BOOST_CHECK(!ContextualCheckTransactionForCurrentBlock(
                        testingSetup.testConfig,
                        CTransaction(tx),
                        chainActive.Height(),
                        chainActive.Tip()->GetMedianTimePast(),
                        state,
                        flags));
        BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-txns-nonfinal");
    }

    // Sequence locks pass.
    BOOST_CHECK(TestSequenceLocks(CTransaction(tx), testingSetup.testConfig, flags));

    {
        // Locktime passes 1 second later.
        GlobalConfig config;
        CValidationState state;
        BOOST_CHECK(ContextualCheckTransaction(
            config, CTransaction(tx), state, chainActive.Tip()->GetHeight() + 1,
            chainActive.Tip()->GetMedianTimePast() + 1, false));
    }

    // mempool-dependent transactions (not added)
    tx.vin[0].prevout = COutPoint(hash, 0);
    prevheights[0] = chainActive.Tip()->GetHeight() + 1;
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
    BOOST_CHECK(TestSequenceLocks(CTransaction(tx), testingSetup.testConfig, flags));
    tx.vin[0].nSequence = 1;
    // Sequence locks fail.
    BOOST_CHECK(!TestSequenceLocks(CTransaction(tx), testingSetup.testConfig, flags));
    tx.vin[0].nSequence = CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG;
    // Sequence locks pass.
    BOOST_CHECK(TestSequenceLocks(CTransaction(tx), testingSetup.testConfig, flags));
    tx.vin[0].nSequence = CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG | 1;
    // Sequence locks fail.
    BOOST_CHECK(!TestSequenceLocks(CTransaction(tx), testingSetup.testConfig, flags));

    BOOST_CHECK(pblocktemplate = jba->CreateNewBlock(scriptPubKey, pindexPrev));

    // None of the of the absolute height/time locked tx should have made it
    // into the template because we still check IsFinalTx in CreateNewBlock, but
    // relative locked txs will if inconsistently added to mempool. For now
    // these will still generate a valid template until BIP68 soft fork.
    BOOST_CHECK_EQUAL(pblocktemplate->GetBlockRef()->vtx.size(), 3UL);
    // However if we advance height by 1 and time by 512, all of them should be
    // mined.
    for (int i = 0; i < CBlockIndex::nMedianTimeSpan; i++) {
        // Trick the MedianTimePast.
        TestAccessCBlockIndex::AddTime(
            *chainActive.Tip()->GetAncestor(chainActive.Tip()->GetHeight() - i),
            512);
    }
    TestAccessCBlockIndex::SetHeight(*chainActive.Tip(), chainActive.Tip()->GetHeight() + 1, *jba);
    SetMockTime(chainActive.Tip()->GetMedianTimePast() + 1);

    BOOST_CHECK(pblocktemplate = jba->CreateNewBlock(scriptPubKey, pindexPrev));
    BOOST_CHECK_EQUAL(pblocktemplate->GetBlockRef()->vtx.size(), 5UL);

    TestAccessCBlockIndex::SetHeight(*chainActive.Tip(), chainActive.Tip()->GetHeight() - 1, *jba);
    SetMockTime(0);
    mempool.Clear();

    fCheckpointsEnabled = true;
}

void CheckBlockMaxSize(TestingSetup& testingSetup, uint64_t size, uint64_t expected)
{
    BOOST_REQUIRE(mining::g_miningFactory.get() == nullptr);
    testingSetup.testConfig.SetMaxGeneratedBlockSize(size);
    mining::CMiningFactory miningFactory { testingSetup.testConfig };
    BOOST_CHECK_EQUAL(miningFactory.GetAssembler()->GetMaxGeneratedBlockSize(), expected);
}

void Test_BlockAssembler_construction(TestingSetup& testingSetup)
{
    // we need to delete global mining factory because we want to 
    // create new mining factory for testing and JBA does not behave ok when
    // there are multiple instances of it
    mining::g_miningFactory.reset();
    // Make sure that default values are not overriden
    BOOST_REQUIRE(!testingSetup.testConfig.MaxGeneratedBlockSizeOverridden());
    
    uint64_t nDefaultMaxGeneratedBlockSize = testingSetup.testConfig.GetMaxGeneratedBlockSize();
    uint64_t nDefaultMaxBlockSize = testingSetup.testConfig.GetMaxBlockSize();

    // We are working on a fake chain and need to protect ourselves.
    LOCK(cs_main);

    // Test around historical 1MB (plus one byte because that's mandatory)
    BOOST_REQUIRE(testingSetup.testConfig.SetMaxBlockSize(ONE_MEGABYTE + 1));
    CheckBlockMaxSize(testingSetup, 0, 1000); 
    CheckBlockMaxSize(testingSetup, 1000, 1000);
    CheckBlockMaxSize(testingSetup, 1001, 1001);
    CheckBlockMaxSize(testingSetup, 12345, 12345);

    CheckBlockMaxSize(testingSetup, ONE_MEGABYTE - 1001, ONE_MEGABYTE - 1001);
    CheckBlockMaxSize(testingSetup, ONE_MEGABYTE - 1000, ONE_MEGABYTE - 1000);
    CheckBlockMaxSize(testingSetup, ONE_MEGABYTE - 999, ONE_MEGABYTE - 999);
    CheckBlockMaxSize(testingSetup, ONE_MEGABYTE, ONE_MEGABYTE - 999);

    // Test around default cap
    BOOST_REQUIRE(testingSetup.testConfig.SetMaxBlockSize(nDefaultMaxBlockSize));

    // Now we can use the default max block size.
    CheckBlockMaxSize(testingSetup, nDefaultMaxBlockSize - 1001, nDefaultMaxBlockSize - 1001);
    CheckBlockMaxSize(testingSetup, nDefaultMaxBlockSize - 1000, nDefaultMaxBlockSize - 1000);
    CheckBlockMaxSize(testingSetup, nDefaultMaxBlockSize - 999, nDefaultMaxBlockSize - 1000);
    CheckBlockMaxSize(testingSetup, nDefaultMaxBlockSize, nDefaultMaxBlockSize - 1000);

    // If the parameter is not specified, we use
    // max(1K, min(DEFAULT_MAX_BLOCK_SIZE - 1K, DEFAULT_MAX_GENERATED_BLOCK_SIZE))
    {
        const auto expected { std::max(ONE_KILOBYTE,
                                std::min(nDefaultMaxBlockSize - ONE_KILOBYTE,
                                    nDefaultMaxGeneratedBlockSize)) };
        
        // Set generated max size to default
        CheckBlockMaxSize(testingSetup, nDefaultMaxGeneratedBlockSize, expected);
    }
}

void CheckBlockMaxSizeForTime(TestingSetup& testingSetup, uint64_t medianPastTime, uint64_t expectedSize)
{
    BlockIndexStore blockIndexStore;

    {
        LOCK(cs_main);

        // Construct chain  with desired median time. Set time of each block to 
        // the same value to get desired median past time.
        int32_t height = 0;
        uint256 prevHash;
        do
        {   
            CBlockHeader header;
            header.nTime = medianPastTime;
            header.hashPrevBlock = prevHash;
            header.nBits =
                GetNextWorkRequired(
                    chainActive.Tip(),
                    &header,
                    GlobalConfig::GetConfig() );
            CBlockIndex* next = blockIndexStore.Insert( header );

            prevHash = next->GetBlockHash();

            // chainActive is used by BlockAssembler to get median past time, which is used to select default block size
            chainActive.SetTip( next );
        }
        while(++height < 11);
    }

    // Make sure that we got correct median past time.
    BOOST_REQUIRE_EQUAL(chainActive.Tip()->GetMedianTimePast(), static_cast<int32_t>(medianPastTime));


    BOOST_REQUIRE(mining::g_miningFactory.get() == nullptr);
    mining::CMiningFactory miningFactory { testingSetup.testConfig };
    BOOST_CHECK_EQUAL(miningFactory.GetAssembler()->GetMaxGeneratedBlockSize(), expectedSize);

    {
        LOCK(cs_main);
        chainActive.SetTip(nullptr); // cleanup
    }
}

void Test_BlockAssembler_construction_activate_new_blocksize(TestingSetup& testingSetup)
{
    // we need to delete global mining factory because we want to 
    // create new mining factory for testing and JBA does not behave ok when
    // there are multiple instances of it
    mining::g_miningFactory.reset();

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

    testingSetup.testConfig.SetDefaultBlockSizeParams(defaultParams);
    
    CheckBlockMaxSizeForTime(testingSetup, 999, 3000);
    CheckBlockMaxSizeForTime(testingSetup, 1000, 4000);
    CheckBlockMaxSizeForTime(testingSetup, 10001, 4000);
    
    // When explicitly set, defaults values must not be used
    testingSetup.testConfig.SetMaxGeneratedBlockSize(3333);
    testingSetup.testConfig.SetMaxGeneratedBlockSize(3333);
    CheckBlockMaxSizeForTime(testingSetup, 10001, 3333);
}


void Test_JournalingBlockAssembler_Construction(TestingSetup& testingSetup)
{
    CScript scriptPubKey =
        CScript() << ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909"
                              "a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112"
                              "de5c384df7ba0b8d578a4c702b6bf11d5f")
                  << OP_CHECKSIG;
    CBlockIndex* pindexPrev {nullptr};

    std::unique_ptr<CBlockTemplate> bt { mining::g_miningFactory->GetAssembler()->CreateNewBlock(scriptPubKey, pindexPrev) };
    BOOST_REQUIRE(bt);
    BOOST_REQUIRE(bt->GetBlockRef());
    BOOST_CHECK_EQUAL(bt->GetBlockRef()->vtx.size(), 1U);
}

void Test_CreateNewBlock_JBA_Config(TestingSetup& testingSetup)
{
    CScript scriptPubKey =
        CScript() << ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909"
                              "a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112"
                              "de5c384df7ba0b8d578a4c702b6bf11d5f")
                  << OP_CHECKSIG;
    TestMemPoolEntryHelper entry {};
    entry.nFee = Amount(11);
    entry.nHeight = 11;

    gArgs.ForceSetArg("-jbamaxtxnbatch", "1");
    gArgs.ForceSetArg("-jbafillafternewblock", "0");
    auto* jbaPtr = dynamic_cast<JournalingBlockAssembler*>(mining::g_miningFactory->GetAssembler().get());
    BOOST_REQUIRE(jbaPtr != nullptr);
    jbaPtr->ReadConfigParameters();

    LOCK(cs_main);
    fCheckpointsEnabled = false;

    // Simple block creation, nothing special yet:
    CBlockIndex* pindexPrev {nullptr};
    std::unique_ptr<CBlockTemplate> pblocktemplate {};
    BOOST_CHECK(pblocktemplate = mining::g_miningFactory->GetAssembler()->CreateNewBlock(scriptPubKey, pindexPrev));

    // We can't make transactions until we have inputs. Therefore, load 100 blocks
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
        txCoinbase.vout.resize(1);
        txCoinbase.vout[0].scriptPubKey = CScript();
        pblock->vtx[0] = MakeTransactionRef(std::move(txCoinbase));
        if (txFirst.size() < 4)
            txFirst.push_back(pblock->vtx[0]);
        pblock->hashMerkleRoot = BlockMerkleRoot(*pblock);
        pblock->nNonce = blockinfo[i].nonce;
        std::shared_ptr<const CBlock> shared_pblock = std::make_shared<const CBlock>(*pblock);
        BOOST_CHECK(ProcessNewBlock(testingSetup.testConfig, shared_pblock, true, nullptr, CBlockSource::MakeLocal("test")));
        pblock->hashPrevBlock = pblock->GetHash();
    }

    const Amount BLOCKSUBSIDY { 50 * COIN };
    const Amount LOWFEE {CENT};
    constexpr unsigned NUM_TXNS {1000};

    CMutableTransaction tx {};
    tx.vin.resize(1);
    tx.vin[0].prevout = COutPoint(txFirst[0]->GetId(), 0);
    tx.vout.resize(1);
    tx.vout[0].nValue = BLOCKSUBSIDY;
    for (unsigned int i = 0; i < NUM_TXNS; ++i) {
        tx.vout[0].nValue -= LOWFEE;
        uint256 hash = tx.GetId();
        // Only first tx spends coinbase.
        bool spendsCoinbase = (i == 0) ? true : false;
        mempool.AddUnchecked(hash,
                             entry.Fee(LOWFEE)
                                 .Time(GetTime())
                                 .SpendsCoinbase(spendsCoinbase)
                                 .FromTx(tx),
                             TxStorage::memory,
                             nullChangeSet);
        tx.vin[0].prevout = COutPoint(hash, 0);
    }

    // CreateNewBlock will only include what we have processed so far from the journal
    BOOST_CHECK(pblocktemplate = mining::g_miningFactory->GetAssembler()->CreateNewBlock(scriptPubKey, pindexPrev));
    BOOST_CHECK(pblocktemplate->GetBlockRef()->vtx.size() < NUM_TXNS);

    gArgs.ForceSetArg("-jbamaxtxnbatch", "1");
    gArgs.ForceSetArg("-jbafillafternewblock", "1");
    jbaPtr->ReadConfigParameters();
    // CreateNewBlock will finish processing and including everything in the journal
    BOOST_CHECK(pblocktemplate = mining::g_miningFactory->GetAssembler()->CreateNewBlock(scriptPubKey, pindexPrev));
    BOOST_CHECK_EQUAL(pblocktemplate->GetBlockRef()->vtx.size(), NUM_TXNS + 1);
}


BOOST_FIXTURE_TEST_SUITE(miner_tests_journal, JournalingTestingSetup)
BOOST_AUTO_TEST_CASE(CreateNewBlock_validity)
{
    Test_CreateNewBlock_validity(*this);
}
BOOST_AUTO_TEST_CASE(BlockAssembler_construction)
{
    Test_BlockAssembler_construction(*this);
}
BOOST_AUTO_TEST_CASE(BlockAssembler_construction_activate_new_blocksize)
{
    Test_BlockAssembler_construction_activate_new_blocksize(*this);
}
BOOST_AUTO_TEST_CASE(JournalingBlockAssembler_Construction)
{
    Test_JournalingBlockAssembler_Construction(*this);
}
BOOST_AUTO_TEST_CASE(CreateNewBlock_JBA_Config)
{
    Test_CreateNewBlock_JBA_Config(*this);
}
BOOST_AUTO_TEST_SUITE_END()
