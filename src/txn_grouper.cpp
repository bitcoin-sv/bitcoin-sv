// Copyright (c) 2022 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "txn_grouper.h"

#include <algorithm>
#include <list>

// Walk the dependency graph building groups of related transactions
auto TxnGrouper::GetGroups(const std::vector<CTransactionRef>& vtx) -> std::vector<UPtrTxnGroup>
{
    // Clear state
    mNodes.clear();

    // Build dependency graph
    ScanDependencies(vtx);

    std::vector<UPtrTxnGroup> groups {};
    
    // Build groups starting from the 1st transaction in the block
    for(const auto& txn : vtx)
    {
        // Build group
        TxnGroup internalGroup {};
        BuildGroupNonRecursive(txn->GetId(), internalGroup);
        if(! internalGroup.empty())
        {
            // Sort group to keep txns in the same order from the block
            if(internalGroup.size() > 1)
            {
                // FIXME: Consider using parallel sort when it becomes available
                std::sort(internalGroup.begin(), internalGroup.end(),
                    [](const TxnAndIndex& i1, const TxnAndIndex& i2)
                    {
                        return i1.mIndex < i2.mIndex;
                    }
                );
            }

            groups.emplace_back(std::make_unique<TxnGroup>(std::move(internalGroup)));
        }
    }

    return groups;
}
 
// Build at most the requested number of groups of transactions while
// ensuring each group is at least the given minimum size.
auto TxnGrouper::GetNumGroups(const std::vector<CTransactionRef>& vtx, size_t numGroups, size_t minSize) -> std::vector<UPtrTxnGroup>
{
    if(numGroups == 0)
    {
        return {};
    }

    // Build all independent groups
    const auto& allGroups { GetGroups(vtx) };
    if(allGroups.empty())
    {
        return {};
    }

    // Create list of groups of the right size. This will be maintained as a min-heap
    // with the smallest group always at the top.
    std::vector<UPtrTxnGroup> groups {};
    for(size_t i = 0; i < numGroups; ++i)
    {
        groups.emplace_back(std::make_unique<TxnGroup>());
    }

    // Lambda to sort groups from largest to smallest
    auto Sort = [](const UPtrTxnGroup& group1, const UPtrTxnGroup& group2)
    {
        // Greater than because we want largest to smallest
        return group1->size() > group2->size();
    };

    // Combine into groups of roughly the same size
    for(const auto& member : allGroups)
    {
        // Add contents of this group to the current smallest group
        const auto& smallest { groups.front() };
        smallest->insert(smallest->end(), std::make_move_iterator(member->begin()), std::make_move_iterator(member->end()));

        // Re-heap to keep smallest group at the top
        std::make_heap(groups.begin(), groups.end(), Sort);
    }

    // Combine any smallest groups below the minimum size
    do
    {
        std::sort(groups.begin(), groups.end(), Sort);
        if(groups.size() == 1 || groups.back()->size() >= minSize)
        {
            break;
        }

        const auto& last { groups[groups.size() - 1] };
        const auto& secondLast { groups[groups.size() - 2] };
        secondLast->insert(secondLast->end(), std::make_move_iterator(last->begin()), std::make_move_iterator(last->end()));
        groups.pop_back();
    } while(true);

    return groups;
}

void TxnGrouper::ScanDependencies(const std::vector<CTransactionRef>& vtx)
{
    // Scan each transaction in turn
    for(size_t index = 0; index < vtx.size(); ++index)
    {
        const auto& txn { vtx[index] };
        const auto& txid { txn->GetId() };

        // Add or update node in graph for this transaction
        auto txnNode { mNodes.find(txid) };
        if(txnNode != mNodes.end())
        {
            if(txnNode->second.HasTransaction())
            {
                // This must be a duplicate transaction within the block
                txnNode = mNodes.emplace(txid, Node{txn, index});
            }
            else
            {
                // Tie this node to a real transaction.
                // NOTE: The only circumstance I can think where this would happen
                // is if a previous transaction had a dependency to this transaction; ie; the 2
                // transactions appear out of order in a block.
                txnNode->second.SetTransaction(txn, index);
            }
        }
        else
        {
            txnNode = mNodes.emplace(txid, Node{txn, index});
        }

        // Scan inputs for this transaction
        for(const auto& input : txn->vin)
        {
            const auto& inputId { input.prevout.GetTxId() };

            // Is this input a dependency we already know about?
            const auto& range { mNodes.equal_range(inputId) };
            if(range.first == mNodes.end())
            {
                // Add input as a node in the graph without a contained transaction
                auto inputNode { mNodes.emplace(inputId, Node{}) };
                // Update dependency node to say we depend on it
                inputNode->second.AddDependency(txid);
            }
            else
            {
                // Update all existing nodes to say we depend on them
                for(auto inputNode = range.first; inputNode != range.second; ++inputNode)
                {
                    inputNode->second.AddDependency(txid);
                }
            }

            // Update our node to add this as a dependency of ours
            txnNode->second.AddDependency(inputId);
        }
    }
}

void TxnGrouper::BuildGroupNonRecursive(const TxId& initialTxid, TxnGroup& group)
{
    // Start list of dependencies to scan
    std::list<TxId> dependencies { initialTxid };

    // Keep scanning all the while we have more dependencies
    while(! dependencies.empty())
    {
        // Lookup node for next dependency
        const TxId& txid { dependencies.front() };
        const auto& range { mNodes.equal_range(txid) };
        for(auto txnNode = range.first; txnNode != range.second; ++txnNode)
        {
            if(txnNode != mNodes.end() && !txnNode->second.GetProcessed())
            {
                // Ensure we don't visit any node more than once
                txnNode->second.SetProcessed();

                // If this node represents a txn from the block, add it to the group
                if(txnNode->second.HasTransaction())
                {
                    group.push_back(txnNode->second.GetTransaction());
                }

                // Add all additional dependencies from this node to the scan list
                const auto& nodeDependencies { txnNode->second.GetAllDependencies() };
                dependencies.insert(dependencies.end(), nodeDependencies.begin(), nodeDependencies.end());
            }
        }

        // Finished processing this dependency
        dependencies.pop_front();
    }
}

