// Copyright (c) 2021 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include "block_index.h"
#include "logging.h"
#include "serialize.h"
#include "tinyformat.h"
#include "uint256.h"

#include <string>

/** Used to marshal pointers into hashes for db storage. */
class CDiskBlockIndex {
public:

    explicit CDiskBlockIndex(CBlockIndex &pindexIn)
        : hashPrev(pindexIn.pprev ? pindexIn.pprev->GetBlockHash() : uint256())
        , blockIndex(pindexIn)
    {}

    ADD_SERIALIZE_METHODS

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        int nVersion = s.GetVersion();
        if (!(s.GetType() & SER_GETHASH)) {
            READWRITE(VARINT(nVersion));
        }

        READWRITE(VARINT(blockIndex.nHeight));
        READWRITE(blockIndex.nStatus);
        READWRITE(VARINT(blockIndex.nTx));
        if (blockIndex.nStatus.hasData() || blockIndex.nStatus.hasUndo()) {
            READWRITE(VARINT(blockIndex.nFile));
        }
        if (blockIndex.nStatus.hasData()) {
            READWRITE(VARINT(blockIndex.nDataPos));
        }
        if (blockIndex.nStatus.hasUndo()) {
            READWRITE(VARINT(blockIndex.nUndoPos));
        }
        if(blockIndex.nStatus.getValidity() == BlockValidity::SCRIPTS)
        {
            blockIndex.mValidationCompletionTime =
                CBlockIndex::SteadyClockTimePoint::min();
        }

        // block header
        READWRITE(blockIndex.nVersion);
        READWRITE(hashPrev);
        READWRITE(blockIndex.hashMerkleRoot);
        READWRITE(blockIndex.nTime);
        READWRITE(blockIndex.nBits);
        READWRITE(blockIndex.nNonce);
        if (blockIndex.nStatus.hasDiskBlockMetaData())
        {
            try {
                READWRITE(blockIndex.mDiskBlockMetaData);
            } catch (const std::ios_base::failure &) {
                blockIndex.nStatus = blockIndex.nStatus.withDiskBlockMetaData(false);
                LogPrintf("Can not read metadata from block %s. Probably upgrading from downgraded version. \n", GetBlockHash().ToString());
            }
        }

        if(blockIndex.nStatus.hasDataForSoftRejection())
        {
            try
            {
                READWRITE(VARINT(blockIndex.nSoftRejected));
            }
            catch (const std::ios_base::failure&)
            {
                // Detect and handle the case when someone has marked a block as soft rejected,
                // then downgraded the executable to version before soft rejected blocks were implemented,
                // then modified this block index so that it was again written to database (nStatus still contains the flag, but value for nSoftRejected is not present),
                // and finally upgraded the executable to version that implements soft rejected blocks.
                // In this case we treat the block as not soft rejected as it was in the downgraded version of executable.
                // NOTE: This does not properly handle all cases, since we could still successfully read value for nSoftRejected
                //       from some garbage that was stored in this place by the downgraded version of executable. For officially
                //       released versions to which downgrading is supported, this should not really happen in practice because care
                //       is taken that new block index data is only appended at the end. Development/test versions, however, do
                //       not have this guarantee.
                //       To avoid this issue in all cases, all blocks should be unmarked as soft rejected before downgrading
                //       back to version that does not implement soft rejected blocks. Note that in downgraded version all blocks
                //       are considered as not being soft rejected anyway so there is no reason not to do that before downgrading.
                blockIndex.nStatus = blockIndex.nStatus.withDataForSoftRejection(false);
                blockIndex.nSoftRejected = -1;
                LogPrintf("Can not read soft rejection status for block %s from database. Probably upgrading from downgraded version.\n", GetBlockHash().ToString());
            }
        }
        else if(ser_action.ForRead())
        {
            // By default the block is not soft rejected so that actual value
            // does not need to be stored for most of the blocks.
            blockIndex.nSoftRejected = -1;
        }

        if(blockIndex.nStatus.hasDataForSoftConsensusFreeze())
        {
            try
            {
                READWRITE(VARINT(blockIndex.mSoftConsensusFreezeForNBlocks));
            }
            catch (std::ios_base::failure&)
            {
                blockIndex.nStatus = blockIndex.nStatus.withDataForSoftConsensusFreeze(false);
                blockIndex.mSoftConsensusFreezeForNBlocks = -1;
                LogPrintf("Can not read soft consensus freeze status for block %s from database. Probably upgrading from downgraded version.\n", GetBlockHash().ToString());
            }
        }
        else if(ser_action.ForRead())
        {
            blockIndex.mSoftConsensusFreezeForNBlocks = -1;
        }
    }

    uint256 GetBlockHash() const {
        CBlockHeader block;
        block.nVersion = blockIndex.nVersion;
        block.hashPrevBlock = hashPrev;
        block.hashMerkleRoot = blockIndex.hashMerkleRoot;
        block.nTime = blockIndex.nTime;
        block.nBits = blockIndex.nBits;
        block.nNonce = blockIndex.nNonce;
        return block.GetHash();
    }

    bool IsGenesis() const { return hashPrev.IsNull(); }
    const uint256& GetHashPrev() const
    {
        assert( !hashPrev.IsNull() );
        return hashPrev;
    }

    std::string ToString() const {
        std::string str = "CDiskBlockIndex(";
        str += blockIndex.ToString();
        str += strprintf("\n                hashBlock=%s, hashPrev=%s)",
                         GetBlockHash().ToString(), hashPrev.ToString());
        return str;
    }

private:
    uint256 hashPrev; // hashPrev is not set only for genesis block
    CBlockIndex& blockIndex;
};

