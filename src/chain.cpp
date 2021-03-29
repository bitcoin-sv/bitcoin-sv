// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chain.h"

/**
 * CChain implementation
 */
void CChain::SetTip(CBlockIndex *pindex)
{
    mChainTip = pindex;

    if (pindex == nullptr) {
        vChain.clear();
        return;
    }

    vChain.resize(static_cast<size_t>(pindex->GetHeight() + 1));
    while (pindex && vChain[static_cast<size_t>(pindex->GetHeight())] != pindex) {
        vChain[static_cast<size_t>(pindex->GetHeight())] = pindex;
        pindex = pindex->GetPrev();
    }
}

CBlockLocator CChain::GetLocator(const CBlockIndex *pindex) const {
    int nStep = 1;
    std::vector<uint256> vHave;
    vHave.reserve(32);

    if (!pindex) {
        pindex = Tip();
    }
    while (pindex) {
        vHave.push_back(pindex->GetBlockHash());
        // Stop when we have added the genesis block.
        if (pindex->GetHeight() == 0) {
            break;
        }
        // Exponentially larger steps back, plus the genesis block.
        int32_t nHeight = std::max(pindex->GetHeight() - nStep, 0);
        if (Contains(pindex)) {
            // Use O(1) CChain index if possible.
            pindex = (*this)[nHeight];
        } else {
            // Otherwise, use O(log n) skiplist.
            pindex = pindex->GetAncestor(nHeight);
        }
        if (vHave.size() > 10) {
            nStep *= 2;
        }
    }

    return CBlockLocator(vHave);
}

const CBlockIndex *CChain::FindFork(const CBlockIndex *pindex) const {
    if (pindex == nullptr) {
        return nullptr;
    }
    if (pindex->GetHeight() > Height()) {
        pindex = pindex->GetAncestor(Height());
    }
    while (pindex && !Contains(pindex)) {
        pindex = pindex->GetPrev();
    }
    return pindex;
}

CBlockIndex *CChain::FindEarliestAtLeast(int64_t nTime) const {
    std::vector<CBlockIndex *>::const_iterator lower =
        std::lower_bound(vChain.begin(), vChain.end(), nTime,
                         [](CBlockIndex *pBlock, const int64_t &time) -> bool {
                             return pBlock->GetBlockTimeMax() < time;
                         });
    return (lower == vChain.end() ? nullptr : *lower);
}
