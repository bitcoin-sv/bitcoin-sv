// Copyright (c) 2019 The Bitcoin SV developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include "uint256.h"
#include "bloom.h"

#include <shared_mutex>
#include <vector>

class CTxnRecentRejects;
using TxnRecentRejectsSPtr = std::shared_ptr<CTxnRecentRejects>;

/**
 * A class needed to support recent rejects when processing p2p txns.
 */
class CTxnRecentRejects {
  public:
	CTxnRecentRejects();
	~CTxnRecentRejects() = default;

    // Forbid copying/assignment
    CTxnRecentRejects(const CTxnRecentRejects&) = delete;
    CTxnRecentRejects(CTxnRecentRejects&&) = delete;
    CTxnRecentRejects& operator=(const CTxnRecentRejects&) = delete;
    CTxnRecentRejects& operator=(CTxnRecentRejects&&) = delete;

	/** Insert txn into the filter */
    void insert(const uint256& txHash);
    /** Check if a given txn was recently rejected */
    bool isRejected(const uint256& txHash) const;
    /** Reset the underlying filter */
    void reset();

  private:
    /**
     * Filter for transactions that were recently rejected by AcceptToMemoryPool.
     * These are not rerequested until the chain tip changes, at which point the
     * entire filter is reset.
     *
     * Without this filter we'd be re-requesting txs from each of our peers,
     * increasing bandwidth consumption considerably. For instance, with 100 peers,
     * half of which relay a tx we don't accept, that might be a 50x bandwidth
     * increase. A flooding attacker attempting to roll-over the filter using
     * minimum-sized, 60byte, transactions might manage to send 1000/sec if we have
     * fast peers, so we pick 120,000 to give our peers a two minute window to send
     * invs to us.
     *
     * Decreasing the false positive rate is fairly cheap, so we pick one in a
     * million to make it highly unlikely for users to have issues with this filter.
     *
     * Memory used: 1.3 MB
     */
    std::unique_ptr<CRollingBloomFilter> mpRecentRejects {};
    mutable std::shared_mutex mRecentRejectsMtx {};
};
