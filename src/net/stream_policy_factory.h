// Copyright (c) 2020 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include <net/stream_policy.h>

#include <memory>
#include <set>
#include <string>
#include <unordered_map>

/**
 * Construct required stream policies.
 *
 * If new stream policies are created they will need to be registered with
 * this factory by modifying the code in this classes constructor.
 */
class StreamPolicyFactory
{
  public:
    StreamPolicyFactory();

    // Create and return the named stream policy
    std::unique_ptr<StreamPolicy> Make(const std::string& policyName) const;

    // Return list of all/supported stream policy names
    std::set<std::string> GetAllPolicyNames() const;
    std::set<std::string> GetSupportedPolicyNames() const;
    std::string GetAllPolicyNamesStr() const;
    std::string GetSupportedPolicyNamesStr() const;

    // Return a prioritised list of supported stream policy names
    std::vector<std::string> GetPrioritisedPolicyNames() const;

  private:

    // Maker for a policy
    // NOLINTNEXTLINE(cppcoreguidelines-special-member-functions)
    class MakerBase
    {
      public:
        MakerBase() = default;
        virtual ~MakerBase() = default;
        virtual std::unique_ptr<StreamPolicy> operator()() = 0;
    };
    using MakerPtr = std::shared_ptr<MakerBase>;

    template<typename PolicyType>
    class Maker : public MakerBase
    {
      public:
        std::unique_ptr<StreamPolicy> operator()() override
        {
            return std::make_unique<PolicyType>();
        }
    };

    // Register the given stream policy as one we know about
    template<typename PolicyType>
    void registerPolicy()
    {
        std::shared_ptr<Maker<PolicyType>> maker { std::make_shared<Maker<PolicyType>>() };
        mMakers[PolicyType::POLICY_NAME] = std::static_pointer_cast<MakerBase>(maker);
    }

    // Map of policy names to policy makers
    std::unordered_map<std::string, MakerPtr> mMakers {};
};

