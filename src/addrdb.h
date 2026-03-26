// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_ADDRDB_H
#define BITCOIN_ADDRDB_H

#include "fs.h"
#include "serialize.h"

#include <map>
#include <string>

class CSubNet;
class CAddrMan;
class CDataStream;
class CChainParams;

typedef enum BanReason //NOLINT(cppcoreguidelines-use-enum-class)
{
    BanReasonUnknown = 0,
    BanReasonNodeMisbehaving = 1,
    BanReasonManuallyAdded = 2
} BanReason;

class CBanEntry {
public:
    static const int CURRENT_VERSION = 1;
    int nVersion{CBanEntry::CURRENT_VERSION};
    int64_t nCreateTime{};
    int64_t nBanUntil{};
    uint8_t banReason{BanReasonUnknown};

    CBanEntry() = default;

    CBanEntry(int64_t nCreateTimeIn):nCreateTime{nCreateTimeIn}
    {}

    ADD_SERIALIZE_METHODS

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        READWRITE(this->nVersion);
        READWRITE(nCreateTime);
        READWRITE(nBanUntil);
        READWRITE(banReason);
    }

    std::string banReasonToString() {
        switch (banReason) {
            case BanReasonNodeMisbehaving:
                return "node misbehaving";
            case BanReasonManuallyAdded:
                return "manually added";
            default:
                return "unknown";
        }
    }
};

typedef std::map<CSubNet, CBanEntry> banmap_t;

/** Access to the (IP) address database (peers.dat) */
class CAddrDB
{
    fs::path pathAddr_;
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
    const CChainParams& chainParams_;

public:
    CAddrDB(const CChainParams&);
    bool Write(const CAddrMan&);
    bool Read(CAddrMan&);
    bool Read(CAddrMan&, CDataStream&);
};

/** Access to the banlist database (banlist.dat) */
class CBanDB
{
    fs::path pathBanlist_;
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
    const CChainParams& chainParams_;

public:
    CBanDB(const CChainParams&);
    bool Write(const banmap_t&);
    bool Read(banmap_t&);
};

#endif // BITCOIN_ADDRDB_H
