// Copyright (c) 2021 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "block_file_info.h"

#include "tinyformat.h"
#include "utiltime.h"

void CBlockFileInfo::AddKnownBlock(
    int32_t nHeightIn,
    uint64_t nTimeIn,
    uint64_t addSize,
    unsigned int startPos)
{
    AddBlock( nHeightIn, nTimeIn );
    nSize = std::max(startPos + addSize, nSize);
}

void CBlockFileInfo::AddNewBlock(
    int32_t nHeightIn,
    uint64_t nTimeIn,
    uint64_t addSize)
{
    AddBlock( nHeightIn, nTimeIn );
    nSize += addSize;
}

    /** update statistics (does not update nSize) */
void CBlockFileInfo::AddBlock(int32_t nHeightIn, uint64_t nTimeIn)
{
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

std::string CBlockFileInfo::ToString() const
{
    return strprintf(
        "CBlockFileInfo(blocks=%u, size=%u, heights=%u...%u, time=%s...%s)",
        nBlocks, nSize, nHeightFirst, nHeightLast,
        DateTimeStrFormat("%Y-%m-%d", nTimeFirst),
        DateTimeStrFormat("%Y-%m-%d", nTimeLast));
}
