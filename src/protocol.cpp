// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2019-2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "protocol.h"
#include "config.h"
#include "util.h"
#include "utilstrencodings.h"

#ifndef WIN32
#include <arpa/inet.h>
#endif

namespace NetMsgType {
const char *VERSION = "version";
const char *VERACK = "verack";
const char *ADDR = "addr";
const char *INV = "inv";
const char *GETDATA = "getdata";
const char *MERKLEBLOCK = "merkleblock";
const char *GETBLOCKS = "getblocks";
const char *GETHEADERS = "getheaders";
const char* GETHDRSEN = "gethdrsen";
const char *TX = "tx";
const char *HEADERS = "headers";
const char* HDRSEN = "hdrsen";
const char *BLOCK = "block";
const char *GETADDR = "getaddr";
const char *MEMPOOL = "mempool";
const char *PING = "ping";
const char *PONG = "pong";
const char *NOTFOUND = "notfound";
const char *FILTERLOAD = "filterload";
const char *FILTERADD = "filteradd";
const char *FILTERCLEAR = "filterclear";
const char *REJECT = "reject";
const char *SENDHEADERS = "sendheaders";
const char *SENDHDRSEN = "sendhdrsen";
const char *FEEFILTER = "feefilter";
const char *SENDCMPCT = "sendcmpct";
const char *CMPCTBLOCK = "cmpctblock";
const char *GETBLOCKTXN = "getblocktxn";
const char *BLOCKTXN = "blocktxn";
const char *PROTOCONF = "protoconf";
const char *CREATESTREAM = "createstrm";
const char *STREAMACK = "streamack";
const char *DSDETECTED = "dsdetected";
const char *EXTMSG = "extmsg";
const char *REVOKEMID = "revokemid";
const char *AUTHCH = "authch";
const char *AUTHRESP = "authresp";
const char *DATAREFTX = "datareftx";

bool IsBlockLike(const std::string &strCommand) {
    return strCommand == NetMsgType::BLOCK ||
           strCommand == NetMsgType::CMPCTBLOCK ||
           strCommand == NetMsgType::BLOCKTXN ||
           strCommand == NetMsgType::HDRSEN; // We treat this message as block like because we don't want the
                                             // message to be bigger than max block size we are willing to accept
}

uint64_t GetMaxMessageLength(const std::string& command, const Config& config)
{
    if (command == NetMsgType::PROTOCONF)
    {
        // If the message is PROTOCONF, it is limited to LEGACY_MAX_PROTOCOL_PAYLOAD_LENGTH.
        return LEGACY_MAX_PROTOCOL_PAYLOAD_LENGTH;
    }
    else if (command == NetMsgType::TX || command == NetMsgType::DATAREFTX)
    {
        // If the message is TX, it is limited to max consensus tx size after Genesis
        // can not use policy limit because of banning rules.
        return config.GetMaxTxSize(true, true);
    }
    else if (command == NetMsgType::GETBLOCKTXN)
    {
        // Minimum realistic transaction size in bytes
        static constexpr unsigned MIN_TX_SIZE { 215 };
        // Short TXID size in bytes
        static constexpr unsigned SHORT_TXID_SIZE { 6 };

        // If the message is GETBLOCKTXN, it is limited to an estimate of the maximum number of
        // short TXIDs the message could contain.
        uint64_t maxPayload { config.GetMaxBlockSize() / MIN_TX_SIZE * SHORT_TXID_SIZE };
        return maxPayload + CMessageHeader::GetHeaderSizeForPayload(maxPayload);
    }
    else if (!NetMsgType::IsBlockLike(command))
    {
        // If the message doesn't not contain a block content,
        // it is limited to MAX_PROTOCOL_RECV_PAYLOAD_LENGTH.
        return config.GetMaxProtocolRecvPayloadLength();
    }
    else
    {
        // Maximum accepted block type message size
        return config.GetMaxBlockSize();
    }
}

} // namespace NetMsgType

/**
 * All known message types. Keep this in the same order as the list of messages
 * above and in protocol.h.
 */
static const std::string allNetMessageTypes[] = {
    NetMsgType::VERSION,      NetMsgType::VERACK,     NetMsgType::ADDR,
    NetMsgType::INV,          NetMsgType::GETDATA,    NetMsgType::MERKLEBLOCK,
    NetMsgType::GETBLOCKS,    NetMsgType::GETHEADERS, NetMsgType::GETHDRSEN,   NetMsgType::TX,
    NetMsgType::HEADERS,      NetMsgType::HDRSEN,     NetMsgType::BLOCK,       NetMsgType::GETADDR,
    NetMsgType::MEMPOOL,      NetMsgType::PING,       NetMsgType::PONG,
    NetMsgType::NOTFOUND,     NetMsgType::FILTERLOAD, NetMsgType::FILTERADD,
    NetMsgType::FILTERCLEAR,  NetMsgType::REJECT,     NetMsgType::SENDHEADERS, NetMsgType::SENDHDRSEN,
    NetMsgType::FEEFILTER,    NetMsgType::SENDCMPCT,  NetMsgType::CMPCTBLOCK,
    NetMsgType::GETBLOCKTXN,  NetMsgType::BLOCKTXN,   NetMsgType::PROTOCONF,
    NetMsgType::CREATESTREAM, NetMsgType::STREAMACK,  NetMsgType::DSDETECTED,
    NetMsgType::EXTMSG,       NetMsgType::AUTHCH,     NetMsgType::AUTHRESP,
    NetMsgType::DATAREFTX
};
static const std::vector<std::string>
    allNetMessageTypesVec(allNetMessageTypes,
                          allNetMessageTypes + ARRAYLEN(allNetMessageTypes));

// Check a command string for errors
static bool CheckCommandFormat(const char* cmd)
{
    for(const char* p1 = cmd; p1 < cmd + CMessageFields::COMMAND_SIZE; p1++)
    {
        if(*p1 == 0) {
            // Must be all zeros after the first zero
            if (!std::all_of(
                    p1 + 1,
                    cmd + CMessageFields::COMMAND_SIZE,
                    [](char i) { return i == 0; } ))
            {
                return false;
            }
            break;
        }
        else if(*p1 < ' ' || *p1 > 0x7E)
        {
            return false;
        }
    }

    return true;
}

CExtendedMessageHeader::CExtendedMessageHeader(const char* pszCommand, uint64_t nPayloadLengthIn)
: nPayloadLength { nPayloadLengthIn }
{
    GCC_WARNINGS_PUSH;
    #if defined __GNUC__ && (__GNUC__ >= 8)
        // -Wstringop-truncation was introduced in GCC 8
        GCC_WARNINGS_IGNORE(-Wstringop-truncation);
    #endif

    // length of pszCommand is always smaller than COMMAND_SIZE
    strncpy(pchCommand.data(), pszCommand, CMessageFields::COMMAND_SIZE);
    GCC_WARNINGS_POP;
}

std::string CExtendedMessageHeader::GetCommand() const
{
    return { pchCommand.data(), pchCommand.data() + strnlen(pchCommand.data(), CMessageFields::COMMAND_SIZE) };
}

bool CExtendedMessageHeader::IsValid(const Config& config) const
{
    // Check command format
    if(!CheckCommandFormat(pchCommand.data()))
    {
        return false;
    }

    // Message size
    if(IsOversized(config))
    {
        LogPrintf("CExtendedMessageHeader::IsValid(): (%s, %lu bytes) is oversized\n", GetCommand(), nPayloadLength);
        return false;
    }

    return true;
}

bool CExtendedMessageHeader::IsOversized(const Config& config) const
{
    return GetPayloadLength() > NetMsgType::GetMaxMessageLength(GetCommand(), config);
}

CMessageHeader::CMessageHeader(const MessageMagic& pchMessageStartIn)
{
    memcpy(pchMessageStart.data(), pchMessageStartIn.data(), CMessageFields::MESSAGE_START_SIZE);
    memset(pchChecksum.data(), 0, pchChecksum.size());
}

CMessageHeader::CMessageHeader(const Config& config, const CSerializedNetMsg& msg)
: CMessageHeader { config.GetChainParams().NetMagic(), msg.Command().c_str(), msg.Size(), msg.Hash() }
{
}

CMessageHeader::CMessageHeader(const MessageMagic& pchMessageStartIn,
                               const char* pszCommand,
                               uint64_t nPayloadLengthIn,
                               const uint256& payloadHash)
{
    memcpy(pchMessageStart.data(), pchMessageStartIn.data(), CMessageFields::MESSAGE_START_SIZE);

    GCC_WARNINGS_PUSH;
    #if defined __GNUC__ && (__GNUC__ >= 8)
        // -Wstringop-truncation was introduced in GCC 8
        GCC_WARNINGS_IGNORE(-Wstringop-truncation);
    #endif

    // Basic or extended header?
    if(nPayloadLengthIn > std::numeric_limits<uint32_t>::max())
    {
        // length of pszCommand is always smaller than COMMAND_SIZE
        strncpy(pchCommand.data(), NetMsgType::EXTMSG, CMessageFields::COMMAND_SIZE);
        nPayloadLength = std::numeric_limits<uint32_t>::max();
        memset(pchChecksum.data(), 0, pchChecksum.size());
        extendedFields = CExtendedMessageHeader { pszCommand, nPayloadLengthIn };
    }
    else
    {
        // length of pszCommand is always smaller than COMMAND_SIZE
        strncpy(pchCommand.data(), pszCommand, CMessageFields::COMMAND_SIZE);
        nPayloadLength = static_cast<uint32_t>(nPayloadLengthIn);

        // Only set the checksum on non-extended messages
        memcpy(pchChecksum.data(), payloadHash.begin(), pchChecksum.size());
    }

    GCC_WARNINGS_POP;
}

// Read data and deserialise ourselves as we go
 uint64_t CMessageHeader::Read(const char* pch, uint64_t numBytes, msg_buffer& buff)
{
    // Must only be called for an incomplete header
    assert(!Complete());

    // Copy as much data as required to the parsing buffer
    uint64_t requiredLength { GetLength() };
    uint64_t mumRemaining { requiredLength - buff.size() };
    uint64_t numToCopy { std::min(mumRemaining, numBytes) };
    buff.write(pch, numToCopy);

    // Do we have all the data we think we need?
    if(buff.size() == requiredLength)
    {
        // We have all the basic header data, check if we also need more for extended fields
        if(! IsExtended() && strncmp(reinterpret_cast<const char*>(buff.data()) + CMessageFields::BASIC_COMMAND_OFFSET, NetMsgType::EXTMSG, strlen(NetMsgType::EXTMSG)) == 0)
        {
            extendedFields = CExtendedMessageHeader {};
        }
        else
        {
            // We have enough data to fully deserialise ourselves
            buff >> *this;
            complete = true;
        }
    }

    return numToCopy;
}

std::string CMessageHeader::GetCommand() const
{
    if(IsExtended())
    {
        return extendedFields->GetCommand();
    }

    return { pchCommand.data(), pchCommand.data() + strnlen(pchCommand.data(), CMessageFields::COMMAND_SIZE) };
}

uint64_t CMessageHeader::GetLength() const
{
    return IsExtended()? CMessageFields::EXTENDED_HEADER_SIZE : CMessageFields::BASIC_HEADER_SIZE;
}

uint64_t CMessageHeader::GetPayloadLength() const
{
    if(IsExtended())
    {
        return extendedFields->GetPayloadLength();;
    }

    return static_cast<uint64_t>(nPayloadLength);
}

uint64_t CMessageHeader::GetHeaderSizeForPayload(uint64_t payloadSize)
{
    if(payloadSize > std::numeric_limits<uint32_t>::max())
    {
        return CMessageFields::EXTENDED_HEADER_SIZE;
    }

    return CMessageFields::BASIC_HEADER_SIZE;
}

uint64_t CMessageHeader::GetMaxPayloadLength(int version)
{
    if(version >= EXTENDED_PAYLOAD_VERSION)
    {
        return std::numeric_limits<uint64_t>::max();
    }

    return std::numeric_limits<uint32_t>::max();
}

bool CMessageHeader::IsExtended(uint64_t payloadSize)
{
    return GetHeaderSizeForPayload(payloadSize) == CMessageFields::EXTENDED_HEADER_SIZE;
}

bool CMessageHeader::CheckHeaderMagicAndCommand(const MessageMagic& magic) const
{
    // Check start string
    if (memcmp(GetMsgStart().data(), magic.data(),
               CMessageFields::MESSAGE_START_SIZE) != 0) {
        return false;
    }

    // Check the command string for errors
    return CheckCommandFormat(pchCommand.data());
}

bool CMessageHeader::IsValid(const Config& config) const {
    // Check start string
    if (!CheckHeaderMagicAndCommand(config.GetChainParams().NetMagic())) {
        return false;
    }

    // Message size
    if (IsOversized(config)) {
        LogPrintf("CMessageHeader::IsValid(): (%s, %u bytes) is oversized\n",
                  GetCommand(), nPayloadLength);
        return false;
    }

    // Extended fields
    if(IsExtended() && !extendedFields->IsValid(config)) {
        return false;
    }

    return true;
}

bool CMessageHeader::IsOversized(const Config& config) const
{
    return GetPayloadLength() > NetMsgType::GetMaxMessageLength(GetCommand(), config);
}

std::string CInv::GetCommand() const {
    std::string cmd;
    switch (GetKind()) {
        case MSG_TX:
            return cmd.append(NetMsgType::TX);
        case MSG_BLOCK:
            return cmd.append(NetMsgType::BLOCK);
        case MSG_FILTERED_BLOCK:
            return cmd.append(NetMsgType::MERKLEBLOCK);
        case MSG_CMPCT_BLOCK:
            return cmd.append(NetMsgType::CMPCTBLOCK);
        case MSG_DATAREF_TX:
            return cmd.append(NetMsgType::DATAREFTX);
        default:
            throw std::out_of_range(
                strprintf("CInv::GetCommand(): type=%d unknown type", type));
    }
}

std::string CInv::ToString() const {
    try {
        return strprintf("%s %s", GetCommand(), hash.ToString());
    } catch (const std::out_of_range &) {
        return strprintf("0x%08x %s", type, hash.ToString());
    }
}

const std::vector<std::string> &getAllNetMessageTypes() {
    return allNetMessageTypesVec;
}
