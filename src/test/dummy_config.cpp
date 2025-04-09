// Copyright (c) 2025 BSV Blockchain Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#include "dummy_config.h"

DummyConfig::DummyConfig():chainParams(CreateChainParams(CBaseChainParams::REGTEST))
{}

DummyConfig::DummyConfig(const std::string& net):chainParams(CreateChainParams(net))
{}

void DummyConfig::SetChainParams(const std::string& net)
{
    chainParams = CreateChainParams(net);
}

int DummyConfig::GetMaxConcurrentAsyncTasksPerNode() const
{
    return DEFAULT_NODE_ASYNC_TASKS_LIMIT;
}

int DummyConfig::GetMaxParallelBlocks() const
{
    return DEFAULT_SCRIPT_CHECK_POOL_SIZE;
}

int DummyConfig::GetPerBlockTxnValidatorThreadsCount() const
{
    return DEFAULT_SCRIPTCHECK_THREADS;
}

int DummyConfig::GetPerBlockScriptValidatorThreadsCount() const
{
    return DEFAULT_SCRIPTCHECK_THREADS;
}

int DummyConfig::GetPerBlockScriptValidationMaxBatchSize() const
{
    return DEFAULT_SCRIPT_CHECK_MAX_BATCH_SIZE;
}

void DummyConfig::Reset() {}
