// Copyright (c) 2022 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#ifndef MINER_ID_TRACKER_H
#define MINER_ID_TRACKER_H

#include "coins.h"
#include "logging.h"
#include "primitives/transaction.h"
#include "pubkey.h"
#include <mutex>
#include <map>
#include <utility>
#include <functional>

#include <boost/filesystem.hpp>

namespace mining {

/*
 * Tracks all transactions that this node has created as minerinfo or dataref transactions at some point.
 * This memory is not deleted by reorgs.
 */
class MempoolDatarefTracker;
class BlockDatarefTracker;
bool move_and_store(MempoolDatarefTracker& mempool_tracker, BlockDatarefTracker& block_tracker);

class MempoolDatarefTracker
{
    std::vector<COutPoint> funds_;
    mutable std::mutex mtx_;

public:
    std::optional<COutPoint> funds_back() const;
    std::optional<COutPoint> funds_front() const;
    std::vector<COutPoint> funds() const;
    bool contains(const TxId& txid) const;
    void funds_replace(std::vector<COutPoint>);
    void funds_append(const COutPoint&);
    void funds_clear();
    bool funds_pop_back();
    
    friend bool move_and_store(MempoolDatarefTracker&, class BlockDatarefTracker&);
};

class BlockDatarefTracker
{
    std::vector<COutPoint> funds_;
    mutable std::mutex mtx_;
    mutable std::mutex mtx_minerid_;
    std::optional<CPubKey> minerId_;

public:
    std::optional<std::pair<COutPoint, std::optional<CoinWithScript>>>
    find_fund(int32_t height,
              std::function<std::optional<CoinWithScript>(const COutPoint&)>
                  get_spendable_coin) const;

    void set_current_minerid(const CPubKey& minerId);
    std::optional<CPubKey> get_current_minerid() const;

    friend bool move_and_store(MempoolDatarefTracker&, BlockDatarefTracker&);

    friend std::unique_ptr<BlockDatarefTracker> make_from_dir();
    friend std::unique_ptr<BlockDatarefTracker> make_from_dir(const boost::filesystem::path&);
};

std::unique_ptr<BlockDatarefTracker> make_from_dir();
std::unique_ptr<BlockDatarefTracker> make_from_dir(const boost::filesystem::path&);


} // namespace mining

extern std::unique_ptr<mining::BlockDatarefTracker> g_BlockDatarefTracker;
extern std::unique_ptr<mining::MempoolDatarefTracker> g_MempoolDatarefTracker;

#endif // MINER_ID_TRACKER_H
