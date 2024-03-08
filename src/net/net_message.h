// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2017 The Bitcoin developers
// Copyright (c) 2020-2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include <hash.h>
#include <protocol.h>
#include <streams.h>
#include "msg_buffer.h"

#include <stdexcept>

class CNetMessage {
private:
    mutable CHash256 hasher {};
    mutable uint256 data_hash {};

    // Incoming data stream
    msg_buffer dataBuff;

    // Message header
    CMessageHeader hdr;

    // Time (in microseconds) of message receipt.
    int64_t nTime {0};

public:
    CNetMessage(const CMessageHeader::MessageMagic& pchMessageStartIn, int nTypeIn, int nVersionIn)
    : dataBuff { nTypeIn, nVersionIn },
      hdr { pchMessageStartIn }
    {
    }

    bool Complete() const {
        if (!hdr.Complete()) {
            return false;
        }

        return (hdr.GetPayloadLength() == dataBuff.size());
    }

    const uint256& GetMessageHash() const;
    const CMessageHeader& GetHeader() const { return hdr; }
    int64_t GetTime() const { return nTime; }
    void SetTime(int64_t time) { nTime = time; }
    msg_buffer& GetData() { return dataBuff; }
    uint64_t GetTotalLength() const;

    void SetVersion(int nVersionIn) {
        dataBuff.SetVersion(nVersionIn);
    }

    uint64_t Read(const Config& config, const char* pch, uint64_t nBytes);
    uint64_t Read(const Config&, const uint8_t* pch, uint64_t nBytes);
};

/**
 * Exception type that gets thrown if we should ban a peer.
 */
class BanPeer : public std::runtime_error
{
  public:
    explicit BanPeer(const std::string& msg) : std::runtime_error{msg} {}
};

