// Copyright (c) 2022 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "rawtxvalidator.h"
#include "config.h"
#include "mining/journal_change_set.h"
#include "config.h"


void RawTxValidator::ThreadFunc() 
{
    while (true)
    {
        auto batch = queue.PopAllWait();
        if (!batch.has_value())
        {
            assert(queue.IsClosed());
            return;
        }

        TxInputDataSPtrVec vTxInputData{};
        vTxInputData.reserve(batch.value().size());
        for (const auto &txData : batch.value()) 
        {
            vTxInputData.push_back(txData.txInputData);
        }
        LogPrint(BCLog::RPC, "Processing a batch of %s transactions from sendrawtransaction/sendrawtransactions\n", batch->size());
        CTxnValidator::RejectedTxns rejectedTxns{};
        // Apply journal changeSet straight after processValidation call.
        {
            // Mempool Journal ChangeSet
            mining::CJournalChangeSetPtr changeSet{ mempool.getJournalBuilder().getNewChangeSet(mining::JournalUpdateReason::NEW_TXN)};
            // Run synch batch validation and wait for results.
            const auto txValidator = g_connman->getTxnValidator();
            rejectedTxns = txValidator->processValidation(
                vTxInputData, // A vector of txns that need to be processed
                changeSet,    // an instance of the journal
                true);        // fLimitMempoolSize
        }

        auto &[mapRejectReasons, evictedTxsVector] = rejectedTxns;
        std::set<TxId> evictedTxsSet{ evictedTxsVector.begin(), evictedTxsVector.end() };

        for (auto &txData : batch.value()) 
        {
            const auto txid = txData.txInputData->GetTxnPtr()->GetId();
            const auto itReject = mapRejectReasons.find(txid);
            const auto itEvict = evictedTxsSet.find(txid);
            RawTxValidatorResult result{txid,
                                        (itReject != mapRejectReasons.end())
                                            ? std::optional<CValidationState>{itReject->second}
                                            : std::nullopt,
                                        itEvict != evictedTxsSet.end()};
            txData.promise.set_value(result);
        }
    }
}

RawTxValidator::RawTxValidator(const Config& conf)
    : queue(conf.GetMaxTxSize(true, true) + sizeof(RawTxValidator::ValidationTaskData),  
            [](const ValidationTaskData &data) { return data.ApproximateSize(); })
{
    workerThread = 
        std::thread([this]() 
        {
            TraceThread("rawtransactionvalidator", [this]() { ThreadFunc(); });
        });
}

RawTxValidator::~RawTxValidator() 
{
    queue.Close(true);
    if (workerThread.joinable())
    {
        workerThread.join();
    }
}

std::future<RawTxValidator::RawTxValidatorResult> 
RawTxValidator::SubmitSingle(TxInputDataSPtr txInputData) 
{
    ValidationTaskData taskData{std::move(txInputData), std::promise<RawTxValidatorResult>()};
    auto future = taskData.promise.get_future();
    queue.PushWait(std::move(taskData));
    return future;
}

std::vector<std::future<RawTxValidator::RawTxValidatorResult>> 
RawTxValidator::SubmitMany(const std::vector<TxInputDataSPtr> &txInputDataVec) 
{
    std::vector<std::future<RawTxValidatorResult>> futures;
    std::vector<ValidationTaskData> taskDataVec;
    for (const auto &txInputData : txInputDataVec)
    {
        ValidationTaskData taskData{txInputData, std::promise<RawTxValidatorResult>()};
        futures.push_back(taskData.promise.get_future());
        taskDataVec.emplace_back(std::move(taskData));
    }
    queue.PushManyWait(std::move(taskDataVec));
    return futures;
}
