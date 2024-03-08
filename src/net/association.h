// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include <net/association_id.h>
#include <net/net_message.h>
#include <net/net_types.h>
#include <net/stream.h>
#include <net/stream_policy.h>
#include <streams.h>

#include <type_traits>

#include <boost/circular_buffer.hpp>

class AssociationStats;
class CConnman;
class CNode;
class Config; // NOLINT(cppcoreguidelines-virtual-class-destructor)
class CSerializedNetMsg;

/**
 * An association is a connection between 2 peers which may carry
 * multiple independent streams of data.
 */
class Association
{ 
  public:
    Association(const Association&) = delete;
    Association(Association&&) = delete;
    Association& operator=(const Association&) = delete;
    Association& operator=(Association&&) = delete;

    Association(CNode* node, SOCKET socket, const CAddress& peerAddr);
    ~Association();

    // Get peer address
    const CAddress& GetPeerAddr() const { return mPeerAddr; }

    // Get/Set peers local address
    CService GetPeerAddrLocal() const;
    void SetPeerAddrLocal(const CService& addrLocal);

    // Generate and set a new association ID
    template<typename IDType, typename... Args>
    void CreateAssociationID(Args&&... args)
    {
        LOCK(cs_mAssocID);
        mAssocID = std::make_shared<IDType>(std::forward<Args>(args)...);
    }

    // Get/Set association ID from peer
    AssociationIDPtr GetAssociationID() const;
    void SetAssociationID(AssociationIDPtr&& id);
    void ClearAssociationID();

    // Shutdown the connection
    void Shutdown();

    // Open any further required streams beyond the initial GENERAL stream
    void OpenRequiredStreams(CConnman& connman);

    // Move ownership of our stream to a different association
    void MoveStream(StreamType newType, Association& to);

    // Replace our active stream policy with a new one
    void ReplaceStreamPolicy(const StreamPolicyPtr& newPolicy);

    // Copy out current statistics
    void CopyStats(AssociationStats& stats) const;

    // Add our sockets to the sets for reading and writing
    bool SetSocketsForSelect(fd_set& setRecv, fd_set& setSend, fd_set& setError, SOCKET& socketMax) const;

    // Fetch the next message for processing
    std::pair<Stream::QueuedNetMessage, bool> GetNextMessage();

    // Service all sockets that are ready
    void ServiceSockets(fd_set& setRecv, fd_set& setSend, fd_set& setError, CConnman& connman,
                        const Config& config, bool& gotNewMsgs, uint64_t& bytesRecv, uint64_t& bytesSent);

    // Get current total send queue size
    uint64_t GetTotalSendQueueSize() const;
    // Get current total send queue estimated memory usage
    uint64_t GetTotalSendQueueMemoryUsage() const;

    // Update average bandwidth measurements
    void AvgBandwithCalc();

    // Get estimated average bandwidth from peer
    uint64_t GetAverageBandwidth() const;
    AverageBandwidth GetAverageBandwidth(const StreamType streamType) const;
    AverageBandwidth GetAverageBandwidth(const StreamPolicy::MessageType msgType) const;

    // Add new message to our list for sending
    uint64_t PushMessage(std::vector<uint8_t>&& serialisedHeader, CSerializedNetMsg&& msg, StreamType streamType);

    // Get last send/receive time for any stream
    int64_t GetLastSendTime() const;
    int64_t GetLastRecvTime() const;

    // Get whether we are paused for receiving (ANY stream or ALL streams)
    enum PausedFor { ANY, ALL };
    bool GetPausedForReceiving(PausedFor anyAll) const;

  private:

    // Node we are for
    CNode* mNode {nullptr};

    // ID possibly passed in from peer
    AssociationIDPtr mAssocID {nullptr};
    mutable CCriticalSection cs_mAssocID {};

    // Streams within the association
    StreamMap mStreams {};
    StreamPolicyPtr mStreamPolicy { std::make_shared<DefaultStreamPolicy>() };
    bool mShutdown {false};
    mutable CCriticalSection cs_mStreams {};

    // The address of the remote peer
    const CAddress mPeerAddr {};
    CService mPeerAddrLocal {};
    mutable CCriticalSection cs_mPeerAddr {};


    // Helper functions for running something over all streams that returns a result
    template <typename Callable,
              std::enable_if_t<!std::is_void_v<std::invoke_result_t<Callable, const StreamPtr&>>, int> = 0>
    // NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward)
    std::vector<std::invoke_result_t<Callable, StreamPtr&>> ForEachStream(Callable&& func) const
    {
        std::vector<std::invoke_result_t<Callable, const StreamPtr&>> res {};

        LOCK(cs_mStreams);
        for(const auto& stream : mStreams)
        {
            res.push_back(func(stream.second));
        }

        return res;
    }

    // Helper functions for running something over all streams that returns void
    template <typename Callable,
              std::enable_if_t<std::is_void_v<std::invoke_result_t<Callable, const StreamPtr&>>, int> = 0>
    // NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward)
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
              std::enable_if_t<std::is_void_v<std::invoke_result_t<Callable, StreamPtr&>>, int> = 0>
    // NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward)
    void ForEachStream(Callable&& func)
    {
        LOCK(cs_mStreams);
        for(auto& stream : mStreams)
        {
            func(stream.second);
        }
    }

};
