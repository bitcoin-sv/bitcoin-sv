// Copyright (c) 2021 The Bitcoin SV developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include "threadpool.h"
#include "txn_validation_data.h"
#include "txmempool.h"
#include "validation.h"
#include "logging.h"

#include <future>

// Schedule status for each transaction in the batch.
enum class ScheduleStatus {
    NOT_STARTED = 0,
    IN_PROGRESS,
    DONE
};

// Used to notify scheduler when task completes.
struct TaskCompletion {
    std::vector<size_t> positions;
    ScheduleStatus status;

    explicit TaskCompletion(std::vector<size_t>&& positions, const ScheduleStatus status) : positions{std::move(positions)}, status{status} {}
};

/*
 * Schedules validation tasks for given batch of transactions in topological order.
 */
class ValidationScheduler : public std::enable_shared_from_this<ValidationScheduler> {
public:

    using TypeValidationResult = std::vector<std::pair<CTxnValResult, CTask::Status>>;
    using TypeValidationFunc = std::function<TypeValidationResult(const TxInputDataSPtrRefVec& vTxInputData)>;

    ValidationScheduler(CThreadPool<CDualQueueAdaptor> &threadPool, TxInputDataSPtrVec &txs, TypeValidationFunc func);

    ValidationScheduler(const ValidationScheduler&) = delete;
    ValidationScheduler& operator=(const ValidationScheduler&) = delete;
    ValidationScheduler(ValidationScheduler&&) = delete;
    ValidationScheduler& operator=(ValidationScheduler&&) = delete;

    ~ValidationScheduler();

    // Schedules transactions given in the constructor. Returns validation results when all transactions are scheduled.
    // Note: Method exits as soon as all transaction validations are scheduled. Method doesn't wait for 
    // validation tasks to complete.
    std::vector<std::future<TypeValidationResult>> Schedule();

private:
    TypeValidationFunc validationFunc;

    // txs to be validated
    TxInputDataSPtrVec& txs;

    // For each txn we must know if it was already scheduled.
    std::vector<ScheduleStatus> txStatuses;
    
    // Mapping from TxId to position in input batch.
    std::unordered_map<TxId, size_t> txIdToPos;

    // Counts how many tasks are currently scheduled.
    size_t numTasksScheduled = 0;
    // Index into transactions to validate. Before this position all txs are already scheduled.
    size_t posUnhandled = 0;
    // Optimization. Index into input transactions to validate. 
    // Used when scanning for candidates. So that we don't always start from posUnhandled.
    size_t scanPos = 0;

    CThreadPool<CDualQueueAdaptor>& validatorThreadPool;

    // input queue for task completion notifications
    std::vector<TaskCompletion> taskCompletionQueue;
    std::mutex taskCompletionMtx;
    // notifies that task just completed
    std::condition_variable taskCompletionCV;

    // Desired number of concurrently scheduled tasks.
    // Used to optimize scheduling of graphs and chains. i.e. don't schedule all independent txs up front as this
    // would delay validation of txs in chains to the end of batch.
    // This is calculated from number of available validator threads and a factor.
    // Higher number is better for isolated transactions. Lower number is better if chains are mixed in.
    const size_t MAX_TO_SCHEDULE;
    // Factor for number of concurrently scheduled tasks.
    // Found with experiments. Higher value doesn't add any benefit.
    static const size_t MAX_TO_SCHEDULE_FACTOR = 8;
    static const size_t MAX_SCAN_WINDOW = 256;

    // Map of spenders i.e. links from transactions to transactions that spend its outputs.
    // Map is build out-of-band in a separate thread.
    std::unordered_multimap<size_t, size_t> spenders;
    std::atomic_bool spendersReady = false;
    // task to build spenders map
    std::future<void> buildSpendersTask;
    std::atomic_bool buildSpendersTaskRun = true;

public:
    // Returns true if graph of spenders is ready.
    bool IsSpendersGraphReady() {
        return spendersReady;
    }

private:
    // Creates task to validate given vector of transactions and submits it to the validation pool.
    // resulting futures are saved into given results vector.
    void SubmitTask(std::vector<size_t>&& txPositions,
                    std::vector<std::future<TypeValidationResult>>& results);
    void SubmitTask(size_t pos,
                    std::vector<std::future<TypeValidationResult>>& results);

    // Callback for task completion notification.
    void MarkResult(std::vector<size_t>&& positions, ScheduleStatus result);

    // Returns true if transaction at the given position can be scheduled now.
    // Validation can be started when all parent transactions in this batch are already validated.
    // If set then prevTxId should be set to the id of the previous transaction in the same task.
    bool CanStartValidation(size_t txPos, const TxId* prevTxId = nullptr);

    // Scans for yet unscheduled txn and schedules them if possible.
    void ScanTransactions(std::vector<std::future<TypeValidationResult>>& taskResults);

    // Schedules given root tx and all child transactions that are in the same chain and can be scheduled now.
    void ScheduleChain(size_t rootPos,
                       std::vector<std::future<TypeValidationResult>>& taskResults);
    /* 
     * Traverses the graph of spenders starting at rootPos and schedules validation tasks for all
     * spenders that can be scheduled now.
     */
    void ScheduleGraph(size_t rootPos,
                       std::vector<std::future<TypeValidationResult>>& taskResults);

    // Builds forward map from transaction to transactions that spend it.
    void BuildSpendersMap();
};

