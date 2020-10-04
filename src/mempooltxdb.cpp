// Copyright (c) 2020 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#include "txmempool.h"
#include "mempoolTxDB.h"

#include <new>
#include <future>

CMempoolTxDB::CMempoolTxDB(size_t nCacheSize_, bool fMemory_, bool fWipe)
    : dbPath{GetDataDir() / "mempoolTxDB"},
      nCacheSize{nCacheSize_},
      fMemory{fMemory_},
      mempoolTxDB{std::make_unique<CDBWrapper>(dbPath, nCacheSize, fMemory, fWipe)}
{
    uint64_t storedValue;
    if (mempoolTxDB->Read(DB_DISK_USAGE, storedValue))
    {
        diskUsage.store(storedValue);
    }
    if (mempoolTxDB->Read(DB_TX_COUNT, storedValue))
    {
        txCount.store(storedValue);
    }
}

void CMempoolTxDB::ClearDatabase()
{
    diskUsage.store(0);
    txCount.store(0);
    dbWriteCount.store(0);
    mempoolTxDB.reset(); // make sure old db is closed before reopening
    mempoolTxDB = std::make_unique<CDBWrapper>(dbPath, nCacheSize, fMemory, true);
}

bool CMempoolTxDB::AddTransactions(const std::vector<CTransactionRef> &txs)
{
    uint64_t diskUsageAdded = 0;
    for (const auto& tx : txs)
    {
        diskUsageAdded += tx->GetTotalSize();
    }
    const uint64_t txCountAdded = txs.size();
    const auto prevDiskUsage = diskUsage.fetch_add(diskUsageAdded);
    const auto prevTxCount = txCount.fetch_add(txCountAdded);

    CDBBatch batch{*mempoolTxDB};
    for (const auto& tx : txs)
    {
        batch.Write(std::make_pair(DB_TRANSACTIONS, tx->GetId()), tx);
    }
    batch.Write(DB_DISK_USAGE, prevDiskUsage + diskUsageAdded);
    batch.Write(DB_TX_COUNT, prevTxCount + txCountAdded);
    batch.Erase(DB_MEMPOOL_XREF);

    ++dbWriteCount;
    if (!mempoolTxDB->WriteBatch(batch, true))
    {
        diskUsage.fetch_sub(diskUsageAdded);
        txCount.fetch_sub(txCountAdded);
        return false;
    }
    return true;
}

bool CMempoolTxDB::GetTransaction(const uint256 &txid, CTransactionRef &tx)
{
    const auto key = std::make_pair(DB_TRANSACTIONS, txid);
    if (mempoolTxDB->Exists(key))
    {
        CMutableTransaction txm;
        if (mempoolTxDB->Read(key, txm))
        {
            tx = MakeTransactionRef(std::move(txm));
            return true;
        }
    }
    return false;
}

bool CMempoolTxDB::TransactionExists(const uint256 &txid)
{
    return mempoolTxDB->Exists(std::make_pair(DB_TRANSACTIONS, txid));
}


bool CMempoolTxDB::RemoveTransactions(const std::vector<TxData>& transactionsToRemove)
{
    const uint64_t txCountRemoved = transactionsToRemove.size();
    const uint64_t diskUsageRemoved = [&transactionsToRemove]() {
        auto accumulator = decltype(transactionsToRemove[0].size)(0);
        for (const auto& td : transactionsToRemove) {
            accumulator += td.size;
        }
        return accumulator;
    }();
    const auto prevDiskUsage = diskUsage.fetch_sub(diskUsageRemoved);
    const auto prevTxCount = txCount.fetch_sub(txCountRemoved);

    CDBBatch batch{*mempoolTxDB};
    for (const auto& td : transactionsToRemove) {
        batch.Erase(std::make_pair(DB_TRANSACTIONS, td.txid));
    }
    batch.Write(DB_DISK_USAGE, prevDiskUsage - diskUsageRemoved);
    batch.Write(DB_TX_COUNT, prevTxCount - txCountRemoved);
    batch.Erase(DB_MEMPOOL_XREF);

    ++dbWriteCount;
    if (!mempoolTxDB->WriteBatch(batch, true))
    {
        diskUsage.fetch_add(diskUsageRemoved);
        txCount.fetch_add(txCountRemoved);
        return false;
    }
    return true;
}

uint64_t CMempoolTxDB::GetDiskUsage()
{
    return diskUsage.load();
}

uint64_t CMempoolTxDB::GetTxCount()
{
    return txCount.load();
}

CMempoolTxDB::TxIdSet CMempoolTxDB::GetKeys()
{
    static const auto initialKey = std::make_pair(DB_TRANSACTIONS, uint256());

    std::unique_ptr<CDBIterator> iter {mempoolTxDB->NewIterator()};
    iter->Seek(initialKey);

    TxIdSet result;
    for (; iter->Valid(); iter->Next())
    {
        auto key {decltype(initialKey){}};
        iter->GetKey(key);
        result.emplace(key.second);
    }
    return result;
}

bool CMempoolTxDB::SetXrefKey(const XrefKey& xrefKey)
{
    CDBBatch batch{*mempoolTxDB};
    batch.Write(DB_MEMPOOL_XREF, xrefKey);
    ++dbWriteCount;
    return mempoolTxDB->WriteBatch(batch, true);
}

bool CMempoolTxDB::GetXrefKey(XrefKey& xrefKey)
{
    if (mempoolTxDB->Exists(DB_MEMPOOL_XREF))
    {
        if (mempoolTxDB->Read(DB_MEMPOOL_XREF, xrefKey))
        {
            return true;
        }
    }
    return false;
}

bool CMempoolTxDB::RemoveXrefKey()
{
    CDBBatch batch{*mempoolTxDB};
    batch.Erase(DB_MEMPOOL_XREF);
    ++dbWriteCount;
    return mempoolTxDB->WriteBatch(batch, true);
}

// The coalescing batch operations assume that the transaction database is
// consistent with the requested operation: that is, a transaction that is to be
// added is not already in the database and a transaction to be removed is in
// the database. Hence, an "add" combined with a "remove" becomes a no-op, and
// vice versa. The corollary is that adds and removes are properly serialized at
// the caller, specifically, you can't have two threads independently adding and
// removing the same transaction. There is some protection against double-add
// and double-remove (see the assertions in Batch::Add() and Batch::Remove()),
// but ideally such double operations should never happen.

void CMempoolTxDB::Batch::Add(const CTransactionRef& tx, const Updater& update)
{
    const auto& txid = tx->GetId();
    if (0 == removes.erase(txid))
    {
        const auto [iter, inserted] = adds.try_emplace(txid, AddOp{tx, update});
        assert(inserted || iter->second.tx->GetTotalSize() == tx->GetTotalSize());
    }
}

void CMempoolTxDB::Batch::Remove(const TxId& txid, uint64_t size, const Updater& update)
{
    if (0 == adds.erase(txid))
    {
        const auto [iter, inserted] = removes.try_emplace(txid, RmOp{size, update});
        assert(inserted || iter->second.size == size);
    }
}

bool CMempoolTxDB::Commit(const Batch& batch)
{
    if (batch.adds.size() == 0 && batch.removes.size() == 0)
    {
        return true;
    }

    const uint64_t txCountDiff = batch.adds.size() - batch.removes.size();
    const uint64_t diskUsageDiff = [&batch]() {
        uint64_t accumulator = 0;
        for (const auto& e : batch.adds)
        {
            accumulator += e.second.tx->GetTotalSize();
        }
        for (const auto& e : batch.removes)
        {
            accumulator -= e.second.size;
        }
        return accumulator;
    }();

    const auto prevDiskUsage = diskUsage.fetch_add(diskUsageDiff);
    const auto prevTxCount = txCount.fetch_add(txCountDiff);

    CDBBatch coalesced {*mempoolTxDB};
    for (const auto& e : batch.adds)
    {
        coalesced.Write(std::make_pair(DB_TRANSACTIONS, e.first), e.second.tx);
    }
    for (const auto& e : batch.removes)
    {
        coalesced.Erase(std::make_pair(DB_TRANSACTIONS, e.first));
    }
    coalesced.Write(DB_DISK_USAGE, prevDiskUsage + diskUsageDiff);
    coalesced.Write(DB_TX_COUNT, prevTxCount + txCountDiff);
    coalesced.Erase(DB_MEMPOOL_XREF);

    ++dbWriteCount;
    if (!mempoolTxDB->WriteBatch(coalesced, true))
    {
        diskUsage.fetch_sub(diskUsageDiff);
        txCount.fetch_sub(txCountDiff);
        return false;
    }

    for (const auto& e : batch.adds)
    {
        if (e.second.update)
        {
            e.second.update(e.first);
        }
    }
    for (const auto& e : batch.removes)
    {
        if (e.second.update)
        {
            e.second.update(e.first);
        }
    }
    return true;
}

uint64_t CMempoolTxDB::GetWriteCount()
{
    return dbWriteCount.load();
}


void CAsyncMempoolTxDB::EnqueueNL(std::initializer_list<Task>&& tasks, bool clearList)
{
    if (clearList)
    {
        taskList.clear();
    }
    for (auto&& task : tasks)
    {
        taskList.emplace(taskList.begin(), std::move(task));
    }
}

void CAsyncMempoolTxDB::Enqueue(std::initializer_list<Task>&& tasks, bool clearList)
{
    std::unique_lock<std::mutex> taskLock{taskListGuard};
    EnqueueNL(std::move(tasks), clearList);
    taskListSignal.notify_all();
}

void CAsyncMempoolTxDB::Synchronize(std::initializer_list<Task>&& tasks, bool clearList)
{
    std::promise<void> sync;
    {
        std::unique_lock<std::mutex> taskLock{taskListGuard};
        EnqueueNL(std::move(tasks), clearList);
        EnqueueNL({SyncTask{&sync}}, false);
        taskListSignal.notify_all();
    }
    sync.get_future().get();
}

CAsyncMempoolTxDB::CAsyncMempoolTxDB(size_t nCacheSize)
    : txdb{std::make_shared<CMempoolTxDB>(nCacheSize)},
      worker{[this](){ Work(); }}
{}

CAsyncMempoolTxDB::~CAsyncMempoolTxDB()
{
    Enqueue({StopTask{}}, true);
    worker.join();
}

void CAsyncMempoolTxDB::Sync()
{
    Synchronize({});
}

void CAsyncMempoolTxDB::Clear()
{
    Synchronize({ClearTask{}}, true);
}

void CAsyncMempoolTxDB::Add(std::vector<CTransactionWrapperRef>&& transactionsToAdd)
{
    Enqueue({AddTask{std::move(transactionsToAdd)}});
}

void CAsyncMempoolTxDB::Remove(std::vector<CMempoolTxDB::TxData>&& transactionsToRemove)
{
    Enqueue({RemoveTask{std::move(transactionsToRemove)}});
}

std::shared_ptr<CMempoolTxDBReader> CAsyncMempoolTxDB::GetDatabase()
{
    return txdb;
}

CMempoolTxDB::TxIdSet CAsyncMempoolTxDB::GetTxKeys()
{
    return txdb->GetKeys();
}

bool CAsyncMempoolTxDB::SetXrefKey(const CMempoolTxDB::XrefKey& xrefKey)
{
    bool result = false;
    const auto function = [&xrefKey, &result](CMempoolTxDB& txdb)
    {
        result = txdb.SetXrefKey(xrefKey);
    };

    Synchronize({InvokeTask{function}});
    return result;
}

bool CAsyncMempoolTxDB::GetXrefKey(CMempoolTxDB::XrefKey& xrefKey)
{
    bool result = false;
    const auto function = [&xrefKey, &result](CMempoolTxDB& txdb)
    {
        result = txdb.GetXrefKey(xrefKey);
    };

    Synchronize({InvokeTask{function}});
    return result;
}

bool CAsyncMempoolTxDB::RemoveXrefKey()
{
    bool result = false;
    const auto function = [&result](CMempoolTxDB& txdb)
    {
        result = txdb.RemoveXrefKey();
    };

    Synchronize({InvokeTask{function}});
    return result;
}


// Overload dispatcher for std::visit.
// See the example at https://en.cppreference.com/w/cpp/utility/variant/visit
namespace {
    template<class... T> struct dispatch : T... { using T::operator()...; };
    template<class... T> dispatch(T...) -> dispatch<T...>;
}

void CAsyncMempoolTxDB::Work()
{
    // Stop the background thread.
    bool running = true;
    const auto stop = [&running](const StopTask&)
    {
        running = false;
    };

    // Commit the adds and removes to the database.
    CMempoolTxDB::Batch batch;
    const auto commit = [this, &batch]()
    {
        if (!txdb->Commit(batch))
        {
            LogPrint(BCLog::MEMPOOL, "Mempool TxDB Batch Commit failed.");
        }
        batch.Clear();
    };

    // Synchronize with the caller.
    const auto sync = [&commit](const SyncTask& task)
    {
        commit();
        task.sync->set_value();
    };

    // Clear the transaction database.
    const auto clear = [this, &batch](const ClearTask&)
    {
        batch.Clear();
        txdb->ClearDatabase();
    };

    // Add transactions to the database and update the wrappers.
    const auto add = [&batch](const AddTask& task)
    {
        for (const auto& wrapper : task.transactions)
        {
            if (wrapper->IsInMemory())
            {
                batch.Add(wrapper->GetTx(),
                          [wrapper](const TxId&) {
                              wrapper->UpdateTxMovedToDisk();
                          });
            }
        }
    };

    // Remove transactions from the database.
    const auto remove = [&batch](const RemoveTask& task)
    {
        for (const auto& txdata : task.transactions)
        {
            batch.Remove(txdata.txid, txdata.size);
        }
    };

    // Invoke a function on the database.
    const auto invoke = [this, &commit](const InvokeTask& task)
    {
        commit();
        task.function(*txdb);
    };

    const auto dispatcher {dispatch{stop, sync, clear, add, remove, invoke}};
    while (running)
    {
        std::deque<Task> tasks;
        {
            std::unique_lock<std::mutex> taskLock(taskListGuard);
            while (taskList.size() == 0)
            {
                taskListSignal.wait(taskLock);
            }

            tasks = std::move(taskList);
        }

        while (running && !tasks.empty())
        {
            std::visit(dispatcher, tasks.back());
            tasks.pop_back();
        }
        commit();
    }
}
