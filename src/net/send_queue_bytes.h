// Copyright (c) 2019-2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include <atomic>

// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions)
class CSendQueueBytes
{
private:
    // nSendQueueBytes holds data of how many bytes are currently in queue for specific node
    size_t nSendQueueBytes = 0;
    // nTotalSendQueuesBytes holds data of how many bytes are currently in all queues across the network (all nodes)
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
    inline static std::atomic_size_t nTotalSendQueuesBytes = 0;
        
    // Holds estimate of how many bytes are currently taken up in memory by queue for specific node
    size_t nSendQueueMemory = 0;
    // Holds estimate of how many bytes are currently taken up in memory by queues across all nodes
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
    inline static std::atomic_size_t nTotalSendQueuesMemory = 0;

public:
    ~CSendQueueBytes() {
        nTotalSendQueuesBytes -= nSendQueueBytes;
        nTotalSendQueuesMemory -= nSendQueueMemory;
    }

    void AddBytesQueued(size_t nBytes) {
        nSendQueueBytes += nBytes;
        nTotalSendQueuesBytes += nBytes;
    }
    void SubBytesQueued(size_t nBytes) {
        nSendQueueBytes -= nBytes;
        nTotalSendQueuesBytes -= nBytes;
    }

    void AddMemoryUsed(size_t nBytes) {
        nSendQueueMemory += nBytes;
        nTotalSendQueuesMemory += nBytes;
    }
    void SubMemoryUsed(size_t nBytes) {
        nSendQueueMemory -= nBytes;
        nTotalSendQueuesMemory -= nBytes;
    }

    size_t getSendQueueBytes() const {
        return nSendQueueBytes;
    }
    static size_t getTotalSendQueuesBytes() {
        return nTotalSendQueuesBytes;
    }

    size_t getSendQueueMemory() const {
        return nSendQueueMemory;
    }
    static size_t getTotalSendQueuesMemory() {
        return nTotalSendQueuesMemory;
    }
};

