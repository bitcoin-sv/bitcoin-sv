// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include <net/net.h>
#include <net/stream_policy_factory.h>
#include <util.h>
#include <boost/algorithm/string.hpp>

namespace
{
    // Join a list of strings into a comma separated string
    template <typename List>
    std::string StringFromList(const List& list)
    {
        std::string res {};
        for(const auto& str : list)
        {
            if(res.empty())
            {
                res += str;
            }
            else
            {
                res += "," + str;
            }
        }

        return res;
    }
}

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

std::set<std::string> StreamPolicyFactory::GetAllPolicyNames() const
{
    std::set<std::string> names {};
    for(const auto& maker : mMakers)
    {
        names.insert(maker.first);
    }

    return names;
}

std::set<std::string> StreamPolicyFactory::GetSupportedPolicyNames() const
{
    // Get configured policy list
    std::string configuredPolicyStr { gArgs.GetArg("-multistreampolicies", DEFAULT_STREAM_POLICY_LIST) };
    std::set<std::string> configuredPolicySet {};
    boost::split(configuredPolicySet, configuredPolicyStr, boost::is_any_of(","));

    // Check items in configured list for validity
    std::set<std::string> allPolicies { GetAllPolicyNames() };
    for(auto it = configuredPolicySet.begin(); it != configuredPolicySet.end();)
    {
        if(allPolicies.count(*it) == 0)
        {
            // Remove unsupported policy from configured list
            it = configuredPolicySet.erase(it);
        }
        else
        {
            ++it;
        }
    }

    // Configured list must always contain the Default policy
    if(configuredPolicySet.count(DefaultStreamPolicy::POLICY_NAME) == 0)
    {
        configuredPolicySet.insert(DefaultStreamPolicy::POLICY_NAME);
    }

    return configuredPolicySet;
}

std::vector<std::string> StreamPolicyFactory::GetPrioritisedPolicyNames() const
{
    // Get supported policies
    std::set<std::string> supportedPolicies { GetSupportedPolicyNames() };

    // Get configured prioritised policy list
    std::string configuredPolicyStr { gArgs.GetArg("-multistreampolicies", DEFAULT_STREAM_POLICY_LIST) };
    std::vector<std::string> prioritisedPolicies {};
    boost::split(prioritisedPolicies, configuredPolicyStr, boost::is_any_of(","));

    // Filter configured prioritised list to only include supported policies
    prioritisedPolicies.erase(
        std::remove_if(prioritisedPolicies.begin(), prioritisedPolicies.end(),
            [&supportedPolicies](const std::string& policy) { return (supportedPolicies.count(policy) == 0); }
        ),
        prioritisedPolicies.end()
    );

    return prioritisedPolicies;
}

std::string StreamPolicyFactory::GetAllPolicyNamesStr() const
{
    return StringFromList(GetAllPolicyNames());
}

std::string StreamPolicyFactory::GetSupportedPolicyNamesStr() const
{
    return StringFromList(GetSupportedPolicyNames());
}

