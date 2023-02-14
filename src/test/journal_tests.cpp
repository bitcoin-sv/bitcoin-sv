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
    // Generate a new random transaction
    CJournalEntry NewTxn()
    {
        static uint32_t lockTime {0};
        CMutableTransaction txn {};
        txn.nLockTime = lockTime++;
        const auto tx = MakeTransactionRef(std::move(txn));
        return JournalEntryAccess::Make(
            std::make_shared<CTransactionWrapper>(tx, nullptr),
            tx->GetTotalSize(), Amount{0}, GetTime(), std::nullopt, false);
    }
    // Generate a new random transaction that depends on another
    CJournalEntry NewTxn(std::initializer_list<CTransactionWrapperRef> other)
    {
        static uint32_t lockTime {0}; // separate counter is OK as we have an input
        CMutableTransaction txn {};
        for (auto prev: other) {
            txn.vin.emplace_back(CTxIn{COutPoint(prev->GetId(), 0), CScript()});
        }
        txn.nLockTime = lockTime++;
        const auto tx = MakeTransactionRef(std::move(txn));
        return JournalEntryAccess::Make(
             std::make_shared<CTransactionWrapper>(tx, nullptr),
             tx->GetTotalSize(), Amount{0}, GetTime(), std::nullopt, false);
    }

    CJournalChangeSetPtr changeSet(CJournalBuilder* builder, JournalUpdateReason reason, std::initializer_list<std::pair<CJournalChangeSet::Operation, CJournalEntry>> ops)
    {
        CJournalChangeSetPtr changeSet = builder->getNewChangeSet(reason);
        for (const auto& [ op, txn ] : ops)
        {
            changeSet->addOperation(op, txn);
        }
        return changeSet;
    }

    CJournalChangeSetPtr reorg(CJournalBuilder* builder, std::initializer_list<std::pair<CJournalChangeSet::Operation, CJournalEntry>> ops) {
        return changeSet(builder, JournalUpdateReason::REORG, ops);
    }
    std::pair<CJournalChangeSet::Operation, CJournalEntry> add(CJournalEntry entry) {
        return std::make_pair(CJournalChangeSet::Operation::ADD, entry);
    }
    std::pair<CJournalChangeSet::Operation, CJournalEntry> remove(CJournalEntry entry) {
        return std::make_pair(CJournalChangeSet::Operation::REMOVE, entry);
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
    BOOST_CHECK_EQUAL(journal->size(), 0U);
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
    BOOST_CHECK_EQUAL(journal->size(), 1U);
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
    BOOST_CHECK_EQUAL(CJournalTester{journal}.journalSize(), 4U);
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
    BOOST_CHECK_EQUAL(CJournalTester{journal}.journalSize(), 2U);
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
    BOOST_CHECK_EQUAL(journal->size(), 0U);

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
    BOOST_CHECK_EQUAL(CJournalTester{journal}.journalSize(), 4U);
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

    BOOST_CHECK_EQUAL(CJournalTester{journal}.journalSize(), 3U);
    BOOST_CHECK(!CJournalTester{journal}.checkTxnExists(singletxn));
    BOOST_CHECK(!CJournalTester{journal}.checkTxnExists(ops[0].second));
    BOOST_CHECK(CJournalTester{journal}.checkTxnExists(ops[1].second));
    BOOST_CHECK(!CJournalTester{journal}.checkTxnExists(ops[2].second));
    BOOST_CHECK(CJournalTester{journal}.checkTxnExists(ops[3].second));
    BOOST_CHECK_EQUAL(CJournalTester{journal}.checkTxnOrdering(ops[1].second, ops[3].second), CJournalTester::TxnOrder::BEFORE);
    BOOST_CHECK_EQUAL(CJournalTester{journal}.checkTxnOrdering(ops2[3].second, ops[1].second), CJournalTester::TxnOrder::BEFORE);
}

BOOST_AUTO_TEST_CASE(TestJournalCheckToposort)
{
    // Create builder to manage journals
    CJournalBuilderPtr builder { std::make_unique<CJournalBuilder>() };
    BOOST_CHECK(builder);

    // Journal is empty to start with
    CJournalPtr journal { builder->getCurrentJournal() };
    BOOST_CHECK_EQUAL(journal->size(), 0U);

    // zero transactions
    {
        auto changeSet = reorg(builder.get(), {});
        BOOST_CHECK(changeSet->CheckTopoSort());
    }

    // one transaction
    {
        auto txn1 = NewTxn();

        auto changeSet = reorg(builder.get(), {add(txn1)});
        BOOST_CHECK(changeSet->CheckTopoSort());
    }
    {
        auto txn1 = NewTxn();

        auto changeSet = reorg(builder.get(), {remove(txn1)});
        BOOST_CHECK(changeSet->CheckTopoSort());
    }

    // two transactions
    {
        auto txn1 = NewTxn();
        auto txn2 = NewTxn({txn1.getTxn()});

        auto changeSet = reorg(builder.get(), {add(txn1), add(txn2)});
        BOOST_CHECK(changeSet->CheckTopoSort());
    }
    {
        auto txn1 = NewTxn();
        auto txn2 = NewTxn({txn1.getTxn()});

        auto changeSet = reorg(builder.get(), {add(txn2), add(txn1)});
        BOOST_CHECK(!changeSet->CheckTopoSort());
    }
    {
        auto txn1 = NewTxn();
        auto txn2 = NewTxn({txn1.getTxn()});

        auto changeSet = reorg(builder.get(), {add(txn1), add(txn2), remove(txn2)});
        BOOST_CHECK(changeSet->CheckTopoSort());
    }
    {
        auto txn1 = NewTxn();
        auto txn2 = NewTxn({txn1.getTxn()});

        auto changeSet = reorg(builder.get(), {add(txn1), add(txn2), remove(txn2)});
        BOOST_CHECK(changeSet->CheckTopoSort());
    }

    // three transactions as a chain
    {
        auto txn1 = NewTxn();
        auto txn2 = NewTxn({txn1.getTxn()});
        auto txn3 = NewTxn({txn2.getTxn()});

        auto changeSet = reorg(builder.get(), {add(txn1), add(txn2), add(txn3)});
        BOOST_CHECK(changeSet->CheckTopoSort());
    }
    {
        auto txn1 = NewTxn();
        auto txn2 = NewTxn({txn1.getTxn()});
        auto txn3 = NewTxn({txn2.getTxn()});

        auto changeSet = reorg(builder.get(), {add(txn1), add(txn3), add(txn2)});
        BOOST_CHECK(!changeSet->CheckTopoSort());
    }
    {
        auto txn1 = NewTxn();
        auto txn2 = NewTxn({txn1.getTxn()});
        auto txn3 = NewTxn({txn2.getTxn()});

        auto changeSet = reorg(builder.get(), {add(txn2), add(txn1), add(txn3)});
        BOOST_CHECK(!changeSet->CheckTopoSort());
    }
    {
        auto txn1 = NewTxn();
        auto txn2 = NewTxn({txn1.getTxn()});
        auto txn3 = NewTxn({txn2.getTxn()});

        auto changeSet = reorg(builder.get(), {add(txn2), add(txn3), add(txn1)});
        BOOST_CHECK(!changeSet->CheckTopoSort());
    }
    {
        auto txn1 = NewTxn();
        auto txn2 = NewTxn({txn1.getTxn()});
        auto txn3 = NewTxn({txn2.getTxn()});

        auto changeSet = reorg(builder.get(), {add(txn3), add(txn1), add(txn2)});
        BOOST_CHECK(!changeSet->CheckTopoSort());
    }
    {
        auto txn1 = NewTxn();
        auto txn2 = NewTxn({txn1.getTxn()});
        auto txn3 = NewTxn({txn2.getTxn()});

        auto changeSet = reorg(builder.get(), {add(txn3), add(txn2), add(txn1)});
        BOOST_CHECK(!changeSet->CheckTopoSort());
    }

    // three transactions as a tree
    {
        auto txn1 = NewTxn();
        auto txn2 = NewTxn();
        auto txn3 = NewTxn({txn1.getTxn(), txn2.getTxn()});

        auto changeSet = reorg(builder.get(), {add(txn1), add(txn2), add(txn3)});
        BOOST_CHECK(changeSet->CheckTopoSort());
    }
    {
        auto txn1 = NewTxn();
        auto txn2 = NewTxn();
        auto txn3 = NewTxn({txn1.getTxn(), txn2.getTxn()});

        auto changeSet = reorg(builder.get(), {add(txn1), add(txn3), add(txn2)});
        BOOST_CHECK(!changeSet->CheckTopoSort());
    }
    {
        auto txn1 = NewTxn();
        auto txn2 = NewTxn();
        auto txn3 = NewTxn({txn1.getTxn(), txn2.getTxn()});

        auto changeSet = reorg(builder.get(), {add(txn2), add(txn1), add(txn3)});
        BOOST_CHECK(changeSet->CheckTopoSort());
    }
    {
        auto txn1 = NewTxn();
        auto txn2 = NewTxn();
        auto txn3 = NewTxn({txn1.getTxn(), txn2.getTxn()});

        auto changeSet = reorg(builder.get(), {add(txn2), add(txn3), add(txn1)});
        BOOST_CHECK(!changeSet->CheckTopoSort());
    }
    {
        auto txn1 = NewTxn();
        auto txn2 = NewTxn();
        auto txn3 = NewTxn({txn1.getTxn(), txn2.getTxn()});

        auto changeSet = reorg(builder.get(), {add(txn3), add(txn1), add(txn2)});
        BOOST_CHECK(!changeSet->CheckTopoSort());
    }
    {
        auto txn1 = NewTxn();
        auto txn2 = NewTxn();
        auto txn3 = NewTxn({txn1.getTxn(), txn2.getTxn()});

        auto changeSet = reorg(builder.get(), {add(txn3), add(txn2), add(txn1)});
        BOOST_CHECK(!changeSet->CheckTopoSort());
    }
}

BOOST_AUTO_TEST_SUITE_END()

