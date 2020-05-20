#include "mining/journal_change_set.h"
#include "mempooltxdb.h"
#include "test/test_bitcoin.h"
#include "key.h"
#include "script/script_num.h"
#include "validation.h"
#include <boost/multi_index_container.hpp>

#include <boost/test/unit_test.hpp>

namespace {
mining::CJournalChangeSetPtr nullChangeSet{nullptr};
}

BOOST_FIXTURE_TEST_SUITE(mempooltxdb_tests, BasicTestingSetup)
BOOST_AUTO_TEST_CASE(save_on_full_mempool)
{
    TestMemPoolEntryHelper entry;
    // Parent transaction with three children, and three grand-children:
    CMutableTransaction txParent;
    txParent.vin.resize(1);
    txParent.vin[0].scriptSig = CScript() << OP_11;
    txParent.vout.resize(3);
    for (int i = 0; i < 3; i++) {
        txParent.vout[i].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
        txParent.vout[i].nValue = Amount(33000LL);
    }
    CMutableTransaction txChild[3];
    for (int i = 0; i < 3; i++) {
        txChild[i].vin.resize(1);
        txChild[i].vin[0].scriptSig = CScript() << OP_11;
        txChild[i].vin[0].prevout = COutPoint(txParent.GetId(), i);
        txChild[i].vout.resize(1);
        txChild[i].vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
        txChild[i].vout[0].nValue = Amount(11000LL);
    }
    CMutableTransaction txGrandChild[3];
    for (int i = 0; i < 3; i++) {
        txGrandChild[i].vin.resize(1);
        txGrandChild[i].vin[0].scriptSig = CScript() << OP_11;
        txGrandChild[i].vin[0].prevout = COutPoint(txChild[i].GetId(), 0);
        txGrandChild[i].vout.resize(1);
        txGrandChild[i].vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
        txGrandChild[i].vout[0].nValue = Amount(11000LL);
    }

    // Nothing in pool, remove should do nothing:
    unsigned int poolSize = mempool.Size();
    mempool.SaveTxsToDisk(10000);
    BOOST_CHECK_EQUAL(mempool.Size(), poolSize);
    uint64_t diskUsage;
    uint64_t sizeTXsAdded = 0;
    if (!mempool.GetMempoolTxDB()->GetDiskUsage(diskUsage)) 
    {
        diskUsage = 0;
    }
    else
    {
        sizeTXsAdded = diskUsage;
    }

    // Add transactions:
    mempool.AddUnchecked(txParent.GetId(), entry.FromTx(txParent), nullChangeSet);
    for (int i = 0; i < 3; i++) {
        mempool.AddUnchecked(txChild[i].GetId(), entry.FromTx(txChild[i]), nullChangeSet);
        mempool.AddUnchecked(txGrandChild[i].GetId(), entry.FromTx(txGrandChild[i]), nullChangeSet);
    }

    poolSize = mempool.Size();
    mempool.SaveTxsToDisk(10000);
    std::cout << poolSize << std::endl;
    std::cout << mempool.Size() << std::endl;
    auto mi = mempool.mapTx.get<entry_time>().begin();
    while (mi != mempool.mapTx.get<entry_time>().end())
    {
        BOOST_CHECK(!mi->IsInMemory());
        sizeTXsAdded += mi->GetTxSize();
        mi++;
    }
    mempool.GetMempoolTxDB()->GetDiskUsage(diskUsage);
    BOOST_CHECK_EQUAL(diskUsage, sizeTXsAdded);
}
BOOST_AUTO_TEST_SUITE_END()