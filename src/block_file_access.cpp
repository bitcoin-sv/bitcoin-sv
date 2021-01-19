// Copyright (c) 2020 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "block_file_access.h"

#include "chain.h"
#include "clientversion.h"
#include "config.h"
#include "disk_block_pos.h"
#include "hash.h"
#include "fs.h"
#include "pow.h"
#include "primitives/block.h"
#include "streams.h"
#include "undo.h"
#include "util.h"

namespace
{
    /**
     * Translation to a filesystem path.
     */
    fs::path GetBlockPosFilename(const CDiskBlockPos& pos, const char* prefix)
    {
        return GetDataDir() / "blocks" / strprintf("%s%05u.dat", prefix, pos.nFile);
    }

    FILE* OpenDiskFile(
        const CDiskBlockPos& pos,
        const char* prefix,
        bool fReadOnly)
    {
        if (pos.IsNull()) {
            return nullptr;
        }

        fs::path path = GetBlockPosFilename(pos, prefix);
        fs::create_directories(path.parent_path());
        FILE *file = fsbridge::fopen(path, "rb+");
        if (!file && !fReadOnly) {
            file = fsbridge::fopen(path, "wb+");
        }
        if (!file) {
            LogPrintf("Unable to open file %s\n", path.string());
            return nullptr;
        }
        if (pos.nPos) {
            if (fseek(file, pos.nPos, SEEK_SET)) {
                LogPrintf("Unable to seek to position %u of %s\n", pos.nPos,
                          path.string());
                fclose(file);
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
}

FILE* BlockFileAccess::OpenBlockFile(const CDiskBlockPos& pos, bool fReadOnly)
{
    return OpenDiskFile(pos, "blk", fReadOnly);
}

FILE* BlockFileAccess::OpenUndoFile(const CDiskBlockPos& pos, bool fReadOnly)
{
    return OpenDiskFile(pos, "rev", fReadOnly);
}

UniqueCFile BlockFileAccess::GetBlockFile( int fileNo )
{
    CDiskBlockPos pos{ fileNo, 0 };

    if (!fs::exists(GetBlockPosFilename(pos, "blk")))
    {
        return nullptr;
    }

    return UniqueCFile{ OpenBlockFile(pos, true) };
}

bool BlockFileAccess::RemoveFile( int fileNo )
{
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
    CAutoFile fileout{ OpenBlockFile(pos), SER_DISK, CLIENT_VERSION };
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

    pos.nPos = (unsigned int)fileOutPos;

    std::vector<uint8_t> data;
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
    // Open history file to append
    CAutoFile fileout{ OpenUndoFile(pos), SER_DISK, CLIENT_VERSION };
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
    pos.nPos = (unsigned int)fileOutPos;
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
    CAutoFile filein{ OpenBlockFile(pos, true), SER_DISK, CLIENT_VERSION };
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
    UniqueCFile file{ OpenBlockFile(pos, true) };

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
    // Open history file to read
    CAutoFile filein{ OpenUndoFile(pos, true), SER_DISK, CLIENT_VERSION };
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
