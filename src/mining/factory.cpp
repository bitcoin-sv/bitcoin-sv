// Copyright (c) 2019 Bitcoin Association.
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include <config.h>
#include <mining/factory.h>
#include <mining/journaling_block_assembler.h>
#include <stdexcept>

namespace mining
{

// Get an appropriate block assembler
BlockAssemblerRef CMiningFactory::GetAssembler(const Config& config)
{
    switch(config.GetMiningCandidateBuilder())
    {
        case(CMiningFactory::BlockAssemblerType::LEGACY):
            return std::make_shared<LegacyBlockAssembler>(config);
            break;
        case(CMiningFactory::BlockAssemblerType::JOURNALING):
            static BlockAssemblerRef journalingAssembler { std::make_shared<JournalingBlockAssembler>(config) };
            return journalingAssembler;
            break;
        default:
            break;
    }

    throw std::runtime_error("Unsupported BlockAssemblerType");
}

// Get a reference to the mining candidate manager
CMiningCandidateManager& CMiningFactory::GetCandidateManager()
{
    static CMiningCandidateManager manager {};
    return manager;
}

// Enable enum_cast for BlockAssemblerType
const enumTableT<CMiningFactory::BlockAssemblerType>& enumTable(CMiningFactory::BlockAssemblerType)
{   
    static enumTableT<CMiningFactory::BlockAssemblerType> table
    {   
        { CMiningFactory::BlockAssemblerType::UNKNOWN,    "UNKNOWN" },
        { CMiningFactory::BlockAssemblerType::LEGACY,     "LEGACY" },
        { CMiningFactory::BlockAssemblerType::JOURNALING, "JOURNALING" }
    };
    return table;
}

}
