// Copyright (c) 2020 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include <string>

#include "serialize.h"

class CBlockFileInfo {
private:
    //!< number of blocks stored in file
    unsigned int nBlocks{ 0 };
    //!< lowest height of block in file
    int32_t nHeightFirst{ 0 };
    //!< highest height of block in file
    int32_t nHeightLast{ 0 };
    //!< earliest time of block in file
    uint64_t nTimeFirst{ 0 };
    //!< latest time of block in file
    uint64_t nTimeLast{ 0 };
    //!< number of used bytes of block file
    uint64_t nSize{ 0 };
    //!< number of used bytes in the undo file
    uint64_t nUndoSize{ 0 };

public:
    uint64_t Size() const { return nSize; }
    uint64_t UndoSize() const { return nUndoSize; }
    int32_t HeightLast() const { return nHeightLast; }

    uint64_t AddUndoSize( uint64_t add ) { return (nUndoSize += add); }

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

    std::string ToString() const;

    void AddKnownBlock(
        int32_t nHeightIn,
        uint64_t nTimeIn,
        uint64_t addSize,
        unsigned int startPos);

    void AddNewBlock(int32_t nHeightIn, uint64_t nTimeIn, uint64_t addSize);

private:
    /** update statistics (does not update nSize) */
    void AddBlock(int32_t nHeightIn, uint64_t nTimeIn);
};
