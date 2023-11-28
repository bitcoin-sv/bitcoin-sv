// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2019-2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#ifndef __cplusplus
#error This header can only be compiled as C++.
#endif

#ifndef BITCOIN_PROTOCOL_H
#define BITCOIN_PROTOCOL_H

#include "consensus/consensus.h"
#include "net/netaddress.h"
#include "net/net_types.h"
#include "serialize.h"
#include "uint256.h"
#include "version.h"

#include <array>
#include <cstdint>
#include <string>

class Config; // NOLINT(cppcoreguidelines-virtual-class-destructor)
class msg_buffer;
class CSerializedNetMsg;

/**
 * Default maximum length of incoming protocol messages set to 2MiB.
 * It is used if 'maxprotocolrecvpayloadlength' parameter is not provided.
 * NB: Messages propagating block content are not subject to this limit.
 */
static const unsigned int DEFAULT_MAX_PROTOCOL_RECV_PAYLOAD_LENGTH = 2 * 1024 * 1024;

/**
 * By default, size of messages to other peers are limited by this default value.
 * This limit is raised if a Protoconf message is received from a peer.
 * Default value is required for compatibility with older versions that do not support Protoconf message.
**/
static const unsigned int LEGACY_MAX_PROTOCOL_PAYLOAD_LENGTH = 1 * 1024 * 1024;

/**
 * Maximal protocol recv payload length allowed to set by 'maxprotocolrecvpayloadlength' parameter
 */
static const uint64_t MAX_PROTOCOL_RECV_PAYLOAD_LENGTH = ONE_GIGABYTE;

/**
 * We limit maximum size of message that can be send to peer to be MAX_PROTOCOL_SEND_PAYLOAD_FACTOR times
 * the size of the maximum size of message that we can recieve (DEFAULT_MAX_PROTOCOL_RECV_PAYLOAD_LENGTH or set by maxprotocolrecvpayloadlength parameter).
**/
static const unsigned int MAX_PROTOCOL_SEND_PAYLOAD_FACTOR = 4;

/**
 * Maximum number of received full size inventory messages to be queued at once.
 * Maximum size of received inventory messages is set by 'maxprotocolrecvpayloadlength' parameter
 */
static const unsigned int DEFAULT_RECV_INV_QUEUE_FACTOR = 3;

/**
 * Maximal and minimal factors of full size inventory messages allowed to be stored.
 */
static const unsigned int MAX_RECV_INV_QUEUE_FACTOR = 100;
static const unsigned int MIN_RECV_INV_QUEUE_FACTOR = 1;

/**
 * Message header enumerations.
 */
struct CMessageFields
{
    enum 
    {
        MESSAGE_START_SIZE = 4,
        COMMAND_SIZE = 12,
        CHECKSUM_SIZE = 4,

        BASIC_MESSAGE_SIZE_SIZE = 4,
        BASIC_MESSAGE_SIZE_OFFSET = MESSAGE_START_SIZE + COMMAND_SIZE,
        CHECKSUM_OFFSET = BASIC_MESSAGE_SIZE_OFFSET + BASIC_MESSAGE_SIZE_SIZE,
        BASIC_COMMAND_OFFSET = MESSAGE_START_SIZE,

        EXTENDED_MESSAGE_SIZE_SIZE = 8,

        BASIC_HEADER_SIZE = MESSAGE_START_SIZE + COMMAND_SIZE + BASIC_MESSAGE_SIZE_SIZE + CHECKSUM_SIZE,
        EXTENDED_HEADER_SIZE = MESSAGE_START_SIZE + COMMAND_SIZE + BASIC_MESSAGE_SIZE_SIZE + CHECKSUM_SIZE + COMMAND_SIZE + EXTENDED_MESSAGE_SIZE_SIZE
    };
};

/**
 * Extended message header.
 * (12) extended command.
 * (8) extended size
 */
class CExtendedMessageHeader
{
  public:
    CExtendedMessageHeader() = default;
    CExtendedMessageHeader(const char* pszCommand, uint64_t nPayloadLengthIn);

    ADD_SERIALIZE_METHODS
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(FLATDATA(pchCommand));
        READWRITE(nPayloadLength);
    }

    std::string GetCommand() const;
    uint64_t GetPayloadLength() const { return nPayloadLength; }
    bool IsValid(const Config& config) const;
    bool IsOversized(const Config& config) const;

    // For unit testing
    template<typename T> struct UnitTestAccess;

  private:

    std::array<char, CMessageFields::COMMAND_SIZE> pchCommand {};   // 0 initialised
    uint64_t nPayloadLength { std::numeric_limits<uint64_t>::max() };
};

/**
 * Message header.
 * (4) message start.
 * (12) command.
 * (4) size.
 * (4) checksum.
 *
 * [(12) extended command] - Only in extended header.
 * [(8) extended size] - Only in extended header.
 */
class CMessageHeader {
public:
    using MessageMagic = std::array<uint8_t, CMessageFields::MESSAGE_START_SIZE>;
    using Checksum = std::array<uint8_t, CMessageFields::CHECKSUM_SIZE>;

    CMessageHeader(const MessageMagic& pchMessageStartIn);
    CMessageHeader(const Config& config, const CSerializedNetMsg& msg);

    uint64_t Read(const char* pch, uint64_t numBytes, msg_buffer&);

    std::string GetCommand() const;
    const MessageMagic& GetMsgStart() const { return pchMessageStart; }
    const Checksum& GetChecksum() const { return pchChecksum; }
    uint64_t GetLength() const;
    uint64_t GetPayloadLength() const;
    bool IsExtended() const { return extendedFields.has_value(); }
    bool Complete() const { return complete; }
    bool IsValid(const Config& config) const;
    bool IsOversized(const Config& config) const;

    static uint64_t GetHeaderSizeForPayload(uint64_t payloadSize);
    static uint64_t GetMaxPayloadLength(int version);
    static bool IsExtended(uint64_t payloadSize);

    ADD_SERIALIZE_METHODS
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(FLATDATA(pchMessageStart));
        READWRITE(FLATDATA(pchCommand));
        READWRITE(nPayloadLength);
        READWRITE(FLATDATA(pchChecksum));

        if(IsExtended())
        {
            READWRITE(*extendedFields);
        }
    }

    // For unit testing
    template<typename T> struct UnitTestAccess;

private:

    // Private constructor for internal delegating and unit testing
    CMessageHeader(const MessageMagic& pchMessageStartIn,
                   const char* pszCommand, uint64_t nPayloadLengthIn,
                   const uint256& payloadHash);

    // Validity checking helper
    bool CheckHeaderMagicAndCommand(const MessageMagic& magic) const;

    MessageMagic pchMessageStart {};
    std::array<char, CMessageFields::COMMAND_SIZE> pchCommand {};   // 0 initialised
    uint32_t nPayloadLength { std::numeric_limits<uint32_t>::max() };
    Checksum pchChecksum {};

    std::optional<CExtendedMessageHeader> extendedFields { std::nullopt };

    bool complete {false};
};

bool operator==(const CMessageHeader&, const CMessageHeader&);

/**
 * Bitcoin protocol message types. When adding new message types, don't forget
 * to update allNetMessageTypes in protocol.cpp.
 */
namespace NetMsgType {

/**
 * The version message provides information about the transmitting node to the
 * receiving node at the beginning of a connection.
 * @see https://bitcoin.org/en/developer-reference#version
 */
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
extern const char *VERSION;
/**
 * The verack message acknowledges a previously-received version message,
 * informing the connecting node that it can begin to send other messages.
 * @see https://bitcoin.org/en/developer-reference#verack
 */
extern const char *VERACK;
/**
 * The addr (IP address) message relays connection information for peers on the
 * network.
 * @see https://bitcoin.org/en/developer-reference#addr
 */
extern const char *ADDR;
/**
 * The inv message (inventory message) transmits one or more inventories of
 * objects known to the transmitting peer.
 * @see https://bitcoin.org/en/developer-reference#inv
 */
extern const char *INV;
/**
 * The getdata message requests one or more data objects from another node.
 * @see https://bitcoin.org/en/developer-reference#getdata
 */
extern const char *GETDATA;
/**
 * The merkleblock message is a reply to a getdata message which requested a
 * block using the inventory type MSG_MERKLEBLOCK.
 * @since protocol version 70001 as described by BIP37.
 * @see https://bitcoin.org/en/developer-reference#merkleblock
 */
extern const char *MERKLEBLOCK;
/**
 * The getblocks message requests an inv message that provides block header
 * hashes starting from a particular point in the block chain.
 * @see https://bitcoin.org/en/developer-reference#getblocks
 */
extern const char *GETBLOCKS;
/**
 * The getheaders message requests a headers message that provides block
 * headers starting from a particular point in the block chain.
 * @since protocol version 31800.
 * @see https://bitcoin.org/en/developer-reference#getheaders
 */
extern const char *GETHEADERS;
/**
 * The gethdrsen message requests a headers message that provides block
 * headers starting from a particular point in the block chain.  The 
 * gethdrsen message is an upgrade of getheaders message that also
 * sends number of transaction, coinbase transaction and merkleproof
 * for coinbase transaction
 */
extern const char* GETHDRSEN;
/**
 * The tx message transmits a single transaction.
 * @see https://bitcoin.org/en/developer-reference#tx
 */
extern const char *TX;
/**
 * The headers message sends one or more block headers to a node which
 * previously requested certain headers with a getheaders message.
 * @since protocol version 31800.
 * @see https://bitcoin.org/en/developer-reference#headers
 */
extern const char *HEADERS;
/**
 * The hdrsen message sends one or more block headers to a node which
 * previously requested certain headers with a gethdrsen message.
 */
extern const char* HDRSEN;
/**
 * The block message transmits a single serialized block.
 * @see https://bitcoin.org/en/developer-reference#block
 */
extern const char *BLOCK;
/**
 * The getaddr message requests an addr message from the receiving node,
 * preferably one with lots of IP addresses of other receiving nodes.
 * @see https://bitcoin.org/en/developer-reference#getaddr
 */
extern const char *GETADDR;
/**
 * The mempool message requests the TXIDs of transactions that the receiving
 * node has verified as valid but which have not yet appeared in a block.
 * @since protocol version 60002.
 * @see https://bitcoin.org/en/developer-reference#mempool
 */
extern const char *MEMPOOL;
/**
 * The ping message is sent periodically to help confirm that the receiving
 * peer is still connected.
 * @see https://bitcoin.org/en/developer-reference#ping
 */
extern const char *PING;
/**
 * The pong message replies to a ping message, proving to the pinging node that
 * the ponging node is still alive.
 * @since protocol version 60001 as described by BIP31.
 * @see https://bitcoin.org/en/developer-reference#pong
 */
extern const char *PONG;
/**
 * The notfound message is a reply to a getdata message which requested an
 * object the receiving node does not have available for relay.
 * @ince protocol version 70001.
 * @see https://bitcoin.org/en/developer-reference#notfound
 */
extern const char *NOTFOUND;
/**
 * The filterload message tells the receiving peer to filter all relayed
 * transactions and requested merkle blocks through the provided filter.
 * @since protocol version 70001 as described by BIP37.
 *   Only available with service bit NODE_BLOOM since protocol version
 *   70011 as described by BIP111.
 * @see https://bitcoin.org/en/developer-reference#filterload
 */
extern const char *FILTERLOAD;
/**
 * The filteradd message tells the receiving peer to add a single element to a
 * previously-set bloom filter, such as a new public key.
 * @since protocol version 70001 as described by BIP37.
 *   Only available with service bit NODE_BLOOM since protocol version
 *   70011 as described by BIP111.
 * @see https://bitcoin.org/en/developer-reference#filteradd
 */
extern const char *FILTERADD;
/**
 * The filterclear message tells the receiving peer to remove a previously-set
 * bloom filter.
 * @since protocol version 70001 as described by BIP37.
 *   Only available with service bit NODE_BLOOM since protocol version
 *   70011 as described by BIP111.
 * @see https://bitcoin.org/en/developer-reference#filterclear
 */
extern const char *FILTERCLEAR;
/**
 * The reject message informs the receiving node that one of its previous
 * messages has been rejected.
 * @since protocol version 70002 as described by BIP61.
 * @see https://bitcoin.org/en/developer-reference#reject
 */
extern const char *REJECT;
/**
 * Indicates that a node prefers to receive new block announcements via a
 * "headers" message rather than an "inv".
 * @since protocol version 70012 as described by BIP130.
 * @see https://bitcoin.org/en/developer-reference#sendheaders
 */
extern const char *SENDHEADERS;
/**
 * Same as "sendheaders" except that new new blocks are announced via
 * "hdrsen" message.
 */
extern const char *SENDHDRSEN;
/**
 * The feefilter message tells the receiving peer not to inv us any txs
 * which do not meet the specified min fee rate.
 * @since protocol version 70013 as described by BIP133
 */
extern const char *FEEFILTER;
/**
 * Contains a 1-byte bool and 8-byte LE version number.
 * Indicates that a node is willing to provide blocks via "cmpctblock" messages.
 * May indicate that a node prefers to receive new block announcements via a
 * "cmpctblock" message rather than an "inv", depending on message contents.
 * @since protocol version 70014 as described by BIP 152
 */
extern const char *SENDCMPCT;
/**
 * Contains a CBlockHeaderAndShortTxIDs object - providing a header and
 * list of "short txids".
 * @since protocol version 70014 as described by BIP 152
 */
extern const char *CMPCTBLOCK;
/**
 * Contains a BlockTransactionsRequest
 * Peer should respond with "blocktxn" message.
 * @since protocol version 70014 as described by BIP 152
 */
extern const char *GETBLOCKTXN;
/**
 * Contains a BlockTransactions.
 * Sent in response to a "getblocktxn" message.
 * @since protocol version 70014 as described by BIP 152
 */
extern const char *BLOCKTXN;
/**
 * Contains a CProtoconf.
 * Sent right after VERACK message, regardless of remote peer's protocol version
 */
extern const char *PROTOCONF;
/**
 * The createstream message is for setting up a new stream within an existing
 * association.
 */
extern const char *CREATESTREAM;
/**
 * The streamack message is an acknowledgement that a previously requested
 * attempt to setup a new stream has been successful.
 */
extern const char *STREAMACK;
/**
 * The dsdetected message is a notification that a block (or blocks) have
 * been observed which contains an attempt to double-spend some UTXOs.
 */
extern const char *DSDETECTED;
/**
 * Contains an extended message (one which may exceed 4GB in size).
 */
extern const char *EXTMSG;
/**
 * The revokemid message is an early notification that the contained miner ID
 * should be revoked.
 */
extern const char *REVOKEMID;
/**
 * The authch net message is for delivering the challenge message to the other party.
 */
extern const char *AUTHCH;
/**
 * The authresp net message delivers the response message to the requestor.
 */
extern const char *AUTHRESP;
/**
 * Contains a dataref transaction.
 */
extern const char *DATAREFTX;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

/**
 * Indicate if the message is used to transmit the content of a block.
 * These messages can be significantly larger than usual messages and therefore
 * may need to be processed differently.
 */
bool IsBlockLike(const std::string &strCommand);

/**
 * Return the maximum message size for the given message type.
 */
uint64_t GetMaxMessageLength(const std::string& command, const Config& config);

} // namespace NetMsgType

/* Get a vector of all valid message types (see above) */
const std::vector<std::string> &getAllNetMessageTypes();

/**
 * nServices flags.
 */
enum ServiceFlags : uint64_t {
    // Nothing
    NODE_NONE = 0,
    // NODE_NETWORK means that the node is capable of serving the block chain.
    // It is currently set by all Bitcoin SV nodes, and is unset by SPV clients
    // or other peers that just want network services but don't provide them.
    NODE_NETWORK = (1 << 0),
    // NODE_GETUTXO means the node is capable of responding to the getutxo
    // protocol request. Bitcoin SV does not support this but a patch set
    // called Bitcoin XT does. See BIP 64 for details on how this is
    // implemented.
    NODE_GETUTXO = (1 << 1),
    // NODE_BLOOM means the node is capable and willing to handle bloom-filtered
    // connections. Bitcoin SV nodes used to support this by default, without
    // advertising this bit, but no longer do as of protocol version 70011 (=
    // NO_BLOOM_VERSION)
    NODE_BLOOM = (1 << 2),
    // NODE_XTHIN means the node supports Xtreme Thinblocks. If this is turned
    // off then the node will not service nor make xthin requests.
    NODE_XTHIN = (1 << 4),
    // NODE_BITCOIN_CASH means the node supports Bitcoin Cash and the
    // associated consensus rule changes.
    // This service bit is intended to be used prior until some time after the
    // UAHF activation when the Bitcoin Cash network has adequately separated.
    // TODO: remove (free up) the NODE_BITCOIN_CASH service bit once no longer
    // needed.
    NODE_BITCOIN_CASH = (1 << 5),

    // Bits 24-31 are reserved for temporary experiments. Just pick a bit that
    // isn't getting used, or one not being used much, and notify the
    // bitcoin-development mailing list. Remember that service bits are just
    // unauthenticated advertisements, so your code must be robust against
    // collisions and other cases where nodes may be advertising a service they
    // do not actually support. Other service bits should be allocated via the
    // BIP process.
};

/**
 * A CService with information about it as peer.
 */
class CAddress : public CService {
public:
    CAddress() = default;
    explicit CAddress(CService ipIn, ServiceFlags nServicesIn)
        : CService{ipIn}, nServices{nServicesIn}
    {}

    ADD_SERIALIZE_METHODS

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        int nVersion = s.GetVersion();
        if (s.GetType() & SER_DISK) READWRITE(nVersion);
        if ((s.GetType() & SER_DISK) ||
            (nVersion >= CADDR_TIME_VERSION && !(s.GetType() & SER_GETHASH)))
            READWRITE(nTime);
        uint64_t nServicesInt = nServices;
        READWRITE(nServicesInt);
        nServices = (ServiceFlags)nServicesInt;
        READWRITE(*(CService *)this);
    }

    // TODO: make private (improves encapsulation)
public:
    ServiceFlags nServices{NODE_NONE};

    // disk and network only
    unsigned int nTime{100000000};
};

/** getdata message type flags */
const uint32_t MSG_TYPE_MASK = 0xffffffff >> 3;

/** getdata / inv message types.
 * These numbers are defined by the protocol. When adding a new value, be sure
 * to mention it in the respective BIP.
 */
enum GetDataMsg {
    UNDEFINED = 0,
    MSG_TX = 1,
    MSG_BLOCK = 2,
    // The following can only occur in getdata. Invs always use TX or BLOCK.
    //!< Defined in BIP37
    MSG_FILTERED_BLOCK = 3,
    //!< Defined in BIP152
    MSG_CMPCT_BLOCK = 4,
    MSG_DATAREF_TX = 5
};

/** inv message data */
class CInv {
public:
    // TODO: make private (improves encapsulation)
    uint32_t type;
    uint256 hash;

public:
    CInv() : type(0), hash() {}
    CInv(uint32_t typeIn, const uint256 &hashIn) : type(typeIn), hash(hashIn) {}

    ADD_SERIALIZE_METHODS

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        READWRITE(type);
        READWRITE(hash);
    }

    friend bool operator<(const CInv &a, const CInv &b) {
        return a.type < b.type || (a.type == b.type && a.hash < b.hash);
    }

	friend bool operator==(const CInv &a, const CInv &b) {
        return a.type == b.type && a.hash == b.hash;
    }

    std::string GetCommand() const;
    std::string ToString() const;

    uint32_t GetKind() const { return type & MSG_TYPE_MASK; }

    bool IsTx() const {
        auto k = GetKind();
        return k == MSG_TX;
    }

    bool IsSomeBlock() const {
        auto k = GetKind();
        return k == MSG_BLOCK || k == MSG_FILTERED_BLOCK ||
               k == MSG_CMPCT_BLOCK;
    }

    /* Estimate the maximum number of INV elements that will fit in given payload.
     * The result is pessimistic, because we assume that 8 bytes are required to encode number
     * of elements, which is only true for very large numbers.
     * @param maxPayloadLength : maximal size of INV message *payload* (without header) that a peer can receive (in bytes)
     * @ return : number of elements in INV message that corresponds to maxPayloadLength 

    **/
    static constexpr uint32_t estimateMaxInvElements(unsigned int maxPayloadLength) {

        return (maxPayloadLength - 8 /* number of elements */) / (4 /* type */ + 32 /* hash size */);
    } 

};

/** protoconf message data **/
class CProtoconf {
    // Maximum number of named stream policies
    static constexpr size_t MAX_NUM_STREAM_POLICIES {10};

public:
    /** numberOfFields is set to 2, increment if new properties are added **/
    uint64_t numberOfFields {2};
    uint32_t maxRecvPayloadLength {0};
    std::string streamPolicies {};
public:
    CProtoconf() = default;
    CProtoconf(unsigned int maxRecvPayloadLengthIn, const std::string& streamPoliciesIn)
    // NOLINTNEXTLINE(cppcoreguidelines-use-default-member-init)
    : numberOfFields{2}, maxRecvPayloadLength{maxRecvPayloadLengthIn}, streamPolicies{streamPoliciesIn}
    {}

    ADD_SERIALIZE_METHODS
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        READWRITECOMPACTSIZE(numberOfFields);
        if (numberOfFields > 0) {
            READWRITE(maxRecvPayloadLength);
            if(numberOfFields > 1) {
                READWRITE(LIMITED_STRING(streamPolicies, (MAX_STREAM_POLICY_NAME_LENGTH + 1) * MAX_NUM_STREAM_POLICIES));
            }
        } else {
            throw std::ios_base::failure("Invalid deserialization. Number of fields specified in protoconf is equal to 0.");
        }
        
    }
};

#endif // BITCOIN_PROTOCOL_H
