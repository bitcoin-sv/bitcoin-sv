// Copyright (c) 2019-2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include <atomic>

class CSendQueueBytes
{
private:
    // nSendQueueBytes holds data of how many bytes are currently in queue for specific node
    size_t nSendQueueBytes = 0;
    // nTotalSendQueuesBytes holds data of how many bytes are currently in all queues across the network (all nodes)
    static std::atomic_size_t nTotalSendQueuesBytes;

public:
    ~CSendQueueBytes() {
        nTotalSendQueuesBytes -= nSendQueueBytes;
    }

    size_t operator-= (size_t nBytes) {
        nSendQueueBytes -= nBytes;
        nTotalSendQueuesBytes -= nBytes;
        return nSendQueueBytes;
    }

     size_t operator+= (size_t nBytes) {
        nSendQueueBytes += nBytes;
        nTotalSendQueuesBytes += nBytes;
        return nSendQueueBytes;
    }

    size_t getSendQueueBytes() const {
        return nSendQueueBytes;
    }

    static size_t getTotalSendQueuesBytes() {
        return nTotalSendQueuesBytes;
    }
};

