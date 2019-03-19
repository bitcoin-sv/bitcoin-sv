// Copyright (c) 2019 The Bitcoin SV developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "candidates.h"


/**
 * Create a new Mining Candidate. This is then ready for use by the BlockConstructor to construct a Candidate Block.
 * The Mining Candidate is assigned a unique id and is added to the set of candidates.
 *
 * @return a reference to the MiningCandidate.
 */
CMiningCandidateRef CMiningCandidateManager::Create(uint256 hashPrevBlock) {
    std::lock_guard<std::mutex> lock(mutex);
    auto candidate = std::make_shared<CMiningCandidate>(CMiningCandidate(nextId, hashPrevBlock));
    candidates[nextId] = candidate;
    nextId++;
    return candidate;
};

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