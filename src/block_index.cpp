// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "block_index.h"

#include "block_file_access.h"
#include "blockfileinfostore.h"
#include "config.h"
#include "clientversion.h"
#include "warnings.h"
#include "abort_node.h"

std::set<CBlockIndex *> setDirtyBlockIndex;

/** Turn the lowest '1' bit in the binary representation of a number into a '0'.
 */
static inline int InvertLowestOne(int n) {
    return n & (n - 1);
}

/** Compute what height to jump back to with the CBlockIndex::pskip pointer. */
static inline int32_t GetSkipHeight(int32_t height) {
    if (height < 2) {
        return 0;
    }

    // Determine which height to jump back to. Any number strictly lower than
    // height is acceptable, but the following expression seems to perform well
    // in simulations (max 110 steps to go back up to 2**18 blocks).
    return (height & 1) ? InvertLowestOne(InvertLowestOne(height - 1)) + 1
                        : InvertLowestOne(height);
}

CBlockIndex *CBlockIndex::GetAncestor(int32_t height) {
    if (height > nHeight || height < 0) {
        return nullptr;
    }

    CBlockIndex *pindexWalk = this;
    int32_t heightWalk = nHeight;
    while (heightWalk > height) {
        int32_t heightSkip = GetSkipHeight(heightWalk);
        int32_t heightSkipPrev = GetSkipHeight(heightWalk - 1);
        if (pindexWalk->pskip != nullptr &&
            (heightSkip == height || (heightSkip > height &&
                                      !(heightSkipPrev < heightSkip - 2 &&
                                        heightSkipPrev >= height)))) {
            // Only follow pskip if pprev->pskip isn't better than pskip->pprev.
            pindexWalk = pindexWalk->pskip;
            heightWalk = heightSkip;
        } else {
            assert(pindexWalk->pprev);
            pindexWalk = pindexWalk->pprev;
            heightWalk--;
        }
    }
    return pindexWalk;
}

const CBlockIndex *CBlockIndex::GetAncestor(int32_t height) const {
    return const_cast<CBlockIndex *>(this)->GetAncestor(height);
}

void CBlockIndex::BuildSkip() {
    if (pprev) {
        pskip = pprev->GetAncestor(GetSkipHeight(nHeight));
    }
}

arith_uint256 GetBlockProof(const CBlockIndex &block) {
    arith_uint256 bnTarget;
    bool fNegative;
    bool fOverflow;
    bnTarget.SetCompact(block.nBits, &fNegative, &fOverflow);
    if (fNegative || fOverflow || bnTarget == 0) {
        return 0;
    }
    // We need to compute 2**256 / (bnTarget+1), but we can't represent 2**256
    // as it's too large for a arith_uint256. However, as 2**256 is at least as
    // large as bnTarget+1, it is equal to ((2**256 - bnTarget - 1) /
    // (bnTarget+1)) + 1, or ~bnTarget / (nTarget+1) + 1.
    return (~bnTarget / (bnTarget + 1)) + 1;
}


int64_t GetBlockProofEquivalentTime(const CBlockIndex &to,
                                    const CBlockIndex &from,
                                    const CBlockIndex &tip,
                                    const Consensus::Params &params) {
    arith_uint256 r;
    int sign = 1;
    if (to.nChainWork > from.nChainWork) {
        r = to.nChainWork - from.nChainWork;
    } else {
        r = from.nChainWork - to.nChainWork;
        sign = -1;
    }
    r = r * arith_uint256(params.nPowTargetSpacing) / GetBlockProof(tip);
    if (r.bits() > 63) {
        return sign * std::numeric_limits<int64_t>::max();
    }
    return sign * r.GetLow64();
}

/**
 * Find the last common ancestor two blocks have.
 * Both pa and pb must be non null.
 */
const CBlockIndex *LastCommonAncestor(const CBlockIndex *pa,
                                      const CBlockIndex *pb) {
    if (pa->nHeight > pb->nHeight) {
        pa = pa->GetAncestor(pb->nHeight);
    } else if (pb->nHeight > pa->nHeight) {
        pb = pb->GetAncestor(pa->nHeight);
    }

    while (pa != pb && pa && pb) {
        pa = pa->pprev;
        pb = pb->pprev;
    }

    // Eventually all chain branches meet at the genesis block.
    assert(pa == pb);
    return pa;
}

std::optional<CBlockUndo> CBlockIndex::GetBlockUndo() const
{
    std::lock_guard lock{ blockIndexMutex };

    std::optional<CBlockUndo> blockUndo{ CBlockUndo{} };
    CDiskBlockPos pos = GetUndoPosNL();

    if (pos.IsNull())
    {
        error("DisconnectBlock(): no undo data available");
        return std::nullopt;
    }

    if (!BlockFileAccess::UndoReadFromDisk(blockUndo.value(), pos, pprev->GetBlockHash()))
    {
        error("DisconnectBlock(): failure reading undo data");
        return std::nullopt;
    }

    return blockUndo;
}


bool CBlockIndex::writeUndoToDisk(CValidationState &state, const CBlockUndo &blockundo,
                            bool fCheckForPruning, const Config &config,
                            std::set<CBlockIndex *, CBlockIndexWorkComparator> &setBlockIndexCandidates)
{
    std::lock_guard lock{ blockIndexMutex };
    if (GetUndoPosNL().IsNull() ||
        !IsValid(BlockValidity::SCRIPTS)) {
        if (GetUndoPosNL().IsNull()) {
            CDiskBlockPos _pos;
            if (!pBlockFileInfoStore->FindUndoPos(
                    state, nFile, _pos,
                    ::GetSerializeSize(blockundo, SER_DISK, CLIENT_VERSION) +
                        40, fCheckForPruning)) {
                return error("CBlockIndex: FindUndoPos failed");
            }

            if (!BlockFileAccess::UndoWriteToDisk(blockundo, _pos, pprev->GetBlockHash(),
                                 config.GetChainParams().DiskMagic())) {
                return AbortNode(state, "Failed to write undo data");
            }

            // update nUndoPos in block index
            nUndoPos = _pos.Pos();
            nStatus = nStatus.withUndo();
        }

        // since we are changing validation time we need to update
        // setBlockIndexCandidates as well - it sorts by that time
        setBlockIndexCandidates.erase(this);

        RaiseValidity(BlockValidity::SCRIPTS);
        setBlockIndexCandidates.insert(this);

        setDirtyBlockIndex.insert(this);
    }

    return true;
}

bool CBlockIndex::verifyUndoValidity()
{
    std::lock_guard lock{ blockIndexMutex };
    CBlockUndo undo;
    CDiskBlockPos pos = GetUndoPosNL();
    if (!pos.IsNull()) {
        if (!BlockFileAccess::UndoReadFromDisk(undo, pos,
                              pprev->GetBlockHash())) {
            return error(
                "VerifyDB(): *** found bad undo data at %d, hash=%s\n",
                nHeight, GetBlockHash().ToString());
        }
    }

    return true;
}

bool CBlockIndex::ReadBlockFromDisk(CBlock &block,
                       const Config &config) const
{
    std::lock_guard lock{ blockIndexMutex };
    if (!BlockFileAccess::ReadBlockFromDisk(block, GetBlockPos(), config))
    {
        return false;
    }

    if (block.GetHash() != GetBlockHash()) {
        return error("ReadBlockFromDisk(CBlock&, CBlockIndex*): GetHash() "
                     "doesn't match index for %s at %s",
                     ToString(), GetBlockPos().ToString());
    }

    return true;
}
