// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include <net/net.h>
#include <net/netbase.h>
#include <net/stream.h>

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
    bool IsOversizedMessage(const Config& config, const CNetMessage& msg)
    {   
        if(!msg.in_data)
        {   
            // Header only, cannot be oversized.
            return false;
        }

        return msg.hdr.IsOversized(config);
    }

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
        LogPrint(BCLog::NET, "closing %s stream to peer=%d\n", enum_cast<std::string>(mStreamType),
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
                bytesRecv = static_cast<uint64_t>(nBytes);
                const RECV_STATUS status = ReceiveMsgBytes(config, pchBuf, bytesRecv, gotNewMsgs);
                if (status != RECV_OK)
                {   
                    mNode->CloseSocketDisconnect();
                    if (status == RECV_BAD_LENGTH)
                    {   
                        // Ban the peer if try to send messages with bad length
                        throw BanStream{};
                    }
                }
            }
            else if (nBytes == 0)
            {   
                // socket closed gracefully
                if (!mNode->GetDisconnect())
                {   
                    LogPrint(BCLog::NET, "stream socket closed\n");
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
                        LogPrintf("stream socket recv error %s\n", NetworkErrorString(nErr));
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
    mSendMsgQueueSize += nTotalSize;

    mSendMsgQueue.push_back(std::make_unique<CVectorStream>(std::move(serialisedHeader)));
    if(nPayloadLength)
    {   
        mSendMsgQueue.push_back(msg.MoveData());
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
        mRecvMsgQueueSize -= msg->vRecv.size() + CMessageHeader::HEADER_SIZE;
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

void Stream::SetOwningNode(CNode* newNode)
{
    LOCK(cs_mNode);
    mNode = newNode;
}

Stream::RECV_STATUS Stream::ReceiveMsgBytes(const Config& config, const char* pch, uint64_t nBytes,
    bool& complete)
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
        if (mRecvMsgQueue.empty() || mRecvMsgQueue.back()->complete())
        {
            mRecvMsgQueue.emplace_back(std::make_unique<CNetMessage>(Params().NetMagic(), SER_NETWORK, INIT_PROTO_VERSION));
        }

        CNetMessage& msg { *(mRecvMsgQueue.back()) };

        // Absorb network data.
        int handled;
        if (!msg.in_data)
        {   
            handled = msg.readHeader(config, pch, nBytes);
            if (handled < 0)
            {   
                return RECV_BAD_LENGTH;//Notify bad message as soon as seen in the header
            }
        }
        else
        {   
            handled = msg.readData(pch, nBytes);
        }

        if (handled < 0)
        {   
            return RECV_FAIL;
        }

        if (IsOversizedMessage(config, msg))
        {   
            LogPrint(BCLog::NET, "Oversized message from peer=%i, disconnecting\n", mNode->GetId());
            return RECV_BAD_LENGTH;
        }

        pch += handled;
        nBytes -= handled;

        if (msg.complete())
        {      
            msg.nTime = nTimeMicros;
            complete = true;
        }
    }   
    
    return RECV_OK;
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
        mSendMsgQueueSize -= sent.sentSize;

        if(sent.sendComplete == false)
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
        if(!(*it)->complete())
        {   
            break;
        }
        uint64_t msgSize { (*it)->vRecv.size() + CMessageHeader::HEADER_SIZE };
        nSizeAdded += msgSize;

        // Update recieved msg counts
        mapMsgCmdSize::iterator i { mRecvBytesPerMsgCmd.find((*it)->hdr.pchCommand) };
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

    if (maxChunkSize == 0)
    {   
        // if maxChunkSize is 0 assign some default chunk size value
        maxChunkSize = 1024;
    }
    uint64_t sentSize = 0;

    do
    {   
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

