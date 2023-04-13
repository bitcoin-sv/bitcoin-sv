// Copyright (c) 2020 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "mining/factory.h"
#include "mining/journal.h"
#include "mining/journal_builder.h"
#include "mining/journal_change_set.h"
#include "random.h"
#include "txmempool.h"
#include "config.h"

#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>

using namespace mining;

namespace
{
    // Only used as unique identifier
    class journal_tests_uid;
}

// For private member access to CJournalEntry
template<>
struct CJournalEntry::UnitTestAccess<journal_tests_uid>
{
    template<typename... Args>
    static CJournalEntry Make(Args&&... args)
    {
        return { std::forward<Args>(args)... };
    }
};
using JournalEntryAccess = CJournalEntry::UnitTestAccess<journal_tests_uid>;

namespace
{
    const size_t txnSize = 500;
    // Generate a new random transaction
    CJournalEntry NewTxn(GroupID groupId, bool isPaying)
    {
        static uint64_t unique {1ULL << 33};  // thwart variable size integer encoding
        CMutableTransaction txn {};
        txn.vout.resize(1);
        std::vector<uint8_t> stuff;
        stuff.resize(txnSize - 32); // make serialized transaction 500 bytes
        txn.vout[0].scriptPubKey = CScript() << stuff << OP_DROP << unique++ << OP_DROP;
        const auto tx = MakeTransactionRef(std::move(txn));
        return JournalEntryAccess::Make(
            std::make_shared<CTransactionWrapper>(tx, nullptr),
            tx->GetTotalSize(), Amount{0}, GetTime(), groupId, isPaying);
    }
    void NewChangeSet(CJournalBuilder &builder, size_t groupSize, GroupID groupId)
    {
        auto changeSet = builder.getNewChangeSet(JournalUpdateReason::NEW_TXN);
        while (groupSize--) {
            CJournalEntry singletxn { NewTxn(groupId, groupSize == 1) };
            changeSet->addOperation(CJournalChangeSet::Operation::ADD, singletxn);
        }
        changeSet->apply();
    }
    void NewChangeSet(CJournalBuilder &builder, size_t nTransactions)
    {
        NewChangeSet(builder, nTransactions, std::nullopt);
    }

    std::unique_ptr<CBlockTemplate> CreateBlock()
    {
        CBlockIndex* pindexPrev {nullptr};
        std::unique_ptr<CBlockTemplate> pblocktemplate;
        CScript scriptPubKey =
        CScript() << ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909"
                              "a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112"
                              "de5c384df7ba0b8d578a4c702b6bf11d5f")
                  << OP_CHECKSIG;
        BOOST_CHECK(pblocktemplate = mining::g_miningFactory->GetAssembler()->CreateNewBlock(scriptPubKey, pindexPrev));
        return pblocktemplate;
    }

    //
    // return number of user transactions in the block
    //
    size_t CountBlockUserTxns(const std::unique_ptr<CBlockTemplate> &block)
    {
        return block->GetBlockRef()->GetTransactionCount() - 1;
    }

    //
    // return number of user transactions in the journal
    //
    size_t CountJournalTxns(CJournalBuilder& builder)
    {
        return builder.getCurrentJournal()->size();
    }

    void PretendTransactionsMinedElsewhere(CJournalBuilder &builder,
                                           std::unique_ptr<CBlockTemplate> pblocktemplate,
                                           size_t transactionsToDrop)
    {
        auto vtx = pblocktemplate->GetBlockRef()->vtx;
        BOOST_REQUIRE_GT(vtx.size(), transactionsToDrop);
        auto changeSet = builder.getNewChangeSet(JournalUpdateReason::NEW_BLOCK);
        for (auto iter = std::next(vtx.begin(), 1); transactionsToDrop > 0;
             ++iter, --transactionsToDrop)
        {
            auto txn = *iter;
            const auto tx = MakeTransactionRef(*txn);
            CJournalEntry entry { JournalEntryAccess::Make(
                std::make_shared<CTransactionWrapper>(tx, nullptr),
                tx->GetTotalSize(), Amount{0}, GetTime(), std::nullopt, false) };
            changeSet->addOperation(CJournalChangeSet::Operation::REMOVE, entry);
        }
        changeSet->apply();
    }
}

namespace mining
{
    // For error reporting
    std::ostream& operator<<(std::ostream& str, CJournalTester::TxnOrder order);
}

BOOST_FIXTURE_TEST_SUITE(journal_assembler_group_tests, TestChain100Setup)

BOOST_AUTO_TEST_CASE(TestJournalAddGroup)
{
    // get the mempool builder
    CJournalBuilder &builder = mempool.getJournalBuilder();

    Config &config = GlobalConfig::GetConfig();
    const size_t maxUserTxns = 10;
    config.SetMaxGeneratedBlockSize(1000 + maxUserTxns * txnSize + 1);

    CJournalPtr journal { builder.getCurrentJournal() };
    std::unique_ptr<CBlockTemplate> block;

    // empty journal and block
    block = CreateBlock();
    BOOST_CHECK_EQUAL(CountJournalTxns(builder), 0U);
    BOOST_CHECK_EQUAL(CountBlockUserTxns(block), 0U);

    // add a transaction
    NewChangeSet(builder, 1);
    block = CreateBlock();
    BOOST_CHECK_EQUAL(CountJournalTxns(builder), 1U);
    BOOST_CHECK_EQUAL(CountBlockUserTxns(block), 1U);


    // add more transactions than will fit in the block
    NewChangeSet(builder, maxUserTxns);
    block = CreateBlock();
    BOOST_CHECK_EQUAL(CountJournalTxns(builder), 1U + maxUserTxns);
    BOOST_CHECK_EQUAL(CountBlockUserTxns(block), maxUserTxns);

    // remove some stuff from journal
    PretendTransactionsMinedElsewhere(builder, std::move(block), maxUserTxns);
    block = CreateBlock();
    BOOST_CHECK_EQUAL(CountJournalTxns(builder), 1U);
    BOOST_CHECK_EQUAL(CountBlockUserTxns(block), 1U);

    // add a group that will fit in the block
    NewChangeSet(builder, maxUserTxns - 4, 1);
    block = CreateBlock();
    BOOST_CHECK_EQUAL(CountJournalTxns(builder), 1U + maxUserTxns - 4);
    BOOST_CHECK_EQUAL(CountBlockUserTxns(block), 1U + maxUserTxns - 4);

    // add a group that will just fit in the block
    NewChangeSet(builder, 3, 2);
    block = CreateBlock();
    BOOST_CHECK_EQUAL(CountJournalTxns(builder), 1U + maxUserTxns - 4 + 3);
    BOOST_CHECK_EQUAL(CountBlockUserTxns(block), 1U + maxUserTxns - 4 + 3);

    // remove stuff from journal
    PretendTransactionsMinedElsewhere(builder, std::move(block), maxUserTxns - 1);
    block = CreateBlock();
    BOOST_CHECK_EQUAL(CountJournalTxns(builder), 1U);
    BOOST_CHECK_EQUAL(CountBlockUserTxns(block), 1U);

    // add a group that will just not fit in the block
    NewChangeSet(builder, maxUserTxns, 3);
    block = CreateBlock();
    BOOST_CHECK_EQUAL(CountJournalTxns(builder), 1U + maxUserTxns);
    BOOST_CHECK_EQUAL(CountBlockUserTxns(block), 1U);

    // remove one transaction from journal, now the group should fit
    PretendTransactionsMinedElsewhere(builder, std::move(block), 1);
    block = CreateBlock();
    BOOST_CHECK_EQUAL(CountJournalTxns(builder), maxUserTxns);
    BOOST_CHECK_EQUAL(CountBlockUserTxns(block), maxUserTxns);
}

BOOST_AUTO_TEST_SUITE_END();

