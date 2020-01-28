// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#ifndef BITCOIN_BLOCKFILEINFOSTORE_H
#define BITCOIN_BLOCKFILEINFOSTORE_H

#include <vector>

#include "sync.h"
#include "chain.h"
#include "validation.h"

/** Stores a collection of CBlockFileInfo-s in memory */
class CBlockFileInfoStore
{
    CCriticalSection cs_LastBlockFile;
    std::vector<CBlockFileInfo> vinfoBlockFile;
    int nLastBlockFile = 0;

    /** Dirty block file entries. */
    std::set<int> setDirtyFileInfo;

    void FindNextFileWithEnoughEmptySpace(const Config &config,
        uint64_t nAddSize, unsigned int& nFile);
public:
    uint64_t CalculateCurrentUsage();

    bool FindBlockPos(const Config &config, CValidationState &state,
        CDiskBlockPos &pos, uint64_t nAddSize, unsigned int nHeight,
        uint64_t nTime, bool& fCheckForPruning, bool fKnown = false);

    void FindFilesToPrune(std::set<int> &setFilesToPrune,
        uint64_t nPruneAfterHeight);

    void FindFilesToPruneManual(std::set<int> &setFilesToPrune,
        int nManualPruneHeight);

    bool FindUndoPos(CValidationState &state, int nFile, CDiskBlockPos &pos,
        uint64_t nAddSize, bool& fCheckForPruning);

    void FlushBlockFile(bool fFinalize = false);

    void LoadBlockFileInfo(int nLastBlockFile, CBlockTreeDB& blockTreeDb);

    // Returns all dirty files infos and clears the set that indicates which are dirty
    std::vector<std::pair<int, const CBlockFileInfo *>> GetAndClearDirtyFileInfo();

    // Clears specified fileInfo and mar it as dirty.
    void ClearFileInfo(int fileNumber);

    // Uninitialize the object (without marking it as dirty)
    void Clear();

    /** Get block file info entry for one block file */
    CBlockFileInfo *GetBlockFileInfo(size_t n);

    // Return number of block files
    int GetnLastBlockFile() {
        return nLastBlockFile;
    }
    CCriticalSection& GetLock()
    {
        return cs_LastBlockFile;
    }
};


/** Utility functions for opening disk and block files */
class CDiskFiles
{
    static FILE *OpenDiskFile(const CDiskBlockPos &pos, const char *prefix,
        bool fReadOnly);
public:
    /** Open a block file (blk?????.dat). */
    static FILE *OpenBlockFile(const CDiskBlockPos &pos, bool fReadOnly = false);

    /** Open an undo file (rev?????.dat) */
    static FILE *OpenUndoFile(const CDiskBlockPos &pos, bool fReadOnly = false);
};


/** Access to info about block files */
extern std::unique_ptr<CBlockFileInfoStore> pBlockFileInfoStore;

#endif // BLOCKFILEINFOSTORE_H