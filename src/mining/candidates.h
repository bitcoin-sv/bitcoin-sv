// Copyright (c) 2019 The Bitcoin SV developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOINSV_CANDIDATES_H
#define BITCOINSV_CANDIDATES_H

#include "primitives/block.h"

#include <mutex>
#include <optional>

typedef uint64_t MiningCandidateId;


/**
 * A mining candidate is a potential block, it is complete apart from the Proof of Work. A mining candidate always has
 * a previous block.
 *
 * Mining candidates can be instantiated by the CMiningCandidateManager.
 *
 * Each mining candidate has an id which identifies the mining candidate. ID's can be compared for equality but should
 * otherwise be treated as opaque. ID's are unique and not re-used for a particular mining candidate manager.
 */
class CMiningCandidate {
    friend class CMiningCandidateManager;
public:
    CBlockRef GetBlock() const { return block; };
    void SetBlock(const CBlockRef &blockRef) {
        block = blockRef;
    };
    MiningCandidateId GetId() const { return id; };

private:
    CMiningCandidate(MiningCandidateId _id, uint256 hashPrevBlock) : block{std::make_shared<CBlock>()}, id{_id} {
        block->hashPrevBlock = hashPrevBlock;
    };

    CBlockRef block;
    MiningCandidateId id;
};


typedef std::shared_ptr<CMiningCandidate> CMiningCandidateRef;


/**
 * The mining candidate manager owns a collection of mining candidates.
 */
class CMiningCandidateManager {
public:
    std::optional<CMiningCandidateRef> Get(MiningCandidateId candidateId) const { return std::nullopt; };
    CMiningCandidateRef Create(uint256 hashPrevBlock);
    void Remove(MiningCandidateId candidateId) {
        std::lock_guard<std::mutex> lock(mutex);
        candidates.erase(candidateId);
    };
    size_t Size() const {
        std::lock_guard<std::mutex> lock(mutex);
        return candidates.size();
    };

    void RemoveOldCandidates();

private:
    mutable std::mutex mutex;   // we don't expect much concurrency, a simple exclusive mutex is sufficient
    std::map<uint64_t, CMiningCandidateRef> candidates;
    MiningCandidateId nextId {0};
};


#endif //BITCOINSV_CANDIDATES_H
