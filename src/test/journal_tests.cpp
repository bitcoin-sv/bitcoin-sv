// Copyright (c) 2019 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "mining/journal.h"
#include "mining/journal_builder.h"
#include "mining/journal_change_set.h"
#include "random.h"

#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>

using namespace mining;

namespace
{
    // Generate a new random transaction
    CJournalEntry NewTxn()
    {
        static uint32_t lockTime {0};
        CMutableTransaction txn {};
        txn.nLockTime = lockTime++;
        return { MakeTransactionRef(std::move(txn)), std::make_shared<AncestorDescendantCounts>(1, 1), Amount{0}, 0 };
    }
}

namespace mining
{
    // For error reporting
    std::ostream& operator<<(std::ostream& str, CJournalTester::TxnOrder order)
    {
        str << enum_cast<std::string>(order);
        return str;
    }
}

BOOST_FIXTURE_TEST_SUITE(journal_tests, TestingSetup)

BOOST_AUTO_TEST_CASE(TestJournalAddRemove)
{
    // Create builder to manage journals
    CJournalBuilderPtr builder { std::make_unique<CJournalBuilder>() };
    BOOST_CHECK(builder);

    // Check journal initial state
    CJournalPtr journal { builder->getCurrentJournal() };
    BOOST_CHECK_EQUAL(journal->size(), 0);
    BOOST_CHECK_EQUAL(journal->getLastInvalidatingTime(), 0);
    BOOST_CHECK(journal->getCurrent());

    // Check index initial state
    CJournal::Index index {};
    BOOST_CHECK(!index.valid());
    index = CJournal::ReadLock{journal}.begin();
    BOOST_CHECK(index.valid());
    BOOST_CHECK(index == CJournal::ReadLock{journal}.end());

    // Play single txn into the journal and check it
    CJournalChangeSetPtr changeSet { builder->getNewChangeSet(JournalUpdateReason::NEW_TXN) };
    CJournalEntry singletxn { NewTxn() };
    changeSet->addOperation(CJournalChangeSet::Operation::ADD, singletxn);
    BOOST_CHECK(changeSet->getTailAppendOnly());
    changeSet.reset();
    BOOST_CHECK_EQUAL(journal->size(), 1);
    BOOST_CHECK(CJournalTester{journal}.checkTxnExists(singletxn));
    BOOST_CHECK_EQUAL(CJournalTester{journal}.checkTxnOrdering(singletxn, singletxn), CJournalTester::TxnOrder::DUPLICATETX);

    // begin() now points to this first txn
    index.reset();
    BOOST_CHECK(index.valid());
    BOOST_CHECK(index != CJournal::ReadLock{journal}.end());
    BOOST_CHECK(index == CJournal::ReadLock{journal}.begin());
    BOOST_CHECK(index.at().getTxn()->GetId() == singletxn.getTxn()->GetId());

    // Play a series of txns into the journal
    using OpList = std::vector<std::pair<CJournalChangeSet::Operation, CJournalEntry>>;
    OpList ops {
        { CJournalChangeSet::Operation::ADD, NewTxn() },
        { CJournalChangeSet::Operation::ADD, NewTxn() },
        { CJournalChangeSet::Operation::ADD, NewTxn() }
    };
    changeSet = builder->getNewChangeSet(JournalUpdateReason::NEW_TXN);
    for(const auto& [ op, txn ] : ops)
    {
        changeSet->addOperation(op, txn);
    }
    BOOST_CHECK(changeSet->getTailAppendOnly());
    changeSet.reset();
    BOOST_CHECK_EQUAL(CJournalTester{journal}.journalSize(), 4);
    for(const auto& [ op, txn ] : ops)
    {
        // supresses unused parameter warning
        (void)op;
        BOOST_CHECK(CJournalTester{journal}.checkTxnExists(txn));
    }
    BOOST_CHECK_EQUAL(CJournalTester{journal}.checkTxnOrdering(ops[0].second, ops[1].second), CJournalTester::TxnOrder::BEFORE);
    BOOST_CHECK_EQUAL(CJournalTester{journal}.checkTxnOrdering(ops[1].second, ops[0].second), CJournalTester::TxnOrder::AFTER);
    BOOST_CHECK_EQUAL(CJournalTester{journal}.checkTxnOrdering(ops[0].second, ops[2].second), CJournalTester::TxnOrder::BEFORE);

    // Check iterator movement
    BOOST_CHECK(index.valid());
    BOOST_CHECK((++index).at().getTxn()->GetId() == ops[0].second.getTxn()->GetId());
    BOOST_CHECK((++index).at().getTxn()->GetId() == ops[1].second.getTxn()->GetId());
    BOOST_CHECK((++index).at().getTxn()->GetId() == ops[2].second.getTxn()->GetId());
    BOOST_CHECK(index != CJournal::ReadLock{journal}.end());
    ++index;
    BOOST_CHECK(index.valid());
    BOOST_CHECK(index == CJournal::ReadLock{journal}.end());

    // Remove some txns
    OpList ops2 = {
        { CJournalChangeSet::Operation::REMOVE, ops[0].second },
        { CJournalChangeSet::Operation::REMOVE, ops[2].second }
    };
    changeSet = builder->getNewChangeSet(JournalUpdateReason::REMOVE_TXN);
    for(const auto& [ op, txn ] : ops2)
    {
        changeSet->addOperation(op, txn);
    }
    BOOST_CHECK(!changeSet->getTailAppendOnly());
    changeSet.reset();
    BOOST_CHECK_EQUAL(CJournalTester{journal}.journalSize(), 2);
    BOOST_CHECK(CJournalTester{journal}.checkTxnExists(singletxn));
    BOOST_CHECK(!CJournalTester{journal}.checkTxnExists(ops[0].second));
    BOOST_CHECK(CJournalTester{journal}.checkTxnExists(ops[1].second));
    BOOST_CHECK(!CJournalTester{journal}.checkTxnExists(ops[2].second));
    BOOST_CHECK_EQUAL(CJournalTester{journal}.checkTxnOrdering(ops[0].second, ops[1].second), CJournalTester::TxnOrder::NOTFOUND);
    BOOST_CHECK_EQUAL(CJournalTester{journal}.checkTxnOrdering(singletxn, ops[1].second), CJournalTester::TxnOrder::BEFORE);

    // Iterator is no longer valid
    BOOST_CHECK(!index.valid());
    BOOST_CHECK_THROW(index.reset(), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(TestJournalReorg)
{
    // Create builder to manage journals
    CJournalBuilderPtr builder { std::make_unique<CJournalBuilder>() };
    BOOST_CHECK(builder);

    // Journal is empty to start with
    CJournalPtr journal { builder->getCurrentJournal() };
    BOOST_CHECK_EQUAL(journal->size(), 0);

    // Populate with some initial txns
    using OpList = std::vector<std::pair<CJournalChangeSet::Operation, CJournalEntry>>;
    OpList ops {
        { CJournalChangeSet::Operation::ADD, NewTxn() },
        { CJournalChangeSet::Operation::ADD, NewTxn() },
        { CJournalChangeSet::Operation::ADD, NewTxn() },
        { CJournalChangeSet::Operation::ADD, NewTxn() }
    };
    CJournalChangeSetPtr changeSet { builder->getNewChangeSet(JournalUpdateReason::NEW_TXN) };
    for(const auto& [ op, txn ] : ops)
    {
        changeSet->addOperation(op, txn);
    }
    changeSet.reset();
    BOOST_CHECK_EQUAL(CJournalTester{journal}.journalSize(), 4);
    BOOST_CHECK(journal->getCurrent());
 
    // Apply a reorg with a mix of additions and removals
    CJournalEntry singletxn { NewTxn() };
    OpList ops2 = {
        { CJournalChangeSet::Operation::ADD, singletxn },
        { CJournalChangeSet::Operation::REMOVE, ops[0].second },
        { CJournalChangeSet::Operation::REMOVE, ops[2].second },
        { CJournalChangeSet::Operation::ADD, NewTxn() },
        { CJournalChangeSet::Operation::REMOVE, singletxn }
    };
    changeSet = builder->getNewChangeSet(JournalUpdateReason::REORG);
    for(const auto& [ op, txn ] : ops2)
    {
        changeSet->addOperation(op, txn);
    }
    BOOST_CHECK(!changeSet->getTailAppendOnly());
    changeSet.reset();
    BOOST_CHECK(!journal->getCurrent());
    journal = builder->getCurrentJournal();
    BOOST_CHECK(journal->getCurrent());

    BOOST_CHECK_EQUAL(CJournalTester{journal}.journalSize(), 3);
    BOOST_CHECK(!CJournalTester{journal}.checkTxnExists(singletxn));
    BOOST_CHECK(!CJournalTester{journal}.checkTxnExists(ops[0].second));
    BOOST_CHECK(CJournalTester{journal}.checkTxnExists(ops[1].second));
    BOOST_CHECK(!CJournalTester{journal}.checkTxnExists(ops[2].second));
    BOOST_CHECK(CJournalTester{journal}.checkTxnExists(ops[3].second));
    BOOST_CHECK_EQUAL(CJournalTester{journal}.checkTxnOrdering(ops[1].second, ops[3].second), CJournalTester::TxnOrder::BEFORE);
    BOOST_CHECK_EQUAL(CJournalTester{journal}.checkTxnOrdering(ops2[3].second, ops[1].second), CJournalTester::TxnOrder::BEFORE);
}

BOOST_AUTO_TEST_SUITE_END();

