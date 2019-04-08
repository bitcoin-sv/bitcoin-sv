// Copyright (c) 2019 The Bitcoin SV developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "candidates.h"

namespace
{
    constexpr unsigned int NEW_CANDIDATE_INTERVAL = 30; // seconds
}

/**
 * Create a new Mining Candidate. This is then ready for use by the BlockConstructor to construct a Candidate Block.
 * The Mining Candidate is assigned a unique id and is added to the set of candidates.
 *
 * @return a reference to the MiningCandidate.
 */
CMiningCandidateRef CMiningCandidateManager::Create(uint256 hashPrevBlock)
{
    // Create UUID for next candidate
    MiningCandidateId nextId { mIdGenerator() };

    auto candidate = std::make_shared<CMiningCandidate>(CMiningCandidate(nextId, hashPrevBlock));
    std::lock_guard<std::mutex> lock(mutex);
    candidates[nextId] = candidate;
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

    std::lock_guard<std::mutex> lock {mutex};
    auto candidateIt { candidates.find(candidateId) };
    if(candidateIt != candidates.end())
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
void CMiningCandidateManager::RemoveOldCandidates() {
    std::lock_guard<std::mutex> lock(mutex);
// old code:
//    LOCK(cs_main);
//    static unsigned int prevheight = 0;
//    unsigned int height = GetBlockchainHeight();
//
//    if (height <= prevheight)
//        return;
//
//    int64_t tdiff = GetTime() - (chainActive.Tip()->nTime + NEW_CANDIDATE_INTERVAL);
//    if (tdiff >= 0)
//    {
//        // Clean out mining candidates that are the same height as a discovered block.
//        for (auto it = MiningCandidates.cbegin(); it != MiningCandidates.cend();)
//        {
//            if (it->second.block.GetHeight() <= prevheight)
//            {
//                it = MiningCandidates.erase(it);
//            }
//            else
//            {
//                ++it;
//            }
//        }
//        prevheight = height;
//    }
}
