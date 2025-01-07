// Copyright (c) 2024 BSV Blockchain Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#pragma once

#include "block_index_store.h"
#include "chain.h"

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
extern CChain chainActive;
extern CCriticalSection cs_main;
extern BlockIndexStore mapBlockIndex;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

struct chain_guard
{
    chain_guard()
    {
        chainActive.SetTip(nullptr);
        mapBlockIndex.clear();
    }

    ~chain_guard()
    {
        chainActive.SetTip(nullptr);
        mapBlockIndex.clear();
    }

    chain_guard(const chain_guard&) = default;
    chain_guard(chain_guard&&) = default;
    chain_guard& operator=(const chain_guard&) = default;
    chain_guard& operator=(chain_guard&&) = default;
};

