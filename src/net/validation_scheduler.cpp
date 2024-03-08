// Copyright (c) 2021 The Bitcoin SV developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "validation_scheduler.h"

#include "logging.h"
#include "util.h"

#include <algorithm>

#ifdef COLLECT_METRICS
#include "metrics.h"
#endif

// For development/debugging.
// If defined then graph of each input batch in Graphviz dot format is created in the working dir.
//#define SCHEDULER_OUTPUT_GRAPH
#ifdef SCHEDULER_OUTPUT_GRAPH
void DrawGraph(const TxInputDataSPtrVec& txs);
#endif

ValidationScheduler::ValidationScheduler(CThreadPool<CDualQueueAdaptor> &threadPool,
                                         TxInputDataSPtrVec &txs, TypeValidationFunc func)
        : validationFunc{std::move(func)},
          txs{txs},
          txStatuses{txs.size(), ScheduleStatus::NOT_STARTED},
          validatorThreadPool{threadPool},
          MAX_TO_SCHEDULE{threadPool.getPoolSize() * MAX_TO_SCHEDULE_FACTOR}
{
    // Initialize status for each transaction and build mapping from TxId to position.
    size_t txsSize = txs.size();
    txIdToPos.reserve(txsSize);
    for (size_t i = 0; i < txsSize; ++i) {
        txIdToPos[txs[i]->GetTxnPtr()->GetId()] = i;
    }

    // Build map of spenders asynchronously. Until map is ready or there is any error building it then we schedule without it.
    try {
        buildSpendersTask = std::async(std::launch::async, &ValidationScheduler::BuildSpendersMap, this);
    }
    catch (const std::exception& e) {
        PrintExceptionContinue(&e, "ValidationScheduler");
    }

#ifdef SCHEDULER_OUTPUT_GRAPH
    DrawGraph(txs);
#endif
}

ValidationScheduler::~ValidationScheduler() {
    // Note: In normal operation it should not happen that spenders graph is still being built when
    // all transactions were already scheduled and scheduler is exiting. Just in case stop building the map.
    buildSpendersTaskRun = false;
    try {
        buildSpendersTask.get();
    }
    catch (const std::exception& e) {
        PrintExceptionContinue(&e, "ValidationScheduler");
    }
}

std::vector<std::future<ValidationScheduler::TypeValidationResult>> ValidationScheduler::Schedule() {
#ifdef COLLECT_METRICS
    static metrics::Histogram durations_batch_t_ms {"PTV_SCHEDULER_BATCH_TIME_MS", 10000};
    static metrics::Histogram durations_wait_task_complete_us {"PTV_SCHEDULER_WAIT_TIME_US", 10000};
    static metrics::Histogram size_batch {"PTV_SCHEDULER_BATCH_SIZE", 20000};
    static metrics::Histogram num_scheduled_tasks {"PTV_SCHEDULER_NUM_SCHEDULED_TASKS", MAX_TO_SCHEDULE*10};
    static metrics::HistogramWriter histogramLogger {"PTV_SCHEDULER", std::chrono::milliseconds {10000}, []() {
        durations_batch_t_ms.dump();
        durations_wait_task_complete_us.dump();
        size_batch.dump();
        num_scheduled_tasks.dump();
    }};
    size_batch.count(txs.size());
    auto batchTimeTimer = metrics::TimedScope<std::chrono::steady_clock, std::chrono::milliseconds> { durations_batch_t_ms };
#endif
    // task results
    std::vector<std::future<TypeValidationResult>> taskResults;
    // Reserve a space for the result set (usually there is one task per transaction).
    taskResults.reserve(txs.size());

    // Keep running until all transactions are scheduled.
    while (posUnhandled < txs.size()) {
        // Process task_results input queue if there are any task_results waiting
        std::vector<TaskCompletion> lastResults;
        {
            std::lock_guard<std::mutex> resultsLock(taskCompletionMtx);
            if (!taskCompletionQueue.empty()) {
                // move all task_results from input queue to local vector.
                std::swap(taskCompletionQueue, lastResults);
            }
        }

        // Update txs statuses of finished transactions.
        for (const TaskCompletion &taskResult : lastResults) {
            --numTasksScheduled;
            for (auto pos : taskResult.positions) {
                txStatuses[pos] = taskResult.status;
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
#ifdef COLLECT_METRICS
            num_scheduled_tasks.count(numTasksScheduled);
            auto waitTimeTimer = metrics::TimedScope<std::chrono::steady_clock, std::chrono::microseconds> { durations_wait_task_complete_us };
#endif
            std::unique_lock<std::mutex> lock(taskCompletionMtx);
            taskCompletionCV.wait_for(lock, std::chrono::milliseconds (100), [this]{return !taskCompletionQueue.empty();});
        }
    }
    return taskResults;
}

void ValidationScheduler::ScanTransactions(std::vector<std::future<TypeValidationResult>>& taskResults) {
    if (scanPos >= txs.size()) {
        // Start new cycle.
        scanPos = posUnhandled;

        // Advance posUnhandled as far as possible.
        while (scanPos < txs.size() && txStatuses[posUnhandled] != ScheduleStatus::NOT_STARTED) {
            posUnhandled = ++scanPos;
        }
    }

    while (numTasksScheduled < MAX_TO_SCHEDULE
           && scanPos < (numTasksScheduled == 0 ? txs.size() : std::min(scanPos + MAX_SCAN_WINDOW, txs.size()))) {
        if (CanStartValidation(scanPos)) {
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
}

void ValidationScheduler::ScheduleGraph(const size_t rootPos,
                                        std::vector<std::future<TypeValidationResult>>& taskResults) {
    auto txSpenders = spenders.equal_range(rootPos);
    for(; txSpenders.first != txSpenders.second; ++txSpenders.first) {
        size_t spenderPos = txSpenders.first->second;
        if (CanStartValidation(spenderPos)) {
            ScheduleChain(spenderPos, taskResults);
        }
    }
}

void ValidationScheduler::ScheduleChain(const size_t rootPos,
                                        std::vector<std::future<TypeValidationResult>>& taskResults) {
    // transactions to schedule in this task
    std::vector<size_t> txsInTask;
    size_t iTxPos = rootPos;
    TxId prevTxId;
    do {
        txsInTask.push_back(iTxPos);
        prevTxId = txs[iTxPos]->GetTxnPtr()->GetId();
        // We only want to detect chains where only one tx in the batch spends output in parent tx.
        // If there are more than one spenders of one parent tx, then we want to schedule 
        // those in parallel. Which is handled by ScheduleGraph.
        if (spenders.count(iTxPos) == 1) {
            iTxPos = spenders.find(iTxPos)->second;
        } else {
            break;
        }
    } while (CanStartValidation(iTxPos, &prevTxId));

    SubmitTask(std::move(txsInTask), taskResults);
}

bool ValidationScheduler::CanStartValidation(const size_t txPos, const TxId* prevTxId) {
    auto& tx = txs[txPos]->GetTxnPtr();
    if (txStatuses[txPos] != ScheduleStatus::NOT_STARTED) {
        // tx is already processed or validation is running just now.
        return false;
    }
    for (const auto& input : tx->vin) {
        const COutPoint &outPoint = input.prevout;
        auto inputPos = txIdToPos.find(outPoint.GetTxId());
        if (inputPos == txIdToPos.end()) {
            // This outPoint tx is not present in the batch of transactions to validate.
            // Therefore tx is still candidate to start validation. Continue checking other inputs.
            continue;
        } else {
            switch (txStatuses[inputPos->second]) {
                case ScheduleStatus::IN_PROGRESS:
                    // If inputs are being validated then validation for tx can't start
                    return false;
                case ScheduleStatus::DONE:
                    // Continue checking other inputs.
                    continue;
                case ScheduleStatus::NOT_STARTED:
                    // Check if this outPoint is to be validated before in the same task.
                    if (prevTxId == nullptr || *prevTxId != outPoint.GetTxId()) {
                        // Not validated yet and not the parent in the same task
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
        txStatuses[tx_pos] = ScheduleStatus::IN_PROGRESS;
    }

    CTask task{priority == TxValidationPriority::low ? CTask::Priority::Low : CTask::Priority::High};
    results.emplace_back(
            task.injectTask([weakSelf, func, txPositions=std::move(txPositions)](const TxInputDataSPtrRefVec& vTxInputData) mutable
                            {
                                // First run validation.
                                TypeValidationResult result;
                                try {
                                    result = func(vTxInputData);
                                }
                                // In case of exceptions just log the error.
                                catch (const std::exception& e) {
                                    PrintExceptionContinue(&e, "ValidationScheduler");
                                }
                                catch (...) {
                                    PrintExceptionContinue(nullptr, "ValidationScheduler");
                                }

                                // Then report back to scheduler that validation is finished, successfully or not.
                                auto strong_self = weakSelf.lock();
                                if (strong_self) {
                                    strong_self->MarkResult(std::move(txPositions), ScheduleStatus::DONE);
                                }
                                // Finally return task result.
                                return result;
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

// Used when building spenders map for tracking parent transactions. 
// Space for up to this many parents is reserved up front.
constexpr size_t PARENTS_SET_RESERVE_SIZE = 10;

void ValidationScheduler::BuildSpendersMap() {
    for (size_t i = 0; i < txs.size(); ++i) {
        if (!buildSpendersTaskRun) {
            // All transactions are already scheduled. Stop building the map as we don't need it any more.
            return;
        }
        auto& txnPtr = txs[i]->GetTxnPtr();
        // Transaction can spend several outputs of the parent transaction. 
        // In such case we want only one link from parent transaction to spending transaction.  
        std::unordered_set<TxId> parents(std::min(txnPtr->vin.size(), PARENTS_SET_RESERVE_SIZE));
        for (const CTxIn &txIn : txnPtr->vin) {
            const TxId &parentId = txIn.prevout.GetTxId();
            if (parents.find(parentId) == parents.end()) {
                if (auto parentPos = txIdToPos.find(parentId); parentPos != txIdToPos.end()) { 
                    spenders.emplace(parentPos->second, i);
                    parents.emplace(parentId);
                }
            }
        }
    }
    spendersReady = true;
}

#ifdef SCHEDULER_OUTPUT_GRAPH
// Outputs graph of the given transaction batch in the Graphviz dot format.
// Only useful for development / debugging.
void DrawGraph(const TxInputDataSPtrVec& txs) {
    static std::atomic_size_t batchNum = 0;
    const auto fileName = strprintf("graph_batch_%d.gv", ++batchNum);
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
}
#endif
