// Copyright (c) 2022 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#ifndef MINER_ID_TRACKER_H
#define MINER_ID_TRACKER_H

#include "primitives/transaction.h"
#include <mutex>
#include <map>
#include <functional>

namespace mining {


/**
 * This data structure trackes minerinfo-txns that this node has created. The idea of this structure is
 * a stack that grows when blocks are added and shrinks when blocks are removed (during a reorg for e.g.)
 * But we cannot add the minerinfo-txn to the tracker when adding a block during a reorg because at that point
 * the node cannot tell appart the transactions it created and transactions comming from other nodes.
 * Consequently we have to store all abandoned chains too and cannot use a stack.
 * The storage key for our minerinfo-txns are pairs of height and blockhash sorted in that order.
 * We store all minerinfo-txns by height and blockhash, the second part of the key serving disambiguation.
 */

class MinerInfoTxTracker {
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
    // This data is persistent
    std::map<Key,TxId> entries_;
    mutable std::mutex mtx_;

    // This member tracks the mempool until the transaction moves
    // into the block. Note that member current_ uses its own mutex.
    std::optional<TxId> current_;
    mutable std::mutex mtx_for_current_;

    bool store_minerinfo_txid (int32_t height, uint256 const &blockHash, TxId const &txid);
    bool load_minerinfo_txid ();

public:
    class LockingAccess {
        friend class MinerInfoTxTracker;

        std::lock_guard<std::mutex> lock_;
        MinerInfoTxTracker &data_;

        explicit LockingAccess(MinerInfoTxTracker &data) : lock_{data.mtx_}, data_{data} {
            if (data_.entries_.empty())
                data_.load_minerinfo_txid ();
        }

    public:

        /**
         * Reverse search transaction because the found transaction will most likely be at the end.
         * @height, we are searching for a previous transaction in the funding chain, hence one that
         * is below height.
         * @get_transaction, This function must find the transaction via the txid if it is in the
         * active chain.
         */
        CTransactionRef find_previous(int32_t height, std::function<CTransactionRef(TxId)> get_transaction) const {
            for (auto i = data_.entries_.crbegin(); i != data_.entries_.crend(); ++i)
            {
                if (i->first.height < height)
                {
                    CTransactionRef tx = get_transaction(i->second);
                    if (tx)
                        return tx;
                }
            }
            return nullptr;
        }

        bool insert_minerinfo_txid(int32_t height, uint256 const &blockHash, TxId const &txid) {
            auto key = Key{.height=height,.blockhash=blockHash};
            data_.entries_[key] = txid;
            return data_.store_minerinfo_txid (height, blockHash, txid);
        }
    };

    auto CreateLockingAccess() {
        return LockingAccess{*this};
    }

    std::optional<TxId> current_txid() const {
        std::lock_guard lock (mtx_for_current_);
        return current_;
    }

    void set_current_txid(TxId const &txid) {
        std::lock_guard lock (mtx_for_current_);
        current_ = txid;
    }

    void clear_current_txid() {
        std::lock_guard lock (mtx_for_current_);
        current_ = std::nullopt;
    }
};

} // namespace mining

#endif // MINER_ID_TRACKER_H
