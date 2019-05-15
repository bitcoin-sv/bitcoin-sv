// Copyright (c) 2019 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "candidates.h"
#include "utiltime.h"
#include "validation.h"

namespace
{
    constexpr unsigned int NEW_CANDIDATE_INTERVAL = 30; // seconds
}

/**
 * CMiningCandidate constructor.
 */
CMiningCandidate::CMiningCandidate(MiningCandidateId id, const CBlockRef& block)
    : mId{id}, mBlock{block}
{
    if(!block || block->vtx.empty())
    {
        throw std::runtime_error("Null or empty block in MiningCandidate creation");
    }

    // Copy out fields from block that are unique to this candidate
    mBlockTime = block->nTime;
    mBlockBits = block->nBits;
    mBlockVersion = block->nVersion;
    mBlockCoinbase = block->vtx[0];
}


/**
 * Create a new Mining Candidate. This is then ready for use by the BlockConstructor to construct a Candidate Block.
 * The Mining Candidate is assigned a unique id and is added to the set of candidates.
 *
 * @return a reference to the MiningCandidate.
 */
CMiningCandidateRef CMiningCandidateManager::Create(const CBlockRef& block)
{
    // Create UUID for next candidate
    MiningCandidateId nextId { mIdGenerator() };

    auto candidate = std::make_shared<CMiningCandidate>(CMiningCandidate(nextId, block));
    std::lock_guard<std::mutex> lock {mMutex};
    mCandidates[nextId] = candidate;
    return candidate;
};

/**
 * Lookup and return a reference to the requested MiningCandidate.
 *
 * @return The requested MiningCandidate, or nullptr if not found.
 */
CMiningCandidateRef CMiningCandidateManager::Get(const MiningCandidateId& candidateId) const
{
    CMiningCandidateRef res {nullptr};

    std::lock_guard<std::mutex> lock {mMutex};
    auto candidateIt { mCandidates.find(candidateId) };
    if(candidateIt != mCandidates.end())
    {
        res = candidateIt->second;
    }

    return res;
}

/**
 * Remove old candidate blocks. This frees up space.
 *
 * An old candidate is defined as a candidate from previous blocks when the latest block was found at least
 * 30 seconds ago. In theory, a sequence of new blocks found within 30 seconds of each other would prevent old
 * candidates from being removed but in practice this wont happen.
 */
void CMiningCandidateManager::RemoveOldCandidates()
{
    unsigned int height {0};
    int64_t tdiff {0};

    {
        LOCK(cs_main);
        if(chainActive.Height() < 0)
            return;

        height = static_cast<unsigned int>(chainActive.Height());
        if(height <= mPrevHeight)
            return;

        tdiff = GetTime() - (chainActive.Tip()->nTime + NEW_CANDIDATE_INTERVAL);
    }

    if(tdiff >= 0)
    {
        // Clean out mining candidates that are older than the discovered block.
        std::lock_guard<std::mutex> lock {mMutex};
        for(auto it = mCandidates.cbegin(); it != mCandidates.cend();)
        {
            if(it->second->mBlock->GetHeightFromCoinbase() <= mPrevHeight)
            {
                it = mCandidates.erase(it);
            }
            else
            {
                ++it;
            }
        }
        mPrevHeight = height;
    }
}
