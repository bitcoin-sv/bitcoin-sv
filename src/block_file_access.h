// Copyright (c) 2020 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include <functional>
#include <memory>
#include <optional>

#include "blockstreams.h"
#include "cfile_util.h"
#include "protocol.h"
#include "streams.h"

class CBlock;
class CBlockFileInfo;
class CBlockUndo;
class Config;
struct CDiskBlockMetaData;
struct CDiskBlockPos;
struct CDiskTxPos;
class CTransaction;

/** The pre-allocation chunk size for blk?????.dat files (since 0.8) */
static constexpr unsigned int BLOCKFILE_CHUNK_SIZE = 0x1000000; // 16 MiB
/** The pre-allocation chunk size for rev?????.dat files (since 0.8) */
static constexpr unsigned int UNDOFILE_CHUNK_SIZE = 0x100000; // 1 MiB

/**
 * Utility functions for opening block and undo files.
 * The entire codebase should access block/undo files through these functions
 * as they internally guarantee read/resize/delete files system access
 * synchronization.
 */
namespace BlockFileAccess
{
    UniqueCFile OpenBlockFile( int fileNo );
    UniqueCFile OpenBlockFile( const CDiskBlockPos& pos );
    bool RemoveFile( int fileNo );

    bool ReadBlockFromDisk(
        CBlock& block,
        const CDiskBlockPos& pos,
        const Config& config);

    std::unique_ptr<CBlockStreamReader<CFileReader>> GetDiskBlockStreamReader(
        const CDiskBlockPos& pos,
        bool calculateDiskBlockMetadata=false);

    bool UndoReadFromDisk(
        CBlockUndo& blockundo,
        const CDiskBlockPos& pos,
        const uint256& hashBlock);

    /**
     * Pre-condition:
     * Block file is already pre-allocated to have enough free space at position
     * pos to write data to disk.
     */
    bool WriteBlockToDisk(
        const CBlock& block,
        CDiskBlockPos& pos,
        const CMessageHeader::MessageMagic& messageStart,
        CDiskBlockMetaData& metaData);

    /**
     * Pre-condition:
     * Block file is already pre-allocated to have enough free space at position
     * pos to write data to disk.
     */
    bool UndoWriteToDisk(
        const CBlockUndo& blockundo,
        CDiskBlockPos& pos,
        const uint256& hashBlock,
        const CMessageHeader::MessageMagic& messageStart);

    /**
     * Function makes sure that all block and undo file data that is remaining
     * in filesystem memory cache is flushed to disk.
     *
     * blockFileInfo is used only in case finalize is set to true.
     * In case finalize is set to true block and undo file are truncated to
     * size of data (empty reserved space after the data is removed).
     */
    void FlushBlockFile(
        int fileNo,
        const CBlockFileInfo& blockFileInfo,
        bool finalize);

    bool PreAllocateBlock( uint64_t nNewChunks, const CDiskBlockPos& pos );
    bool PreAllocateUndoBlock( uint64_t nNewChunks, const CDiskBlockPos& pos );

    bool LoadBlockHashAndTx(
        const CDiskTxPos& postx,
        uint256& hashBlock,
        std::shared_ptr<const CTransaction>& txOut);
}
