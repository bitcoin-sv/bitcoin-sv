// Copyright (c) 2020 The Bitcoin SV developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include "primitives/transaction.h"

#include <mutex>
#include <shared_mutex>
#include <unordered_set>

/**
 * A class for tracking TxIds.
 *
 * It is a wrapper over an unordered set which provides mt support.
 */
// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions)
class CTxIdTracker final
{
  public:
    CTxIdTracker() = default;
    // Forbid copying/assignment
    CTxIdTracker(const CTxIdTracker&) = delete;
    CTxIdTracker(CTxIdTracker&&) = delete;
    CTxIdTracker& operator=(const CTxIdTracker&) = delete;
    CTxIdTracker& operator=(CTxIdTracker&&) = delete;

    // Insert
    bool Insert(const TxId& txid) {
        std::unique_lock lock {mMtx};
        return mTxIds.insert(txid).second;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved)
    bool Insert(TxId&& txid) {
        std::unique_lock lock {mMtx};
        return mTxIds.insert(txid).second;
    }
    // Erase
    size_t Erase(const TxId& txid) {
        std::unique_lock lock {mMtx};
        return mTxIds.erase(txid);
    }
    // Clear
    void Clear() {
        std::unique_lock lock {mMtx};
        mTxIds.clear();
    }
    // Contains
    bool Contains(const TxId& txid) const {
        std::shared_lock lock {mMtx};
        return mTxIds.find(txid) != mTxIds.end();
    }
    // Size
    size_t Size() const {
        std::shared_lock lock {mMtx};
        return mTxIds.size();
    }

  private:
    std::unordered_set<TxId, std::hash<TxId>> mTxIds{};
    mutable std::shared_mutex mMtx {};
};

using TxIdTrackerSPtr = std::shared_ptr<CTxIdTracker>;
using TxIdTrackerWPtr = std::weak_ptr<CTxIdTracker>;
