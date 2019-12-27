// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "blockfileinfostore.h"
#include "config.h"
#include "util.h"
#include "txdb.h"  // CBlockTreeDB
#include "consensus/validation.h" // CValidationState

/** Access to info about block files */
std::unique_ptr<CBlockFileInfoStore> pBlockFileInfoStore = std::make_unique<CBlockFileInfoStore>();

void CBlockFileInfoStore::FindNextFileWithEnoughEmptySpace(const Config &config,
    uint64_t nAddSize, unsigned int& nFile)
{
    // this while instead of if is here because first commit introduced it
    // and vinfoBlockFile.size() can exceed nLastBlockFile at least in
    // LoadBlockIndexDB function where block file info is being loaded
    // and we can't be certain that it's the only case without more tests
    // and extensive refactoring
    while (vinfoBlockFile[nFile].nSize &&
           // >= is here for legacy purposes - could possibly be changed to > as
           // currently max file size is one byte less than preferred block file size
           // but larger code analisys would be required
           vinfoBlockFile[nFile].nSize + nAddSize >= config.GetPreferredBlockFileSize()) {
        nFile++;
        if (vinfoBlockFile.size() <= nFile) {
            vinfoBlockFile.resize(nFile + 1);
        }
    }
}

void CBlockFileInfoStore::FlushBlockFile(bool fFinalize) {
    LOCK(cs_LastBlockFile);

    CDiskBlockPos posOld(nLastBlockFile, 0);

    FILE *fileOld = CDiskFiles::OpenBlockFile(posOld);
    if (fileOld) {
        if (fFinalize) {
            TruncateFile(fileOld, vinfoBlockFile[nLastBlockFile].nSize);
        }
        FileCommit(fileOld);
        fclose(fileOld);
    }

    fileOld = CDiskFiles::OpenUndoFile(posOld);
    if (fileOld) {
        if (fFinalize) {
            TruncateFile(fileOld, vinfoBlockFile[nLastBlockFile].nUndoSize);
        }
        FileCommit(fileOld);
        fclose(fileOld);
    }
}

std::vector<std::pair<int, const CBlockFileInfo *>> CBlockFileInfoStore::GetAndClearDirtyFileInfo()
{
    std::vector<std::pair<int, const CBlockFileInfo *>> vFiles;
    vFiles.reserve(setDirtyFileInfo.size());
    for (std::set<int>::iterator it = setDirtyFileInfo.begin();
        it != setDirtyFileInfo.end();) {
        vFiles.push_back(
            std::make_pair(*it, &vinfoBlockFile[*it]));
        setDirtyFileInfo.erase(it++);
    }
    return vFiles;
}


bool CBlockFileInfoStore::FindBlockPos(const Config &config, CValidationState &state,
    CDiskBlockPos &pos, uint64_t nAddSize, unsigned int nHeight,
    uint64_t nTime, bool& fCheckForPruning, bool fKnown) {
    LOCK(cs_LastBlockFile);

    unsigned int nFile = fKnown ? pos.nFile : nLastBlockFile;
    if (vinfoBlockFile.size() <= nFile) {
        vinfoBlockFile.resize(nFile + 1);
    }

    if (!fKnown) {
        FindNextFileWithEnoughEmptySpace(config, nAddSize, nFile);
        pos.nFile = nFile;
        pos.nPos = vinfoBlockFile[nFile].nSize;
    }

    if ((int)nFile != nLastBlockFile) {
        if (!fKnown) {
            LogPrintf("Leaving block file %i: %s\n", nLastBlockFile,
                vinfoBlockFile[nLastBlockFile].ToString());
        }
        pBlockFileInfoStore->FlushBlockFile(!fKnown);
        nLastBlockFile = nFile;
    }

    vinfoBlockFile[nFile].AddBlock(nHeight, nTime);
    if (fKnown) {
        vinfoBlockFile[nFile].nSize =
            std::max(pos.nPos + nAddSize, vinfoBlockFile[nFile].nSize);
    }
    else {
        vinfoBlockFile[nFile].nSize += nAddSize;
    }

    if (!fKnown) {
        uint64_t nOldChunks =
            (pos.nPos + BLOCKFILE_CHUNK_SIZE - 1) / BLOCKFILE_CHUNK_SIZE;
        uint64_t nNewChunks =
            (vinfoBlockFile[nFile].nSize + BLOCKFILE_CHUNK_SIZE - 1) /
            BLOCKFILE_CHUNK_SIZE;
        if (nNewChunks > nOldChunks) {
            if (fPruneMode) {
                fCheckForPruning = true;
            }
            if (CheckDiskSpace(nNewChunks * BLOCKFILE_CHUNK_SIZE - pos.nPos)) {
                FILE *file = CDiskFiles::OpenBlockFile(pos);
                if (file) {
                    LogPrintf(
                        "Pre-allocating up to position 0x%x in blk%05u.dat\n",
                        nNewChunks * BLOCKFILE_CHUNK_SIZE, pos.nFile);
                    AllocateFileRange(file, pos.nPos,
                        nNewChunks * BLOCKFILE_CHUNK_SIZE -
                        pos.nPos);
                    fclose(file);
                }
            }
            else {
                return state.Error("out of disk space");
            }
        }
    }

    setDirtyFileInfo.insert(nFile);
    return true;
}

bool CBlockFileInfoStore::FindUndoPos(CValidationState &state, int nFile, CDiskBlockPos &pos,
    uint64_t nAddSize, bool& fCheckForPruning) {
    pos.nFile = nFile;

    LOCK(cs_LastBlockFile);

    uint64_t nNewSize;
    pos.nPos = vinfoBlockFile[nFile].nUndoSize;
    nNewSize = vinfoBlockFile[nFile].nUndoSize += nAddSize;
    setDirtyFileInfo.insert(nFile);

    uint64_t nOldChunks =
        (pos.nPos + UNDOFILE_CHUNK_SIZE - 1) / UNDOFILE_CHUNK_SIZE;
    uint64_t nNewChunks =
        (nNewSize + UNDOFILE_CHUNK_SIZE - 1) / UNDOFILE_CHUNK_SIZE;
    if (nNewChunks > nOldChunks) {
        if (fPruneMode) {
            fCheckForPruning = true;
        }
        if (CheckDiskSpace(nNewChunks * UNDOFILE_CHUNK_SIZE - pos.nPos)) {
            FILE *file = CDiskFiles::OpenUndoFile(pos);
            if (file) {
                LogPrintf("Pre-allocating up to position 0x%x in rev%05u.dat\n",
                    nNewChunks * UNDOFILE_CHUNK_SIZE, pos.nFile);
                AllocateFileRange(file, pos.nPos,
                    nNewChunks * UNDOFILE_CHUNK_SIZE - pos.nPos);
                fclose(file);
            }
        }
        else {
            return state.Error("out of disk space");
        }
    }

    return true;
}

/**
 * Calculate the amount of disk space the block & undo files currently use.
 */
uint64_t CBlockFileInfoStore::CalculateCurrentUsage() {
    // TODO: this method currently required cs_LastBlockFile to be held. Consider moving locking code insied this method  
    uint64_t retval = 0;
    for (const CBlockFileInfo &file : vinfoBlockFile) {
        retval += file.nSize + file.nUndoSize;
    }
    return retval;
}

void CBlockFileInfoStore::ClearFileInfo(int fileNumber)
{
    vinfoBlockFile[fileNumber].SetNull();
    setDirtyFileInfo.insert(fileNumber);
}


/**
 * Calculate the block/rev files to delete based on height specified by user
 * with RPC command pruneblockchain
 */
void CBlockFileInfoStore::FindFilesToPruneManual(std::set<int> &setFilesToPrune,
    int nManualPruneHeight) {
    assert(fPruneMode && nManualPruneHeight > 0);

    LOCK2(cs_main, cs_LastBlockFile);
    if (chainActive.Tip() == nullptr) {
        return;
    }

    // last block to prune is the lesser of (user-specified height,
    // MIN_BLOCKS_TO_KEEP from the tip)
    unsigned int nLastBlockWeCanPrune =
        std::min((unsigned)nManualPruneHeight,
            chainActive.Tip()->nHeight - MIN_BLOCKS_TO_KEEP);
    int count = 0;
    for (int fileNumber = 0; fileNumber < nLastBlockFile; fileNumber++) {
        if (vinfoBlockFile[fileNumber].nSize == 0 ||
            vinfoBlockFile[fileNumber].nHeightLast > nLastBlockWeCanPrune) {
            continue;
        }
        setFilesToPrune.insert(fileNumber);
        count++;
    }
    LogPrintf("Prune (Manual): prune_height=%d found %d blk/rev pairs for removal\n",
        nLastBlockWeCanPrune, count);
}


/**
 * Prune block and undo files (blk???.dat and undo???.dat) so that the disk
 * space used is less than a user-defined target. The user sets the target (in
 * MB) on the command line or in config file.  This will be run on startup and
 * whenever new space is allocated in a block or undo file, staying below the
 * target. Changing back to unpruned requires a reindex (which in this case
 * means the blockchain must be re-downloaded.)
 *
 * Pruning functions are called from FlushStateToDisk when the global
 * fCheckForPruning flag has been set. Block and undo files are deleted in
 * lock-step (when blk00003.dat is deleted, so is rev00003.dat.). Pruning cannot
 * take place until the longest chain is at least a certain length (100000 on
 * mainnet, 1000 on testnet, 1000 on regtest). Pruning will never delete a block
 * within a defined distance (currently 288) from the active chain's tip. The
 * block index is updated by unsetting HAVE_DATA and HAVE_UNDO for any blocks
 * that were stored in the deleted files. A db flag records the fact that at
 * least some block files have been pruned.
 *
 * @param[out]   setFilesToPrune   The set of file indices that can be unlinked
 * will be returned
 */
void CBlockFileInfoStore::FindFilesToPrune(std::set<int> &setFilesToPrune,
    uint64_t nPruneAfterHeight) {
    LOCK2(cs_main, cs_LastBlockFile);
    if (chainActive.Tip() == nullptr || nPruneTarget == 0) {
        return;
    }
    if (uint64_t(chainActive.Tip()->nHeight) <= nPruneAfterHeight) {
        return;
    }

    unsigned int nLastBlockWeCanPrune =
        chainActive.Tip()->nHeight - MIN_BLOCKS_TO_KEEP;
    uint64_t nCurrentUsage = pBlockFileInfoStore->CalculateCurrentUsage();
    // We don't check to prune until after we've allocated new space for files,
    // so we should leave a buffer under our target to account for another
    // allocation before the next pruning.
    uint64_t nBuffer = BLOCKFILE_CHUNK_SIZE + UNDOFILE_CHUNK_SIZE;
    uint64_t nBytesToPrune;
    int count = 0;

    if (nCurrentUsage + nBuffer >= nPruneTarget) {
        for (int fileNumber = 0; fileNumber < nLastBlockFile; fileNumber++) {
            nBytesToPrune = vinfoBlockFile[fileNumber].nSize +
                vinfoBlockFile[fileNumber].nUndoSize;

            if (vinfoBlockFile[fileNumber].nSize == 0) {
                continue;
            }

            // are we below our target?
            if (nCurrentUsage + nBuffer < nPruneTarget) {
                break;
            }

            // don't prune files that could have a block within
            // MIN_BLOCKS_TO_KEEP of the main chain's tip but keep scanning
            if (vinfoBlockFile[fileNumber].nHeightLast > nLastBlockWeCanPrune) {
                continue;
            }

            // Queue up the files for removal
            setFilesToPrune.insert(fileNumber);
            nCurrentUsage -= nBytesToPrune;
            count++;
        }
    }

    LogPrint(BCLog::PRUNE, "Prune: target=%dMiB actual=%dMiB diff=%dMiB "
        "max_prune_height=%d found %d blk/rev pairs for removal\n",
        nPruneTarget / 1024 / 1024, nCurrentUsage / 1024 / 1024,
        ((int64_t)nPruneTarget - (int64_t)nCurrentUsage) / 1024 / 1024,
        nLastBlockWeCanPrune, count);
}

void CBlockFileInfoStore::LoadBlockFileInfo(int nLastBlockFile, CBlockTreeDB& blockTreeDb)
{
    this->nLastBlockFile = nLastBlockFile;
    vinfoBlockFile.resize(nLastBlockFile + 1);
    LogPrintf("%s: last block file = %i\n", __func__, nLastBlockFile);
    for (int nFile = 0; nFile <= nLastBlockFile; nFile++) {
        blockTreeDb.ReadBlockFileInfo(nFile, vinfoBlockFile[nFile]);
    }
    LogPrintf("%s: last block file info: %s\n", __func__,
        vinfoBlockFile[nLastBlockFile].ToString());
    for (int nFile = nLastBlockFile + 1; true; nFile++) {
        CBlockFileInfo info;
        if (blockTreeDb.ReadBlockFileInfo(nFile, info)) {
            vinfoBlockFile.push_back(info);
        }
        else {
            break;
        }
    }
}

void CBlockFileInfoStore::Clear()
{
    vinfoBlockFile.clear();
    nLastBlockFile = 0;
    setDirtyFileInfo.clear();
}

CBlockFileInfo *CBlockFileInfoStore::GetBlockFileInfo(size_t n)
{
    return &vinfoBlockFile.at(n);
}
