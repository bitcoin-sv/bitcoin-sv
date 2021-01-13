// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include <net/net.h>
#include <net/stream_policy.h>
#include <logging.h>

namespace
{
    // Classify messages we consider to be block related
    bool IsBlockMsg(const std::string& cmd, CSerializedNetMsg::PayloadType payloadType)
    {
        return cmd == NetMsgType::BLOCK ||
               cmd == NetMsgType::CMPCTBLOCK ||
               cmd == NetMsgType::BLOCKTXN ||
               cmd == NetMsgType::GETBLOCKTXN ||
               cmd == NetMsgType::HEADERS ||
               cmd == NetMsgType::GETHEADERS ||
               payloadType == CSerializedNetMsg::PayloadType::BLOCK;
    }

    // Classify msgs we consider high priority
    bool IsHighPriorityMsg(const CSerializedNetMsg& msg)
    {
        const std::string& cmd { msg.Command() };
        return cmd == NetMsgType::PING ||
               cmd == NetMsgType::PONG ||
               IsBlockMsg(cmd, msg.GetPayloadType());
    }
}


/*************************/
/** A BasicStreamPolicy **/
/*************************/

void BasicStreamPolicy::ServiceSockets(StreamMap& streams, fd_set& setRecv,
    fd_set& setSend, fd_set& setError, const Config& config, bool& gotNewMsgs,
    uint64_t& bytesRecv, uint64_t& bytesSent)
{
    // Service each stream socket
    for(auto& stream : streams)
    {   
        uint64_t streamBytesRecv {0};
        uint64_t streamBytesSent {0};
        stream.second->ServiceSocket(setRecv, setSend, setError, config, gotNewMsgs,
            streamBytesRecv, streamBytesSent);
        bytesRecv += streamBytesRecv;
        bytesSent += streamBytesSent;
    }
}

uint64_t BasicStreamPolicy::PushMessageCommon(StreamMap& streams, StreamType streamType,
    bool exactMatch, std::vector<uint8_t>&& serialisedHeader, CSerializedNetMsg&& msg,
    uint64_t nPayloadLength, uint64_t nTotalSize)
{
    // Find the appropriate stream
    StreamPtr destStream {nullptr};
    for(const auto& [type, stream] : streams)
    {
        if(type == streamType)
        {
            // Got the requested stream type
            destStream = stream;
            break;
        }
        else if(!exactMatch && type == StreamType::GENERAL)
        {
            // Can always send anything over a GENERAL stream
            destStream = stream;
        }
    }

    // If we found a stream, send
    if(!destStream)
    {
        std::stringstream err {};
        err << "No stream avilable of type " << enum_cast<std::string>(streamType)
            << " for message of type " << msg.Command();
        throw std::runtime_error(err.str());
    }

    return destStream->PushMessage(std::move(serialisedHeader), std::move(msg), nPayloadLength, nTotalSize);
}


/*****************************/
/** The DefaultStreamPolicy **/
/*****************************/

std::pair<Stream::QueuedNetMessage, bool> DefaultStreamPolicy::GetNextMessage(StreamMap& streams)
{
    // Check we have a stream available (if we do we will have the GENERAL stream)
    if(streams.size() > 0)
    {
        return streams[StreamType::GENERAL]->GetNextMessage();
    }

    return { nullptr, false };
}

uint64_t DefaultStreamPolicy::PushMessage(StreamMap& streams, StreamType streamType,
    std::vector<uint8_t>&& serialisedHeader, CSerializedNetMsg&& msg,
    uint64_t nPayloadLength, uint64_t nTotalSize)
{
    // Have we been told which stream to use?
    bool exactMatch { streamType != StreamType::UNKNOWN };

    // If we haven't been told which stream to use, decide which we would prefer
    if(!exactMatch)
    {
        // Send over the GENERAL stream
        streamType = StreamType::GENERAL;
    }

    return PushMessageCommon(streams, streamType, exactMatch, std::move(serialisedHeader),
        std::move(msg), nPayloadLength, nTotalSize);
}


/***********************************/
/** The BlockPriorityStreamPolicy **/
/***********************************/

void BlockPriorityStreamPolicy::SetupStreams(CConnman& connman, const CAddress& peerAddr,
    const AssociationIDPtr& assocID)
{
    LogPrint(BCLog::NET, "BlockPriorityStreamPolicy opening required streams\n");
    connman.QueueNewStream(peerAddr, StreamType::DATA1, assocID, GetPolicyName());
}

std::pair<Stream::QueuedNetMessage, bool> BlockPriorityStreamPolicy::GetNextMessage(StreamMap& streams)
{
    // Look for messages from streams in order of priority
    if(streams.count(StreamType::DATA1) == 1)
    {
        // Check highest priority DATA1 stream
        auto msg { streams[StreamType::DATA1]->GetNextMessage() };
        if(msg.first != nullptr)
        {
            return msg;
        }
    }

    if(streams.count(StreamType::GENERAL) == 1)
    {
        // Check lowest priority GENERAL stream
        return streams[StreamType::GENERAL]->GetNextMessage();
    }

    return { nullptr, false };
}

uint64_t BlockPriorityStreamPolicy::PushMessage(StreamMap& streams, StreamType streamType,
    std::vector<uint8_t>&& serialisedHeader, CSerializedNetMsg&& msg,
    uint64_t nPayloadLength, uint64_t nTotalSize)
{
    // Have we been told which stream to use?
    bool exactMatch { streamType != StreamType::UNKNOWN };

    // If we haven't been told which stream to use, decide which we would prefer
    if(!exactMatch)
    {
        if(IsHighPriorityMsg(msg))
        {
            // Pings, pongs and block msgs are sent over the high priority DATA1 stream if we have it
            streamType = StreamType::DATA1;
        }
        else
        {
            // Send over the GENERAL stream
            streamType = StreamType::GENERAL;
        }
    }

    return PushMessageCommon(streams, streamType, exactMatch, std::move(serialisedHeader),
        std::move(msg), nPayloadLength, nTotalSize);
}

StreamType BlockPriorityStreamPolicy::GetStreamTypeForMessage(MessageType msgType) const
{
    if(msgType == MessageType::BLOCK || msgType == MessageType::PING)
    {
        // Block & ping messages are sent over DATA1
        return StreamType::DATA1;
    }

    return StreamType::GENERAL;
}

