// Copyright (c) 2020 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include "cfile_util.h"
#include "protocol.h"

class CBlock;
class CBlockUndo;
struct CDiskBlockMetaData;
struct CDiskBlockPos;

/** Utility functions for opening block and undo files */
namespace BlockFileAccess
{
    /** Open a block file (blk?????.dat). */
    FILE* OpenBlockFile(const CDiskBlockPos& pos, bool fReadOnly = false);

    /** Open an undo file (rev?????.dat) */
    FILE* OpenUndoFile(const CDiskBlockPos& pos, bool fReadOnly = false);

    UniqueCFile GetBlockFile( int fileNo );
    bool RemoveFile( int fileNo );

    bool WriteBlockToDisk(
        const CBlock& block,
        CDiskBlockPos& pos,
        const CMessageHeader::MessageMagic& messageStart,
        CDiskBlockMetaData& metaData);

    bool UndoWriteToDisk(
        const CBlockUndo& blockundo,
        CDiskBlockPos& pos,
        const uint256& hashBlock,
        const CMessageHeader::MessageMagic& messageStart);
}
