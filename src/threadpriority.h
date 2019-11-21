// Copyright (c) 2018-2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include <enum_cast.h>

// Some pre-defined thread priority levels.
enum class ThreadPriority : int
{
    Low = 0,
    Normal = 1,
    High = 2
};

// Enable enum_cast for ThreadPriority, so we can log informatively
const enumTableT<ThreadPriority>& enumTable(ThreadPriority);
