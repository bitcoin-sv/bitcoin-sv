// Copyright (c) 2022 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include "primitives/transaction.h"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

/**
 * Class to manage grouping transactions during block validation.
 *
 * Each constructed group of transactions will have no dependencies on any
 * transactions from a different group, and can therefore be validated
 * independently and in parallel.
 */
class TxnGrouper
{
  public:

    TxnGrouper() = default;

    // Track transactions and their original position in the vtx
    struct TxnAndIndex
    {
        TxnAndIndex() = default;
        TxnAndIndex(const CTransactionRef& txn, size_t index)
            : mTxn{txn}, mIndex{index}
        {}

        CTransactionRef mTxn {nullptr};
        size_t mIndex {0};
    };

    // Build dependecies between txns and return groups of dependent txns
    using TxnGroup = std::vector<TxnAndIndex>;
    using UPtrTxnGroup = std::unique_ptr<TxnGroup>;
    std::vector<UPtrTxnGroup> GetGroups(const std::vector<CTransactionRef>& vtx);
    std::vector<UPtrTxnGroup> GetNumGroups(const std::vector<CTransactionRef>& vtx, size_t numGroups, size_t minSize);

  private:

    // A node within the dependency graph.
    // Tracks transactions and their index (if they came from the block),
    // and maintains a list of other transaction IDs that either it depends on
    // or that depend on it.
    class Node
    {
      public:
        Node() = default;
        Node(const CTransactionRef& txn, size_t index)
            : mTxnAndIndex{txn, index}
        {}

        void SetProcessed() { mProcessed = true; }
        bool GetProcessed() const { return mProcessed; }

        // Set transaction ref
        void SetTransaction(const CTransactionRef& txn, size_t index)
        {
            mTxnAndIndex = { txn, index };
        }

        // Add a new dependency of ours
        void AddDependency(const TxId& txid)
        {
            mDependencies.push_back(txid);
        }

        // Get transaction and index
        const TxnAndIndex& GetTransaction() const { return mTxnAndIndex; }

        // Do we contain a transaction from the block?
        bool HasTransaction() const { return mTxnAndIndex.mTxn != nullptr; }

        // Get dependencies
        const std::vector<TxId>& GetAllDependencies() const { return mDependencies; }

      private:

        bool mProcessed {false};

        // If this node is for a transaction in the block, keep the transction ref and
        // its index in the block
        TxnAndIndex mTxnAndIndex {};

        // All dependencies (may contain duplicates)
        std::vector<TxId> mDependencies {};
    };

    // All nodes in the dependency graph
    std::unordered_multimap<TxId, Node> mNodes {};

    void ScanDependencies(const std::vector<CTransactionRef>& vtx);
    void BuildGroupNonRecursive(const TxId& txid, TxnGroup& group);
};

