// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include <net/net_message.h>
#include <net/net_types.h>
#include <net/send_queue_bytes.h>
#include <streams.h>

#include <boost/circular_buffer.hpp>

class CConnman;
class CNode;
class CNodeStats;
class Config;
class CSerializedNetMsg;

/**
 * An association is a connection between 2 peers which may carry
 * multiple independent streams of data.
 */
class CAssociation
{ 
  public:
    CAssociation(const CAssociation&) = delete;
    CAssociation(CAssociation&&) = delete;
    CAssociation& operator=(const CAssociation&) = delete;
    CAssociation& operator=(CAssociation&&) = delete;

    CAssociation(CNode& node, SOCKET socket, const CAddress& peerAddr);
    ~CAssociation();


    // Get peer address
    const CAddress& GetPeerAddr() const { return mPeerAddr; }

    // Get/Set peers local address
    CService GetPeerAddrLocal() const;
    void SetPeerAddrLocal(const CService& addrLocal);

    // Shutdown the connection
    void Shutdown();

    // Copy out current statistics
    void CopyStats(CNodeStats& stats) const;

    // Write the next batch of data to the wire
    size_t SocketSendData();

    // Add our sockets to the sets for reading and writing
    bool SetSocketsForSelect(fd_set& setRecv, fd_set& setSend, fd_set& setError,
                             SOCKET& socketMax, bool pauseRecv) const;

    // Move newly read completed messages to the caller's queue
    size_t GetNewMsgs(std::list<CNetMessage>& msgList);

    // Service all sockets that are ready
    void ServiceSockets(fd_set& setRecv, fd_set& setSend, fd_set& setError, CConnman& connman,
                        const Config& config, bool& gotNewMsgs, size_t& bytesRecv, size_t& bytesSent);

    // Get last send/receive time
    int64_t GetLastSendTime() const { return mLastSendTime; }
    int64_t GetLastRecvTime() const { return mLastRecvTime; }

    // Get current send queue size
    size_t GetSendQueueSize() const;

    // Update average bandwidth measurements
    void AvgBandwithCalc();

    // Get estimated average bandwidth from peer
    uint64_t GetAverageBandwidth() const;

    // Add new message to our list for sending
    size_t PushMessage(std::vector<uint8_t>&& serialisedHeader, CSerializedNetMsg&& msg);

  private:

    // Node we are for
    CNode& mNode;

    // Socket
    SOCKET mSocket {0};
    mutable CCriticalSection cs_mSocket {};

    // Send message queue
    std::deque<std::unique_ptr<CForwardAsyncReadonlyStream>> mSendMsgQueue {};
    uint64_t mTotalBytesSent {0};
    CSendQueueBytes mSendMsgQueueSize {};
    mapMsgCmdSize mSendBytesPerMsgCmd {};
    mutable CCriticalSection cs_mSendMsgQueue {};

    // The address of the remote peer
    const CAddress mPeerAddr {};
    CService mPeerAddrLocal {};
    mutable CCriticalSection cs_mPeerAddr {};

    // Receive message queue
    std::list<CNetMessage> mRecvMsgQueue {};
    uint64_t mTotalBytesRecv {0};
    mapMsgCmdSize mRecvBytesPerMsgCmd {};
    mutable CCriticalSection cs_mRecvMsgQueue {};

    // Last time we sent or received anything
    std::atomic<int64_t> mLastSendTime {0};
    std::atomic<int64_t> mLastRecvTime {0};

    /** Average bandwidth measurements */
    // Keep enough spot measurements to cover 1 minute
    boost::circular_buffer<double> mAvgBandwidth {60 / PEER_AVG_BANDWIDTH_CALC_FREQUENCY_SECS};
    // Time we last took a spot measurement
    int64_t mLastSpotMeasurementTime { GetTimeMicros() };
    // Bytes received since last spot measurement
    uint64_t mBytesRecvThisSpot {0};

    /**
     * Storage for the last chunk being sent to the peer. This variable contains
     * data for the duration of sending the chunk. Once the chunk is sent it is
     * cleared.
     * In case there is an interruption during sending (sent size exceeded or
     * network layer can not process any more data at the moment) this variable
     * remains set and is used to continue streaming on the next try.
     */
    std::optional<CSpan> mSendChunk {};

    /**
     * Notification structure for SendMessage function that returns:
     * sendComplete: whether the send was fully complete/partially complete and
     *               data is needed for sending the rest later.
     * sentSize: amount of data that was sent.
     */
    struct CSendResult
    {   
        bool sendComplete {false};
        size_t sentSize {0};
    };

    // Message sending
    CSendResult SendMessage(CForwardAsyncReadonlyStream& data, size_t maxChunkSize);

    // Process some newly read bytes from our underlying socket
    enum RECV_STATUS {RECV_OK, RECV_BAD_LENGTH, RECV_FAIL};
    RECV_STATUS ReceiveMsgBytes(const Config& config, const char* pch, size_t nBytes, bool& complete);

};
