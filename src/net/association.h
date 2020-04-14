// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include <net/net_message.h>
#include <net/net_types.h>
#include <net/stream.h>
#include <streams.h>

#include <type_traits>

#include <boost/circular_buffer.hpp>

class CAssociationStats;
class CConnman;
class CNode;
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
    void CopyStats(CAssociationStats& stats) const;

    // Add our sockets to the sets for reading and writing
    bool SetSocketsForSelect(fd_set& setRecv, fd_set& setSend, fd_set& setError,
                             SOCKET& socketMax, bool pauseRecv) const;

    // Move newly read completed messages to the caller's queue
    size_t GetNewMsgs(std::list<CNetMessage>& msgList);

    // Service all sockets that are ready
    void ServiceSockets(fd_set& setRecv, fd_set& setSend, fd_set& setError, CConnman& connman,
                        const Config& config, bool& gotNewMsgs, size_t& bytesRecv, size_t& bytesSent);

    // Get current total send queue size
    size_t GetTotalSendQueueSize() const;

    // Update average bandwidth measurements
    void AvgBandwithCalc();

    // Get estimated average bandwidth from peer
    uint64_t GetAverageBandwidth() const;
    AverageBandwidth GetAverageBandwidth(const StreamType streamType) const;

    // Add new message to our list for sending
    size_t PushMessage(std::vector<uint8_t>&& serialisedHeader, CSerializedNetMsg&& msg);

    // Get last send/receive time for any stream
    int64_t GetLastSendTime() const;
    int64_t GetLastRecvTime() const;

  private:

    // Node we are for
    CNode& mNode;

    // Streams within the association
    using StreamMap = std::map<StreamType, CStreamPtr>;
    StreamMap mStreams {};
    bool mShutdown {false};
    mutable CCriticalSection cs_mStreams {};

    // Track bytes sent/received per command
    mapMsgCmdSize mSendBytesPerMsgCmd {};
    mapMsgCmdSize mRecvBytesPerMsgCmd {};
    mutable CCriticalSection cs_mSendRecvBytes {};

    // The address of the remote peer
    const CAddress mPeerAddr {};
    CService mPeerAddrLocal {};
    mutable CCriticalSection cs_mPeerAddr {};

    // Helper functions for running something over all streams that returns a result
    template <typename Callable,
              std::enable_if_t<!std::is_void<typename std::result_of<Callable(const CStreamPtr&)>::type>::value, int> = 0>
    std::vector<typename std::result_of<Callable(const CStreamPtr&)>::type> ForEachStream(Callable&& func) const
    {
        std::vector<typename std::result_of<Callable(const CStreamPtr&)>::type> res {};

        LOCK(cs_mStreams);
        for(const auto& stream : mStreams)
        {
            res.push_back(func(stream.second));
        }

        return res;
    }

    // Helper functions for running something over all streams that returns void
    template <typename Callable,
              std::enable_if_t<std::is_void<typename std::result_of<Callable(const CStreamPtr&)>::type>::value, int> = 0>
    void ForEachStream(Callable&& func) const
    {
        LOCK(cs_mStreams);
        for(const auto& stream : mStreams)
        {
            func(stream.second);
        }
    }
    // Non-const version
    template <typename Callable,
              std::enable_if_t<std::is_void<typename std::result_of<Callable(CStreamPtr&)>::type>::value, int> = 0>
    void ForEachStream(Callable&& func)
    {
        LOCK(cs_mStreams);
        for(auto& stream : mStreams)
        {
            func(stream.second);
        }
    }

};
