// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include <config.h>
#include <net/net.h>
#include <net/netbase.h>
#include <net/stream.h>
#include "config.h"

// Enable enum_cast for StreamType, so we can log informatively
const enumTableT<StreamType>& enumTable(StreamType)
{
    static enumTableT<StreamType> table
    {
        { StreamType::UNKNOWN,  "UNKNOWN" },
        { StreamType::GENERAL,  "GENERAL" },
        { StreamType::DATA1,    "DATA1" },
        { StreamType::DATA2,    "DATA2" },
        { StreamType::DATA3,    "DATA3" },
        { StreamType::DATA4,    "DATA4" },
    };
    return table;
}

namespace
{
    const std::string NET_MESSAGE_COMMAND_OTHER { "*other*" };
}

Stream::Stream(CNode* node, StreamType streamType, SOCKET socket, uint64_t maxRecvBuffSize)
: mNode{node}, mStreamType{streamType}, mSocket{socket}, mMaxRecvBuffSize{maxRecvBuffSize}
{
    // Setup bytes count per message type
    for(const std::string& msg : getAllNetMessageTypes())
    {   
        mRecvBytesPerMsgCmd[msg] = 0;
    }
    mRecvBytesPerMsgCmd[NET_MESSAGE_COMMAND_OTHER] = 0;

    // Remember any sending rate limit that's been set
    mSendRateLimit = GlobalConfig::GetConfig().GetStreamSendRateLimit();

    // Fetch the MSS for the underlying socket
    int mss {0};
    socklen_t mssLen { sizeof(mss) };
#ifdef TCP_MAXSEG
#ifdef WIN32
    if(getsockopt(mSocket, IPPROTO_TCP, TCP_MAXSEG, reinterpret_cast<char*>(&mss), &mssLen) != SOCKET_ERROR)
#else
    if(getsockopt(mSocket, IPPROTO_TCP, TCP_MAXSEG, &mss, &mssLen) != SOCKET_ERROR)
#endif
    {
        // Sanity check read mss before using it
        size_t sizeMss { static_cast<size_t>(mss) };
        if(sizeMss > MIN_MAX_SEGMENT_SIZE && sizeMss <= MAX_MAX_SEGMENT_SIZE)
        {
            mMSS = sizeMss;
        }
    }
#endif
}

Stream::~Stream()
{
    Shutdown();
}

void Stream::Shutdown()
{
    LOCK(cs_mNode);

    // Close the socket connection
    LOCK(cs_mSocket);
    if(mSocket != INVALID_SOCKET)
    {
        LogPrint(BCLog::NETCONN, "closing %s stream to peer=%d\n", enum_cast<std::string>(mStreamType),
            mNode->GetId());
        CloseSocket(mSocket);
    }
}

bool Stream::SetSocketForSelect(fd_set& setRecv, fd_set& setSend, fd_set& setError,
                                SOCKET& socketMax) const
{
    // Implement the following logic:
    // * If there is data to send select() for sending data.
    // * If there is space left in the receive buffer select() for receiving data.

    bool select_recv = !mPauseRecv;
    bool select_send;
    {   
        LOCK(cs_mSendMsgQueue);
        select_send = !mSendMsgQueue.empty();
    }

    LOCK(cs_mSocket);
    if(mSocket == INVALID_SOCKET)
    {   
        return false;
    }

    FD_SET(mSocket, &setError);
    socketMax = std::max(socketMax, mSocket);

    if(select_send)
    {   
        FD_SET(mSocket, &setSend);
    }
    if(select_recv)
    {   
        FD_SET(mSocket, &setRecv);
    }

    return true;
}

void Stream::ServiceSocket(fd_set& setRecv, fd_set& setSend, fd_set& setError, const Config& config,
                           bool& gotNewMsgs, uint64_t& bytesRecv, uint64_t& bytesSent)
{
    //
    // Receive
    //
    bool recvSet = false;
    bool sendSet = false;
    bool errorSet = false;

    {
        LOCK(cs_mNode);
        {   
            LOCK(cs_mSocket);
            if (mSocket == INVALID_SOCKET)
            {   
                return;
            }
            recvSet = FD_ISSET(mSocket, &setRecv);
            sendSet = FD_ISSET(mSocket, &setSend);
            errorSet = FD_ISSET(mSocket, &setError);
        }
        if (recvSet || errorSet)
        {   
            // typical socket buffer is 8K-64K
            char pchBuf[0x10000];
            ssize_t nBytes = 0;

            {   
                LOCK(cs_mSocket);
                if (mSocket == INVALID_SOCKET)
                {   
                    return;
                }
                nBytes = recv(mSocket, pchBuf, sizeof(pchBuf), MSG_DONTWAIT);
            }
            if (nBytes > 0)
            {
                // Process received data
                bytesRecv = static_cast<uint64_t>(nBytes);
                ReceiveMsgBytes(config, pchBuf, bytesRecv, gotNewMsgs);
            }
            else if (nBytes == 0)
            {   
                // socket closed gracefully
                if (!mNode->GetDisconnect())
                {   
                    LogPrint(BCLog::NETCONN, "stream socket gracefully closed by peer=%d\n", mNode->id);
                }
                mNode->CloseSocketDisconnect();
            }
            else if (nBytes < 0)
            {
                // error
                int nErr = WSAGetLastError();
                if (nErr != WSAEWOULDBLOCK && nErr != WSAEMSGSIZE && nErr != WSAEINTR && nErr != WSAEINPROGRESS)
                {
                    if (!mNode->GetDisconnect())
                    {
                        LogPrint(BCLog::NETCONN, "stream socket recv error %s peer=%d\n",
                            NetworkErrorString(nErr), mNode->id);
                    }
                    mNode->CloseSocketDisconnect();
                }
            }
        }

        //
        // Send
        //
        if (sendSet)
        {
            bytesSent = SocketSendData();
        }
    }

    // Pull out any completely received msgs
    if(gotNewMsgs)
    {
        GetNewMsgs();
    }
}

uint64_t Stream::PushMessage(std::vector<uint8_t>&& serialisedHeader, CSerializedNetMsg&& msg,
    uint64_t nPayloadLength, uint64_t nTotalSize)
{   
    uint64_t nBytesSent {0};

    LOCK(cs_mNode);
    LOCK(cs_mSendMsgQueue);
    bool optimisticSend { mSendMsgQueue.empty() };

    // Log total amount of bytes per command
    mSendBytesPerMsgCmd[msg.Command()] += nTotalSize;

    // Track send queue length
    mSendMsgQueueSize.AddBytesQueued(nTotalSize);

    // Combine short messages and their header into a single item in the queue.
    // This helps to reduce the number of TCP segments sent and so reduces wastage.
    if(nPayloadLength && nTotalSize <= mMSS)
    {
        // Extract all payload from the underlying stream and combine it with the header
        serialisedHeader.reserve(nTotalSize);
        auto payloadStream { msg.MoveData() };
        while(!payloadStream->EndOfStream())
        {
            const CSpan& data { payloadStream->ReadAsync(msg.Size()) };
            serialisedHeader.insert(serialisedHeader.end(), data.Begin(), data.Begin() + data.Size());
        }

        // Queue combined header & data
        auto combinedStream { msg.headerStreamCreator ? msg.headerStreamCreator(std::move(serialisedHeader)) : std::make_unique<CVectorStream>(std::move(serialisedHeader)) };
        mSendMsgQueueSize.AddMemoryUsed(combinedStream->GetEstimatedMaxMemoryUsage());
        mSendMsgQueue.push_back(std::move(combinedStream));
    }
    else
    {
        // Queue header and payload separately
        auto headerStream { msg.headerStreamCreator ? msg.headerStreamCreator(std::move(serialisedHeader)) : std::make_unique<CVectorStream>(std::move(serialisedHeader)) };
        mSendMsgQueueSize.AddMemoryUsed(headerStream->GetEstimatedMaxMemoryUsage());
        mSendMsgQueue.push_back(std::move(headerStream));
        if(nPayloadLength)
        {
            auto payloadStream { msg.MoveData() };
            mSendMsgQueueSize.AddMemoryUsed(payloadStream->GetEstimatedMaxMemoryUsage());
            mSendMsgQueue.push_back(std::move(payloadStream));
        }
    }

    // If write queue empty, attempt "optimistic write"
    if(optimisticSend)
    {   
        nBytesSent = SocketSendData();
    }

    return nBytesSent;
}

std::pair<Stream::QueuedNetMessage, bool> Stream::GetNextMessage()
{
    QueuedNetMessage msg {nullptr};

    LOCK(cs_mRecvMsgQueue);

    // If we have completed msgs queued, return the first one
    if(!mRecvCompleteMsgQueue.empty())
    {
        msg = std::move(mRecvCompleteMsgQueue.front());
        mRecvCompleteMsgQueue.pop_front();

        // Update total queued msgs size
        mRecvMsgQueueSize -= msg->GetTotalLength();
        mPauseRecv = mRecvMsgQueueSize > mMaxRecvBuffSize;
    }

    // Return whether we still have more msgs queued
    return { std::move(msg), !mRecvCompleteMsgQueue.empty() };
}

void Stream::CopyStats(StreamStats& stats) const
{
    stats.streamType = enum_cast<std::string>(mStreamType);
    stats.nLastSend = mLastSendTime;
    stats.nLastRecv = mLastRecvTime;

    {
        LOCK(cs_mSendMsgQueue);
        stats.nSendBytes = mTotalBytesSent;
        stats.nSendSize = mSendMsgQueueSize.getSendQueueBytes();
        stats.nSendMemory = mSendMsgQueueSize.getSendQueueMemory();
        stats.mapSendBytesPerMsgCmd = mSendBytesPerMsgCmd;
    }

    {
        LOCK(cs_mRecvMsgQueue);
        stats.nRecvBytes = mTotalBytesRecv;
        stats.fPauseRecv = mPauseRecv;
        stats.nRecvSize = mRecvMsgQueueSize;
        stats.mapRecvBytesPerMsgCmd = mRecvBytesPerMsgCmd;

        // Avg bandwidth measurements
        if(!mAvgBandwidth.empty())
        {
            stats.nMinuteBytesPerSec = GetAverageBandwidth().first;
            stats.nSpotBytesPerSec = static_cast<uint64_t>(mAvgBandwidth.back());
        }
        else
        {
            stats.nMinuteBytesPerSec = 0;
            stats.nSpotBytesPerSec = 0;
        }
    }
}

void Stream::AvgBandwithCalc()
{   
    LOCK(cs_mRecvMsgQueue);
    int64_t currTime { GetTimeMicros() };
    if(mLastSpotMeasurementTime > 0)
    {   
        double secsSinceLastSpot { static_cast<double>(currTime - mLastSpotMeasurementTime) / MICROS_PER_SECOND };
        if(secsSinceLastSpot > 0)
        {   
            double spotbw { mBytesRecvThisSpot / secsSinceLastSpot };
            mAvgBandwidth.push_back(spotbw);
        }
    }

    mLastSpotMeasurementTime = currTime;
    mBytesRecvThisSpot = 0;
}

// If we have sufficient samples then get average bandwidth from node,
// otherwise we must be in early startup measuring the bandwidth so just
// report it as 0.
AverageBandwidth Stream::GetAverageBandwidth() const
{
    LOCK(cs_mRecvMsgQueue);

    if(!mAvgBandwidth.empty())
    {   
        // If we don't yet have a full minutes worth of measurements then just
        // average with what we have
        uint64_t avg { static_cast<uint64_t>(Average(mAvgBandwidth.begin(), mAvgBandwidth.end())) };
        return { avg, mAvgBandwidth.size() };
    }

    return {0,0};
}

uint64_t Stream::GetSendQueueSize() const
{
    LOCK(cs_mSendMsgQueue);
    return mSendMsgQueueSize.getSendQueueBytes();
}
uint64_t Stream::GetSendQeueMemoryUsage() const
{
    LOCK(cs_mSendMsgQueue);
    return mSendMsgQueueSize.getSendQueueMemory();
}

void Stream::SetOwningNode(CNode* newNode)
{
    LOCK(cs_mNode);
    mNode = newNode;
}

void Stream::ReceiveMsgBytes(const Config& config, const char* pch, uint64_t nBytes, bool& complete)
{
    AssertLockHeld(cs_mNode);

    complete = false;
    int64_t nTimeMicros = GetTimeMicros();

    LOCK(cs_mRecvMsgQueue);
    mLastRecvTime = nTimeMicros / MICROS_PER_SECOND;
    mTotalBytesRecv += nBytes;
    mBytesRecvThisSpot += nBytes;

    while (nBytes > 0)
    {   
        // Get current incomplete message, or create a new one.
        if (mRecvMsgQueue.empty() || mRecvMsgQueue.back()->Complete())
        {
            mRecvMsgQueue.emplace_back(std::make_unique<CNetMessage>(Params().NetMagic(), SER_NETWORK, INIT_PROTO_VERSION));
        }

        CNetMessage& msg { *(mRecvMsgQueue.back()) };

        // Absorb network data
        uint64_t handled { msg.Read(config, pch, nBytes) };

        pch += handled;
        nBytes -= handled;

        if (msg.Complete())
        {      
            msg.SetTime(nTimeMicros);
            complete = true;
        }
    }   
}   

uint64_t Stream::SocketSendData()
{   
    uint64_t nSentSize = 0;
    uint64_t nMsgCount = 0;
    uint64_t nSendBufferMaxSize = g_connman->GetSendBufferSize();

    AssertLockHeld(cs_mNode);
    LOCK(cs_mSendMsgQueue);

    for(const auto& data : mSendMsgQueue)
    {   
        auto sent = SendMessage(*data, nSendBufferMaxSize);
        nSentSize += sent.sentSize;
        mSendMsgQueueSize.SubBytesQueued(sent.sentSize);

        if(sent.sendComplete)
        {
            mSendMsgQueueSize.SubMemoryUsed(data->GetEstimatedMaxMemoryUsage());
        }
        else
        {   
            break;
        }

        ++nMsgCount;
    }

    mSendMsgQueue.erase(mSendMsgQueue.begin(), mSendMsgQueue.begin() + nMsgCount);

    if (mSendMsgQueue.empty())
    {   
        assert(!mSendChunk);
        assert(mSendMsgQueueSize.getSendQueueBytes() == 0);
        assert(mSendMsgQueueSize.getSendQueueMemory() == 0);
    }

    return nSentSize;
}

void Stream::GetNewMsgs()
{
    uint64_t nSizeAdded {0};

    LOCK(cs_mRecvMsgQueue);
    auto it { mRecvMsgQueue.begin() };
    for(; it != mRecvMsgQueue.end(); ++it)
    {   
        if(!(*it)->Complete())
        {   
            break;
        }
        uint64_t msgSize { (*it)->GetTotalLength() };
        nSizeAdded += msgSize;

        // Update recieved msg counts
        mapMsgCmdSize::iterator i { mRecvBytesPerMsgCmd.find((*it)->GetHeader().GetCommand()) };
        if(i == mRecvBytesPerMsgCmd.end())
        {   
            i = mRecvBytesPerMsgCmd.find(NET_MESSAGE_COMMAND_OTHER);
        }

        assert(i != mRecvBytesPerMsgCmd.end());
        i->second += msgSize;
    }

    mRecvCompleteMsgQueue.splice(mRecvCompleteMsgQueue.end(), mRecvMsgQueue, mRecvMsgQueue.begin(), it);

    // Track total queued complete msgs size
    mRecvMsgQueueSize += nSizeAdded;
    mPauseRecv = mRecvMsgQueueSize > mMaxRecvBuffSize;
}

Stream::CSendResult Stream::SendMessage(CForwardAsyncReadonlyStream& data, uint64_t maxChunkSize)
{
    AssertLockHeld(cs_mNode);
    AssertLockHeld(cs_mSendMsgQueue);

    if (maxChunkSize == 0 || mSendRateLimit >= 0)
    {   
        // If maxChunkSize is 0 or we're applying rate limiting for testing,
        // assign some small default chunk size value
        maxChunkSize = 1024;
    }
    uint64_t sentSize = 0;

    do
    {
        // See if we need to apply a sending rate limit
        if(mSendRateLimit >= 0)
        {
            double timeSending { static_cast<double>(GetTimeMicros() - mSendStartTime) };
            double avgBytesSec { (mTotalBytesSent / timeSending) * MICROS_PER_SECOND };
            if(avgBytesSec >= mSendRateLimit)
            {
                // Don't send any more for now
                return {false, sentSize};
            }
        }

        ssize_t nBytes = 0;
        if (!mSendChunk)
        {   
            mSendChunk = data.ReadAsync(maxChunkSize);

            if (!mSendChunk->Size())
            {   
                // we need to wait for data to load so we should let others
                // send data in the meantime
                mSendChunk = std::nullopt;
                return {false, sentSize};
            }
        }

        {   
            LOCK(cs_mSocket);
            if (mSocket == INVALID_SOCKET)
            {   
                return {false, sentSize};
            }

            nBytes = send(mSocket,
                          reinterpret_cast<const char *>(mSendChunk->Begin()),
                          mSendChunk->Size(),
                          MSG_NOSIGNAL | MSG_DONTWAIT);
        }

        if (nBytes == 0)
        {   
            // couldn't send anything at all
            return {false, sentSize};
        }
        if (nBytes < 0)
        {   
            // error
            int nErr = WSAGetLastError();
            if (nErr != WSAEWOULDBLOCK && nErr != WSAEMSGSIZE && nErr != WSAEINTR && nErr != WSAEINPROGRESS)
            {
                LogPrintf("socket send error %s\n", NetworkErrorString(nErr));
                mNode->CloseSocketDisconnect();
            }

            return {false, sentSize};
        }

        assert(nBytes > 0);
        mLastSendTime = GetSystemTimeInSeconds();
        mTotalBytesSent += nBytes;
        sentSize += nBytes;
        if (static_cast<uint64_t>(nBytes) != mSendChunk->Size())
        {
            // could not send full message; stop sending more
            mSendChunk =
                CSpan {
                    mSendChunk->Begin() + nBytes,
                    mSendChunk->Size() - nBytes
                };
            return {false, sentSize};
        }

        mSendChunk = std::nullopt;
    } while(!data.EndOfStream());

    return {true, sentSize};
}

