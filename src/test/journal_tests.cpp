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

    // Tester to inspect journals
    CJournalTester tester { builder->getCurrentJournal() };

    // Journal is empty to start with
    BOOST_CHECK_EQUAL(tester.journalSize(), 0);

    // Play single txn into the journal and check it
    CJournalChangeSetPtr changeSet { builder->getNewChangeSet(JournalUpdateReason::NEW_TXN) };
    CJournalEntry singletxn { NewTxn() };
    changeSet->addOperation(CJournalChangeSet::Operation::ADD, singletxn);
    BOOST_CHECK(changeSet->getSimple());
    changeSet.reset();
    BOOST_CHECK_EQUAL(tester.journalSize(), 1);
    BOOST_CHECK(tester.checkTxnExists(singletxn));
    BOOST_CHECK_EQUAL(tester.checkTxnOrdering(singletxn, singletxn), CJournalTester::TxnOrder::DUPLICATE);

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
    BOOST_CHECK(changeSet->getSimple());
    changeSet.reset();
    BOOST_CHECK_EQUAL(tester.journalSize(), 4);
    for(const auto& [ op, txn ] : ops)
    {
        BOOST_CHECK(tester.checkTxnExists(txn));
    }
    BOOST_CHECK_EQUAL(tester.checkTxnOrdering(ops[0].second, ops[1].second), CJournalTester::TxnOrder::BEFORE);
    BOOST_CHECK_EQUAL(tester.checkTxnOrdering(ops[1].second, ops[0].second), CJournalTester::TxnOrder::AFTER);
    BOOST_CHECK_EQUAL(tester.checkTxnOrdering(ops[0].second, ops[2].second), CJournalTester::TxnOrder::BEFORE);

    // Remove some txns
    OpList ops2 = {
        { CJournalChangeSet::Operation::REMOVE, ops[0].second },
        { CJournalChangeSet::Operation::REMOVE, ops[2].second }
    };
    changeSet = builder->getNewChangeSet(JournalUpdateReason::NEW_BLOCK);
    for(const auto& [ op, txn ] : ops2)
    {
        changeSet->addOperation(op, txn);
    }
    BOOST_CHECK(!changeSet->getSimple());
    changeSet.reset();
    tester.updateJournal(builder->getCurrentJournal());
    BOOST_CHECK_EQUAL(tester.journalSize(), 2);
    BOOST_CHECK(tester.checkTxnExists(singletxn));
    BOOST_CHECK(!tester.checkTxnExists(ops[0].second));
    BOOST_CHECK(tester.checkTxnExists(ops[1].second));
    BOOST_CHECK(!tester.checkTxnExists(ops[2].second));
    BOOST_CHECK_EQUAL(tester.checkTxnOrdering(ops[0].second, ops[1].second), CJournalTester::TxnOrder::NOTFOUND);
    BOOST_CHECK_EQUAL(tester.checkTxnOrdering(singletxn, ops[1].second), CJournalTester::TxnOrder::BEFORE);
}

BOOST_AUTO_TEST_CASE(TestJournalReorg)
{
    // Create builder to manage journals
    CJournalBuilderPtr builder { std::make_unique<CJournalBuilder>() };
    BOOST_CHECK(builder);

    // Tester to inspect journals
    CJournalTester tester { builder->getCurrentJournal() };

    // Journal is empty to start with
    BOOST_CHECK_EQUAL(tester.journalSize(), 0);

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
    BOOST_CHECK_EQUAL(tester.journalSize(), 4);
 
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
    BOOST_CHECK(!changeSet->getSimple());
    changeSet.reset();
    tester.updateJournal(builder->getCurrentJournal());

    BOOST_CHECK_EQUAL(tester.journalSize(), 3);
    BOOST_CHECK(!tester.checkTxnExists(singletxn));
    BOOST_CHECK(!tester.checkTxnExists(ops[0].second));
    BOOST_CHECK(tester.checkTxnExists(ops[1].second));
    BOOST_CHECK(!tester.checkTxnExists(ops[2].second));
    BOOST_CHECK(tester.checkTxnExists(ops[3].second));
    BOOST_CHECK_EQUAL(tester.checkTxnOrdering(ops[1].second, ops[3].second), CJournalTester::TxnOrder::BEFORE);
    BOOST_CHECK_EQUAL(tester.checkTxnOrdering(ops2[3].second, ops[1].second), CJournalTester::TxnOrder::BEFORE);
}

BOOST_AUTO_TEST_SUITE_END();

