// Copyright (c) 2020 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "block_file_access.h"

#include "block_file_info.h"
#include "chain.h"
#include "clientversion.h"
#include "config.h"
#include "disk_block_pos.h"
#include "disk_tx_pos.h"
#include "hash.h"
#include "fs.h"
#include "pow.h"
#include "primitives/block.h"
#include "streams.h"
#include "undo.h"
#include "util.h"

#include <mutex>

namespace
{
    // Mutex is used to synchronize file deletions and resizes.
    // For block files that are opened for reading we don't need to hold the mutex
    // as we rely on filesystem to do the right thing (on Linux/Mac the filesystem
    // extends file's life if the file is deleted while in use; on Windows
    // filesystem prevents deletion so we need to try deleting the file again at
    // a later point in time).
    // For block undo files we need to hold the lock during reading as during
    // deletion it is expected that if block file deletion succeeded, block
    // undo file deletion will also succeed (and ignored if deletion doesn't
    // succeed).
    std::shared_mutex serializationMutex;

    /**
     * Translation to a filesystem path.
     */
    fs::path GetBlockPosFilename(const CDiskBlockPos& pos, const char* prefix)
    {
        return GetDataDir() / "blocks" / strprintf("%s%05u.dat", prefix, pos.File());
    }

    enum class OpenDiskType
    {
        ReadIfExists,
        WriteIfExists,
        Write
    };

    UniqueCFile OpenDiskFile(
        const CDiskBlockPos& pos,
        const char* prefix,
        OpenDiskType type,
        bool missingFileIsNotExpected )
    {
        if (pos.IsNull()) {
            return nullptr;
        }

        fs::path path = GetBlockPosFilename(pos, prefix);

        UniqueCFile file{
            [&path, type]
            {
                if (type == OpenDiskType::ReadIfExists)
                {
                    return UniqueCFile{ fsbridge::fopen(path, "rb") };
                }
                else if (type == OpenDiskType::WriteIfExists)
                {
                    return UniqueCFile{ fsbridge::fopen(path, "rb+") };
                }

                if (UniqueCFile file{ fsbridge::fopen(path, "rb+") }; file)
                {
                    return file;
                }

                // Only create directories for new files
                fs::create_directories(path.parent_path());

                // make a new file
                return UniqueCFile{ fsbridge::fopen(path, "wb+") };
            }()};

        if (!file) {
            if (missingFileIsNotExpected)
            {
                LogPrintf("Unable to open file %s\n", path.string());
            }
            return nullptr;
        }
        if (pos.Pos()) {
            if (fseek(file.get(), pos.Pos(), SEEK_SET)) {
                LogPrintf("Unable to seek to position %u of %s\n", pos.Pos(),
                          path.string());
                return nullptr;
            }
        }
        return file;
    }

    /**
     * Write index header. If size larger thant 32 bit max than write 32 bit max and 64 bit size.
     * 32 bit max (0xFFFFFFFF) indicates that there is 64 bit size value following.
     */
    void WriteIndexHeader(CAutoFile& fileout,
                          const CMessageHeader::MessageMagic& messageStart,
                          uint64_t nSize)
    {
        if (nSize >= std::numeric_limits<unsigned int>::max())
        {
            fileout << FLATDATA(messageStart) << std::numeric_limits<uint32_t>::max() << nSize;
        }
        else
        {
            fileout << FLATDATA(messageStart) << static_cast<uint32_t>(nSize);
        }
    }

    /** Open a block file (blk?????.dat). */
    UniqueCFile OpenBlockFile(
        const CDiskBlockPos& pos,
        OpenDiskType type,
        bool missingFileIsNotExpected )
    {
        return OpenDiskFile( pos, "blk", type, missingFileIsNotExpected );
    }

    /** Open an undo file (rev?????.dat) */
    UniqueCFile OpenUndoFile(
        const CDiskBlockPos& pos,
        OpenDiskType type,
        bool missingFileIsNotExpected )
    {
        return OpenDiskFile( pos, "rev", type, missingFileIsNotExpected );
    }
}

UniqueCFile BlockFileAccess::OpenBlockFile( int fileNo )
{
    return OpenBlockFile( { fileNo, 0 } );
}

UniqueCFile BlockFileAccess::OpenBlockFile( const CDiskBlockPos& pos )
{
    return ::OpenBlockFile(pos, OpenDiskType::ReadIfExists, true);
}

bool BlockFileAccess::RemoveFile( int fileNo )
{
    // We use lock to prevent cases where block file deletion would succeed
    // while deleting undo file would fail as the undo file is in use.
    std::scoped_lock lock{ serializationMutex };

    CDiskBlockPos pos{ fileNo, 0 };
    boost::system::error_code ec;
    fs::remove(GetBlockPosFilename(pos, "blk"), ec);

    if (ec)
    {
        return false;
    }

    // only delete rev file if blk file deletion succeeded otherwise keep the
    // data for now as it's most likely still being used
    fs::remove(GetBlockPosFilename(pos, "rev"));

    return true;
}

bool BlockFileAccess::WriteBlockToDisk(
    const CBlock& block,
    CDiskBlockPos& pos,
    const CMessageHeader::MessageMagic& messageStart,
    CDiskBlockMetaData& metaData)
{
    // Open history file to append
    CAutoFile fileout{ ::OpenBlockFile(pos, OpenDiskType::WriteIfExists, true), SER_DISK, CLIENT_VERSION };
    if (fileout.IsNull()) {
        return error("WriteBlockToDisk: OpenBlockFile failed");
    }

    // Write index header.
    WriteIndexHeader(fileout, messageStart, GetSerializeSize(fileout, block));    
    
    // Write block
    long fileOutPos = ftell(fileout.Get());
    if (fileOutPos < 0) {
        return error("WriteBlockToDisk: ftell failed");
    }

    pos = { pos.File(), (unsigned int)fileOutPos };
    
    std::vector<uint8_t> data;
    data.reserve(ser_size(block));
    CVectorWriter{SER_DISK, CLIENT_VERSION, data, 0, block};
    metaData = { Hash(data.begin(), data.end()), data.size() };

    fileout.write(reinterpret_cast<const char*>(data.data()), data.size());

    return true;
}

bool BlockFileAccess::UndoWriteToDisk(
    const CBlockUndo& blockundo,
    CDiskBlockPos& pos,
    const uint256& hashBlock,
    const CMessageHeader::MessageMagic& messageStart)
{
    // We know that we are writing to separate locations as pre-requirement
    // is to allocate space so this can be a shared lock.
    // We use shared lock to prevent BlockFileStore::RemoveFile from only
    // partially succeeding (deletes block file but can't delete undo file)
    // - this should never happen in practice since we don't write to old
    // undo files and don't delete new ones.
    std::shared_lock lock{ serializationMutex };

    // Open history file to append
    CAutoFile fileout{ ::OpenUndoFile(pos, OpenDiskType::WriteIfExists, true), SER_DISK, CLIENT_VERSION };
    if (fileout.IsNull()) {
        return error("%s: OpenUndoFile failed", __func__);
    }

    // Write index header. 
    WriteIndexHeader(fileout, messageStart, GetSerializeSize(fileout, blockundo));

    // Write undo data
    long fileOutPos = ftell(fileout.Get());
    if (fileOutPos < 0) {
        return error("%s: ftell failed", __func__);
    }
    pos = { pos.File(), (unsigned int)fileOutPos };
    fileout << blockundo;

    // calculate & write checksum
    CHashWriter hasher(SER_GETHASH, PROTOCOL_VERSION);
    hasher << hashBlock;
    hasher << blockundo;
    fileout << hasher.GetHash();

    return true;
}

bool BlockFileAccess::ReadBlockFromDisk(
    CBlock& block,
    const CDiskBlockPos& pos,
    const Config& config)
{
    block.SetNull();

    // Open history file to read
    CAutoFile filein{ ::OpenBlockFile(pos, OpenDiskType::ReadIfExists, true), SER_DISK, CLIENT_VERSION };
    if (filein.IsNull()) {
        return error("ReadBlockFromDisk: OpenBlockFile failed for %s",
                     pos.ToString());
    }

    // Read block
    try {
        filein >> block;
    } catch (const std::exception &e) {
        return error("%s: Deserialize or I/O error - %s at %s", __func__,
                     e.what(), pos.ToString());
    }

    // Check the header
    if (!CheckProofOfWork(block.GetHash(), block.nBits, config)) {
        return error("ReadBlockFromDisk: Errors in block header at %s",
                     pos.ToString());
    }

    return true;
}

auto BlockFileAccess::GetDiskBlockStreamReader(
    const CDiskBlockPos& pos,
    bool calculateDiskBlockMetadata)
    -> std::unique_ptr<CBlockStreamReader<CFileReader>>
{
    UniqueCFile file{ ::OpenBlockFile(pos, OpenDiskType::ReadIfExists, true) };

    if (!file)
    {
        error("GetDiskBlockStreamReader(CDiskBlockPos&): OpenBlockFile failed for %s",
            pos.ToString());
        return {}; // could not open a stream
    }

    return
        std::make_unique<CBlockStreamReader<CFileReader>>(
            std::move(file),
            CStreamVersionAndType{SER_DISK, CLIENT_VERSION},
            calculateDiskBlockMetadata,
            pos);
}

bool BlockFileAccess::UndoReadFromDisk(
    CBlockUndo& blockundo,
    const CDiskBlockPos& pos,
    const uint256& hashBlock)
{
    // We use shared lock to prevent BlockFileStore::RemoveFile from only
    // partially succeeding (deletes block file but can't delete undo file)
    // - this should never happen in practice since we don't write to old
    // undo files and don't delete new ones.
    std::shared_lock lock{ serializationMutex };

    // Open history file to read
    CAutoFile filein{ ::OpenUndoFile(pos, OpenDiskType::ReadIfExists, true), SER_DISK, CLIENT_VERSION };
    if (filein.IsNull()) {
        return error("%s: OpenUndoFile failed", __func__);
    }

    // Read block
    uint256 hashChecksum;
    // We need a CHashVerifier as reserializing may lose data
    CHashVerifier<CAutoFile> verifier(&filein);
    try {
        verifier << hashBlock;
        verifier >> blockundo;
        filein >> hashChecksum;
    } catch (const std::exception &e) {
        return error("%s: Deserialize or I/O error - %s", __func__, e.what());
    }

    // Verify checksum
    if (hashChecksum != verifier.GetHash()) {
        return error("%s: Checksum mismatch", __func__);
    }

    return true;
}

void BlockFileAccess::FlushBlockFile(
    int fileNo,
    const CBlockFileInfo& blockFileInfo,
    bool finalize)
{
    // We use lock to make sure there are no file resizes pending - this should
    // not happen in practice as we don't truncate fies before a new file is
    // already prepared.
    std::scoped_lock lock{ serializationMutex };

    CDiskBlockPos posOld(fileNo, 0);

    UniqueCFile fileOld = ::OpenBlockFile(posOld, OpenDiskType::WriteIfExists, false);
    if (fileOld) {
        if (finalize)
        {
            TruncateFile(fileOld.get(), blockFileInfo.Size());
        }
        FileCommit(fileOld.get());
    }
    else
    {
        assert( blockFileInfo.Size() == 0 );
    }

    fileOld = ::OpenUndoFile(posOld, OpenDiskType::WriteIfExists, false);
    if (fileOld) {
        if (finalize)
        {
            TruncateFile(fileOld.get(), blockFileInfo.UndoSize());
        }
        FileCommit(fileOld.get());
    }
    else
    {
        assert( blockFileInfo.UndoSize() == 0 );
    }
}

bool BlockFileAccess::PreAllocateBlock(
    uint64_t nNewChunks,
    const CDiskBlockPos& pos)
{
    // We use lock to make sure there is only one resize active at the same time.
    // Also OpenBlockFile OpenDiskType::Write parameter requires a unique lock.
    std::scoped_lock lock{ serializationMutex };

    if (CheckDiskSpace(nNewChunks * BLOCKFILE_CHUNK_SIZE - pos.Pos())) {
        UniqueCFile file = ::OpenBlockFile(pos, OpenDiskType::Write, true);
        if (file) {
            LogPrintf(
                "Pre-allocating up to position 0x%x in blk%05u.dat\n",
                nNewChunks * BLOCKFILE_CHUNK_SIZE, pos.File());
            AllocateFileRange(file.get(), pos.Pos(),
                nNewChunks * BLOCKFILE_CHUNK_SIZE -
                pos.Pos());

            return true;
        }
    }

    return false;
}

bool BlockFileAccess::PreAllocateUndoBlock(
    uint64_t nNewChunks,
    const CDiskBlockPos& pos)
{
    // We use lock to make sure there is only one resize active at the same time.
    // Also OpenBlockFile OpenDiskType::Write parameter requires a unique lock.
    std::scoped_lock lock{ serializationMutex };

    if (CheckDiskSpace(nNewChunks * UNDOFILE_CHUNK_SIZE - pos.Pos())) {
        UniqueCFile file = ::OpenUndoFile(pos, OpenDiskType::Write, true);
        if (file) {
            LogPrintf("Pre-allocating up to position 0x%x in rev%05u.dat\n",
                nNewChunks * UNDOFILE_CHUNK_SIZE, pos.File());
            AllocateFileRange(file.get(), pos.Pos(),
                nNewChunks * UNDOFILE_CHUNK_SIZE - pos.Pos());

            return true;
        }
    }

    return false;
}

bool BlockFileAccess::LoadBlockHashAndTx(
    const CDiskTxPos& postx,
    uint256& hashBlock,
    CTransactionRef& txOut)
{
    CAutoFile file{ ::OpenBlockFile(postx, OpenDiskType::ReadIfExists, true), SER_DISK, CLIENT_VERSION };
    if (file.IsNull()) {
        return error("%s: OpenBlockFile failed", __func__);
    }
    CBlockHeader header;
    try {
        file >> header;
#if defined(WIN32)
        _fseeki64(file.Get(), postx.TxOffset(), SEEK_CUR);
#else
        fseek(file.Get(), postx.TxOffset(), SEEK_CUR);
#endif
        file >> txOut;
    } catch (const std::exception &e) {
        return error("%s: Deserialize or I/O error - %s", __func__,
                     e.what());
    }
    hashBlock = header.GetHash();

    return true;
}
