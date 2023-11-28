// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include <net/association_id.h>
#include <net/net_message.h>
#include <net/stream.h>

#include <list>
#include <memory>
#include <mutex>
#include <string>

class CConnman;
class Config; // NOLINT(cppcoreguidelines-virtual-class-destructor)

/**
 * A stream policy defines how a collection of streams to a peer are utilised.
 * For example; What streams are established? Which stream is used to send
 * particular message types? What order are received messages processed in?
 */
// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions)
class StreamPolicy
{
  public:

    // Enumerate high level message categories.
    // If you extend the number of high level categories, don't forget to also
    // update the implementations of GetStreamType()
    enum class MessageType { BLOCK, PING, OTHER };

    virtual ~StreamPolicy() = default;

    // Return the policy name
    virtual const std::string GetPolicyName() const = 0;

    // Create the required streams for this policy
    virtual void SetupStreams(CConnman& connman, const CAddress& peerAddr,
                              const AssociationIDPtr& assocID) = 0;

    // Fetch the next message for processing
    virtual std::pair<Stream::QueuedNetMessage, bool> GetNextMessage(StreamMap& streams) = 0;

    // Service the sockets of the streams
    virtual void ServiceSockets(StreamMap& streams, fd_set& setRecv, fd_set& setSend,
                                fd_set& setError, const Config& config, bool& gotNewMsgs,
                                uint64_t& bytesRecv, uint64_t& bytesSent) = 0;

    // Queue an outgoing message on the appropriate stream
    virtual uint64_t PushMessage(StreamMap& streams, StreamType streamType,
                                 std::vector<uint8_t>&& serialisedHeader, CSerializedNetMsg&& msg,
                                 uint64_t nPayloadLength, uint64_t nTotalSize) = 0;

    // Get the stream type the given message category is sent over
    virtual StreamType GetStreamTypeForMessage(MessageType msgType) const = 0;
};
using StreamPolicyPtr = std::shared_ptr<StreamPolicy>;


/**
 * Implement standard basic stream policy functions.
 */
class BasicStreamPolicy : public StreamPolicy
{
  public:
    BasicStreamPolicy() = default;

    // Service the sockets of the streams
    void ServiceSockets(StreamMap& streams, fd_set& setRecv, fd_set& setSend,
                        fd_set& setError, const Config& config, bool& gotNewMsgs,
                        uint64_t& bytesRecv, uint64_t& bytesSent) override;

  protected:

    // Common PushMessage functionality
    uint64_t PushMessageCommon(StreamMap& streams, StreamType streamType, bool exactMatch,
                               std::vector<uint8_t>&& serialisedHeader, CSerializedNetMsg&& msg,
                               uint64_t nPayloadLength, uint64_t nTotalSize);
};


/**
 * The default stream policy.
 *
 * Used when no other better policy has been configured.
 *
 * Requires no additional streams beyond the always available GENERAL stream,
 * and gives equal priority to all traffic. I.e; this policy behaves just like
 * the old single stream P2P model.
 */
class DefaultStreamPolicy : public BasicStreamPolicy
{
  public:
    DefaultStreamPolicy() = default;

    // Our name for registering with the factory
    static constexpr const char* POLICY_NAME { "Default" };

    // Return the policy name
    const std::string GetPolicyName() const override { return POLICY_NAME; }

    // Create the required streams for this policy
    void SetupStreams(CConnman& connman, const CAddress& peerAddr,
                      const AssociationIDPtr& assocID) override
    {}

    // Fetch the next message for processing
    std::pair<Stream::QueuedNetMessage, bool> GetNextMessage(StreamMap& streams) override;

    // Queue an outgoing message on the appropriate stream
    uint64_t PushMessage(StreamMap& streams, StreamType streamType,
                         std::vector<uint8_t>&& serialisedHeader, CSerializedNetMsg&& msg,
                         uint64_t nPayloadLength, uint64_t nTotalSize) override;

    // Get the stream type the given message category is sent over
    StreamType GetStreamTypeForMessage(MessageType msgType) const override { return StreamType::GENERAL; }
};


/**
 * A block priority stream policy.
 *
 * This policy tries to prioritise block and control messages that keep the
 * connection alive (pings/pongs).
 *
 * In addition to the GENERAL stream it creates a DATA1 stream.
 *
 * The DATA1 stream is for high priority traffic and carries block messages
 * plus pings and pongs. All other messages are lower priority and sent over
 * the GENERAL stream.
 *
 * Gives equal priority to all stream sockets for reading and writing.
 */
class BlockPriorityStreamPolicy : public BasicStreamPolicy
{
  public:
    BlockPriorityStreamPolicy() = default;

    // Our name for registering with the factory
    static constexpr const char* POLICY_NAME { "BlockPriority" };

    // Return the policy name
    const std::string GetPolicyName() const override { return POLICY_NAME; }

    // Create the required streams for this policy
    void SetupStreams(CConnman& connman, const CAddress& peerAddr,
                      const AssociationIDPtr& assocID) override;

    // Fetch the next message for processing
    std::pair<Stream::QueuedNetMessage, bool> GetNextMessage(StreamMap& streams) override;

    // Queue an outgoing message on the appropriate stream
    uint64_t PushMessage(StreamMap& streams, StreamType streamType,
                         std::vector<uint8_t>&& serialisedHeader, CSerializedNetMsg&& msg,
                         uint64_t nPayloadLength, uint64_t nTotalSize) override;

    // Get the stream type the given message category is sent over
    StreamType GetStreamTypeForMessage(MessageType msgType) const override;
};

