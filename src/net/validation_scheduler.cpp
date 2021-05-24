// Copyright (c) 2021 The Bitcoin SV developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "validation_scheduler.h"

#include <util.h>
#include <thread>
#include <algorithm>

#include "logging.h"

//#define DEBUG_SCHEDULER

size_t ValidationScheduler::_batchNum = 0;

ValidationScheduler::ValidationScheduler(CThreadPool<CDualQueueAdaptor> &threadPool,
                                         TxInputDataSPtrVec &txs, TypeValidationFunc func)
        : validationFunc(std::move(func)),
          txs(txs),
          validatorThreadPool(threadPool),
          NUM_VALIDATORS{threadPool.getPoolSize()},
          MAX_TO_SCHEDULE{NUM_VALIDATORS * 8}
{
    // Initialize status for each transaction.
    txStatuses.reserve(txs.size());
    for (auto& tx : txs) {
        txStatuses[tx->GetTxnPtr()->GetId()] = ScheduleStatus::NOT_STARTED;
    }

    // Build map of spenders on a separate thread. Until map is ready we schedule without it.
    buildSpendersThread = std::thread(&ValidationScheduler::BuildSpendersMap, this);

    ++_batchNum;
}

ValidationScheduler::~ValidationScheduler() {
    // Note: In normal operation it should not happen that spenders graph is still being built when
    // all transactions were already scheduled and scheduler is exiting. Just in case stop the thread.
    buildSpendersThreadRun = false;
    buildSpendersThread.join();
}

std::vector<std::future<ValidationScheduler::TypeValidationResult>> ValidationScheduler::Schedule() {
#ifdef DEBUG_SCHEDULER
    const std::chrono::time_point tsStart = std::chrono::steady_clock::now();
    LogPrint(BCLog::TXNVAL, "================== scheduler loop started. batch:%d, batch size:%d ==================\n", _batchNum, txs.size());
    
    DrawGraph();
#endif

    // task results
    std::vector<std::future<TypeValidationResult>> taskResults;
    // Reserve a space for the result set (usually there is one task per transaction).
    taskResults.reserve(txs.size());

    // Keep running until all transactions are scheduled.
    while (posUnhandled < txs.size()) {
#ifdef DEBUG_SCHEDULER
        LogPrint(BCLog::TXNVAL, "== loop run ------------------------------------\n");
#endif
        // Process task_results input queue if there are any task_results waiting
        std::vector<TaskCompletion> lastResults;
        {
            std::scoped_lock<std::mutex> resultsLock(taskCompletionMtx);
            if (!taskCompletionQueue.empty()) {
                // move all task_results from input queue to local vector.
                lastResults = std::move(taskCompletionQueue);
                taskCompletionQueue = std::vector<TaskCompletion>();
            }
        }

        // Update txs statuses of finished transactions.
        for (const TaskCompletion &taskResult : lastResults) {
            --numTasksScheduled;
            for (auto pos : taskResult.positions) {
                txStatuses[txs[pos]->GetTxnPtr()->GetId()] = taskResult.status;
            }
        }

        // If graph of spenders is available then try to schedule children of txs that were just validated.
        if (IsSpendersGraphReady() && !spenders.empty()) {
            for (const TaskCompletion &taskResult : lastResults) {
                // If there are many txs in the task, then task is a chain. We only look at the last tx.
                // See also ScheduleChain.
                ScheduleGraph(*taskResult.positions.rbegin(), taskResults);
            }
        }

        // Scan unhandled transactions and try to schedule tasks up to max
        ScanTransactions(taskResults);

        if (posUnhandled < txs.size()) {
            // We are not done yet as we scheduled as much as possible in this iteration. 
            // There is nothing else to do until we get some task result back.
#if DEBUG_SCHEDULER
            const std::chrono::time_point tsWaitStart = std::chrono::steady_clock::now();
#endif
            std::unique_lock<std::mutex> lock(taskCompletionMtx);
            taskCompletionCV.wait_for(lock, std::chrono::milliseconds (100), [this]{return !taskCompletionQueue.empty();});
#if DEBUG_SCHEDULER
            const std::chrono::time_point tsWaitEnd = std::chrono::steady_clock::now();
            LogPrint(BCLog::TXNVAL, "== tasks running: %d, waiting for task results: %dus\n", 
                     numTasksScheduled,
                     std::chrono::duration_cast<std::chrono::microseconds>(tsWaitEnd-tsWaitStart).count());
#endif
        }
    }

#ifdef DEBUG_SCHEDULER
    const std::chrono::time_point tsEnd = std::chrono::steady_clock::now();
    LogPrint(BCLog::TXNVAL, "================== scheduler loop ended. batch:%d, num txs:%d, num tasks:%d, run time:%dus\n", _batchNum, txs.size(), taskResults.size(), std::chrono::duration_cast<std::chrono::microseconds>(tsEnd - tsStart).count());
#endif

    return taskResults;
}

void ValidationScheduler::ScanTransactions(std::vector<std::future<TypeValidationResult>>& taskResults) {
#ifdef DEBUG_SCHEDULER
    LogPrint(BCLog::TXNVAL, "==>> ScanTransactions\n");
    const std::chrono::time_point tsStart = std::chrono::steady_clock::now();
#endif

    if (scanPos >= txs.size()) {
        // Start new cycle.
        scanPos = posUnhandled;

        // Advance posUnhandled as far as possible.
        while (scanPos < txs.size() && txStatuses[txs[posUnhandled]->GetTxnPtr()->GetId()] != ScheduleStatus::NOT_STARTED) {
            posUnhandled = ++scanPos;
        }
    }

    while (numTasksScheduled < MAX_TO_SCHEDULE
           && scanPos < (numTasksScheduled == 0 ? txs.size() : std::min(scanPos + MAX_SCAN_WINDOW, txs.size()))) {
        if (CanStartValidation(scanPos, {})) {
            if (IsSpendersGraphReady()) {
                // If spender's graph is ready then schedule this transaction and the chain starting at it.
                ScheduleChain(scanPos, taskResults);
            } else {
                // Schedule just this transaction.
                SubmitTask(scanPos, taskResults);
            }
        }
        ++scanPos;
    }

#ifdef DEBUG_SCHEDULER
    const std::chrono::time_point tsEnd = std::chrono::steady_clock::now();
    LogPrint(BCLog::TXNVAL, "==<< ScanTransactions, run time:%dus\n", std::chrono::duration_cast<std::chrono::microseconds>(tsEnd-tsStart).count());
#endif
}

void ValidationScheduler::ScheduleGraph(size_t rootPos,
                                        std::vector<std::future<TypeValidationResult>>& taskResults) {
#ifdef DEBUG_SCHEDULER
    LogPrint(BCLog::TXNVAL, "==>> ScheduleGraph: %d\n", rootPos);
    const std::chrono::time_point tsStart = std::chrono::steady_clock::now();
#endif

    auto &rootTx = txs[rootPos]->GetTxnPtr();
    auto txSpenders = spenders.equal_range(rootTx->GetId());
    for (auto iterSpender = txSpenders.first; iterSpender != txSpenders.second; ++iterSpender) {
        size_t spenderPos = iterSpender->second;
        if (CanStartValidation(spenderPos, {})) {
            ScheduleChain(spenderPos, taskResults);
        }
    }

#ifdef DEBUG_SCHEDULER
    const std::chrono::time_point tsEnd = std::chrono::steady_clock::now();
    LogPrint(BCLog::TXNVAL, "==<< ScheduleGraph, run time:%dus\n\n", std::chrono::duration_cast<std::chrono::microseconds>(tsEnd-tsStart).count());
#endif
}

void ValidationScheduler::ScheduleChain(size_t rootPos,
                                        std::vector<std::future<TypeValidationResult>>& taskResults) {
#ifdef DEBUG_SCHEDULER
    LogPrint(BCLog::TXNVAL, "==>> ScheduleChain: %d\n", rootPos);
    const std::chrono::time_point tsStart = std::chrono::steady_clock::now();
#endif

    // transactions to schedule in this task
    std::vector<size_t> txsInTask;
    std::optional<size_t> iTxPos = rootPos;
    std::unordered_set<TxId> assumedDone;
    do {
        txsInTask.push_back(iTxPos.value());
        const TxId& txId = txs[iTxPos.value()]->GetTxnPtr()->GetId();
        // We only want to detect chains where only one tx in the batch spends output in parent tx.
        // If there are more than one spenders of one parent tx, then we want to schedule 
        // those in parallel. Which is handled by ScheduleGraph.
        if (spenders.count(txId) == 1) {
            iTxPos = spenders.find(txId)->second;
            assumedDone = {txId};
        } else {
            iTxPos = {};
        }
    } while (iTxPos.has_value()
             && CanStartValidation(iTxPos.value(), assumedDone));

    SubmitTask(std::move(txsInTask), taskResults);

#ifdef DEBUG_SCHEDULER
    const std::chrono::time_point tsEnd = std::chrono::steady_clock::now();
    LogPrint(BCLog::TXNVAL, "==<< ScheduleChain, run time:%dus\n\n", std::chrono::duration_cast<std::chrono::microseconds>(tsEnd-tsStart).count());
#endif
}

bool ValidationScheduler::CanStartValidation(const size_t txPos, const std::unordered_set<TxId>& assumeDone) {
    auto& tx = txs[txPos]->GetTxnPtr();
    auto txStatus = txStatuses.find(tx->GetId());
    if (txStatus != txStatuses.end() && txStatus->second != ScheduleStatus::NOT_STARTED) {
        // tx is already processed or validation is running just now.
        return false;
    }
    for (const auto& inputAAA : tx->vin) {
        const COutPoint &outPoint = inputAAA.prevout;
        auto inputStatus = txStatuses.find(outPoint.GetTxId());
        if (inputStatus == txStatuses.end()) {
            // This outPoint is not present in txs_statuses, therefore the outPoint tx is not present in the batch.
            // Therefore tx is still candidate to start validation. Continue checking other inputs.
            continue;
        } else {
            switch (inputStatus->second) {
                case ScheduleStatus::IN_PROGRESS:
                    // If inputs are being validated then validation for tx can't start
                    return false;
                case ScheduleStatus::DONE:
                    // Continue checking other inputs.
                    continue;
                case ScheduleStatus::NOT_STARTED:
                    // Check if this outPoint is to be validated before in the same task.
                    if (assumeDone.find(outPoint.GetTxId()) == assumeDone.end()) {
                        // Not validated yet and not in the same task
                        return false;
                    }
            }
        }
    }

    // all criteria are satisfied.
    return true;
}

void ValidationScheduler::SubmitTask(const size_t pos,
                                     std::vector<std::future<TypeValidationResult>>& results) {
    SubmitTask(std::vector<size_t>{pos}, results);
}

void ValidationScheduler::SubmitTask(std::vector<size_t>&& txPositions,
                                     std::vector<std::future<TypeValidationResult>>& results) {
    // Note: 'MarkResult' can be called after scheduler is already gone.
    //       This is because scheduler exits/is dismissed after all txn are scheduled.
    //       Scheduler doesn't wait for all scheduled validation tasks to complete.
    //       This is because scheduler doesn't care about results.
    //       Scheduler only cares to schedule validation for all transactions.
    auto weakSelf = this->weak_from_this();
    // Also copy the validation function. Scheduler can be gone, when task function is called. See above.
    auto func = validationFunc;

    TxInputDataSPtrRefVec txsToValidate;
    txsToValidate.reserve(txPositions.size());
    TxValidationPriority priority = TxValidationPriority::high;
    for (size_t tx_pos : txPositions) {
        std::shared_ptr<CTxInputData> &tx = txs[tx_pos];
        txsToValidate.emplace_back(tx);
        priority = std::min(priority, tx->GetTxValidationPriority());
        // Bookkeeping
        txStatuses[tx->GetTxnPtr()->GetId()] = ScheduleStatus::IN_PROGRESS;
    }

#ifdef DEBUG_SCHEDULER
    {
        std::stringstream strPositions;
        for (size_t pos : txPositions) {
            strPositions << pos << " ";
        }
        LogPrint(BCLog::TXNVAL, "====>> task %d: %s\n", numTasksScheduled, strPositions.str());
    }
#endif

    CTask task{priority == TxValidationPriority::low ? CTask::Priority::Low : CTask::Priority::High};
    results.emplace_back(
            task.injectTask([weakSelf, func, txPositions=std::move(txPositions)](const TxInputDataSPtrRefVec& vTxInputData) mutable
                            {
                                // First run validation.
                                auto&& result = func(vTxInputData);
                                // Then report back to scheduler that validation is finished.
                                auto strong_self = weakSelf.lock();
                                if (strong_self) {
                                    strong_self->MarkResult(std::move(txPositions), ScheduleStatus::DONE);
                                }
                                // Finally return task result.
                                return std::move(result);
                            },
                            std::move(txsToValidate)));

    ++numTasksScheduled;
    validatorThreadPool.submit(std::move(task));
}

void ValidationScheduler::MarkResult(std::vector<size_t>&& positions, ScheduleStatus result) {
    // Save result into results queue.
    {
        std::scoped_lock<std::mutex> lock(taskCompletionMtx);
        taskCompletionQueue.emplace_back(std::move(positions), result);
    }

    // Notify scheduler.
    taskCompletionCV.notify_one();
}

void ValidationScheduler::BuildSpendersMap() {
    // Set of known txn ids.
    std::unordered_set<TxId> txnIds{txs.size()};
    for (auto& tx : txs) {
        txnIds.emplace(tx->GetTxnPtr()->GetId());
    }

    for (size_t i = 0; i < txs.size(); ++i) {
        if (!buildSpendersThreadRun) {
            // All transactions are already scheduled. Stop building the map as we don't need it any more.
            return;
        }
        auto& txnPtr = txs[i]->GetTxnPtr();
        // Transaction can spend several outputs of the parent transaction. 
        // In such case we want only one link from parent transaction to spending transaction.  
        std::unordered_set<TxId> parents(std::min(txnPtr->vin.size(), (size_t)10));
        for (const CTxIn &txIn : txnPtr->vin) {
            const TxId &parentId = txIn.prevout.GetTxId();
            if (parents.find(parentId) == parents.end() && txnIds.find(parentId) != txnIds.end()) {
                spenders.emplace(parentId, i);
                parents.emplace(parentId);
            }
        }
    }
    spendersReady = true;
}

void ValidationScheduler::DrawGraph() {
#ifdef DEBUG_SCHEDULER
    const auto fileName = strprintf("graph_batch_%d.gv", _batchNum);
    const fs::path &filePath = GetDataDir() / fileName;
    std::ofstream outfile;
    outfile.open(filePath.c_str());
    outfile << "digraph G {" << std::endl
            << "rankdir=TD" << std::endl
            << std::endl
            << "edge[weight=2, style=invis];" << std::endl;
    // nodes in one line - top to down
    std::unordered_map<TxId, size_t> idToPos(txs.size());
    for (size_t n = 0; n < txs.size(); ++n) {
        idToPos[txs[n]->GetTxnPtr()->GetId()] = n;
        if (n % 20 == 0) {
            if (n == 0) {
                outfile << n;
            } else {
                outfile << " -> " << n << std::endl << n;
            }
        } else {
            outfile << " -> " << n;
        }
    }

    // edges
    outfile << std::endl
            << "edge[weight=1, style=solid];" << std::endl;
    for (size_t n = 0; n < txs.size(); ++n) {
        for (const CTxIn &txIn : txs[n]->GetTxnPtr()->vin) {
            auto iterPrev = idToPos.find(txIn.prevout.GetTxId());
            if (iterPrev != idToPos.end()) {
                size_t nPrev = iterPrev->second;
                outfile << n << " -> " << nPrev << std::endl;
            }
        }
    }

    outfile << "}" << std::endl;
    outfile.close();
#endif
}
