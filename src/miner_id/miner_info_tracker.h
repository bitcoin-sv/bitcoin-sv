// Copyright (c) 2022 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#ifndef MINER_ID_TRACKER_H
#define MINER_ID_TRACKER_H

#include "primitives/transaction.h"
#include <mutex>
#include <map>

namespace mining {

class MinerInfoTxTracker {

    TxId txid;
    std::map<int32_t, std::map<uint256,TxId>> entries_;
    mutable std::mutex mtx_;
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
        template<typename Func, typename Rettype>
        Rettype find_latest(Func func) const {
            for (auto i = data_.entries_.crbegin(); i != data_.entries_.crend(); ++i) {
                for (auto j = i->second.crbegin(); j != i->second.crend(); ++j) {
                    auto x = func(i->first, j->second); //first=blockhash, second=txid
                    if (x)
                        return x;
                }
            }
            return {};
        }

        bool insert_minerinfo_txid(int32_t height, uint256 const &blockHash, TxId const &txid) {
            data_.entries_[height][blockHash] = txid;
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
