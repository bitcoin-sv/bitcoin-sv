// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include <compat.h>
#include <enum_cast.h>
#include <net/send_queue_bytes.h>
#include <sync.h>

#include <memory>

#include <boost/circular_buffer.hpp>

class CConnman;
class CNode;
class Config;
class CSerializedNetMsg;
class CStreamStats;

// Enumerate possible stream types
enum class StreamType
{
    UNKNOWN,
    GENERAL,
    BLOCK,
    TRANSACTION
};
// Enable enum_cast for StreamType, so we can log informatively
const enumTableT<StreamType>& enumTable(StreamType);

/**
 * A stream is a single channel of communiction carried over an association
 * between 2 peers.
 */
class CStream
{
  public:
    CStream(CNode& node, StreamType streamType, SOCKET socket);
    ~CStream();

    CStream(const CStream&) = delete;
    CStream& operator=(const CStream&) = delete;
    CStream(CStream&& that) = delete;
    CStream& operator=(CStream&&) = delete;

    // Shutdown the stream
    void Shutdown();

    // Add our socket to the sets for reading and writing
    bool SetSocketForSelect(fd_set& setRecv, fd_set& setSend, fd_set& setError,
                            SOCKET& socketMax, bool pauseRecv) const;

    // Service our socket for reading and writing
    void ServiceSocket(fd_set& setRecv, fd_set& setSend, fd_set& setError,
                       CConnman& connman, const Config& config, const CNetAddr& peerAddr,
                       bool& gotNewMsgs, size_t& bytesRecv, size_t& bytesSent);


    // Add new message to our list for sending
    size_t PushMessage(std::vector<uint8_t>&& serialisedHeader, CSerializedNetMsg&& msg,
                       size_t nPayloadLength, size_t nTotalSize);

    // Move newly read completed messages to the callers queue
    size_t GetNewMsgs(std::list<CNetMessage>& msgList);

    // Get last send/receive time
    int64_t GetLastSendTime() const { return mLastSendTime; }
    int64_t GetLastRecvTime() const { return mLastRecvTime; }

    // Copy out our stats
    void CopyStats(CStreamStats& stats) const;

    // Update average bandwidth measurements
    void AvgBandwithCalc();

    // Get estimated average bandwidth from peer
    AverageBandwidth GetAverageBandwidth() const;

    // Get current send queue size
    size_t GetSendQueueSize() const;

  private:

    // Node we are for
    CNode& mNode;

    // What does this stream carry?
    StreamType mStreamType { StreamType::UNKNOWN };

    // Our socket
    SOCKET mSocket {0};
    mutable CCriticalSection cs_mSocket {};

    // Send message queue
    std::deque<std::unique_ptr<CForwardAsyncReadonlyStream>> mSendMsgQueue {};
    uint64_t mTotalBytesSent {0};
    CSendQueueBytes mSendMsgQueueSize {};
    mutable CCriticalSection cs_mSendMsgQueue {};

    // Receive message queue
    std::list<CNetMessage> mRecvMsgQueue {};
    uint64_t mTotalBytesRecv {0};
    mutable CCriticalSection cs_mRecvMsgQueue {};

    // Last time we sent or received anything
    std::atomic<int64_t> mLastSendTime {0};
    std::atomic<int64_t> mLastRecvTime {0};

    // Process some newly read bytes from our underlying socket
    enum RECV_STATUS {RECV_OK, RECV_BAD_LENGTH, RECV_FAIL};
    RECV_STATUS ReceiveMsgBytes(const Config& config, const char* pch, size_t nBytes, bool& complete);

    // Write the next batch of data to the wire
    size_t SocketSendData();

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

};

using CStreamPtr = std::shared_ptr<CStream>;
