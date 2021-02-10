// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.


#include "zmq_publisher.h"
#include "zmqpublishnotifier.h"
#include "consensus/consensus.h"
#include <logging.h>
#include <vector>
#include <zmq.h>
#include <memusage.h>
#include <util.h>

/* Internal function to send the message. Returns false if message could not be sent.
 * Set lastMessage to false to send additional messages as multipart, true otherwise.
 */
static bool zmq_send_message(void* socket, const void* data, const size_t size, bool lastMessage)
{
    zmq_msg_t msg;
    int rc = zmq_msg_init_size(&msg, size);
    if (rc != 0)
    {
        zmqError("Unable to initialize ZMQ msg");
        return false;
    }

    void *buf = zmq_msg_data(&msg);
    memcpy(buf, data, size);

    rc = zmq_msg_send(&msg, socket, lastMessage ? 0 : ZMQ_SNDMORE);
    if (rc == -1)
    {
        zmqError("Unable to send ZMQ msg");
        zmq_msg_close(&msg);
        return false;
    }
    zmq_msg_close(&msg);
    return true;
}

size_t CZMQPublisher::ZMQMessage::MemoryUsage() const
{
    // sizeof(CZMQPublisher) consists of sizes of all members. data and topic can allocate on the heap, so we need to add their sizes
    return sizeof(CZMQPublisher) + memusage::DynamicUsage(data) + topic.capacity();
}

CZMQPublisher::CZMQPublisher()
    :ZMQQueue(4*ONE_GIGABYTE, [](const ZMQMessage& message){ return message.MemoryUsage(); })
{
    auto threadFunction = [this]()
    {
        while(true)
        {
            auto message = ZMQQueue.PopWait();
            
            if(!message.has_value())
            {
                if(!ZMQQueue.IsClosed())
                {
                    LogPrintf("Could not get a zmq message from the thread safe queue!\n");
                }
                break;
            }

            if(!message->socketPointer)
            {
                LogPrintf("Socket pointer in thread safe queue is null\n");
                continue;
            }

            SendMultipart(message.value());
        }
    };

    // only start thread if it is not already running
    if (!zmqThread.joinable())
    {
        zmqThread = std::thread([threadFunction](){TraceThread("zmqpublisherthread", threadFunction);});
    }
}

CZMQPublisher::~CZMQPublisher()
{
    if(!ZMQQueue.IsClosed())
    {
        ZMQQueue.Close(true);
    }

    if(zmqThread.joinable())
    {
        zmqThread.join();
    }
}

bool CZMQPublisher::SendZMQMessage(void* psocket, const char* command, const void* data, size_t size, uint32_t nSequence)
{
    if (ZMQQueue.IsClosed())
    {
        return false;
    }

    if (!psocket)
    {
        return false;
    }

    ZMQMessage message{psocket, command, data, size, nSequence};

    if(!ZMQQueue.PushWait(std::move(message)))
    {
        LogPrintf("Pushing message to the thread safe queue failed.\n");
        return false;
    }

    return true;
}

void CZMQPublisher::SendMultipart(const ZMQMessage& message) const
{
    // Send the command, data and the sequence number
    if (zmq_send_message(message.socketPointer, message.topic.c_str(), message.topic.length(), false) &&
        zmq_send_message(message.socketPointer, message.data.data(), message.data.size(), false))
    {
        // Calculate and send LE 4byte sequence number
        std::vector<uint8_t> msgSequence(sizeof(uint32_t));
        WriteLE32(msgSequence.data(), message.nSequence);
        zmq_send_message(message.socketPointer, msgSequence.data(), msgSequence.size(), true);
    }
}
