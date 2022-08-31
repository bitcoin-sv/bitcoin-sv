// Copyright (c) 2022 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#ifndef MINER_ID_TRACKER_H
#define MINER_ID_TRACKER_H

#include "primitives/transaction.h"
#include "logging.h"
#include <mutex>
#include <map>
#include <utility>
#include <functional>

namespace mining {


/**
 * This data structure tracks minerinnfo txns and dataref txns that this node has created.
 * Dataref and minerinfo txns build a funding chain and this class helps finding a spendable fund in this chain.
 * There may be more than one dataref transactions per block and the last one is the corresponding minerinfo txn
 * if it exists.
 * Transaction in blocks and in the mempool are tracked separately and separate mutexes are used for such access.
 */

class DatarefTracker {
    struct FundingNode {
        COutPoint outPoint;
        COutPoint previous;
    };
    struct DatarefVector {
        std::vector<FundingNode> funds;
    };
    struct Key
    {
        int32_t height{-1};
        uint256 blockhash{};
        bool operator== (Key const & other) const noexcept
        {
            return std::tie(height, blockhash) ==
                   std::tie(other.height, other.blockhash);
        }
        bool operator< (Key const & other) const noexcept
        {
            return std::tie(height, blockhash) <
                    std::tie(other.height, other.blockhash);
        }
    };
    int32_t keep_for_reorg{1001};
    std::map<Key,DatarefVector> entries_;
    mutable std::mutex mtx_;

    // Member current_ tracks the mempool until the transaction moves
    // into the block. Note that member current_ uses its own mutex.
    DatarefVector current_;
    mutable std::mutex mtx_for_current_;

    bool store_minerinfo_fund (int32_t height, const uint256& blockhash, const FundingNode& fundingNode, size_t idx);
    bool load_minerinfo_funds ();

public:
    class LockingAccess {
        friend class DatarefTracker;

        std::lock_guard<std::mutex> lock_;
        DatarefTracker &data_;

        explicit LockingAccess(DatarefTracker &data) : lock_{data.mtx_}, data_{data} {
            if (data_.entries_.empty())
                data_.load_minerinfo_funds ();
        }

    public:

        /**
         * Reverse search transaction because the found transaction will most likely be at the end.
         * @height, skip everything at or above height
         * @get_transaction, This function must find the transaction via the txid if it is in the
         * active chain. GetTransaction will not do because this could be a pruning node.
         */
        std::optional<std::pair<COutPoint, COutPoint>> find_fund(int32_t height, std::function<bool(const COutPoint&, const COutPoint&)> get_transaction) const {
            for (auto i = data_.entries_.crbegin(); i != data_.entries_.crend(); ++i) {
                if (i->first.height < height) {
                    for (auto j = i->second.funds.crbegin(); j != i->second.funds.crend(); ++j) {
                        const FundingNode& node = *j;
                        if(get_transaction(node.outPoint, node.previous))
                            return {{node.outPoint, node.previous}};
                    }
                }
            }
            return std::nullopt;
        }

        bool move_current_to_store(int32_t height, uint256 const &blockHash) {
            try {
                // copy temporary txns in "current" to store
                auto key = Key{.height=height,.blockhash=blockHash};
                DatarefVector & entries = data_.entries_[key];
                size_t counter{0};

                std::vector<FundingNode> tmp;
                {
                    std::lock_guard lock(data_.mtx_for_current_);
                    tmp = data_.current_.funds;
                    data_.current_.funds.clear();
                }
                for (const auto& outputs: tmp) {
                    entries.funds.push_back(outputs);
                    if(data_.store_minerinfo_fund (height, blockHash, outputs, counter))
                        ++counter;
                }
                // clear temporary txns in "current" to store

                // Todo: purge old entries.
                size_t to_prune = std::count_if(
                        data_.entries_.begin(),
                        data_.entries_.end(),
                        [height, p=data_.keep_for_reorg](const auto& element) {return element.first.height < height - p;});
                for (size_t i = 0; i < to_prune; ++i)
                    data_.entries_.erase(data_.entries_.begin());
            } catch (const std::exception &e) {
                LogPrintf(strprintf("could not store minerinfo tracking information: %s\n", e.what()));
                return false;
            } catch (...) {
                LogPrintf("could not store minerinfo tracking information\n");
                return false;
            }
            return true;
        }
    };

    auto CreateLockingAccess() {
        return LockingAccess{*this};
    }

    std::optional<COutPoint> get_current_funds_back() const {
        std::lock_guard lock (mtx_for_current_);
        if (!current_.funds.empty())
            return current_.funds.back().outPoint;
        else
            return std::nullopt;
    }

    std::optional<COutPoint> get_current_funds_front() const {
        std::lock_guard lock (mtx_for_current_);
        if (!current_.funds.empty())
            return current_.funds.front().outPoint;
        else
            return std::nullopt;
    }

    std::vector<TxId> get_current_funds() const {
        std::lock_guard lock (mtx_for_current_);
        //return current_.funds;
        std::vector<TxId> v;
        for (const auto& outputs: current_.funds)
            v.push_back(outputs.outPoint.GetTxId());
        return v;
    }

    void append_to_current_funds(const COutPoint& outp, const COutPoint& prev_outp) {
        std::lock_guard lock (mtx_for_current_);
        current_.funds.push_back({outp, prev_outp});
    }

    void clear_current_funds() {
        std::lock_guard lock (mtx_for_current_);
        current_.funds.clear();
    }

    bool pop_back_from_current_funds () {
        std::lock_guard lock (mtx_for_current_);
        if (current_.funds.empty())
            return false;
        current_.funds.pop_back();
        return true;
    }
};

} // namespace mining

#endif // MINER_ID_TRACKER_H
