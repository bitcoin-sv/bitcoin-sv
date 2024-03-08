// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once
#include <mutex>
#include <thread>
#include <vector>
#include <string>
#include <cstddef>
#include "thread_safe_queue.h"

class CZMQPublisher
{
public:

    CZMQPublisher(CZMQPublisher const &) = delete;
    CZMQPublisher & operator= (CZMQPublisher const &) = delete;
    CZMQPublisher(CZMQPublisher &&) = delete;
    CZMQPublisher & operator= (CZMQPublisher &&) = delete;
    
    CZMQPublisher();
    ~CZMQPublisher();
    bool SendZMQMessage(void* psocket, const char* command, const void* data, size_t size, uint32_t nSequence);

private:

    // Objects of type ZMQMessage are created for pushing into the thread safe queue
    // Every object contains pointer to ZMQ socket and ZMQ message consisting of three parts
    // topic, data, sequence number
    struct ZMQMessage
    {
        void* socketPointer;
        std::string topic;
        std::vector<std::byte> data;
        uint32_t nSequence;

        ZMQMessage(void* socketPointer, const std::string& topic,  const void* data, size_t size, uint32_t nSequence) : 
            socketPointer(socketPointer),
            topic(topic),
            // NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast)
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
            data(std::vector<std::byte>(reinterpret_cast<const std::byte*>(data), reinterpret_cast<const std::byte*>(data) + size)),
            // NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast)
            nSequence(nSequence)
        {}

        // We need this function for thread safe queue, because sizes of ZMQMessages are not constant
        size_t MemoryUsage() const;
    };

    // Queue for messages that should be sent to ZMQ by worker thread
    CThreadSafeQueue<ZMQMessage> ZMQQueue;
    // Helper function used to send message in three parts; the command, data and the LE 4byte sequence number
    void SendMultipart(const ZMQMessage& message) const;

    // worker thread which takes messages from the queue and sends it to the ZMQ
    std::thread zmqThread; 
};
