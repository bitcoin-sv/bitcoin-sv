// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2017 The Bitcoin developers
// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include <net/association.h>
#include <net/net.h>
#include <net/netbase.h>
#include <net/node_stats.h>

namespace
{
    const std::string NET_MESSAGE_COMMAND_OTHER { "*other*" };
}

Association::Association(CNode* node, SOCKET socket, const CAddress& peerAddr)
: mNode{node}, mPeerAddr{peerAddr}
{
    // Create initial stream
    mStreams[StreamType::GENERAL] = std::make_shared<Stream>(mNode, StreamType::GENERAL, socket);

    // Setup bytes count per message type
    for(const std::string& msg : getAllNetMessageTypes())
    {
        mRecvBytesPerMsgCmd[msg] = 0;
    }
    mRecvBytesPerMsgCmd[NET_MESSAGE_COMMAND_OTHER] = 0;
}

AssociationIDPtr Association::GetAssociationID() const
{
    LOCK(cs_mAssocID);
    return mAssocID;
}

void Association::SetAssociationID(AssociationIDPtr&& id)
{
    LOCK(cs_mAssocID);
    mAssocID = std::move(id);
    LogPrint(BCLog::NET, "association ID set to %s for peer=%d\n", mAssocID->ToString(), mNode->GetId());
}

void Association::ClearAssociationID()
{
    LOCK(cs_mAssocID);
    mAssocID = nullptr;
    LogPrint(BCLog::NET, "association ID cleared for peer=%d\n", mNode->GetId());
}

Association::~Association()
{
    Shutdown();
}

CService Association::GetPeerAddrLocal() const
{
    LOCK(cs_mPeerAddr);
    return mPeerAddrLocal;
}

void Association::SetPeerAddrLocal(const CService& addrLocal)
{
    LOCK(cs_mPeerAddr);
    if(mPeerAddrLocal.IsValid())
    {
        error("Addr local already set for node: %i. Refusing to change from %s to %s",
              mNode->GetId(), mPeerAddrLocal.ToString(), addrLocal.ToString());
    }
    else
    {
        mPeerAddrLocal = addrLocal;
    }
}

void Association::Shutdown()
{
    // Shutdown all our streams
    LOCK(cs_mStreams);
    if(!mShutdown)
    {
        mShutdown = true;
        if(!mStreams.empty())
        {
            LogPrint(BCLog::NET, "disconnecting peer=%d\n", mNode->GetId());
            for(auto& stream : mStreams)
            {
                stream.second->Shutdown();
            }
        }
    }
}

void Association::MoveStream(StreamType newType, Association& to)
{
    // Lock both associations so we can move stream from one to the other atomically
    LOCK(cs_mStreams);
    LOCK(to.cs_mStreams);

    // Sanity check; we should only ever be moving a single stream at a time
    if(mStreams.size() != 1)
    {
        throw std::runtime_error("Unexpected number of streams being moved");
    }
    // Check we aren't overwriting an existing stream in the target association
    if(to.mStreams.find(newType) != to.mStreams.end())
    {
        throw std::runtime_error("Attempt to overwrite existing stream in move");
    }

    // Give stream to target association
    auto handle { mStreams.extract(mStreams.begin()) };
    StreamPtr streamToMove { handle.mapped() };
    streamToMove->SetStreamType(newType);
    streamToMove->SetOwningNode(to.mNode);
    to.mStreams[newType] = streamToMove;
}

uint64_t Association::GetAverageBandwidth() const
{
    LOCK(cs_mStreams);

    // Sum of averages from all streams
    uint64_t sumOfMeans {0};
    size_t sumOfNumberOfItems {0};

    for(const auto& stream : mStreams)
    {
        AverageBandwidth bw { stream.second->GetAverageBandwidth() };
        sumOfMeans += (bw.first * bw.second);
        sumOfNumberOfItems += bw.second;
    }

    if(sumOfNumberOfItems == 0)
    {
        return 0;
    }

    // Just return rounded sum of averages
    return static_cast<uint64_t>(sumOfMeans / sumOfNumberOfItems);
}

AverageBandwidth Association::GetAverageBandwidth(const StreamType streamType) const
{
    LOCK(cs_mStreams);

    if(mStreams.empty())
    {
        return {0,0};
    }

    // Do we have a stream that exactly matches the requested type?
    StreamMap::const_iterator streamIt { mStreams.find(streamType) };
    if(streamIt == mStreams.end())
    {
        // No we don't, so fall back to using the GENERAL stream
        streamIt = mStreams.begin();
    }

    return streamIt->second->GetAverageBandwidth();
}

void Association::CopyStats(AssociationStats& stats) const
{
    {
        // ID
        LOCK(cs_mAssocID);
        if(mAssocID)
        {
            stats.assocID = mAssocID->ToString();
        }
        else
        {
            stats.assocID = "Null";
        }
    }

    {
        // Build stream stats
        LOCK(cs_mStreams);
        for(const auto& stream : mStreams)
        {
            StreamStats streamStats {};
            stream.second->CopyStats(streamStats);
            stats.streamStats.push_back(std::move(streamStats));
        }
    }
    const std::vector<StreamStats>& streamStats { stats.streamStats };

    // Last send/recv times are the latest for any of our underlying streams
    if(streamStats.empty())
    {
        stats.nLastSend = 0;
        stats.nLastRecv = 0;
    }
    else
    {
        auto maxStreamSendTime = std::max_element(streamStats.begin(), streamStats.end(),
            [](const StreamStats& s1, const StreamStats& s2) {
                return s1.nLastSend < s2.nLastSend;
            }
        );
        stats.nLastSend = maxStreamSendTime->nLastSend;
        auto maxStreamRecvTime = std::max_element(streamStats.begin(), streamStats.end(),
            [](const StreamStats& s1, const StreamStats& s2) {
                return s1.nLastRecv < s2.nLastRecv;
            }
        );
        stats.nLastRecv = maxStreamRecvTime->nLastRecv;
    }

    stats.addr = mPeerAddr;
    stats.nAvgBandwidth = GetAverageBandwidth();

    // Total send/recv bytes for all our underlying streams
    stats.nSendBytes = std::accumulate(streamStats.begin(), streamStats.end(), 0ULL,
        [](const uint64_t& tot, const StreamStats& s) {
            return tot + s.nSendBytes;
        }
    );
    stats.nRecvBytes = std::accumulate(streamStats.begin(), streamStats.end(), 0ULL,
        [](const uint64_t& tot, const StreamStats& s) {
            return tot + s.nRecvBytes;
        }
    );

    // Total send queue bytes for all our underlying streams
    stats.nSendSize = std::accumulate(streamStats.begin(), streamStats.end(), 0ULL,
        [](const uint64_t& tot, const StreamStats& s) {
            return tot + s.nSendSize;
        }
    );

    {
        LOCK(cs_mSendRecvBytes);
        stats.mapSendBytesPerMsgCmd = mSendBytesPerMsgCmd;
        stats.mapRecvBytesPerMsgCmd = mRecvBytesPerMsgCmd;
    }
}

int64_t Association::GetLastSendTime() const
{
    // Get most recent send time for any of our underlying streams
    auto getLastSentTime = [](const StreamPtr& stream) { return stream->GetLastSendTime(); };
    std::vector<int64_t> streamTimes { ForEachStream(getLastSentTime) };
    if(streamTimes.empty())
    {
        return 0;
    }
    else
    {
        return *(std::max_element(streamTimes.begin(), streamTimes.end()));
    }
}

int64_t Association::GetLastRecvTime() const
{
    // Get most recent recv time for any of our underlying streams
    auto getLastRecvTime = [](const StreamPtr& stream) { return stream->GetLastRecvTime(); };
    std::vector<int64_t> streamTimes { ForEachStream(getLastRecvTime) };
    if(streamTimes.empty())
    {
        return 0;
    }
    else
    {
        return *(std::max_element(streamTimes.begin(), streamTimes.end()));
    }
}

bool Association::SetSocketsForSelect(fd_set& setRecv, fd_set& setSend, fd_set& setError,
                                       SOCKET& socketMax, bool pauseRecv) const
{
    bool havefds {false};

    // Get all sockets from each stream
    LOCK(cs_mStreams);
    for(const auto& stream : mStreams)
    {
        havefds |= stream.second->SetSocketForSelect(setRecv, setSend, setError, socketMax, pauseRecv);
    }

    return havefds;
}

size_t Association::GetNewMsgs(std::list<CNetMessage>& msgList)
{
    size_t nSizeAdded {0};

    // Fetch from all our streams
    std::list<CNetMessage> newMsgs {};
    {
        LOCK(cs_mStreams);
        for(auto& stream : mStreams)
        {
            nSizeAdded += stream.second->GetNewMsgs(newMsgs);
        }
    }

    // Update recieved msg counts
    {
        LOCK(cs_mSendRecvBytes);
        for(const CNetMessage& msg : newMsgs)
        {
            mapMsgCmdSize::iterator i { mRecvBytesPerMsgCmd.find(msg.hdr.pchCommand) };
            if (i == mRecvBytesPerMsgCmd.end())
            {   
                i = mRecvBytesPerMsgCmd.find(NET_MESSAGE_COMMAND_OTHER);
            }

            assert(i != mRecvBytesPerMsgCmd.end());
            i->second += msg.hdr.nPayloadLength + CMessageHeader::HEADER_SIZE;
        }
    }

    // Move all new msgs to callers list
    msgList.splice(msgList.end(), std::move(newMsgs));

    return nSizeAdded;
}

void Association::ServiceSockets(fd_set& setRecv, fd_set& setSend, fd_set& setError,
                                  CConnman& connman, const Config& config, bool& gotNewMsgs,
                                  size_t& bytesRecv, size_t& bytesSent)
{
    bytesRecv = bytesSent = 0;

    // Service each stream socket
    LOCK(cs_mStreams);
    for(auto& stream : mStreams)
    {
        size_t streamBytesRecv {0};
        size_t streamBytesSent {0};
        stream.second->ServiceSocket(setRecv, setSend, setError, connman, config, GetPeerAddr(),
            gotNewMsgs, streamBytesRecv, streamBytesSent);
        bytesRecv += streamBytesRecv;
        bytesSent += streamBytesSent;
    }
}

void Association::AvgBandwithCalc()
{
    // Let each stream do its own calculations
    ForEachStream([](StreamPtr& stream){ stream->AvgBandwithCalc(); });
}

size_t Association::GetTotalSendQueueSize() const
{
    // Get total of all stream send queue sizes
    LOCK(cs_mStreams);
    return std::accumulate(mStreams.begin(), mStreams.end(), 0,
        [](const size_t& tot, const auto& stream) {
            return tot + stream.second->GetSendQueueSize();
        }
    );
}

size_t Association::PushMessage(std::vector<uint8_t>&& serialisedHeader, CSerializedNetMsg&& msg)
{
    size_t nPayloadLength { msg.Size() };
    size_t nTotalSize { nPayloadLength + CMessageHeader::HEADER_SIZE };
    size_t nBytesSent {0};

    // Decide which stream to send this message on
    LOCK(cs_mStreams);
    if(!mStreams.empty())
    {
        StreamPtr& stream { mStreams[StreamType::GENERAL] };

        {
            // Log total amount of bytes per command
            LOCK(cs_mSendRecvBytes);
            mSendBytesPerMsgCmd[msg.Command()] += nTotalSize;
        }

        // Send it
        nBytesSent = stream->PushMessage(std::move(serialisedHeader), std::move(msg), nPayloadLength, nTotalSize);
    }
    else
    {
        LogPrint(BCLog::NET, "No stream available to send message on for peer=%d\n", mNode->GetId());
    }

    return nBytesSent;
}

