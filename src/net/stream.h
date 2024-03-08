// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include <compat.h>
#include <enum_cast.h>
#include <net/net_message.h>
#include <net/net_types.h>
#include <net/send_queue_bytes.h>
#include <streams.h>
#include <sync.h>
#include <utiltime.h>

#include <atomic>
#include <exception>
#include <list>
#include <memory>
#include <optional>
#include <vector>

#include <boost/circular_buffer.hpp>

class CConnman;
class CNetAddr;
class CNode;
class Config; // NOLINT(cppcoreguidelines-virtual-class-destructor)
class CSerializedNetMsg;
class StreamStats;

/**
 * Enumerate possible stream types.
 *
 * All associations have at least a GENERAL stream established, and anything
 * can always be sent over a GENERAL stream.
 *
 * Streams DATA1 - DATA4 are optional additional general purpose streams that
 * can be used for whatever the currently active stream policy deems useful.
 */
enum class StreamType : uint8_t
{
    UNKNOWN = 0,
    GENERAL,
    DATA1,
    DATA2,
    DATA3,
    DATA4,

    MAX_STREAM_TYPE
};
// Enable enum_cast for StreamType, so we can log informatively
const enumTableT<StreamType>& enumTable(StreamType);

/**
 * A stream is a single channel of communication carried over an association
 * between 2 peers.
 */
class Stream
{
  public:
    // Default stream sending bandwidth rate limit to apply (no limit)
    static constexpr int64_t DEFAULT_SEND_RATE_LIMIT {-1};

    Stream(CNode* node, StreamType streamType, SOCKET socket, uint64_t maxRecvBuffSize);
    ~Stream();

    Stream(const Stream&) = delete;
    Stream& operator=(const Stream&) = delete;
    Stream(Stream&& that) = delete;
    Stream& operator=(Stream&&) = delete;

    // Shutdown the stream
    void Shutdown();

    // Add our socket to the sets for reading and writing
    bool SetSocketForSelect(fd_set& setRecv, fd_set& setSend, fd_set& setError, SOCKET& socketMax) const;

    // Service our socket for reading and writing
    void ServiceSocket(fd_set& setRecv, fd_set& setSend, fd_set& setError, const Config& config,
                       bool& gotNewMsgs, uint64_t& bytesRecv, uint64_t& bytesSent);


    // Add new message to our list for sending
    uint64_t PushMessage(std::vector<uint8_t>&& serialisedHeader, CSerializedNetMsg&& msg,
                         uint64_t nPayloadLength, uint64_t nTotalSize);

    // Fetch the next message for processing.
    // Also returns a boolean set true if there are more queued messages available, and false if not.
    using QueuedNetMessage = std::unique_ptr<CNetMessage>;
    std::pair<QueuedNetMessage, bool> GetNextMessage();

    // Get last send/receive time
    int64_t GetLastSendTime() const { return mLastSendTime; }
    int64_t GetLastRecvTime() const { return mLastRecvTime; }

    // Copy out our stats
    void CopyStats(StreamStats& stats) const;

    // Update average bandwidth measurements
    void AvgBandwithCalc();

    // Get estimated average bandwidth from peer
    AverageBandwidth GetAverageBandwidth() const;

    // Get current send queue size
    uint64_t GetSendQueueSize() const;
    // Get current estimated send queue memory usage
    uint64_t GetSendQeueMemoryUsage() const;

    // Get/Set stream type
    StreamType GetStreamType() const { return mStreamType; }
    void SetStreamType(StreamType streamType) { mStreamType = streamType; }

    // Set our owning CNode
    void SetOwningNode(CNode* newNode);

    // Get whether we're paused for receiving
    bool GetPausedForReceiving() const { return mPauseRecv; }

  private:

    // Minimum TCP maximum segment size. Used as the default maximum message
    // size for header/payload combining if we can't read the real MSS.
    static constexpr size_t MIN_MAX_SEGMENT_SIZE { 536 };
    // Maximum TCP maximum segment size
    static constexpr size_t MAX_MAX_SEGMENT_SIZE { 65535 };

    // Node we are for
    CNode* mNode {nullptr};
    mutable CCriticalSection cs_mNode {};

    // What does this stream carry?
    std::atomic<StreamType> mStreamType { StreamType::UNKNOWN };

    // Our socket
    SOCKET mSocket {0};
    mutable CCriticalSection cs_mSocket {};

    // TCP maximum segment size for our underlying socket
    size_t mMSS { MIN_MAX_SEGMENT_SIZE };

    // Send message queue
    std::deque<std::unique_ptr<CForwardAsyncReadonlyStream>> mSendMsgQueue {};
    uint64_t mTotalBytesSent {0};
    CSendQueueBytes mSendMsgQueueSize {};
    mapMsgCmdSize mSendBytesPerMsgCmd {};
    mutable CCriticalSection cs_mSendMsgQueue {};

    // Receive message queue
    std::list<QueuedNetMessage> mRecvMsgQueue {};
    uint64_t mTotalBytesRecv {0};
    uint64_t mRecvMsgQueueSize {0};
    std::atomic_bool mPauseRecv {false};
    mapMsgCmdSize mRecvBytesPerMsgCmd {};
    std::list<QueuedNetMessage> mRecvCompleteMsgQueue {};
    mutable CCriticalSection cs_mRecvMsgQueue {};

    // Last time we sent or received anything
    std::atomic<int64_t> mLastSendTime {0};
    std::atomic<int64_t> mLastRecvTime {0};

    // Maximum receieve queue size
    const uint64_t mMaxRecvBuffSize {0};

    // Process some newly read bytes from our underlying socket
    void ReceiveMsgBytes(const Config& config, const char* pch, uint64_t nBytes, bool& complete);

    // Write the next batch of data to the wire
    uint64_t SocketSendData();

    /** Average bandwidth measurements */
    // Keep enough spot measurements to cover 1 minute
    boost::circular_buffer<double> mAvgBandwidth {60 / PEER_AVG_BANDWIDTH_CALC_FREQUENCY_SECS};
    // Time we last took a spot measurement
    int64_t mLastSpotMeasurementTime { GetTimeMicros() };
    // Bytes received since last spot measurement
    uint64_t mBytesRecvThisSpot {0};

    // Sending rate limiting
    int64_t mSendRateLimit {DEFAULT_SEND_RATE_LIMIT};
    int64_t mSendStartTime { GetTimeMicros() };

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
        uint64_t sentSize {0};
    };

    // Move newly read completed messages to another queue
    void GetNewMsgs();

    // Message sending
    CSendResult SendMessage(CForwardAsyncReadonlyStream& data, uint64_t maxChunkSize);

};

using StreamPtr = std::shared_ptr<Stream>;
using StreamMap = std::map<StreamType, StreamPtr>;
