// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2017 The Bitcoin developers
// Copyright (c) 2020-2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include <net/net_message.h>
#include <logging.h>

uint64_t CNetMessage::Read(const Config& config, const char* pch, uint64_t nBytes)
{
    // Still reading header?
    if(!hdr.Complete())
    {
        try
        {
            uint64_t numRead { hdr.Read(pch, nBytes, dataBuff) };
            if(hdr.Complete())
            {
                // Reject oversized messages
                if(hdr.IsOversized(config))
                {
                    throw BanPeer { "Oversized header detected" };
                }

                dataBuff.command(hdr.GetCommand());
                dataBuff.payload_len(hdr.GetPayloadLength());
            }

            return numRead;
        }
        catch(const std::exception& e)
        {
            throw BanPeer { std::string { "Bad header format: " } + e.what() };
        }
    }

    // Read payload data
    uint64_t nRemaining { hdr.GetPayloadLength() - dataBuff.size() };
    uint64_t nCopy { std::min(nRemaining, nBytes) };
    dataBuff.write(pch, nCopy);

    // No need to calculate message hash for extended format msgs
    if(! hdr.IsExtended())
    {
        hasher.Write(reinterpret_cast<const uint8_t*>(pch), nCopy);
    }

    return nCopy;
}

uint64_t CNetMessage::Read(const Config& config, const uint8_t* p, uint64_t nBytes)
{
    return Read(config, reinterpret_cast<const char*>(p), nBytes);
}

const uint256& CNetMessage::GetMessageHash() const
{
    assert(Complete());
    if (data_hash.IsNull()) {
        hasher.Finalize(data_hash.begin());
    }
    return data_hash;
}

// Header length + payload length
uint64_t CNetMessage::GetTotalLength() const
{
    return hdr.GetLength() + hdr.GetPayloadLength();
}

