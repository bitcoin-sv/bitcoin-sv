// Copyright (c) 2020 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include "serialize.h"
#include "tinyformat.h"

#include <string>

struct CDiskBlockPos {
private:
    int nFile{ -1 };
    unsigned int nPos{ 0 };

public:
    int File() const { return nFile; }
    unsigned int Pos() const { return nPos; }

    ADD_SERIALIZE_METHODS

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        READWRITE(VARINT(nFile));
        READWRITE(VARINT(nPos));
    }

    CDiskBlockPos() = default;

    CDiskBlockPos(int nFileIn, unsigned int nPosIn) {
        // NOLINTNEXTLINE(cppcoreguidelines-prefer-member-initializer)
        nFile = nFileIn;
        // NOLINTNEXTLINE(cppcoreguidelines-prefer-member-initializer)
        nPos = nPosIn;
    }

    bool operator==(const CDiskBlockPos& other) const
    {
        return (nFile == other.nFile && nPos == other.nPos);
    }

    bool operator!=(const CDiskBlockPos& other) const
    {
        return !(*this == other);
    }

    bool IsNull() const { return (nFile == -1); }

    std::string ToString() const {
        return strprintf("CBlockDiskPos(nFile=%i, nPos=%i)", nFile, nPos);
    }
};
