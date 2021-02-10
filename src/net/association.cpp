// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2017 The Bitcoin developers
// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include <net/association.h>
#include <net/net.h>
#include <net/netbase.h>
#include <net/node_stats.h>

#include <algorithm>

namespace
{
    // Take msg sizes per command per stream and combine into a single per command total
    std::pair<mapMsgCmdSize,mapMsgCmdSize> CombineStreamMsgCmdSizes(const std::vector<StreamStats>& allStreamStats)
    {
        mapMsgCmdSize sendResult {};
        mapMsgCmdSize recvResult {};

        for(const StreamStats& streamStats : allStreamStats)
        {
            for(const auto& [cmd, total] : streamStats.mapSendBytesPerMsgCmd)
            {
                sendResult[cmd] += total;
            }
            for(const auto& [cmd, total] : streamStats.mapRecvBytesPerMsgCmd)
            {
                recvResult[cmd] += total;
            }
        }

        return { sendResult, recvResult };
    }
}


Association::Association(CNode* node, SOCKET socket, const CAddress& peerAddr)
: mNode{node}, mPeerAddr{peerAddr}
{
    // Create initial stream
    mStreams[StreamType::GENERAL] = std::make_shared<Stream>(mNode, StreamType::GENERAL, socket,
        g_connman->GetReceiveFloodSize());
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

void Association::OpenRequiredStreams(CConnman& connman)
{
    // On inbound connections we wait to see what the other side wants to do
    if(!mNode->fInbound)
    {
        // If required, queue attempts to create additional streams to our peer
        AssociationIDPtr assocID { GetAssociationID() };
        if(assocID)
        {
            LOCK(cs_mStreams);

            // Create policy
            mStreamPolicy = connman.GetStreamPolicyFactory().Make(mNode->GetPreferredStreamPolicyName());

            // Queue messages to setup an further required streams
            LogPrint(BCLog::NET, "Queuing new stream requests to peer=%d\n", mNode->id);
            mStreamPolicy->SetupStreams(connman, mPeerAddr, assocID);
        }
    }
    else
    {
        LogPrint(BCLog::NET, "AssociationID not set so not queuing new stream requests to peer=%d\n",
            mNode->id);
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

void Association::ReplaceStreamPolicy(const StreamPolicyPtr& newPolicy)
{
    {
        LOCK(cs_mStreams);
        mStreamPolicy = newPolicy;
    }
    LogPrint(BCLog::NET, "Stream policy changed to %s for peer=%d\n", newPolicy->GetPolicyName(), mNode->id);
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
            stats.assocID = AssociationID::NULL_ID_STR;
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
        stats.streamPolicyName = mStreamPolicy->GetPolicyName();
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

    // Total send/recv queue bytes for all our underlying streams
    stats.nSendSize = std::accumulate(streamStats.begin(), streamStats.end(), 0ULL,
        [](const uint64_t& tot, const StreamStats& s) {
            return tot + s.nSendSize;
        }
    );
    stats.nRecvSize = std::accumulate(streamStats.begin(), streamStats.end(), 0ULL,
        [](const uint64_t& tot, const StreamStats& s) {
            return tot + s.nRecvSize;
        }
    );

    // Per command msg sizes
    const auto& [sendSizes, recvSizes] { CombineStreamMsgCmdSizes(streamStats) };
    stats.mapSendBytesPerMsgCmd = sendSizes;
    stats.mapRecvBytesPerMsgCmd = recvSizes;
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

bool Association::GetPausedForReceiving(PausedFor anyAll) const
{
    LOCK(cs_mStreams);

    // Get whether ANY or ALL of our underlying streams are paused
    if(anyAll == PausedFor::ANY)
    {
        return std::any_of(mStreams.begin(), mStreams.end(),
            [](const auto& stream){ return stream.second->GetPausedForReceiving(); });
    }
    else
    {
        return std::all_of(mStreams.begin(), mStreams.end(),
            [](const auto& stream){ return stream.second->GetPausedForReceiving(); });
    }
}

bool Association::SetSocketsForSelect(fd_set& setRecv, fd_set& setSend, fd_set& setError,
                                      SOCKET& socketMax) const
{
    bool havefds {false};

    // Get all sockets from each stream
    LOCK(cs_mStreams);
    for(const auto& stream : mStreams)
    {
        havefds |= stream.second->SetSocketForSelect(setRecv, setSend, setError, socketMax);
    }

    return havefds;
}

std::pair<Stream::QueuedNetMessage, bool> Association::GetNextMessage()
{
    LOCK(cs_mStreams);
    return mStreamPolicy->GetNextMessage(mStreams);
}

void Association::ServiceSockets(fd_set& setRecv, fd_set& setSend, fd_set& setError,
                                  CConnman& connman, const Config& config, bool& gotNewMsgs,
                                  uint64_t& bytesRecv, uint64_t& bytesSent)
{
    bytesRecv = bytesSent = 0;

    // Service each stream socket
    try
    {
        LOCK(cs_mStreams);
        mStreamPolicy->ServiceSockets(mStreams, setRecv, setSend, setError, config,
            gotNewMsgs, bytesRecv, bytesSent);
    }
    catch(BanStream& ban)
    {
        connman.Ban(GetPeerAddr(), BanReasonNodeMisbehaving);
    }
}

void Association::AvgBandwithCalc()
{
    // Let each stream do its own calculations
    ForEachStream([](StreamPtr& stream){ stream->AvgBandwithCalc(); });
}

uint64_t Association::GetTotalSendQueueSize() const
{
    // Get total of all stream send queue sizes
    LOCK(cs_mStreams);
    return std::accumulate(mStreams.begin(), mStreams.end(), 0,
        [](const uint64_t& tot, const auto& stream) {
            return tot + stream.second->GetSendQueueSize();
        }
    );
}

uint64_t Association::PushMessage(std::vector<uint8_t>&& serialisedHeader, CSerializedNetMsg&& msg, StreamType streamType)
{
    uint64_t nPayloadLength { msg.Size() };
    uint64_t nTotalSize { nPayloadLength + CMessageHeader::HEADER_SIZE };
    uint64_t nBytesSent {0};

    try
    {
        // Send the message
        LOCK(cs_mStreams);
        nBytesSent = mStreamPolicy->PushMessage(mStreams, streamType, std::move(serialisedHeader),
            std::move(msg), nPayloadLength, nTotalSize);
    }
    catch(std::exception& e)
    {
        LogPrint(BCLog::NET, "Failed to send message (%s) for peer=%d\n", e.what(), mNode->id);
    }

    return nBytesSent;
}

