// Copyright (c) 2020 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "block_file_access.h"

#include "disk_block_pos.h"
#include "fs.h"
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
