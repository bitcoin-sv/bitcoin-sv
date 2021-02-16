// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2017 The Bitcoin developers
// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include <net/net_message.h>
#include <logging.h>

int CNetMessage::readHeader(const Config &config, const char *pch,
                            uint32_t nBytes) {
    // copy data to temporary parsing buffer
    uint32_t nRemaining = 24 - nHdrPos;
    uint32_t nCopy = std::min(nRemaining, nBytes);

    memcpy(&hdrbuf[nHdrPos], pch, nCopy);
    nHdrPos += nCopy;

    // if header incomplete, exit
    if (nHdrPos < 24) {
        return nCopy;
    }

    // deserialize to CMessageHeader
    try {
        hdrbuf >> hdr;
    } catch (const std::exception &) {
        LogPrint(BCLog::NETMSG, "Bad header format\n");
        return -1;
    }

    // Reject oversized messages
    if (hdr.IsOversized(config)) {
        LogPrint(BCLog::NETMSG, "Oversized header detected\n");
        return -1;
    }

    // switch state to reading message data
    in_data = true;

    return nCopy;
}

int CNetMessage::readData(const char *pch, uint32_t nBytes) {
    unsigned int nRemaining = hdr.nPayloadLength - nDataPos;
    unsigned int nCopy = std::min(nRemaining, nBytes);

    if (vRecv.size() < nDataPos + nCopy) {
        // Allocate up to 256 KiB ahead, but never more than the total message
        // size.
        vRecv.resize(std::min(hdr.nPayloadLength, nDataPos + nCopy + 256 * 1024));
    }

    hasher.Write((const uint8_t *)pch, nCopy);
    memcpy(&vRecv[nDataPos], pch, nCopy);
    nDataPos += nCopy;

    return nCopy;
}

const uint256 &CNetMessage::GetMessageHash() const {
    assert(complete());
    if (data_hash.IsNull()) {
        hasher.Finalize(data_hash.begin());
    }
    return data_hash;
}

