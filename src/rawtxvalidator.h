// Copyright (c) 2022 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include <memory>
#include <thread>

#include "txn_validator.h"
#include "thread_safe_queue.h"
#include "validation.h"

class Config;

class RawTxValidator 
{

public:
    struct RawTxValidatorResult 
    {
        TxId txid;
        std::optional<CValidationState> state;
        bool evicted = false;
    };

private:
    struct ValidationTaskData 
    {
        std::unique_ptr<CTxInputData> txInputData;
        std::promise<RawTxValidatorResult> promise;
        size_t ApproximateSize() const 
        {
            return sizeof(*this) + txInputData->GetTxnPtr()->GetTotalSize();
        }
    };

    // data members
    CThreadSafeQueue<ValidationTaskData> queue;
    std::thread workerThread;

    void ThreadFunc();

public:
    RawTxValidator(const Config& conf);
    RawTxValidator(RawTxValidator &&) = delete;
    RawTxValidator &operator=(RawTxValidator &&) = delete;
    RawTxValidator(const RawTxValidator &) = delete;
    RawTxValidator &operator=(const RawTxValidator &) = delete;

    ~RawTxValidator();

    std::future<RawTxValidatorResult> SubmitSingle(std::unique_ptr<CTxInputData>);
    std::vector<std::future<RawTxValidatorResult>> SubmitMany(std::vector<std::unique_ptr<CTxInputData>>&);
};
