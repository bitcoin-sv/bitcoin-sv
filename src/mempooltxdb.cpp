// Copyright (c) 2020 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#include "config.h"
#include "logging.h"
#include "txmempool.h"
#include "mempooltxdb.h"
#include "util.h"
#include "thread_safe_queue.h"
#include "consensus/consensus.h"

#include <future>
#include <limits>
#include <variant>

CMempoolTxDB::CMempoolTxDB(const fs::path& dbPath_, size_t nCacheSize_, bool fMemory_, bool fWipe)
    : dbPath{dbPath_},
      nCacheSize{nCacheSize_},
      fMemory{fMemory_},
      wrapper{std::make_unique<CDBWrapper>(dbPath, nCacheSize, fMemory, fWipe)}
{
    uint64_t storedValue;
    if (wrapper->Read(DB_DISK_USAGE, storedValue))
    {
        diskUsage.store(storedValue);
    }
    if (wrapper->Read(DB_TX_COUNT, storedValue))
    {
        txCount.store(storedValue);
    }
}

void CMempoolTxDB::ClearDatabase()
{
    diskUsage.store(0);
    txCount.store(0);
    dbWriteCount.store(0);
    wrapper.reset();   // Release the old environment before creating a new one.
    wrapper = std::make_unique<CDBWrapper>(dbPath, nCacheSize, fMemory, true);
}

bool CMempoolTxDB::AddTransactions(const std::vector<CTransactionRef>& txs)
{
    const uint64_t txCountAdded = txs.size();
    const uint64_t diskUsageAdded = [&txs]() {
        auto accumulator = decltype(txs[0]->GetTotalSize()){0};
        for (const auto& tx : txs)
        {
            accumulator += tx->GetTotalSize();
        }
        return accumulator;
    }();

    auto batch = CDBBatch{*wrapper};
    for (const auto& tx : txs)
    {
        batch.Write(std::make_pair(DB_TRANSACTIONS, tx->GetId()), tx);
    }
    batch.Write(DB_DISK_USAGE, diskUsage.load() + diskUsageAdded);
    batch.Write(DB_TX_COUNT, txCount.load() + txCountAdded);
    batch.Erase(DB_MEMPOOL_XREF);

    ++dbWriteCount;
    if (wrapper->WriteBatch(batch, true))
    {
        diskUsage.fetch_add(diskUsageAdded);
        txCount.fetch_add(txCountAdded);
        return true;
    }
    return false;
}

bool CMempoolTxDB::GetTransaction(const uint256 &txid, CTransactionRef &tx)
{
    const auto key = std::make_pair(DB_TRANSACTIONS, txid);
    if (wrapper->Exists(key))
    {
        CMutableTransaction txm;
        if (wrapper->Read(key, txm))
        {
            tx = MakeTransactionRef(std::move(txm));
            return true;
        }
    }
    return false;
}

bool CMempoolTxDB::TransactionExists(const uint256 &txid)
{
    return wrapper->Exists(std::make_pair(DB_TRANSACTIONS, txid));
}


bool CMempoolTxDB::RemoveTransactions(const std::vector<TxData>& txData)
{
    const uint64_t txCountRemoved = txData.size();
    const uint64_t diskUsageRemoved = [&txData]() {
        auto accumulator = decltype(txData[0].size){0};
        for (const auto& td : txData) {
            accumulator += td.size;
        }
        return accumulator;
    }();

    auto batch = CDBBatch{*wrapper};
    for (const auto& td : txData) {
        batch.Erase(std::make_pair(DB_TRANSACTIONS, td.txid));
    }
    batch.Write(DB_DISK_USAGE, diskUsage.load() - diskUsageRemoved);
    batch.Write(DB_TX_COUNT, txCount.load() - txCountRemoved);
    batch.Erase(DB_MEMPOOL_XREF);

    ++dbWriteCount;
    if (wrapper->WriteBatch(batch, true))
    {
        diskUsage.fetch_sub(diskUsageRemoved);
        txCount.fetch_sub(txCountRemoved);
        return true;
    }
    return false;
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
    static const auto initialKey = std::make_pair(DB_TRANSACTIONS, uint256{});

    std::unique_ptr<CDBIterator> iter {wrapper->NewIterator()};
    iter->Seek(initialKey);

    TxIdSet result;
    for (; iter->Valid(); iter->Next())
    {
        auto key = decltype(initialKey){};
        iter->GetKey(key);
        result.emplace(key.second);
    }
    return result;
}

bool CMempoolTxDB::SetXrefKey(const XrefKey& xrefKey)
{
    auto batch = CDBBatch{*wrapper};
    batch.Write(DB_MEMPOOL_XREF, xrefKey);
    ++dbWriteCount;
    return wrapper->WriteBatch(batch, true);
}

bool CMempoolTxDB::GetXrefKey(XrefKey& xrefKey)
{
    if (wrapper->Exists(DB_MEMPOOL_XREF))
    {
        if (wrapper->Read(DB_MEMPOOL_XREF, xrefKey))
        {
            return true;
        }
    }
    return false;
}

bool CMempoolTxDB::RemoveXrefKey()
{
    auto batch = CDBBatch{*wrapper};
    batch.Erase(DB_MEMPOOL_XREF);
    ++dbWriteCount;
    return wrapper->WriteBatch(batch, true);
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

    auto coalesced = CDBBatch{*wrapper};
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
    if (!wrapper->WriteBatch(coalesced, true))
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


// Task queue managment for CAsyncMempoolTxDB
namespace {
    struct SyncTask
    {
        std::promise<void>* sync;
    };

    struct ClearTask {};

    struct InvokeTask
    {
        std::function<void(CMempoolTxDB&)> function;
    };

    struct AddTask
    {
        CTransactionWrapperRef transaction;
    };

    struct RemoveTask
    {
        CMempoolTxDB::TxData transaction;
    };

    using Task = std::variant<ClearTask, SyncTask, InvokeTask, AddTask, RemoveTask>;

    // Estimate the maximum size of the task queue based on the
    // ancestor limit parameters.
    size_t EstimateTaskQueueSize(const Config& config)
    {
        // The size of a single transaction data element.
        static constexpr size_t dataSize {sizeof(Task)};

        // Additional factor to account for:
        //   - vector capacity being larger than the number of elements;
        //   - more space in the queue for better parallelization.
        static constexpr size_t sizeFactor {53}; // A nice round prime number

        // Use the larger of ancestor limits to estimate the maximum
        // number of transactions in an add or remove task.
        const auto maxTxCount = std::max(config.GetLimitAncestorCount(),
                                         config.GetLimitSecondaryMempoolAncestorCount());
        assert(maxTxCount < std::numeric_limits<size_t>::max() / dataSize);

        // Finally, calculate the queue size, checking for overflow.
        const auto maxTaskSize = static_cast<size_t>(maxTxCount * dataSize);
        assert(maxTaskSize < std::numeric_limits<size_t>::max() / sizeFactor);
        return sizeFactor * maxTaskSize;
    }
} // anonymous namespace

class CAsyncMempoolTxDB::TaskQueue : CThreadSafeQueue<Task>
{
    // Calculate the actual size of a given task.
    static size_t CalculateTaskSize(const Task& task) noexcept
    {
        return sizeof(task);
    }

public:
    explicit TaskQueue(size_t maxSize)
        : CThreadSafeQueue<Task>{maxSize, CalculateTaskSize}
    {
        SetOnPushBlockedNotifier(
            [](const char* method, size_t requiredSize, size_t availableSize)
            {
                LogPrint(BCLog::MEMPOOL,
                         "Mempool TxDB work queue producer blocked"
                         " (%s needs %zu space but has %zu available).\n",
                         method, requiredSize, availableSize);
            }
        );
    }

    using CThreadSafeQueue<Task>::Close;
    using CThreadSafeQueue<Task>::MaximalSize;
    using CThreadSafeQueue<Task>::PushWait;
    using CThreadSafeQueue<Task>::PopAllWait;

    // Atomically push a set of tasks to the task queue and wait until the tasks
    // have been processed.
    void Synchronize(std::initializer_list<Task>&& tasks = {}, bool clearList = false)
    {
        std::promise<void> sync;
        bool success;
        if (!clearList && tasks.size() == 0)
        {
            success = PushWait(Task{SyncTask{&sync}});
        }
        else
        {
            std::vector<Task> temp{std::move(tasks)};
            temp.emplace_back(SyncTask{&sync});
            success = (clearList ? ReplaceContent(std::move(temp))
                                 : PushManyWait(std::move(temp)));
        }
        assert(success && "Push to task queue failed");
        sync.get_future().get();
    }
};

CAsyncMempoolTxDB::CAsyncMempoolTxDB(const fs::path& dbPath, size_t cacheSize, bool inMemory)
    : queue{new TaskQueue{EstimateTaskQueueSize(GlobalConfig::GetConfig())}},
      txdb{std::make_shared<CMempoolTxDB>(dbPath, cacheSize, inMemory)},
      worker{[this](){ Work(); }}
{
    const auto maxSize = queue->MaximalSize();
    if (maxSize > 5 * ONE_MEBIBYTE)
    {
        LogPrint(BCLog::MEMPOOL,
                 "Using %.1f MiB for the mempool transaction database work queue\n",
                 maxSize * (1.0 / ONE_MEBIBYTE));
    }
    else
    {
        LogPrint(BCLog::MEMPOOL,
                 "Using %.0f KiB for the mempool transaction database work queue\n",
                 maxSize * (1.0 / ONE_KIBIBYTE));
    }
}

CAsyncMempoolTxDB::~CAsyncMempoolTxDB()
{
    queue->Close(true);
    worker.join();
}

void CAsyncMempoolTxDB::Sync()
{
    queue->Synchronize();
}

void CAsyncMempoolTxDB::Clear()
{
    queue->Synchronize({ClearTask{}}, true);
}

void CAsyncMempoolTxDB::Add(CTransactionWrapperRef&& transactionToAdd)
{
    const auto success = queue->PushWait(Task{AddTask{std::move(transactionToAdd)}});
    assert(success && "Push to task queue failed");
}

void CAsyncMempoolTxDB::Remove(CMempoolTxDB::TxData&& transactionToRemove)
{
    const auto success = queue->PushWait(Task{RemoveTask{std::move(transactionToRemove)}});
    assert(success && "Push to task queue failed");
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

    queue->Synchronize({InvokeTask{function}});
    return result;
}

bool CAsyncMempoolTxDB::GetXrefKey(CMempoolTxDB::XrefKey& xrefKey)
{
    bool result = false;
    const auto function = [&xrefKey, &result](CMempoolTxDB& txdb)
    {
        result = txdb.GetXrefKey(xrefKey);
    };

    queue->Synchronize({InvokeTask{function}});
    return result;
}

bool CAsyncMempoolTxDB::RemoveXrefKey()
{
    bool result = false;
    const auto function = [&result](CMempoolTxDB& txdb)
    {
        result = txdb.RemoveXrefKey();
    };

    queue->Synchronize({InvokeTask{function}});
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
    const auto name = strprintf("mempooldb-%x", uintptr_t(static_cast<void*>(this)));
    RenameThread(name.c_str());
    LogPrint(BCLog::MEMPOOL, "Entering mempool TxDB worker thread.\n");

    // Commit the adds and removes to the database.
    CMempoolTxDB::Batch batch;
    const auto commit = [this, &batch]()
    {
        if (!txdb->Commit(batch))
        {
            LogPrint(BCLog::MEMPOOL, "Mempool TxDB batch commit failed.\n");
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

    // Invoke a function on the database.
    const auto invoke = [this, &commit](const InvokeTask& task)
    {
        commit();
        task.function(*txdb);
    };

    // Add transactions to the database and update the wrappers.
    //
    // Due to the way CTxMemPool::SaveTxsToDisk() works, there may be multiple
    // adds for the same transaction. These are resolved as follows:
    //
    //  * If the second instance arrives after the current batch has been
    //    committed to disk:
    //     - ResetTransaction() will already have been called;
    //     - GetInMemoryTx() will return a null pointer;
    //     - consequently, this second instance will be ignored.
    //  * If the second instance arrives while the current batch is still
    //    being constructed:
    //     - if the first add was *not* coalesced with (removed due to) a
    //       subsequent remove (see: CMempoolTxDB::Batch::Remove), the second
    //       add will be ignored due to the first instance still living in the
    //       coalescing batch (see: CMempoolTxDB::Batch::Add).
    //     - if the first add *was* removed due to a subsequent remove, the
    //       current add prevails. However, this can only happen if the
    //       transaction was re-added to the mempool after it had already been
    //       removed from it.
    const auto add = [&batch](const AddTask& task)
    {
        const auto& wrapper = task.transaction;
        if (const auto tx = wrapper->GetInMemoryTx())
        {
            batch.Add(tx,
                      [wrapper](const TxId&) {
                          wrapper->ResetTransaction();
                      });
        }
    };

    // Remove transactions from the database.
    const auto remove = [&batch](const RemoveTask& task)
    {
        batch.Remove(task.transaction.txid, task.transaction.size);
    };

    const auto dispatcher {dispatch{sync, clear, invoke, add, remove}};
    for (;;)
    {
        try
        {
            const auto tasks {queue->PopAllWait()};
            if (!tasks.has_value())
            {
                break;
            }

            for (const auto& task : tasks.value())
            {
                std::visit(dispatcher, task);
            }
            commit();
        }
        catch (...)
        {
            // There's really nothing we can do here to recover except terminate
            // the thread and close the queue so that producers will also fail.
            LogPrint(BCLog::MEMPOOL, "Unexpected exception in mempool TxDB worker thread.\n");
            queue->Close();
            break;
        }
    }

    LogPrint(BCLog::MEMPOOL, "Exiting mempool TxDB worker thread.\n");
}
