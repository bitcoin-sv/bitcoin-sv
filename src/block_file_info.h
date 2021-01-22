// Copyright (c) 2020 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include <string>

#include "serialize.h"
#include "tinyformat.h"
#include "utiltime.h"

class CBlockFileInfo {
public:
    //!< number of blocks stored in file
    unsigned int nBlocks;
    //!< lowest height of block in file
    int32_t nHeightFirst;
    //!< highest height of block in file
    int32_t nHeightLast;
    //!< earliest time of block in file
    uint64_t nTimeFirst;
    //!< latest time of block in file
    uint64_t nTimeLast;
    //!< number of used bytes of block file
    uint64_t nSize;
    //!< number of used bytes in the undo file
    uint64_t nUndoSize;

    ADD_SERIALIZE_METHODS

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) 
    {
        // Legacy 32 bit sizes used for reading and writing. 
        // When writing size larger or equal than max 32 bit value, 
        // max 32 bit value (0xFFFFFFFF) is written in 32 bit field 
        // and actual size is written in separate 64 bit field. 
        // When reading, separate 64 bit value should be read when 32 bit value
        // is max (0xFFFFFFFF).
        unsigned int nSizeLegacy; 
        unsigned int nUndoSizeLegacy;
        if (nSize >= std::numeric_limits<uint32_t>::max())
        {
            nSizeLegacy = std::numeric_limits<uint32_t>::max();
        }
        else
        {
            nSizeLegacy = static_cast<uint32_t>(nSize);
        }
        if (nUndoSize >= std::numeric_limits<uint32_t>::max())
        {
            nUndoSizeLegacy = std::numeric_limits<uint32_t>::max();
        }
        else
        {
            nUndoSizeLegacy = static_cast<uint32_t>(nUndoSize);
        }
        READWRITE(VARINT(nBlocks));
        READWRITE(VARINT(nSizeLegacy));
        READWRITE(VARINT(nUndoSizeLegacy));
        READWRITE(VARINT(nHeightFirst));
        READWRITE(VARINT(nHeightLast));
        READWRITE(VARINT(nTimeFirst));
        READWRITE(VARINT(nTimeLast));
        if (nSizeLegacy == std::numeric_limits<uint32_t>::max())
        {
            READWRITE(VARINT(nSize));
        }
        else
        {
            nSize = nSizeLegacy;
        }
        if (nUndoSize == std::numeric_limits<uint32_t>::max())
        {
            READWRITE(VARINT(nUndoSize));
        }
        else
        {
            nUndoSize = nUndoSizeLegacy;
        }
    }

    void SetNull() {
        nBlocks = 0;
        nHeightFirst = 0;
        nHeightLast = 0;
        nTimeFirst = 0;
        nTimeLast = 0;
        nSize = 0;
        nUndoSize = 0;
    }

    CBlockFileInfo() { SetNull(); }

    std::string ToString() const;

    /** update statistics (does not update nSize) */
    void AddBlock(int32_t nHeightIn, uint64_t nTimeIn) {
        if (nBlocks == 0 || nHeightFirst > nHeightIn) {
            nHeightFirst = nHeightIn;
        }
        if (nBlocks == 0 || nTimeFirst > nTimeIn) {
            nTimeFirst = nTimeIn;
        }
        nBlocks++;
        if (nHeightIn > nHeightLast) {
            nHeightLast = nHeightIn;
        }
        if (nTimeIn > nTimeLast) {
            nTimeLast = nTimeIn;
        }
    }
};

inline std::string CBlockFileInfo::ToString() const {
    return strprintf(
        "CBlockFileInfo(blocks=%u, size=%u, heights=%u...%u, time=%s...%s)",
        nBlocks, nSize, nHeightFirst, nHeightLast,
        DateTimeStrFormat("%Y-%m-%d", nTimeFirst),
        DateTimeStrFormat("%Y-%m-%d", nTimeLast));
}
