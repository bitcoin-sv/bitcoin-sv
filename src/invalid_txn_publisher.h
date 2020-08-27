// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include "consensus/consensus.h"

enum class InvalidTxEvictionPolicy
{
    IGNORE_NEW,
    DELETE_OLD
};

// Class used for asynchronous publishing invalid transactions to different sinks,
// implemented as singleton, thread safe
class CInvalidTxnPublisher
{
public:
    static constexpr int64_t DEFAULT_FILE_SINK_DISK_USAGE = 3 * ONE_GIGABYTE;
    static constexpr InvalidTxEvictionPolicy DEFAULT_FILE_SINK_EVICTION_POLICY = InvalidTxEvictionPolicy::IGNORE_NEW;
#if ENABLE_ZMQ
    static constexpr int64_t DEFAULT_ZMQ_SINK_MAX_MESSAGE_SIZE = 500 * ONE_MEGABYTE;
#endif
};

