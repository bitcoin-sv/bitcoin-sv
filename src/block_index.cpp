// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "block_index.h"

#include "block_file_access.h"
#include "async_file_reader.h"
#include "blockfileinfostore.h"
#include "blockstreams.h"
#include "config.h"
#include "clientversion.h"
#include "pow.h"
#include "warnings.h"
#include "abort_node.h"

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


/**
 * To avoid having each instance of CBlockIndex having its own mutex,
 * we have global array of mutexes that current instances of CBlockIndex share.
 * We use modificated hash method to achieve uniform distribution across the mutexes and also
 * to make sure each instance of CBlockIndex always uses the same mutex.
 **/
namespace
{
    constexpr uint32_t MUTEX_COUNT = 8;
    std::array<std::mutex, MUTEX_COUNT> blockIndexMutexes;
    // Helper to calculate a byte hash from given integer value of type T.
    template<typename T, std::size_t NumBits = sizeof(T)*CHAR_BIT>
    inline T hash_byte(T v)
    {
        static_assert(NumBits % CHAR_BIT == 0, "NumBits must be a multiple of CHAR_BIT!");
        if constexpr(NumBits>CHAR_BIT)
        {
            return hash_byte<T, NumBits/2>(
                (v >> (NumBits/2))  // top half of bits in value
                ^
                (v & (std::size_t(-1)>>(NumBits/2))) // bottom half of bits in value
            );
        }
        else
        {
            return v;
        }
    }
}
std::mutex& CBlockIndex::GetMutex() const { return blockIndexMutexes[ hash_byte(reinterpret_cast<std::size_t>(this)) % blockIndexMutexes.size() ]; }

CBlockIndex *CBlockIndex::GetAncestor(int32_t height) {

    return const_cast<CBlockIndex*>(static_cast<const CBlockIndex*>(this)->GetAncestor(height));
}

const CBlockIndex *CBlockIndex::GetAncestor(int32_t height) const {
    if (height > nHeight || height < 0) {
        return nullptr;
    }

    const CBlockIndex *pindexWalk = this;
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

void CBlockIndex::BuildSkipNL()
{
    if (pprev) {
        pskip = pprev->GetAncestor(GetSkipHeight(nHeight));
    }
}

arith_uint256 GetBlockProof(const CBlockIndex &block) {
    arith_uint256 bnTarget;
    bool fNegative;
    bool fOverflow;
    bnTarget.SetCompact(block.GetBits(), &fNegative, &fOverflow);
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
    const auto fromChainWork = from.GetChainWork();
    const auto toChainWork = to.GetChainWork();
    if (toChainWork > fromChainWork)
    {
        r = toChainWork - fromChainWork;
    } else {
        r = fromChainWork - toChainWork;
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
    if (pa->GetHeight() > pb->GetHeight()) {
        pa = pa->GetAncestor(pb->GetHeight());
    } else if (pb->GetHeight() > pa->GetHeight()) {
        pb = pb->GetAncestor(pa->GetHeight());
    }

    while (pa != pb && pa && pb) {
        pa = pa->GetPrev();
        pb = pb->GetPrev();
    }

    // Eventually all chain branches meet at the genesis block.
    assert(pa == pb);
    return pa;
}

std::optional<CBlockUndo> CBlockIndex::GetBlockUndo() const
{
    std::lock_guard lock { GetMutex() };

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
                            bool fCheckForPruning, const Config &config, DirtyBlockIndexStore& notifyDirty)
{
    std::lock_guard lock { GetMutex() };
    if (GetUndoPosNL().IsNull() ||
        !IsValidNL(BlockValidity::SCRIPTS)) {
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

        RaiseValidityNL(BlockValidity::SCRIPTS, notifyDirty);
    }

    return true;
}

bool CBlockIndex::verifyUndoValidity() const
{
    std::lock_guard lock { GetMutex() };
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
    std::lock_guard lock { GetMutex() };
    if (!BlockFileAccess::ReadBlockFromDisk(block, GetBlockPosNL(), config))
    {
        return false;
    }

    if (block.GetHash() != GetBlockHash()) {
        return error("ReadBlockFromDisk(CBlock&, CBlockIndex*): GetHash() "
                     "doesn't match index for %s at %s",
                     ToString(), GetBlockPosNL().ToString());
    }

    return true;
}

void CBlockIndex::SetBlockIndexFileMetaDataIfNotSetNL(
    CDiskBlockMetaData metadata, DirtyBlockIndexStore& notifyDirty) const
{
    if (!nStatus.hasDiskBlockMetaData())
    {
        if (!nStatus.hasData())
        {
            LogPrintf("Block index file metadata for block %s will not be set, because disk block data was pruned while processing block.\n", GetBlockHash().ToString());
            return;
        }
        LogPrintf("Setting block index file metadata for block %s\n", GetBlockHash().ToString());
        SetDiskBlockMetaData(std::move(metadata.diskDataHash), metadata.diskDataSize, notifyDirty);
    }
}

void CBlockIndex::SetBlockIndexFileMetaDataIfNotSet(
    CDiskBlockMetaData metadata, DirtyBlockIndexStore& notifyDirty) const
{
    std::lock_guard lock { GetMutex() };
    SetBlockIndexFileMetaDataIfNotSetNL(metadata, notifyDirty);
}

std::unique_ptr<CBlockStreamReader<CFileReader>> CBlockIndex::GetDiskBlockStreamReader(
    bool calculateDiskBlockMetadata) const
{
    std::lock_guard lock { GetMutex() };
    return
        BlockFileAccess::GetDiskBlockStreamReader(
                GetBlockPosNL(),
                calculateDiskBlockMetadata);
}

std::unique_ptr<CBlockStreamReader<CFileReader>> CBlockIndex::GetDiskBlockStreamReader(
    const Config &config, bool calculateDiskBlockMetadata) const
{
    std::lock_guard lock { GetMutex() };
    std::unique_ptr<CBlockStreamReader<CFileReader>> blockStreamReader;
    try
    {
        blockStreamReader = BlockFileAccess::GetDiskBlockStreamReader(GetBlockPosNL(), calculateDiskBlockMetadata);
    }
    catch(const std::exception& e)
    {
        error("GetDiskBlockStreamReader(CBlockIndex*): Deserialize or I/O error - %s at %s",
            e.what(), GetBlockPosNL().ToString());
        return {};
    }

    if (!blockStreamReader)
    {
        return {};
    }

    if (!CheckProofOfWork(blockStreamReader->GetBlockHeader().GetHash(), blockStreamReader->GetBlockHeader().nBits, config))
    {
        error("GetDiskBlockStreamReader(CBlockIndex*): Errors in block header at %s",
            GetBlockPosNL().ToString());
        return {};
    }

    if (blockStreamReader->GetBlockHeader().GetHash() != GetBlockHash())
    {
        error("GetDiskBlockStreamReader(CBlockIndex*): GetHash() doesn't match index for %s at %s",
            ToString(), GetBlockPosNL().ToString());
        return {};
    }

    return blockStreamReader;
}

bool CBlockIndex::PopulateBlockIndexBlockDiskMetaDataNL(
    FILE* file,
    int networkVersion,
    DirtyBlockIndexStore& notifyDirty) const
{
    CBlockStream stream{
        CNonOwningFileReader{file},
        CStreamVersionAndType{SER_DISK, CLIENT_VERSION},
        CStreamVersionAndType{SER_NETWORK, networkVersion}};
    CHash256 hasher;
    uint256 hash;
    size_t size = 0;

    do
    {
        auto chunk = stream.Read(4096);
        hasher.Write(chunk.Begin(), chunk.Size());
        size += chunk.Size();
    } while(!stream.EndOfStream());

    hasher.Finalize(reinterpret_cast<uint8_t*>(&hash));

    SetBlockIndexFileMetaDataIfNotSetNL(CDiskBlockMetaData{hash, size}, notifyDirty);

    if(fseek(file, GetBlockPosNL().Pos(), SEEK_SET) != 0)
    {
        // this should never happen but for some odd reason we aren't
        // able to rewind the file pointer back to the beginning
        return false;
    }

    return true;
}
auto CBlockIndex::StreamBlockFromDisk(
    int networkVersion, DirtyBlockIndexStore& notifyDirty) const
    -> BlockStreamAndMetaData
{
    std::lock_guard lock { GetMutex() };

    UniqueCFile file{ BlockFileAccess::OpenBlockFile(GetBlockPosNL()) };

    if (!file)
    {
        return {}; // could not open a stream
    }

    if (!nStatus.hasDiskBlockMetaData())
    {
        if (!PopulateBlockIndexBlockDiskMetaDataNL(file.get(), networkVersion, notifyDirty))
        {
            return {};
        }
    }

    assert(mDiskBlockMetaData.diskDataSize > 0);
    assert(!mDiskBlockMetaData.diskDataHash.IsNull());

    // We expect that block data on disk is in same format as data sent over the
    // network. If this would change in the future then CBlockStream would need
    // to be used to change the resulting fromat.
    return
        {
            std::make_unique<CFixedSizeStream<CAsyncFileReader>>(
                mDiskBlockMetaData.diskDataSize,
                CAsyncFileReader{std::move(file)}),
            mDiskBlockMetaData
        };
}

std::unique_ptr<CForwardReadonlyStream> CBlockIndex::StreamSyncBlockFromDisk() const
{
    std::lock_guard lock { GetMutex() };
    UniqueCFile file{ BlockFileAccess::OpenBlockFile(GetBlockPosNL()) };

    if (!file)
    {
        return {}; // could not open a stream
    }

    if (nStatus.hasDiskBlockMetaData())
    {
        return
            std::make_unique<CSyncFixedSizeStream<CFileReader>>(
                mDiskBlockMetaData.diskDataSize,
                CFileReader{std::move(file)});
    }

    return
        std::make_unique<CBlockStream<CFileReader>>(
            CFileReader{std::move(file)},
            CStreamVersionAndType{SER_DISK, CLIENT_VERSION},
            CStreamVersionAndType{SER_NETWORK, PROTOCOL_VERSION});
}

std::unique_ptr<CForwardReadonlyStream> CBlockIndex::StreamSyncPartialBlockFromDisk(uint64_t offset, uint64_t length) const
{
    std::lock_guard lock { GetMutex() };
    CDiskBlockPos p = GetBlockPosNL();

    UniqueCFile file{ BlockFileAccess::OpenBlockFile(CDiskBlockPos(p.File(), p.Pos() + offset)) };

    if (!file)
    {
        return {}; // could not open a stream
    }

    return
        std::make_unique<CSyncFixedSizeStream<CFileReader>>(
            length,
            CFileReader{std::move(file)});

}


std::string to_string(const enum BlockValidity& bv)
{
    switch(bv)
    {
    case BlockValidity::UNKNOWN:
        return "unknown";
    case BlockValidity::HEADER:
        return "header";
    case BlockValidity::TREE:
        return "tree";
    case BlockValidity::TRANSACTIONS:
        return "transactions";
    case BlockValidity::CHAIN:
        return "chain";
    case BlockValidity::SCRIPTS:
        return "scripts";
    default:
        return "";
    } 
}
