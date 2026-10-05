// Copyright (c) 2015-2016 The Bitcoin Core developers
// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "merkle.h"

#include "binary_counter.h"
#include "crypto/sha256_dispatch.h"
#include "hash.h"
#include "utilstrencodings.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>

/*     WARNING! If you're reading this because you're learning about crypto
       and/or designing a new system that will use merkle trees, keep in mind
       that the following merkle tree algorithm has a serious flaw related to
       duplicate txids, resulting in a vulnerability (CVE-2012-2459).

       The reason is that if the number of hashes in the list at a given time
       is odd, the last one is duplicated before computing the next level (which
       is unusual in Merkle trees). This results in certain sequences of
       transactions leading to the same merkle root. For example, these two
       trees:

                    A               A
                  /  \            /   \
                B     C         B       C
               / \    |        / \     / \
              D   E   F       D   E   F   F
             / \ / \ / \     / \ / \ / \ / \
             1 2 3 4 5 6     1 2 3 4 5 6 5 6

       for transaction lists [1,2,3,4,5,6] and [1,2,3,4,5,6,5,6] (where 5 and
       6 are repeated) result in the same root hash A (because the hash of both
       of (F) and (F,F) is C).

       The vulnerability results from being able to send a block with such a
       transaction list, with the same merkle root, and the same block hash as
       the original without duplication, resulting in failed validation. If the
       receiving node proceeds to mark that block as permanently invalid
       however, it will fail to accept further unmodified (and thus potentially
       valid) versions of the same block. We defend against this by detecting
       the case where we would hash two identical hashes at the end of the list
       together, and treating that identically to the block having an invalid
       merkle root. Assuming no double-SHA256 collisions, this will detect all
       known ways of changing the transactions without affecting the merkle
       root.
*/

/* This implements a constant-space merkle root/path calculator, limited to 2^32
 * leaves. */
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index)
static void MerkleComputation(const std::vector<uint256> &leaves,
                              uint256 *proot, bool *pmutated,
                              uint32_t branchpos,
                              std::vector<uint256> *pbranch)
{
    if (pbranch) pbranch->clear();
    if (leaves.size() == 0) {
        if (pmutated) *pmutated = false;
        if (proot) *proot = uint256();
        return;
    }
    bool mutated = false;
    // count is the number of leaves processed so far.
    uint32_t count = 0;
    // inner is an array of eagerly computed subtree hashes, indexed by tree
    // level (0 being the leaves).
    // For example, when count is 25 (11001 in binary), inner[4] is the hash of
    // the first 16 leaves, inner[3] of the next 8 leaves, and inner[0] equal to
    // the last leaf. The other inner entries are undefined.
    std::array<uint256, 32> inner;
    // Which position in inner is a hash that depends on the matching leaf.
    int matchlevel = -1;
    // First process all leaves into 'inner' values.
    while (count < leaves.size()) {
        uint256 h = leaves[count];
        bool matchh = count == branchpos;
        count++;
        int level{};
        // For each of the lower bits in count that are 0, do 1 step. Each
        // corresponds to an inner value that existed before processing the
        // current leaf, and each needs a hash to combine it.
        for(; !(count & (((uint32_t)1) << level)); level++) {
            if (pbranch) {
                if (matchh) {
                    pbranch->push_back(inner[level]);
                } else if (matchlevel == level) {
                    pbranch->push_back(h);
                    matchh = true;
                }
            }
            mutated |= (inner[level] == h);
            CHash256()
                .Write(inner[level].begin(), 32)
                .Write(h.begin(), 32)
                .Finalize(CHash256::span{h.begin(), CHash256::OUTPUT_SIZE});
        }
        // Store the resulting hash at inner position level.
        inner[level] = h;
        if (matchh) {
            matchlevel = level;
        }
    }
    // Do a final 'sweep' over the rightmost branch of the tree to process
    // odd levels, and reduce everything to a single top value.
    // Level is the level (counted from the bottom) up to which we've sweeped.
    int level = 0;
    // As long as bit number level in count is zero, skip it. It means there
    // is nothing left at this level.
    while (!(count & (((uint32_t)1) << level))) {
        level++;
    }
    uint256 h = inner[level];
    bool matchh = matchlevel == level;
    while (count != (((uint32_t)1) << level)) {
        // If we reach this point, h is an inner value that is not the top.
        // We combine it with itself (Bitcoin's special rule for odd levels in
        // the tree) to produce a higher level one.
        if (pbranch && matchh) {
            pbranch->push_back(h);
        }
        CHash256()
            .Write(h.begin(), 32)
            .Write(h.begin(), 32)
            .Finalize(CHash256::span{h.begin(), CHash256::OUTPUT_SIZE});
        // Increment count to the value it would have if two entries at this
        // level had existed.
        count += (((uint32_t)1) << level);
        level++;
        // And propagate the result upwards accordingly.
        while (!(count & (((uint32_t)1) << level))) {
            if (pbranch) {
                if (matchh) {
                    pbranch->push_back(inner[level]);
                } else if (matchlevel == level) {
                    pbranch->push_back(h);
                    matchh = true;
                }
            }
            CHash256()
                .Write(inner[level].begin(), 32)
                .Write(h.begin(), 32)
                .Finalize(CHash256::span{h.begin(), CHash256::OUTPUT_SIZE});
            level++;
        }
    }
    // Return result.
    if (pmutated) *pmutated = mutated;
    if (proot) *proot = h;
}
// NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index)

void SHA256D64_2way(std::span<const uint256, 4> ip, std::span<uint256, 2> op)
{
    using sha256_dispatch::hash_bytes;
    using sha256_dispatch::d_pair_bytes;

    // NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast)
    const std::span<const std::uint8_t, 2 * d_pair_bytes> ip_bytes{reinterpret_cast<const std::uint8_t*>(ip.data()),
                                                                                                         2 * d_pair_bytes};
    const std::span<std::uint8_t, 2 * hash_bytes> op_bytes{reinterpret_cast<std::uint8_t*>(op.data()),
                                                                                           2 * hash_bytes};
    // NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast)

    if(sha256_dispatch::d_block_2way)
    {
        sha256_dispatch::d_block_2way(ip_bytes, op_bytes);
    }
    else
    {
        sha256_dispatch::d_block_1way(ip_bytes.first<d_pair_bytes>(), op_bytes.first<hash_bytes>());
        sha256_dispatch::d_block_1way(ip_bytes.last<d_pair_bytes>(),  op_bytes.last<hash_bytes>());
    }
}

namespace
{
    enum class odd_rule : uint8_t { no, yes };

    class MerkleSiblings
    {
        std::array<uint256, 2> siblings_{};
        std::uint8_t level_{};
        odd_rule odd_rule_{odd_rule::no};
        bool mutated_{};

    public:
        MerkleSiblings() = default;
        MerkleSiblings(const uint256& lhs,
                       const uint256& rhs,
                       std::uint8_t level = 0,
                       odd_rule rule = odd_rule::no,
                       bool mutated = false):
            siblings_{lhs, rhs},
            level_{level},
            odd_rule_{rule},
            mutated_{mutated}
        {}

        const uint256& lhs() const { return siblings_[0]; }
        const uint256& rhs() const { return siblings_[1]; }

        std::uint8_t level() const { return level_; }

        bool mutated() const
        {
            return mutated_ || (lhs() == rhs() && odd_rule_ == odd_rule::no);
        }

        MerkleSiblings lift() const
        {
            const uint256 parent{Sha256d(lhs(), rhs())};
            return MerkleSiblings{parent, parent,
                                  static_cast<uint8_t>(level_ + 1),
                                  odd_rule::yes,
                                  mutated()};
        }

        bool operator==(const MerkleSiblings&) const = default;
    };

    MerkleSiblings combine(const MerkleSiblings& a, const MerkleSiblings& b)
    {
        const std::array<uint256, 4> ip{a.lhs(), a.rhs(),
                                        b.lhs(), b.rhs()};
        std::array<uint256, 2> op;
        SHA256D64_2way(ip, op);
        return MerkleSiblings{op[0], op[1],
                              static_cast<uint8_t>(a.level() + 1),
                              odd_rule::no,
                              a.mutated() || b.mutated()};
    }

    MerkleSiblings lift_combine(const MerkleSiblings& a, MerkleSiblings b)
    {
        while(b.level() < a.level())
            b = b.lift();
        return combine(a, b);
    }
}

uint256 ComputeMerkleRoot(const std::vector<uint256>& leaves, bool* mutated)
{
    if(leaves.size() <= 1)
    {
        if(mutated != nullptr)
            *mutated = false;

        return leaves.empty() ? uint256{} : leaves[0];
    }

    using opt_ms = std::optional<MerkleSiblings>;

    const auto combine_opt = [](const opt_ms& a, const opt_ms& b) -> opt_ms 
                               { return combine(*a, *b); };
    binary_counter bc{combine_opt};

    for(size_t i{}; i < leaves.size() / 2; ++i)
    {
        const size_t idx{2 * i};
        bc(opt_ms{MerkleSiblings{leaves[idx], leaves[idx + 1]}});
    }

    if(leaves.size() & 1)
        bc(opt_ms{MerkleSiblings{leaves.back(), leaves.back(), 0, odd_rule::yes}});
    
    const auto lift_combine_opt = [](const opt_ms& a, const opt_ms& b) -> opt_ms
                                    { return lift_combine(*a, *b); };
    const opt_ms result{reduce_counter(bc.begin(), bc.end(),
                                       lift_combine_opt,
                                       opt_ms{})};
    assert(result.has_value());
    if(mutated != nullptr)
        *mutated = result->mutated();

    return Sha256d(result->lhs(), result->rhs());
}

std::vector<uint256> ComputeMerkleBranch(const std::vector<uint256> &leaves,
                                         uint32_t position) {
    std::vector<uint256> ret;
    MerkleComputation(leaves, nullptr, nullptr, position, &ret);
    return ret;
}

uint256 ComputeMerkleRootFromBranch(const uint256 &leaf,
                                    const std::vector<uint256> &vMerkleBranch,
                                    uint32_t nIndex) {
    uint256 hash = leaf;
    for (std::vector<uint256>::const_iterator it = vMerkleBranch.begin();
         it != vMerkleBranch.end(); ++it) {
        if ((*it).IsNull())
        {
            // Duplicated node
            hash = Hash(BEGIN(hash), END(hash), BEGIN(hash), END(hash));
        }
        else if (nIndex & 1) {
            hash = Hash(BEGIN(*it), END(*it), BEGIN(hash), END(hash));
        } else {
            hash = Hash(BEGIN(hash), END(hash), BEGIN(*it), END(*it));
        }
        nIndex >>= 1;
    }
    return hash;
}

uint256 BlockMerkleRoot(const CBlock &block, bool *mutated) {
    std::vector<uint256> leaves;
    leaves.resize(block.vtx.size());
    for (size_t s = 0; s < block.vtx.size(); s++) {
        leaves[s] = block.vtx[s]->GetId();
    }
    return ComputeMerkleRoot(leaves, mutated);
}

std::vector<uint256> BlockMerkleBranch(const CBlock &block, uint32_t position) {
    std::vector<uint256> leaves;
    leaves.resize(block.vtx.size());
    for (size_t s = 0; s < block.vtx.size(); s++) {
        leaves[s] = block.vtx[s]->GetId();
    }
    return ComputeMerkleBranch(leaves, position);
}
