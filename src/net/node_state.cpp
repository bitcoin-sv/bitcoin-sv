// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include <net/node_state.h>

/** Map maintaining per-node state. */
//NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
std::map<NodeId, CNodeStatePtr> mapNodeState {};
std::shared_mutex mapNodeStateMtx {};
//NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

// Fetch node state
CNodeStateRef GetState(NodeId pnode)
{
    // Lock access for reading to the map of node states
    std::shared_lock<std::shared_mutex> lock { mapNodeStateMtx };

    auto it { mapNodeState.find(pnode) };
    if(it == mapNodeState.end()) {
        // Not found, return null
        return {};
    }
    // Return a shared ref to the item in the map, locked appropriately
    return { it->second, it->second->mMtx };
}

