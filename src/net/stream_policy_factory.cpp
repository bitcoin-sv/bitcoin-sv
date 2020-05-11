// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include <net/stream_policy_factory.h>

#include <exception>

StreamPolicyFactory::StreamPolicyFactory()
{
    // One-time static registration of all policies we know how to handle.
    // Hopefully we'll never need anything more sophisticated than this, but
    // if we do we'll worry about it then.
    registerPolicy<DefaultStreamPolicy>();
    registerPolicy<BlockPriorityStreamPolicy>();
}

std::unique_ptr<StreamPolicy> StreamPolicyFactory::Make(const std::string& policyName) const
{
    const auto& makerIt { mMakers.find(policyName) };
    if(makerIt == mMakers.end())
    {
        throw std::runtime_error("Unknown stream policy name " + policyName);
    }

    return (*(makerIt->second))();
}

