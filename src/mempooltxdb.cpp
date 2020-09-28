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

bool CMempoolTxDB::RemoveTransactions(const std::vector<TxId> &txidsRemoved,
                                      uint64_t diskUsageRemoved)
{
    const uint64_t txCountRemoved = txidsRemoved.size();
    const auto prevDiskUsage = diskUsage.fetch_sub(diskUsageRemoved);
    const auto prevTxCount = txCount.fetch_sub(txCountRemoved);

    CDBBatch batch{*mempoolTxDB};
    for (const uint256 &txid : txidsRemoved) {
        batch.Erase(std::make_pair(DB_TRANSACTIONS,txid));
    }
    batch.Write(DB_DISK_USAGE, prevDiskUsage - diskUsageRemoved);
    batch.Write(DB_TX_COUNT, prevTxCount - txCountRemoved);
    batch.Erase(DB_MEMPOOL_XREF);

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
    return mempoolTxDB->WriteBatch(batch, true);
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

void CAsyncMempoolTxDB::Remove(std::vector<TxId>&& transactionsToRemove,
                               std::uint64_t diskUsageRemoved)
{
    Enqueue({RemoveTask{std::move(transactionsToRemove), diskUsageRemoved}});
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
    bool running = true;

    // Stop the background thread.
    const auto stop = [&running](const StopTask&)
    {
        running = false;
    };

    // Synchronize with the caller.
    const auto sync = [](const SyncTask& task)
    {
        task.sync->set_value();
    };

    // Clear the transaction database.
    const auto clear = [this](const ClearTask&)
    {
        txdb->ClearDatabase();
    };

    // Add transactions to the database and update the wrappers.
    const auto add = [this](const AddTask& task)
    {
        std::vector<CTransactionRef> transactions;
        transactions.reserve(task.transactions.size());
        for (const auto& wrapper : task.transactions)
        {
            if (wrapper->IsInMemory())
            {
                transactions.emplace_back(wrapper->GetTx());
            }
        }

        if (txdb->AddTransactions(transactions))
        {
            for (const auto& wrapper : task.transactions)
            {
                wrapper->UpdateTxMovedToDisk();
            }
        }
        else
        {
            LogPrint(BCLog::MEMPOOL, "WriteBatch failed. Transactions were not moved to DB successfully.");
        }
    };

    // Remove transactions from the database.
    const auto remove = [this](const RemoveTask& task)
    {
        if (!txdb->RemoveTransactions(task.transactions, task.size))
        {
            LogPrint(BCLog::MEMPOOL, "WriteBatch failed. Transactions were not removed from DB successfully.");
        }
    };

    // Invoke a function on the database.
    const auto invoke = [this](const InvokeTask& task)
    {
        task.function(*txdb);
    };

    while (running)
    {
        Task task;
        {
            std::unique_lock<std::mutex> taskLock(taskListGuard);
            while (taskList.size() == 0)
            {
                taskListSignal.wait(taskLock);
            }
            std::swap(task, taskList.back());
            taskList.pop_back();
        }
        std::visit(dispatch{stop, sync, clear, add, remove, invoke}, task);
    }
}
